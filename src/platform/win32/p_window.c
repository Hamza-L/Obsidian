#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>

#include "o_log.h"
#include "o_window.h"

#include <stdlib.h>
#include <string.h>

#define WINDOW_CLASS_NAME L"ObsidianWindow"
#define RESIZE_BORDER 8

static struct {
    HINSTANCE hinstance;
    bool initialized;
    uint32_t window_count;
    OWindow *windows;
} g_app;

struct OWindow {
    HWND hwnd;
    OWindowCallbacks callbacks;
    void *user_data;
    bool resizable_undecorated;

    float last_mouse_x, last_mouse_y;
    bool has_last_mouse;
    uint16_t pending_high_surrogate;

    OWindow *next;
};

static void to_wide(const char *utf8, wchar_t *out, int cap) {
    if (!utf8 || MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, cap) <= 0) out[0] = 0;
}

static OModKeys get_modifiers(void) {
    OModKeys mods = 0;
    if (GetKeyState(VK_SHIFT) & 0x8000) mods |= O_MOD_SHIFT;
    if (GetKeyState(VK_CONTROL) & 0x8000) mods |= O_MOD_CONTROL;
    if (GetKeyState(VK_MENU) & 0x8000) mods |= O_MOD_ALT;
    if ((GetKeyState(VK_LWIN) | GetKeyState(VK_RWIN)) & 0x8000) mods |= O_MOD_SUPER;
    if (GetKeyState(VK_CAPITAL) & 0x0001) mods |= O_MOD_CAPS_LOCK;
    return mods;
}

static OKey map_key(WPARAM vk) {
    if (vk >= 'A' && vk <= 'Z') return (OKey)(O_KEY_A + (vk - 'A'));
    if (vk >= '0' && vk <= '9') return (OKey)(O_KEY_0 + (vk - '0'));
    switch (vk) {
    case VK_ESCAPE: return O_KEY_ESCAPE;
    case VK_RETURN: return O_KEY_ENTER;
    case VK_TAB: return O_KEY_TAB;
    case VK_BACK: return O_KEY_BACKSPACE;
    case VK_SPACE: return O_KEY_SPACE;
    case VK_LEFT: return O_KEY_LEFT;
    case VK_RIGHT: return O_KEY_RIGHT;
    case VK_UP: return O_KEY_UP;
    case VK_DOWN: return O_KEY_DOWN;
    default: return O_KEY_UNKNOWN;
    }
}

static OMouseButton map_mouse_button(UINT msg) {
    switch (msg) {
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: return O_MOUSE_BUTTON_LEFT;
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: return O_MOUSE_BUTTON_RIGHT;
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: return O_MOUSE_BUTTON_MIDDLE;
    default: return O_MOUSE_BUTTON_OTHER;
    }
}

static OKeyEvent make_key_event(WPARAM wparam, LPARAM lparam, bool down) {
    return (OKeyEvent){
        .key = map_key(wparam),
        .modifiers = get_modifiers(),
        .native_key_code = (uint16_t)((lparam >> 16) & 0xFF),
        .is_repeat = down && (lparam & (1 << 30)),
    };
}

static OMouseButtonEvent make_mouse_button_event(UINT msg, LPARAM lparam) {
    return (OMouseButtonEvent){
        .button = map_mouse_button(msg),
        .modifiers = get_modifiers(),
        .x = (float)GET_X_LPARAM(lparam),
        .y = (float)GET_Y_LPARAM(lparam),
    };
}

static LRESULT hit_test_resize_border(HWND hwnd, LPARAM lparam) {
    static const LRESULT hits[3][3] = {
        {HTTOPLEFT, HTTOP, HTTOPRIGHT},
        {HTLEFT, HTCLIENT, HTRIGHT},
        {HTBOTTOMLEFT, HTBOTTOM, HTBOTTOMRIGHT},
    };
    POINT p = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
    RECT r;
    ScreenToClient(hwnd, &p);
    GetClientRect(hwnd, &r);
    const int row = p.y < RESIZE_BORDER ? 0 : p.y >= r.bottom - RESIZE_BORDER ? 2 : 1;
    const int col = p.x < RESIZE_BORDER ? 0 : p.x >= r.right - RESIZE_BORDER ? 2 : 1;
    return hits[row][col];
}

static void set_content_size(HWND hwnd, int32_t width, int32_t height) {
    RECT r = {0, 0, width, height};
    const DWORD style = (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE);
    const DWORD ex_style = (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    AdjustWindowRectExForDpi(&r, style, FALSE, ex_style, GetDpiForWindow(hwnd));
    SetWindowPos(hwnd, NULL, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
}

static void emit_text_input(OWindow *w, uint16_t c) {
    if (c >= 0xD800 && c <= 0xDBFF) {
        w->pending_high_surrogate = c;
        return;
    }
    uint32_t cp = c;
    if (c >= 0xDC00 && c <= 0xDFFF && w->pending_high_surrogate) {
        cp = 0x10000u + (((uint32_t)w->pending_high_surrogate - 0xD800u) << 10) + (c - 0xDC00u);
    }
    w->pending_high_surrogate = 0;
    if (cp < 0x20 || cp == 0x7F) return;
    if (w->callbacks.on_text_input) w->callbacks.on_text_input(w, cp, w->user_data);
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        OWindow *w = ((CREATESTRUCTW *)lparam)->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)w);
        w->hwnd = hwnd;
        g_app.window_count++;
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    OWindow *w = (OWindow *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!w) return DefWindowProcW(hwnd, msg, wparam, lparam);

    switch (msg) {
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        g_app.window_count--;
        if (w->callbacks.on_close) w->callbacks.on_close(w, w->user_data);
        return 0;

    case WM_NCDESTROY:
        w->hwnd = NULL;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;

    case WM_NCHITTEST:
        if (!w->resizable_undecorated) break;
        return hit_test_resize_border(hwnd, lparam);

    case WM_SIZE:
        if (w->callbacks.on_resize) w->callbacks.on_resize(w, LOWORD(lparam), HIWORD(lparam), w->user_data);
        if (w->callbacks.on_refresh) w->callbacks.on_refresh(w, w->user_data);
        return 0;

    case WM_MOVE:
        if (w->callbacks.on_move) w->callbacks.on_move(w, (int16_t)LOWORD(lparam), (int16_t)HIWORD(lparam), w->user_data);
        return 0;

    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (w->callbacks.on_key_down) w->callbacks.on_key_down(w, make_key_event(wparam, lparam, true), w->user_data);
        return 0;

    case WM_KEYUP:
    case WM_SYSKEYUP:
        if (w->callbacks.on_key_up) w->callbacks.on_key_up(w, make_key_event(wparam, lparam, false), w->user_data);
        return 0;

    case WM_CHAR:
        emit_text_input(w, (uint16_t)wparam);
        return 0;

    case WM_MOUSEMOVE: {
        const float x = (float)GET_X_LPARAM(lparam);
        const float y = (float)GET_Y_LPARAM(lparam);
        OMouseMoveEvent e = {
            .x = x,
            .y = y,
            .delta_x = w->has_last_mouse ? x - w->last_mouse_x : 0.0f,
            .delta_y = w->has_last_mouse ? y - w->last_mouse_y : 0.0f,
        };
        w->last_mouse_x = x;
        w->last_mouse_y = y;
        w->has_last_mouse = true;
        if (w->callbacks.on_mouse_move) w->callbacks.on_mouse_move(w, e, w->user_data);
        return 0;
    }

    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN:
        if (w->callbacks.on_mouse_down) w->callbacks.on_mouse_down(w, make_mouse_button_event(msg, lparam), w->user_data);
        return msg == WM_XBUTTONDOWN;

    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_XBUTTONUP:
        if (w->callbacks.on_mouse_up) w->callbacks.on_mouse_up(w, make_mouse_button_event(msg, lparam), w->user_data);
        return msg == WM_XBUTTONUP;

    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: {
        const float notches = (float)GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
        OMouseScrollEvent e = {
            .delta_x = msg == WM_MOUSEHWHEEL ? notches : 0.0f,
            .delta_y = msg == WM_MOUSEWHEEL ? notches : 0.0f,
        };
        if (w->callbacks.on_mouse_scroll) w->callbacks.on_mouse_scroll(w, e, w->user_data);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static bool app_init(void) {
    if (g_app.initialized) return true;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g_app.hinstance = GetModuleHandleW(NULL);

    WNDCLASSEXW wc = {
        .cbSize = sizeof wc,
        .style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC,
        .lpfnWndProc = wndproc,
        .hInstance = g_app.hinstance,
        .hCursor = LoadCursorW(NULL, IDC_ARROW),
        .lpszClassName = WINDOW_CLASS_NAME,
    };
    g_app.initialized = RegisterClassExW(&wc) != 0;
    if (!g_app.initialized) LOG_ERROR("RegisterClassExW failed: %lu", GetLastError());
    return g_app.initialized;
}

bool window_poll_events(void) {
    if (!g_app.initialized) return false;

    MSG msg;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    for (OWindow *w = g_app.windows; w; w = w->next) {
        if (w->hwnd && w->callbacks.on_refresh) w->callbacks.on_refresh(w, w->user_data);
    }
    return g_app.window_count > 0;
}

OWindow *window_create(const OWindowDesc *desc) {
    if (!desc || desc->width <= 0 || desc->height <= 0) {
        LOG_ERROR("window_create: invalid desc");
        return NULL;
    }
    if (!app_init()) return NULL;

    OWindow *window = calloc(1, sizeof *window);
    if (!window) {
        LOG_ERROR("window_create: out of memory");
        return NULL;
    }
    window->resizable_undecorated = desc->undecorated && desc->resizable;

    DWORD style = WS_POPUP;
    if (!desc->undecorated) style |= WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    if (desc->resizable) style |= WS_THICKFRAME;

    wchar_t title[256];
    to_wide(desc->title, title, 256);

    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, WINDOW_CLASS_NAME, title, style, desc->x, desc->y,
                                desc->width, desc->height, NULL, NULL, g_app.hinstance, window);
    if (!hwnd) {
        LOG_ERROR("CreateWindowExW failed: %lu", GetLastError());
        free(window);
        return NULL;
    }
    set_content_size(hwnd, desc->width, desc->height);

    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);
    if (desc->undecorated) {
        MARGINS margins = {-1, -1, -1, -1};
        DwmExtendFrameIntoClientArea(hwnd, &margins);
    }
    if (desc->transparent) {
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    }

    window->next = g_app.windows;
    g_app.windows = window;
    return window;
}

void window_destroy(OWindow *window) {
    if (!window) return;

    memset(&window->callbacks, 0, sizeof window->callbacks);
    window_close(window);

    for (OWindow **pp = &g_app.windows; *pp; pp = &(*pp)->next) {
        if (*pp == window) {
            *pp = window->next;
            break;
        }
    }
    free(window);
}

void window_show(OWindow *window) {
    if (window && window->hwnd) ShowWindow(window->hwnd, SW_SHOW);
}

void window_hide(OWindow *window) {
    if (window && window->hwnd) ShowWindow(window->hwnd, SW_HIDE);
}

void window_close(OWindow *window) {
    if (window && window->hwnd) DestroyWindow(window->hwnd);
}

void window_set_title(OWindow *window, const char *title) {
    if (!window || !window->hwnd || !title) return;
    wchar_t wide[256];
    to_wide(title, wide, 256);
    SetWindowTextW(window->hwnd, wide);
}

void window_set_position(OWindow *window, int32_t x, int32_t y) {
    if (window && window->hwnd) SetWindowPos(window->hwnd, NULL, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}

void window_set_size(OWindow *window, int32_t width, int32_t height) {
    if (window && window->hwnd && width > 0 && height > 0) set_content_size(window->hwnd, width, height);
}

void window_set_callbacks(OWindow *window, const OWindowCallbacks *callbacks, void *user_data) {
    if (!window) return;
    if (callbacks) {
        window->callbacks = *callbacks;
    } else {
        memset(&window->callbacks, 0, sizeof window->callbacks);
    }
    window->user_data = user_data;
}

bool window_get_content_size(const OWindow *window, int32_t *out_width, int32_t *out_height) {
    RECT r;
    if (!window || !window->hwnd || !GetClientRect(window->hwnd, &r)) return false;
    if (out_width) *out_width = r.right - r.left;
    if (out_height) *out_height = r.bottom - r.top;
    return true;
}

bool window_get_pixel_size(const OWindow *window, int32_t *out_width, int32_t *out_height) {
    return window_get_content_size(window, out_width, out_height);
}

bool window_is_open(const OWindow *window) {
    return window && window->hwnd;
}

void *window_native_handle(const OWindow *window) {
    return window ? window->hwnd : NULL;
}
