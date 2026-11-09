#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>

#include "o_log.h"
#include "o_window.h"
#include "core/o_typedefs.h"
#include "memory/o_memory_internal.h"

#include <stdlib.h>
#include <string.h>

#define WINDOW_CLASS_NAME L"ObsidianWindow"
#define RESIZE_BORDER 8
#define MAX_TITLE_LEN 64
#define TITLEBAR_HEIGHT 28

static i32 g_xOffset;
static i32 g_yOffset;

static struct {
    HINSTANCE hinstance;
    bool initialized;
    OWindow *windows;
    u32 window_count;
} g_app;

typedef struct {
    const char title[MAX_TITLE_LEN];
    u32 height;
} OTitleBar;

struct OWindow {
    HWND hwnd;
    OWindowCallbacks callbacks;
    OTitleBar titlebar;
    bool open;

    struct {
        void *data;
        i32 width;
        i32 height;
        HDC dc;
        BITMAPINFO info;
    } bitmap;

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

static void set_content_size(HWND hwnd, i32 width, i32 height) {
    RECT r = {0, 0, width, height};
    const DWORD style = (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE);
    const DWORD ex_style = (DWORD)GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    AdjustWindowRectExForDpi(&r, style, FALSE, ex_style, GetDpiForWindow(hwnd));
    SetWindowPos(hwnd, NULL, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
}

// 32-bit BI_RGB pixels are 0xAARRGGBB, i.e. bytes B, G, R, A in memory (same as B8G8R8A8).
static u32 pack_bgra(u8 r, u8 g, u8 b, u8 a) {
    return ((u32)a << 24) | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}

static void color_bitmap(void *data, i32 width, i32 height, i32 offsetX, i32 offsetY) {
    u32 *pixel = data;
    for (i32 i = 0; i < height; ++i) {
        for (i32 j = 0; j < width; ++j) {
            u8 r = (u8)(j + offsetX);
            u8 g = (u8)(255 - (255 * (i + j)) / (height + width));
            u8 b = (u8)(i + offsetY);
            *pixel++ = pack_bgra(r, g, b, 255);
        }
    }
}

static void window_release_bitmap(OWindow *w) {
    if (w->bitmap.data) m_release(w->bitmap.data, (size_t)w->bitmap.width * w->bitmap.height * 4);
    w->bitmap.data = NULL;
    w->bitmap.width = 0;
    w->bitmap.height = 0;
}

static void window_create_bitmap(OWindow* w, i32 width, i32 height) {
    window_release_bitmap(w);
    if (width <= 0 || height <= 0) return;

    BITMAPINFO bmapInfo = {0};
    bmapInfo.bmiHeader.biSize = sizeof(bmapInfo.bmiHeader);
    bmapInfo.bmiHeader.biWidth = width;
    bmapInfo.bmiHeader.biHeight = -height; //negative to make bitmap use top-left is 0,0
    bmapInfo.bmiHeader.biPlanes = 1; //default to 1
    bmapInfo.bmiHeader.biBitCount = 32;
    bmapInfo.bmiHeader.biCompression = BI_RGB;

    if(!w->bitmap.dc) w->bitmap.dc = CreateCompatibleDC(0);

    w->bitmap.info = bmapInfo;
    w->bitmap.width = width;
    w->bitmap.height = height;

    // NOTE(casey) Thank you to Chris Hecker of Spy Party Fame
    // for clarifying the deal with StretchDIBits and BitBlt!
    // no more DC for us

    size_t dataSize = width * height * sizeof(u8) * 4;
    w->bitmap.data = m_reserve(dataSize);
    m_commit(w->bitmap.data, dataSize);
}

static void update_window(OWindow *w, HDC dc, i32 width, i32 height) {
    StretchDIBits(dc,
                  0, 0, width, height,
                  0, 0, w->bitmap.width, w->bitmap.height, // copy from exact rect size to exact rect size
                  w->bitmap.data,
                  &w->bitmap.info,
                  DIB_RGB_COLORS, //data per pixel
                  SRCCOPY); //raster op code
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    OWindow *w = (OWindow *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);

    switch (msg) {
    case WM_NCCREATE: {
        OWindow *window = ((CREATESTRUCTW *)lparam)->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)window);
        if(window) window->hwnd = hwnd;
        g_app.window_count++;
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    } break;

    case WM_CLOSE: {
        DestroyWindow(hwnd);
        return 0;
    } break;

    case WM_PAINT: {
        if (!w) break;
        i32 width = 0, height = 0;
        window_get_content_size(w, &width, &height);
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(hwnd, &paint);
        if (w->bitmap.data) update_window(w, dc, width, height);
        EndPaint(hwnd, &paint);
        return 0;
    }

    case WM_DESTROY: {
        if (!w) return 0;
        w->open = false;
        g_app.window_count--;
        if (w->callbacks.on_close) {
            w->callbacks.on_close(w, w->userdata);
        }

        if(g_app.window_count == 0) PostQuitMessage(0);
    } break;

    case WM_NCDESTROY: {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)0);
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    case WM_SIZE: {
        if (!w) return 0;
        i32 width = LOWORD(lparam), height = HIWORD(lparam);
        if (w->callbacks.on_resize) w->callbacks.on_resize(w, width, height, w->userdata);
        if (w->callbacks.on_refresh) w->callbacks.on_refresh(w, w->userdata);
        if (w->bitmap.width != width || w->bitmap.height != height) {
            window_create_bitmap(w, width, height);
        }
        color_bitmap(w->bitmap.data, w->bitmap.width, w->bitmap.height, g_xOffset, g_yOffset);
        g_xOffset++;
        g_yOffset++;
    } break;

    case WM_MOVE: {
        if (w && w->callbacks.on_move) w->callbacks.on_move(w, (i16)LOWORD(lparam), (i16)HIWORD(lparam), w->userdata);
    } break;
    }

    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static bool app_init(void) {
    if (g_app.initialized) return true;

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    g_app.hinstance = GetModuleHandleW(NULL);

    WNDCLASSEXW wc = {
        .cbSize = sizeof(WNDCLASSEXW),
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

    MSG msg = {0};
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    for (OWindow *w = g_app.windows; w; w = w->next) {
        if (w->bitmap.data) {
            color_bitmap(w->bitmap.data, w->bitmap.width, w->bitmap.height, g_xOffset, g_yOffset);

            HDC dc = GetDC(w->hwnd);
            i32 width = 0, height = 0;
            window_get_content_size(w, &width, &height);
            update_window(w, dc, width, height);
            ReleaseDC(w->hwnd, dc);
        }

        if (w->hwnd && w->callbacks.on_refresh) w->callbacks.on_refresh(w, w->userdata);
    }

    g_xOffset++;
    g_yOffset++;

    return g_app.window_count > 0;
}

OWindow *window_create(OWindowDesc desc) {
    if (desc.width <= 0 || desc.height <= 0) {
        LOG_ERROR("window_create: invalid desc. both width and height must be defined");
        return NULL;
    }

    if (!app_init()) return NULL;

    OWindow *window = calloc(1, sizeof *window);
    if (!window) {
        LOG_ERROR("window_create: out of memory");
        return NULL;
    }

    DWORD style = WS_POPUP;
    if (!desc.undecorated) {
        style |= WS_CAPTION | WS_OVERLAPPED | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_THICKFRAME;
    }

    wchar_t title[256];
    to_wide(desc.title, title, 256);
    HWND hwnd = CreateWindowExW(WS_EX_APPWINDOW, WINDOW_CLASS_NAME, title, style, desc.x, desc.y,
                                desc.width, desc.height, NULL, NULL, g_app.hinstance, window);
    if (!hwnd) {
        LOG_ERROR("CreateWindowExW failed: %lu", GetLastError());
        free(window);
        return NULL;
    }

    set_content_size(hwnd, desc.width, desc.height);

    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof dark);
    if (desc.undecorated) {
        MARGINS margins = {-1, -1, -1, -1};
        DwmExtendFrameIntoClientArea(hwnd, &margins);
    }

    // NOTE(Hamza): doubt this is needed
    // if (desc.transparent) {
    //     SetWindowLongPtrW(hwnd, GWL_EXSTYLE, GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_LAYERED);
    //     SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    // }

    memcpy((void*)window->titlebar.title, desc.title, min(strlen(desc.title), MAX_TITLE_LEN));
    window->titlebar.height = TITLEBAR_HEIGHT;
    window->open = true;
    window->next = g_app.windows;
    g_app.windows = window;

    i32 width = 0, height = 0;
    window_get_content_size(window, &width, &height);
    window_create_bitmap(window, width, height);

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
    window_release_bitmap(window);
    if (window->bitmap.dc) DeleteDC(window->bitmap.dc);
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

void window_set_position(OWindow *window, i32 x, i32 y) {
    if (window && window->hwnd) SetWindowPos(window->hwnd, NULL, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
}

void window_set_size(OWindow *window, i32 width, i32 height) {
    if (window && window->hwnd && width > 0 && height > 0) set_content_size(window->hwnd, width, height);
}

void window_set_callbacks(OWindow *window, OWindowCallbacks callbacks, void *userdata) {
    if (!window) return;
    window->callbacks = callbacks;
    window->userdata = userdata;
}

bool window_get_content_size(const OWindow *window, int32_t *out_width, int32_t *out_height) {
    RECT r = {0};
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

void *window_get_bitmap(OWindow *window, int32_t *width, int32_t *height) {
    window_get_content_size(window, width, height);
    return window->bitmap.data;
}
