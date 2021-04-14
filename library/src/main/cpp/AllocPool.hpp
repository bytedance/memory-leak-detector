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
    }

    ~AllocPool() {
        delete cache;
        delete index;
    }

    void reset() {
        tail.store(0);
        head.store(0);
    }

    T* apply() {
        uint i = tail.fetch_add(1, memory_order_acquire);
        return (i <= masks) ? cache + i : index[i & masks];
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
};

#endif //ALLOC_POOL_H