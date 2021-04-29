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

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.text.TextUtils;

import java.io.File;

/**
 * adb shell am broadcast -a com.bytedance.raphael.ACTION_START -f 0x01000000 --es configs 0xCF0400 --es regex ".*libXXX\\.so$"
 * adb shell am broadcast -a com.bytedance.raphael.ACTION_STOP -f 0x01000000
 * adb shell am broadcast -a com.bytedance.raphael.ACTION_PRINT -f 0x01000000
 */
public class RaphaelReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context ctx, Intent intent) {
        String action = intent.getAction();
        if ("com.bytedance.raphael.ACTION_START".equals(action)) {
            Raphael.start(getConfigs(intent.getStringExtra("configs")), getSpace(ctx), intent.getStringExtra("regex"));
        } else if ("com.bytedance.raphael.ACTION_STOP".equals(action)) {
            Raphael.stop();
        } else if ("com.bytedance.raphael.ACTION_PRINT".equals(action)) {
            Raphael.print();
        }
    }

    int getConfigs(String params) {
        if (TextUtils.isEmpty(params)) {
            return Raphael.MAP64_MODE | Raphael.ALLOC_MODE | 0xF0000 | 4096;
        }

        try {
            return Integer.decode(params);
        } catch (NumberFormatException e) {
            e.printStackTrace();
            return Raphael.MAP64_MODE | Raphael.ALLOC_MODE | 0xF0000 | 4096;
        }
    }

    String getSpace(Context ctx) {
        File space = ctx.getExternalFilesDir("raphael");
        if (!space.exists()) {
            space.mkdir();
        }
        return space.getAbsolutePath();
    }
}