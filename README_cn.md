# MemoryLeakDetector
[![GitHub license](https://img.shields.io/badge/license-Apache--2.0-brightgreen.svg)](https://github.com/bytedance/memory-leak-detector/blob/master/LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Android-brightgreen.svg)](https://developer.android.com)
[![API](https://img.shields.io/badge/api-14%2B-green)](https://developer.android.com/about/dashboards)

MemoryLeakDetector 是西瓜视频基础技术团队开发的一款 native 内存泄漏监控工具，具有接入简单、监控范围广、性能优良、
稳定性好的特点。广泛用于字节跳动旗下各大 App 的 native 内存泄漏治理，收益显著！

## Apps using MemoryLeakDetector

<img src="docs/xigua.png" width="100"/><img src="docs/douyin.png" width="100"/><img src="docs/toutiao.png" width="100"/><img src="docs/huoshan.png" width="100"/><img src="docs/jianying.png" width="100"/><img src="docs/kaiyan.png" width="100"/>

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
    implementation 'com.github.bytedance:memory-leak-detector:0.0.8'
}
```

Step 3: Add code for simple usage (This step is not necessary for using broadcast control)
```java
// 监控指定的so
Raphael.start(
    Raphael.MAP64_MODE|Raphael.ALLOC_MODE|0x0F0000|1024,
    "/storage/emulated/0/raphael", // need sdcard permission
    ".*libxxx\\.so$"
);
```

```java
// 监控整个进程
Raphael.start(
    Raphael.MAP64_MODE|Raphael.ALLOC_MODE|0x0F0000|1024,
    "/storage/emulated/0/raphael", // need sdcard permission
    null
);
```

```shell
## 通过本地广播监控指定的so
## 0x0CF0400=Raphael.MAP64_MODE|Raphael.ALLOC_MODE|0x0F0000|1024
adb shell am broadcast -a com.bytedance.raphael.ACTION_START -f 0x01000000 --es configs 0xCF0400 --es regex ".*libXXX\\.so$"
```

```shell
## 监控整个进程（RaphaelReceiver 组件所在的进程）
## 0x0CF0400=Raphael.MAP64_MODE|Raphael.ALLOC_MODE|0x0F0000|1024
adb shell am broadcast -a com.bytedance.raphael.ACTION_START -f 0x01000000 --es configs 0xCF0400
```

Step 4: Print result
```java
// 代码控制
Raphael.print();
```

```shell
## 本地广播
adb shell am broadcast -a com.bytedance.raphael.ACTION_PRINT -f 0x01000000
```

Step 5: Analysis
```shell
## 聚合 report，该文件在 print/stop 之后生成，需要手动 pull 出来
##   -r: 日志路径, 必需，手机端生成的report文件
##   -o: 输出文件名，非必需，默认为 leak-doubts.txt
##   -s: 符号表目录，非必需，有符号化需求时可传，符号表文件需跟so同名，如：libXXX.so，多个文件需放在同一目录下儿
python library/src/main/python/raphael.py -r report -o leak-doubts.txt -s ./symbol/
```

```shell
## 分析 maps
##  -m: maps文件路径，必需
python library/src/main/python/mmap.py -m maps
```

Step 6: Stop monitoring
```java
// 代码控制
Raphael.stop();
```

```shell
## 广播控制
adb shell am broadcast -a com.bytedance.raphael.ACTION_STOP -f 0x01000000
```

## Extra

1. [Android Camera内存问题剖析](https://mp.weixin.qq.com/s/-oaN-bOqHDjN30UP1FMpgA)
2. [西瓜视频稳定性治理体系建设一：Tailor 原理及实践](https://mp.weixin.qq.com/s/DWOQ9MSTkKSCBFQjPswPIQ)
3. [西瓜视频稳定性治理体系建设二：Raphael 原理及实践](https://mp.weixin.qq.com/s/RF3m9_v5bYTYbwY-d1RloQ)

## Support

1. 在[GitHub issues](https://github.com/bytedance/memory-leak-detector/issues)上交流
2. 邮件: <a href="mailto:shentianzhou.stz@gmail.com">shentianzhou.stz@gmail.com</a>
3. 微信: 429013449
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