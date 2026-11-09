#include "o_memory.h"

#include "core/o_alloc.h"
#include "core/o_typedefs.h"
#include "o_log.h"

#include <string.h>
#include <stdlib.h>

#define INIT_POOL_SLOT_COUNT 16
#define MEM_SCALING_FACTOR 2

typedef enum {
MPOOL_OK,
MPOOL_OOM,
MPOOL_COMMIT_FAILED,
} MPOOL_ERR;

typedef struct {
    u32 slotSize;
    u32 maxCount;
    u32 commitedCount;
    u32 count;
    u8  *data;
    u32 *generation;
    struct {
        u32 *entries;
        u32 count;
    } freelist;
    MPOOL_ERR errCode;
} MPool_;

MPool memory_pool_create(const u32 slot_size, const u32 max_count) {
    MPool_ *pool = (MPool_*)malloc(sizeof(*pool));
    pool->slotSize = slot_size;
    pool->count = 0;
    pool->commitedCount = max_count < INIT_POOL_SLOT_COUNT ? max_count : INIT_POOL_SLOT_COUNT;
    pool->maxCount = max_count;
    pool->freelist.count = 0;
    pool->errCode = MPOOL_OK;

    pool->data = m_reserve((size_t)pool->maxCount * slot_size);
    pool->freelist.entries = m_reserve(pool->maxCount * sizeof(u32));
    pool->generation = m_reserve(pool->maxCount * sizeof(u32));

    bool commitFailed = 0;
    commitFailed |= m_commit(pool->data, (size_t)pool->commitedCount * slot_size) == NULL;
    commitFailed |= m_commit(pool->freelist.entries, pool->commitedCount * sizeof(u32)) == NULL;
    commitFailed |= m_commit(pool->generation, pool->commitedCount * sizeof(u32)) == NULL;

    if (commitFailed) {
        LOG_ERROR("failed to create memory pool");
        memory_pool_destroy(pool);
        return NULL;
    }

    return pool;
}

void memory_pool_destroy(MPool pool_) {
    MPool_* pool = (MPool_*)pool_;
    m_release(pool->data, (size_t)pool->maxCount * pool->slotSize);
    m_release(pool->freelist.entries, pool->maxCount * sizeof(u32));
    m_release(pool->generation, pool->maxCount * sizeof(u32));
    free(pool);
}

MHandle memory_pool_item_acquire(MPool pool_) {
    MPool_* pool = (MPool_*)pool_;

    u32 freeslot = 0;
    if (pool->freelist.count) {
        freeslot = pool->freelist.entries[pool->freelist.count - 1];
        pool->freelist.count--;
    } else {
        freeslot = pool->count;
    }

    if (freeslot >= pool->maxCount) {
        LOG_ERROR("pool reached max capacity %d", pool->maxCount);
        pool->errCode = MPOOL_OOM;
        return HANDLE_INVALID;
    }

    if (freeslot >= pool->commitedCount) {
        size_t newCommitCount = pool->commitedCount * MEM_SCALING_FACTOR;
        newCommitCount = newCommitCount > pool->maxCount ? pool->maxCount : newCommitCount;

        bool commitFailed = 0;
        commitFailed |= m_commit(pool->data, newCommitCount * pool->slotSize) == NULL;
        commitFailed |= m_commit(pool->freelist.entries, newCommitCount * sizeof(u32)) == NULL;
        commitFailed |= m_commit(pool->generation, newCommitCount * sizeof(u32)) == NULL;

        if(commitFailed){
            pool->errCode = MPOOL_COMMIT_FAILED;
            return HANDLE_INVALID;
        } else {
            pool->commitedCount = newCommitCount;
        }
    }

    if (pool->count == freeslot) { //fresh path
        pool->generation[freeslot] = 0;
        pool->count++;
    }

    MHandle handle = {.age = pool->generation[freeslot], .id = freeslot + 1};
    return handle;
}

bool memory_pool_item_invalidate(MPool pool_, MHandle handle) {
    MPool_* pool = (MPool_*)pool_;

    if (handle.id == 0) {
        LOG_DEBUG("lookup with null handle");
        return false;
    }

    if (pool->generation[handle.id - 1] == handle.age) {
        pool->generation[handle.id - 1]++;
        return true;
    }
    return false;
}

bool memory_pool_item_recycle(MPool pool_, MHandle handle) {
    MPool_* pool = (MPool_*)pool_;

    if (handle.id == 0) {
        LOG_DEBUG("lookup with null handle");
        return false;
    }

    if (pool->generation[handle.id - 1] == handle.age) {
        LOG_DEBUG("recycling handle currently in use. Consider invalidating first.");
        return false;
    }

    pool->freelist.entries[pool->freelist.count++] = handle.id - 1;
    if(pool->errCode == MPOOL_OOM) pool->errCode = MPOOL_OK;
    return true;
}

void memory_pool_item_remove(MPool pool_, MHandle handle) {
    MPool_* pool = (MPool_*)pool_;

    if (handle.id == 0) {
        LOG_DEBUG("lookup with null handle");
        return;
    }

    if (pool->generation[handle.id - 1] == handle.age) {
        pool->generation[handle.id - 1]++;
        pool->freelist.entries[pool->freelist.count++] = handle.id - 1;
        if(pool->errCode == MPOOL_OOM) pool->errCode = MPOOL_OK;
    }
}

void *memory_pool_item_at(MPool pool_, u32 index) {
    MPool_* pool = (MPool_*)pool_;
    if (index == 0) {
        LOG_DEBUG("lookup with null index [index = 0 is NULL]");
        return NULL;
    }

    if (index > pool->count) {
        LOG_DEBUG("indexing out of capacity");
        return NULL;
    }

    return &pool->data[(index - 1) * pool->slotSize];
}

void *memory_pool_item_get(MPool pool_, MHandle handle) {
    MPool_* pool = (MPool_*)pool_;
    if (handle.id == 0) {
        LOG_DEBUG("lookup with null handle");
        return NULL;
    }
    if (pool->generation[handle.id - 1] == handle.age) {
        return &pool->data[(handle.id - 1) * pool->slotSize];
    }
    return NULL;
}

bool memory_pool_is_valid(MPool pool_) {
    MPool_* pool = (MPool_*)pool_;
    return !pool->errCode;
}
