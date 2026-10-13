#include "o_input.h"
#include "o_log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

#include <stdlib.h>
#include <stdio.h>

#define MAX_SUBSCRIBER_CAP 8
#define DEFAULT_RAWINPUT_BUFFER_SIZE 64

// Define QWORD if the Windows SDK has not done so
#ifndef QWORD
typedef uint64_t QWORD;
#endif

struct OInputSubscriber {
    OInputCallbacks callbacks;
    void* userdata;
    bool online;
    bool disabled;
};

struct {
    bool isInit;
    OInputSubscriber subscribers[MAX_SUBSCRIBER_CAP];
    uint32_t count;

    void* rawInputBuffer;
    size_t rawInputBufferSize;
} g_input;

static void input_system_init(void) {
    for (int i = 0; i < MAX_SUBSCRIBER_CAP; i++) {
        g_input.subscribers[i] = (OInputSubscriber){.callbacks = EMPTY_INPUT_CALLBACK, .userdata = NULL, .online = false, .disabled = false};
    }
    g_input.count = 0;
    g_input.isInit = true;
    g_input.rawInputBuffer = malloc(DEFAULT_RAWINPUT_BUFFER_SIZE * sizeof(RAWINPUT));
    g_input.rawInputBufferSize = DEFAULT_RAWINPUT_BUFFER_SIZE * sizeof(RAWINPUT);

    RAWINPUTDEVICE Rid[2];
    Rid[0].usUsagePage = 0x01;          // HID_USAGE_PAGE_GENERIC
    Rid[0].usUsage = 0x02;              // HID_USAGE_GENERIC_MOUSE
    Rid[0].dwFlags = 0;                 // adds mouse and also ignores legacy mouse messages
    Rid[0].hwndTarget = 0;

    Rid[1].usUsagePage = 0x01;          // HID_USAGE_PAGE_GENERIC
    Rid[1].usUsage = 0x06;              // HID_USAGE_GENERIC_KEYBOARD
    Rid[1].dwFlags = 0;                 // adds keyboard and also ignores legacy keyboard messages
    Rid[1].hwndTarget = 0;

    if (RegisterRawInputDevices(Rid, 2, sizeof(Rid[0])) == FALSE) {
        //registration failed. Call GetLastError for the cause of the error
        LOG_ERROR("Raw Input Registration failed with errcode : %lul\n", GetLastError());
    }

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

static void ProcessInput(const RAWINPUT* input)
{
    if (input->header.dwType == RIM_TYPEKEYBOARD)
    {
        const RAWKEYBOARD* kb = &input->data.keyboard;
        const char* transition = (kb->Flags & RI_KEY_BREAK) ? "up" : "down";
        const char* extended   = (kb->Flags & RI_KEY_E0)    ? " e0" :
                                 (kb->Flags & RI_KEY_E1)    ? " e1" : "";
        fprintf(stderr,"keyboard: vk=0x%02x scan=0x%02x %s%s msg=0x%04x extra=0x%08lx\n",
            kb->VKey, kb->MakeCode, transition, extended,
            kb->Message, kb->ExtraInformation);
    }
    else if (input->header.dwType == RIM_TYPEMOUSE)
    {
        const RAWMOUSE* mouse = &input->data.mouse;
        const char* moveMode  = (mouse->usFlags & MOUSE_MOVE_ABSOLUTE) ? "abs" : "rel";

        fprintf(stderr, "mouse: move %s dx=%ld dy=%ld\n", moveMode, mouse->lLastX, mouse->lLastY);

        if (mouse->usButtonFlags & RI_MOUSE_LEFT_BUTTON_DOWN)   fprintf(stderr, "  left down\n");
        if (mouse->usButtonFlags & RI_MOUSE_LEFT_BUTTON_UP)     fprintf(stderr, "  left up\n");
        if (mouse->usButtonFlags & RI_MOUSE_RIGHT_BUTTON_DOWN)  fprintf(stderr, "  right down\n");
        if (mouse->usButtonFlags & RI_MOUSE_RIGHT_BUTTON_UP)    fprintf(stderr, "  right up\n");
        if (mouse->usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_DOWN) fprintf(stderr, "  middle down\n");
        if (mouse->usButtonFlags & RI_MOUSE_MIDDLE_BUTTON_UP)   fprintf(stderr, "  middle up\n");
        if (mouse->usButtonFlags & RI_MOUSE_BUTTON_4_DOWN)      fprintf(stderr, "  x1 down\n");
        if (mouse->usButtonFlags & RI_MOUSE_BUTTON_4_UP)        fprintf(stderr, "  x1 up\n");
        if (mouse->usButtonFlags & RI_MOUSE_BUTTON_5_DOWN)      fprintf(stderr, "  x2 down\n");
        if (mouse->usButtonFlags & RI_MOUSE_BUTTON_5_UP)        fprintf(stderr, "  x2 up\n");

        if (mouse->usButtonFlags & RI_MOUSE_WHEEL)
            fprintf(stderr,"  wheel delta=%d\n",  (int)(short)mouse->usButtonData);
        if (mouse->usButtonFlags & RI_MOUSE_HWHEEL)
            fprintf(stderr,"  hwheel delta=%d\n", (int)(short)mouse->usButtonData);
    }
    else if (input->header.dwType == RIM_TYPEHID)
    {
        const RAWHID* hid = &input->data.hid;
        fprintf(stderr,"hid: count=%lu size=%lu\n", hid->dwCount, hid->dwSizeHid);
    }
}

bool input_poll_events(void) {
    bool hasInput = false;

    for (;;) {
        UINT bufferSize = g_input.rawInputBufferSize;
        UINT count = GetRawInputBuffer(g_input.rawInputBuffer, &bufferSize, sizeof(RAWINPUTHEADER));
        if (count == 0) {
            break; // No input messages in queue
        }

        if (count == (UINT)-1) {
            /* Buffer too small — grow and retry. */
            g_input.rawInputBufferSize = max(bufferSize, g_input.rawInputBufferSize * 2);
            g_input.rawInputBuffer = realloc(g_input.rawInputBuffer, g_input.rawInputBufferSize);
            if (g_input.rawInputBuffer == NULL) {
                break;
            }
            continue;
        }

        PRAWINPUT p = g_input.rawInputBuffer;
        for (UINT i = 0; i < count; ++i) {
            // if (p->header.dwType == RIM_TYPEKEYBOARD) {
            //     // Process keyboard input: pCurr->data.keyboard
            //     hasInput = true;
            // } else if (p->header.dwType == RIM_TYPEMOUSE) {
            //     // Process mouse input: pCurr->data.mouse
            //     hasInput = true;
            // }
            ProcessInput(p);

            // Advance to the next raw input block using NEXTRAWINPUTBLOCK macro
            p = NEXTRAWINPUTBLOCK(p);
        }
    }
    return hasInput;
}

OInputSubscriber *input_subscribe(const OInputCallbacks callbacks, void *userdata) {
    if(!g_input.isInit) input_system_init();

    if (g_input.count >= MAX_SUBSCRIBER_CAP) {
        LOG_WARN("input system: OUT OF SUBSCRIBER SLOT");
    }

    uint32_t idx = 0;
    for (idx = 0; idx < MAX_SUBSCRIBER_CAP; idx++) {
        if(!g_input.subscribers[idx].online) break;
    }
    g_input.subscribers[idx].callbacks = callbacks;
    g_input.subscribers[idx].userdata = userdata;
    g_input.subscribers[idx].online = true;
    g_input.subscribers[idx].disabled = false;
    g_input.count++;

    return &g_input.subscribers[idx];
}

void input_unsubscribe(OInputSubscriber *subscriber) {
    subscriber->callbacks = EMPTY_INPUT_CALLBACK;
    subscriber->userdata = NULL;
    subscriber->disabled = false;
    subscriber->online = false;
    g_input.count--;
}
