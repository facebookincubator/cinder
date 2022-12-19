import dis
import os
import sys
import types
import unittest
from compiler import compile, opcode_static as op
from inspect import cleandoc
from typing import List, Tuple

sys.path.append(os.path.join(sys.path[0], "..", "fuzzer"))

import cfgutil
import verifier
from cfgutil import BytecodeOp
from verifier import VerificationError, Verifier


class VerifierTests(unittest.TestCase):
    def convert_bytecodes_to_byte_representation(self, bytecodes: List[Tuple]) -> bytes:
        b_list = []
        if len(bytecodes) == 0:
            return b""
        for i in bytecodes:
            if isinstance(i, tuple) and len(i) == 2:
                b_list.append(i[0])
                b_list.append(i[1])
            else:
                b_list.append(i)
        return bytes(b_list)

    def compile_helper(
        self,
        source: str,
        bytecodes: List[Tuple] = None,
        consts: tuple = None,
        varnames: tuple = None,
        names: tuple = None,
        freevars: tuple = None,
        cellvars: tuple = None,
        stacksize: int = None,
    ) -> types.CodeType:
        code = compile(source, "", "exec")
        if bytecodes is not None:
            code = code.replace(
                co_code=self.convert_bytecodes_to_byte_representation(bytecodes)
            )
        if stacksize is not None:
            code = code.replace(co_stacksize=stacksize)
        if consts is not None:
            code = code.replace(co_consts=consts)
        if varnames is not None:
            code = code.replace(co_varnames=varnames)
        if names is not None:
            code = code.replace(co_names=names)
        if freevars is not None:
            code = code.replace(co_freevars=freevars)
        if cellvars is not None:
            code = code.replace(co_cellvars=cellvars)
        return code


class VerifierBasicsTest(VerifierTests):
    def test_length_cannot_be_odd(self):
        code = self.compile_helper(
            "", [(op.opcode.LOAD_CONST, 57), (op.opcode.POP_TOP)]
        )
        self.assertRaisesRegex(
            VerificationError,
            "Bytecode length cannot be odd",
            Verifier.validate_code,
            code,
        )

    def test_length_cannot_be_zero(self):
        code = self.compile_helper("", [])
        self.assertRaisesRegex(
            VerificationError,
            "Bytecode length cannot be zero or negative",
            Verifier.validate_code,
            code,
        )

    def test_op_name_must_exist(self):
        code = self.compile_helper(
            "", [(op.opcode.LOAD_CONST, 0), (7, 68)]
        )  # 7 is not a valid opcode id
        self.assertRaisesRegex(
            VerificationError,
            "Operation 7 at offset 2 does not exist",
            Verifier.validate_code,
            code,
        )

    def test_cannot_jump_outside_of_file(self):
        code = self.compile_helper(
            "", [(op.opcode.LOAD_CONST, 0), (op.opcode.JUMP_ABSOLUTE, 153)]
        )
        self.assertRaisesRegex(
            VerificationError,
            "can not jump out of bounds$",
            Verifier.validate_code,
            code,
        )

    def test_error_in_nested_code_object(self):
        # nested code object has an invalid jump which should be caught
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_CONST, 0), (op.opcode.POP_TOP, 0)],
            consts=(
                (
                    self.compile_helper(
                        "", [(op.opcode.LOAD_CONST, 0), (op.opcode.JUMP_ABSOLUTE, 153)]
                    ),
                )
            ),
        )
        self.assertRaisesRegex(
            VerificationError,
            "can not jump out of bounds$",
            Verifier.validate_code,
            code,
        )


class VerifierCFGTests(VerifierTests):
    def test_cfg_with_single_block(self):
        source = cleandoc(
            """
            x = 3
            x += 1"""
        )
        code = self.compile_helper(source)
        block_map = Verifier.create_blocks(Verifier.parse_bytecode(code.co_code))
        assert str(block_map) == cleandoc(
            """
        bb0:
          LOAD_CONST : 0
          STORE_NAME : 0
          LOAD_NAME : 0
          LOAD_CONST : 1
          INPLACE_ADD : 0
          STORE_NAME : 0
          LOAD_CONST : 2
          RETURN_VALUE : 0"""
        )

    def test_cfg_with_conditional(self):
        source = cleandoc(
            """
            x, y = 3, 0
            if x > 0:
                y += 1
            elif x == 0:
                y += 3
            else:
                y += 2"""
        )
        code = self.compile_helper(source)
        block_map = Verifier.create_blocks(Verifier.parse_bytecode(code.co_code))
        self.assertEqual(
            str(block_map),
            cleandoc(
                """
        bb0:
          LOAD_CONST : 0
          UNPACK_SEQUENCE : 2
          STORE_NAME : 0
          STORE_NAME : 1
          LOAD_NAME : 0
          LOAD_CONST : 1
          COMPARE_OP : 4
          POP_JUMP_IF_FALSE bb2
        bb1:
          LOAD_NAME : 1
          LOAD_CONST : 2
          INPLACE_ADD : 0
          STORE_NAME : 1
          LOAD_CONST : 5
          RETURN_VALUE : 0
        bb2:
          LOAD_NAME : 0
          LOAD_CONST : 1
          COMPARE_OP : 2
          POP_JUMP_IF_FALSE bb4
        bb3:
          LOAD_NAME : 1
          LOAD_CONST : 3
          INPLACE_ADD : 0
          STORE_NAME : 1
          LOAD_CONST : 5
          RETURN_VALUE : 0
        bb4:
          LOAD_NAME : 1
          LOAD_CONST : 4
          INPLACE_ADD : 0
          STORE_NAME : 1
          LOAD_CONST : 5
          RETURN_VALUE : 0"""
            ),
        )

    def test_cfg_with_loop(self):
        source = cleandoc(
            """
            arr = []
            i = 0
            while i < 10:
              arr.append(i)
              i+=1"""
        )
        code = self.compile_helper(source)
        block_map = Verifier.create_blocks(Verifier.parse_bytecode(code.co_code))
        self.assertEqual(
            str(block_map),
            cleandoc(
                """
        bb0:
          BUILD_LIST : 0
          STORE_NAME : 0
          LOAD_CONST : 0
          STORE_NAME : 1
          LOAD_NAME : 1
          LOAD_CONST : 1
          COMPARE_OP : 0
          POP_JUMP_IF_FALSE bb3
        bb1:
          LOAD_NAME : 0
          LOAD_METHOD : 2
          LOAD_NAME : 1
          CALL_METHOD : 1
          POP_TOP : 0
          LOAD_NAME : 1
          LOAD_CONST : 2
          INPLACE_ADD : 0
          STORE_NAME : 1
          LOAD_NAME : 1
          LOAD_CONST : 1
          COMPARE_OP : 0
          POP_JUMP_IF_TRUE bb1
        bb2:
          LOAD_CONST : 3
          RETURN_VALUE : 0
        bb3:
          LOAD_CONST : 3
          RETURN_VALUE : 0"""
            ),
        )

    def test_cfg_with_function_call(self):
        source = cleandoc(
            """
            arr = []
            for i in range(10):
              arr.append(i)"""
        )
        code = self.compile_helper(source)
        block_map = Verifier.create_blocks(Verifier.parse_bytecode(code.co_code))
        self.assertEqual(
            str(block_map),
            cleandoc(
                """
        bb0:
          BUILD_LIST : 0
          STORE_NAME : 0
          LOAD_NAME : 1
          LOAD_CONST : 0
          CALL_FUNCTION : 1
          GET_ITER : 0
        bb1:
          FOR_ITER bb3
        bb2:
          STORE_NAME : 2
          LOAD_NAME : 0
          LOAD_METHOD : 3
          LOAD_NAME : 2
          CALL_METHOD : 1
          POP_TOP : 0
          JUMP_ABSOLUTE bb1
        bb3:
          LOAD_CONST : 1
          RETURN_VALUE : 0"""
            ),
        )

    def test_cfg_try_except(self):
        source = cleandoc(
            """
        y = 9
        a = 3
        try:
            c = y+a
        except:
            raise
        """
        )
        code = self.compile_helper(source)
        block_map = Verifier.create_blocks(Verifier.parse_bytecode(code.co_code))
        self.assertEqual(
            str(block_map),
            cleandoc(
                """
        bb0:
          LOAD_CONST : 0
          STORE_NAME : 0
          LOAD_CONST : 1
          STORE_NAME : 1
          SETUP_FINALLY : 7
          LOAD_NAME : 0
          LOAD_NAME : 1
          BINARY_ADD : 0
          STORE_NAME : 2
          POP_BLOCK : 0
          LOAD_CONST : 2
          RETURN_VALUE : 0
        bb1:
          POP_TOP : 0
          POP_TOP : 0
          POP_TOP : 0
          RAISE_VARARGS : 0"""
            ),
        )

    def test_cfg_try_except_else_finally(self):
        source = cleandoc(
            """
        y = 9
        a = "b"
        try:
            c = y+a
        except:
            raise
        else:
            y+=1
        finally:
            y+=3
        """
        )
        code = self.compile_helper(source)
        block_map = Verifier.create_blocks(Verifier.parse_bytecode(code.co_code))
        self.assertEqual(
            str(block_map),
            cleandoc(
                """
        bb0:
          LOAD_CONST : 0
          STORE_NAME : 0
          LOAD_CONST : 1
          STORE_NAME : 1
          SETUP_FINALLY : 22
          SETUP_FINALLY : 6
          LOAD_NAME : 0
          LOAD_NAME : 1
          BINARY_ADD : 0
          STORE_NAME : 2
          POP_BLOCK : 0
          JUMP_FORWARD bb2
        bb1:
          POP_TOP : 0
          POP_TOP : 0
          POP_TOP : 0
          RAISE_VARARGS : 0
        bb2:
          LOAD_NAME : 0
          LOAD_CONST : 2
          INPLACE_ADD : 0
          STORE_NAME : 0
          POP_BLOCK : 0
          LOAD_NAME : 0
          LOAD_CONST : 3
          INPLACE_ADD : 0
          STORE_NAME : 0
          LOAD_CONST : 4
          RETURN_VALUE : 0
        bb3:
          LOAD_NAME : 0
          LOAD_CONST : 3
          INPLACE_ADD : 0
          STORE_NAME : 0
          RERAISE : 0"""
            ),
        )

    def test_cfg_continue_statement_in_try(self):
        source = cleandoc(
            """
        for i in range(10):
            x = 0
            z = 2
            try:
                y = x+z
                continue
            except:
                raise
            finally:
                x+=1
        """
        )
        code = self.compile_helper(source)
        block_map = Verifier.create_blocks(Verifier.parse_bytecode(code.co_code))
        self.assertEqual(
            str(block_map),
            cleandoc(
                """
        bb0:
          LOAD_NAME : 0
          LOAD_CONST : 0
          CALL_FUNCTION : 1
          GET_ITER : 0
        bb1:
          FOR_ITER bb5
        bb2:
          STORE_NAME : 1
          LOAD_CONST : 1
          STORE_NAME : 2
          LOAD_CONST : 2
          STORE_NAME : 3
          SETUP_FINALLY : 16
          SETUP_FINALLY : 11
          LOAD_NAME : 2
          LOAD_NAME : 3
          BINARY_ADD : 0
          STORE_NAME : 4
          POP_BLOCK : 0
          POP_BLOCK : 0
          LOAD_NAME : 2
          LOAD_CONST : 3
          INPLACE_ADD : 0
          STORE_NAME : 2
          JUMP_ABSOLUTE bb1
        bb3:
          POP_TOP : 0
          POP_TOP : 0
          POP_TOP : 0
          RAISE_VARARGS : 0
        bb4:
          LOAD_NAME : 2
          LOAD_CONST : 3
          INPLACE_ADD : 0
          STORE_NAME : 2
          RERAISE : 0
        bb5:
          LOAD_CONST : 4
          RETURN_VALUE : 0"""
            ),
        )


class VerifierStackDepthTests(VerifierTests):
    def test_cannot_pop_from_empty_stack(self):
        code = self.compile_helper(
            "",
            [(op.opcode.LOAD_CONST, 0), (op.opcode.POP_TOP, 0), (op.opcode.POP_TOP, 0)],
        )
        self.assertRaisesRegex(
            VerificationError,
            "Stack depth -1 dips below minimum of 0 for operation POP_TOP @ offset 4",
            Verifier.validate_code,
            code,
        )

    def test_stack_depth_cannot_exceed_max(self):
        code = self.compile_helper(
            "",
            [
                (op.opcode.LOAD_CONST, 0),
                (op.opcode.LOAD_CONST, 0),
                (op.opcode.LOAD_CONST, 0),
            ],
            stacksize=1,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Stack depth 2 exceeds maximum of 1 for operation LOAD_CONST @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_branch(self):
        source = cleandoc(
            """
        x, y = 0, 0
        if x: y += 1
        else: y += 3"""
        )
        code = self.compile_helper(source)
        self.assertTrue(Verifier.validate_code(code))

    def test_for_loop(self):
        source = cleandoc(
            """
        x = 0
        for i in range(10):
          x += 1"""
        )
        code = self.compile_helper(source)
        self.assertTrue(Verifier.validate_code(code))

    def test_while_loop(self):
        source = cleandoc(
            """
        i = 0
        while i < 10:
          i+=1"""
        )
        code = self.compile_helper(source)
        self.assertTrue(Verifier.validate_code(code))

    def test_cannot_pop_from_empty_stack_during_loop(self):
        bytecodes = [
            (op.opcode.LOAD_CONST, 0),
            (op.opcode.UNPACK_SEQUENCE, 2),
            (op.opcode.STORE_NAME, 0),
            (op.opcode.STORE_NAME, 1),
            (op.opcode.LOAD_NAME, 0),
            (op.opcode.POP_JUMP_IF_FALSE, 11),
            (op.opcode.LOAD_NAME, 1),
            (op.opcode.LOAD_CONST, 1),
            (op.opcode.POP_TOP, 0),
            (op.opcode.INPLACE_ADD, 0),
            (op.opcode.STORE_NAME, 1),
            (op.opcode.JUMP_FORWARD, 4),
            (op.opcode.LOAD_NAME, 1),
            (op.opcode.LOAD_CONST, 2),
            (op.opcode.INPLACE_ADD, 0),
            (op.opcode.STORE_NAME, 1),
            (op.opcode.LOAD_CONST, 3),
            (op.opcode.RETURN_VALUE, 0),
        ]
        code = self.compile_helper(
            "", bytecodes, consts=(1, 2, 3, 4), names=("e", "e", "e"), stacksize=10
        )
        self.assertRaisesRegex(
            VerificationError,
            "Stack depth -1 dips below minimum of 0 for operation STORE_NAME @ offset 20",
            Verifier.validate_code,
            code,
        )

    def test_stack_depth_should_not_exceed_max_while_looping(self):
        bytecodes = [
            (op.opcode.LOAD_CONST, 0),
            (op.opcode.UNPACK_SEQUENCE, 2),
            (op.opcode.STORE_NAME, 0),
            (op.opcode.STORE_NAME, 1),
            (op.opcode.LOAD_NAME, 0),
            (op.opcode.POP_JUMP_IF_FALSE, 11),
            (op.opcode.LOAD_NAME, 1),
            (op.opcode.LOAD_CONST, 1),
            (op.opcode.LOAD_CONST, 1),
            (op.opcode.LOAD_CONST, 1),
            (op.opcode.LOAD_CONST, 1),
            (op.opcode.LOAD_CONST, 1),
            (op.opcode.POP_TOP, 0),
            (op.opcode.INPLACE_ADD, 0),
            (op.opcode.STORE_NAME, 1),
            (op.opcode.JUMP_FORWARD, 4),
            (op.opcode.LOAD_NAME, 1),
            (op.opcode.LOAD_CONST, 2),
            (op.opcode.INPLACE_ADD, 0),
            (op.opcode.STORE_NAME, 1),
            (op.opcode.LOAD_CONST, 3),
            (op.opcode.RETURN_VALUE, 0),
        ]
        code = self.compile_helper(
            "", bytecodes, consts=(1, 2, 3, 4), names=("e", "e", "e")
        )
        self.assertRaisesRegex(
            VerificationError,
            "Stack depth 2 exceeds maximum of 1 for operation UNPACK_SEQUENCE @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_branch_with_nested_conditions(self):
        source = cleandoc(
            """
        x, y, arr = 0, 0, []
        if x:
          y = 5 if x > 3 else 3
        else:
          arr.append(x)
        arr.append(y)"""
        )
        code = self.compile_helper(source)
        self.assertTrue(Verifier.validate_code(code))

    def test_nested_loop(self):
        source = cleandoc(
            """
        x, arr = 0, []
        for i in range(10):
          for j in range(12):
            x+=3
            arr.append(x)"""
        )
        code = self.compile_helper(source)
        self.assertTrue(Verifier.validate_code(code))

    def test_while_loop_with_multiple_conditions(self):
        source = cleandoc(
            """
        i, stack = 0, [1, 2, 3, 4]
        while stack and i < 10:
          stack.pop()
          i+=3"""
        )
        code = self.compile_helper(source)
        self.assertTrue(Verifier.validate_code(code))

    def test_recursive_function_maintains_stack_depth(self):
        source = cleandoc(
            """
        y = 7
        def f(x):
          if x == 0:
            return 0
          if x == 1:
            return 1
          return f(x-1) + f(x-2)
        print(f(y))"""
        )
        code = self.compile_helper(source)
        self.assertTrue(Verifier.validate_code(code))

    def test_try_except_maintains_stack_depth(self):
        source = cleandoc(
            """
        y = 9
        a = 3
        try:
            c = y+a
        except:
            raise
        """
        )
        code = self.compile_helper(source)
        self.assertTrue(Verifier.validate_code(code))

    def test_try_except_else_finally_maintains_stack_depth(self):
        source = cleandoc(
            """
        y = 9
        a = "b"
        try:
            c = y+a
        except:
            raise
        else:
            y+=1
        finally:
            y+=3
        """
        )
        code = self.compile_helper(source)
        self.assertTrue(Verifier.validate_code(code))

    def test_try_except_not_handled_by_except(self):
        source = cleandoc(
            """
        y = 9
        a = "b"
        try:
            c = y+a
        except IndexError:
            raise
        finally:
            y+=3
        """
        )
        code = self.compile_helper(source)
        self.assertTrue(Verifier.validate_code(code))

    def test_try_except_with_continue_statement_in_try(self):
        source = cleandoc(
            """
        for i in range(10):
            x = 0
            z = 2
            try:
                y = x+z
                continue
            except:
                raise
            finally:
                x+=1
        """
        )
        code = self.compile_helper(source)
        self.assertTrue(Verifier.validate_code(code))

    def test_try_except_with_break_statement_in_finally(self):
        source = cleandoc(
            """
        for i in range(10):
            x = 0
            z = 2
            try:
                y = x+z
                continue
            except:
                raise
            finally:
                break
        """
        )
        code = self.compile_helper(source)
        self.assertTrue(Verifier.validate_code(code))

    def test_try_except_with_return_statements(self):
        source = cleandoc(
            """
        def f(x, y):
            try:
                x += y
                return x
            except:
                raise
            finally:
                return y
        """
        )
        code = self.compile_helper(source)
        self.assertTrue(Verifier.validate_code(code))


class VerifierOpArgTests(VerifierTests):
    # Testing invalid oparg types is not possible in certain cases
    # Since the compiler will abort
    # i.e. Creating / Modifying a code object by putting a non string type in co_varnames
    def test_LOAD_CONST_with_valid_oparg_index_is_successful(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.LOAD_CONST, 0)], consts=(3,)
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_LOAD_CONST_oparg_type_can_be_any_object(self):
        code = self.compile_helper(
            "",
            bytecodes=[
                (op.opcode.LOAD_CONST, 0),
                (op.opcode.LOAD_CONST, 1),
                (op.opcode.LOAD_CONST, 2),
            ],
            consts=(3, None, "hello"),
            stacksize=4,
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_LOAD_CONST_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_CONST, 0), (op.opcode.LOAD_CONST, 1)],
            consts=(3,),
            stacksize=2,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 1 out of bounds for size 1 for operation LOAD_CONST @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_LOAD_CLASS_with_valid_oparg_index_is_successful(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.LOAD_CLASS, 0)], consts=((1, 3),)
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_LOAD_CLASS_oparg_type_can_be_any_tuple(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.LOAD_CLASS, 0)], consts=(tuple(),), stacksize=4
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_LOAD_CLASS_oparg_type_cannot_be_non_tuple(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.LOAD_CLASS, 0)], consts=(object(),), stacksize=1
        )
        self.assertRaisesRegex(
            VerificationError,
            "Incorrect oparg type of object, expected tuple for operation LOAD_CLASS @ offset 0",
            Verifier.validate_code,
            code,
        )

    def test_LOAD_CLASS_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_CONST, 0), (op.opcode.LOAD_CLASS, 1)],
            consts=(3,),
            stacksize=2,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 1 out of bounds for size 1 for operation LOAD_CLASS @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_LOAD_FIELD_with_valid_oparg_index_is_successful(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.LOAD_CONST, 0)], consts=((1, 3),)
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_LOAD_FIELD_oparg_type_can_be_any_tuple(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.LOAD_FIELD, 0)], consts=(tuple(),), stacksize=1
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_LOAD_FIELD_oparg_type_cannot_be_non_tuple(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.LOAD_FIELD, 0)], consts=(object(),), stacksize=1
        )
        self.assertRaisesRegex(
            VerificationError,
            "Incorrect oparg type of object, expected tuple for operation LOAD_FIELD @ offset 0",
            Verifier.validate_code,
            code,
        )

    def test_LOAD_FIELD_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_CONST, 0), (op.opcode.LOAD_CONST, 1)],
            consts=(3,),
            stacksize=2,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 1 out of bounds for size 1 for operation LOAD_CONST @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_STORE_FIELD_with_valid_oparg_index_is_successful(self):
        code = self.compile_helper(
            "",
            bytecodes=[
                (op.opcode.LOAD_CONST, 0),
                (op.opcode.LOAD_CONST, 1),
                (op.opcode.STORE_FIELD, 1),
            ],
            consts=(3, (1, 3)),
            stacksize=4,
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_STORE_FIELD_oparg_type_can_be_any_tuple(self):
        code = self.compile_helper(
            "",
            bytecodes=[
                (op.opcode.LOAD_CONST, 0),
                (op.opcode.LOAD_CONST, 1),
                (op.opcode.LOAD_CONST, 2),
                (op.opcode.STORE_FIELD, 3),
            ],
            consts=(3, 2, 1, tuple()),
            stacksize=4,
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_STORE_FIELD_oparg_type_cannot_be_non_tuple(self):
        code = self.compile_helper(
            "",
            bytecodes=[
                (op.opcode.LOAD_CONST, 0),
                (op.opcode.LOAD_CONST, 1),
                (op.opcode.LOAD_CONST, 2),
                (op.opcode.STORE_FIELD, 3),
            ],
            consts=(3, 2, 1, object()),
            stacksize=4,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Incorrect oparg type of object, expected tuple for operation STORE_FIELD @ offset 6",
            Verifier.validate_code,
            code,
        )

    def test_STORE_FIELD_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_CONST, 0), (op.opcode.STORE_FIELD, 1)],
            consts=(3,),
            stacksize=2,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 1 out of bounds for size 1 for operation STORE_FIELD @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_CAST_with_valid_oparg_index_is_successful(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.CAST, 0)], consts=((1, 3),), stacksize=4
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_CAST_oparg_type_can_be_any_tuple(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.CAST, 0)], consts=(tuple(),), stacksize=4
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_CAST_oparg_type_cannot_be_non_tuple(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.CAST, 0)], consts=(object(),), stacksize=1
        )
        self.assertRaisesRegex(
            VerificationError,
            "Incorrect oparg type of object, expected tuple for operation CAST @ offset 0",
            Verifier.validate_code,
            code,
        )

    def test_CAST_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_CONST, 0), (op.opcode.CAST, 1)],
            consts=(3,),
            stacksize=2,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 1 out of bounds for size 1 for operation CAST @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_PRIMITIVE_BOX_with_valid_oparg_index_is_successful(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.PRIMITIVE_BOX, 0)], consts=((1, 3),), stacksize=1
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_PRIMITIVE_BOX_oparg_type_can_be_any_tuple(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.PRIMITIVE_BOX, 0)], consts=(tuple(),), stacksize=4
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_PRIMITIVE_BOX_oparg_type_cannot_be_non_tuple(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.PRIMITIVE_BOX, 0)],
            consts=(object(),),
            stacksize=1,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Incorrect oparg type of object, expected tuple for operation PRIMITIVE_BOX @ offset 0",
            Verifier.validate_code,
            code,
        )

    def test_PRIMITIVE_BOX_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_CONST, 0), (op.opcode.PRIMITIVE_BOX, 1)],
            consts=(3,),
            stacksize=2,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 1 out of bounds for size 1 for operation PRIMITIVE_BOX @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_PRIMITIVE_UNBOX_with_valid_oparg_index_is_successful(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.PRIMITIVE_UNBOX, 0)],
            consts=((1, 3),),
            stacksize=1,
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_PRIMITIVE_UNBOX_oparg_type_can_be_any_tuple(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.PRIMITIVE_UNBOX, 0)],
            consts=(tuple(),),
            stacksize=4,
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_PRIMITIVE_UNBOX_oparg_type_cannot_be_non_tuple(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.PRIMITIVE_UNBOX, 0)],
            consts=(object(),),
            stacksize=1,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Incorrect oparg type of object, expected tuple for operation PRIMITIVE_UNBOX @ offset 0",
            Verifier.validate_code,
            code,
        )

    def test_PRIMITIVE_UNBOX_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_CONST, 0), (op.opcode.PRIMITIVE_UNBOX, 1)],
            consts=(3,),
            stacksize=2,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 1 out of bounds for size 1 for operation PRIMITIVE_UNBOX @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_TP_ALLOC_with_valid_oparg_index_is_successful(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.TP_ALLOC, 0)], consts=((1, 2),), stacksize=1
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_TP_ALLOC_oparg_type_can_be_any_tuple(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.TP_ALLOC, 0)], consts=(tuple(),), stacksize=1
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_TP_ALLOC_oparg_type_cannot_be_non_tuple(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.TP_ALLOC, 0)], consts=(object(),), stacksize=1
        )
        self.assertRaisesRegex(
            VerificationError,
            "Incorrect oparg type of object, expected tuple for operation TP_ALLOC @ offset 0",
            Verifier.validate_code,
            code,
        )

    def test_TP_ALLOC_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_CONST, 0), (op.opcode.TP_ALLOC, 1)],
            consts=(3,),
            stacksize=2,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 1 out of bounds for size 1 for operation TP_ALLOC @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_CHECK_ARGS_with_valid_oparg_index_is_successful(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.CHECK_ARGS, 0)], consts=((1, 3),), stacksize=1
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_CHECK_ARGS_oparg_type_can_be_any_tuple(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.CHECK_ARGS, 0)], consts=(tuple(),), stacksize=1
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_CHECK_ARGS_oparg_type_cannot_be_non_tuple(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.CHECK_ARGS, 0)], consts=(object(),), stacksize=1
        )
        self.assertRaisesRegex(
            VerificationError,
            "Incorrect oparg type of object, expected tuple for operation CHECK_ARGS @ offset 0",
            Verifier.validate_code,
            code,
        )

    def test_CHECK_ARGS_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_CONST, 0), (op.opcode.CHECK_ARGS, 1)],
            consts=(3,),
            stacksize=2,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 1 out of bounds for size 1 for operation CHECK_ARGS @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_PRIMITIVE_LOAD_CONST_with_valid_oparg_index_is_successful(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.PRIMITIVE_LOAD_CONST, 0)],
            consts=(3,),
            stacksize=1,
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_PRIMITIVE_LOAD_CONST_oparg_type_can_be_any_int(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.PRIMITIVE_LOAD_CONST, 0)],
            consts=(1,),
            stacksize=1,
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_PRIMITIVE_LOAD_CONST_oparg_type_cannot_be_non_int(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.PRIMITIVE_LOAD_CONST, 0)],
            consts=("h",),
            stacksize=1,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Incorrect oparg type of str, expected int for operation PRIMITIVE_LOAD_CONST @ offset 0",
            Verifier.validate_code,
            code,
        )

    def test_PRIMITIVE_LOAD_CONST_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_CONST, 0), (op.opcode.PRIMITIVE_LOAD_CONST, 1)],
            consts=(3,),
            stacksize=2,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 1 out of bounds for size 1 for operation PRIMITIVE_LOAD_CONST @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_REFINE_TYPE_with_valid_oparg_index_is_successful(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.REFINE_TYPE, 0)],
            consts=(("a", "b"),),
            stacksize=1,
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_REFINE_TYPE_oparg_type_can_be_any_tuple(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.REFINE_TYPE, 0)],
            consts=(("s", "str", "s"),),
            stacksize=1,
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_REFINE_TYPE_oparg_cannot_be_non_tuple(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.REFINE_TYPE, 0)], consts=("h",), stacksize=1
        )
        self.assertRaisesRegex(
            VerificationError,
            "Incorrect oparg type of str, expected tuple for operation REFINE_TYPE @ offset 0",
            Verifier.validate_code,
            code,
        )

    def test_REFINE_TYPE_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_CONST, 0), (op.opcode.REFINE_TYPE, 1)],
            consts=(3,),
            stacksize=2,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 1 out of bounds for size 1 for operation REFINE_TYPE @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_LOAD_FAST_with_valid_oparg_index_is_successful(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.LOAD_FAST, 0)], varnames=("h",), stacksize=1
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_LOAD_FAST_oparg_type_can_be_any_str(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.LOAD_FAST, 0)], varnames=(str(),), stacksize=1
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_LOAD_FAST_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_FAST, 0), (op.opcode.LOAD_FAST, 1)],
            varnames=("h",),
            stacksize=2,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 1 out of bounds for size 1 for operation LOAD_FAST @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_STORE_FAST_with_valid_oparg_index_is_successful(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_FAST, 0), (op.opcode.STORE_FAST, 1)],
            varnames=("h", "h"),
            stacksize=2,
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_STORE_FAST_oparg_type_can_be_any_str(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_FAST, 0), (op.opcode.STORE_FAST, 1)],
            varnames=("h", str()),
            stacksize=2,
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_STORE_FAST_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_FAST, 0), (op.opcode.STORE_FAST, 1)],
            varnames=("h",),
            stacksize=2,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 1 out of bounds for size 1 for operation STORE_FAST @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_DELETE_FAST_with_valid_oparg_index_is_successful(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.DELETE_FAST, 0)], varnames=("h",), stacksize=1
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_DELETE_FAST_oparg_type_can_be_any_str(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.DELETE_FAST, 0)], varnames=(str(),), stacksize=1
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_DELETE_FAST_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_FAST, 0), (op.opcode.DELETE_FAST, 1)],
            varnames=("h",),
            stacksize=2,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 1 out of bounds for size 1 for operation DELETE_FAST @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_LOAD_NAME_with_valid_oparg_index_is_successful(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.LOAD_NAME, 0)], names=("h",), stacksize=1
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_LOAD_NAME_oparg_type_can_be_any_str(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.LOAD_NAME, 0)], names=(str(),), stacksize=1
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_LOAD_NAME_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_NAME, 0), (op.opcode.LOAD_NAME, 1)],
            names=("h",),
            stacksize=2,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 1 out of bounds for size 1 for operation LOAD_NAME @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_LOAD_GLOBAL_with_valid_oparg_index_is_successful(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.LOAD_GLOBAL, 0)], names=("h",), stacksize=1
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_LOAD_GLOBAL_oparg_type_can_be_any_str(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.LOAD_GLOBAL, 0)], names=(str(),), stacksize=1
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_LOAD_GLOBAL_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_NAME, 0), (op.opcode.LOAD_GLOBAL, 1)],
            names=("h",),
            stacksize=2,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 1 out of bounds for size 1 for operation LOAD_GLOBAL @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_STORE_GLOBAL_with_valid_oparg_index_is_successful(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_NAME, 0), (op.opcode.STORE_GLOBAL, 1)],
            names=("h", "h"),
            stacksize=2,
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_STORE_GLOBAL_oparg_type_can_be_any_str(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_NAME, 0), (op.opcode.STORE_GLOBAL, 1)],
            names=("h", str()),
            stacksize=2,
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_STORE_GLOBAL_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_NAME, 0), (op.opcode.STORE_GLOBAL, 1)],
            names=("h",),
            stacksize=2,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 1 out of bounds for size 1 for operation STORE_GLOBAL @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_DELETE_GLOBAL_with_valid_oparg_index_is_successful(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.DELETE_GLOBAL, 0)], names=("h",), stacksize=1
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_DELETE_GLOBAL_oparg_type_can_be_any_str(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.DELETE_GLOBAL, 0)], names=(str(),), stacksize=1
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_DELETE_GLOBAL_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_NAME, 0), (op.opcode.DELETE_GLOBAL, 1)],
            names=("h",),
            stacksize=2,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 1 out of bounds for size 1 for operation DELETE_GLOBAL @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_STORE_NAME_with_valid_oparg_index_is_successful(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_NAME, 0), (op.opcode.STORE_NAME, 1)],
            names=("h", "h"),
            stacksize=2,
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_STORE_NAME_oparg_type_can_be_any_str(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_NAME, 0), (op.opcode.STORE_NAME, 1)],
            names=("h", str()),
            stacksize=2,
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_STORE_NAME_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_NAME, 0), (op.opcode.STORE_NAME, 1)],
            names=("h",),
            stacksize=2,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 1 out of bounds for size 1 for operation STORE_NAME @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_DELETE_NAME_with_valid_oparg_index_is_successful(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.DELETE_NAME, 0)], names=("h",), stacksize=1
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_DELETE_NAME_oparg_type_can_be_any_str(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.DELETE_NAME, 0)], names=(str(),), stacksize=1
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_DELETE_NAME_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_NAME, 0), (op.opcode.DELETE_NAME, 1)],
            names=("h",),
            stacksize=2,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 1 out of bounds for size 1 for operation DELETE_NAME @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_IMPORT_NAME_with_valid_oparg_index_is_successful(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_NAME, 0), (op.opcode.IMPORT_NAME, 1)],
            names=("h", "h"),
            stacksize=2,
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_IMPORT_NAME_oparg_type_can_be_any_str(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_NAME, 0), (op.opcode.IMPORT_NAME, 1)],
            names=("h", str()),
            stacksize=2,
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_IMPORT_NAME_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_NAME, 0), (op.opcode.IMPORT_NAME, 1)],
            names=("h",),
            stacksize=2,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 1 out of bounds for size 1 for operation IMPORT_NAME @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_IMPORT_FROM_with_valid_oparg_index_is_successful(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.IMPORT_FROM, 0)], names=("h",), stacksize=1
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_IMPORT_FROM_oparg_type_can_be_any_str(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.IMPORT_FROM, 0)], names=(str(),), stacksize=1
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_IMPORT_FROM_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_NAME, 0), (op.opcode.IMPORT_FROM, 1)],
            names=("h",),
            stacksize=2,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 1 out of bounds for size 1 for operation IMPORT_FROM @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_STORE_ATTR_with_valid_oparg_index_is_successful(self):
        code = self.compile_helper(
            "",
            bytecodes=[
                (op.opcode.LOAD_NAME, 0),
                (op.opcode.LOAD_NAME, 1),
                (op.opcode.STORE_ATTR, 2),
            ],
            names=("h", "h", "h"),
            stacksize=3,
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_STORE_ATTR_oparg_type_can_be_any_str(self):
        code = self.compile_helper(
            "",
            bytecodes=[
                (op.opcode.LOAD_NAME, 0),
                (op.opcode.LOAD_NAME, 1),
                (op.opcode.STORE_ATTR, 2),
            ],
            names=("h", "h", str()),
            stacksize=3,
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_STORE_ATTR_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_NAME, 0), (op.opcode.STORE_ATTR, 1)],
            names=("h",),
            stacksize=2,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 1 out of bounds for size 1 for operation STORE_ATTR @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_LOAD_ATTR_with_valid_oparg_index_is_successful(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.LOAD_ATTR, 0)], names=("h",), stacksize=1
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_LOAD_ATTR_oparg_type_can_be_any_str(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.LOAD_ATTR, 0)], names=(str(),), stacksize=1
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_LOAD_ATTR_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_NAME, 0), (op.opcode.LOAD_ATTR, 1)],
            names=("h",),
            stacksize=2,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 1 out of bounds for size 1 for operation LOAD_ATTR @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_DELETE_ATTR_with_valid_oparg_index_is_successful(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_NAME, 0), (op.opcode.DELETE_ATTR, 1)],
            names=("h", "h"),
            stacksize=2,
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_DELETE_ATTR_oparg_type_can_be_any_str(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_NAME, 0), (op.opcode.DELETE_ATTR, 1)],
            names=("h", str()),
            stacksize=2,
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_DELETE_ATTR_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_NAME, 0), (op.opcode.DELETE_ATTR, 1)],
            names=("h",),
            stacksize=2,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 1 out of bounds for size 1 for operation DELETE_ATTR @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_LOAD_METHOD_with_valid_oparg_index_is_successful(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.LOAD_METHOD, 0)], names=("h",), stacksize=1
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_LOAD_METHOD_oparg_type_can_be_any_str(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.LOAD_METHOD, 0)], names=(str(),), stacksize=1
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_LOAD_METHOD_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_NAME, 0), (op.opcode.LOAD_METHOD, 1)],
            names=("h",),
            stacksize=2,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 1 out of bounds for size 1 for operation LOAD_METHOD @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_LOAD_DEREF_with_valid_oparg_index_in_freevars_is_successful(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.LOAD_DEREF, 0)], freevars=("h",)
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_LOAD_DEREF_with_valid_oparg_index_in_closure_is_successful(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.LOAD_DEREF, 0)], cellvars=("h",)
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_LOAD_DEREF_oparg_type_can_be_any_str(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.LOAD_DEREF, 0)], freevars=(str(),), stacksize=1
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_LOAD_DEREF_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper("", bytecodes=[(op.opcode.LOAD_DEREF, 0)])
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 0 out of bounds for size 0 for operation LOAD_DEREF @ offset 0",
            Verifier.validate_code,
            code,
        )

    def test_STORE_DEREF_with_valid_oparg_index_in_freevars_is_successful(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_DEREF, 0), (op.opcode.STORE_DEREF, 1)],
            freevars=("h", "h"),
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_STORE_DEREF_with_valid_oparg_index_in_closure_is_successful(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_DEREF, 0), (op.opcode.STORE_DEREF, 1)],
            cellvars=("h", "h"),
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_STORE_DEREF_oparg_type_can_be_any_str(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_DEREF, 0), (op.opcode.STORE_DEREF, 0)],
            freevars=(str(), str()),
            stacksize=1,
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_STORE_DEREF_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper("", bytecodes=[(op.opcode.STORE_DEREF, 0)])
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 0 out of bounds for size 0 for operation STORE_DEREF @ offset 0",
            Verifier.validate_code,
            code,
        )

    def test_DELETE_DEREF_with_valid_oparg_index_in_freevars_is_successful(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.DELETE_DEREF, 0)], freevars=("h",)
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_DELETE_DEREF_with_valid_oparg_index_in_closure_is_successful(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.DELETE_DEREF, 0)], cellvars=("h",)
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_DELETE_DEREF_oparg_type_can_be_any_str(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.DELETE_DEREF, 0)], freevars=(str(),), stacksize=1
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_DELETE_DEREF_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper("", bytecodes=[(op.opcode.DELETE_DEREF, 0)])
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 0 out of bounds for size 0 for operation DELETE_DEREF @ offset 0",
            Verifier.validate_code,
            code,
        )

    def test_LOAD_CLASSDEREF_with_valid_oparg_index_in_freevars_is_successful(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.LOAD_CLASSDEREF, 0)], freevars=("h",)
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_LOAD_CLASSDEREF_with_valid_oparg_index_in_closure_is_successful(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.LOAD_CLASSDEREF, 0)], cellvars=("h",)
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_LOAD_CLASSDEREF_oparg_type_can_be_any_str(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_CLASSDEREF, 0)],
            freevars=(str(),),
            stacksize=1,
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_LOAD_CLASSDEREF_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper("", bytecodes=[(op.opcode.LOAD_CLASSDEREF, 0)])
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 0 out of bounds for size 0 for operation LOAD_CLASSDEREF @ offset 0",
            Verifier.validate_code,
            code,
        )

    def test_COMPARE_OP_with_valid_oparg_index_is_successful(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.LOAD_CONST, 0), (op.opcode.COMPARE_OP, 0)]
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_COMPARE_OP_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_CONST, 0), (op.opcode.COMPARE_OP, 15)],
            stacksize=2,
        )
        self.assertRaisesRegex(
            VerificationError,
            # TODO(emacs): This should be 6, not 7. Why is it 7?
            "Argument index 15 out of bounds for size 7 for operation COMPARE_OP @ offset 2",
            Verifier.validate_code,
            code,
        )

    def test_LOAD_CLOSURE_with_valid_oparg_index_is_successful(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.LOAD_CLOSURE, 0)], cellvars=("h",)
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_LOAD_CLOSURE_oparg_can_be_any_str(self):
        code = self.compile_helper(
            "", bytecodes=[(op.opcode.LOAD_CLOSURE, 0)], cellvars=(str(),)
        )
        self.assertTrue(Verifier.validate_code(code))

    def test_LOAD_CLOSURE_with_invalid_oparg_index_raises_exception(self):
        code = self.compile_helper(
            "",
            bytecodes=[(op.opcode.LOAD_CLOSURE, 0), (op.opcode.LOAD_CLOSURE, 15)],
            cellvars=("h",),
            stacksize=2,
        )
        self.assertRaisesRegex(
            VerificationError,
            "Argument index 15 out of bounds for size 1 for operation LOAD_CLOSURE @ offset 2",
            Verifier.validate_code,
            code,
        )


if __name__ == "__main__":
    unittest.main()
