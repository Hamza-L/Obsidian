#include "o_log.h"
#include "o_memory.h"
#include "o_window.h"
#include "o_input.h"

#include <stdio.h>

#define PROFILE_START(name) \
    struct timespec start_##name, end_##name; \
    timespec_get(&start_##name, TIME_UTC);

#define PROFILE_END(name) \
    timespec_get(&end_##name, TIME_UTC); \
    double elapsed_##name = (end_##name.tv_sec - start_##name.tv_sec) + \
                            (end_##name.tv_nsec - start_##name.tv_nsec) / 1000000000.0; \
    printf("[PROFILE] %s took %.6f seconds\n", #name, elapsed_##name);

static void i_am_pressed(OInputSubscriber *input, OMouseButtonEvent event, void *user_data) {
    (void)input;
    (void)event;
    (void)user_data;
    fprintf(stderr, "MOUSE_PRESS\n");
}

static void on_refresh(OWindow *window, void *user) {
    (void)window;
    (void)user;
    input_poll_events();
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    OWindow * window = window_create(&(OWindowDesc){.width = 800, .height = 600, .x = 0, .y = 0, .title = "Obsidian"});
    window_show(window);
    window_set_callbacks(window, &(OWindowCallbacks){.on_refresh = on_refresh,}, NULL);

    input_subscribe((OInputCallbacks){.on_mouse_down = i_am_pressed }, NULL);
    while (window_poll_events()) {
        //do work
    }

    window_destroy(window);

    return 0;
}
