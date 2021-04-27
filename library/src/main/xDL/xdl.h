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

//
// xDL version: 1.0.1
//
// You can always get the latest version from:
// https://github.com/hexhacking/xDL
//

#ifndef IO_HEXHACKING_XDL
#define IO_HEXHACKING_XDL

#include <stddef.h>
#include <dlfcn.h>
#include <link.h>

#ifdef __cplusplus
extern "C" {
#endif

//
// Enhanced dlopen() / dlclose() / dlsym()
//
void *xdl_open(const char *filename);
void xdl_close(void *handle);

void *xdl_sym(void *handle, const char *symbol);
void *xdl_dsym(void *handle, const char *symbol);

//
// Enhanced dladdr()
//
int xdl_addr(void *addr, Dl_info *info, void **cache);
void xdl_addr_clean(void **cache);

//
// Enhanced dl_iterate_phdr()
//
#define XDL_DEFAULT       0x00
#define XDL_WITH_LINKER   0x01
#define XDL_FULL_PATHNAME 0x02

int xdl_iterate_phdr(int (*callback)(struct dl_phdr_info *, size_t, void *), void *data, int flags);

#ifdef __cplusplus
}
#endif

#endif
