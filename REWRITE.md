# Fiber Rewrite
As I've written Fiber I've started to get a better understanding of what the library
needs to look like. Before finding large issues in the fiber_wait() function, I thought
I had it figured out. Boy was I wrong. This rewrite will mostly be a structural one. If
I want Fiber to be a fast, general purpose, portable (except for Windows, yuck) thread
pool library, I need to redesign everything from the ground up. Here are some of my
major missteps:
1. **Using plain makefiles**: This one may not have been a *major* misstep, but I think
   it was a small mistake. I got to a point where I needed to start adding better build
   time configuration support. If I wanted to stick with Makefiles, I had one option:
   autotools. I was not willing to learn GNU M4.
2. **Not thinking about lock freedom from the start**: I first heard about lock free data
   structures in 2024. I was intrigued and wanted to build Fiber as a lock free thread pool
   library. The problem was I had no experience implementing or using lock free data
   structures. I thought "I'll just use locks for now and then make everything lock free when
   I'm finished." That was very naive because lock free data structures in non garbage collected
   languages typically required non-trivial reclamation schemes (that's honestly the hardest
   part). The library should be built based around this need rather than transformed to fit
   this need. For example, when using lock based data structures a thread can essentially exit
   at any time (assuming no other constraints). This is not true when using something like
   epoch based reclamation. A thread maintains a local list of "logically deleted" pointers that
   must be freed at some point in the future. You can see how this completely changes the
   manner in which we should remove threads from the pool upon the user's request. Should a
   thread transfer its list to another thread? Should it not be allowed to exit? Should it
   block until it can free all of those threads? These questions should have been explored
   while building the removal mechanism, not after.
3. **Putting the sleeping burden on the queue**: This is just a poor design choice. For a
   handful of reasons, the job queue should not be required to implement the sleeping logic.
4. **Not realizing I need futexes earlier**: Futexes (Fast user space mutexes) are a simple
   structure that can act as the building block of sleeping and synchronization schemes. In a
   thread pool library, sleeping until X event happens is a very common need. A thread may
   want to sleep until a job is available, the job Y has completed, the pool has no more
   jobs, etc. A semaphore can fulfill this need most of the time, but some schemes may
   require or desire a more customized blocking primitive. Using futexes would allow Fiber
   to implement whatever mechanism where sleeping is required without needing to retrofit
   mutexes or semaphores.

There are most definitely more, but these are the big ones that led to my decision to
rethink how I'm building Fiber.
