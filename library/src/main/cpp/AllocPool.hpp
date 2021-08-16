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

#ifndef ALLOC_POOL_H
#define ALLOC_POOL_H

#include <atomic>
#include <stdlib.h>

using namespace std;

template <class T> class AllocPool {
public:
    AllocPool(size_t count) {
        masks = count - 1;
        cache = (T * ) malloc(count * sizeof(T  ));
        index = (T **) malloc(count * sizeof(T *));
        usingRecycle = false;
    }

    ~AllocPool() {
        delete cache;
        delete index;
    }

    void reset() {
        tail.store(0);
        head.store(0);
        usingRecycle = false;
    }

    T* apply() {
        uint i = tail.fetch_add(1, memory_order_acquire);
        if (i <= masks && !usingRecycle) {
            return cache + i;
        } else {
            if (!usingRecycle) {
                usingRecycle = true;
            }
            uint recycleIndex = i & masks;
            if (recycleIndex < head.load(memory_order_acquire)) {
                T *recycle = index[recycleIndex];
                if (recycle == nullptr) {
                    //no recycle item,tail back
                    tail.fetch_sub(1, memory_order_acquire);
                } else {
                    //set to null,mark it used
                    index[recycleIndex] = nullptr;
                }
                return recycle;
            } else {
                //no recycle item
                tail.fetch_sub(1, memory_order_acquire);
                return nullptr;
            }
        }
    }

    void recycle(T *t) {
        uint i = head.fetch_add(1, memory_order_acquire);
        index[i & masks] = t;
    }
private:
    uint              masks;
    std::atomic<uint> tail;
    std::atomic<uint> head;
    T *               cache;
    T **              index;
    bool              usingRecycle;
};

#endif //ALLOC_POOL_H