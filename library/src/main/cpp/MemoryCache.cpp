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

#include <cstring>
#include <cstdlib>
#include <pthread.h>
#include <dlfcn.h>
#include <cxxabi.h>
#include <xdl.h>

#include "Logger.h"
#include "MapData.h"
#include "MemoryCache.h"

//**************************************************************************************************
inline AllocNode *remove_alloc(AllocNode **header, uintptr_t address) {
    AllocNode *hptr = *header;
    if (hptr == 0) {
        return nullptr;
    } else if (hptr->addr == address) {
        AllocNode *p = hptr;
        *header = p->next;
        return p;
    } else {
        AllocNode *p = hptr;
        while (p->next != nullptr && p->next->addr != address) p = p->next;
        AllocNode *t = p->next;
        if (t != nullptr) {
            p->next = t->next;
        }
        return t;
    }
}

void write_trace(FILE *output, AllocNode *alloc_node, MapData *map_data, void **dl_cache) {
    fprintf(output, STACK_FORMAT_HEADER, alloc_node->addr, alloc_node->size);
    for (int i = 0; alloc_node->trace[i] != 0; i++) {
        uintptr_t pc = alloc_node->trace[i];
        Dl_info info;
        if (0 == xdl_addr((void *) pc, &info, dl_cache) || (uintptr_t) info.dli_fbase > pc) {
            fprintf(
                    output,
                    STACK_FORMAT_UNKNOWN,
                    pc
            );
        } else {
            if (nullptr == info.dli_fname || '\0' == info.dli_fname[0]) {
                fprintf(
                        output,
                        STACK_FORMAT_ANONYMOUS,
                        pc - (uintptr_t) info.dli_fbase,
                        (uintptr_t) info.dli_fbase
                );
            } else {
                if (nullptr == info.dli_sname || '\0' == info.dli_sname[0]) {
                    fprintf(
                            output,
                            STACK_FORMAT_FILE,
                            pc - (uintptr_t) info.dli_fbase,
                            info.dli_fname
                    );
                } else {
                    int s;
                    const char *symbol = __cxxabiv1::__cxa_demangle(
                            info.dli_sname,
                            nullptr,
                            nullptr,
                            &s
                    );
                    if (0 == (uintptr_t) info.dli_saddr || (uintptr_t) info.dli_saddr > pc) {
                        fprintf(
                                output,
                                STACK_FORMAT_FILE_NAME,
                                pc - (uintptr_t) info.dli_fbase,
                                info.dli_fname,
                                symbol == nullptr ? info.dli_sname : symbol
                        );
                    } else {
                        fprintf(
                                output,
                                STACK_FORMAT_FILE_NAME_LINE,
                                pc - (uintptr_t) info.dli_fbase,
                                info.dli_fname,
                                symbol == nullptr ? info.dli_sname : symbol,
                                pc - (uintptr_t) info.dli_saddr
                        );
                    }
                    if (symbol != nullptr) {
                        free((void *) symbol);
                    }
                }
            }
        }
    }
}

MemoryCache::MemoryCache(const char *sdcard) : Cache(sdcard) {
    pthread_mutex_init(&alloc_mutex, NULL);
    alloc_cache = new AllocPool(ALLOC_CACHE_SIZE);
}

MemoryCache::~MemoryCache() {
    delete alloc_cache;
}

void MemoryCache::reset() {
    alloc_cache->reset();
    for (uint i = 0; i < ALLOC_INDEX_SIZE; i++) {
        alloc_table[i] = nullptr;
    }
}

void MemoryCache::insert(uintptr_t address, size_t size, Backtrace *backtrace) {
    AllocNode *p = alloc_cache->apply();
    if (p == nullptr) {
        LOGGER("Alloc cache is full!!!!!!!!");
        return;
    }

    p->addr = address;
    p->size = size;
    uint depth = backtrace->depth > 2 ? backtrace->depth - 2 : 1;
    memcpy(p->trace, backtrace->trace + 2, depth * sizeof(uintptr_t));
    p->trace[depth] = 0;

    uint16_t alloc_hash = (address >> ADDR_HASH_OFFSET) & 0xFFFF;
    pthread_mutex_lock(&alloc_mutex);
    p->next = alloc_table[alloc_hash];
    alloc_table[alloc_hash] = p;
    pthread_mutex_unlock(&alloc_mutex);
}

void MemoryCache::remove(uintptr_t address) {
    uint16_t alloc_hash = (address >> ADDR_HASH_OFFSET) & 0xFFFF;
    if (alloc_table[alloc_hash] == nullptr) {
        return;
    }

    pthread_mutex_lock(&alloc_mutex);
    AllocNode *p = remove_alloc(&alloc_table[alloc_hash], address);
    pthread_mutex_unlock(&alloc_mutex);

    if (p != nullptr) {
        alloc_cache->recycle(p);
    }
}

void MemoryCache::print() {
    char path[MAX_BUFFER_SIZE];
    sprintf(path, "%s/report", mSpace);

    FILE *report = fopen(path, "w");
    if (report == nullptr) {
        LOGGER("print report failed, can't open report file");
        return;
    }
    void *dl_cache = nullptr;

    pthread_mutex_lock(&alloc_mutex);
    for (auto p : alloc_table) {
        for (; p != nullptr; p = p->next) {
            write_trace(report, p, nullptr, &dl_cache);
        }
    }
    pthread_mutex_unlock(&alloc_mutex);

    xdl_addr_clean(&dl_cache);
    fclose(report);
}