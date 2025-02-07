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

#ifndef RAPHAEL_H
#define RAPHAEL_H

#include <string>


#define RAPHAEL_API __attribute__((visibility("default")))


#define MAP64_MODE 0x00800000
#define ALLOC_MODE 0x00400000
#define DEPTH_MASK 0x001F0000
#define LIMIT_MASK 0x0000FFFF
class Cache;
class RAPHAEL_API Raphael {
public:
    void start(int configs, const char* space, const char* regex);
    void stop();
    void print();
private:
    void clean_cache();
    void dump_system();
private:
    std::string mSpace;
    Cache *mCache;
};

#endif //RAPHAEL_H