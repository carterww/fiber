/* See LICENSE file for copyright and license details. */

#ifndef _FIBER_THREAD_LIST_H
#define _FIBER_THREAD_LIST_H

#include <stddef.h>

#include "fiber/fiber.h"
#include "fiber_internal.h"

struct fiber_thread_list_init_result {
	int error;
	struct fiber_thread *threads_head;
};

/* Important note: There is no free here because each thread is responsible
 * for freeing itself.
 */

/* Allocates threads_number fiber_thread structs.
 * @param threads_number -> The number of fiber_thread struct to allocate.
 * @param malloc -> Allocating function to use. This must not be NULL.
 * @returns -> A struct that contains an error code and pointer to the head
 * of the allocated list. If error = 0, threads_head is a valid pointer to a
 * linked list of fiber_thread structs.
 * @error FBR_ENO_RSC -> malloc returned a NULL pointer.
 */
struct fiber_thread_list_init_result
fiber_thread_list_alloc(tpsize threads_number, malloc_function_t _malloc);

/* Appends the list new to the head.
 * @param head -> Pointer to the pointer of the head to append to. If *head is NULL,
 * *head is set to the list new. If *head is not NULL, new is placed directly after
 * the head.
 * @param new -> Pointer to the head of the fiber_thread list to add. It is
 * appended directly after head.
 */
void fiber_thread_list_add(struct fiber_thread **head,
			   struct fiber_thread *new);

/* Removes a single fiber_thread from the list head.
 * @param head -> Pointer to the pointer of the head. *head can be set to NULL
 * if the last thread is removed.
 * @param thread -> The thread to remove from the list.
 */
void fiber_thread_list_remove(struct fiber_thread **head,
			      const struct fiber_thread *thread);

#endif /* _FIBER_THREAD_LIST_H */
