#!/usr/bin/env python3
# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)

import opcode
import sys

try:
    import cinderjit
except ModuleNotFoundError:
    cinderjit = None


CATEGORIES = {
    "Straightforward": [
        "BUILD_STRING",
        "FORMAT_VALUE",
        "UNPACK_SEQUENCE",
        "ROT_FOUR",
        "NOP",
        "DELETE_SUBSCR",
    ],
    "Async": [
        "BEFORE_ASYNC_WITH",
        "GET_ANEXT",
        "GET_AITER",
        "SETUP_ASYNC_WITH",
        "END_ASYNC_FOR",
    ],
    "Exception handling": [
        "SETUP_WITH",
        "WITH_CLEANUP_FINISH",
        "WITH_CLEANUP_START",
        "CALL_FINALLY",
        "POP_FINALLY",
    ],
    "Static Python": [
        "CONVERT_PRIMITIVE",
        "JUMP_IF_ZERO_OR_POP",
        "JUMP_IF_NONZERO_OR_POP",
        "BUILD_CHECKED_MAP",
        "LIST_DEL",
        "LOAD_MAPPING_ARG",
    ],
    "Straightforward, impact unclear": [
        "BUILD_LIST_UNPACK",
        "BUILD_MAP_UNPACK",
        "BUILD_MAP_UNPACK_WITH_CALL",
        "BUILD_SET",
        "BUILD_SET_UNPACK",
        "BUILD_TUPLE_UNPACK",
        "BUILD_TUPLE_UNPACK_WITH_CALL",
        "IMPORT_STAR",
        "MAP_ADD",
        "SET_ADD",
        "UNPACK_EX",
    ],
    "Won't do": [
        "DELETE_ATTR",
        "DELETE_DEREF",
        "DELETE_FAST",
        "DELETE_GLOBAL",
        "DELETE_NAME",
        "LOAD_BUILD_CLASS",
        "LOAD_CLASSDEREF",
        "LOAD_NAME",
        "PRINT_EXPR",
        "SETUP_ANNOTATIONS",
        "STORE_GLOBAL",
        "STORE_NAME",
    ],
}


def main():
    if cinderjit is None:
        print("This script must be run under the Cinder JIT", file=sys.stderr)
        return 1

    supported_ops = {opcode.opname[i] for i in cinderjit.get_supported_opcodes()}
    shadow_names = {opcode.opname[i] for i in opcode.shadowop}
    unsupported_ops = set(opcode.opmap.keys()) - supported_ops - shadow_names

    print(
        f"Total opcodes: {len(opcode.opmap):>3}\n"
        f"    Supported: {len(supported_ops):>3}\n"
        f"  Unsupported: {len(unsupported_ops):>3}\n"
        f"   Shadow ops: {len(shadow_names):>3}"
    )

    for category, ops in CATEGORIES.items():
        print_ops = []
        for op in ops:
            if op in unsupported_ops:
                print_ops.append(op)
        if len(print_ops) == 0:
            continue

        print(f"\n  {category} ({len(print_ops)}):")
        for op in print_ops:
            unsupported_ops.remove(op)
            print(f"    {op}")

    if len(unsupported_ops) > 0:
        print(f"\n  Other ({len(unsupported_ops)}):")
        for op in sorted(unsupported_ops):
            print(f"    {op}")


if __name__ == "__main__":
    sys.exit(main())
