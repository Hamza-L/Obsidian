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

static void i_am_pressed(OInput *input, OMouseButtonEvent event, void *user_data) {
    (void)input;
    (void)event;
    (void)user_data;
    fprintf(stderr, "MOUSE_PRESS\n");
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    OWindow * window = window_create(&(OWindowDesc){.width = 800, .height = 600, .x = 0, .y = 0, .resizable = false, .title = "Obsidian"});
    window_show(window);

    OInput * input = input_subscribe(&(OInputCallbacks){.on_mouse_down = i_am_pressed }, NULL);

    bool input_update = input_poll_events();
    while (window_poll_events() || input_update) {
    }

    window_destroy(window);

    return 0;
}
