#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <Xinput.h>

#include "input/o_input_internal.h"
#include "core/o_typedefs.h"

#define KEY_IS_DOWN(vk) ((GetKeyState(vk) & 0x8000) != 0)
#define KEY_IS_TOGGLED(vk) ((GetKeyState(vk) & 0x0001) != 0)
#define GET_SCAN_CODE(lparam) ((uint16_t)LOBYTE(HIWORD(lparam)))
#define GET_KEY_IS_REPEAT(lparam) ((HIWORD(lparam) & KF_REPEAT) != 0)

#define UNICODE_DELETE 0x7F
#define UNICODE_FIRST_PRINTABLE 0x20
#define UNICODE_PRIVATE_USE_FIRST 0xE000
#define UNICODE_PRIVATE_USE_LAST 0xF8FF
#define UTF16_HIGH_SURROGATE_FIRST 0xD800
#define UTF16_LOW_SURROGATE_FIRST 0xDC00
#define UTF16_SURROGATE_PAYLOAD_BITS 10
#define UTF16_SUPPLEMENTARY_PLANE_FIRST 0x10000


#define INPUT_WINDOW_CLASS_NAME L"OInput"

static struct {
    f32 last_x, last_y;
    bool has_last;
    wchar_t pending_high_surrogate;
    bool initialized;
} g_ctx;

static inline u32 utf16_decode_pair(u16 high, u16 low) {
    const u32 high_bits = (u32)(high - UTF16_HIGH_SURROGATE_FIRST) << UTF16_SURROGATE_PAYLOAD_BITS;
    const u32 low_bits = (u32)(low - UTF16_LOW_SURROGATE_FIRST);
    return UTF16_SUPPLEMENTARY_PLANE_FIRST + high_bits + low_bits;
}

static inline OModKeys get_modifiers(void) {
    OModKeys mods = 0;
    if (KEY_IS_DOWN(VK_SHIFT)) mods |= O_MOD_SHIFT;
    if (KEY_IS_DOWN(VK_CONTROL)) mods |= O_MOD_CONTROL;
    if (KEY_IS_DOWN(VK_MENU)) mods |= O_MOD_ALT;
    if (KEY_IS_DOWN(VK_LWIN) || KEY_IS_DOWN(VK_RWIN)) mods |= O_MOD_SUPER;
    if (KEY_IS_TOGGLED(VK_CAPITAL)) mods |= O_MOD_CAPS_LOCK;
    return mods;
}

static inline OKey map_key(WPARAM vk) {
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

static inline OMouseButton map_mouse_button(UINT msg) {
    switch (msg) {
    case WM_LBUTTONDOWN: case WM_LBUTTONUP: return O_MOUSE_BUTTON_LEFT;
    case WM_RBUTTONDOWN: case WM_RBUTTONUP: return O_MOUSE_BUTTON_RIGHT;
    case WM_MBUTTONDOWN: case WM_MBUTTONUP: return O_MOUSE_BUTTON_MIDDLE;
    default: return O_MOUSE_BUTTON_OTHER;
    }
}

static inline OInputEvent translate_key(OInputEventKind kind, WPARAM wparam, LPARAM lparam) {
    return (OInputEvent){
        .kind = kind,
        .data.key = {
            .key = map_key(wparam),
            .modifiers = get_modifiers(),
            .native_key_code = GET_SCAN_CODE(lparam),
            .is_repeat = kind == O_INPUT_KEY_DOWN && GET_KEY_IS_REPEAT(lparam),
        },
    };
}

static inline OInputEvent translate_text_input(u32 codepoint) {
    const bool is_control = codepoint < UNICODE_FIRST_PRINTABLE || codepoint == UNICODE_DELETE;
    const bool is_private_use = codepoint >= UNICODE_PRIVATE_USE_FIRST && codepoint <= UNICODE_PRIVATE_USE_LAST;
    if (is_control || is_private_use) return(OInputEvent){0};
    return (OInputEvent){.kind = O_INPUT_TEXT, .data.codepoint = codepoint};
}

static inline OInputEvent translate_char_input(wchar_t c) {
    if (IS_HIGH_SURROGATE(c)) {
        g_ctx.pending_high_surrogate = c;
        return(OInputEvent){0};
    }

    const wchar_t high = g_ctx.pending_high_surrogate;
    g_ctx.pending_high_surrogate = 0;
    if (IS_SURROGATE_PAIR(high, c)) {
        return translate_text_input(utf16_decode_pair(high, c));
    } else {
        return translate_text_input(c);
    }
}

static inline OInputEvent translate_mouse_move(LPARAM lparam) {
    const f32 x = (f32)GET_X_LPARAM(lparam);
    const f32 y = (f32)GET_Y_LPARAM(lparam);
    g_ctx.last_x = x;
    g_ctx.last_y = y;
    g_ctx.has_last = true;
    return (OInputEvent){
        .kind = O_INPUT_MOUSE_MOVE,
        .data.move = {
            .x = x,
            .y = y,
            .delta_x = g_ctx.has_last ? x - g_ctx.last_x : 0.0f,
            .delta_y = g_ctx.has_last ? y - g_ctx.last_y : 0.0f,
        },
    };
}

static inline OInputEvent translate_mouse_button(OInputEventKind kind, UINT msg, LPARAM lparam) {
    return (OInputEvent){
        .kind = kind,
        .data.button = {
            .button = map_mouse_button(msg),
            .modifiers = get_modifiers(),
            .x = (f32)GET_X_LPARAM(lparam),
            .y = (f32)GET_Y_LPARAM(lparam),
        },
    };
}

static inline OInputEvent translate_scroll(UINT msg, WPARAM wparam) {
    const f32 notches = (f32)GET_WHEEL_DELTA_WPARAM(wparam) / WHEEL_DELTA;
    return (OInputEvent){
        .kind = O_INPUT_MOUSE_SCROLL,
        .data.scroll = {
            .delta_x = msg == WM_MOUSEHWHEEL ? notches : 0.0f,
            .delta_y = msg == WM_MOUSEWHEEL ? notches : 0.0f,
        },
    };
}

OInputEvent input_translate_event(void *platformEvent) {
    const MSG *msg = platformEvent;
    OInputEvent event = {0};
    switch (msg->message) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: return translate_key(O_INPUT_KEY_DOWN, msg->wParam, msg->lParam);
    case WM_KEYUP:
    case WM_SYSKEYUP: return translate_key(O_INPUT_KEY_UP, msg->wParam, msg->lParam);
    case WM_CHAR: return translate_char_input((wchar_t)msg->wParam);
    case WM_MOUSEMOVE: return translate_mouse_move(msg->lParam);
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN: return translate_mouse_button(O_INPUT_MOUSE_DOWN, msg->message, msg->lParam);
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
    case WM_XBUTTONUP: return translate_mouse_button(O_INPUT_MOUSE_UP, msg->message, msg->lParam);
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL: return translate_scroll(msg->message, msg->wParam);
    default: break;
    }
    return event;
}

// not needed for win32. oinput is coupled to owindow in win32 which owns the wndproc
void input_platform_init(void) {
    //donothing;
}
