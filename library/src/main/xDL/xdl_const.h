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

#ifndef IO_HEXHACKING_XDL_CONST
#define IO_HEXHACKING_XDL_CONST

#ifndef __LP64__
#define XDL_CONST_PATHNAME_LINKER     "/system/bin/linker" // we only use this when Android < 8.1
#define XDL_CONST_BASENAME_LINKER     "linker"
#define XDL_CONST_PATHNAME_LZMA       "/system/lib/liblzma.so"
#else
#define XDL_CONST_PATHNAME_LINKER     "/system/bin/linker64" // we only use this when Android < 8.1
#define XDL_CONST_BASENAME_LINKER     "linker64"
#define XDL_CONST_PATHNAME_LZMA       "/system/lib64/liblzma.so"
#endif

#define XDL_CONST_SYM_LINKER_MUTEX    "__dl__ZL10g_dl_mutex"

#define XDL_CONST_SYM_LZMA_CRCGEN     "CrcGenerateTable"
#define XDL_CONST_SYM_LZMA_CRC64GEN   "Crc64GenerateTable"
#define XDL_CONST_SYM_LZMA_CONSTRUCT  "XzUnpacker_Construct"
#define XDL_CONST_SYM_LZMA_ISFINISHED "XzUnpacker_IsStreamWasFinished"
#define XDL_CONST_SYM_LZMA_FREE       "XzUnpacker_Free"
#define XDL_CONST_SYM_LZMA_CODE       "XzUnpacker_Code"

#endif
