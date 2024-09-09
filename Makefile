CC = gcc
DEFS = -DFIBER_CHECK_JID_OVERFLOW
ALLFLAGS = -Wall
PERFFLAGS = -O2
DEBUGFLAGS = -g

example:
	$(CC) example.c $(DEFS) $(ALLFLAGS) $(PERFFLAGS) $(DEBUGFLAGS) fiber.c queue_impls/fifo_job_queue.c -o build/example
