#include "o_test.h"
#include "o_input.h"

static void on_key(OInputSubscriber *input, OKeyEvent event, void *user) {
    (void)input;
    (void)event;
    (*(int *)user)++;
}

void test_o_input(void) {
    CHECK(!input_poll_events());

    int hits = 0;
    OInputSubscriber *subs[8];
    for (int i = 0; i < 8; i++) {
        subs[i] = input_subscribe((OInputCallbacks){.on_key_down = on_key}, &hits);
        CHECK(subs[i] != NULL);
    }
    CHECK(input_subscribe((OInputCallbacks){0}, NULL) == NULL);

    input_unsubscribe(subs[3]);
    OInputSubscriber *reused = input_subscribe((OInputCallbacks){0}, NULL);
    CHECK(reused == subs[3]);

    CHECK(!input_poll_events());
    CHECK(hits == 0);

    for (int i = 0; i < 8; i++) input_unsubscribe(subs[i]);
    input_unsubscribe(NULL);
}
