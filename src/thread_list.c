/* See LICENSE file for copyright and license details. */

#include <stddef.h>

#include "fiber.h"
#include "fiber_internal.h"
#include "thread_list.h"
#include "utils.h"

struct fiber_thread_list_init_result
fiber_thread_list_alloc(tpsize threads_number, malloc_function_t _malloc)
{
	struct fiber_thread_list_init_result res;
	struct fiber_thread *curr;
	tpsize i;

	fiber_assert(threads_number > 0);
	fiber_assert(_malloc != NULL);

	res.threads_head = _malloc(sizeof(*res.threads_head));
	if (res.threads_head == NULL) {
		res.error = FBR_ENOMEM;
		return res;
	}
	curr = res.threads_head;
	for (i = 1; i < threads_number; ++i) {
		curr->next = _malloc(sizeof(*curr));
		if (curr->next == NULL) {
			res.error = FBR_ENOMEM;
			return res;
		}
		curr = curr->next;
	}
	curr->next = NULL;
	res.error = 0;
	return res;
}

void fiber_thread_list_add(struct fiber_thread **head, struct fiber_thread *new)
{
	struct fiber_thread *next;
	fiber_assert(new != NULL);
	if (*head == NULL) {
		*head = new;
		return;
	}
	next = (*head)->next;
	(*head)->next = new;
	while (new->next != NULL) {
		new = new->next;
	}
	new->next = next;
}

void fiber_thread_list_remove(struct fiber_thread **head,
			      struct fiber_thread *thread)
{
	struct fiber_thread *curr;
	struct fiber_thread *prev;

	fiber_assert(*head != NULL);
	fiber_assert(thread != NULL);
	curr = (*head)->next;
	if (*head == thread) {
		*head = curr;
		return;
	}
	prev = *head;
	while (curr != NULL) {
		if (curr != thread) {
			prev = curr;
			curr = curr->next;
			continue;
		}
		prev->next = curr->next;
		return;
	}
}
