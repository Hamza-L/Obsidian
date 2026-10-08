#include "o_memory.h"

#include "m_alloc_internal.h"
#include "o_log.h"

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
    bool is_out_of_mem;
} MPool_;

MPool memory_pool_create(const size_t slot_size) {
    MPool_* pool = m_malloc(sizeof(*pool));
    pool->slot_size = slot_size;
    pool->count = 0;
    pool->capacity = INIT_POOL_SLOT_COUNT;
    pool->data = m_malloc(pool->capacity * slot_size);
    pool->freelist.entries = m_malloc(pool->capacity * sizeof(uint32_t));
    pool->freelist.count = 0;
    pool->generation = m_malloc(pool->capacity * sizeof(uint32_t));
    pool->is_out_of_mem = false;

    return pool;
}

void memory_pool_destroy(MPool pool_) {
    MPool_* pool = (MPool_*)pool_;
    m_free(pool->data);
    m_free(pool->freelist.entries);
    m_free(pool->generation);
    m_free(pool);
}

MHandle memory_pool_item_acquire(MPool pool_) {
    MPool_* pool = (MPool_*)pool_;

    if (pool->is_out_of_mem) return MHANDLE_INVALID;

    if (pool->count >= pool->capacity) {
        size_t newcap = pool->capacity*MEM_SCALING_FACTOR;
        void *data = m_realloc(pool->data, newcap * pool->slot_size);
        if(data) pool->data = data;
        uint32_t *entries = m_realloc(pool->freelist.entries, newcap * sizeof(uint32_t));
        if(entries) pool->freelist.entries = entries;
        uint32_t *generation = m_realloc(pool->generation, newcap * sizeof(uint32_t));
        if(generation) pool->generation = generation;

        if(!data || !entries || !generation){
            LOG_ERROR("pool out of memory growing to %zu slots of %u bytes", newcap, pool->slot_size);
            pool->is_out_of_mem = true;
            return MHANDLE_INVALID;
        } else {
            pool->capacity = newcap;
        }
    }

    uint32_t freeslot = 0;
    if (pool->freelist.count) {
        freeslot = pool->freelist.entries[pool->freelist.count - 1];
        pool->freelist.count--;
    } else {
        freeslot = pool->count;
        pool->generation[freeslot] = 0;
        pool->count++;
    }

    MHandle handle = {.age = pool->generation[freeslot], .index = freeslot + 1};
    return handle;
}

void memory_pool_item_remove(MPool pool_, MHandle handle) {
    MPool_* pool = (MPool_*)pool_;
    pool->generation[handle.index - 1]++;
    pool->freelist.entries[pool->freelist.count++] = handle.index - 1;
}

void *memory_pool_item_get(MPool pool_, MHandle handle) {
    MPool_* pool = (MPool_*)pool_;
    if (handle.index == 0) {
        LOG_DEBUG("lookup with null handle");
        return NULL;
    }
    if (pool->generation[handle.index - 1] == handle.age) {
        return &pool->data[handle.index - 1];
    }
    return NULL;
}

bool memory_pool_is_oom(MPool pool_) {
    MPool_* pool = (MPool_*)pool_;
    return pool->is_out_of_mem;
}
