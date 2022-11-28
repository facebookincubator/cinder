// Copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)

#pragma once

#include "Python.h"

#include <stdarg.h>

#ifndef Py_LIMITED_API
#ifdef __cplusplus
extern "C" {
#endif

/*
 * Parse an instruction and save the opcode and oparg.
 */
void parse_instr(_Py_CODEUNIT* instr, int* opcode, int* oparg);

/*
 * Parse an instruction and match against the passed opcode.
 * Returns 1 on match, zero otherwise.
 */
int match_op(_Py_CODEUNIT* instr, int opcode);

/*
 * Parse an instruction and match against the either of the passed opcodes.
 * Special case of match_op_n that is much faster than dealing with varargs.
 * Returns 1 on match, zero otherwise.
 */
int match_op_2(_Py_CODEUNIT* instr, int a, int b);

/*
 * Same as match_op but allows you to check multiple opcodes passed via
 * varargs. You must pass the number of opcodes checks in the second param.
 */
int match_op_n(_Py_CODEUNIT* instr, int n, ...);

/*
 * Parse an instruction and match against the passed opcode and oparg.
 * Returns 1 if both match, zero otherwise.
 */
int match_oparg(_Py_CODEUNIT* instr, int opcode, int oparg);

/*
 * Parse an instruction and match against the passed opcode and save the oparg.
 * The oparg will be set regardless of match.
 * Returns 1 on match, zero otherwise.
 */
int match_op_save_arg(_Py_CODEUNIT* instr, int opcode, int* oparg);

/*
 * Macro versions that automatically return 0 from the function if not matched.
 * This makes writing functions that do matching a little cleaner to write.
 */

#define MATCH_OP(instr, opcode)       \
  if (!match_op((instr), (opcode))) { \
    return 0;                         \
  }

#define MATCH_OP_N(instr, n, ...)               \
  if (!match_op_n((instr), (n), __VA_ARGS__)) { \
    return 0;                                   \
  }

#define MATCH_OP_2(instr, op1, op2)         \
  if (!match_op_2((instr), (op1), (op2))) { \
    return 0;                               \
  }

#define MATCH_OPARG(instr, opcode, oparg)         \
  if (!match_oparg((instr), (opcode), (oparg))) { \
    return 0;                                     \
  }

#define MATCH_OP_SAVE_ARG(instr, opcode, oparg_ptr)         \
  if (!match_op_save_arg((instr), (opcode), (oparg_ptr))) { \
    return 0;                                               \
  }

#ifdef __cplusplus
}
#endif
#endif /* Py_LIMITED_API */
