/*
 * Platform functions of WAMR for the oDFe plugin host (see platform_internal.h).
 */
#include "platform_api_vmcore.h"
#include "platform_api_extension.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

/* the engine decides where WAMR's messages go */
extern int ODF_Wasm_VPrintf(const char *format, va_list ap);

int
bh_platform_init(void)
{
    return 0;
}

void
bh_platform_destroy(void)
{
}

void *
os_malloc(unsigned size)
{
    return malloc(size);
}

void *
os_realloc(void *ptr, unsigned size)
{
    return realloc(ptr, size);
}

void
os_free(void *ptr)
{
    free(ptr);
}

int
os_printf(const char *format, ...)
{
    va_list ap;
    int n;

    va_start(ap, format);
    n = ODF_Wasm_VPrintf(format, ap);
    va_end(ap);
    return n;
}

int
os_vprintf(const char *format, va_list ap)
{
    return ODF_Wasm_VPrintf(format, ap);
}

uint64
os_time_get_boot_us(void)
{
#ifdef _WIN32
    LARGE_INTEGER freq, now;

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    return (uint64)(now.QuadPart * 1000000LL / freq.QuadPart);
#else
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64)ts.tv_sec * 1000000ULL + (uint64)ts.tv_nsec / 1000ULL;
#endif
}

uint64
os_time_thread_cputime_us(void)
{
    return os_time_get_boot_us();
}

korp_tid
os_self_thread(void)
{
    return (korp_tid)1;
}

uint8 *
os_thread_get_stack_boundary(void)
{
    return NULL; /* no native stack check */
}

void
os_thread_jit_write_protect_np(bool enabled)
{
    (void)enabled;
}

/* everything runs on the main thread: the locks of the runtime have nothing to protect */
int
os_mutex_init(korp_mutex *mutex)
{
    *mutex = 0;
    return BHT_OK;
}

int
os_mutex_destroy(korp_mutex *mutex)
{
    (void)mutex;
    return BHT_OK;
}

int
os_mutex_lock(korp_mutex *mutex)
{
    (void)mutex;
    return BHT_OK;
}

int
os_mutex_unlock(korp_mutex *mutex)
{
    (void)mutex;
    return BHT_OK;
}

int
os_getpagesize(void)
{
#ifdef _WIN32
    SYSTEM_INFO si;

    GetSystemInfo(&si);
    return (int)si.dwPageSize;
#else
    return (int)sysconf(_SC_PAGESIZE);
#endif
}

void *
os_mmap(void *hint, size_t size, int prot, int flags, os_file_handle file)
{
    (void)hint;
    (void)prot;
    (void)flags;
    (void)file;
#ifdef _WIN32
    return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
    {
        void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        return p == MAP_FAILED ? NULL : p;
    }
#endif
}

void
os_munmap(void *addr, size_t size)
{
#ifdef _WIN32
    (void)size;
    VirtualFree(addr, 0, MEM_RELEASE);
#else
    munmap(addr, size);
#endif
}

int
os_mprotect(void *addr, size_t size, int prot)
{
    (void)addr;
    (void)size;
    (void)prot;
    return 0;
}

void *
os_mremap(void *old_addr, size_t old_size, size_t new_size)
{
    return os_mremap_slow(old_addr, old_size, new_size);
}

void
os_dcache_flush(void)
{
}

void
os_icache_flush(void *start, size_t len)
{
    (void)start;
    (void)len;
}

int
os_cond_init(korp_cond *cond)
{
    (void)cond;
    return BHT_OK;
}

int
os_cond_destroy(korp_cond *cond)
{
    (void)cond;
    return BHT_OK;
}

int
os_dumps_proc_mem_info(char *out, unsigned int size)
{
    (void)out;
    (void)size;
    return -1;
}
