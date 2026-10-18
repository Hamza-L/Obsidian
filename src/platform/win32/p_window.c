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
#define MAX_TITLE_LEN 64
#define TITLEBAR_HEIGHT 28

static struct {
    HINSTANCE hinstance;
    bool initialized;
    uint32_t window_count;
    OWindow *windows;
} g_app;

typedef struct {
    const char title[MAX_TITLE_LEN];
    uint32_t height;
} OTitleBar;

struct OWindow {
    HWND hwnd;
    OWindowCallbacks callbacks;
    OTitleBar titlebar;
    bool open;

    void *userdata;
    OWindow *next;
};

static void to_wide(const char *utf8, wchar_t *out, int cap) {
    if (!utf8 || MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out, cap) <= 0) out[0] = 0;
}

LRESULT hit_test_resize_border(HWND hwnd, LPARAM lparam) {
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

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    OWindow *w = (OWindow *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_NCCREATE: {
        OWindow *w = ((CREATESTRUCTW *)lparam)->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)w);
        if(w) w->hwnd = hwnd;
        g_app.window_count++;
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_PAINT:
        return DefWindowProcW(hwnd, msg, wparam, lparam);

    case WM_DESTROY:
        if (!w) return 0;
        w->open = false;
        g_app.window_count--;
        if (w->callbacks.on_close) {
            w->callbacks.on_close(w, w->userdata);
        }
        return 0;

    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)0);
        return DefWindowProcW(hwnd, msg, wparam, lparam);

    case WM_SIZE:
        if (!w) return 0;
        if (w->callbacks.on_resize) w->callbacks.on_resize(w, LOWORD(lparam), HIWORD(lparam), w->userdata);
        if (w->callbacks.on_refresh) w->callbacks.on_refresh(w, w->userdata);
        return 0;

    case WM_MOVE:
        if (!w) return 0;
        if (w->callbacks.on_move) w->callbacks.on_move(w, (int16_t)LOWORD(lparam), (int16_t)HIWORD(lparam), w->userdata);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
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
        .hbrBackground = NULL,
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
        if (w->hwnd && w->callbacks.on_refresh) w->callbacks.on_refresh(w, w->userdata);
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

    window->open = true;

    memcpy((void*)window->titlebar.title, desc->title, min(strlen(desc->title), MAX_TITLE_LEN));
    window->titlebar.height = TITLEBAR_HEIGHT;

    DWORD style = WS_POPUP;
    if (!desc->undecorated) {
        style |= WS_CAPTION | WS_OVERLAPPED | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME;
    }

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

    // NOTE(Hamza): doubt this is needed
    // if (desc->transparent) {
    //     SetWindowLongPtrW(hwnd, GWL_EXSTYLE, GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
    //     SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    // }

    window->next = g_app.windows;
    g_app.windows = window;

    window_show(window);
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
    memcpy((void*)window->titlebar.title, title, MAX_TITLE_LEN);

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

void window_set_callbacks(OWindow *window, const OWindowCallbacks *callbacks, void *userdata) {
    if (!window) return;
    if (callbacks) {
        window->callbacks = *callbacks;
    } else {
        memset(&window->callbacks, 0, sizeof window->callbacks);
    }
    window->userdata = userdata;
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
