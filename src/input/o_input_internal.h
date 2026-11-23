#ifndef I_INPUT_INTERNAL_H
#define I_INPUT_INTERNAL_H

#include "o_input.h"

enum{ MAX_INPUT_SUBSCRIBERS = 8};

typedef enum OInputEventKind {
    O_INPUT_NONE,
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
        OTextEvent text;
        OMouseMoveEvent move;
        OMouseButtonEvent button;
        OMouseScrollEvent scroll;
    } data;
} OInputEvent;

void input_platform_init(void);

// used the respective platform to push events to our input backend
OInputEvent input_translate_event(void *platformEvent);
void input_dispatch_event(OInputEvent event); //definition in TU o_input.c

#endif
