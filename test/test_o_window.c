#include "o_test.h"
#include "o_window.h"

typedef struct {
    int refreshes;
    int resizes;
    int closes;
    int32_t last_w, last_h;
} Events;

static void on_refresh(OWindow *window, void *user) {
    Events *e = user;
    if (++e->refreshes == 10) window_close(window);
}

static void on_resize(OWindow *window, int32_t w, int32_t h, void *user) {
    (void)window;
    Events *e = user;
    e->resizes++;
    e->last_w = w;
    e->last_h = h;
}

static void on_close(OWindow *window, void *user) {
    (void)window;
    ((Events *)user)->closes++;
}

void test_o_window(void) {
    CHECK(window_create(NULL) == NULL);
    CHECK(window_create(&(OWindowDesc){.width = 0, .height = 100}) == NULL);
    CHECK(!window_is_open(NULL));
    CHECK(window_native_handle(NULL) == NULL);

    Events events = {0};
    OWindow *w = window_create(&(OWindowDesc){
        .title = "o_window test",
        .x = 50,
        .y = 50,
        .width = 320,
        .height = 240,
        .resizable = true,
    });
    CHECK(w != NULL);
    if (!w) return;

    CHECK(window_is_open(w));
    CHECK(window_native_handle(w) != NULL);

    int32_t cw = 0, ch = 0, pw = 0, ph = 0;
    CHECK(window_get_content_size(w, &cw, &ch));
    CHECK(cw == 320 && ch == 240);
    CHECK(window_get_pixel_size(w, &pw, &ph));
    CHECK(pw >= cw && ph >= ch);

    window_set_callbacks(w, &(OWindowCallbacks){
        .on_refresh = on_refresh,
        .on_resize = on_resize,
        .on_close = on_close,
    }, &events);
    window_set_title(w, "renamed");
    window_set_position(w, 60, 60);
    window_show(w);

    window_set_size(w, 400, 300);
    CHECK(window_get_content_size(w, &cw, &ch));
    CHECK(cw == 400 && ch == 300);
    CHECK(events.resizes >= 1);
    CHECK(events.last_w == 400 && events.last_h == 300);

    while (window_poll_events()) {
    }
    CHECK(events.refreshes == 10);
    CHECK(events.closes == 1);
    CHECK(!window_is_open(w));
    CHECK(window_native_handle(w) == NULL);
    CHECK(!window_get_content_size(w, &cw, &ch));
    window_destroy(w);

    OWindow *w2 = window_create(&(OWindowDesc){.width = 100, .height = 100});
    CHECK(w2 != NULL);
    Events events2 = {0};
    window_set_callbacks(w2, &(OWindowCallbacks){.on_close = on_close}, &events2);
    window_destroy(w2);
    CHECK(events2.closes == 0);
    CHECK(!window_poll_events());
}
