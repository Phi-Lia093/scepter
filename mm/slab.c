#include "mm/slab.h"
#include "mm/buddy.h"
#include "lib/list.h"
#include "lib/printk.h"
#include <stdint.h>
#include <stddef.h>

/* =========================================================================
 * Constants
 * ========================================================================= */

#define PAGE_SIZE       4096
#define MAX_SLAB_CACHES 16
#define SLAB_MAGIC      0x534C4142  /* 'SLAB' */

/* =========================================================================
 * Slab Structures
 * ========================================================================= */

/* Free object list node (stored at the start of each free object) */
typedef struct free_obj {
    struct free_obj *next;
} free_obj_t;

/* Slab metadata – stored at the start of each slab page */
typedef struct slab {
    uint32_t    magic;       /* SLAB_MAGIC – identifies this page as a slab  */
    list_head_t node;        /* embedded in slab_cache_t.partial or .full    */
    uint16_t    obj_size;    /* size of each object in this slab             */
    uint16_t    num_objs;    /* total objects                                */
    uint16_t    free_count;  /* currently free objects                       */
    free_obj_t *free_list;   /* head of intrusive free-object list           */
} slab_t;

/* Slab cache – one per object size */
typedef struct {
    uint16_t    obj_size;
    list_head_t partial;     /* slabs with ≥1 free slot                      */
    list_head_t full;        /* fully allocated slabs                        */
} slab_cache_t;

/* =========================================================================
 * Global State
 * ========================================================================= */

static slab_cache_t slab_caches[MAX_SLAB_CACHES];
static int num_caches = 0;

/* =========================================================================
 * Helper Functions
 * ========================================================================= */

/* Round size up to the next power of 2 (minimum 8) */
static uint16_t round_up_pow2(size_t size)
{
    if (size <= 8) return 8;
    uint16_t pow2 = 8;
    while (pow2 < size && pow2 < 2048)
        pow2 <<= 1;
    return pow2;
}

/* Return the slab_t for the page that contains addr */
static inline slab_t *addr_to_slab(void *addr)
{
    return (slab_t *)((uint32_t)addr & ~(uint32_t)(PAGE_SIZE - 1));
}

static inline int is_slab_page(void *addr)
{
    return addr_to_slab(addr)->magic == SLAB_MAGIC;
}

/* Allocate a new page and format it as a slab */
static slab_t *create_slab(uint16_t obj_size)
{
    void *page = page_alloc(PAGE_SIZE);
    if (!page)
        return NULL;

    slab_t *slab    = (slab_t *)page;
    slab->magic     = SLAB_MAGIC;
    slab->obj_size  = obj_size;

    uint32_t usable = PAGE_SIZE - sizeof(slab_t);
    slab->num_objs  = (uint16_t)(usable / obj_size);
    slab->free_count = slab->num_objs;

    /* Build the intrusive free list (back to front so first alloc is front) */
    uint8_t *obj_area = (uint8_t *)page + sizeof(slab_t);
    slab->free_list = NULL;
    for (int i = slab->num_objs - 1; i >= 0; i--) {
        free_obj_t *obj = (free_obj_t *)(obj_area + i * obj_size);
        obj->next = slab->free_list;
        slab->free_list = obj;
    }

    INIT_LIST_HEAD(&slab->node);
    return slab;
}

/* Find or create the cache for obj_size */
static slab_cache_t *get_cache(uint16_t obj_size)
{
    for (int i = 0; i < num_caches; i++) {
        if (slab_caches[i].obj_size == obj_size)
            return &slab_caches[i];
    }

    if (num_caches >= MAX_SLAB_CACHES)
        return NULL;

    slab_cache_t *cache = &slab_caches[num_caches++];
    cache->obj_size = obj_size;
    INIT_LIST_HEAD(&cache->partial);
    INIT_LIST_HEAD(&cache->full);
    return cache;
}

/* =========================================================================
 * Public API - Initialization
 * ========================================================================= */

void slab_init(void)
{
    num_caches = 0;

    static const uint16_t sizes[] = {8, 16, 32, 64, 128, 256, 512, 1024, 2048};
    for (int i = 0; i < 9; i++)
        add_slab(sizes[i]);
}

/* =========================================================================
 * Public API - Allocation
 * ========================================================================= */

void *kalloc(size_t size)
{
    if (size == 0)
        return NULL;

    /* Large allocations go directly to the page allocator */
    if (size > 2048)
        return page_alloc(size);

    uint16_t obj_size = round_up_pow2(size);
    slab_cache_t *cache = get_cache(obj_size);
    if (!cache)
        return NULL;

    /* Take the first slab with a free slot */
    slab_t *slab = NULL;
    if (!list_empty(&cache->partial))
        slab = list_first_entry(&cache->partial, slab_t, node);

    if (!slab) {
        /* No partial slab – create a fresh one */
        slab = create_slab(obj_size);
        if (!slab)
            return NULL;
        list_add(&slab->node, &cache->partial);
    }

    /* Allocate one object from the free list */
    free_obj_t *obj = slab->free_list;
    slab->free_list = obj->next;
    slab->free_count--;

    /* Move to full list if completely allocated */
    if (slab->free_count == 0) {
        list_move(&slab->node, &cache->full);
    }

    return (void *)obj;
}

/* =========================================================================
 * Public API - Deallocation
 * ========================================================================= */

void kfree(void *addr)
{
    if (!addr)
        return;

    if (!is_slab_page(addr)) {
        /* Not a slab page – return directly to the page allocator */
        page_free(addr);
        return;
    }

    slab_t *slab    = addr_to_slab(addr);
    uint16_t obj_size = slab->obj_size;

    int was_full = (slab->free_count == 0);

    free_obj_t *obj = (free_obj_t *)addr;
    obj->next       = slab->free_list;
    slab->free_list = obj;
    slab->free_count++;

    slab_cache_t *cache = get_cache(obj_size);
    if (!cache)
        return;

    /* If the slab was full, move it back to the partial list */
    if (was_full)
        list_move(&slab->node, &cache->partial);

    /* If the slab is now fully empty, return it to the page allocator
     * (but only if there are other slabs available) */
    if (slab->free_count == slab->num_objs) {
        /* Check: is this the only slab in the partial list? */
        int only_slab = (cache->partial.next == &slab->node &&
                         cache->partial.prev == &slab->node);
        if (!only_slab) {
            list_del(&slab->node);
            page_free((void *)slab);
        }
    }
}

/* =========================================================================
 * Public API - Cache Management
 * ========================================================================= */

int add_slab(size_t obj_size)
{
    uint16_t size = round_up_pow2(obj_size);
    if (size > 2048)
        return -1;

    /* get_cache creates the cache if it doesn't exist */
    return get_cache(size) ? 0 : -1;
}

/* =========================================================================
 * Public API - Statistics
 * ========================================================================= */

void slab_stats(uint32_t *total_allocated, uint32_t *total_free)
{
    uint32_t allocated = 0, free = 0;

    for (int i = 0; i < num_caches; i++) {
        slab_cache_t *cache = &slab_caches[i];
        slab_t *slab;

        list_for_each_entry(slab, &cache->partial, node) {
            allocated += (uint32_t)(slab->num_objs - slab->free_count) * slab->obj_size;
            free      += (uint32_t) slab->free_count                   * slab->obj_size;
        }
        list_for_each_entry(slab, &cache->full, node) {
            allocated += (uint32_t) slab->num_objs * slab->obj_size;
        }
    }

    if (total_allocated) *total_allocated = allocated;
    if (total_free)      *total_free      = free;
}