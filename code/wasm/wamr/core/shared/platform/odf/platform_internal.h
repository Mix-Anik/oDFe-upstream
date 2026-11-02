/*
 * Minimal single-threaded platform layer of WAMR for the oDFe plugin host: no threads, no files,
 * no sockets, no signal handlers. Everything the plugin sees comes from the engine's imports.
 */
#ifndef _PLATFORM_INTERNAL_H
#define _PLATFORM_INTERNAL_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <ctype.h>
#include <limits.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <assert.h>

#ifndef BH_PLATFORM_ODF
#define BH_PLATFORM_ODF
#endif

#define BH_APPLET_PRESERVED_STACK_SIZE (32 * 1024)
#define BH_THREAD_DEFAULT_PRIORITY 0

typedef void *korp_thread;
typedef void *korp_tid;
typedef int korp_mutex;
typedef int korp_sem;

typedef struct {
    int dummy;
} korp_rwlock;

struct os_thread_wait_node;
typedef struct os_thread_wait_node *os_thread_wait_list;
typedef struct korp_cond {
    int dummy;
} korp_cond;

typedef int os_file_handle;
typedef void *os_dir_stream;
typedef int os_raw_file_handle;
typedef int os_poll_file_handle;
typedef unsigned int os_nfds_t;
typedef struct {
    int dummy;
} os_timespec_dummy;

#define OS_THREAD_MUTEX_INITIALIZER 0

static inline os_file_handle
os_get_invalid_handle(void)
{
    return -1;
}

int
os_getpagesize(void);

#endif /* _PLATFORM_INTERNAL_H */
