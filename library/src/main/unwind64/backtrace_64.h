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

#ifndef BACKTRACE_64_H
#define BACKTRACE_64_H

#ifdef __cplusplus
extern "C" {
#endif

#include <pthread.h>
#include <stdlib.h>

static const uintptr_t kFrameSize = 2 * sizeof(uintptr_t);

static inline bool isValid(uintptr_t fp, uintptr_t st, uintptr_t sb) {
    return fp > sb && fp < st - kFrameSize;
}

static inline void GetStackRange(uintptr_t *st, uintptr_t *sb) {
    void *address;
    size_t size;

    pthread_attr_t attr;
    pthread_getattr_np(pthread_self(), &attr);
    pthread_attr_getstack(&attr, &address, &size);

    pthread_attr_destroy(&attr);

    *st = (uintptr_t) address + size;
    *sb = (uintptr_t) address;
}

__attribute__((visibility("default")))
size_t unwind_backtrace(uintptr_t *stack, size_t max_depth);

void init_arm64_unwind();

#ifdef __cplusplus
}
#endif

#endif // BACKTRACE_64_H