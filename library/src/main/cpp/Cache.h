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

#ifndef CACHE_H
#define CACHE_H

#include <jni.h>
#include <atomic>

#ifdef __cplusplus
extern "C" {
#endif

//**************************************************************************************************
#ifdef __arm__
#define ADDR_HASH_OFFSET 4
#else
#define ADDR_HASH_OFFSET 6
#endif

#define MAX_TRACE_DEPTH 16
#define MAX_BUFFER_SIZE 1024

#define ALLOC_INDEX_SIZE 1 << 16
#define ALLOC_CACHE_SIZE 1 << 15

typedef struct {
    uint32_t          depth;
    uintptr_t         trace[MAX_TRACE_DEPTH];
} Backtrace;

struct AllocNode {
    uint32_t size;
    uintptr_t addr;
    uintptr_t trace[MAX_TRACE_DEPTH];
    AllocNode *next;
};

class Cache {
public:
    Cache(const char *space) {this->mSpace = space;}
    virtual ~Cache() {}
public:
    virtual void reset() = 0;
    virtual void insert(uintptr_t address, size_t size, Backtrace *backtrace) = 0;
    virtual void remove(uintptr_t address) = 0;
    virtual void print() = 0;
protected:
    const char *mSpace;
};

#ifdef __cplusplus
}
#endif

#endif //CACHE_H