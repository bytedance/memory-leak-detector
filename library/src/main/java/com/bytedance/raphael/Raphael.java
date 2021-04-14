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

package com.bytedance.raphael;

import android.support.annotation.Keep;
import android.util.Log;

import java.util.concurrent.atomic.AtomicBoolean;

@Keep
public class Raphael {
    public static int MAP64_MODE = 0x00800000;
    public static int ALLOC_MODE = 0x00400000;
    public static int DIFF_CACHE = 0x00200000;

    static {
        System.loadLibrary("raphael");
    }

    static AtomicBoolean sIsRunning = new AtomicBoolean(false);

    public static void start(int configs, String space, String regex) {
        if (sIsRunning.compareAndSet(false, true)) {
            nStart(configs, space, regex);
        } else {
            Log.e("RAPHAEL", "start >>> already started");
        }
    }

    public static void stop() {
        if (sIsRunning.compareAndSet(true, false)) {
            nStop();
        } else {
            Log.e("RAPHAEL", "stop >>> already stopped");
        }
    }

    public static void print() {
        if (sIsRunning.get()) {
            nPrint();
        } else {
            Log.e("RAPHAEL", "print >>> not start");
        }
    }

    private static native void nStart(int configs, String space, String regex);

    private static native void nStop();

    private static native void nPrint();
}