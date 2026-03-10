#ifndef CACHE_H
#define CACHE_H

#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Block Device LRU Cache
 *
 * Implements a Least Recently Used (LRU) cache for block devices.
 * Caches fixed-size blocks (512 bytes) to reduce physical I/O.
 * ========================================================================= */

/* Cache configuration */
#define CACHE_BLOCK_SIZE 512    /* Standard disk sector size */
#define CACHE_MAX_ENTRIES 64    /* Maximum cached blocks (32KB total) */

/* =========================================================================
 * Cache API
 * ========================================================================= */

/**
 * Initialize the block cache
 * Must be called before any cache operations
 */
void cache_init(void);

/**
 * Look up a block in the cache
 * 
 * @param prim_id Primary device ID
 * @param scnd_id Secondary device ID
 * @param offset Block offset (block number relative to device)
 * @param buf Buffer to copy data into (if found)
 * @return 1 if found (cache hit), 0 if not found (cache miss)
 */
int cache_lookup(int prim_id, int scnd_id, uint32_t offset, void *buf);

/**
 * Insert a block into the cache
 * If cache is full, evicts the least recently used block
 * 
 * @param prim_id Primary device ID
 * @param scnd_id Secondary device ID
 * @param offset Block offset (block number relative to device)
 * @param data Block data to cache
 * @return 0 on success, -1 on failure
 */
int cache_insert(int prim_id, int scnd_id, uint32_t offset, const void *data);

/**
 * Mark a cached block as dirty (modified)
 * 
 * @param prim_id Primary device ID
 * @param scnd_id Secondary device ID
 * @param offset Block offset (block number relative to device)
 * @return 0 on success, -1 if block not in cache
 */
int cache_mark_dirty(int prim_id, int scnd_id, uint32_t offset);

/**
 * Flush all dirty blocks to their devices
 * Should be called periodically or before shutdown
 * 
 * @return Number of blocks written, or -1 on error
 */
int cache_flush(void);

/**
 * Invalidate (remove) a specific block from cache
 * Writes back if dirty
 * 
 * @param prim_id Primary device ID
 * @param scnd_id Secondary device ID
 * @param offset Block offset (block number relative to device)
 */
void cache_invalidate(int prim_id, int scnd_id, uint32_t offset);

/**
 * Get cache statistics
 * 
 * @param hits Pointer to store hit count (can be NULL)
 * @param misses Pointer to store miss count (can be NULL)
 * @param entries Pointer to store current entry count (can be NULL)
 */
void cache_stats(uint32_t *hits, uint32_t *misses, uint32_t *entries);

#endif /* CACHE_H */