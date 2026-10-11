// brEARk realtime check
// ---------------------
// A small library loaded into the soak test with DYLD_INSERT_LIBRARIES. It
// interposes the calls an audio thread must never make (allocation, locks,
// waits, sleeps, file I/O). While a thread is "watched" (the soak test's audio
// thread, only for the duration of brEARk's processBlock), every such call is
// counted and the first call stacks are kept for the report.
//
// Apple's clang has no RealtimeSanitizer (-fsanitize=realtime), so this is
// the same idea by hand. Calls from the library itself are never interposed,
// so the replacements simply call the originals.

#include <dispatch/dispatch.h>
#include <execinfo.h>
#include <fcntl.h>
#include <malloc/malloc.h>
#include <os/lock.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#define BREARK_INTERPOSE(replacement, original)                                                        \
    __attribute__ ((used)) static const struct { const void* r; const void* o; } interpose_##original \
        __attribute__ ((section ("__DATA,__interpose"))) = { (const void*) &replacement, (const void*) &original }

namespace
{
constexpr int maxRecords = 8;
constexpr int maxFrames = 32;

struct Record
{
    char what[32];
    void* frames[maxFrames];
    int numFrames;
};

std::atomic<pthread_t> watched { nullptr };
std::atomic<long> violations { 0 };
Record records[maxRecords];
std::atomic<int> numRecords { 0 };
bool inside = false; // only ever touched by the watched thread

void violation (const char* what) noexcept
{
    if (pthread_self() != watched.load (std::memory_order_relaxed) || inside)
        return;

    inside = true; // backtrace() itself may allocate on first use
    violations.fetch_add (1, std::memory_order_relaxed);

    if (const auto n = numRecords.load(); n < maxRecords)
    {
        auto& record = records[n];
        strlcpy (record.what, what, sizeof (record.what));
        record.numFrames = backtrace (record.frames, maxFrames);
        numRecords.store (n + 1);
    }

    inside = false;
}

// Allocation
void* rt_malloc (size_t size) { violation ("malloc"); return malloc (size); }
void* rt_calloc (size_t count, size_t size) { violation ("calloc"); return calloc (count, size); }
void* rt_realloc (void* p, size_t size) { violation ("realloc"); return realloc (p, size); }
void* rt_valloc (size_t size) { violation ("valloc"); return valloc (size); }
void* rt_aligned_alloc (size_t alignment, size_t size) { violation ("aligned_alloc"); return aligned_alloc (alignment, size); }
int rt_posix_memalign (void** p, size_t alignment, size_t size) { violation ("posix_memalign"); return posix_memalign (p, alignment, size); }
void rt_free (void* p) { if (p != nullptr) violation ("free"); free (p); }
void* rt_malloc_zone_malloc (malloc_zone_t* z, size_t size) { violation ("malloc_zone_malloc"); return malloc_zone_malloc (z, size); }
void* rt_malloc_zone_calloc (malloc_zone_t* z, size_t n, size_t size) { violation ("malloc_zone_calloc"); return malloc_zone_calloc (z, n, size); }
void* rt_malloc_zone_realloc (malloc_zone_t* z, void* p, size_t size) { violation ("malloc_zone_realloc"); return malloc_zone_realloc (z, p, size); }
void* rt_malloc_zone_memalign (malloc_zone_t* z, size_t a, size_t size) { violation ("malloc_zone_memalign"); return malloc_zone_memalign (z, a, size); }
void rt_malloc_zone_free (malloc_zone_t* z, void* p) { if (p != nullptr) violation ("malloc_zone_free"); malloc_zone_free (z, p); }

// Locks and waits
int rt_pthread_mutex_lock (pthread_mutex_t* m) { violation ("pthread_mutex_lock"); return pthread_mutex_lock (m); }
int rt_pthread_rwlock_rdlock (pthread_rwlock_t* l) { violation ("pthread_rwlock_rdlock"); return pthread_rwlock_rdlock (l); }
int rt_pthread_rwlock_wrlock (pthread_rwlock_t* l) { violation ("pthread_rwlock_wrlock"); return pthread_rwlock_wrlock (l); }
int rt_pthread_cond_wait (pthread_cond_t* c, pthread_mutex_t* m) { violation ("pthread_cond_wait"); return pthread_cond_wait (c, m); }
int rt_pthread_cond_timedwait (pthread_cond_t* c, pthread_mutex_t* m, const struct timespec* t) { violation ("pthread_cond_timedwait"); return pthread_cond_timedwait (c, m, t); }
int rt_pthread_join (pthread_t t, void** r) { violation ("pthread_join"); return pthread_join (t, r); }
void rt_os_unfair_lock_lock (os_unfair_lock_t l) { violation ("os_unfair_lock_lock"); os_unfair_lock_lock (l); }
int rt_sem_wait (sem_t* s) { violation ("sem_wait"); return sem_wait (s); }
intptr_t rt_dispatch_semaphore_wait (dispatch_semaphore_t s, dispatch_time_t t) { violation ("dispatch_semaphore_wait"); return dispatch_semaphore_wait (s, t); }
void rt_dispatch_sync (dispatch_queue_t q, dispatch_block_t b) { violation ("dispatch_sync"); dispatch_sync (q, b); }

// Sleeping
int rt_usleep (useconds_t us) { violation ("usleep"); return usleep (us); }
int rt_nanosleep (const struct timespec* a, struct timespec* b) { violation ("nanosleep"); return nanosleep (a, b); }
unsigned rt_sleep (unsigned s) { violation ("sleep"); return sleep (s); }

// File and console I/O
ssize_t rt_write (int fd, const void* buf, size_t n) { violation ("write"); return write (fd, buf, n); }
ssize_t rt_read (int fd, void* buf, size_t n) { violation ("read"); return read (fd, buf, n); }
int rt_close (int fd) { violation ("close"); return close (fd); }
FILE* rt_fopen (const char* path, const char* mode) { violation ("fopen"); return fopen (path, mode); }

int rt_open (const char* path, int flags, ...)
{
    violation ("open");
    mode_t mode = 0;

    if ((flags & O_CREAT) != 0)
    {
        va_list args;
        va_start (args, flags);
        mode = (mode_t) va_arg (args, int);
        va_end (args);
    }

    return open (path, flags, mode);
}
} // namespace

BREARK_INTERPOSE (rt_malloc, malloc);
BREARK_INTERPOSE (rt_calloc, calloc);
BREARK_INTERPOSE (rt_realloc, realloc);
BREARK_INTERPOSE (rt_valloc, valloc);
BREARK_INTERPOSE (rt_aligned_alloc, aligned_alloc);
BREARK_INTERPOSE (rt_posix_memalign, posix_memalign);
BREARK_INTERPOSE (rt_free, free);
BREARK_INTERPOSE (rt_malloc_zone_malloc, malloc_zone_malloc);
BREARK_INTERPOSE (rt_malloc_zone_calloc, malloc_zone_calloc);
BREARK_INTERPOSE (rt_malloc_zone_realloc, malloc_zone_realloc);
BREARK_INTERPOSE (rt_malloc_zone_memalign, malloc_zone_memalign);
BREARK_INTERPOSE (rt_malloc_zone_free, malloc_zone_free);
BREARK_INTERPOSE (rt_pthread_mutex_lock, pthread_mutex_lock);
BREARK_INTERPOSE (rt_pthread_rwlock_rdlock, pthread_rwlock_rdlock);
BREARK_INTERPOSE (rt_pthread_rwlock_wrlock, pthread_rwlock_wrlock);
BREARK_INTERPOSE (rt_pthread_cond_wait, pthread_cond_wait);
BREARK_INTERPOSE (rt_pthread_cond_timedwait, pthread_cond_timedwait);
BREARK_INTERPOSE (rt_pthread_join, pthread_join);
BREARK_INTERPOSE (rt_os_unfair_lock_lock, os_unfair_lock_lock);
BREARK_INTERPOSE (rt_sem_wait, sem_wait);
BREARK_INTERPOSE (rt_dispatch_semaphore_wait, dispatch_semaphore_wait);
BREARK_INTERPOSE (rt_dispatch_sync, dispatch_sync);
BREARK_INTERPOSE (rt_usleep, usleep);
BREARK_INTERPOSE (rt_nanosleep, nanosleep);
BREARK_INTERPOSE (rt_sleep, sleep);
BREARK_INTERPOSE (rt_write, write);
BREARK_INTERPOSE (rt_read, read);
BREARK_INTERPOSE (rt_close, close);
BREARK_INTERPOSE (rt_fopen, fopen);
BREARK_INTERPOSE (rt_open, open);

//==============================================================================
// The soak test finds these with dlsym().
extern "C" __attribute__ ((visibility ("default"))) void breark_rtcheck_watch (bool watch)
{
    watched.store (watch ? pthread_self() : nullptr, std::memory_order_relaxed);
}

extern "C" __attribute__ ((visibility ("default"))) long breark_rtcheck_violations()
{
    return violations.load();
}

extern "C" __attribute__ ((visibility ("default"))) void breark_rtcheck_reset()
{
    violations.store (0);
    numRecords.store (0);
}

// Prints the recorded call stacks (not from the watched thread).
extern "C" __attribute__ ((visibility ("default"))) void breark_rtcheck_print()
{
    for (int i = 0; i < numRecords.load(); ++i)
    {
        std::printf ("  realtime violation %d: %s\n", i + 1, records[i].what);
        std::fflush (stdout);
        backtrace_symbols_fd (records[i].frames, records[i].numFrames, STDOUT_FILENO);
    }
}
