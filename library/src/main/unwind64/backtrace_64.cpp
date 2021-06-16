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

#include "backtrace_64.h"
#include <sys/resource.h>
#include <cinttypes>
#include <cstring>
#include <unistd.h>

static uintptr_t fp_main_thread_stack_low = 0;
static uintptr_t fp_main_thread_stack_high = 0;

static pthread_key_t thread_t_key;
static pthread_key_t thread_b_key;
static bool use_thread_local = false;

void init_arm64_unwind() {
    if (pthread_key_create(&thread_t_key, nullptr) != 0) {
        return;
    }
    if (pthread_key_create(&thread_b_key, nullptr) != 0) {
        pthread_key_delete(thread_t_key);
        return;
    }
    use_thread_local = true;
}

size_t unwind_backtrace(uintptr_t *stack, size_t max_depth) {
    uintptr_t st; // stack top
    uintptr_t sb; // stack bottom
    if (use_thread_local) {
        if ((st = (uintptr_t) pthread_getspecific(thread_t_key)) == 0 ||
            (sb = (uintptr_t) pthread_getspecific(thread_b_key)) == 0) {
            GetStackRange(&st, &sb);
            pthread_setspecific(thread_t_key, (void *) st);
            pthread_setspecific(thread_b_key, (void *) sb);
        }
    } else {
        if (gettid() == getpid()) {
            if (fp_main_thread_stack_low == 0 || fp_main_thread_stack_high == 0) {
                GetStackRange(&st, &sb);
                fp_main_thread_stack_high = st;
                fp_main_thread_stack_low = sb;
            } else {
                st = fp_main_thread_stack_high;
                sb = fp_main_thread_stack_low;
            }
        } else {
            GetStackRange(&st, &sb);
        }
    }

    auto fp = (uintptr_t) __builtin_frame_address(0);

    size_t depth = 0;
    uintptr_t pc = 0;
    while (isValid(fp, st, sb) && depth < max_depth) {
        uintptr_t tt = *((uintptr_t *) fp + 1);
        uintptr_t pre = *((uintptr_t *) fp);
        if (pre & 0xfu || pre < fp + kFrameSize) {
            break;
        }
        if (tt != pc) {
            stack[depth++] = tt;
        }
        pc = tt;
        sb = fp;
        fp = pre;
    }
    return depth;
}