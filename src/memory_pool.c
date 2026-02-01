#include "memory_pool.h"
#include "log.h"
#include "ost.h"
#include "ruring.h"

#include <stdlib.h>
#include <string.h>

// Pre-allocated pools for different object types
memory_pool_t request_pool;
memory_pool_t io_request_pool;
memory_pool_t accept_request_pool;
memory_pool_t proto_frame_pool;
memory_pool_t conn_pool;

// Initialize a memory pool
int memory_pool_init(memory_pool_t* pool, size_t item_size, uint32_t capacity) {
    pool->items = malloc(capacity * sizeof(void*));
    if (!pool->items) {
        return -1;
    }

    // Allocate all items upfront
    pool->capacity = capacity;
    pool->size = 0;
    pool->item_size = item_size;
    pool->is_pool_allocated = true;

    // Pre-allocate all items and add them to the pool
    for (uint32_t i = 0; i < capacity; i++) {
        void* item = malloc(item_size);
        if (!item) {
            // Cleanup on failure
            for (uint32_t j = 0; j < i; j++) {
                free(pool->items[j]);
            }
            free(pool->items);
            return -1;
        }
        // Initialize memory to zero to avoid uninitialized value errors
        memset(item, 0, item_size);
        pool->items[i] = item;
        pool->size++;
    }

    return 0;
}

// Get an item from the pool (returns NULL if empty)
void* memory_pool_alloc(memory_pool_t* pool) {
    if (pool->size == 0) {
        FLOG_INFO(stderr, "Memory pool exhausted!\n");
        return NULL;
    }

    // Return the last item in the pool (stack-like behavior)
    pool->size--;
    return pool->items[pool->size];
}

// Return an item to the pool
void memory_pool_free(memory_pool_t* pool, void* item) {
    if (pool->size >= pool->capacity) {
        FLOG_INFO(stderr, "Memory pool overflow!\n");
        return;
    }

    // Add the item back to the end of the pool
    pool->items[pool->size] = item;
    pool->size++;
}

// Cleanup the pool
void memory_pool_destroy(memory_pool_t* pool) {
    if (pool->items) {
        // Free all allocated items
        for (uint32_t i = 0; i < pool->capacity; i++) {
            free(pool->items[i]);
        }
        free(pool->items);
        pool->items = NULL;
        pool->size = 0;
        pool->capacity = 0;
    }
}

// Initialize all memory pools
int init_memory_pools(void) {
    // Initialize request pool (for basic request_t objects)
    if (memory_pool_init(&request_pool, sizeof(request_t), CQES * 4) != 0) {
        return -1;
    }

    // Initialize IO request pool (for io_request_t objects)
    if (memory_pool_init(&io_request_pool, sizeof(io_request_t), CQES * 4) !=
        0) {
        memory_pool_destroy(&request_pool);
        return -1;
    }

    // Initialize accept request pool
    if (memory_pool_init(
            &accept_request_pool, sizeof(accept_request_t), CQES
        ) != 0) {
        memory_pool_destroy(&request_pool);
        memory_pool_destroy(&io_request_pool);
        return -1;
    }

    // Initialize protocol frame pool (for proto_resp_frame_t objects)
    // Reasonable size based on io_uring capacity constraints
    if (memory_pool_init(
            &proto_frame_pool, sizeof(proto_resp_frame_t), CQES * 4
        ) != 0) {
        memory_pool_destroy(&request_pool);
        memory_pool_destroy(&io_request_pool);
        memory_pool_destroy(&accept_request_pool);
        return -1;
    }

    LOG_INFO("Memory pools initialized successfully\n");
    return 0;
}

// Cleanup all memory pools
void cleanup_memory_pools(void) {
    memory_pool_destroy(&request_pool);
    memory_pool_destroy(&io_request_pool);
    memory_pool_destroy(&accept_request_pool);
    memory_pool_destroy(&proto_frame_pool);
    LOG_INFO("Memory pools cleaned up\n");
}
