#ifndef LIB_LIST_H
#define LIB_LIST_H

#include <stddef.h>

/* =========================================================================
 * Generic Intrusive Doubly-Linked List
 *
 * Usage pattern (Linux kernel style):
 *
 *   // 1. Embed list_head_t in your struct:
 *   typedef struct my_obj {
 *       int value;
 *       list_head_t node;
 *   } my_obj_t;
 *
 *   // 2. Declare a sentinel head:
 *   static LIST_HEAD(my_list);
 *
 *   // 3. Add objects:
 *   my_obj_t *obj = kalloc(sizeof(*obj));
 *   list_add_tail(&obj->node, &my_list);
 *
 *   // 4. Iterate:
 *   my_obj_t *pos;
 *   list_for_each_entry(pos, &my_list, node) {
 *       // use pos->value
 *   }
 *
 *   // 5. Delete while iterating (safe version):
 *   my_obj_t *pos, *tmp;
 *   list_for_each_entry_safe(pos, tmp, &my_list, node) {
 *       list_del(&pos->node);
 *       kfree(pos);
 *   }
 * ========================================================================= */

typedef struct list_head {
    struct list_head *next;
    struct list_head *prev;
} list_head_t;

/* =========================================================================
 * Initialisation
 * ========================================================================= */

/* Static initialiser – for global/static list heads */
#define LIST_HEAD_INIT(name)  { &(name), &(name) }

/* Declare and initialise a list head in one step */
#define LIST_HEAD(name)  list_head_t name = LIST_HEAD_INIT(name)

/* Runtime initialiser – point the sentinel to itself (empty list) */
static inline void INIT_LIST_HEAD(list_head_t *h)
{
    h->next = h;
    h->prev = h;
}

/* =========================================================================
 * Predicates
 * ========================================================================= */

static inline int list_empty(const list_head_t *head)
{
    return head->next == head;
}

/* =========================================================================
 * Core Insert / Remove (internal helpers)
 * ========================================================================= */

static inline void __list_add(list_head_t *new,
                               list_head_t *prev,
                               list_head_t *next)
{
    next->prev = new;
    new->next  = next;
    new->prev  = prev;
    prev->next = new;
}

static inline void __list_del(list_head_t *prev, list_head_t *next)
{
    next->prev = prev;
    prev->next = next;
}

/* =========================================================================
 * Public Insert / Remove
 * ========================================================================= */

/* Insert NEW immediately after HEAD (prepend / push-front) */
static inline void list_add(list_head_t *new, list_head_t *head)
{
    __list_add(new, head, head->next);
}

/* Insert NEW immediately before HEAD (append / push-back) */
static inline void list_add_tail(list_head_t *new, list_head_t *head)
{
    __list_add(new, head->prev, head);
}

/* Remove ENTRY from its list (leaves entry->next/prev dangling) */
static inline void list_del(list_head_t *entry)
{
    __list_del(entry->prev, entry->next);
    entry->next = NULL;
    entry->prev = NULL;
}

/* Remove ENTRY from its list and re-insert at the front of HEAD */
static inline void list_move(list_head_t *entry, list_head_t *head)
{
    __list_del(entry->prev, entry->next);
    __list_add(entry, head, head->next);
}

/* Remove ENTRY from its list and re-insert at the tail of HEAD */
static inline void list_move_tail(list_head_t *entry, list_head_t *head)
{
    __list_del(entry->prev, entry->next);
    __list_add(entry, head->prev, head);
}

/* =========================================================================
 * container_of / list_entry
 * ========================================================================= */

#ifndef offsetof
#define offsetof(type, member)  ((size_t)&((type *)0)->member)
#endif

/**
 * container_of - get the enclosing struct from a member pointer
 * @ptr:    pointer to the embedded member
 * @type:   type of the enclosing struct
 * @member: name of the member within the struct
 */
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))

/**
 * list_entry - get the struct that embeds the given list_head_t
 */
#define list_entry(ptr, type, member) \
    container_of(ptr, type, member)

/**
 * list_first_entry - get the first element of a non-empty list
 */
#define list_first_entry(head, type, member) \
    list_entry((head)->next, type, member)

/**
 * list_last_entry - get the last element of a non-empty list
 */
#define list_last_entry(head, type, member) \
    list_entry((head)->prev, type, member)

/* =========================================================================
 * Iteration Macros
 * ========================================================================= */

/**
 * list_for_each - iterate over raw list_head_t pointers
 * @pos:  loop cursor (list_head_t *)
 * @head: list sentinel head
 */
#define list_for_each(pos, head) \
    for ((pos) = (head)->next; (pos) != (head); (pos) = (pos)->next)

/**
 * list_for_each_safe - iterate, safe against removal of current entry
 * @pos:  loop cursor (list_head_t *)
 * @n:    temporary storage (list_head_t *)
 * @head: list sentinel head
 */
#define list_for_each_safe(pos, n, head) \
    for ((pos) = (head)->next, (n) = (pos)->next; \
         (pos) != (head); \
         (pos) = (n), (n) = (pos)->next)

/**
 * list_for_each_entry - iterate over structs that embed list_head_t
 * @pos:    loop cursor (pointer to enclosing type)
 * @head:   list sentinel head
 * @member: name of the list_head_t member within the enclosing struct
 */
#define list_for_each_entry(pos, head, member)                          \
    for ((pos) = list_entry((head)->next, __typeof__(*(pos)), member);  \
         &(pos)->member != (head);                                      \
         (pos) = list_entry((pos)->member.next, __typeof__(*(pos)), member))

/**
 * list_for_each_entry_safe - iterate, safe against removal of current entry
 * @pos:    loop cursor (pointer to enclosing type)
 * @n:      temporary (pointer to same type as pos)
 * @head:   list sentinel head
 * @member: name of the list_head_t member within the enclosing struct
 */
#define list_for_each_entry_safe(pos, n, head, member)                  \
    for ((pos) = list_entry((head)->next, __typeof__(*(pos)), member),  \
         (n)   = list_entry((pos)->member.next, __typeof__(*(pos)), member); \
         &(pos)->member != (head);                                      \
         (pos) = (n),                                                   \
         (n)   = list_entry((n)->member.next, __typeof__(*(pos)), member))

#endif /* LIB_LIST_H */