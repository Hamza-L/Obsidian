#ifndef O_WINDOW_H
#define O_WINDOW_H

#include <stdbool.h>
#include <stdint.h>

typedef struct OWindow OWindow;

typedef struct OWindowDesc {
    const char *title;
    int32_t x, y;
    int32_t width, height;

    bool resizable;
    bool undecorated;
    bool transparent;
} OWindowDesc;

typedef uint32_t OModKeys;
enum {
    O_MOD_SHIFT = 1u << 0,
    O_MOD_CONTROL = 1u << 1,
    O_MOD_ALT = 1u << 2,
    O_MOD_SUPER = 1u << 3,
    O_MOD_CAPS_LOCK = 1u << 4,
};

typedef enum OKey {
    O_KEY_UNKNOWN = 0,

    O_KEY_A,
    O_KEY_B,
    O_KEY_C,
    O_KEY_D,
    O_KEY_E,
    O_KEY_F,
    O_KEY_G,
    O_KEY_H,
    O_KEY_I,
    O_KEY_J,
    O_KEY_K,
    O_KEY_L,
    O_KEY_M,
    O_KEY_N,
    O_KEY_O,
    O_KEY_P,
    O_KEY_Q,
    O_KEY_R,
    O_KEY_S,
    O_KEY_T,
    O_KEY_U,
    O_KEY_V,
    O_KEY_W,
    O_KEY_X,
    O_KEY_Y,
    O_KEY_Z,

    O_KEY_0,
    O_KEY_1,
    O_KEY_2,
    O_KEY_3,
    O_KEY_4,
    O_KEY_5,
    O_KEY_6,
    O_KEY_7,
    O_KEY_8,
    O_KEY_9,

    O_KEY_ESCAPE,
    O_KEY_ENTER,
    O_KEY_TAB,
    O_KEY_BACKSPACE,
    O_KEY_SPACE,

    O_KEY_LEFT,
    O_KEY_RIGHT,
    O_KEY_UP,
    O_KEY_DOWN,
} OKey;

typedef enum OMouseButton {
    O_MOUSE_BUTTON_LEFT = 0,
    O_MOUSE_BUTTON_RIGHT = 1,
    O_MOUSE_BUTTON_MIDDLE = 2,
    O_MOUSE_BUTTON_OTHER = 3,
} OMouseButton;

typedef struct OKeyEvent {
    OKey key;
    OModKeys modifiers;
    uint16_t native_key_code;
    bool is_repeat;
} OKeyEvent;

typedef struct OMouseButtonEvent {
    OMouseButton button;
    OModKeys modifiers;
    float x , y;
} OMouseButtonEvent;

typedef struct OMouseMoveEvent {
    float x, y;
    float delta_x, delta_y;
} OMouseMoveEvent;

typedef struct OMouseScrollEvent {
    float delta_x, delta_y;
} OMouseScrollEvent;


typedef void (*OnRefreshFn)(OWindow *window, void *user_data);
typedef void (*OnCloseFn)(OWindow *window, void *user_data);
typedef void (*OnResizeFn)(OWindow *window, int32_t width, int32_t height, void *user_data);
typedef void (*OnMoveFn)(OWindow *window, int32_t x, int32_t y, void *user_data);
typedef void (*OnKeyFn)(OWindow *window, OKeyEvent event, void *user_data);
typedef void (*OnTextInputFn)(OWindow *window, uint32_t codepoint, void *user_data);

typedef void (*OnMouseMoveFn)(OWindow *window, OMouseMoveEvent event, void *user_data);
typedef void (*OnMouseButtonFn)(OWindow *window, OMouseButtonEvent event, void *user_data);
typedef void (*OnMouseScrollFn)(OWindow *window, OMouseScrollEvent event, void *user_data);

typedef struct OWindowCallbacks {
    OnRefreshFn on_refresh;
    OnCloseFn on_close;
    OnResizeFn on_resize;
    OnMoveFn on_move;

    OnKeyFn on_key_down;
    OnKeyFn on_key_up;
    OnTextInputFn on_text_input;

    OnMouseMoveFn on_mouse_move;
    OnMouseButtonFn on_mouse_down;
    OnMouseButtonFn on_mouse_up;
    OnMouseScrollFn on_mouse_scroll;
} OWindowCallbacks;

OWindow *window_create(const OWindowDesc *desc);
void window_show(OWindow *window);
void window_hide(OWindow *window);
void window_destroy(OWindow *window);
void window_close(OWindow *window);

bool window_poll_events(void);

void window_set_title(OWindow *window, const char *title);
void window_set_position(OWindow *window, int32_t x, int32_t y);
void window_set_size(OWindow *window, int32_t width, int32_t height);
void window_set_callbacks(OWindow *window, const OWindowCallbacks *callbacks, void *user_data);

bool window_get_content_size(const OWindow *window, int32_t *out_width, int32_t *out_height);
bool window_get_pixel_size(const OWindow *window, int32_t *out_width, int32_t *out_height);

bool window_is_open(const OWindow *window);

// NSView* on macOS, HWND on Windows.
void *window_native_handle(const OWindow *window);

#endif
