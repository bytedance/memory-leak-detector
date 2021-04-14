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

#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <dlfcn.h>
#include <cxxabi.h>

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

void write_trace(FILE *output, AllocNode *alloc_node, MapData *map_data) {
    Dl_info dli;
    fprintf(output, "\n%p, %u, 1\n", alloc_node->addr, alloc_node->size & 0x03FFFFFF);
    for (int i = 0, status; i < alloc_node->size >> 27; i++) {
        dli.dli_sname = nullptr;
        dli.dli_saddr = nullptr;
        dli.dli_fname = nullptr;
        dladdr((void *) alloc_node->trace[i], &dli);

        uintptr_t pc;
        const MapEntry *entry = map_data->find(alloc_node->trace[i], &pc);

        const char *soname = (entry != nullptr) ? entry->name.c_str() : dli.dli_fname;
        if (soname == nullptr) {
            soname = "<unknown>";
        }

        char *symbol = nullptr;
        if (dli.dli_sname != nullptr) {
            symbol = __cxxabiv1::__cxa_demangle(dli.dli_sname, 0, 0, &status);
        }

        if (symbol != nullptr) {
            fprintf(output, "0x%08X %s (%s + %u)\n", pc, soname, symbol, alloc_node->trace[i] - (uintptr_t) dli.dli_saddr);
            free(symbol);
        } else if (dli.dli_sname != nullptr) {
            fprintf(output, "0x%08X %s (%s + \?)\n", pc, soname, dli.dli_sname);
        } else {
            fprintf(output, "0x%08X %s (unknown)\n", pc, soname);
        }
    }
}

MemoryCache::MemoryCache(const char *sdcard) : Cache(sdcard) {
    pthread_mutex_init(&alloc_mutex, NULL);
    alloc_cache = new AllocPool<AllocNode>(ALLOC_CACHE_SIZE);
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
    if (backtrace->depth > 2) {
        p->size = size | (backtrace->depth - 2) << 27;
        memcpy(p->trace, backtrace->trace + 2, (backtrace->depth - 2) * sizeof(uintptr_t));
    } else {
        p->size = size;
    }

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

    MapData map_data = MapData();
    for (int i = 0; i < ALLOC_INDEX_SIZE; i++) {
        for (AllocNode *p = alloc_table[i]; p != nullptr; p = p->next) {
            write_trace(report, p, &map_data);
        }
    }
    fclose(report);
}