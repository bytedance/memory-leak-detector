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

#include <stdlib.h>
#include <jni.h>

#include "Logger.h"
#include "Raphael.h"
//**************************************************************************************************
Raphael* sRaphael = new Raphael();

void start(JNIEnv *env, jobject obj, jint configs, jstring space, jstring regex) {
    sRaphael->start(env, obj, configs, space, regex);
}

void stop(JNIEnv *env, jobject obj) {
    sRaphael->stop(env, obj);
}

void print(JNIEnv *env, jobject obj) {
    sRaphael->print(env, obj);
}

static const JNINativeMethod sMethods[] = {
        {
                "nStart",
                "(ILjava/lang/String;Ljava/lang/String;)V",
                (void *) start
        }, {
                "nStop",
                "()V",
                (void *) stop
        }, {
                "nPrint",
                "()V",
                (void *) print
        }
};

static int registerNativeImpl(JNIEnv *env) {
    jclass clazz = env->FindClass("com/bytedance/raphael/Raphael");
    if (clazz == NULL) {
        return JNI_FALSE;
    }

    if (env->RegisterNatives(clazz, sMethods, sizeof(sMethods) / sizeof(sMethods[0])) < 0) {
        return JNI_FALSE;
    } else {
        return JNI_TRUE;
    }
}

JNIEXPORT jint JNI_OnLoad(JavaVM *vm, void *res) {
    JNIEnv *env = NULL;
    if (vm->GetEnv((void **) &env, JNI_VERSION_1_6) != JNI_OK) {
        return -1;
    }

    if (registerNativeImpl(env) == 0) {
        return -1;
    } else {
        return JNI_VERSION_1_6;
    }
}
//**************************************************************************************************