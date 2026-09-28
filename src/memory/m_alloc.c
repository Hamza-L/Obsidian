#include "o_memory.h"

#include "m_config.h"

#include <stdio.h>
#include <stdlib.h>

typedef struct {
    uint32_t slot_size;
    uint32_t count;
    uint32_t capacity;
    MHandle* free_list;
    void* data;
} MPool_;

MPool memory_pool_create(const size_t slot_size) {
    MPool_* pool = malloc(sizeof(*pool));
    pool->slot_size = slot_size;
    pool->count = 0;
    pool->capacity = INIT_POOL_SLOT_COUNT;
    pool->free_list = calloc(pool->capacity, sizeof(MHandle));
    pool->data = calloc(pool->capacity, slot_size);

    // fill free list
    for (size_t i = pool->capacity - 1; i >= 0; i--) {
        pool->free_list[i] = i;
    }

    return pool;
}

void memory_pool_destroy(MPool pool_) {
    MPool_* pool = (MPool_*)pool_;
    free(pool->data);
    free(pool->free_list);
    free(pool);
}

MHandle memory_pool_item_add(MPool pool_, void *data, size_t size) {
    MPool_* pool = (MPool_*)pool_;
    if (pool->count >= pool->capacity) {
        pool->capacity*=2;
        pool->free_list = realloc(pool->free_list, pool->capacity * sizeof(MHandle));
        pool->data = realloc(pool->data, pool->capacity * pool->slot_size);
    }

    uint32_t top_of_stack = pool->capacity - pool->count - 1;
    pool->count++;

    MHandle handle = ((uint64_t)top_of_stack << 32) + ();
    return handle;
}

void memory_pool_item_remove(MPool pool_, MHandle handle) {
    MPool_* pool = (MPool_*)pool_;

}

void *memory_pool_item_get(MPool pool_, MHandle handle) {
    MPool_* pool = (MPool_*)pool_;

}
