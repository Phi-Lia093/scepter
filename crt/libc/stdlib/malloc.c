/* ============================================================================
 * malloc / calloc / realloc / free
 *
 * A simple first-fit allocator over the brk heap:
 *   - every block (free or busy) carries a header and is linked in the
 *     free list, which is actually the chain of ALL blocks in address order
 *   - allocations are split when the free block is much larger
 *   - free() coalesces with the next and previous neighbours
 * ============================================================================ */

#include <stddef.h>

typedef struct block {
    size_t      size;     /* usable data size (excluding header) */
    struct block *next;   /* next block in address order        */
    int         is_free;
} block_t;

#define BLOCK_SIZE sizeof(block_t)
#define ALIGN8(x) (((x) + 7) & ~(size_t)7)

static block_t *free_list = NULL;   /* head of the whole chain */
static void    *heap_start = NULL;
static void    *heap_end   = NULL;

extern int brk(void *addr);

/* ============================================================================
 * sbrk: extend the heap via brk()
 * ============================================================================ */

static void *sbrk(size_t increment)
{
    if (!heap_start) {
        heap_start = (void *)brk(0);
        heap_end   = heap_start;
    }
    void *old = heap_end;
    if (brk((char *)heap_end + increment) < 0)
        return (void *)-1;
    heap_end = (char *)heap_end + increment;
    return old;
}

/* ============================================================================
 * Find the first free block big enough.  prev_out receives the block before
 * it in the chain (or NULL if it is the head).
 * ============================================================================ */

static block_t *find_free(size_t size, block_t **prev_out)
{
    block_t *prev = NULL;
    for (block_t *b = free_list; b; b = b->next) {
        if (b->is_free && b->size >= size) {
            *prev_out = prev;
            return b;
        }
        prev = b;
    }
    *prev_out = prev;
    return NULL;
}

/* ============================================================================
 * Append a fresh block at the end of the heap.
 * ============================================================================ */

static block_t *request_space(block_t *last, size_t size)
{
    block_t *b = (block_t *)sbrk(BLOCK_SIZE + size);
    if (b == (void *)-1)
        return NULL;
    b->size    = size;
    b->next    = NULL;
    b->is_free = 0;
    if (last)
        last->next = b;
    return b;
}

/* ============================================================================
 * malloc
 * ============================================================================ */

void *malloc(size_t size)
{
    if (size == 0)
        size = 1;
    size = ALIGN8(size);

    block_t *b;

    if (!free_list) {
        b = request_space(NULL, size);
        if (!b)
            return NULL;
        free_list = b;
        return (void *)(b + 1);
    }

    block_t *prev = NULL;
    b = find_free(size, &prev);
    if (!b) {
        b = request_space(prev, size);
        if (!b)
            return NULL;
        return (void *)(b + 1);
    }

    /* Split if there is room for a new header + a useful payload */
    if (b->size >= size + BLOCK_SIZE + 16) {
        block_t *rest = (block_t *)((char *)(b + 1) + size);
        rest->size    = b->size - size - BLOCK_SIZE;
        rest->next    = b->next;
        rest->is_free = 1;
        b->next       = rest;
        b->size       = size;
    }

    b->is_free = 0;
    return (void *)(b + 1);
}

/* ============================================================================
 * free (with coalescing)
 * ============================================================================ */

void free(void *ptr)
{
    if (!ptr)
        return;

    block_t *b = (block_t *)ptr - 1;
    b->is_free = 1;

    /* Coalesce with the next block */
    if (b->next && b->next->is_free) {
        b->size += BLOCK_SIZE + b->next->size;
        b->next  = b->next->next;
    }

    /* Coalesce with the previous block (walk from the head) */
    if (b != free_list) {
        block_t *prev = free_list;
        while (prev && prev->next != b)
            prev = prev->next;
        if (prev && prev->is_free) {
            prev->size += BLOCK_SIZE + b->size;
            prev->next  = b->next;
        }
    }
}

/* ============================================================================
 * calloc
 * ============================================================================ */

void *calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    if (nmemb != 0 && total / nmemb != size)
        return NULL;   /* overflow */

    void *ptr = malloc(total ? total : 1);
    if (!ptr)
        return NULL;

    char *p = (char *)ptr;
    for (size_t i = 0; i < total; i++)
        p[i] = 0;
    return ptr;
}

/* ============================================================================
 * realloc
 * ============================================================================ */

void *realloc(void *ptr, size_t size)
{
    if (!ptr)
        return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }

    block_t *b = (block_t *)ptr - 1;
    size = ALIGN8(size);

    /* Can we grow in place into a following free block? */
    if (size > b->size && b->next && b->next->is_free &&
        b->size + BLOCK_SIZE + b->next->size >= size) {
        size_t avail = b->size + BLOCK_SIZE + b->next->size;
        /* absorb the neighbour */
        b->size = avail;
        b->next  = b->next->next;
        if (avail >= size + BLOCK_SIZE + 16) {
            block_t *rest = (block_t *)((char *)(b + 1) + size);
            rest->size    = avail - size - BLOCK_SIZE;
            rest->next    = b->next;
            rest->is_free = 1;
            b->next       = rest;
            b->size       = size;
        }
        return ptr;
    }

    if (b->size >= size)
        return ptr;

    void *new_ptr = malloc(size);
    if (!new_ptr)
        return NULL;

    char *src = (char *)ptr;
    char *dst = (char *)new_ptr;
    size_t copy = (b->size < size) ? b->size : size;
    for (size_t i = 0; i < copy; i++)
        dst[i] = src[i];

    free(ptr);
    return new_ptr;
}
