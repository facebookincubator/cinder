#!/usr/bin/env python3

# Parsers the output of Cinder refcount debugging info.
# To get these dumps, build Python with --enable-cinder-ref-debug, and use
# cinder.toggle_dump_ref_changes().

import sys
import re

RE_DELTA_LINE = re.compile(r"\*\*\* (inc|dec): (0x[0-9a-f]*) (\w+) (\d+)")

ptr_to_types = {}
inc_by_ptr = {}
inc_by_type = {}
dec_by_ptr = {}
dec_by_type = {}
delta_by_ptr = {}
delta_by_type = {}

for line in map(str.rstrip, sys.stdin):
    m = RE_DELTA_LINE.match(line)
    if m:
        ev = m[1]
        ptr = m[2]
        typ = m[3]
        cnt = int(m[4])
        ptr_types = ptr_to_types.get(ptr, [])
        if typ not in ptr_types:
            ptr_types.append(typ)
            ptr_to_types[ptr] = ptr_types
        if ev == 'inc':
            inc_by_ptr[ptr] = inc_by_ptr.get(ptr, 0) + 1
            inc_by_type[typ] = inc_by_type.get(typ, 0) + 1
            delta_by_ptr[ptr] = delta_by_ptr.get(ptr, 0) + 1
            delta_by_type[typ] = delta_by_type.get(typ, 0) + 1
        else:  # 'dec'
            if cnt == 1:
                # break the asymmetry where new objects
                # start with ref count 1 rather than 0.
                continue
            dec_by_ptr[ptr] = dec_by_ptr.get(ptr, 0) + 1
            dec_by_type[typ] = dec_by_type.get(typ, 0) + 1
            delta_by_ptr[ptr] = delta_by_ptr.get(ptr, 0) - 1
            delta_by_type[typ] = delta_by_type.get(typ, 0) - 1

print("By ptr:")
for ptr, delta in sorted(delta_by_ptr.items(), key=lambda i: abs(i[1])):
    if delta:
        print(f"{ptr} - inc: {inc_by_ptr.get(ptr, 0)}, dec: {dec_by_ptr.get(ptr, 0)}, delta: {delta}, types: {ptr_to_types[ptr]}")

print()
print("By type:")
for typ, delta in sorted(delta_by_type.items(), key=lambda i: abs(i[1])):
    if delta:
        print(f"{typ} - inc: {inc_by_type.get(typ, 0)}, dec: {dec_by_type.get(typ, 0)}, delta: {delta}")
