#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdlib.h>

#include "fifo_job_queue.h"
#include "job_queue.h"

static int __fifo_length_semaphores(struct fifo_jq *fq, qsize *out);

// TODO: Need to check for error code in all mutex and semaphore stuff!

int fiber_queue_fifo_init(void **queue, qsize capacity) {
  if (queue == NULL || *queue == NULL) {
    return EINVAL;
  }
  int error_code = 0;
  struct fifo_jq *fq = malloc(sizeof(*fq));
  if (fq == NULL) {
    error_code = errno;
    goto err;
  }
  struct fiber_job *jobs = malloc(capacity * sizeof(*jobs));
  if (jobs == NULL) {
    error_code = errno;
    goto err;
  }
  int mutex_res = pthread_mutex_init(&fq->lock, NULL);
  if (mutex_res != 0) {
    error_code = mutex_res;
    goto err;
  }
  int sem_void_res = sem_init(&fq->void_num, 0, capacity);
  if (sem_void_res != 0) {
    error_code = errno;
    goto err;
  }
  int sem_jobs_res = sem_init(&fq->jobs_num, 0, 0);
  if (sem_jobs_res != 0) {
    error_code = errno;
    goto err;
  }

  fq->jobs = jobs;
  fq->head = 0;
  fq->tail = 0;
  fq->capacity = capacity;
  *queue = fq;

  return 0;
err:
  if (fq != NULL)
    free(fq);
  if (jobs != NULL)
    free(jobs);
  if (mutex_res == 0)
    pthread_mutex_destroy(&fq->lock);
  if (sem_void_res == 0)
    sem_destroy(&fq->void_num);
  if (sem_jobs_res == 0)
    sem_destroy(&fq->jobs_num);
  return error_code;
}

int fiber_queue_fifo_push(void *queue, struct fiber_job *job, uint32_t flags) {
  if (job == NULL || queue == NULL) {
    return EINVAL;
  }

  struct fifo_jq *fq = (struct fifo_jq *)queue;
  // Decrement semaphore
  if (flags & FIBER_BLOCK) {
    while (sem_wait(&fq->void_num) == -1 && errno == EINTR)
      ;
  } else {
    int try_res = sem_trywait(&fq->void_num);
    if (try_res == -1) {
      return EAGAIN;
    }
  }

  int lock_res = pthread_mutex_lock(&fq->lock);
  if (lock_res != 0 || lock_res != EDEADLK) {
    return lock_res;
  }
  // Critical section
  fq->jobs[fq->tail] = *job;
  fq->tail = (fq->tail + 1) % fq->capacity;
  pthread_mutex_unlock(&fq->lock);
  lock_res = sem_post(&fq->jobs_num);
  if (lock_res != 0) {
    // TODO: We should kill here in debug because the possible errors
    // are not a valid semaphore and overflow
    return lock_res;
  }
  return 0;
}

int fiber_queue_fifo_pop(void *queue, struct fiber_job *buffer,
                         uint32_t flags) {
  if (buffer == NULL || queue == NULL) {
    return EINVAL;
  }

  struct fifo_jq *fq = (struct fifo_jq *)queue;
  if (flags & FIBER_BLOCK) {
    while (sem_wait(&fq->jobs_num) == -1) {
      if (errno == EINTR) {
        return EINTR; 
      }
    }
  } else {
    int try_res = sem_trywait(&fq->jobs_num);
    if (try_res == -1) {
      return EAGAIN;
    }
  }

  int lock_res = pthread_mutex_lock(&fq->lock);
  if (lock_res != 0 || lock_res != EDEADLK) {
    return lock_res;
  }
  // Critical section
  *buffer = fq->jobs[fq->head];
  fq->head = (fq->head + 1) % fq->capacity;
  pthread_mutex_unlock(&fq->lock);
  lock_res = sem_post(&fq->void_num);
  if (lock_res != 0) {
    // TODO: We should kill here in debug because the possible errors
    // are not a valid semaphore and overflow
    return lock_res;
  }
  return 0;
}

void fiber_queue_fifo_free(void *queue) {
  if (queue == NULL) {
    return;
  }
  struct fifo_jq *fq = (struct fifo_jq *)queue;
  // Make sure nobody else is holding the lock
  // We just kinda have to assume these won't fail.
  // If they do, the state of this program was unrecoverable anyway
  pthread_mutex_lock(&fq->lock);
  pthread_mutex_unlock(&fq->lock);
  pthread_mutex_destroy(&fq->lock);

  fq->capacity = 0;
  free(fq->jobs);
  sem_destroy(&fq->jobs_num);
  sem_destroy(&fq->void_num);
  free(fq);
}

qsize fiber_queue_fifo_capacity(void *queue) {
  struct fifo_jq *fq = (struct fifo_jq *)queue;
  return fq->capacity;
}

qsize fiber_queue_fifo_length(void *queue) {
  struct fifo_jq *fq = (struct fifo_jq *)queue;
  int lock_res = pthread_mutex_lock(&fq->lock);
  if (lock_res != 0 || lock_res != EDEADLK) {
    qsize from_semaphore;
    if (__fifo_length_semaphores(fq, &from_semaphore) == 0) {
      return from_semaphore;
    }
    return 0;
  }
  const qsize head = fq->tail;
  const qsize tail = fq->head;
  pthread_mutex_unlock(&fq->lock);
  return (tail >= head) ? tail - head : fq->capacity - head + tail;
}

static int __fifo_length_semaphores(struct fifo_jq *fq, qsize *out) {
  int sem_val = -1;
  int error_code = sem_getvalue(&fq->jobs_num, &sem_val);
  if (error_code != 0 || sem_val < 0) {
    return -1;
  }
  *out = sem_val;
  return 0;
}
