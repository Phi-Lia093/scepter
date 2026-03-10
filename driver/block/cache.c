#include "driver/block/cache.h"
#include "driver/driver.h"
#include "mm/slab.h"
#include "lib/printk.h"
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * LRU Cache Entry Structure
 * ========================================================================= */

typedef struct cache_entry {
    int prim_id;                    /* Device primary ID */
    int scnd_id;                    /* Device secondary ID */
    uint32_t offset;                /* Block offset (block number) */
    uint8_t *data;                  /* Block data (CACHE_BLOCK_SIZE bytes) */
    int dirty;                      /* 1 if modified, needs write-back */
    struct cache_entry *prev;       /* Previous in LRU list (more recent) */
    struct cache_entry *next;       /* Next in LRU list (less recent) */
} cache_entry_t;

/* =========================================================================
 * Global Cache State
 * ========================================================================= */

typedef struct {
    cache_entry_t *head;            /* Most recently used */
    cache_entry_t *tail;            /* Least recently used */
    uint32_t num_entries;           /* Current number of entries */
    uint32_t hits;                  /* Cache hit count */
    uint32_t misses;                /* Cache miss count */
} lru_cache_t;

static lru_cache_t cache;

/* =========================================================================
 * Internal Helper Functions
 * ========================================================================= */

/* Copy memory (simple memcpy implementation) */
static void cache_memcpy(void *dest, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dest;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
}

/* Remove entry from LRU list (doesn't free it) */
static void lru_remove(cache_entry_t *entry)
{
    if (entry->prev) {
        entry->prev->next = entry->next;
    } else {
        cache.head = entry->next;
    }
    
    if (entry->next) {
        entry->next->prev = entry->prev;
    } else {
        cache.tail = entry->prev;
    }
    
    entry->prev = NULL;
    entry->next = NULL;
}

/* Move entry to head of LRU list (mark as most recently used) */
static void lru_move_to_head(cache_entry_t *entry)
{
    if (entry == cache.head) {
        return;  /* Already at head */
    }
    
    /* Remove from current position */
    lru_remove(entry);
    
    /* Insert at head */
    entry->next = cache.head;
    entry->prev = NULL;
    
    if (cache.head) {
        cache.head->prev = entry;
    }
    cache.head = entry;
    
    if (!cache.tail) {
        cache.tail = entry;
    }
}

/* Add new entry to head of LRU list */
static void lru_add_to_head(cache_entry_t *entry)
{
    entry->next = cache.head;
    entry->prev = NULL;
    
    if (cache.head) {
        cache.head->prev = entry;
    }
    cache.head = entry;
    
    if (!cache.tail) {
        cache.tail = entry;
    }
}

/* Find entry in cache by device and block offset */
static cache_entry_t *find_entry(int prim_id, int scnd_id, uint32_t offset)
{
    cache_entry_t *entry = cache.head;
    while (entry) {
        if (entry->prim_id == prim_id && entry->scnd_id == scnd_id && entry->offset == offset) {
            return entry;
        }
        entry = entry->next;
    }
    return NULL;
}

/* Write back a dirty entry to the device */
static int writeback_entry(cache_entry_t *entry)
{
    if (!entry->dirty) {
        return 0;  /* Not dirty, nothing to write */
    }
    
    /* Write block back to device (1 block at offset) */
    int ret = bwrite(entry->prim_id, entry->scnd_id, entry->data, entry->offset, 1);
    if (ret > 0) {
        entry->dirty = 0;
        return 0;
    }
    
    return -1;
}

/* Evict the least recently used entry */
static int evict_lru(void)
{
    if (!cache.tail) {
        return -1;  /* Cache is empty */
    }
    
    cache_entry_t *victim = cache.tail;
    
    /* Write back if dirty */
    if (victim->dirty) {
        if (writeback_entry(victim) < 0) {
            printk("[CACHE] Warning: Failed to write back dirty block\n");
        }
    }
    
    /* Remove from LRU list */
    lru_remove(victim);
    
    /* Free data and entry */
    kfree(victim->data);
    kfree(victim);
    
    cache.num_entries--;
    
    return 0;
}

/* =========================================================================
 * Public API Implementation
 * ========================================================================= */

void cache_init(void)
{
    cache.head = NULL;
    cache.tail = NULL;
    cache.num_entries = 0;
    cache.hits = 0;
    cache.misses = 0;
    
    printk("[CACHE] Initialized LRU cache: %d entries x %d bytes = %d KB\n",
           CACHE_MAX_ENTRIES, CACHE_BLOCK_SIZE, 
           (CACHE_MAX_ENTRIES * CACHE_BLOCK_SIZE) / 1024);
}

int cache_lookup(int prim_id, int scnd_id, uint32_t offset, void *buf)
{
    cache_entry_t *entry = find_entry(prim_id, scnd_id, offset);
    
    if (entry) {
        /* Cache hit! */
        cache.hits++;
        
        /* Copy data to buffer */
        cache_memcpy(buf, entry->data, CACHE_BLOCK_SIZE);
        
        /* Move to head (mark as recently used) */
        lru_move_to_head(entry);
        
        return 1;
    }
    
    /* Cache miss */
    cache.misses++;
    return 0;
}

int cache_insert(int prim_id, int scnd_id, uint32_t offset, const void *data)
{
    /* Check if already in cache */
    cache_entry_t *existing = find_entry(prim_id, scnd_id, offset);
    if (existing) {
        /* Update existing entry */
        cache_memcpy(existing->data, data, CACHE_BLOCK_SIZE);
        lru_move_to_head(existing);
        return 0;
    }
    
    /* Evict if cache is full */
    if (cache.num_entries >= CACHE_MAX_ENTRIES) {
        if (evict_lru() < 0) {
            return -1;
        }
    }
    
    /* Allocate new entry */
    cache_entry_t *entry = (cache_entry_t *)kalloc(sizeof(cache_entry_t));
    if (!entry) {
        return -1;
    }
    
    /* Allocate data buffer */
    entry->data = (uint8_t *)kalloc(CACHE_BLOCK_SIZE);
    if (!entry->data) {
        kfree(entry);
        return -1;
    }
    
    /* Initialize entry */
    entry->prim_id = prim_id;
    entry->scnd_id = scnd_id;
    entry->offset = offset;
    entry->dirty = 0;
    cache_memcpy(entry->data, data, CACHE_BLOCK_SIZE);
    
    /* Add to head of LRU list */
    lru_add_to_head(entry);
    cache.num_entries++;
    
    return 0;
}

int cache_mark_dirty(int prim_id, int scnd_id, uint32_t offset)
{
    cache_entry_t *entry = find_entry(prim_id, scnd_id, offset);
    if (!entry) {
        return -1;
    }
    
    entry->dirty = 1;
    lru_move_to_head(entry);
    
    return 0;
}

int cache_flush(void)
{
    int written = 0;
    cache_entry_t *entry = cache.head;
    
    while (entry) {
        if (entry->dirty) {
            if (writeback_entry(entry) == 0) {
                written++;
            }
        }
        entry = entry->next;
    }
    
    return written;
}

void cache_invalidate(int prim_id, int scnd_id, uint32_t offset)
{
    cache_entry_t *entry = find_entry(prim_id, scnd_id, offset);
    if (!entry) {
        return;
    }
    
    /* Write back if dirty */
    if (entry->dirty) {
        writeback_entry(entry);
    }
    
    /* Remove from LRU list */
    lru_remove(entry);
    
    /* Free memory */
    kfree(entry->data);
    kfree(entry);
    
    cache.num_entries--;
}

void cache_stats(uint32_t *hits, uint32_t *misses, uint32_t *entries)
{
    if (hits) *hits = cache.hits;
    if (misses) *misses = cache.misses;
    if (entries) *entries = cache.num_entries;
}