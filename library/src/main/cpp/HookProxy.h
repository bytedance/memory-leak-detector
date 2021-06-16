/*
 * Copyright (C) 2021 ByteDance Inc
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef HOOK_PROXY_H
#define HOOK_PROXY_H

#include <cstdlib>
#include <pthread.h>

#include <unwind.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <android/api-level.h>

#include "xh_core.h"
#include "xdl.h"

#ifdef __arm__
#include "inlineHook.h"
#include "backtrace.h"
#else

#include "And64InlineHook.hpp"
#include "backtrace_64.h"

#endif

#include "Logger.h"
#include "Raphael.h"

//**************************************************************************************************
static Cache *cache = nullptr;
static pthread_key_t guard;
static volatile uint32_t limit;
static volatile uint32_t depth;
static volatile uint32_t isPss;
static volatile uint32_t isVss;

void update_configs(Cache *pNew, uint32_t params) {
    cache = (pNew ? pNew : cache);
    limit = (params & LIMIT_MASK);
    depth = (params & DEPTH_MASK) >> 16;
    isPss = (params & ALLOC_MODE) != 0;
    isVss = (params & MAP64_MODE) != 0;
}

//**************************************************************************************************
static inline void insert_memory_backtrace(void *address, size_t size) {
    Backtrace backtrace;
    backtrace.depth = 0;

#ifdef __arm__
    backtrace.depth = libudf_unwind_backtrace(backtrace.trace, 2, depth + 1);
#else
    backtrace.depth = unwind_backtrace(backtrace.trace, depth + 1);
#endif

    cache->insert((uintptr_t) address, size, &backtrace);
}

//**************************************************************************************************
static void *(*malloc_origin)(size_t) = malloc;

static void *(*calloc_origin)(size_t, size_t) = calloc;

static void *(*realloc_origin)(void *, size_t) = realloc;

static void *(*memalign_origin)(size_t, size_t) = memalign;

static void (*free_origin)(void *) = free;

static void *(*mmap_origin)(void *, size_t, int, int, int, off_t) = mmap;

static void *(*mmap64_origin)(void *, size_t, int, int, int, off64_t) = mmap64;

static int (*munmap_origin)(void *, size_t) = munmap;

static void (*pthread_exit_origin)(void *) = pthread_exit;

//**************************************************************************************************
static void *malloc_proxy(size_t size) {
    if (isPss && size >= limit && !(uintptr_t) pthread_getspecific(guard)) {
        pthread_setspecific(guard, (void *) 1);
        void *address = malloc_origin(size);
        if (address != NULL) {
            insert_memory_backtrace(address, size);
        }
        pthread_setspecific(guard, (void *) 0);
        return address;
    } else {
        return malloc_origin(size);
    }
}

static void *calloc_proxy(size_t count, size_t bytes) {
    uint size = count * bytes;
    if (isPss && size >= limit && !(uintptr_t) pthread_getspecific(guard)) {
        pthread_setspecific(guard, (void *) 1);
        void *address = calloc_origin(count, bytes);
        if (address != NULL) {
            insert_memory_backtrace(address, size);
        }
        pthread_setspecific(guard, (void *) 0);
        return address;
    } else {
        return calloc_origin(count, bytes);
    }
}

static void *realloc_proxy(void *ptr, size_t size) {
    if (isPss && !(uintptr_t) pthread_getspecific(guard)) {
        pthread_setspecific(guard, (void *) 1);
        void *address = realloc_origin(ptr, size);
        if (ptr != NULL && (size == 0 || address != NULL)) {
            cache->remove((uintptr_t) ptr);
        }

        if (address != NULL && size >= limit) {
            insert_memory_backtrace(address, size);
        }
        pthread_setspecific(guard, (void *) 0);
        return address;
    } else {
        return realloc_origin(ptr, size);
    }
}

static void *memalign_proxy(size_t alignment, size_t size) {
    if (isPss && size >= limit && !(uintptr_t) pthread_getspecific(guard)) {
        pthread_setspecific(guard, (void *) 1);
        void *address = memalign_origin(alignment, size);
        if (address != NULL) {
            insert_memory_backtrace(address, size);
        }
        pthread_setspecific(guard, (void *) 0);
        return address;
    } else {
        return memalign_origin(alignment, size);
    }
}

static void free_proxy(void *address) {
    if ((isVss | isPss) && address && !(uintptr_t) pthread_getspecific(guard)) {
        pthread_setspecific(guard, (void *) 1);
        free_origin(address);
        cache->remove((uintptr_t) address);
        pthread_setspecific(guard, (void *) 0);
    } else {
        free_origin(address);
    }
}

static void *mmap_proxy(void *ptr, size_t size, int port, int flags, int fd, off_t offset) {
    if (isVss && !(uintptr_t) pthread_getspecific(guard)) {
        pthread_setspecific(guard, (void *) 1);
        void *address = mmap_origin(ptr, size, port, flags, fd, offset);
        if (address != MAP_FAILED) {
            insert_memory_backtrace(address, size);
        }
        pthread_setspecific(guard, (void *) 0);
        return address;
    } else {
        return mmap_origin(ptr, size, port, flags, fd, offset);
    }
}

static void *mmap64_proxy(void *ptr, size_t size, int port, int flags, int fd, off64_t offset) {
    if (isVss && !(uintptr_t) pthread_getspecific(guard)) {
        pthread_setspecific(guard, (void *) 1);
        void *address = mmap64_origin(ptr, size, port, flags, fd, offset);
        if (address != MAP_FAILED) {
            insert_memory_backtrace(address, size);
        }
        pthread_setspecific(guard, (void *) 0);
        return address;
    } else {
        return mmap64_origin(ptr, size, port, flags, fd, offset);
    }
}

static int munmap_proxy(void *address, size_t size) {
    if (isVss && address && !(uintptr_t) pthread_getspecific(guard)) {
        pthread_setspecific(guard, (void *) 1);
        int result = munmap_origin(address, size);
        if (result == 0) {
            cache->remove((uintptr_t) address);
        }
        pthread_setspecific(guard, (void *) 0);
        return result;
    } else {
        return munmap_origin(address, size);
    }
}

static void pthread_exit_proxy(void *value) {
    pthread_attr_t attr;
    if (isVss && pthread_getattr_np(pthread_self(), &attr) == 0) {
        pthread_setspecific(guard, (void *) 1);
        cache->remove((uintptr_t) attr.stack_base);
        pthread_attr_destroy(&attr);
        pthread_setspecific(guard, (void *) 0);
    }
    pthread_exit_origin(value);
}

//**************************************************************************************************
static const void *sInline[][4] = {
        {
                "malloc",
                (void *) malloc,
                (void *) malloc_proxy,
                (void *) &malloc_origin
        },
        {
                "calloc",
                (void *) calloc,
                (void *) calloc_proxy,
                (void *) &calloc_origin
        },
        {
                "realloc",
                (void *) realloc,
                (void *) realloc_proxy,
                (void *) &realloc_origin
        },
        {
                "memalign",
                (void *) memalign,
                (void *) memalign_proxy,
                (void *) &memalign_origin
        },
        {
                "free",
                (void *) free,
                (void *) free_proxy,
                (void *) &free_origin
        },
        {
                "mmap",
                (void *) mmap,
                (void *) mmap_proxy,
                (void *) &mmap_origin
        },
        {
                "mmap64",
                (void *) mmap64,
                (void *) mmap64_proxy,
                (void *) &mmap64_origin
        },
        {
                "munmap",
                (void *) munmap,
                (void *) munmap_proxy,
                (void *) &munmap_origin
        },
        {
                "pthread_exit",
                (void *) pthread_exit,
                (void *) pthread_exit_proxy,
                (void *) &pthread_exit_origin
        }
};

static const void *sPltGot[][2] = {
        {
                "malloc",
                (void *) malloc_proxy
        },
        {
                "calloc",
                (void *) calloc_proxy
        },
        {
                "realloc",
                (void *) realloc_proxy
        },
        {
                "memalign",
                (void *) memalign_proxy
        },
        {
                "free",
                (void *) free_proxy
        },
        {
                "mmap",
                (void *) mmap_proxy
        },
        {
                "mmap64",
                (void *) mmap64_proxy
        },
        {
                "munmap",
                (void *) munmap_proxy
        },
        {
                "pthread_exit",
                (void *) pthread_exit_proxy
        }
};

static void invoke_je_free() {
    int api_level = android_get_device_api_level();
    if (api_level < __ANDROID_API_O__) {
        return;
    }
    void *handle;
    if (api_level < __ANDROID_API_Q__) {
#if defined(__LP64__)
        handle = xdl_open("/system/lib64/libc.so");
#else
        handle = xdl_open("/system/lib/libc.so");
#endif
    } else {
#if defined(__LP64__)
        handle = xdl_open("/apex/com.android.runtime/lib64/bionic/libc.so");
#else
        handle = xdl_open("/apex/com.android.runtime/lib/bionic/libc.so");
#endif
    }
    if (handle == nullptr) {
        LOGGER("invoke failed at xdl_open");
        return;
    }
    void *target = xdl_sym(handle, "je_free");
    if (target == nullptr) {
        LOGGER("invoke failed at xdl_sym");
    } else {
        sInline[4][1] = target;
    }
    xdl_close(handle);
}

//**************************************************************************************************
void registerPltGotProxy(JNIEnv *env, jstring regex) {
    const char *focused = (char *) env->GetStringUTFChars(regex, 0);
    const int PROXY_MAPPING_LENGTH = sizeof(sPltGot) / sizeof(sPltGot[0]);
    for (int i = 0; i < PROXY_MAPPING_LENGTH; i++) {
        if (0 !=
            xh_core_register(focused, (const char *) sPltGot[i][0], (void *) sPltGot[i][1], NULL)) {
            LOGGER("register focused failed: %s, %s", focused, (const char *) sPltGot[i][0]);
        }
    }
    env->ReleaseStringUTFChars(regex, focused);

    const char *ignored = ".*libraphael\\.so$";
    for (int i = 0; i < PROXY_MAPPING_LENGTH; i++) {
        if (0 != xh_core_ignore(ignored, (const char *) sPltGot[i][0])) {
            LOGGER("register ignored failed: %s, %s", ignored, (const char *) sPltGot[i][0]);
        }
    }

    if (0 != xh_core_refresh(0)) {
        LOGGER("refresh failed");
    }
}

void registerInlineProxy(JNIEnv *env) {
    invoke_je_free();

    const int PROXY_MAPPING_LENGTH = sizeof(sInline) / sizeof(sInline[0]);
#ifdef __arm__
    for (int i = 0; i < PROXY_MAPPING_LENGTH; i++) {
        if (registerInlineHook((uint32_t) sInline[i][1], (uint32_t) sInline[i][2], (uint32_t **) sInline[i][3]) != ELE7EN_OK) {
            LOGGER("register inline hook failed: %s", (const char *) sInline[i][0]);
        }
    }
    inlineHookAll();
#else
    init_arm64_unwind();
    for (int i = 0; i < PROXY_MAPPING_LENGTH; i++) {
        A64HookFunction((void *) sInline[i][1], (void *) sInline[i][2], (void **) sInline[i][3]);
    }
#endif
}

#endif