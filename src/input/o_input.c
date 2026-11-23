#include "core/o_typedefs.h"
#include "o_log.h"

#include "input/o_input_internal.h"

#include <string.h>

#ifndef INPUT_TRACE
#define INPUT_TRACE 1
#endif

struct OInputSubscriber {
    OInputCallbacks callbacks;
    void *user_data;
    bool online;
};

static struct {
    OInputSubscriber subscribers[MAX_INPUT_SUBSCRIBERS];
    bool initialized;
} g_input;

OInputSubscriber *input_subscribe(const OInputCallbacks callbacks, void *user_data) {
    for (u32 i = 0; i < MAX_INPUT_SUBSCRIBERS; i++) {
        OInputSubscriber *s = &g_input.subscribers[i];
        if (s->online) continue;
        s->callbacks = callbacks;
        s->user_data = user_data;
        s->online = true;
        return s;
    }

    LOG_ERROR("input_subscribe: all %d subscriber slots in use", MAX_INPUT_SUBSCRIBERS);
    return NULL;
}

void input_unsubscribe(OInputSubscriber *subscriber) {
    if (!subscriber) return;
    subscriber->callbacks = EMPTY_INPUT_CALLBACK;
    subscriber->online = false;
    subscriber->user_data = NULL;
}

static inline void trace(OInputEvent event) {
    if (!INPUT_TRACE) return;
    switch (event.kind) {
    case O_INPUT_KEY_DOWN:
    case O_INPUT_KEY_UP:
        LOG_DEBUG("key %-4s key=%d native=%u mods=0x%x repeat=%d", event.kind == O_INPUT_KEY_DOWN ? "down" : "up",
                  event.data.key.key, event.data.key.native_key_code, event.data.key.modifiers, event.data.key.is_repeat);
        break;
    case O_INPUT_TEXT:
        LOG_DEBUG("text     U+%04X", event.data.codepoint);
        break;
    case O_INPUT_MOUSE_MOVE:
        LOG_DEBUG("mouse move x=%.0f y=%.0f dx=%.0f dy=%.0f", event.data.move.x, event.data.move.y, event.data.move.delta_x,
                  event.data.move.delta_y);
        break;
    case O_INPUT_MOUSE_DOWN:
    case O_INPUT_MOUSE_UP:
        LOG_DEBUG("mouse %-4s button=%d x=%.0f y=%.0f mods=0x%x", event.kind == O_INPUT_MOUSE_DOWN ? "down" : "up",
                  event.data.button.button, event.data.button.x, event.data.button.y, event.data.button.modifiers);
        break;
    case O_INPUT_MOUSE_SCROLL:
        LOG_DEBUG("scroll   dx=%.2f dy=%.2f", event.data.scroll.delta_x, event.data.scroll.delta_y);
        break;
    }
}

void input_dispatch_event(OInputEvent event) {
    trace(event);
    for (u32 i = 0; i < MAX_INPUT_SUBSCRIBERS; i++) {
        OInputSubscriber *s = &g_input.subscribers[i];
        if (!s->online) continue;
        const OInputCallbacks *cb = &s->callbacks;
        switch (event.kind) {
        case O_INPUT_KEY_DOWN: if (cb->on_key_down) cb->on_key_down(s, event.data.key, s->user_data); break;
        case O_INPUT_KEY_UP: if (cb->on_key_up) cb->on_key_up(s, event.data.key, s->user_data); break;
        case O_INPUT_TEXT: if (cb->on_text_input) cb->on_text_input(s, event.data.codepoint, s->user_data); break;
        case O_INPUT_MOUSE_MOVE: if (cb->on_mouse_move) cb->on_mouse_move(s, event.data.move, s->user_data); break;
        case O_INPUT_MOUSE_DOWN: if (cb->on_mouse_down) cb->on_mouse_down(s, event.data.button, s->user_data); break;
        case O_INPUT_MOUSE_UP: if (cb->on_mouse_up) cb->on_mouse_up(s, event.data.button, s->user_data); break;
        case O_INPUT_MOUSE_SCROLL: if (cb->on_mouse_scroll) cb->on_mouse_scroll(s, event.data.scroll, s->user_data); break;
        }
    }
}
