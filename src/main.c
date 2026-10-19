#include "o_log.h"
#include "o_window.h"
#include "o_renderer.h"
#include "o_input.h"

#include <stdio.h>

static void on_key(OInputSubscriber *input, OKeyEvent event, void *user) {
    (void)input;
    if (event.key == O_KEY_ESCAPE) {
        OWindow* window = (OWindow*)user;
        window_close(window);
    }
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    log_set_level(O_LOG_DEBUG);

    OWindow * window = window_create(&(OWindowDesc){.width = 800, .height = 600, .x = 0, .y = 0, .title = "Obsidian"});
    input_subscribe((OInputCallbacks){.on_key_down = on_key }, window);

    ORenderer *renderer = renderer_create(&(ORendererDesc){0});

    while (window_poll_events()) {
        //do work
        input_poll_events();
    }

    renderer_destroy(renderer);
    window_destroy(window);

    return 0;
}
