#ifndef _FIBER_JOB_QUEUE_H
#define _FIBER_JOB_QUEUE_H

#include <limits.h>
#include <stdint.h>

#define FIBER_CHECK_JID_OVERFLOW 1
typedef long jid; // Type to represent Job id
typedef unsigned int qsize; // Type to represent a queue's size
#define JOB_ID_MAX LONG_MAX
#define JOB_ID_MIN LONG_MIN
#define QUEUE_SIZE_MAX UINT_MAX

/** Jobs **/

struct fiber_job {
	jid job_id;
	void *(*job_func)(void *arg);
	void *job_arg;
};

/** Queue **/

typedef int (*fiber_queue_init)(void **queue, qsize capacity);
typedef int (*fiber_queue_push)(void *queue, struct fiber_job *job,
				uint32_t flags);
typedef int (*fiber_queue_pop)(void *queue, struct fiber_job *buffer,
			       uint32_t flags);
typedef void (*fiber_queue_free)(void *queue);

typedef qsize (*fiber_queue_capacity)(void *queue);
typedef qsize (*fiber_queue_length)(void *queue);

struct fiber_queue_operations {
	// Required (Cannot be NULL)
	fiber_queue_push push;
	fiber_queue_pop pop;
	fiber_queue_init init;
	fiber_queue_free free;

	// Optional
	fiber_queue_capacity capactity;
	fiber_queue_length length;
};

#define FIBER_BLOCK (1 << 31)

#endif // _FIBER_JOB_QUEUE_H
