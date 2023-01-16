/*
 * Copyright (C) 2011 The Android Open Source Project
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
b * limitations under the License.
 */

#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/ptrace.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <inttypes.h>

#include "ptrace-arch.h"
#include "libudf_unwind_p.h"
#include "ptrace.h"

uint32_t d15_get_word(void *addr) __attribute__((noinline));

uint32_t d15_get_word(void *addr) {
#if defined(__arm__)
  int val = 0xdeadaee0;
  asm volatile ("ldr     %0, [%1, #0]\n"
		"b       1f\n"
		".long   0x75666475\n"
		".long   0x00006477\n"
		"1:\n"
		:"+r"(val):"r"(addr):"memory");
  return val;
#else
  return *(uint32_t *)addr;
#endif
}

#if 0
uint32_t d15_get_word_wo(void *addr) {
  return *(uint32_t *)addr;
}
#endif

void init_memory(memory_t* memory, const map_info_t* map_info_list) {
    memory->tid = -1;
    memory->map_info_list = map_info_list;
}

void init_memory_ptrace(memory_t* memory, pid_t tid) {
    memory->tid = tid;
    memory->map_info_list = NULL;
}

bool try_get_word(const memory_t* memory, uintptr_t ptr, uint32_t* out_value)
{
    LIBUDF_LOG("try_get_word: reading word at %p", (void*) ptr);
    if (ptr & 3) {
	LIBUDF_LOG("try_get_word: invalid pointer %p", (void*) ptr);
        *out_value = 0xffffffffL;
        return false;
    }
    if (memory->tid < 0) {
#if 0
        *out_value = *(uint32_t*)ptr;
        return true;
#else
        uint32_t out = d15_get_word((void *)ptr);
        if (out != 0xdeadaee0) {
          *out_value = out;
          return true;
        } else {
          *out_value = 0xffffffffL;
          return false;
        }
#endif
    } else {
        // ptrace() returns -1 and sets errno when the operation fails.
        // To disambiguate -1 from a valid result, we clear errno beforehand.
        errno = 0;
        *out_value = ptrace(PTRACE_PEEKTEXT, memory->tid, (void*)ptr, NULL);
        if (*out_value == 0xffffffffL && errno) {
            LIBUDF_LOG("try_get_word: invalid pointer 0x%08x reading from tid %d, "
                       "ptrace() errno=%d", ptr, memory->tid, errno);
            return false;
        }
        return true;
    }
}

#define UNLIKELY(cond) __builtin_expect(!!(cond), 0)
#define TLSVAR __thread __attribute__((tls_model("initial-exec")))

#define THREAD_STACK_SIZE (8 * 1024 * 1024)

static TLSVAR size_t thread_stack_start = 0;
static TLSVAR size_t thread_stack_end = 0;
static TLSVAR pthread_once_t once_control_tls = PTHREAD_ONCE_INIT;

static int ubrd_get_main_thread_stack()
{
	FILE* fd = fopen("/proc/self/maps", "r");
	if (fd == NULL) {
		LIBUDF_LOG("/proc/self/maps open fail\n");
		return -1;
	}

	char line[1024];

	while (fgets(line, sizeof(line), fd)) {
		if (strstr(line, "[stack]") != NULL) {
			LIBUDF_LOG("stack:%s\n", line);
			const char* stack_start;
#if defined(__LP64__)
			//64bit main stack format
            //7ff1bf1000-7ff1c12000 rw-p 00000000 00:00 0                              [stack]
            line[9] = '0';
            line[10] = 'x';
            line[21] = '\0'; //line[11~20] stack start
            stack_start = (const char*)&line[9];
#else
			//32bit main stack format
			//becd7000-becf8000 rw-p 00000000 00:00 0          [stack]
			line[7] = '0';
			line[8] = 'x';
			line[17] = '\0'; //line[9~16] 32bit main thread stack start
			stack_start = (const char*)&line[7];
#endif
			thread_stack_start = strtoul(stack_start, (char **)NULL, 16);
			thread_stack_end = thread_stack_start - THREAD_STACK_SIZE;
			LIBUDF_LOG("[LCH_DEBUG]thread_stack_start:%p, thread_stack_end:%p\n", (void *)thread_stack_start, (void *)thread_stack_end);
			fclose(fd);
			return 0;
		}
	}
	fclose(fd);

	// stack is 28th parameter from /proc/self/stat
	fd = fopen("/proc/self/stat", "re");
	if (fd == NULL) {
		LIBUDF_LOG("/proc/self/stat open fail\n");
		return -1;
	}

	if (fgets(line, sizeof(line), fd)) {
		const char *end_of_comm = strrchr((const char *)line, (int)')');
		size_t stack_start = 0;
		if (1 == sscanf(end_of_comm + 1,
						" %*c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %*u %*u %*d %*d %*d %*d %*d %*d %*u %*u "
						"%*d %*u %*u %*u %" SCNuPTR,
					&stack_start)) {
			thread_stack_start = stack_start;
			thread_stack_end = thread_stack_start - THREAD_STACK_SIZE;
			LIBUDF_LOG("[LCH_DEBUG]thread_stack_start:%p, thread_stack_end:%p\n", (void *)thread_stack_start, (void *)thread_stack_end);
			fclose(fd);
			return 0;
		}
	}

	fclose(fd);
	return -1;
}

static void ubrd_get_thread_stack() {
	if (gettid() == getpid() && 0 == ubrd_get_main_thread_stack()) {
		return;
	}
	pthread_attr_t attr;
	pthread_getattr_np(pthread_self(), &attr);

	thread_stack_start = (size_t)attr.stack_base + attr.stack_size;
	thread_stack_end = (size_t)attr.stack_base;
}

static void ubrd_get_stack(size_t* stack_start, size_t* stack_end) {
	pthread_once(&once_control_tls, ubrd_get_thread_stack);

	stack_t ss;
	if (UNLIKELY(sigaltstack(NULL, &ss) == 0 && (ss.ss_flags & SS_ONSTACK) != 0 )) {
		*stack_start = (size_t)ss.ss_sp + ss.ss_size;
		*stack_end = (size_t)ss.ss_sp;
	} else {
		*stack_start = thread_stack_start;
		*stack_end = thread_stack_end;
	}
}

bool try_get_word_stack(uintptr_t ptr, uint32_t* out_value)
{
	size_t sstart = 0, send = 0;
	ubrd_get_stack(&sstart, &send);

	if ((ptr >= send) && (ptr <= sstart)) {
		*out_value = *(uint32_t*)ptr;
		return true;
	}
	return false;
}
