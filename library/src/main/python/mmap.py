#
# Copyright (C) 2021 ByteDance Inc
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

#!/usr/bin/python3

import re
import os
import sys
import time
import argparse

__PATTERN__ = re.compile(r'(\S+)-(\S+) \S+ \S+ \S+ (\d+)\s*(.*)$')


def analyse(name):
    reader = open(name, 'r')
    totals = 0
    detail = {}
    for line in reader.readlines():
        m = re.match(__PATTERN__, line)
        if not m:
            continue
        elif not m.group(4):
            key = 'unknown'
        elif m.group(4).endswith('.so'):
            key = 'native'
        elif m.group(4).endswith('.art') | m.group(4).endswith('.oat') | m.group(4).endswith('.apk') | m.group(4).endswith('.jar') | m.group(4).endswith('dex'):
            key = 'library'
        elif m.group(4).startswith('/dev/ashmem/dalvik-large object'):
            key = 'dalvik-large-object'
        elif m.group(4).startswith('/dev/ashmem/dalvik-thread local'):
            key = 'dalvik-thread-local'
        elif m.group(4).startswith('/dev/ashmem/dalvik-indirect ref'):
            key = 'dalvik-indirect-ref'
        elif m.group(4).startswith('/dev/ashmem/dalvik-main space'):
            key = 'dalvik-main-space'
        elif m.group(4).startswith('/dev/ashmem/dalvik'):
            key = 'dalvik'
        elif m.group(4) == '/dev/kgsl-3d0':
            key = 'kgsl-3d0'
        elif m.group(4).startswith('[stack:') | m.group(4).startswith('[anon:thread') | m.group(4).startswith('[anon:bionic TLS'):
            key = 'thread'
        elif m.group(4) == '[anon:libc_malloc]':
            key = 'malloc'
        elif m.group(4).startswith('[anon:linker_alloc'):
            key = 'linker'
        elif m.group(4).startswith('/dev/ashmem/'):
            key = 'ashmem'
        elif m.group(4).startswith('/dev/__properties__/u:object_r'):
            key = 'object'
        elif m.group(4) == 'anon_inode:dmabuf':
            key = 'dmabuf'
        elif m.group(4) == '/dev/mali0':
            key = 'mali0'
        elif ('/oat/arm/base.odex' in m.group(4)) | ('/oat/arm/base.vdex' in m.group(4)):
            key = 'dex'
        elif m.group(4).endswith('.otf'):
            key = 'otf'
        elif m.group(4) == '[anon:.bss]':
            key = 'bss'
        elif m.group(4).endswith('.ttf'):
            key = 'ttf'
        elif m.group(4).endswith('.hyb'):
            key = 'hyb'
        elif m.group(4).endswith('.blk'):
            key = 'blk'
        elif m.group(4).endswith('.chk'):
            key = 'chk'
        elif m.group(4) == '/dev/dri/renderD128':
            key = 'renderD128'
        elif m.group(4) == '[anon:atexit handlers]':
            key = 'atexit'
        else:
            key = 'extras'
        length = int(m.group(2), 16) - int(m.group(1), 16)
        totals += length
        length += (detail.get(key) if key in detail.keys() else 0)
        detail.update({key: length})
    print('========== %s ==========' % os.path.basename(name))
    print('%s\t%s' % (format(totals, ',').rjust(13, ' '), 'totals'))
    detail = sorted(detail.items(), key=lambda x: x[1], reverse=True)
    extras = -1
    for i in range(0, len(detail)):
        if detail[i][0] != 'extras':
            print('%s\t%s' % (format(detail[i][1], ',').rjust(13, ' '), detail[i][0]))
        else:
            extras = i
    if extras != -1:
        print('%s\t%s' % (format(detail[extras][1], ',').rjust(13, ' '), detail[extras][0]))
    reader.close()


if __name__ == '__main__':
    argParser = argparse.ArgumentParser()
    argParser.add_argument('-m', '--maps', help='maps file path')
    argParams = argParser.parse_args()

    try:
        analyse(argParams.maps)
    except Exception as e:
        print(e)

