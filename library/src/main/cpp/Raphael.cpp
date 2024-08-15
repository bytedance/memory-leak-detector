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

#include <cstring>
#include <dirent.h>
#include "Cache.h"
#include "Raphael.h"
#include "HookProxy.h"
#include "MemoryCache.h"
#include "PltGotHookProxy.h"

//**************************************************************************************************
void Raphael::start(int configs, const char* space, const char* regex) {
    mSpace = space;
    mCache = new MemoryCache(mSpace.c_str());
    update_configs(mCache, 0);

    if (regex != nullptr) {
        registerSoLoadProxy(regex);
    } else {
        registerInlineProxy();
    }

    mCache->reset();
    pthread_key_create(&guard, nullptr);
    LOGGER("start >>> %#x, %s", (uint) configs, mSpace.c_str());
    update_configs(mCache, configs);
}

void Raphael::stop() {
    update_configs(nullptr, 0);
    print();

    delete mCache;
    mCache = nullptr;

    xh_core_clear();
    pthread_key_delete(guard);
    LOGGER("stop >>> %s", mSpace.c_str());
}

void Raphael::print() {
    pthread_setspecific(guard, (void *) 1);

    clean_cache();
    mCache->print();
    dump_system();

    LOGGER("print >>> %s", mSpace.c_str());
    pthread_setspecific(guard, (void *) 0);
}

void Raphael::clean_cache() {
    DIR *pDir;
    struct dirent *pDirent;

    char path[MAX_BUFFER_SIZE];
    if ((pDir = opendir(mSpace.c_str())) != NULL) {
        while ((pDirent = readdir(pDir)) != NULL) {
            if (strcmp(pDirent->d_name, ".") != 0 && strcmp(pDirent->d_name, "..") != 0) {
                if (snprintf(path, MAX_BUFFER_SIZE, "%s/%s", mSpace.c_str(), pDirent->d_name) < MAX_BUFFER_SIZE) {
                    remove(path);
                }
            }
        }
        closedir(pDir);
    } else if (mkdir(mSpace.c_str(), 777) != 0) {
        LOGGER("create %s failed, please check permissions", mSpace.c_str());
    } else {
        LOGGER("create %s success", mSpace.c_str());
    }
}

void Raphael::dump_system() {
    char path[MAX_BUFFER_SIZE];
    if (snprintf(path, MAX_BUFFER_SIZE, "%s/maps", mSpace.c_str()) >= MAX_BUFFER_SIZE) {
        return;
    }

    FILE *target = fopen(path, "w");
    if (target == nullptr) {
        LOGGER("dump maps failed, can't open %s/maps", mSpace.c_str());
        return;
    }

    FILE *source = fopen("/proc/self/maps", "re");
    if (source == nullptr) {
        LOGGER("dump maps failed, can't open /proc/self/maps");
        return;
    }

    while (fgets(path, MAX_BUFFER_SIZE, source) != nullptr) {
        fprintf(target, "%s", path);
    }

    fclose(source);
    fclose(target);
}