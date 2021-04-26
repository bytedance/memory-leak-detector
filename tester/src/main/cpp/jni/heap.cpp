//
// Created by huchao on 2021/4/26.
//
#include <jni.h>
#include <malloc.h>
#include <android/log.h>

#ifdef __cplusplus
extern "C" {
#endif

JNIEXPORT void JNICALL
Java_com_huchao_jni_Heap_malloc(JNIEnv *env, jclass clazz, jlong size) {
    void* ptr = malloc(size);
    if (ptr != nullptr) {
        __android_log_print(ANDROID_LOG_DEBUG, "HEAP", "malloc succeed! size: %ld, ptr: %p", (long) size, ptr);
    } else {
        __android_log_print(ANDROID_LOG_DEBUG, "HEAP", "malloc failed!");
    }
}

#ifdef __cplusplus
}
#endif
