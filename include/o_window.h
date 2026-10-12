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

typedef void (*OnRefreshFn)(OWindow *window, void *user_data);
typedef void (*OnCloseFn)(OWindow *window, void *user_data);
typedef void (*OnResizeFn)(OWindow *window, int32_t width, int32_t height, void *user_data);
typedef void (*OnMoveFn)(OWindow *window, int32_t x, int32_t y, void *user_data);

typedef struct OWindowCallbacks {
    OnRefreshFn on_refresh;
    OnCloseFn on_close;
    OnResizeFn on_resize;
    OnMoveFn on_move;
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
