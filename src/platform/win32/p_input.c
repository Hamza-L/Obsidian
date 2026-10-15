#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

#include "i_input_internal.h"
#include "o_log.h"

#define KEY_IS_DOWN(vk) ((GetKeyState(vk) & 0x8000) != 0)
#define KEY_IS_TOGGLED(vk) ((GetKeyState(vk) & 0x0001) != 0)
#define GET_SCAN_CODE(lparam) ((uint16_t)LOBYTE(HIWORD(lparam)))
#define GET_KEY_IS_REPEAT(lparam) ((HIWORD(lparam) & KF_REPEAT) != 0)

static struct {
    HHOOK hook;
    float last_x, last_y;
    bool has_last;
    wchar_t pending_high_surrogate;
} g_win;

static OModKeys get_modifiers(void) {
    OModKeys mods = 0;
    if (KEY_IS_DOWN(VK_SHIFT)) mods |= O_MOD_SHIFT;
    if (KEY_IS_DOWN(VK_CONTROL)) mods |= O_MOD_CONTROL;
    if (KEY_IS_DOWN(VK_MENU)) mods |= O_MOD_ALT;
    if (KEY_IS_DOWN(VK_LWIN) || KEY_IS_DOWN(VK_RWIN)) mods |= O_MOD_SUPER;
    if (KEY_IS_TOGGLED(VK_CAPITAL)) mods |= O_MOD_CAPS_LOCK;
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

static void push_key(OInputEventKind kind, WPARAM wparam, LPARAM lparam) {
    input_push(&(OInputEvent){
        .kind = kind,
        .data.key = {
            .key = map_key(wparam),
            .modifiers = get_modifiers(),
            .native_key_code = GET_SCAN_CODE(lparam),
            .is_repeat = kind == O_INPUT_KEY_DOWN && GET_KEY_IS_REPEAT(lparam),
        },
    });
}

static void push_char(wchar_t c) {
    if (IS_HIGH_SURROGATE(c)) {
        g_win.pending_high_surrogate = c;
        return;
    }
    const wchar_t high = g_win.pending_high_surrogate;
    g_win.pending_high_surrogate = 0;
    if (IS_SURROGATE_PAIR(high, c)) {
        input_push_text(utf16_decode_pair(high, c));
    } else {
        input_push_text(c);
    }
}

static void push_mouse_move(LPARAM lparam) {
    const float x = (float)GET_X_LPARAM(lparam);
    const float y = (float)GET_Y_LPARAM(lparam);
    input_push(&(OInputEvent){
        .kind = O_INPUT_MOUSE_MOVE,
        .data.move = {
            .x = x,
            .y = y,
            .delta_x = g_win.has_last ? x - g_win.last_x : 0.0f,
            .delta_y = g_win.has_last ? y - g_win.last_y : 0.0f,
        },
    });
    g_win.last_x = x;
    g_win.last_y = y;
    g_win.has_last = true;
}

static void push_mouse_button(OInputEventKind kind, UINT msg, LPARAM lparam) {
    input_push(&(OInputEvent){
        .kind = kind,
        .data.button = {
            .button = map_mouse_button(msg),
            .modifiers = get_modifiers(),
            .x = (float)GET_X_LPARAM(lparam),
            .y = (float)GET_Y_LPARAM(lparam),
        },
    });
}

static void push_scroll(UINT msg, WPARAM wparam) {
    const float notches = (float)GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
    input_push(&(OInputEvent){
        .kind = O_INPUT_MOUSE_SCROLL,
        .data.scroll = {
            .delta_x = msg == WM_MOUSEHWHEEL ? notches : 0.0f,
            .delta_y = msg == WM_MOUSEWHEEL ? notches : 0.0f,
        },
    });
}

static void translate(const MSG *msg) {
    switch (msg->message) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: push_key(O_INPUT_KEY_DOWN, msg->wParam, msg->lParam); break;
    case WM_KEYUP:
    case WM_SYSKEYUP: push_key(O_INPUT_KEY_UP, msg->wParam, msg->lParam); break;
    case WM_CHAR: push_char((wchar_t)msg->wParam); break;
    case WM_MOUSEMOVE: push_mouse_move(msg->lParam); break;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN: push_mouse_button(O_INPUT_MOUSE_DOWN, msg->message, msg->lParam); break;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_XBUTTONUP: push_mouse_button(O_INPUT_MOUSE_UP, msg->message, msg->lParam); break;
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: push_scroll(msg->message, msg->wParam); break;
    default: break;
    }
}

static LRESULT CALLBACK get_message_hook(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION && wparam == PM_REMOVE) translate((const MSG *)lparam);
    return CallNextHookEx(NULL, code, wparam, lparam);
}

void input_platform_init(void) {
    g_win.hook = SetWindowsHookExW(WH_GETMESSAGE, get_message_hook, NULL, GetCurrentThreadId());
    if (!g_win.hook) LOG_ERROR("SetWindowsHookExW failed: %lu", GetLastError());
}
