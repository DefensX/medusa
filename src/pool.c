
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <pthread.h>

#include "queue.h"
#include "pool.h"

#if defined(__WINDOWS__)
#define getpagesize()   0x1000
#endif

SLIST_HEAD(entries, entry);
struct entry {
        SLIST_ENTRY(entry) list;
        unsigned char data[0];
};

TAILQ_HEAD(blocks, block);
struct block {
        struct medusa_pool *pool;
        TAILQ_ENTRY(block) list;
        struct entries free;
        unsigned int nused;
        unsigned char data[0];
};

struct medusa_pool {
        char *name;
        struct blocks free;
        struct blocks half;
        struct blocks full;
        unsigned int size;
        unsigned int flags;
        unsigned int page_size;
        unsigned int count;
        unsigned int entry_capacity;
        unsigned int entry_used;
        unsigned int entry_used_average;
        void (*constructor) (void *ptr, void *context);
        void (*destructor) (void *ptr, void *context);
        void *context;
        pthread_mutex_t mutex;
};

static inline int aligncount (unsigned int size, unsigned int count, unsigned int header, unsigned int page_size)
{
        count *= size;
        count = (count + header + sizeof(void *) * 4 + (page_size - 1)) & ~(page_size - 1);
        count = count - header - sizeof(void *) * 4;
        count /= size;
        return count;
}

static void block_destroy (struct block *block)
{
        struct entry *entry;
        if (block == NULL) {
                return;
        }
        while (!SLIST_EMPTY(&block->free)) {
                entry = SLIST_FIRST(&block->free);
                SLIST_REMOVE_HEAD(&block->free, list);
                if (block->pool != NULL &&
                    block->pool->destructor != NULL) {
                        block->pool->destructor(entry->data, block->pool->context);
                }
        }
        block->pool->entry_capacity -= block->pool->count;
        free(block);
}

static struct block * block_create (struct medusa_pool *pool)
{
        unsigned int i;
        struct entry *entry;
        struct block *block;
        block = malloc(sizeof(struct block) + pool->size * pool->count);
        if (block == NULL) {
                goto bail;
        }
        memset(block, 0, sizeof(struct block));
        block->pool = pool;
        SLIST_INIT(&block->free);
        for (i = 0; i < pool->count; i++) {
                entry = (struct entry *) (block->data + pool->size * i);
                SLIST_INSERT_HEAD(&block->free, entry, list);
                if (pool->constructor != NULL) {
                        pool->constructor(entry->data, pool->context);
                }
        }
        pool->entry_capacity += pool->count;
        return block;
bail:   if (block != NULL) {
                block_destroy(block);
        }
        return NULL;
}

__attribute__ ((visibility ("default"))) struct medusa_pool * medusa_pool_create (
                const char *name,
                unsigned int size,
                unsigned int align,
                unsigned int count,
                unsigned int flags,
                void (*constructor) (void *ptr, void *context),
                void (*destructor) (void *ptr, void *context),
                void *context)
{
        struct medusa_pool *pool;
        if (align == 0) {
                align = 16;
        }
        if (count == 0) {
                count = 1;
        }
        pool = malloc(sizeof(struct medusa_pool));
        if (pool == NULL) {
                goto bail;
        }
        memset(pool, 0, sizeof(struct medusa_pool));
        TAILQ_INIT(&pool->free);
        TAILQ_INIT(&pool->half);
        TAILQ_INIT(&pool->full);
        if (flags & MEDUSA_POOL_FLAG_THREAD_SAFE) {
                pthread_mutex_init(&pool->mutex, NULL);
        }
        if (name != NULL) {
                pool->name = strdup(name);
                if (pool->name == NULL) {
                        goto bail;
                }
        }
        pool->size = sizeof(struct entry) + size;
        pool->size = (pool->size + align - 1) & ~(align - 1);
        pool->flags = flags;
        pool->constructor = constructor;
        pool->destructor = destructor;
        pool->context = context;
        pool->page_size = getpagesize();
        pool->count = aligncount(pool->size, count, sizeof(struct block), pool->page_size);
        return pool;
bail:   if (pool != NULL) {
                medusa_pool_destroy(pool);
        }
        return NULL;
}

__attribute__ ((visibility ("default"))) void medusa_pool_destroy (struct medusa_pool *pool)
{
        struct block *block;
        struct block *nblock;
        if (pool == NULL) {
                return;
        }
        if (pool->name != NULL) {
                free(pool->name);
        }
        TAILQ_FOREACH_SAFE(block, &pool->free, list, nblock) {
                TAILQ_REMOVE(&pool->free, block, list);
                block_destroy(block);
        }
        TAILQ_FOREACH_SAFE(block, &pool->half, list, nblock) {
                TAILQ_REMOVE(&pool->half, block, list);
                block_destroy(block);
        }
        TAILQ_FOREACH_SAFE(block, &pool->full, list, nblock) {
                TAILQ_REMOVE(&pool->full, block, list);
                block_destroy(block);
        }
        if (pool->flags & MEDUSA_POOL_FLAG_THREAD_SAFE) {
                pthread_mutex_destroy(&pool->mutex);
        }
        free(pool);
}

__attribute__ ((visibility ("default"))) void * medusa_pool_malloc (struct medusa_pool *pool)
{
        struct entry *entry;
        struct block *block;
        struct blocks *rblocks;
        struct blocks *ablocks;
        if (pool == NULL) {
                goto bail;
        }
        if (pool->flags & MEDUSA_POOL_FLAG_THREAD_SAFE) {
                pthread_mutex_lock(&pool->mutex);
        }
        if (!TAILQ_EMPTY(&pool->half)) {
                rblocks = &pool->half;
        } else if (!TAILQ_EMPTY(&pool->free)) {
                rblocks = &pool->free;
        } else {
                rblocks = NULL;
                block = block_create(pool);
                if (block == NULL) {
                        goto bail;
                }
        }
        if (rblocks != NULL) {
                block = TAILQ_FIRST(rblocks);
        }
        entry = SLIST_FIRST(&block->free);
        SLIST_REMOVE_HEAD(&block->free, list);
        block->nused += 1;
        pool->entry_used += 1;
        if (SLIST_EMPTY(&block->free)) {
                ablocks = &pool->full;
        } else {
                ablocks = &pool->half;
        }
        if (rblocks != ablocks) {
                if (rblocks != NULL) {
                        TAILQ_REMOVE(rblocks, block, list);
                }
                TAILQ_INSERT_HEAD(ablocks, block, list);
        }
        entry->list.sle_next = (void *) block;
        if (pool->flags & MEDUSA_POOL_FLAG_THREAD_SAFE) {
                pthread_mutex_unlock(&pool->mutex);
        }
        return entry->data;
bail:   if (pool != NULL) {
                if (pool->flags & MEDUSA_POOL_FLAG_THREAD_SAFE) {
                        pthread_mutex_unlock(&pool->mutex);
                }
        }
        return NULL;
}

__attribute__ ((visibility ("default"))) void medusa_pool_free (void *ptr)
{
        struct medusa_pool *pool;
        struct block *block;
        struct entry *entry;
        if (ptr == NULL) {
                return;
        }
        entry = (struct entry *) (((unsigned char *) ptr) - sizeof(struct entry));
        block = (struct block *) entry->list.sle_next;
        pool = block->pool;
        if (pool->flags & MEDUSA_POOL_FLAG_THREAD_SAFE) {
                pthread_mutex_lock(&pool->mutex);
        }
        if (SLIST_EMPTY(&block->free)) {
                TAILQ_REMOVE(&pool->full, block, list);
        } else {
                TAILQ_REMOVE(&pool->half, block, list);
        }
        SLIST_INSERT_HEAD(&block->free, entry, list);
        block->nused -= 1;
        pool->entry_used -= 1;
        if (block->nused == 0) {
                if (pool->flags & MEDUSA_POOL_FLAG_RESERVE_NONE) {
                        block_destroy(block);
                } else if (pool->flags & MEDUSA_POOL_FLAG_RESERVE_SINGLE) {
                        if (TAILQ_EMPTY(&pool->free)) {
                                TAILQ_INSERT_HEAD(&pool->free, block, list);
                        } else {
                                block_destroy(block);
                        }
                } else if (pool->flags & MEDUSA_POOL_FLAG_RESERVE_HEURISTIC) {
                        TAILQ_INSERT_HEAD(&pool->free, block, list);
                        if (pool->entry_used_average == 0) {
                                pool->entry_used_average = pool->entry_used;
                        }
                        pool->entry_used_average = pool->entry_used_average * 3 / 4 + pool->entry_used / 4;
                        while (pool->entry_used_average * 2 < pool->entry_capacity && !TAILQ_EMPTY(&pool->free)) {
                                block = TAILQ_LAST(&pool->free, blocks);
                                TAILQ_REMOVE(&pool->free, block, list);
                                block_destroy(block);
                        }

                } else {
                        block_destroy(block);
                }
        } else {
                TAILQ_INSERT_HEAD(&pool->half, block, list);
        }
        if (pool->flags & MEDUSA_POOL_FLAG_THREAD_SAFE) {
                pthread_mutex_unlock(&pool->mutex);
        }
}
