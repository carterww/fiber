#ifndef _FIBER_TEST_QUEUE_FIFO_VALIDATE_H
#define _FIBER_TEST_QUEUE_FIFO_VALIDATE_H

#include "test/unity.h"

#include "fiber.h"
#include "fiber_fifo.h"
#include "src/queue/fifo_internal.h"
#include "src/threading.h"

static void test_queue_sem(fiber_semaphore *sem, qsize expected_val)
{
        int sem_val;
        int sem_res;

        sem_res = fiber_sem_getvalue(sem, &sem_val);
        TEST_ASSERT_EQUAL(0, sem_res);
        TEST_ASSERT_EQUAL(expected_val, sem_val);

        if (expected_val <= 0) {
                sem_res = fiber_sem_trywait(sem);
                TEST_ASSERT_EQUAL(sem_res, FBR_EAGAIN);
        } else {
                sem_res = fiber_sem_trywait(sem);
                TEST_ASSERT_EQUAL(0, sem_res);
                sem_res = fiber_sem_getvalue(sem, &sem_val);
                TEST_ASSERT_EQUAL(0, sem_res);
                TEST_ASSERT_EQUAL(expected_val - 1, sem_val);
        }
}

static void test_queue_mutex(fiber_mutex *mut)
{
        int mut_res;
        
        mut_res = fiber_mutex_lock(mut);
        TEST_ASSERT_EQUAL(0, mut_res);
        mut_res = fiber_mutex_unlock(mut);
        TEST_ASSERT_EQUAL(0, mut_res);
}

static void validate_queue(struct fiber_fifo_jq *jq, qsize queue_length, free_function_t _free)
{
        test_queue_sem(&jq->void_num, queue_length);
        test_queue_sem(&jq->jobs_num, 0);
        TEST_ASSERT_NOT_NULL(jq->jobs);
        TEST_ASSERT_EQUAL(queue_length, jq->capacity);
        test_queue_mutex(&jq->head_lock);
        TEST_ASSERT_EQUAL(0, jq->head);
        test_queue_mutex(&jq->tail_lock);
        TEST_ASSERT_EQUAL(0, jq->tail);

        TEST_ASSERT_EQUAL(_free, jq->free);
}


#endif /* _FIBER_TEST_QUEUE_FIFO_VALIDATE_H */
