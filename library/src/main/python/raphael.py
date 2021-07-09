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

import argparse
import subprocess


# addr2line environment
__ARMEABI_ADDR2LINE_FORMAT__ = "arm-linux-androideabi-addr2line -e %s -f %s"
__AARCH64_ADDR2LINE_FORMAT__ = "aarch64-linux-android-addr2line -e %s -f %s"


system_group = [
    'libhwui.so',
    'libsqlite.so',
    'WebViewGoogle.apk',
    'libstagefright.so',
    'libcamera_client.so',
    'libandroid_runtime.so'
]


symbol_table = {}
symbol_cache = {}


class Frame:
    def __init__(self, pc, path, desc):
        self.pc   = pc
        self.path = path
        self.desc = desc

    def __eq__(self, b):
        return self.pc == b.pc and self.path == b.path

    def __ne__(self, b):
        return self.pc != b.pc or self.path != b.path

    def __sub__(self, b):
        return 0 if self == b else (-1 if self.pc > b.pc else 1)


class Trace:
    def __init__(self, id, size, count, stack):
        self.id    = id
        self.size  = int(size)
        self.count = int(count)
        self.stack = stack

    def __eq__(self, b):
        if len(self.stack) != len(b.stack):
            return False
        for i in range(0, len(self.stack)):
            if self.stack[i] != b.stack[i]:
                return False
        return True

    def __sub__(self, b):
        if len(self.stack) != len(b.stack):
            return -1 if len(self.stack) > len(b.stack) else 1
        for i in range(0, len(self.stack)):
            if self.stack[i] != b.stack[i]:
                return self.stack[i] - b.stack[i]
        return 0


def addr_to_line(address, symbol_path):
    status, output = subprocess.getstatusoutput('arm-linux-androideabi-addr2line -C -e %s -f %s' % (symbol_path, address))
    if status != 0:
        raise Exception('execute [arm-linux-androideabi-addr2line -C -e %s -f %s] failed' % (symbol_path, address))
    return output.split('\n')[0]


def retry_symbol(record):
    for frame in record.stack:
        match = re.match(r'.+\/(.+\.so)$', frame.path, re.M | re.I)
        if not match or match.group(1) not in symbol_table.keys():
            continue

        caches = symbol_cache.get(match.group(1))
        if not caches:
            caches = {}
            symbol_cache.update({match.group(1): caches})

        symbol = caches.get(frame.pc)
        if not symbol:
            symbol = addr_to_line(frame.pc, symbol_table.get(match.group(1)))
            caches.update({frame.pc: (symbol if symbol else "unknown")})

        frame.desc = (symbol if symbol else "unknown")


def group_record(record):
    default = None
    for frame in record.stack:
        match = re.match(r'.+\/(.+\.(so|apk|oat))', frame.path, re.M | re.I)
        if not match or match.group(1) == 'libraphael.so':
            continue
        elif frame.path.startswith('/data/'):
            return match.group(1)
        elif match.group(1) in system_group and not default:
            default = match.group(1)
    return default if default else 'extras'


def print_report(writer, report):
    groups = {}
    totals = 0
    for record in report:
        name = group_record(record)
        size = record.size
        groups.update({name: size + (int(groups.get(name)) if name in groups else 0)})
        totals += size
    groups = sorted(groups.items(), key=lambda x: x[1], reverse=True)

    writer.write('%s\t%s\n' % (format(totals, ',').rjust(13, ' '), 'totals'))
    extras = -1
    for i in range(0, len(groups)):
        if groups[i][0] != 'extras':
            writer.write('%s\t%s\n' % (format(groups[i][1], ',').rjust(13, ' '), groups[i][0]))
        else:
            extras = i
    if extras != -1:
        writer.write('%s\t%s\n' % (format(groups[extras][1], ',').rjust(13, ' '), groups[extras][0]))

    report.sort(key=lambda x: x.size, reverse=True)
    for record in report:
        retry_symbol(record)

        writer.write('\n%s, %s, %s\n' % (record.id, record.size, record.count))
        for frame in record.stack:
            writer.write('%s %s (%s)\n' % (frame.pc, frame.path, frame.desc))


def merge_report(report):
    merged = []
    report.sort(key=lambda x: x.size, reverse=True)

    record = report[0]
    for i in range(1, len(report)):
        if record == report[i]:
            record.size += report[i].size
            record.count += 1
        else:
            merged.append(record)
            record = report[i]
    merged.append(record)
    return merged


def parse_report(string):
    report = []
    splits = re.split(r'\n\n', string)
    for split in splits:
        match = re.compile(r'(0x[0-9a-f]+)\ (.+)\ \((.+)\)$', re.M | re.I).findall(split)
        stack = []
        for frame in match:
            stack.append(Frame(frame[0], frame[1], frame[2]))
        match = re.compile(r'(0x[0-9a-f]+),\ (\d+),\ (\d+)$', re.M | re.I).findall(split)
        report.append(Trace(match[0][0], match[0][1], match[0][2], stack))
    return report


def parse_symbol(path):
    for sub in os.listdir(path):
        if sub.endswith('.so'):
            symbol_table.update({sub: os.path.abspath(sub)})
    else:
        print(symbol_table)


if __name__ == '__main__':
    print('Raphael script version: 1.0.0')
    argParser = argparse.ArgumentParser()
    argParser.add_argument('-s', '--symbol', help='symbol folder path, symbol name must be the same as it in phone')
    argParser.add_argument('-o', '--output', help='output report name, the default output report name is leak.txt')
    argParser.add_argument('-r', '--report', help='report')
    argParams = argParser.parse_args()

    if not argParams.report:
        sys.exit(">>>>>>>> no report file")

    if not argParams.symbol:
        symbol_table = {}
    else:
        parse_symbol(argParams.symbol)

    reader = open(argParams.report)
    string = reader.read()
    reader.close()

    report = parse_report(string)
    report = merge_report(report)

    writer = open(argParams.output if argParams.output else 'leak-doubts.txt', 'w')
    print_report(writer, report)
    writer.close()
