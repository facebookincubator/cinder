import os
import sys
import unittest
import types
from compiler import compile
from compiler import opcodes as op
from inspect import cleandoc
from typing import List, Tuple
import dis
sys.path.append(os.path.join(sys.path[0],'..','fuzzer'))

import cfgutil
import verifier
from verifier import Verifier, VerificationError
from cfgutil import BytecodeOp

class VerifierTests(unittest.TestCase):
    def convert_bytecodes_to_byte_representation(self, bytecodes: List[Tuple]) -> bytes:
        b_list = []
        if len(bytecodes) == 0:
            return b''
        for i in bytecodes:
            if isinstance(i, tuple) and len(i) == 2:
                b_list.append(i[0])
                b_list.append(i[1])
            else:
                b_list.append(i)
        return bytes(b_list)

    def compile_helper(self, source: str, bytecodes: List[Tuple] = None, stacksize: int = None, consts: tuple = None) -> types.CodeType:
        code = compile(source, '', 'exec')
        if bytecodes is not None:
            code = code.replace(co_code=self.convert_bytecodes_to_byte_representation(bytecodes))
        if stacksize is not None:
            code = code.replace(co_stacksize=stacksize)
        if consts is not None:
            code = code.replace(co_consts=consts)
        return code

class VerifierBasicsTest(VerifierTests):
    def test_first_index_cannot_be_nonzero(self):
        code = self.compile_helper("", [(op.opcode.LOAD_CONST, 57), (op.opcode.POP_TOP, 145)])
        self.assertRaisesRegex(VerificationError, "Bytecode value at first index must be zero", Verifier.validate_code, code)

    def test_length_cannot_be_odd(self):
        code = self.compile_helper("", [(op.opcode.LOAD_CONST, 57), (op.opcode.POP_TOP)])
        self.assertRaisesRegex(VerificationError, "Bytecode length cannot be odd", Verifier.validate_code, code)

    def test_length_cannot_be_zero(self):
        code = self.compile_helper("", [])
        self.assertRaisesRegex(VerificationError, "Bytecode length cannot be zero or negative", Verifier.validate_code, code)

    def test_op_name_must_exist(self):
        code = self.compile_helper("", [(op.opcode.LOAD_CONST, 0), (32, 68)]) # 32 is not a valid opcode id
        self.assertRaisesRegex(VerificationError, "Operation does not exist", Verifier.validate_code, code)

    def test_cannot_jump_to_odd_index(self):
        code = self.compile_helper("", [(op.opcode.LOAD_CONST, 0), (op.opcode.JUMP_ABSOLUTE, 1)])
        self.assertRaisesRegex(VerificationError, "Can not jump out of bounds or to odd index", Verifier.validate_code, code)

    def test_cannot_jump_outside_of_file(self):
        code = self.compile_helper("", [(op.opcode.LOAD_CONST, 0), (op.opcode.JUMP_ABSOLUTE, 153)])
        self.assertRaisesRegex(VerificationError, "Can not jump out of bounds or to odd index", Verifier.validate_code, code)

    def test_error_in_nested_code_object(self):
        # nested code object has an invalid jump which should be caught
        code = self.compile_helper("", bytecodes=[(op.opcode.LOAD_CONST, 0), (op.opcode.POP_TOP, 0)], \
         consts=((self.compile_helper("", [(op.opcode.LOAD_CONST, 0), (op.opcode.JUMP_ABSOLUTE, 153)]),)))
        self.assertRaisesRegex(VerificationError, "Can not jump out of bounds or to odd index", Verifier.validate_code, code)

class VerifierCFGTests(VerifierTests):
    def test_cfg_with_single_block(self):
        source = cleandoc("""
            x = 3
            x += 1""")
        code = self.compile_helper(source)
        block_map = Verifier.create_blocks(Verifier.parse_bytecode(code.co_code))
        assert str(block_map) == cleandoc("""
        bb0:
          LOAD_CONST : 0
          STORE_NAME : 0
          LOAD_NAME : 0
          LOAD_CONST : 1
          INPLACE_ADD : 0
          STORE_NAME : 0
          LOAD_CONST : 2
          RETURN_VALUE : 0""")

    def test_cfg_with_conditional(self):
        source = cleandoc("""
            x, y = 3, 0
            if x > 0:
                y += 1
            elif x == 0:
                y += 3
            else:
                y += 2""")
        code = self.compile_helper(source)
        block_map = Verifier.create_blocks(Verifier.parse_bytecode(code.co_code))
        self.assertEqual(str(block_map), cleandoc("""
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
          JUMP_FORWARD bb5
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
          JUMP_FORWARD bb5
        bb4:
          LOAD_NAME : 1
          LOAD_CONST : 4
          INPLACE_ADD : 0
          STORE_NAME : 1
        bb5:
          LOAD_CONST : 5
          RETURN_VALUE : 0"""))

    def test_cfg_with_loop(self):
        source = cleandoc("""
            arr = []
            i = 0
            while i < 10:
              arr.append(i)
              i+=1""")
        code = self.compile_helper(source)
        block_map = Verifier.create_blocks(Verifier.parse_bytecode(code.co_code))
        self.assertEqual(str(block_map), cleandoc("""
        bb0:
          BUILD_LIST : 0
          STORE_NAME : 0
          LOAD_CONST : 0
          STORE_NAME : 1
        bb1:
          LOAD_NAME : 1
          LOAD_CONST : 1
          COMPARE_OP : 0
          POP_JUMP_IF_FALSE bb3
        bb2:
          LOAD_NAME : 0
          LOAD_METHOD : 2
          LOAD_NAME : 1
          CALL_METHOD : 1
          POP_TOP : 0
          LOAD_NAME : 1
          LOAD_CONST : 2
          INPLACE_ADD : 0
          STORE_NAME : 1
          JUMP_ABSOLUTE bb1
        bb3:
          LOAD_CONST : 3
          RETURN_VALUE : 0"""))

    def test_cfg_with_function_call(self):
        source = cleandoc("""
            arr = []
            for i in range(10):
              arr.append(i)""")
        code = self.compile_helper(source)
        block_map = Verifier.create_blocks(Verifier.parse_bytecode(code.co_code))
        self.assertEqual(str(block_map), cleandoc("""
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
          RETURN_VALUE : 0"""))

    def test_cfg_try_except(self):
        source = cleandoc("""
        y = 9
        a = 3
        try:
            c = y+a
        except:
            raise
        """)
        code = self.compile_helper(source)
        block_map = Verifier.create_blocks(Verifier.parse_bytecode(code.co_code))
        self.assertEqual(str(block_map), cleandoc("""
        bb0:
          LOAD_CONST : 0
          STORE_NAME : 0
          LOAD_CONST : 1
          STORE_NAME : 1
          SETUP_FINALLY : 12
          LOAD_NAME : 0
          LOAD_NAME : 1
          BINARY_ADD : 0
          STORE_NAME : 2
          POP_BLOCK : 0
          JUMP_FORWARD bb4
        bb1:
          POP_TOP : 0
          POP_TOP : 0
          POP_TOP : 0
          RAISE_VARARGS : 0
        bb2:
          POP_EXCEPT : 0
          JUMP_FORWARD bb4
        bb3:
          END_FINALLY : 0
        bb4:
          LOAD_CONST : 2
          RETURN_VALUE : 0"""))

    def test_cfg_try_except_else_finally(self):
        source = cleandoc("""
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
        """)
        code = self.compile_helper(source)
        block_map = Verifier.create_blocks(Verifier.parse_bytecode(code.co_code))
        self.assertEqual(str(block_map), cleandoc("""
        bb0:
          LOAD_CONST : 0
          STORE_NAME : 0
          LOAD_CONST : 1
          STORE_NAME : 1
          SETUP_FINALLY : 40
          SETUP_FINALLY : 12
          LOAD_NAME : 0
          LOAD_NAME : 1
          BINARY_ADD : 0
          STORE_NAME : 2
          POP_BLOCK : 0
          JUMP_FORWARD bb4
        bb1:
          POP_TOP : 0
          POP_TOP : 0
          POP_TOP : 0
          RAISE_VARARGS : 0
        bb2:
          POP_EXCEPT : 0
          JUMP_FORWARD bb5
        bb3:
          END_FINALLY : 0
        bb4:
          LOAD_NAME : 0
          LOAD_CONST : 3
          INPLACE_ADD : 0
          STORE_NAME : 0
        bb5:
          POP_BLOCK : 0
          BEGIN_FINALLY : 0
          LOAD_NAME : 0
          LOAD_CONST : 2
          INPLACE_ADD : 0
          STORE_NAME : 0
          END_FINALLY : 0
          LOAD_CONST : 4
          RETURN_VALUE : 0"""))

    def test_cfg_continue_statement_in_try(self):
        source = cleandoc("""
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
        """)
        code = self.compile_helper(source)
        block_map = Verifier.create_blocks(Verifier.parse_bytecode(code.co_code))
        self.assertEqual(str(block_map), cleandoc("""
        bb0:
          LOAD_NAME : 0
          LOAD_CONST : 0
          CALL_FUNCTION : 1
          GET_ITER : 0
        bb1:
          FOR_ITER bb8
        bb2:
          STORE_NAME : 1
          LOAD_CONST : 1
          STORE_NAME : 2
          LOAD_CONST : 2
          STORE_NAME : 3
          SETUP_FINALLY : 40
          SETUP_FINALLY : 20
          LOAD_NAME : 2
          LOAD_NAME : 3
          BINARY_ADD : 0
          STORE_NAME : 4
          POP_BLOCK : 0
          POP_BLOCK : 0
          CALL_FINALLY : 24
          JUMP_ABSOLUTE bb1
        bb3:
          POP_BLOCK : 0
          JUMP_FORWARD bb7
        bb4:
          POP_TOP : 0
          POP_TOP : 0
          POP_TOP : 0
          RAISE_VARARGS : 0
        bb5:
          POP_EXCEPT : 0
          JUMP_FORWARD bb7
        bb6:
          END_FINALLY : 0
        bb7:
          POP_BLOCK : 0
          BEGIN_FINALLY : 0
          LOAD_NAME : 2
          LOAD_CONST : 3
          INPLACE_ADD : 0
          STORE_NAME : 2
          END_FINALLY : 0
          JUMP_ABSOLUTE bb1
        bb8:
          LOAD_CONST : 4
          RETURN_VALUE : 0"""))

class VerifierStackDepthTests(VerifierTests):
    def test_cannot_pop_from_empty_stack(self):
        code = self.compile_helper("", [(op.opcode.LOAD_CONST, 0), (op.opcode.POP_TOP, 0), (op.opcode.POP_TOP, 0)])
        self.assertRaisesRegex(VerificationError, "Stack depth either dips below zero or goes above max size", Verifier.validate_code, code)

    def test_stack_depth_cannot_exceed_max(self):
        code = self.compile_helper("", [(op.opcode.LOAD_CONST, 0), (op.opcode.LOAD_CONST, 0), (op.opcode.LOAD_CONST, 0)], 1)
        self.assertRaisesRegex(VerificationError, "Stack depth either dips below zero or goes above max size", Verifier.validate_code, code)

    def test_branch(self):
        source = cleandoc("""
        x, y = 0, 0
        if x: y += 1
        else: y += 3""")
        code = self.compile_helper(source)
        self.assertTrue(Verifier.validate_code(code))

    def test_for_loop(self):
        source = cleandoc("""
        x = 0
        for i in range(10):
          x += 1""")
        code = self.compile_helper(source)
        self.assertTrue(Verifier.validate_code(code))

    def test_while_loop(self):
        source = cleandoc("""
        i = 0
        while i < 10:
          i+=1""")
        code = self.compile_helper(source)
        self.assertTrue(Verifier.validate_code(code))

    def test_cannot_pop_from_empty_stack_during_loop(self):
        bytecodes = [(op.opcode.LOAD_CONST, 0), (op.opcode.UNPACK_SEQUENCE, 2), (op.opcode.STORE_NAME, 0), \
        (op.opcode.STORE_NAME, 1), (op.opcode.LOAD_NAME, 0), (op.opcode.POP_JUMP_IF_FALSE, 22), \
        (op.opcode.LOAD_NAME, 1), (op.opcode.LOAD_CONST, 1), (op.opcode.POP_TOP, 0), (op.opcode.INPLACE_ADD, 0), \
        (op.opcode.STORE_NAME, 1), (op.opcode.JUMP_FORWARD, 8), (op.opcode.LOAD_NAME, 1), \
        (op.opcode.LOAD_CONST, 2), (op.opcode.INPLACE_ADD, 0), (op.opcode.STORE_NAME, 1), \
        (op.opcode.LOAD_CONST, 3), (op.opcode.RETURN_VALUE, 0)]
        code = self.compile_helper("", bytecodes)
        self.assertRaisesRegex(VerificationError, "Stack depth either dips below zero or goes above max size", Verifier.validate_code, code)

    def test_stack_depth_should_not_exceed_max_while_looping(self):
        bytecodes = [(op.opcode.LOAD_CONST, 0), (op.opcode.UNPACK_SEQUENCE, 2), (op.opcode.STORE_NAME, 0), \
        (op.opcode.STORE_NAME, 1), (op.opcode.LOAD_NAME, 0), (op.opcode.POP_JUMP_IF_FALSE, 22), \
        (op.opcode.LOAD_NAME, 1), (op.opcode.LOAD_CONST, 1), (op.opcode.LOAD_CONST, 1), (op.opcode.LOAD_CONST, 1), \
        (op.opcode.LOAD_CONST, 1), (op.opcode.LOAD_CONST, 1), (op.opcode.POP_TOP, 0), \
        (op.opcode.INPLACE_ADD, 0), (op.opcode.STORE_NAME, 1), (op.opcode.JUMP_FORWARD, 8), \
        (op.opcode.LOAD_NAME, 1), (op.opcode.LOAD_CONST, 2), (op.opcode.INPLACE_ADD, 0), \
        (op.opcode.STORE_NAME, 1), (op.opcode.LOAD_CONST, 3), (op.opcode.RETURN_VALUE, 0)]
        code = self.compile_helper("", bytecodes)
        self.assertRaisesRegex(VerificationError, "Stack depth either dips below zero or goes above max size", Verifier.validate_code, code)

    def test_branch_with_nested_conditions(self):
        source = cleandoc("""
        x, y, arr = 0, 0, []
        if x:
          y = 5 if x > 3 else 3
        else:
          arr.append(x)
        arr.append(y)""")
        code = self.compile_helper(source)
        self.assertTrue(Verifier.validate_code(code))

    def test_nested_loop(self):
        source = cleandoc("""
        x, arr = 0, []
        for i in range(10):
          for j in range(12):
            x+=3
            arr.append(x)""")
        code = self.compile_helper(source)
        self.assertTrue(Verifier.validate_code(code))

    def test_while_loop_with_multiple_conditions(self):
        source = cleandoc("""
        i, stack = 0, [1, 2, 3, 4]
        while stack and i < 10:
          stack.pop()
          i+=3""")
        code = self.compile_helper(source)
        self.assertTrue(Verifier.validate_code(code))

    def test_recursive_function_maintains_stack_depth(self):
        source = cleandoc("""
        y = 7
        def f(x):
          if x == 0:
            return 0
          if x == 1:
            return 1
          return f(x-1) + f(x-2)
        print(f(y))""")
        code = self.compile_helper(source)
        self.assertTrue(Verifier.validate_code(code))

    def test_try_except_maintains_stack_depth(self):
        source = cleandoc("""
        y = 9
        a = 3
        try:
            c = y+a
        except:
            raise
        """)
        code = self.compile_helper(source)
        self.assertTrue(Verifier.validate_code(code))

    def test_try_except_else_finally_maintains_stack_depth(self):
        source = cleandoc("""
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
        """)
        code = self.compile_helper(source)
        self.assertTrue(Verifier.validate_code(code))

    def test_try_except_not_handled_by_except(self):
        source = cleandoc("""
        y = 9
        a = "b"
        try:
            c = y+a
        except IndexError:
            raise
        finally:
            y+=3
        """)
        code = self.compile_helper(source)
        self.assertTrue(Verifier.validate_code(code))

    def test_try_except_with_continue_statement_in_try(self):
        source = cleandoc("""
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
        """)
        code = self.compile_helper(source)
        self.assertTrue(Verifier.validate_code(code))


    def test_try_except_with_break_statement_in_finally(self):
        source = cleandoc("""
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
        """)
        code = self.compile_helper(source)
        self.assertTrue(Verifier.validate_code(code))

    def test_try_except_with_return_statements(self):
        source = cleandoc("""
        def f(x, y):
            try:
                x += y
                return x
            except:
                raise
            finally:
                return y
        """)
        code = self.compile_helper(source)
        self.assertTrue(Verifier.validate_code(code))


if __name__ == '__main__':
    unittest.main()
