#include "o_input.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

// Define QWORD if the Windows SDK has not done so
#ifndef QWORD
typedef uint64_t QWORD;
#endif

#include "o_log.h"

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

OKeyEvent make_key_event(WPARAM wparam, LPARAM lparam, bool down) {
    return (OKeyEvent){
        .key = map_key(wparam),
        .modifiers = get_modifiers(),
        .native_key_code = (uint16_t)((lparam >> 16) & 0xFF),
        .is_repeat = down && (lparam & (1 << 30)),
    };
}

OMouseButtonEvent make_mouse_button_event(UINT msg, LPARAM lparam) {
    return (OMouseButtonEvent){
        .button = map_mouse_button(msg),
        .modifiers = get_modifiers(),
        .x = (float)GET_X_LPARAM(lparam),
        .y = (float)GET_Y_LPARAM(lparam),
    };
}

bool input_poll_events(void) {
    UINT bsize = 0;
    RAWINPUT buffer[32];
    bool hasInput = false;

    UINT count = GetRawInputBuffer(buffer, &bsize, sizeof(RAWINPUTHEADER));
    if (bsize == 0) {
        return hasInput; // No input messages in queue
    }

    if (count == (UINT)-1) {
        DWORD err = GetLastError();
        LOG_ERROR("input_poll_events: error %lul", err);
        // Handle error...
        return hasInput;
    }

    PRAWINPUT p = buffer;
    for (UINT i = 0; i < count; ++i) {
        if (p->header.dwType == RIM_TYPEKEYBOARD) {
            // Process keyboard input: pCurr->data.keyboard
            hasInput = true;
        } else if (p->header.dwType == RIM_TYPEMOUSE) {
            // Process mouse input: pCurr->data.mouse
            hasInput = true;
        }

        // Advance to the next raw input block using NEXTRAWINPUTBLOCK macro
        p = NEXTRAWINPUTBLOCK(p);
    }
    return hasInput;
}
