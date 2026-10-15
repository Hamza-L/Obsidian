#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>

#include "i_input_internal.h"
#include "o_log.h"

#define VIRTUAL_KEY_CODE_COUNT 128

static const OKey k_key_map[VIRTUAL_KEY_CODE_COUNT] = {
    [kVK_ANSI_A] = O_KEY_A, [kVK_ANSI_B] = O_KEY_B, [kVK_ANSI_C] = O_KEY_C, [kVK_ANSI_D] = O_KEY_D,
    [kVK_ANSI_E] = O_KEY_E, [kVK_ANSI_F] = O_KEY_F, [kVK_ANSI_G] = O_KEY_G, [kVK_ANSI_H] = O_KEY_H,
    [kVK_ANSI_I] = O_KEY_I, [kVK_ANSI_J] = O_KEY_J, [kVK_ANSI_K] = O_KEY_K, [kVK_ANSI_L] = O_KEY_L,
    [kVK_ANSI_M] = O_KEY_M, [kVK_ANSI_N] = O_KEY_N, [kVK_ANSI_O] = O_KEY_O, [kVK_ANSI_P] = O_KEY_P,
    [kVK_ANSI_Q] = O_KEY_Q, [kVK_ANSI_R] = O_KEY_R, [kVK_ANSI_S] = O_KEY_S, [kVK_ANSI_T] = O_KEY_T,
    [kVK_ANSI_U] = O_KEY_U, [kVK_ANSI_V] = O_KEY_V, [kVK_ANSI_W] = O_KEY_W, [kVK_ANSI_X] = O_KEY_X,
    [kVK_ANSI_Y] = O_KEY_Y, [kVK_ANSI_Z] = O_KEY_Z,

    [kVK_ANSI_0] = O_KEY_0, [kVK_ANSI_1] = O_KEY_1, [kVK_ANSI_2] = O_KEY_2, [kVK_ANSI_3] = O_KEY_3,
    [kVK_ANSI_4] = O_KEY_4, [kVK_ANSI_5] = O_KEY_5, [kVK_ANSI_6] = O_KEY_6, [kVK_ANSI_7] = O_KEY_7,
    [kVK_ANSI_8] = O_KEY_8, [kVK_ANSI_9] = O_KEY_9,

    [kVK_Escape] = O_KEY_ESCAPE, [kVK_Return] = O_KEY_ENTER, [kVK_Tab] = O_KEY_TAB,
    [kVK_Delete] = O_KEY_BACKSPACE, [kVK_Space] = O_KEY_SPACE,

    [kVK_LeftArrow] = O_KEY_LEFT, [kVK_RightArrow] = O_KEY_RIGHT,
    [kVK_UpArrow] = O_KEY_UP, [kVK_DownArrow] = O_KEY_DOWN,
};

static id g_monitor;

static OModKeys map_modifiers(NSEventModifierFlags flags) {
    OModKeys mods = 0;
    if (flags & NSEventModifierFlagShift) mods |= O_MOD_SHIFT;
    if (flags & NSEventModifierFlagControl) mods |= O_MOD_CONTROL;
    if (flags & NSEventModifierFlagOption) mods |= O_MOD_ALT;
    if (flags & NSEventModifierFlagCommand) mods |= O_MOD_SUPER;
    if (flags & NSEventModifierFlagCapsLock) mods |= O_MOD_CAPS_LOCK;
    return mods;
}

static OMouseButton map_mouse_button(NSInteger button) {
    return button >= 0 && button <= 2 ? (OMouseButton)button : O_MOUSE_BUTTON_OTHER;
}

static NSPoint content_location(NSEvent *event) {
    NSView *view = event.window.contentView;
    if (!view) return event.locationInWindow;
    NSPoint p = [view convertPoint:event.locationInWindow fromView:nil];
    if (!view.isFlipped) p.y = view.bounds.size.height - p.y;
    return p;
}

static void push_key(OInputEventKind kind, NSEvent *event) {
    const uint16_t key_code = (uint16_t)event.keyCode;
    input_push(&(OInputEvent){
        .kind = kind,
        .data.key = {
            .key = key_code < VIRTUAL_KEY_CODE_COUNT ? k_key_map[key_code] : O_KEY_UNKNOWN,
            .modifiers = map_modifiers(event.modifierFlags),
            .native_key_code = key_code,
            .is_repeat = event.isARepeat,
        },
    });
}

static void push_text(NSEvent *event) {
    const bool is_shortcut = (event.modifierFlags & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) != 0;
    if (is_shortcut) return;

    NSString *text = event.characters;
    uint32_t codepoints[32];
    NSUInteger used = 0;
    [text getBytes:codepoints
             maxLength:sizeof codepoints
            usedLength:&used
              encoding:NSUTF32LittleEndianStringEncoding
               options:0
                 range:NSMakeRange(0, text.length)
        remainingRange:NULL];
    for (NSUInteger i = 0; i < used / sizeof codepoints[0]; i++) input_push_text(codepoints[i]);
}

static void push_mouse_move(NSEvent *event) {
    NSPoint p = content_location(event);
    input_push(&(OInputEvent){
        .kind = O_INPUT_MOUSE_MOVE,
        .data.move = {.x = (float)p.x, .y = (float)p.y, .delta_x = (float)event.deltaX, .delta_y = (float)event.deltaY},
    });
}

static void push_mouse_button(OInputEventKind kind, NSEvent *event) {
    NSPoint p = content_location(event);
    input_push(&(OInputEvent){
        .kind = kind,
        .data.button = {
            .button = map_mouse_button(event.buttonNumber),
            .modifiers = map_modifiers(event.modifierFlags),
            .x = (float)p.x,
            .y = (float)p.y,
        },
    });
}

static void push_scroll(NSEvent *event) {
    input_push(&(OInputEvent){
        .kind = O_INPUT_MOUSE_SCROLL,
        .data.scroll = {.delta_x = (float)event.scrollingDeltaX, .delta_y = (float)event.scrollingDeltaY},
    });
}

static NSEvent *translate(NSEvent *event) {
    switch (event.type) {
    case NSEventTypeKeyDown: push_key(O_INPUT_KEY_DOWN, event); push_text(event); break;
    case NSEventTypeKeyUp: push_key(O_INPUT_KEY_UP, event); break;
    case NSEventTypeMouseMoved:
    case NSEventTypeLeftMouseDragged:
    case NSEventTypeRightMouseDragged:
    case NSEventTypeOtherMouseDragged: push_mouse_move(event); break;
    case NSEventTypeLeftMouseDown:
    case NSEventTypeRightMouseDown:
    case NSEventTypeOtherMouseDown: push_mouse_button(O_INPUT_MOUSE_DOWN, event); break;
    case NSEventTypeLeftMouseUp:
    case NSEventTypeRightMouseUp:
    case NSEventTypeOtherMouseUp: push_mouse_button(O_INPUT_MOUSE_UP, event); break;
    case NSEventTypeScrollWheel: push_scroll(event); break;
    default: break;
    }
    return event;
}

void input_platform_init(void) {
    [NSApplication sharedApplication];
    const NSEventMask mask = NSEventMaskKeyDown | NSEventMaskKeyUp | NSEventMaskMouseMoved |
                             NSEventMaskLeftMouseDragged | NSEventMaskRightMouseDragged | NSEventMaskOtherMouseDragged |
                             NSEventMaskLeftMouseDown | NSEventMaskLeftMouseUp | NSEventMaskRightMouseDown |
                             NSEventMaskRightMouseUp | NSEventMaskOtherMouseDown | NSEventMaskOtherMouseUp |
                             NSEventMaskScrollWheel;
    g_monitor = [NSEvent addLocalMonitorForEventsMatchingMask:mask handler:^NSEvent *(NSEvent *event) {
        return translate(event);
    }];
    if (!g_monitor) LOG_ERROR("addLocalMonitorForEventsMatchingMask failed");
}
