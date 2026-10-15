#include "o_log.h"
#include "o_window.h"
#include "o_input.h"

#include <stdio.h>

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    log_set_level(O_LOG_DEBUG);

    OWindow * window = window_create(&(OWindowDesc){.width = 800, .height = 600, .x = 0, .y = 0, .title = "Obsidian"});
    window_show(window);

    input_subscribe(EMPTY_INPUT_CALLBACK, NULL);

    bool isRunning = true;
    while (isRunning) {
        //do work
        isRunning = window_poll_events();
        input_poll_events();
    }

    window_destroy(window);

    return 0;
}
