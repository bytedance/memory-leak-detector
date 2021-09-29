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

#ifndef NODE_POOL_H
#define NODE_POOL_H

#include <atomic>
#include <stdlib.h>
//**************************************************************************************************
class AllocPool {
public:
    AllocPool(size_t count) {
        mCache = (AllocNode *) malloc(count * sizeof(AllocNode));
        mCount = count;
    }

    ~AllocPool() {
        free(mCache);
        mCache = nullptr;
    }
public:
    void reset() {
        mIndex.store(0, std::memory_order_relaxed);
        mStack.store(nullptr, std::memory_order_relaxed);
    }

    AllocNode* apply() {
        AllocNode *p = mStack.load(std::memory_order_relaxed);
        while (p != nullptr) {
            if (mStack.compare_exchange_weak(p, p->next, std::memory_order_release, std::memory_order_relaxed)) {
                return p;
            }
        }

        uint ii = mIndex.load(std::memory_order_relaxed);
        while (ii < mCount) {
            if (mIndex.compare_exchange_weak(ii, ii + 1, std::memory_order_release, std::memory_order_relaxed)) {
                return mCache + ii;
            }
        }

        return nullptr;
    }

    void recycle(AllocNode *p) {
        p->next = mStack.load(std::memory_order_relaxed);
        while (1) {
            if (mStack.compare_exchange_weak(p->next, p, std::memory_order_release, std::memory_order_relaxed)) {
                return;
            }
        }
    }
private:
    AllocNode *              mCache;
    size_t                   mCount;
    std::atomic<uint>        mIndex;
    std::atomic<AllocNode *> mStack;
};
//**************************************************************************************************
#endif //NODE_POOL_H