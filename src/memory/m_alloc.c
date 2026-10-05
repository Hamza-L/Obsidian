#include "o_memory.h"

#include "m_config.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t slot_size;
    uint32_t count;
    uint32_t capacity;
    struct {
        uint32_t* entries;
        uint32_t count;
    } freelist;
    uint32_t* generation;
    uint8_t* data;
} MPool_;

MPool memory_pool_create(const size_t slot_size) {
    MPool_* pool = malloc(sizeof(*pool));
    pool->slot_size = slot_size;
    pool->count = 0;
    pool->capacity = INIT_POOL_SLOT_COUNT;
    pool->data = calloc(pool->capacity, slot_size);
    pool->freelist.entries = calloc(pool->capacity, sizeof(uint32_t));
    pool->freelist.count = 0;
    pool->generation = calloc(pool->capacity, sizeof(uint32_t));

    return pool;
}

void memory_pool_destroy(MPool pool_) {
    MPool_* pool = (MPool_*)pool_;
    free(pool->data);
    free(pool->freelist.entries);
    free(pool->generation);
    free(pool);
    pool->count = 0;
    pool->capacity = 0;
    pool->slot_size = 0;
}

GHandle memory_pool_item_acquire(MPool pool_) {
    MPool_* pool = (MPool_*)pool_;
    if (pool->count >= pool->capacity) {
        pool->capacity*=2;
        pool->data = realloc(pool->data, pool->capacity * pool->slot_size);
        pool->freelist.entries = realloc(pool->freelist.entries, pool->capacity * sizeof(uint32_t));
        pool->generation = realloc(pool->generation, pool->capacity * sizeof(uint32_t));
    }

    uint32_t freeslot = 0;
    if (pool->freelist.count) {
        freeslot = pool->freelist.entries[pool->freelist.count - 1];
	    pool->freelist.count--;
    } else {
        freeslot = pool->capacity - pool->count - 1;
        pool->count++;
    }

    GHandle handle = {.age = pool->generation[freeslot], .index = freeslot};
    return handle;
}

void memory_pool_item_remove(MPool pool_, GHandle handle) {
    MPool_* pool = (MPool_*)pool_;
    pool->generation[handle.index]++;
    pool->freelist.entries[pool->freelist.count++] = handle.index;
}

void *memory_pool_item_get(MPool pool_, GHandle handle) {
    MPool_* pool = (MPool_*)pool_;
    if (pool->generation[handle.index] == handle.age) {
        return &pool->data[handle.index];
    }
    return NULL;
}
