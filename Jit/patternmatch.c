// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#include "Jit/patternmatch.h"

void parse_instr(_Py_CODEUNIT* instr, int* opcode, int* oparg) {
  _Py_CODEUNIT data = *instr;
  *opcode = _Py_OPCODE(data);
  *oparg = _Py_OPARG(data);
}

int match_op(_Py_CODEUNIT* instr, int opcode) {
  return opcode == _Py_OPCODE(*instr);
}

int match_op_2(_Py_CODEUNIT* instr, int a, int b) {
  int opcode = _Py_OPCODE(*instr);
  return opcode == a || opcode == b;
}

int match_op_n(_Py_CODEUNIT* instr, int n, ...) {
  int oc = _Py_OPCODE(*instr);

  va_list opcodes;
  va_start(opcodes, n);

  int o;
  for (int i = 0; i < n; ++i) {
    o = va_arg(opcodes, int);
    if (oc == o) {
      va_end(opcodes);
      return 1;
    }
  }
  va_end(opcodes);
  return 0;
}

int match_oparg(_Py_CODEUNIT* instr, int opcode, int oparg) {
  int oc, oa;
  parse_instr(instr, &oc, &oa);
  return oc == opcode && oa == oparg;
}

int match_op_save_arg(_Py_CODEUNIT* instr, int opcode, int* oparg) {
  int oc, oa;
  parse_instr(instr, &oc, &oa);
  *oparg = oa;
  return oc == opcode;
}
