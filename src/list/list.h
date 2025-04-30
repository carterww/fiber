/* See LICENSE file for copyright and license details. */

#ifndef _FIBER_LIST_H
#define _FIBER_LIST_H

#include <stddef.h>

/* Intrusive singly linked list node */
struct list_node {
	struct list_node *next;
};

/* Gets the pointer to the container of the list node.
 * @param node_ptr -> Pointer to the node inside the containing struct.
 * @param container_type -> Type name of the containing struct.
 * @param container_node_member -> Name of the list node variable in the
 * containing struct.
 * 
 * Example:
 * struct container_struct {
 *     unsigned long a;
 *     struct list_node list;
 * };
 * struct container_struct first = { ... };
 * ...
 * struct container_struct *next = list_entry(first.list.next, struct container_struct,
 *                                            node);
 */
#define list_entry(node_ptr, container_type, container_node_member) \
	((container_type *)((unsigned long)(node_ptr) -             \
			    offsetof(container_type, container_node_member)))

/* Returns 0 if the list is empty, non-zero if the list has at least one
 * member.
 *
 * Example:
 * struct container_struct {
 *     unsigned long a;
 *     struct list_node list;
 * };
 * struct container_struct first = { ... };
 * ...
 * int is_empty = list_empty(&first.list);
 */
#define list_empty(node_ptr) ((node_ptr)->next == (node_ptr))

#define list_for_each(node_ptr, container_ptr, container_type,               \
		      container_node_member)                                 \
	for (container_ptr = list_entry((node_ptr)->next, container_type,    \
					container_node_member);              \
	     &((container_ptr)->container_node_member) != (node_ptr);        \
	     container_ptr =                                                 \
		     list_entry((container_ptr)->container_node_member.next, \
				container_type, container_node_member))

static void list_init(struct list_node *node)
{
	node->next = node;
}

static struct list_node *list_find_prev(struct list_node *node)
{
	struct list_node *prev = node;
	struct list_node *curr = node->next;
	while (curr != node) {
		prev = curr;
		curr = curr->next;
	}
	return prev;
}

static void list_add(struct list_node *head, struct list_node *new_node)
{
	struct list_node *next = head->next;
	head->next = new_node;
	new_node->next = next;
}

static void list_splice(struct list_node *head, struct list_node *list)
{
	struct list_node *list_last = list_find_prev(list);
	struct list_node *next = head->next;

	head->next = list;
	list_last->next = next;
}

static void list_remove_next(struct list_node *node)
{
	node->next = node->next->next;
}

static void list_remove(struct list_node *node)
{
	struct list_node *prev = list_find_prev(node);
	list_remove_next(prev);
}

#endif /* _FIBER_LIST_H */
