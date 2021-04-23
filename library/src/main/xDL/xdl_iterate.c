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

#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <ctype.h>
#include <elf.h>
#include <link.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <android/api-level.h>
#include "xdl_iterate.h"
#include "xdl.h"
#include "xdl_util.h"
#include "xdl_const.h"

/*
 * =========================================================================================================
 * API-LEVEL  ANDROID-VERSION  SOLUTION
 * =========================================================================================================
 * 16         4.1              /proc/self/maps
 * 17         4.2              /proc/self/maps
 * 18         4.3              /proc/self/maps
 * 19         4.4              /proc/self/maps
 * 20         4.4W             /proc/self/maps
 * ---------------------------------------------------------------------------------------------------------
 * 21         5.0              dl_iterate_phdr() + __dl__ZL10g_dl_mutex + linker/linker64 in /proc/self/maps
 * 22         5.1              dl_iterate_phdr() + __dl__ZL10g_dl_mutex + linker/linker64 in /proc/self/maps
 * ---------------------------------------------------------------------------------------------------------
 * 23         6.0              dl_iterate_phdr() + linker/linker64 in /proc/self/maps
 * 24         7.0              dl_iterate_phdr() + linker/linker64 in /proc/self/maps
 * 25         7.1              dl_iterate_phdr() + linker/linker64 in /proc/self/maps
 * 26         8.0              dl_iterate_phdr() + linker/linker64 in /proc/self/maps
 * ---------------------------------------------------------------------------------------------------------
 * >= 27      >= 8.1           dl_iterate_phdr()
 * =========================================================================================================
 */

extern __attribute((weak)) int dl_iterate_phdr(int (*)(struct dl_phdr_info *, size_t, void *), void *);

// Android 5.0/5.1 linker's global mutex in .symtab
static pthread_mutex_t *xdl_iterate_linker_mutex = NULL;

static void xdl_iterate_linker_mutex_init()
{
    void *linker = xdl_open(XDL_CONST_PATHNAME_LINKER);
    if(NULL == linker) return;

    xdl_iterate_linker_mutex = xdl_dsym(linker, XDL_CONST_SYM_LINKER_MUTEX);

    xdl_close(linker);
}

static uintptr_t xdl_iterate_get_min_vaddr(struct dl_phdr_info *info)
{
    uintptr_t min_vaddr = UINTPTR_MAX;
    for(size_t i = 0; i < info->dlpi_phnum; i++)
    {
        const ElfW(Phdr) *phdr = &(info->dlpi_phdr[i]);
        if(PT_LOAD == phdr->p_type)
        {
            if(min_vaddr > phdr->p_vaddr) min_vaddr = phdr->p_vaddr;
        }
    }
    return min_vaddr;
}

static int xdl_iterate_open_or_rewind_maps(FILE **maps)
{
    if(NULL == *maps)
    {
        *maps = fopen("/proc/self/maps", "r");
        if(NULL == *maps) return -1;
    }
    else
        rewind(*maps);

    return 0;
}

static uintptr_t xdl_iterate_get_pathname_from_maps(struct dl_phdr_info *info, char *buf, size_t buf_len, FILE **maps)
{
    // get base address
    uintptr_t min_vaddr = xdl_iterate_get_min_vaddr(info);
    if(UINTPTR_MAX == min_vaddr) return 0; // failed
    uintptr_t base = (uintptr_t)(info->dlpi_addr + min_vaddr);

    // open or rewind maps-file
    if(0 != xdl_iterate_open_or_rewind_maps(maps)) return 0; // failed

    char line[1024];
    while(fgets(line, sizeof(line), *maps))
    {
        // check base address
        uintptr_t start, end;
        if(2 != sscanf(line, "%"SCNxPTR"-%"SCNxPTR" r", &start, &end)) continue;
        if(base < start) break; // failed
        if(base >= end) continue;

        // get pathname
        char *pathname = strchr(line, '/');
        if(NULL == pathname) break; // failed
        xdl_util_trim_ending(pathname);

        // found it
        strlcpy(buf, pathname, buf_len);
        return (uintptr_t)buf; // OK
    }

    return 0; // failed
}

static int xdl_iterate_by_linker_cb(struct dl_phdr_info *info, size_t size, void *arg)
{
    uintptr_t *pkg = (uintptr_t *)arg;
    xdl_iterate_phdr_cb_t cb = (xdl_iterate_phdr_cb_t)*pkg++;
    void *cb_arg = (void *)*pkg++;
    FILE **maps = (FILE **)*pkg++;
    uintptr_t linker_load_bias = *pkg++;
    int flags = (int)*pkg;

    if(0 == info->dlpi_addr || NULL == info->dlpi_name || '\0' == info->dlpi_name[0]) return 0; // ignore invalid ELF
    if(linker_load_bias == info->dlpi_addr) return 0; // ignore linker if we have returned it already

    if('/' != info->dlpi_name[0] && '[' != info->dlpi_name[0] && (0 != (flags & XDL_FULL_PATHNAME)))
    {
        // get pathname from /proc/self/maps
        char buf[512];
        uintptr_t pathname = xdl_iterate_get_pathname_from_maps(info, buf, sizeof(buf), maps);
        if(0 == pathname) return 0; // ignore this ELF

        // callback
        struct dl_phdr_info info_fixed;
        info_fixed.dlpi_addr = info->dlpi_addr;
        info_fixed.dlpi_name = (const char *)pathname;
        info_fixed.dlpi_phdr = info->dlpi_phdr;
        info_fixed.dlpi_phnum = info->dlpi_phnum;
        return cb(&info_fixed, size, cb_arg);
    }
    else
    {
        // callback
        return cb(info, size, cb_arg);
    }
}

static uintptr_t xdl_iterate_find_linker_base(FILE **maps)
{
    // open or rewind maps-file
    if(0 != xdl_iterate_open_or_rewind_maps(maps)) return 0; // failed

    size_t linker_pathname_len = strlen(" "XDL_CONST_PATHNAME_LINKER);

    char line[1024];
    while(fgets(line, sizeof(line), *maps))
    {
        // check pathname
        size_t line_len = xdl_util_trim_ending(line);
        if(line_len < linker_pathname_len)continue;
        if(0 != memcmp(line + line_len - linker_pathname_len, " "XDL_CONST_PATHNAME_LINKER, linker_pathname_len)) continue;

        // get base address
        uintptr_t base, offset;
        if(2 != sscanf(line, "%"SCNxPTR"-%*"SCNxPTR" r%*2sp %"SCNxPTR" ", &base, &offset)) continue;
        if(0 != offset) continue;
        if(0 != memcmp((void *)base, ELFMAG, SELFMAG)) continue;

        // find it
        return base;
    }

    return 0;
}

static int xdl_iterate_do_callback(xdl_iterate_phdr_cb_t cb, void *cb_arg, uintptr_t base, const char *pathname, uintptr_t *load_bias)
{
    ElfW(Ehdr) *ehdr = (ElfW(Ehdr) *)base;

    struct dl_phdr_info info;
    info.dlpi_name = pathname;
    info.dlpi_phdr = (const ElfW(Phdr) *)(base + ehdr->e_phoff);
    info.dlpi_phnum = ehdr->e_phnum;

    // get load bias
    uintptr_t min_vaddr = xdl_iterate_get_min_vaddr(&info);
    if(UINTPTR_MAX == min_vaddr) return 0; // ignore invalid ELF
    info.dlpi_addr = (ElfW(Addr))(base - min_vaddr);
    if(NULL != load_bias) *load_bias = info.dlpi_addr;

    return cb(&info, sizeof(struct dl_phdr_info), cb_arg);
}

static int xdl_iterate_by_linker(xdl_iterate_phdr_cb_t cb, void *cb_arg, int flags)
{
    if(NULL == dl_iterate_phdr) return -1;

    FILE *maps = NULL;

    // for linker/linker64 in Android version < 8.1 (API level 27)
    uintptr_t linker_base = 0, linker_load_bias = 0;
    if((flags & XDL_WITH_LINKER) && xdl_util_get_api_level() < __ANDROID_API_O_MR1__)
    {
        linker_base = xdl_iterate_find_linker_base(&maps);
        if(0 != linker_base)
        {
            if(0 != xdl_iterate_do_callback(cb, cb_arg, linker_base, XDL_CONST_PATHNAME_LINKER, &linker_load_bias)) return 0;
        }
    }

    // for other ELF
    uintptr_t pkg[5] = {(uintptr_t)cb, (uintptr_t)cb_arg, (uintptr_t)&maps, linker_load_bias, (uintptr_t)flags};
    if(NULL != xdl_iterate_linker_mutex) pthread_mutex_lock(xdl_iterate_linker_mutex);
    dl_iterate_phdr(xdl_iterate_by_linker_cb, pkg);
    if(NULL != xdl_iterate_linker_mutex) pthread_mutex_unlock(xdl_iterate_linker_mutex);

    if(NULL != maps) fclose(maps);
    return 0;
}

#if defined(__arm__) || defined(__i386__)
static int xdl_iterate_by_maps(xdl_iterate_phdr_cb_t cb, void *cb_arg)
{
    FILE *maps = fopen("/proc/self/maps", "r");
    if(NULL == maps) return 0;

    char line[1024];
    while(fgets(line, sizeof(line), maps))
    {
        // Try to find an ELF which loaded by linker. This is almost always correct in android 4.x.
        uintptr_t base, offset;
        if(2 != sscanf(line, "%"SCNxPTR"-%*"SCNxPTR" r-xp %"SCNxPTR" ", &base, &offset)) continue;
        if(0 != offset) continue;
        if(0 != memcmp((void *)base, ELFMAG, SELFMAG)) continue;

        // get pathname
        char *pathname = strchr(line, '/');
        if(NULL == pathname) break;
        xdl_util_trim_ending(pathname);

        // callback
        if(0 != xdl_iterate_do_callback(cb, cb_arg, base, pathname, NULL)) break;
    }

    fclose(maps);
    return 0;
}
#endif

int xdl_iterate_phdr_impl(xdl_iterate_phdr_cb_t cb, void *cb_arg, int flags)
{
    int api_level = xdl_util_get_api_level();

    // get linker's __dl__ZL10g_dl_mutex for Android 5.0/5.1
    static bool linker_mutex_inited = false;
    if(__ANDROID_API_L__ == api_level || __ANDROID_API_L_MR1__ == api_level)
    {
        if(!linker_mutex_inited)
        {
            linker_mutex_inited = true;
            xdl_iterate_linker_mutex_init();
        }
    }

    // iterate by /proc/self/maps in Android 4.x (Android 4.x only supports arm32 and x86)
#if defined(__arm__) || defined(__i386__)
    if(api_level < __ANDROID_API_L__)
        return xdl_iterate_by_maps(cb, cb_arg);
#endif

    // iterate by dl_iterate_phdr()
    return xdl_iterate_by_linker(cb, cb_arg, flags);
}
