#include "o_test.h"
#include "o_memory.h"

#include <stdint.h>
#include <string.h>

typedef struct {
    uint64_t a, b, c;
} Item;

static bool handle_valid(MHandle h) {
    return h.id != 0;
}

void test_o_memory(void) {
    MPool pool = memory_pool_create(sizeof(Item), 1024);
    CHECK(pool != NULL);
    CHECK(memory_pool_is_valid(pool));

    MHandle h1 = memory_pool_item_acquire(pool);
    MHandle h2 = memory_pool_item_acquire(pool);
    CHECK(handle_valid(h1));
    CHECK(handle_valid(h2));
    CHECK(h1.id != h2.id);

    Item *i1 = memory_pool_item_get(pool, h1);
    Item *i2 = memory_pool_item_get(pool, h2);
    CHECK(i1 != NULL);
    CHECK(i2 != NULL);
    CHECK(i1 != i2);
    CHECK((size_t)((uint8_t *)i2 - (uint8_t *)i1) >= sizeof(Item));

    *i1 = (Item){1, 2, 3};
    *i2 = (Item){4, 5, 6};
    CHECK(i1->a == 1 && i1->b == 2 && i1->c == 3);
    CHECK(memory_pool_item_get(pool, HANDLE_INVALID) == NULL);

    memory_pool_item_remove(pool, h1);
    CHECK(memory_pool_item_get(pool, h1) == NULL);
    CHECK(memory_pool_item_get(pool, h2) == i2);

    MHandle h3 = memory_pool_item_acquire(pool);
    CHECK(h3.id == h1.id);
    CHECK(h3.age != h1.age);
    CHECK(memory_pool_item_get(pool, h1) == NULL);
    CHECK(memory_pool_item_get(pool, h3) != NULL);

    enum { grow_count = 1000 };
    MHandle handles[grow_count];
    for (int i = 0; i < grow_count; i++) {
        handles[i] = memory_pool_item_acquire(pool);
        CHECK(handle_valid(handles[i]));
        Item *item = memory_pool_item_get(pool, handles[i]);
        CHECK(item != NULL);
        if (item) *item = (Item){(uint64_t)i, 0, 0};
    }
    bool all_intact = true;
    for (int i = 0; i < grow_count; i++) {
        Item *item = memory_pool_item_get(pool, handles[i]);
        all_intact = all_intact && item && item->a == (uint64_t)i;
    }
    CHECK(all_intact);
    CHECK(memory_pool_item_get(pool, h2) != NULL);
    CHECK(((Item *)memory_pool_item_get(pool, h2))->a == 4);
    CHECK(memory_pool_is_valid(pool));

    memory_pool_destroy(pool);

    MPool small = memory_pool_create(sizeof(Item), 4);
    CHECK(small != NULL);

    MHandle full[4];
    for (int i = 0; i < 4; i++) {
        full[i] = memory_pool_item_acquire(small);
        CHECK(handle_valid(full[i]));
    }
    CHECK(!handle_valid(memory_pool_item_acquire(small)));

    memory_pool_item_remove(small, full[1]);
    MHandle reused = memory_pool_item_acquire(small);
    CHECK(handle_valid(reused));
    CHECK(reused.id == full[1].id);
    CHECK(reused.age != full[1].age);
    CHECK(memory_pool_item_get(small, full[1]) == NULL);
    CHECK(memory_pool_item_get(small, reused) != NULL);
    CHECK(!handle_valid(memory_pool_item_acquire(small)));

    memory_pool_destroy(small);
}
