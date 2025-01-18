#include "fiber.h"
#include "fiber_fifo.h"
#include "src/fiber.c"
#include "xtal.h"

#define DEFAULT_THREADS_NUMBER 2
struct fiber_queue_operations queue_ops = FIBER_FIFO_QUEUE_OPERATIONS;
struct fiber_pool_init_options default_opts = {
	.queue_ops = &queue_ops,
	.malloc = malloc,
	.free = free,
	.threads_number = DEFAULT_THREADS_NUMBER,
	.queue_length = 10,
};

int fake_qpush(void *queue, struct fiber_job *job, uint32_t flags)
{
	return 0;
}
int fake_qpop(void *queue, struct fiber_job *buffer, uint32_t flags)
{
	return 0;
}
struct fiber_queue_init_result
fake_qinit(qsize capacity, void *(*malloc)(size_t), void (*free)(void *))
{
	return (struct fiber_queue_init_result){
		.error = 0,
		.queue = NULL + 1, // fiber_init expects this to be non null
	};
}
void fake_qfree(void *queue)
{
	return;
}
qsize fake_qlength(void *queue)
{
	return 0;
}
struct fiber_queue_operations fake_queue_ops = { .push = fake_qpush,
						 .pop = fake_qpop,
						 .init = fake_qinit,
						 .free = fake_qfree,
						 .length = fake_qlength };

#define ASSERT_EQUAL_JID(expected, actual) ASSERT_EQUAL_LONG(expected, actual)

TEST(pool_null_args)
{
	struct fiber_init_result nopts_res = fiber_init(NULL);
	ASSERT_EQUAL_INT(FBR_ENULL_ARGS, nopts_res.error);
}

TEST(pool_bad_sizes)
{
	struct fiber_pool_init_options cstm = default_opts;
	cstm.threads_number = -1;
	struct fiber_init_result tlow = fiber_init(&cstm);
	ASSERT_EQUAL_INT(FBR_EINVLD_SIZE, tlow.error);
	cstm.threads_number = 0;
	tlow = fiber_init(&cstm);
	ASSERT_EQUAL_INT(FBR_EINVLD_SIZE, tlow.error);

	cstm.threads_number = 2;
	cstm.queue_length = -1;
	struct fiber_init_result qlow = fiber_init(&cstm);
	ASSERT_EQUAL_INT(FBR_EINVLD_SIZE, qlow.error);
	cstm.queue_length = 0;
	qlow = fiber_init(&cstm);
	ASSERT_EQUAL_INT(FBR_EINVLD_SIZE, qlow.error);
}

TEST(missing_queue_operations)
{
	struct fiber_pool_init_options cstm = default_opts;
	struct fiber_queue_operations bad_ops = fake_queue_ops;
	bad_ops.push = NULL;
	cstm.queue_ops = &bad_ops;
	// Make sure we catch if any of the required are NULL
	struct fiber_init_result res = fiber_init(&cstm);
	ASSERT_EQUAL_INT(FBR_EQUEOPS_NONE, res.error);
	bad_ops.push = fake_qpush;
	bad_ops.pop = NULL;
	res = fiber_init(&cstm);
	ASSERT_EQUAL_INT(FBR_EQUEOPS_NONE, res.error);
	bad_ops.pop = fake_qpop;
	bad_ops.init = NULL;
	res = fiber_init(&cstm);
	ASSERT_EQUAL_INT(FBR_EQUEOPS_NONE, res.error);
	bad_ops.init = fake_qinit;
	bad_ops.free = NULL;
	res = fiber_init(&cstm);
	ASSERT_EQUAL_INT(FBR_EQUEOPS_NONE, res.error);
}

TEST(optional_queue_operations_assigned)
{
	struct fiber_pool_init_options cstm = default_opts;
	struct fiber_queue_operations all_ops = fake_queue_ops;
	cstm.queue_ops = &all_ops;
	struct fiber_init_result res = fiber_init(&cstm);
	ASSERT_EQUAL_INT(0, res.error);
	ASSERT_NOT_NULL(res.pool);
	ASSERT_EQUAL_PTR(res.pool->queue_ops->length, all_ops.length);
}

TEST(init_normal)
{
	struct fiber_init_result res = fiber_init(&default_opts);
	ASSERT_EQUAL_INT(0, res.error);
	ASSERT_NOT_NULL(res.pool);
	struct fiber_pool *pool = res.pool;
	ASSERT_EQUAL_JID((jid)-1, pool->job_id_prev);
	ASSERT_NOT_NULL(pool->queue_ops);
	ASSERT_NOT_NULL(pool->job_queue);
	ASSERT_NOT_NULL(pool->thread_head);
	ASSERT_EQUAL_INT(DEFAULT_THREADS_NUMBER, pool->threads_number);
	ASSERT_EQUAL_INT(0, pool->threads_working);
	ASSERT_EQUAL_INT(0, pool->threads_kill_number);
	ASSERT_EQUAL_INT(0, pool->pool_flags);
	ASSERT_NOT_NULL(pool->malloc);
	ASSERT_NOT_NULL(pool->free);
	fiber_free(pool);
}

int main()
{
	run_tests();
	return 0;
}
