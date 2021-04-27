// Copyright (c) 2020-present, HexHacking Team. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

// Created by caikelun on 2020-10-04.

#include <inttypes.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <unistd.h>
#include <link.h>
#include <elf.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <android/api-level.h>
#include "xdl.h"
#include "xdl_iterate.h"
#include "xdl_util.h"
#include "xdl_lzma.h"
#include "xdl_const.h"

#define XDL_DYNSYM_IS_EXPORT_SYM(shndx) (SHN_UNDEF != (shndx))
#define XDL_SYMTAB_IS_EXPORT_SYM(shndx) (SHN_UNDEF != (shndx) && !((shndx) >= SHN_LORESERVE && (shndx) <= SHN_HIRESERVE))

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"

typedef struct xdl
{
    struct xdl       *next;

    char             *pathname;
    uintptr_t         load_bias;
    const ElfW(Phdr) *dlpi_phdr;
    ElfW(Half)        dlpi_phnum;

    //
    // (1) for searching symbols from .dynsym
    //

    bool        dynsym_try_load;
    ElfW(Sym)  *dynsym;  // .dynsym
    const char *dynstr;  // .dynstr

    // .hash (SYSV hash for .dynstr)
    struct
    {
        const uint32_t *buckets;
        uint32_t        buckets_cnt;
        const uint32_t *chains;
        uint32_t        chains_cnt;
    } sysv_hash;

    // .gnu.hash (GNU hash for .dynstr)
    struct
    {
        const uint32_t   *buckets;
        uint32_t          buckets_cnt;
        const uint32_t   *chains;
        uint32_t          symoffset;
        const ElfW(Addr) *bloom;
        uint32_t          bloom_cnt;
        uint32_t          bloom_shift;
    } gnu_hash;

    //
    // (2) for searching symbols from .symtab
    //

    bool       symtab_try_load;
    uintptr_t  base;

    void      *debugdata; // decompressed .gnu_debugdata

    ElfW(Sym) *symtab;  // .symtab
    size_t     symtab_cnt;
    char      *strtab;  // .strtab
    size_t     strtab_sz;
} xdl_t;

#pragma clang diagnostic pop

// load from memory
static int xdl_dynsym_load(xdl_t *self)
{
    // find the dynamic segment
    ElfW(Dyn) *dynamic = NULL;
    for(size_t i = 0; i < self->dlpi_phnum; i++)
    {
        const ElfW(Phdr) *phdr = &(self->dlpi_phdr[i]);
        if(PT_DYNAMIC == phdr->p_type)
        {
            dynamic = (ElfW(Dyn) *)(self->load_bias + phdr->p_vaddr);
            break;
        }
    }
    if(NULL == dynamic) return -1;

    // iterate the dynamic segment
    for(ElfW(Dyn) * entry = dynamic; entry && entry->d_tag != DT_NULL; entry++)
    {
        switch (entry->d_tag)
        {
            case DT_SYMTAB: //.dynsym
                self->dynsym = (ElfW(Sym) *)(self->load_bias + entry->d_un.d_ptr);
                break;
            case DT_STRTAB: //.dynstr
                self->dynstr = (const char *)(self->load_bias + entry->d_un.d_ptr);
                break;
            case DT_HASH: //.hash
                self->sysv_hash.buckets_cnt = ((const uint32_t *)(self->load_bias + entry->d_un.d_ptr))[0];
                self->sysv_hash.chains_cnt = ((const uint32_t *)(self->load_bias + entry->d_un.d_ptr))[1];
                self->sysv_hash.buckets = &(((const uint32_t *)(self->load_bias + entry->d_un.d_ptr))[2]);
                self->sysv_hash.chains = &(self->sysv_hash.buckets[self->sysv_hash.buckets_cnt]);
                break;
            case DT_GNU_HASH: //.gnu.hash
                self->gnu_hash.buckets_cnt = ((const uint32_t *)(self->load_bias + entry->d_un.d_ptr))[0];
                self->gnu_hash.symoffset = ((const uint32_t *)(self->load_bias + entry->d_un.d_ptr))[1];
                self->gnu_hash.bloom_cnt = ((const uint32_t *)(self->load_bias + entry->d_un.d_ptr))[2];
                self->gnu_hash.bloom_shift = ((const uint32_t *)(self->load_bias + entry->d_un.d_ptr))[3];
                self->gnu_hash.bloom = (const ElfW(Addr) *)(self->load_bias + entry->d_un.d_ptr + 16);
                self->gnu_hash.buckets = (const uint32_t *)(&(self->gnu_hash.bloom[self->gnu_hash.bloom_cnt]));
                self->gnu_hash.chains = (const uint32_t *)(&(self->gnu_hash.buckets[self->gnu_hash.buckets_cnt]));
                break;
            default:
                break;
        }
    }

    if(NULL == self->dynsym || NULL == self->dynstr || (0 == self->sysv_hash.buckets_cnt && 0 == self->gnu_hash.buckets_cnt))
    {
        self->dynsym = NULL;
        self->dynstr = NULL;
        self->sysv_hash.buckets_cnt = 0;
        self->gnu_hash.buckets_cnt = 0;
        return -1;
    }

    return 0;
}

static void *xdl_read_file_to_heap(int file_fd, size_t file_sz, size_t data_offset, size_t data_len)
{
    if(0 == data_len) return NULL;
    if(data_offset >= file_sz) return NULL;
    if(data_offset + data_len > file_sz) return NULL;

    if(data_offset != (size_t)lseek(file_fd, (off_t)data_offset, SEEK_SET)) return NULL;

    void *data = malloc(data_len);
    if(NULL == data) return NULL;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-statement-expression"
    if((ssize_t)data_len != XDL_UTIL_TEMP_FAILURE_RETRY(read(file_fd, data, data_len)))
#pragma clang diagnostic pop
    {
        free(data);
        return NULL;
    }

    return data;
}

static void *xdl_read_file_to_heap_by_section(int file_fd, size_t file_sz, ElfW(Shdr) *shdr)
{
    return xdl_read_file_to_heap(file_fd, file_sz, (size_t)shdr->sh_offset, shdr->sh_size);
}

static void *xdl_read_memory(void *mem, size_t mem_sz, size_t data_offset, size_t data_len)
{
    if(0 == data_len) return NULL;
    if(data_offset >= mem_sz) return NULL;
    if(data_offset + data_len > mem_sz) return NULL;

    return (void *)((uintptr_t)mem + data_offset);
}

static void *xdl_read_memory_by_section(void *mem, size_t mem_sz, ElfW(Shdr) *shdr)
{
    return xdl_read_memory(mem, mem_sz, (size_t)shdr->sh_offset, shdr->sh_size);
}

// load from disk and memory
static int xdl_symtab_load_from_debugdata(xdl_t *self, int file_fd, size_t file_sz, ElfW(Shdr) *shdr_debugdata)
{
    int r = -1;

    // get zipped .gnu_debugdata
    uint8_t *debugdata_zip = (uint8_t *)xdl_read_file_to_heap_by_section(file_fd, file_sz, shdr_debugdata);
    if(NULL == debugdata_zip) return -1;

    // get unzipped .gnu_debugdata
    size_t debugdata_sz;
    if(0 != xdl_lzma_decompress(debugdata_zip, shdr_debugdata->sh_size, (uint8_t **)(&(self->debugdata)), &debugdata_sz)) goto end;

    // get ELF header
    ElfW(Ehdr) *ehdr = (ElfW(Ehdr) *)self->debugdata;
    if(0 == ehdr->e_shnum || ehdr->e_shentsize != sizeof(ElfW(Shdr))) goto end;

    // get section headers
    ElfW(Shdr) *shdrs = (ElfW(Shdr) *)xdl_read_memory(self->debugdata, debugdata_sz, (size_t)ehdr->e_shoff, ehdr->e_shentsize * ehdr->e_shnum);
    if(NULL == shdrs) goto end;

    // get .shstrtab
    if(SHN_UNDEF == ehdr->e_shstrndx || ehdr->e_shstrndx >= ehdr->e_shnum) goto end;
    char *shstrtab = (char *)xdl_read_memory_by_section(self->debugdata, debugdata_sz, shdrs + ehdr->e_shstrndx);
    if(NULL == shstrtab) goto end;

    // lookup .symtab & .strtab
    for(ElfW(Shdr) *shdr = shdrs; shdr < shdrs + ehdr->e_shnum; shdr++)
    {
        char *shdr_name = shstrtab + shdr->sh_name;

        if(SHT_SYMTAB == shdr->sh_type && 0 == strcmp(".symtab", shdr_name))
        {
            // get & check associated .strtab section
            if(shdr->sh_link >= ehdr->e_shnum) continue;
            ElfW(Shdr) *shdr_strtab = shdrs + shdr->sh_link;
            if(SHT_STRTAB != shdr_strtab->sh_type) continue;

            // get .symtab & .strtab
            ElfW(Sym) *symtab = (ElfW(Sym) *)xdl_read_memory_by_section(self->debugdata, debugdata_sz, shdr);
            if(NULL == symtab) continue;
            char *strtab = (char *)xdl_read_memory_by_section(self->debugdata, debugdata_sz, shdr_strtab);
            if(NULL == strtab) continue;

            // OK
            self->symtab = symtab;
            self->symtab_cnt = shdr->sh_size / shdr->sh_entsize;
            self->strtab = strtab;
            self->strtab_sz = shdr_strtab->sh_size;
            r = 0;
            break;
        }
    }

 end:
    free(debugdata_zip);
    if(0 != r && NULL != self->debugdata)
    {
        free(self->debugdata);
        self->debugdata = NULL;
    }
    return r;
}

// load from disk and memory
static int xdl_symtab_load(xdl_t *self)
{
    int r = -1;
    ElfW(Shdr) *shdrs = NULL;
    char *shstrtab = NULL;

    // get base address
    uintptr_t vaddr_min = UINTPTR_MAX;
    for(size_t i = 0; i < self->dlpi_phnum; i++)
    {
        const ElfW(Phdr) *phdr = &(self->dlpi_phdr[i]);
        if(PT_LOAD == phdr->p_type)
        {
            if(vaddr_min > phdr->p_vaddr) vaddr_min = phdr->p_vaddr;
        }
    }
    if(UINTPTR_MAX == vaddr_min) return -1;
    self->base = self->load_bias + vaddr_min;

    // open file
    int file_fd = open(self->pathname, O_RDONLY | O_CLOEXEC);
    if(file_fd < 0) return -1;
    struct stat st;
    if(0 != fstat(file_fd, &st)) goto end;
    size_t file_sz = (size_t)st.st_size;

    // get ELF header
    ElfW(Ehdr) *ehdr = (ElfW(Ehdr) *)self->base;
    if(0 == ehdr->e_shnum || ehdr->e_shentsize != sizeof(ElfW(Shdr))) goto end;

    // get section headers
    shdrs = (ElfW(Shdr) *)xdl_read_file_to_heap(file_fd, file_sz, (size_t)ehdr->e_shoff, ehdr->e_shentsize * ehdr->e_shnum);
    if(NULL == shdrs) goto end;

    // get .shstrtab
    if(SHN_UNDEF == ehdr->e_shstrndx || ehdr->e_shstrndx >= ehdr->e_shnum) goto end;
    shstrtab = (char *)xdl_read_file_to_heap_by_section(file_fd, file_sz, shdrs + ehdr->e_shstrndx);
    if(NULL == shstrtab) goto end;

    // lookup .symtab & .strtab
    for(ElfW(Shdr) *shdr = shdrs; shdr < shdrs + ehdr->e_shnum; shdr++)
    {
        char *shdr_name = shstrtab + shdr->sh_name;

        if(SHT_SYMTAB == shdr->sh_type && 0 == strcmp(".symtab", shdr_name))
        {
            // get & check associated .strtab section
            if(shdr->sh_link >= ehdr->e_shnum) continue;
            ElfW(Shdr) *shdr_strtab = shdrs + shdr->sh_link;
            if(SHT_STRTAB != shdr_strtab->sh_type) continue;

            // get .symtab & .strtab
            ElfW(Sym) *symtab = (ElfW(Sym) *)xdl_read_file_to_heap_by_section(file_fd, file_sz, shdr);
            if(NULL == symtab) continue;
            char *strtab = (char *)xdl_read_file_to_heap_by_section(file_fd, file_sz, shdr_strtab);
            if(NULL == strtab)
            {
                free(symtab);
                continue;
            }

            // OK
            self->symtab = symtab;
            self->symtab_cnt = shdr->sh_size / shdr->sh_entsize;
            self->strtab = strtab;
            self->strtab_sz = shdr_strtab->sh_size;
            r = 0;
            break;
        }
        else if(SHT_PROGBITS == shdr->sh_type && 0 == strcmp(".gnu_debugdata", shdr_name))
        {
            if(0 == xdl_symtab_load_from_debugdata(self, file_fd, file_sz, shdr))
            {
                // OK
                r = 0;
                break;
            }
        }
    }

 end:
    close(file_fd);
    if(NULL != shdrs) free(shdrs);
    if(NULL != shstrtab) free(shstrtab);
    return r;
}

static int xdl_open_iterate_cb(struct dl_phdr_info *info, size_t size, void *arg)
{
    (void)size;

    uintptr_t *pkg = (uintptr_t *)arg;
    xdl_t **self = (xdl_t **)*pkg++;
    const char *filename = (const char *)*pkg;

    // check load_bias and pathname
    if(0 == info->dlpi_addr || NULL == info->dlpi_name) return 0;
    if('/' == filename[0] || '[' == filename[0])
    {
        // full pathname
        if(0 != strcmp(info->dlpi_name, filename)) return 0;
    }
    else
    {
        // basename ?
        size_t basename_len = strlen(filename);
        size_t pathname_len = strlen(info->dlpi_name);
        if(1 + basename_len > pathname_len) return 0;
        if(0 != strcmp(info->dlpi_name + (pathname_len - basename_len), filename)) return 0;
        if('/' != *(info->dlpi_name + (pathname_len - basename_len) - 1)) return 0;
    }

    // found the target ELF
    if(NULL == ((*self) = calloc(1, sizeof(xdl_t)))) return 1; // failed
    if(NULL == ((*self)->pathname = strdup(info->dlpi_name)))
    {
        free(*self);
        *self = NULL;
        return 1; // failed
    }
    (*self)->load_bias = info->dlpi_addr;
    (*self)->dlpi_phdr = info->dlpi_phdr;
    (*self)->dlpi_phnum = info->dlpi_phnum;
    (*self)->dynsym_try_load = false;
    (*self)->symtab_try_load = false;
    return 1; // OK
}

void *xdl_open(const char *filename)
{
    if(NULL == filename) return NULL;

    xdl_t *self = NULL;
    uintptr_t pkg[2] = {(uintptr_t)&self, (uintptr_t)filename};

    int iterate_flags = XDL_FULL_PATHNAME;
    if(xdl_util_ends_with(filename, XDL_CONST_BASENAME_LINKER)) iterate_flags |= XDL_WITH_LINKER;
    xdl_iterate_phdr(xdl_open_iterate_cb, pkg, iterate_flags);

    return (void *)self;
}

void xdl_close(void *handle)
{
    if(NULL == handle) return;

    xdl_t *self = (xdl_t *)handle;
    if(NULL != self->pathname) free(self->pathname);

    if(NULL != self->debugdata)
    {
        // free unzipped .gnu_debugdata
        // self->symtab and self->strtab points to self->debugdata
        free(self->debugdata);
    }
    else
    {
        if(NULL != self->symtab) free(self->symtab);
        if(NULL != self->strtab) free(self->strtab);
    }

    free(self);
}

static uint32_t xdl_sysv_hash(const uint8_t *name)
{
    uint32_t h = 0, g;

    while(*name)
    {
        h = (h << 4) + *name++;
        g = h & 0xf0000000;
        h ^= g;
        h ^= g >> 24;
    }
    return h;
}

static uint32_t xdl_gnu_hash(const uint8_t *name)
{
    uint32_t h = 5381;

    while(*name)
    {
        h += (h << 5) + *name++;
    }
    return h;
}

static ElfW(Sym) *xdl_dynsym_find_symbol_use_sysv_hash(xdl_t *self, const char* sym_name)
{
    uint32_t hash = xdl_sysv_hash((const uint8_t *)sym_name);

    for(uint32_t i = self->sysv_hash.buckets[hash % self->sysv_hash.buckets_cnt]; 0 != i; i = self->sysv_hash.chains[i])
    {
        ElfW(Sym) *sym = self->dynsym + i;
        if(0 != strcmp(self->dynstr + sym->st_name, sym_name)) continue;
        return sym;
    }

    return NULL;
}

static ElfW(Sym) *xdl_dynsym_find_symbol_use_gnu_hash(xdl_t *self, const char* sym_name)
{
    uint32_t hash = xdl_gnu_hash((const uint8_t *)sym_name);

    static uint32_t elfclass_bits = sizeof(ElfW(Addr)) * 8;
    size_t word = self->gnu_hash.bloom[(hash / elfclass_bits) % self->gnu_hash.bloom_cnt];
    size_t mask = 0 | (size_t)1 << (hash % elfclass_bits)
                  | (size_t)1 << ((hash >> self->gnu_hash.bloom_shift) % elfclass_bits);

    //if at least one bit is not set, this symbol is surely missing
    if((word & mask) != mask) return NULL;

    //ignore STN_UNDEF
    uint32_t i = self->gnu_hash.buckets[hash % self->gnu_hash.buckets_cnt];
    if(i < self->gnu_hash.symoffset) return NULL;

    //loop through the chain
    while(1)
    {
        ElfW(Sym) *sym = self->dynsym + i;
        uint32_t sym_hash = self->gnu_hash.chains[i - self->gnu_hash.symoffset];

        if((hash | (uint32_t)1) == (sym_hash | (uint32_t)1))
        {
            if(0 == strcmp(self->dynstr + sym->st_name, sym_name))
            {
                return sym;
            }
        }

        //chain ends with an element with the lowest bit set to 1
        if(sym_hash & (uint32_t)1) break;

        i++;
    }

    return NULL;
}

void *xdl_sym(void *handle, const char *symbol)
{
    xdl_t *self = (xdl_t *)handle;

    // load .dynsym only once
    if(!self->dynsym_try_load)
    {
        self->dynsym_try_load = true;
        if(0 != xdl_dynsym_load(self)) return NULL;
    }

    // find symbol
    if(NULL == self->dynsym) return NULL;
    ElfW(Sym) *sym = NULL;
    if(self->gnu_hash.buckets_cnt > 0)
    {
        // use GNU hash (.gnu.hash -> .dynsym -> .dynstr), O(x) + O(1) + O(1)
        sym = xdl_dynsym_find_symbol_use_gnu_hash(self, symbol);
    }
    if(NULL == sym && self->sysv_hash.buckets_cnt > 0)
    {
        // use SYSV hash (.hash -> .dynsym -> .dynstr), O(x) + O(1) + O(1)
        sym = xdl_dynsym_find_symbol_use_sysv_hash(self, symbol);
    }
    if(NULL == sym || !XDL_DYNSYM_IS_EXPORT_SYM(sym->st_shndx)) return NULL;

    return (void *)(self->load_bias + sym->st_value);
}

void *xdl_dsym(void *handle, const char *symbol)
{
    xdl_t *self = (xdl_t *)handle;

    // load .symtab only once
    if(!self->symtab_try_load)
    {
        self->symtab_try_load = true;
        if(0 != xdl_symtab_load(self)) return NULL;
    }

    // find symbol
    if(NULL == self->symtab) return NULL;
    for(size_t i = 0; i < self->symtab_cnt; i++)
    {
        ElfW(Sym) *sym = self->symtab + i;

        if(!XDL_SYMTAB_IS_EXPORT_SYM(sym->st_shndx)) continue;
        if(0 != strncmp(self->strtab + sym->st_name, symbol, self->strtab_sz - sym->st_name)) continue;

        return (void *)(self->load_bias + sym->st_value);
    }

    return NULL;
}

static bool xdl_elf_is_match(uintptr_t load_bias, const ElfW(Phdr) *dlpi_phdr, ElfW(Half) dlpi_phnum, uintptr_t addr)
{
    if(addr < load_bias) return false;

    uintptr_t vaddr = addr - load_bias;
    for(size_t i = 0; i < dlpi_phnum; i++)
    {
        const ElfW(Phdr) *phdr = &(dlpi_phdr[i]);
        if(PT_LOAD != phdr->p_type) continue;

        if(phdr->p_vaddr <= vaddr && vaddr < phdr->p_vaddr + phdr->p_memsz) return true;
    }

    return false;
}

static int xdl_open_by_addr_iterate_cb(struct dl_phdr_info *info, size_t size, void *arg)
{
    (void)size;

    uintptr_t *pkg = (uintptr_t *)arg;
    xdl_t **self = (xdl_t **)*pkg++;
    uintptr_t addr = *pkg;

    if(xdl_elf_is_match(info->dlpi_addr, info->dlpi_phdr, info->dlpi_phnum, addr))
    {
        // found the target ELF
        if(NULL == ((*self) = calloc(1, sizeof(xdl_t)))) return 1; // failed
        if(NULL == ((*self)->pathname = strdup(info->dlpi_name)))
        {
            free(*self);
            *self = NULL;
            return 1; // failed
        }
        (*self)->load_bias = info->dlpi_addr;
        (*self)->dlpi_phdr = info->dlpi_phdr;
        (*self)->dlpi_phnum = info->dlpi_phnum;
        (*self)->dynsym_try_load = false;
        (*self)->symtab_try_load = false;
        return 1; // OK
    }

    return 0; // mismatch
}

static void *xdl_open_by_addr(void *addr)
{
    if(NULL == addr) return NULL;

    xdl_t *self = NULL;
    uintptr_t pkg[2] = {(uintptr_t)&self, (uintptr_t)addr};
    xdl_iterate_phdr(xdl_open_by_addr_iterate_cb, pkg, XDL_FULL_PATHNAME | XDL_WITH_LINKER);

    return (void *)self;
}

static bool xdl_sym_is_match(ElfW(Sym)* sym, uintptr_t offset, bool is_symtab)
{
    if(is_symtab)
    {
        if(!XDL_SYMTAB_IS_EXPORT_SYM(sym->st_shndx)) false;
    }
    else
    {
        if(!XDL_DYNSYM_IS_EXPORT_SYM(sym->st_shndx)) false;
    }

    return ELF_ST_TYPE(sym->st_info) != STT_TLS &&
        offset >= sym->st_value &&
        offset < sym->st_value + sym->st_size;
}

static ElfW(Sym) *xdl_sym_by_addr(void *handle, void * addr)
{
    xdl_t *self = (xdl_t *)handle;

    // load .dynsym only once
    if(!self->dynsym_try_load)
    {
        self->dynsym_try_load = true;
        if(0 != xdl_dynsym_load(self)) return NULL;
    }

    // lookup symbol
    if(NULL == self->dynsym) return NULL;
    uintptr_t offset = (uintptr_t)addr - self->load_bias;
    if(self->gnu_hash.buckets_cnt > 0)
    {
        const uint32_t *chains_all = self->gnu_hash.chains - self->gnu_hash.symoffset;
        for(size_t i = 0; i < self->gnu_hash.buckets_cnt; i++)
        {
            uint32_t n = self->gnu_hash.buckets[i];
            if(n < self->gnu_hash.symoffset) continue;
            do {
                ElfW(Sym)* sym = self->dynsym + n;
                if(xdl_sym_is_match(sym, offset, false)) return sym;
            } while ((chains_all[n++] & 1) == 0);
        }
    }
    else if(self->sysv_hash.chains_cnt > 0)
    {
        for(size_t i = 0; i < self->sysv_hash.chains_cnt; i++)
        {
            ElfW(Sym)* sym = self->dynsym + i;
            if(xdl_sym_is_match(sym, offset, false)) return sym;
        }
    }

    return NULL;
}

static ElfW(Sym) *xdl_dsym_by_addr(void *handle, void * addr)
{
    xdl_t *self = (xdl_t *)handle;

    // load .symtab only once
    if(!self->symtab_try_load)
    {
        self->symtab_try_load = true;
        if(0 != xdl_symtab_load(self)) return NULL;
    }

    // lookup symbol
    if(NULL == self->symtab) return NULL;
    uintptr_t offset = (uintptr_t)addr - self->load_bias;
    for(size_t i = 0; i < self->symtab_cnt; i++)
    {
        ElfW(Sym) *sym = self->symtab + i;
        if(xdl_sym_is_match(sym, offset, true)) return sym;
    }

    return NULL;
}

int xdl_addr(void *addr, Dl_info *info, void **cache)
{
    if(NULL == info || NULL == addr || NULL == cache) return 0;
    memset(info, 0, sizeof(Dl_info));

    // lookup handle from cache
    xdl_t *handle = NULL;
    for(handle = *((xdl_t **)cache); NULL != handle; handle = handle->next)
        if(xdl_elf_is_match(handle->load_bias, handle->dlpi_phdr, handle->dlpi_phnum, (uintptr_t)addr))
            break;

    // create new handle, save handle to cache
    if(NULL == handle)
    {
        handle = (xdl_t *)xdl_open_by_addr(addr);
        if(NULL == handle) return 0;
        handle->next = *(xdl_t **)cache;
        *(xdl_t **)cache = handle;
    }

    // we have at least load_bias and pathname
    info->dli_fbase = (void *)handle->load_bias;
    info->dli_fname = handle->pathname;
    info->dli_sname = NULL;
    info->dli_saddr = 0;

    // keep looking for symbol name and symbol offset
    ElfW(Sym) *sym;
    if(NULL != (sym = xdl_sym_by_addr((void *)handle, addr)))
    {
        info->dli_sname = handle->dynstr + sym->st_name;
        info->dli_saddr = (void *)(handle->load_bias + sym->st_value);
    }
    else if(NULL != (sym = xdl_dsym_by_addr((void *)handle, addr)))
    {
        info->dli_sname = handle->strtab + sym->st_name;
        info->dli_saddr = (void *)(handle->load_bias + sym->st_value);
    }

    return 1;
}

void xdl_addr_clean(void **cache)
{
    if(NULL == cache) return;

    xdl_t *handle = *((xdl_t **)cache);
    while(NULL != handle)
    {
        xdl_t *tmp = handle;
        handle = handle->next;
        xdl_close(tmp);
    }
    *cache = NULL;
}

int xdl_iterate_phdr(int (*callback)(struct dl_phdr_info *, size_t, void *), void *data, int flags)
{
    return xdl_iterate_phdr_impl(callback, data, flags);
}
