#include "fiber.h"
#include "src/threading.h"

/** Semaphore functions **/

int fiber_sem_init(fiber_semaphore *sem, unsigned int initial_value)
{
        (void)sem;
        (void)initial_value;
        return 0;
}

int fiber_sem_destroy(fiber_semaphore *sem)
{
        (void)sem;
        return 0;
}

int fiber_sem_wait(fiber_semaphore *sem)
{
        (void)sem;
        return 0;
}

int fiber_sem_trywait(fiber_semaphore *sem)
{
        (void)sem;
        return 0;
}

int fiber_sem_post(fiber_semaphore *sem)
{
        (void)sem;
        return 0;
}

int fiber_sem_getvalue(fiber_semaphore *sem, int *value_out)
{
        (void)sem;
        (void)value_out;
        return 0;
}

/** Mutex functions **/

int fiber_mutex_init(fiber_mutex *mut)
{
        (void)mut;
        return 0;
}

int fiber_mutex_destroy(fiber_mutex *mut)
{
        (void)mut;
        return 0;
}

int fiber_mutex_lock(fiber_mutex *mut)
{
        (void)mut;
        return 0;
}

int fiber_mutex_unlock(fiber_mutex *mut)
{
        (void)mut;
        return 0;
}

/** Thread functions **/

int fiber_thread_create(tid *thread_id, fiber_job_function_t runner, void *arg)
{
        (void)thread_id;
        (void)runner;
        (void)arg;
        return 0;
}

void fiber_thread_exit(void *ret_val)
{
        (void)ret_val;
        return;
}

int fiber_thread_detach(const tid *thread_id)
{
        (void)thread_id;
        return 0;
}

int fiber_thread_join(const tid *thread_id, void **ret_val)
{
        (void)thread_id;
        (void)ret_val;
        return 0;
}

int fiber_thread_cancel_enable(void)
{
        return 0;
}

int fiber_thread_cancel_disable(void)
{
        return 0;
}

int fiber_thread_cancel_type_set(int cancel_type)
{
        (void)cancel_type;
        return 0;
}

int fiber_thread_cancel(const tid *thread_id)
{
        (void)thread_id;
        return 0;
}
