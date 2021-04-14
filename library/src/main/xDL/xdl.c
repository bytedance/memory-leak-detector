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

    int        file_fd;
    uint8_t   *file;
    size_t     file_sz;

    uint8_t   *debugdata; // decompressed .gnu_debugdata
    size_t     debugdata_sz;

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

// load from disk and memory
static int xdl_symtab_load(xdl_t *self, ElfW(Shdr) *shdr_debugdata)
{
    ElfW(Ehdr) *ehdr;
    uint8_t *elf;
    size_t elf_sz;

    if(NULL == shdr_debugdata)
    {
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
        if(UINTPTR_MAX == vaddr_min) goto err;
        self->base = self->load_bias + vaddr_min;

        // open file
        if(0 > (self->file_fd = open(self->pathname, O_RDONLY | O_CLOEXEC))) goto err;

        // get file size
        struct stat st;
        if(0 != fstat(self->file_fd, &st)) goto err;
        self->file_sz = (size_t)st.st_size;

        // mmap file
        if(MAP_FAILED == (self->file = (uint8_t *)mmap(NULL, self->file_sz, PROT_READ, MAP_PRIVATE, self->file_fd, 0))) goto err;

        // for ELF parsing
        ehdr = (ElfW(Ehdr) *)self->base;
        elf = self->file;
        elf_sz = self->file_sz;
    }
    else
    {
        // decompress the .gnu_debugdata section
        if(0 != xdl_lzma_decompress(self->file + shdr_debugdata->sh_offset, shdr_debugdata->sh_size, &self->debugdata, &self->debugdata_sz)) goto err;

        // for ELF parsing
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcast-align"
        ehdr = (ElfW(Ehdr) *)self->debugdata;
#pragma clang diagnostic pop
        elf = self->debugdata;
        elf_sz = self->debugdata_sz;
    }

    // check ELF size
    if(0 == ehdr->e_shnum) goto err;
    if(elf_sz < ehdr->e_shoff + ehdr->e_shentsize * ehdr->e_shnum) goto err;

    // get .shstrtab
    if(SHN_UNDEF == ehdr->e_shstrndx) goto err;
    ElfW(Shdr) *shdr_shstrtab = (ElfW(Shdr) *)((uintptr_t)elf + ehdr->e_shoff + ehdr->e_shstrndx * ehdr->e_shentsize);
    char *shstrtab = (char *)((uintptr_t)elf + shdr_shstrtab->sh_offset);

    for(size_t i = 0; i < ehdr->e_shnum; i++)
    {
        ElfW(Shdr) *shdr = (ElfW(Shdr) *)((uintptr_t)elf + ehdr->e_shoff + i * ehdr->e_shentsize);

        if(SHT_SYMTAB == shdr->sh_type && 0 == strcmp(".symtab", shstrtab + shdr->sh_name))
        {
            // get and check .strtab
            if(shdr->sh_link >= ehdr->e_shnum) continue;
            ElfW(Shdr) *shdr_strtab = (ElfW(Shdr) *)((uintptr_t)elf + ehdr->e_shoff + shdr->sh_link * ehdr->e_shentsize);
            if(SHT_STRTAB != shdr_strtab->sh_type) continue;

            // found the .symtab and .strtab
            self->symtab     = (ElfW(Sym) *)((uintptr_t)elf + shdr->sh_offset);
            self->symtab_cnt = shdr->sh_size / shdr->sh_entsize;
            self->strtab     = (char *)((uintptr_t)elf + shdr_strtab->sh_offset);
            self->strtab_sz  = shdr_strtab->sh_size;
            return 0; // OK
        }
        else if(SHT_PROGBITS == shdr->sh_type && 0 == strcmp(".gnu_debugdata", shstrtab + shdr->sh_name) && NULL == shdr_debugdata)
        {
            return xdl_symtab_load(self, shdr);
        }
    }

 err:
    if(MAP_FAILED != self->file)
    {
        munmap(self->file, self->file_sz);
        self->file = MAP_FAILED;
    }
    if(self->file_fd >= 0)
    {
        close(self->file_fd);
        self->file_fd = -1;
    }
    if(NULL != self->debugdata)
    {
        free(self->debugdata);
        self->debugdata = NULL;
    }
    self->base = 0;
    self->file_sz = 0;
    self->debugdata_sz = 0;
    self->symtab = NULL;
    self->symtab_cnt = 0;
    self->strtab = NULL;
    self->strtab_sz = 0;
    return -1;
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
    (*self)->file_fd = -1;
    (*self)->file = MAP_FAILED;
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

    if(MAP_FAILED != self->file) munmap(self->file, self->file_sz);
    if(self->file_fd >= 0) close(self->file_fd);
    if(NULL != self->debugdata) free(self->debugdata);
    if(NULL != self->pathname) free(self->pathname);
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

    // check .dynsym
    if(NULL == self->dynsym) return NULL;

    // find symbol
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
        if(0 != xdl_symtab_load(self, NULL)) return NULL;
    }

    // check .symtab
    if(NULL == self->symtab) return NULL;

    // find symbol
    for(size_t i = 0; i < self->symtab_cnt; i++)
    {
        ElfW(Sym) *sym = self->symtab + i;

        if(!XDL_SYMTAB_IS_EXPORT_SYM(sym->st_shndx)) continue;
        if(0 != strncmp(self->strtab + sym->st_name, symbol, self->strtab_sz - sym->st_name)) continue;

        return (void *)(self->load_bias + sym->st_value);
    }

    return NULL;
}

static ElfW(Sym) *xdl_find_symtab_by_addr(xdl_t *self, void * addr)
{
    uintptr_t offset = (uintptr_t)addr - self->load_bias;

    // find symbol
    for(size_t i = 0; i < self->symtab_cnt; i++)
    {
        ElfW(Sym) *sym = self->symtab + i;

        if(!XDL_SYMTAB_IS_EXPORT_SYM(sym->st_shndx)) continue;
        if(offset < sym->st_value || offset >= sym->st_value + sym->st_size) continue;

        return sym;
    }

    return NULL;
}

static int xdl_addr_by_iterate_cb(struct dl_phdr_info *info, size_t size, void *arg)
{
    (void)size;

    uintptr_t *pkg = (uintptr_t *)arg;
    uintptr_t addr = *pkg++;
    Dl_info *dlinfo = (Dl_info *)*pkg++;
    char **tmpbuf = (char **)*pkg++;
    size_t *tmpbuf_len = (size_t *)*pkg;

    // check load_bias
    if(addr < info->dlpi_addr) return 0;

    uintptr_t vaddr = addr - info->dlpi_addr;
    for(size_t i = 0; i < info->dlpi_phnum; i++)
    {
        const ElfW(Phdr) *phdr = &(info->dlpi_phdr[i]);

        // check program header
        if(PT_LOAD != phdr->p_type) continue;
        if(vaddr < phdr->p_vaddr || vaddr >= phdr->p_vaddr + phdr->p_memsz) continue;

        // found it
        dlinfo->dli_fbase = (void *)info->dlpi_addr;
        strlcpy(*tmpbuf, info->dlpi_name, *tmpbuf_len);
        dlinfo->dli_fname = *tmpbuf;
        dlinfo->dli_sname = NULL;
        dlinfo->dli_saddr = NULL;

        size_t len = strlen(*tmpbuf) + 1;
        *tmpbuf += len;
        *tmpbuf_len -= len;
        return 1; // OK
    }

    return 0;
}

static int xdl_addr_by_iterate(void *addr, Dl_info *info, char **tmpbuf, size_t *tmpbuf_len)
{
    info->dli_fname = NULL;
    uintptr_t pkg[4] = {(uintptr_t)addr, (uintptr_t)info, (uintptr_t)tmpbuf, (uintptr_t)tmpbuf_len};

    xdl_iterate_phdr(xdl_addr_by_iterate_cb, pkg, XDL_WITH_LINKER | XDL_FULL_PATHNAME);

    return NULL == info->dli_fname ? 0 : 1;
}

int xdl_addr(void *addr, Dl_info *info, char *tmpbuf, size_t tmpbuf_len)
{
    if(NULL == addr || NULL == info || NULL == tmpbuf || 0 == tmpbuf_len) return 0;
    int r = dladdr(addr, info);

    if(0 == r) // dladdr() failed
    {
        r = xdl_addr_by_iterate(addr, info, &tmpbuf, &tmpbuf_len);
        if(0 == r) return 0; // xdl_addr_by_iterate() failed
    }

    // match ELF failed
    if(NULL == info->dli_fname || '\0' == info->dli_fname[0]) return r;

    // check address failed
    if(addr < info->dli_fbase) return r;

    // match symbol failed, but we can try to find symbol again from .symtab
    if(NULL == info->dli_sname || NULL == info->dli_saddr)
    {
        // open
        xdl_t *self = (xdl_t *)xdl_open(info->dli_fname);
        if(NULL == self) return r;

        // find symbol name
        if((uintptr_t)addr >= self->load_bias)
        {
            // load .symtab
            self->symtab_try_load = true;
            if(0 == xdl_symtab_load(self, NULL))
            {
                // find symbol name by address
                ElfW(Sym) * sym = xdl_find_symtab_by_addr(self, addr);
                if(NULL != sym)
                {
                    strlcpy(tmpbuf, self->strtab + sym->st_name, tmpbuf_len);
                    info->dli_sname = tmpbuf;
                    info->dli_saddr = (void *)(self->load_bias + sym->st_value);
                }
            }
        }

        // close
        xdl_close((void *)self);
    }

    return r;
}

int xdl_iterate_phdr(int (*callback)(struct dl_phdr_info *, size_t, void *), void *data, int flags)
{
    return xdl_iterate_phdr_impl(callback, data, flags);
}
