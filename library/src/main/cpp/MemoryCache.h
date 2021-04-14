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

#ifndef MEMORF_CACHE_H
#define MEMORF_CACHE_H

#include "Cache.h"
#include "AllocPool.hpp"

using namespace std;

struct AllocNode {
    uint32_t         size;
    uintptr_t        addr;
    uintptr_t        trace[MAX_TRACE_DEPTH];
    AllocNode       *next;
};

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
    pthread_mutex_t       alloc_mutex;
    AllocNode            *alloc_table[ALLOC_INDEX_SIZE];
    AllocPool<AllocNode> *alloc_cache;
};

#endif //MEMORF_CACHE_H