#ifndef I_INPUT_INTERNAL_H
#define I_INPUT_INTERNAL_H

#include "o_input.h"

typedef enum OInputEventKind {
    O_INPUT_KEY_DOWN,
    O_INPUT_KEY_UP,
    O_INPUT_TEXT,
    O_INPUT_MOUSE_MOVE,
    O_INPUT_MOUSE_DOWN,
    O_INPUT_MOUSE_UP,
    O_INPUT_MOUSE_SCROLL,
} OInputEventKind;

typedef struct OInputEvent {
    OInputEventKind kind;
    union {
        OKeyEvent key;
        uint32_t codepoint;
        OMouseMoveEvent move;
        OMouseButtonEvent button;
        OMouseScrollEvent scroll;
    } data;
} OInputEvent;

void input_platform_init(void);
void input_push(const OInputEvent *event);
void input_push_text(uint32_t codepoint);
uint32_t utf16_decode_pair(uint16_t high, uint16_t low);

#endif
