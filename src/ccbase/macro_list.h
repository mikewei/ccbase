/* Copyright (c) 2012-2017, Bin Wei <bin@vip.qq.com>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * The names of its contributors may not be used to endorse or 
 * promote products derived from this software without specific prior 
 * written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#ifndef CCBASE_MACRO_LIST_H_
#define CCBASE_MACRO_LIST_H_

#include "ccbase/common.h"

namespace ccb {

struct ListHead {
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

/*
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

/*
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

/*
 * list_del - deletes entry from list.
 * @entry: the element to delete from the list.
 * Note: list_empty on entry does not return true after this, the entry is in an undefined state.
 */
#define LIST_DEL(entry) do { \
  ccb::ListHead * prev = (entry)->prev; \
  ccb::ListHead * next = (entry)->next; \
  _LIST_DEL(prev, next); \
} while (0)

/*
 * list_del_init - deletes entry from list and reinitialize it.
 * @entry: the element to delete from the list.
 */
#define LIST_DEL_INIT(entry) do { \
  LIST_DEL(entry); \
  INIT_LIST_HEAD(entry); \
} while (0)

/*
 * list_empty - tests whether a list is empty
 * @head: the list to test.
 */
#define LIST_EMPTY(head) ((head)->next == (head))

/*
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

/*
 * list_entry - get the struct for this entry
 * @ptr:    the &struct list_head pointer.
 * @type:   the type of the struct this is embedded in.
 * @member: the name of the list_struct within the struct.
 */
#define LIST_ENTRY(ptr, type, member) \
  ((type *)((char *)(ptr)-(unsigned long)(&((type *)0)->member)))  // NOLINT

/*
 * list_for_each  -  iterate over a list
 * @pos:   the &struct list_head to use as a loop counter.
 * @head:  the head for your list.
 */
#define LIST_FOR_EACH(pos, head) \
  for ((pos) = (head)->next; (pos) != (head); pos = (pos)->next)

/*
 * list_for_each_safe  -  iterate over a list safe against removal of list entry
 * @pos:   the &struct list_head to use as a loop counter.
 * @n:     another &struct list_head to use as temporary storage
 * @head:  the head for your list.
 */
#define LIST_FOR_EACH_SAFE(pos, n, head) \
  for ((pos) = (head)->next, (n) = (pos)->next; (pos) != (head); \
      (pos) = (n), (n) = (pos)->next)

/*
 * list_for_each_prev  -  iterate over a list in reverse order
 * @pos:   the &struct list_head to use as a loop counter.
 * @head:  the head for your list.
 */
#define LIST_FOR_EACH_PREV(pos, head) \
  for ((pos) = (head)->prev; (pos) != (head); (pos) = (pos)->prev)


}  // namespace ccb

#endif  // CCBASE_MACRO_LIST_H_
