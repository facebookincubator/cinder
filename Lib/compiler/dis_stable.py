#!/usr/bin/env python3
#
# Dissassemble code objects:
# a) recursively (like dis.dis() in CPython behaves);
# b) providing stable references to internal code objects (by replacing
#    memory address with incrementing number);
# c) besides disassembly, also dump other fields of code objects.
# Useful for comparing disassembly outputs from different runs.
#
from __future__ import print_function

import dis as _dis
import opcode
import re
import sys
from pprint import pformat
from types import CodeType
from typing import Dict, Generator, Iterable, List, Optional, Pattern, TextIO, Tuple


def _make_stable(
    gen: Iterable[_dis.Instruction],
) -> Generator[_dis.Instruction, None, None]:
    for instr in gen:
        yield _dis.Instruction(
            instr.opname,
            instr.opcode,
            instr.arg,
            instr.argval,
            _stable_repr(instr.argval),
            instr.offset,
            instr.starts_line,
            instr.is_jump_target,
        )


def _stable_repr(obj: object) -> str:
    if isinstance(obj, frozenset):
        replacement = frozenset([i for i in sorted(obj, key=lambda x: repr(x))])
        return repr(replacement)
    return repr(obj)


def _disassemble_bytes(
    code: bytes,
    lasti: int = -1,
    varnames: Optional[Tuple[str]] = None,
    names: Optional[Tuple[str]] = None,
    constants: Optional[Tuple[object]] = None,
    cells: Optional[Tuple[object]] = None,
    linestarts: Optional[Dict[int, int]] = None,
    *,
    file: Optional[TextIO] = None,
    line_offset: int = 0
) -> None:
    # Omit the line number column entirely if we have no line number info
    show_lineno = linestarts is not None
    if show_lineno:
        # pyre-fixme [16]: `Optional` has no attribute `values`.
        maxlineno = max(linestarts.values()) + line_offset
        if maxlineno >= 1000:
            lineno_width = len(str(maxlineno))
        else:
            lineno_width = 3
    else:
        lineno_width = 0
    maxoffset = len(code) - 2
    if maxoffset >= 10000:
        offset_width = len(str(maxoffset))
    else:
        offset_width = 4
    for instr in _make_stable(
        # pyre-fixme [16]: Module `dis` has no attribute `_get_instructions_bytes`
        _dis._get_instructions_bytes(
            code, varnames, names, constants, cells, linestarts, line_offset=line_offset
        )
    ):
        new_source_line = (
            show_lineno and instr.starts_line is not None and instr.offset > 0
        )
        if new_source_line:
            print(file=file)
        is_current_instr = instr.offset == lasti

        print(
            # pyre-fixme [16]: `_dis.Instruction` has no attribute `_disassemble`
            instr._disassemble(lineno_width, is_current_instr, offset_width),
            file=file,
        )


def disassemble(
    co: CodeType,
    lasti: int = -1,
    *,
    file: Optional[TextIO] = None,
    skip_line_nos: bool = False
) -> None:
    cell_names = co.co_cellvars + co.co_freevars
    if skip_line_nos:
        linestarts = None
    else:
        linestarts = dict(_dis.findlinestarts(co))
    _disassemble_bytes(
        co.co_code,
        lasti,
        co.co_varnames,
        co.co_names,
        co.co_consts,
        cell_names,
        linestarts,
        file=file,
    )


class Disassembler:
    def __init__(self) -> None:
        self.id_map: Dict[int, int] = {}
        self.id_cnt: int = 0

    def get_co_id(self, co: CodeType) -> int:
        addr = id(co)
        if addr in self.id_map:
            return self.id_map[addr]
        self.id_map[addr] = self.id_cnt
        self.id_cnt += 1
        return self.id_cnt - 1

    def co_repr(self, co: CodeType) -> str:
        return '<code object %s at #%d, file "%s", line %d>' % (
            co.co_name,
            self.get_co_id(co),
            co.co_filename,
            co.co_firstlineno,
        )

    def disassemble(
        self,
        co: CodeType,
        lasti: int = -1,
        file: Optional[TextIO] = None,
        skip_line_nos: bool = False,
    ) -> None:
        """Disassemble a code object."""
        consts = tuple(
            [self.co_repr(x) if hasattr(x, "co_code") else x for x in co.co_consts]
        )
        codeobj = CodeType(
            co.co_argcount,
            co.co_posonlyargcount,
            co.co_kwonlyargcount,
            co.co_nlocals,
            co.co_stacksize,
            co.co_flags,
            co.co_code,
            consts,
            co.co_names,
            co.co_varnames,
            co.co_filename,
            co.co_name,
            co.co_firstlineno,
            co.co_linetable,
            co.co_freevars,
            co.co_cellvars,
        )
        disassemble(codeobj, file=file, skip_line_nos=skip_line_nos)

    def dump_code(self, co: CodeType, file: Optional[TextIO] = None) -> None:
        if not file:
            file = sys.stdout
        print(self.co_repr(co), file=file)
        self.disassemble(co, file=file, skip_line_nos=True)
        print("co_argcount:", co.co_argcount, file=file)
        print("co_kwonlyargcount:", co.co_kwonlyargcount, file=file)
        print("co_stacksize:", co.co_stacksize, file=file)
        flags = []
        co_flags = co.co_flags
        for val, name in _dis.COMPILER_FLAG_NAMES.items():
            if co_flags & val:
                flags.append(name)
                co_flags &= ~val
        if co_flags:
            flags.append(hex(co_flags))
        print("co_flags:", hex(co.co_flags), "(" + " | ".join(flags) + ")", file=file)
        print(
            "co_consts:",
            pformat(
                tuple(
                    [
                        self.co_repr(x) if hasattr(x, "co_code") else _stable_repr(x)
                        for x in co.co_consts
                    ]
                )
            ),
            file=file,
        )
        print("co_firstlineno:", co.co_firstlineno, file=file)
        print("co_names:", co.co_names, file=file)
        print("co_varnames:", co.co_varnames, file=file)
        print("co_cellvars:", co.co_cellvars, file=file)
        print("co_freevars:", co.co_freevars, file=file)
        print("co_lines:", pformat(list(co.co_lines())), file=file)
        print(file=file)
        for c in co.co_consts:
            if hasattr(c, "co_code"):
                self.dump_code(c, file)


# https://www.python.org/dev/peps/pep-0263/
coding_re: Pattern[bytes] = re.compile(
    rb"^[ \t\f]*#.*?coding[:=][ \t]*([-_.a-zA-Z0-9]+)"
)


def open_with_coding(fname: str) -> TextIO:
    with open(fname, "rb") as f:
        l = f.readline()
        m = coding_re.match(l)
        if not m:
            l = f.readline()
            m = coding_re.match(l)
        encoding = "utf-8"
        if m:
            encoding = m.group(1).decode()
    return open(fname, encoding=encoding)


if __name__ == "__main__":
    with open_with_coding(sys.argv[1]) as f:
        co: CodeType = compile(f.read(), sys.argv[1], "exec")
    Disassembler().dump_code(co, file=sys.stdout)
