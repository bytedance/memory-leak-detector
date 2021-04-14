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

#include "backtrace_64.h"

size_t unwind_backtrace(uintptr_t *stack, size_t max_depth) {
    uintptr_t st; // stack top
    uintptr_t sb; // stack bottom
    GetStackRange(&st, &sb);

    uintptr_t fp = (uintptr_t) __builtin_frame_address(0);

    size_t depth = 0;
    uintptr_t pc = 0;
    while (isValid(fp, st, sb) && depth < max_depth) {
        uintptr_t tt = *((uintptr_t *) fp + 1);
        if (tt != pc) {
            stack[depth++] = tt;
        }
        pc = tt;
        sb = fp;
        fp = *((uintptr_t *) fp);
    }
    return depth;
}