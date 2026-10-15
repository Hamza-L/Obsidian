#include "i_input_internal.h"
#include "o_input.h"
#include "o_log.h"

#include <string.h>

#define MAX_SUBSCRIBERS 8
#define QUEUE_CAPACITY 256

#define UNICODE_DELETE 0x7F
#define UNICODE_FIRST_PRINTABLE 0x20
#define UNICODE_PRIVATE_USE_FIRST 0xE000
#define UNICODE_PRIVATE_USE_LAST 0xF8FF
#define UTF16_HIGH_SURROGATE_FIRST 0xD800
#define UTF16_LOW_SURROGATE_FIRST 0xDC00
#define UTF16_SURROGATE_PAYLOAD_BITS 10
#define UTF16_SUPPLEMENTARY_PLANE_FIRST 0x10000

struct OInputSubscriber {
    OInputCallbacks callbacks;
    void *user_data;
    bool online;
};

static struct {
    bool initialized;
    OInputSubscriber subscribers[MAX_SUBSCRIBERS];
    OInputEvent queue[QUEUE_CAPACITY];
    uint32_t head, tail;
    bool overflowed;
} g_input;

static void init(void) {
    if (g_input.initialized) return;
    g_input.initialized = true;
    input_platform_init();
}

// input push events are used by the platform layers (macos/win32) to submit events to our input-layer queue.
void input_push(const OInputEvent *event) {
    const uint32_t next = (g_input.tail + 1) % QUEUE_CAPACITY;
    if (next == g_input.head) {
        if (!g_input.overflowed) LOG_WARN("input queue full, dropping events until next poll");
        g_input.overflowed = true;
        return;
    }
    g_input.queue[g_input.tail] = *event;
    g_input.tail = next;
}

void input_push_text(uint32_t codepoint) {
    const bool is_control = codepoint < UNICODE_FIRST_PRINTABLE || codepoint == UNICODE_DELETE;
    const bool is_private_use = codepoint >= UNICODE_PRIVATE_USE_FIRST && codepoint <= UNICODE_PRIVATE_USE_LAST;
    if (is_control || is_private_use) return;
    input_push(&(OInputEvent){.kind = O_INPUT_TEXT, .data.codepoint = codepoint});
}

uint32_t utf16_decode_pair(uint16_t high, uint16_t low) {
    const uint32_t high_bits = (uint32_t)(high - UTF16_HIGH_SURROGATE_FIRST) << UTF16_SURROGATE_PAYLOAD_BITS;
    const uint32_t low_bits = (uint32_t)(low - UTF16_LOW_SURROGATE_FIRST);
    return UTF16_SUPPLEMENTARY_PLANE_FIRST + high_bits + low_bits;
}

static void dispatch(const OInputEvent *e) {
    for (uint32_t i = 0; i < MAX_SUBSCRIBERS; i++) {
        OInputSubscriber *s = &g_input.subscribers[i];
        if (!s->online) continue;
        const OInputCallbacks *cb = &s->callbacks;
        switch (e->kind) {
        case O_INPUT_KEY_DOWN: if (cb->on_key_down) cb->on_key_down(s, e->data.key, s->user_data); break;
        case O_INPUT_KEY_UP: if (cb->on_key_up) cb->on_key_up(s, e->data.key, s->user_data); break;
        case O_INPUT_TEXT: if (cb->on_text_input) cb->on_text_input(s, e->data.codepoint, s->user_data); break;
        case O_INPUT_MOUSE_MOVE: if (cb->on_mouse_move) cb->on_mouse_move(s, e->data.move, s->user_data); break;
        case O_INPUT_MOUSE_DOWN: if (cb->on_mouse_down) cb->on_mouse_down(s, e->data.button, s->user_data); break;
        case O_INPUT_MOUSE_UP: if (cb->on_mouse_up) cb->on_mouse_up(s, e->data.button, s->user_data); break;
        case O_INPUT_MOUSE_SCROLL: if (cb->on_mouse_scroll) cb->on_mouse_scroll(s, e->data.scroll, s->user_data); break;
        }
    }
}

bool input_poll_events(void) {
    if (!g_input.initialized) return false;
    const bool had_events = g_input.head != g_input.tail;
    while (g_input.head != g_input.tail) {
        OInputEvent e = g_input.queue[g_input.head];
        g_input.head = (g_input.head + 1) % QUEUE_CAPACITY;
        dispatch(&e);
    }
    g_input.overflowed = false;
    return had_events;
}

OInputSubscriber *input_subscribe(const OInputCallbacks callbacks, void *user_data) {
    init();
    for (uint32_t i = 0; i < MAX_SUBSCRIBERS; i++) {
        OInputSubscriber *s = &g_input.subscribers[i];
        if (s->online) continue;
        s->callbacks = callbacks;
        s->user_data = user_data;
        s->online = true;
        return s;
    }
    LOG_ERROR("input_subscribe: all %d subscriber slots in use", MAX_SUBSCRIBERS);
    return NULL;
}

void input_unsubscribe(OInputSubscriber *subscriber) {
    if (!subscriber) return;
    subscriber->callbacks = EMPTY_INPUT_CALLBACK;
    subscriber->online = false;
    subscriber->user_data = NULL;
}
