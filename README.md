# MemoryLeakDetector

[简体中文版说明 >>>](/README_cn.md)

[![GitHub license](https://img.shields.io/badge/license-Apache--2.0-brightgreen.svg)](https://github.com/bytedance/memory-leak-detector/blob/master/LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Android-brightgreen.svg)](https://developer.android.com)
[![API](https://img.shields.io/badge/api-14%2B-green)](https://developer.android.com/about/dashboards)

MemoryLeakDetector is a native memory leak monitoring tool developed by Xigua video android team. It
has simple access, wide monitoring range, excellent performance and good stability. It is widely used
in native-memory-leak-governance of ByteDance's major apps, and the benefits are significant!

## Apps using MemoryLeakDetector

<img src="docs/xigua.png" width="100"/> <img src="docs/douyin.png" width="100"/> <img src="docs/toutiao.png" width="100"/> <img src="docs/huoshan.png" width="100"/> <img src="docs/jianying.png" width="100"/> <img src="docs/kaiyan.png" width="100"/>

## Get started

Step 1: Add the JitPack repository to your build file
```gradle
allprojects {
    repositories {
        maven { url 'https://jitpack.io' }
    }
}
```

Step 2: Add the dependency
```gradle
dependencies {
    implementation 'com.github.bytedance:memory-leak-detector:0.1.5'
}
```

Step 3: Add code for simple usage (This step is not necessary for using broadcast control)
```java
// Using MemoryLeakDetector to monitor specified so
Raphael.start(
    Raphael.MAP64_MODE|Raphael.ALLOC_MODE|0x0F0000|1024,
    "/storage/emulated/0/raphael", // need sdcard permission
    ".*libxxx\\.so$"
);
```

```java
// Using MemoryLeakDetector to monitor current process
Raphael.start(
    Raphael.MAP64_MODE|Raphael.ALLOC_MODE|0x0F0000|1024,
    "/storage/emulated/0/raphael", // need sdcard permission
    null
);
```

```shell
## broadcast command for specified so
adb shell am broadcast -a com.bytedance.raphael.ACTION_START -f 0x01000000 --es configs 0xCF0400 --es regex ".*libXXX\\.so$"
```

```shell
## broadcast command （RaphaelReceiver component process）
adb shell am broadcast -a com.bytedance.raphael.ACTION_START -f 0x01000000 --es configs 0xCF0400
```

Step 4: Print result
```java
// code control
Raphael.print();
```

```shell
## broadcast command
adb shell am broadcast -a com.bytedance.raphael.ACTION_PRINT -f 0x01000000
```

Step 5: Analysis
```shell
## analysis report
##   -r: report path
##   -o: output file name
##   -s: symbol file dir
python3 library/src/main/python/raphael.py -r report -o leak-doubts.txt -s ./symbol/
```

```shell
## analysis maps
##   -m: maps file path
python3 library/src/main/python/mmap.py -m maps
```

Step 6: Stop monitoring
```java
// code control
Raphael.stop();
```

```shell
## broadcast command
adb shell am broadcast -a com.bytedance.raphael.ACTION_STOP -f 0x01000000
```

## Extra

1. [Android Camera内存问题剖析](https://mp.weixin.qq.com/s/-oaN-bOqHDjN30UP1FMpgA)
2. [西瓜视频稳定性治理体系建设一：Tailor 原理及实践](https://mp.weixin.qq.com/s/DWOQ9MSTkKSCBFQjPswPIQ)
3. [西瓜视频稳定性治理体系建设二：Raphael 原理及实践](https://mp.weixin.qq.com/s/RF3m9_v5bYTYbwY-d1RloQ)

## Support

1. Communicate on [GitHub issues](https://github.com/bytedance/memory-leak-detector/issues)
2. Mail: <a href="mailto:shentianzhou.stz@gmail.com">shentianzhou.stz@gmail.com</a>
3. WeChat: 429013449
<p align="left"><img src="docs/wechat.jpg" alt="Wechat group" width="320px"></p>

## License
~~~
Copyright (c) 2021 ByteDance Inc

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
~~~