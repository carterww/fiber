#include "fiber.c"
#include "xtal.h"

#define default_fiber_thread(j)                            \
	{                                                  \
		.job_id = j, .next = NULL, .thread_id = 0, \
	}

TEST(allocate_threads)
{
	struct fiber_thread *head = NULL;
	int err = thread_ll_alloc_n(&head, 5, malloc);
	ASSERT_EQUAL_INT(0, err);
	struct fiber_thread *curr = head;
	for (int i = 0; i < 5; i++) {
		ASSERT_NOT_NULL(curr);
		curr = curr->next;
	}
	ASSERT_NULL(curr); // Ensure last thread points to NULL
	thread_ll_free(head, free);
}

TEST(add_threads_empty_ll)
{
	struct fiber_thread *head = NULL;
	struct fiber_thread first = default_fiber_thread(1);
	thread_ll_add(&head, &first);
	ASSERT_NOT_NULL(head);
	ASSERT_EQUAL_PTR(&first, head);
}

TEST(add_threads_ll)
{
	struct fiber_thread head_next = default_fiber_thread(2);
	struct fiber_thread head = default_fiber_thread(1);
	head.next = &head_next;
	struct fiber_thread *head_ptr = &head;

	struct fiber_thread new_next = default_fiber_thread(3);
	struct fiber_thread new = default_fiber_thread(4);
	new.next = &new_next;

	thread_ll_add(&head_ptr, &new);

	struct fiber_thread *curr = head_ptr;
	for (int i = 0; i < 4; i++) {
		ASSERT_NOT_NULL(curr);
		if (curr->job_id > 4 || curr->job_id < 1) {
			FAIL("We encountered some memory that was likely "
			     "initialized.")
		}
		curr = curr->next;
	}
	ASSERT_NULL(curr);
}

TEST(remove_thread_head)
{
	struct fiber_thread head = default_fiber_thread(1);
	struct fiber_thread *head_ptr = &head;
	thread_ll_remove(&head_ptr, head_ptr);
	ASSERT_NULL(head_ptr);
}

TEST(remove_thread)
{
	struct fiber_thread head = default_fiber_thread(1);
	struct fiber_thread next = default_fiber_thread(2);
	struct fiber_thread next_next = default_fiber_thread(3);
	struct fiber_thread *head_ptr = &head;
	head.next = &next;
	next.next = &next_next;

	thread_ll_remove(&head_ptr, &next);
	ASSERT_NOT_NULL(head_ptr);
	ASSERT_NOT_NULL(head_ptr->next);
	ASSERT_EQUAL_PTR(head_ptr->next, &next_next);
}

int main()
{
	run_tests();
	return 0;
}
