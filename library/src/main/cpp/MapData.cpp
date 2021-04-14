/*
 * Copyright (C) 2012 The Android Open Source Project
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <ctype.h>
#include <elf.h>
#include <inttypes.h>
#include <link.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include "MapData.h"
// Format of /proc/<PID>/maps:
//   6f000000-6f01e000 rwxp 00000000 00:0c 16389419   /system/lib/libcomposer.so
MapEntry* parse_line(char* line) {
    uintptr_t start;
    uintptr_t end;
    uintptr_t offset;
    char permissions[5];
    int name_pos;
    if (sscanf(line, "%" PRIxPTR "-%" PRIxPTR " %4s %" PRIxPTR " %*x:%*x %*d %n", &start, &end,
               permissions, &offset, &name_pos) < 2) {
        return nullptr;
    }
    const char* name = line + name_pos;
    size_t name_len = strlen(name);
    if (name_len && name[name_len - 1] == '\n') {
        name_len -= 1;
    }
    MapEntry* entry = new MapEntry(start, end, offset, name, name_len);
    if (permissions[0] != 'r' || name_len < 3 || entry->name.rfind(".so", name_len - 3) != (name_len - 3)) {
        // Any unreadable map will just get a zero load base.
        entry->load_base = 0;
        entry->load_base_read = true;
    }
    return entry;
}
template <typename T>
static inline bool get_val(MapEntry* entry, uintptr_t addr, T* store) {
    if (addr < entry->start || addr + sizeof(T) > entry->end) {
        return false;
    }
    // Make sure the address is aligned properly.
    if (addr & (sizeof(T) - 1)) {
        return false;
    }
    *store = *reinterpret_cast<T*>(addr);
    return true;
}
void read_loadbase(MapEntry* entry) {
    entry->load_base = 0;
    entry->load_base_read = true;
    uintptr_t addr = entry->start;
    ElfW(Ehdr) ehdr;
    if (!get_val<ElfW(Half)>(entry, addr + offsetof(ElfW(Ehdr), e_phnum), &ehdr.e_phnum)) {
        return;
    }
    if (!get_val<ElfW(Off)>(entry, addr + offsetof(ElfW(Ehdr), e_phoff), &ehdr.e_phoff)) {
        return;
    }
    addr += ehdr.e_phoff;
    for (size_t i = 0; i < ehdr.e_phnum; i++) {
        ElfW(Phdr) phdr;
        if (!get_val<ElfW(Word)>(entry, addr + offsetof(ElfW(Phdr), p_type), &phdr.p_type)) {
            return;
        }
        if (!get_val<ElfW(Off)>(entry, addr + offsetof(ElfW(Phdr), p_offset), &phdr.p_offset)) {
            return;
        }
        if (phdr.p_type == PT_LOAD && phdr.p_offset == entry->offset) {
            if (!get_val<ElfW(Addr)>(entry, addr + offsetof(ElfW(Phdr), p_vaddr), &phdr.p_vaddr)) {
                return;
            }
            entry->load_base = phdr.p_vaddr;
            return;
        }
        addr += sizeof(phdr);
    }
}
bool MapData::ReadMaps() {
    FILE* fp = fopen("/proc/self/maps", "re");
    if (fp == nullptr) {
        return false;
    }
    std::vector<char> buffer(1024);
    while (fgets(buffer.data(), buffer.size(), fp) != nullptr) {
        MapEntry* entry = parse_line(buffer.data());
        if (entry == nullptr) {
            fclose(fp);
            return false;
        }
        auto it = entries_.find(entry);
        if (it == entries_.end()) {
            entries_.insert(entry);
        } else {
            delete entry;
        }
    }
    fclose(fp);
    return true;
}
MapData::~MapData() {
    for (auto* entry : entries_) {
        delete entry;
    }
    entries_.clear();
}
// Find the containing map info for the PC.
const MapEntry* MapData::find(uintptr_t pc, uintptr_t* rel_pc) {
    MapEntry pc_entry(pc);
    auto it = entries_.find(&pc_entry);
    if (it == entries_.end()) {
        ReadMaps();
    }
    it = entries_.find(&pc_entry);
    if (it == entries_.end()) {
        return nullptr;
    }
    MapEntry* entry = *it;
    if (!entry->load_base_read) {
        read_loadbase(entry);
    }
    if (rel_pc) {
        *rel_pc = pc - entry->start + entry->load_base;
    }
    return entry;
}