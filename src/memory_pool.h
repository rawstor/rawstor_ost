#ifndef RAWSTOR_MEMORY_POOL_H
#define RAWSTOR_MEMORY_POOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Memory pool for request objects to eliminate per-request allocations
typedef struct {
    void** items;
    uint32_t size;
    uint32_t capacity;
    size_t item_size;
    bool is_pool_allocated;
} memory_pool_t;

// Initialize a memory pool
int memory_pool_init(memory_pool_t* pool, size_t item_size, uint32_t capacity);

// Get an item from the pool (returns NULL if empty)
void* memory_pool_alloc(memory_pool_t* pool);

// Return an item to the pool
void memory_pool_free(memory_pool_t* pool, void* item);

// Cleanup the pool
void memory_pool_destroy(memory_pool_t* pool);

// Pre-allocated pools for different object types
extern memory_pool_t request_pool;
extern memory_pool_t io_request_pool;
extern memory_pool_t accept_request_pool;
extern memory_pool_t proto_frame_pool;
extern memory_pool_t conn_pool;

// Initialize all memory pools
int init_memory_pools(void);

// Cleanup all memory pools
void cleanup_memory_pools(void);

#endif // RAWSTOR_MEMORY_POOL_H
