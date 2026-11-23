#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>

#include "input/o_input_internal.h"
#include "core/o_typedefs.h"

#define VIRTUAL_KEY_CODE_COUNT 128

#define UNICODE_DELETE 0x7F
#define UNICODE_FIRST_PRINTABLE 0x20
#define UNICODE_PRIVATE_USE_FIRST 0xE000
#define UNICODE_PRIVATE_USE_LAST 0xF8FF

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

static bool is_text_codepoint(u32 codepoint) {
    const bool is_control = codepoint < UNICODE_FIRST_PRINTABLE || codepoint == UNICODE_DELETE;
    const bool is_private_use = codepoint >= UNICODE_PRIVATE_USE_FIRST && codepoint <= UNICODE_PRIVATE_USE_LAST;
    return !is_control && !is_private_use;
}

static OTextEvent translate_text(NSEvent *event) {
    OTextEvent text = {0};
    const bool is_shortcut = (event.modifierFlags & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) != 0;
    if (is_shortcut) return text;

    NSString *characters = event.characters;
    u32 codepoints[O_MAX_TEXT_CODEPOINTS];
    NSUInteger used = 0;
    [characters getBytes:codepoints
               maxLength:sizeof codepoints
              usedLength:&used
                encoding:NSUTF32LittleEndianStringEncoding
                 options:0
                   range:NSMakeRange(0, characters.length)
          remainingRange:NULL];
    for (NSUInteger i = 0; i < used / sizeof codepoints[0]; i++) {
        if (is_text_codepoint(codepoints[i])) text.codepoints[text.count++] = codepoints[i];
    }
    return text;
}

static OInputEvent translate_key(OInputEventKind kind, NSEvent *event) {
    const u16 key_code = (u16)event.keyCode;
    return (OInputEvent){
        .kind = kind,
        .data.key = {
            .key = key_code < VIRTUAL_KEY_CODE_COUNT ? k_key_map[key_code] : O_KEY_UNKNOWN,
            .modifiers = map_modifiers(event.modifierFlags),
            .native_key_code = key_code,
            .is_repeat = event.isARepeat,
            .text = kind == O_INPUT_KEY_DOWN ? translate_text(event) : (OTextEvent){0},
        },
    };
}

static OInputEvent translate_mouse_move(NSEvent *event) {
    const NSPoint p = content_location(event);
    return (OInputEvent){
        .kind = O_INPUT_MOUSE_MOVE,
        .data.move = {.x = (f32)p.x, .y = (f32)p.y, .delta_x = (f32)event.deltaX, .delta_y = (f32)event.deltaY},
    };
}

static OInputEvent translate_mouse_button(OInputEventKind kind, NSEvent *event) {
    const NSPoint p = content_location(event);
    return (OInputEvent){
        .kind = kind,
        .data.button = {
            .button = map_mouse_button(event.buttonNumber),
            .modifiers = map_modifiers(event.modifierFlags),
            .x = (f32)p.x,
            .y = (f32)p.y,
        },
    };
}

static OInputEvent translate_scroll(NSEvent *event) {
    return (OInputEvent){
        .kind = O_INPUT_MOUSE_SCROLL,
        .data.scroll = {.delta_x = (f32)event.scrollingDeltaX, .delta_y = (f32)event.scrollingDeltaY},
    };
}

OInputEvent input_translate_event(void *platformEvent) {
    NSEvent *event = (__bridge NSEvent *)platformEvent;
    switch (event.type) {
    case NSEventTypeKeyDown: return translate_key(O_INPUT_KEY_DOWN, event);
    case NSEventTypeKeyUp: return translate_key(O_INPUT_KEY_UP, event);
    case NSEventTypeMouseMoved:
    case NSEventTypeLeftMouseDragged:
    case NSEventTypeRightMouseDragged:
    case NSEventTypeOtherMouseDragged: return translate_mouse_move(event);
    case NSEventTypeLeftMouseDown:
    case NSEventTypeRightMouseDown:
    case NSEventTypeOtherMouseDown: return translate_mouse_button(O_INPUT_MOUSE_DOWN, event);
    case NSEventTypeLeftMouseUp:
    case NSEventTypeRightMouseUp:
    case NSEventTypeOtherMouseUp: return translate_mouse_button(O_INPUT_MOUSE_UP, event);
    case NSEventTypeScrollWheel: return translate_scroll(event);
    default: return (OInputEvent){0};
    }
}

void input_platform_init(void) {}
