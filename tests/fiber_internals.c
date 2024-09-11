#include "../fiber.c"
#include "xtal.h"

#define ASSERT_EQUAL_JID(expected, actual) ASSERT_EQUAL_LONG(expected, actual)

#if JOB_ID_MAX < INT64_MAX || defined(FIBER_CHECK_JID_OVERFLOW)
TEST(job_id_overflow)
{
	jid almost_max = JOB_ID_MAX - 1;
	jid max_jid = get_and_update_jid(&almost_max);
	ASSERT_EQUAL_JID(JOB_ID_MAX, max_jid);
	jid wrapped = get_and_update_jid(&almost_max);
	ASSERT_EQUAL_JID((jid)0, wrapped);
}
#endif

int main()
{
	run_tests();
	return 0;
}
