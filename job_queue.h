#ifndef _FIBER_JOB_QUEUE_H
#define _FIBER_JOB_QUEUE_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

typedef long jid; // Type to represent Job id
typedef unsigned int qsize; // Type to represent a queue's size
#define JOB_ID_MAX LONG_MAX
#define JOB_ID_MIN LONG_MIN
#define QUEUE_SIZE_MAX UINT_MAX

struct fiber_job {
	jid job_id;
	void *(*job_func)(void *arg);
	void *job_arg;
};

struct fiber_queue_operations {
	// Required (Cannot be NULL)
	int (*push)(void *queue, struct fiber_job *job, uint32_t flags);
	int (*pop)(void *queue, struct fiber_job *buffer, uint32_t flags);
	int (*init)(void **queue, qsize capacity, void *(*malloc)(size_t),
		    void (*free)(void *));
	void (*free)(void *queue);

	// Optional
	qsize (*capactity)(void *queue);
	qsize (*length)(void *queue);
};

#define FIBER_BLOCK (1 << 31)
#define FIBER_NO_BLOCK 0

#endif // _FIBER_JOB_QUEUE_H
