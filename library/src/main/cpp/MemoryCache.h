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

#ifndef DIFF_CACHE_H
#define DIFF_CACHE_H

#include "Cache.h"
#include "AllocPool.hpp"

#if defined(__LP64__)
#define STACK_FORMAT_HEADER "\n0x%016lx, %u, 1\n"
#define STACK_FORMAT_UNKNOWN "0x%016lx <unknown>\n"
#define STACK_FORMAT_ANONYMOUS "0x%016lx <anonymous:%016lx>\n"
#define STACK_FORMAT_FILE "0x%016lx %s (unknown)\n"
#define STACK_FORMAT_FILE_NAME "0x%016lx %s (%s + \?)\n"
#define STACK_FORMAT_FILE_NAME_LINE "0x%016lx %s (%s + %lu)\n"
#else
#define STACK_FORMAT_HEADER "\n0x%08x, %u, 1\n"
#define STACK_FORMAT_UNKNOWN "0x%08x <unknown>\n"
#define STACK_FORMAT_ANONYMOUS "0x%08x <anonymous:%08x>\n"
#define STACK_FORMAT_FILE "0x%08x %s (unknown)\n"
#define STACK_FORMAT_FILE_NAME "0x%08x %s (%s + \?)\n"
#define STACK_FORMAT_FILE_NAME_LINE "0x%08x %s (%s + %u)\n"
#endif

class MemoryCache : public Cache {
public:
    MemoryCache(const char *space);
    ~MemoryCache();
public:
    void reset();
    void insert(uintptr_t address, size_t size, Backtrace *backtrace);
    void remove(uintptr_t address);
    void print();
private:
    pthread_mutex_t alloc_mutex;
    AllocNode *alloc_table[ALLOC_INDEX_SIZE];
    AllocPool *alloc_cache;
};

#endif //DIFF_CACHE_H