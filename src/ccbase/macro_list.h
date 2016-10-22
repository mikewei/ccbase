#ifndef _MACRO_LIST_H
#define _MACRO_LIST_H

#include "ccbase/common.h"

namespace ccb {

struct ListHead
{
  struct ListHead* next;
  struct ListHead* prev;
};

#define INIT_LIST_HEAD(ptr) do { \
  (ptr)->next = (ptr); (ptr)->prev = (ptr); \
} while (0)

#define LIST_HEAD_INIT(name) { &(name), &(name) }
#define LIST_HEAD(name) \
  ccb::ListHead name = LIST_HEAD_INIT(name)

/*
 * Insert a new entry between two known consecutive entries. 
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
#define _LIST_ADD(node, prev_node, next_node) do { \
  (next_node)->prev = (node); \
  (node)->next = (next_node); \
  (node)->prev = (prev_node); \
  (prev_node)->next = (node); \
} while (0)
  
/**
 * list_add - add a new entry
 * @new:  new entry to be added
 * @head: list head to add it after
 *
 * Insert a new entry after the specified head.
 * This is good for implementing stacks.
 */
#define LIST_ADD(node, head) do { \
  ccb::ListHead * newn = (node); \
  ccb::ListHead * prev = (head); \
  ccb::ListHead * next = (head)->next; \
  _LIST_ADD(newn, prev, next); \
} while (0)

/**
 * list_add_tail - add a new entry
 * @new: new entry to be added
 * @head: list head to add it before
 *
 * Insert a new entry before the specified head.
 * This is useful for implementing queues.
 */
#define LIST_ADD_TAIL(node, head) do { \
  ccb::ListHead * newn = (node); \
  ccb::ListHead * prev = (head)->prev; \
  ccb::ListHead * next = (head); \
  _LIST_ADD(newn, prev, next); \
} while (0)

/*
 * Delete a list entry by making the prev/next entries
 * point to each other.
 *
 * This is only for internal list manipulation where we know
 * the prev/next entries already!
 */
#define _LIST_DEL(prev_node, next_node) do { \
  (next_node)->prev = (prev_node); \
  (prev_node)->next = (next_node); \
} while (0)

/**
 * list_del - deletes entry from list.
 * @entry: the element to delete from the list.
 * Note: list_empty on entry does not return true after this, the entry is in an undefined state.
 */
#define LIST_DEL(entry) do { \
  ccb::ListHead * prev = (entry)->prev; \
  ccb::ListHead * next = (entry)->next; \
  _LIST_DEL(prev, next); \
} while (0)

/**
 * list_del_init - deletes entry from list and reinitialize it.
 * @entry: the element to delete from the list.
 */
#define LIST_DEL_INIT(entry) do { \
  LIST_DEL(entry); \
  INIT_LIST_HEAD(entry); \
} while (0)

/**
 * list_empty - tests whether a list is empty
 * @head: the list to test.
 */
#define LIST_EMPTY(head) ((head)->next == (head))

/**
 * list_splice - join two lists
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 */
#define LIST_SPLICE(list, head) do { \
  ccb::ListHead * first = (list)->next; \
  if (first != (list)) { \
    ccb::ListHead * last = (list)->prev; \
    ccb::ListHead * at = (head)->next; \
    first->prev = (head); \
    last->next = at; \
    st->prev = last; \
  } \
} while (0)

/**
 * list_entry - get the struct for this entry
 * @ptr:    the &struct list_head pointer.
 * @type:   the type of the struct this is embedded in.
 * @member: the name of the list_struct within the struct.
 */
#define LIST_ENTRY(ptr, type, member) \
  ((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))

/**
 * list_for_each  -  iterate over a list
 * @pos:   the &struct list_head to use as a loop counter.
 * @head:  the head for your list.
 */
#define LIST_FOR_EACH(pos, head) \
  for ((pos) = (head)->next; (pos) != (head); pos = (pos)->next)

/**
 * list_for_each_safe  -  iterate over a list safe against removal of list entry
 * @pos:   the &struct list_head to use as a loop counter.
 * @n:     another &struct list_head to use as temporary storage
 * @head:  the head for your list.
 */
#define LIST_FOR_EACH_SAFE(pos, n, head) \
  for ((pos) = (head)->next, (n) = (pos)->next; (pos) != (head); \
      (pos) = (n), (n) = (pos)->next)

/**
 * list_for_each_prev  -  iterate over a list in reverse order
 * @pos:   the &struct list_head to use as a loop counter.
 * @head:  the head for your list.
 */
#define LIST_FOR_EACH_PREV(pos, head) \
  for ((pos) = (head)->prev; (pos) != (head); (pos) = (pos)->prev)


} // namespace ccb

#endif
