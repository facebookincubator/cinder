from __future__ import annotations

import ast
from ast import AST
from types import CodeType
from typing import cast, List, Optional, Tuple, Type, Union

from ..opcodes import opcode
from ..pyassem import Block, PyFlowGraph, PyFlowGraphCinder
from ..pycodegen import (
    CinderCodeGenerator,
    CodeGenerator,
    CompNode,
    FOR_LOOP,
    FuncOrLambda,
)
from ..symbols import CinderSymbolVisitor, ClassScope, ModuleScope, SymbolVisitor
from .type_binder import ReadonlyTypeBinder
from .types import FunctionValue, READONLY
from .util import is_readonly_compile_forced


class ReadonlyCodeGenerator(CinderCodeGenerator):
    flow_graph = PyFlowGraphCinder
    _SymbolVisitor = CinderSymbolVisitor

    def _get_containing_function(
        self,
    ) -> Optional[Union[ast.FunctionDef, ast.AsyncFunctionDef, ast.Lambda]]:
        cur_gen = self
        while cur_gen and not isinstance(
            cur_gen.tree, (ast.FunctionDef, ast.AsyncFunctionDef, ast.Lambda)
        ):
            cur_gen = cur_gen.parent_code_gen

        if not cur_gen:
            return None

        assert isinstance(
            cur_gen.tree, (ast.FunctionDef, ast.AsyncFunctionDef, ast.Lambda)
        )
        return cur_gen.tree

    def __init__(
        self,
        parent: Optional[CodeGenerator],
        node: AST,
        symbols: SymbolVisitor,
        graph: PyFlowGraph,
        binder: ReadonlyTypeBinder,
        flags: int = 0,
        optimization_lvl: int = 0,
    ) -> None:
        super().__init__(
            parent, node, symbols, graph, flags=flags, optimization_lvl=optimization_lvl
        )
        self.binder = binder
        self.emit_readonly_checks: bool = False
        self.current_function_is_readonly_nonlocal: bool = False

        cur_func = self._get_containing_function()
        if cur_func:
            cur_func_val = self.binder.read_only_funcs.get(cur_func)
            self.emit_readonly_checks = cur_func_val != None
            if cur_func_val:
                self.current_function_is_readonly_nonlocal = (
                    cur_func_val.readonly_nonlocal
                )

    @classmethod
    def make_code_gen(
        cls,
        module_name: str,
        tree: AST,
        filename: str,
        flags: int,
        optimize: int,
        ast_optimizer_enabled: bool = True,
    ) -> ReadonlyCodeGenerator:
        s = cls._SymbolVisitor()
        s.visit(tree)
        binder = ReadonlyTypeBinder(tree, filename, s)
        graph = cls.flow_graph(
            module_name,
            filename,
            s.scopes[tree],
        )
        codegen = cls(
            None,
            tree,
            s,
            graph,
            binder,
            flags=flags,
            optimization_lvl=optimize,
        )
        codegen.visit(tree)
        return codegen

    def make_child_codegen(
        self,
        tree: FuncOrLambda | CompNode | ast.ClassDef,
        graph: PyFlowGraph,
        codegen_type: Optional[Type[CodeGenerator]] = None,
    ) -> CodeGenerator:
        if codegen_type is None:
            codegen_type = type(self)
        assert issubclass(codegen_type, ReadonlyCodeGenerator)
        codegen_type = cast(Type[ReadonlyCodeGenerator], codegen_type)
        return codegen_type(
            self,
            tree,
            self.symbols,
            graph,
            binder=self.binder,
            flags=self.flags,
            optimization_lvl=self.optimization_lvl,
        )

    def emit_readonly_op(
        self, opname: str, args: List[int | str], target: Optional[Block] = None
    ) -> None:
        op = opcode.readonlyop[opname]
        if target is None:
            self.emit("READONLY_OPERATION", (op, *args))
        else:
            self.emitWithBlock("READONLY_OPERATION", (op, *args), target=target)

    def visitCall(self, node: ast.Call) -> None:
        if (
            isinstance(node.func, ast.Name)
            and node.func.id == "readonly"
            and len(node.args) == 1
            and len(node.keywords) == 0
        ):
            # Don't emit an actual call to readonly(), instead just emit its contents.
            self.visit(node.args[0])
            return

        super().visitCall(node)

    def build_operation_mask(self, node: Optional[ast.AST], args: List[ast.AST]) -> int:
        mask = 0x80
        if node is not None and self.binder.is_readonly(node):
            mask |= 0x40

        if len(args) > 6:
            raise SyntaxError("Too many arguments provided to an operator.")
        curArgMask = 0x01
        for arg in args:
            if self.binder.is_readonly(arg):
                mask |= curArgMask
            curArgMask <<= 1
        return mask

    def visitUnaryOp(self, node: ast.UnaryOp) -> None:
        if not self.emit_readonly_checks:
            super().visitUnaryOp(node)
            return

        self.set_lineno(node)
        self.visit(node.operand)
        readonlyMask = self.build_operation_mask(node, [node.operand])
        self.emit_readonly_op(self._unary_opcode[type(node.op)], [readonlyMask])

    def visitBinOp(self, node: ast.BinOp) -> None:
        if not self.emit_readonly_checks:
            super().visitBinOp(node)
            return

        self.set_lineno(node)
        self.visit(node.left)
        self.visit(node.right)
        readonlyMask = self.build_operation_mask(node, [node.left, node.right])
        self.emit_readonly_op(self._binary_opcode[type(node.op)], [readonlyMask])

    def emitReadonlyCompareOp(self, readonlyMask: int, op: ast.AST) -> None:
        # 'is' and 'is not' are not user overloadable, so are always safe regardless
        # of any readonlyness.
        if isinstance(op, ast.Is) or isinstance(op, ast.IsNot):
            self.defaultEmitCompare(op)
        else:
            cmpOp = opcode.CMP_OP.index(self._cmp_opcode[type(op)])
            self.emit_readonly_op("COMPARE_OP", [readonlyMask, cmpOp])

    def emitReadonlyChainedCompareStep(
        self,
        readonlyMask: int,
        op: ast.AST,
        value: ast.AST,
        cleanup: Block,
        always_pop: bool = False,
    ) -> None:
        self.visit(value)
        self.emit("DUP_TOP")
        self.emit("ROT_THREE")
        self.emitReadonlyCompareOp(readonlyMask, op)
        self.emit(
            "POP_JUMP_IF_FALSE" if always_pop else "JUMP_IF_FALSE_OR_POP", cleanup
        )
        self.nextBlock(label="compare_or_cleanup")

    def visitCompare(self, node: ast.Compare) -> None:
        if not self.emit_readonly_checks:
            super().visitCompare(node)
            return

        self.set_lineno(node)
        self.visit(node.left)
        prev_value = node.left
        cleanup = self.newBlock("cleanup")
        for op, code in zip(node.ops[:-1], node.comparators[:-1]):
            readonlyMask = self.build_operation_mask(node, [prev_value, code])
            self.emitReadonlyChainedCompareStep(readonlyMask, op, code, cleanup)
            prev_value = code
        # now do the last comparison
        if node.ops:
            op = node.ops[-1]
            code = node.comparators[-1]
            self.visit(code)
            readonlyMask = self.build_operation_mask(node, [prev_value, code])
            self.emitReadonlyCompareOp(readonlyMask, op)
        if len(node.ops) > 1:
            end = self.newBlock("end")
            self.emit("JUMP_FORWARD", end)
            self.nextBlock(cleanup)
            self.emit("ROT_TWO")
            self.emit("POP_TOP")
            self.nextBlock(end)

    def _maybeEmitReadonlyJumpIf(
        self, test: AST, next: Block, is_if_true: bool
    ) -> None:
        binder = self.binder
        ro = binder.is_readonly(test)

        if ro:
            self.emit_readonly_op(
                "POP_JUMP_IF_TRUE" if is_if_true else "POP_JUMP_IF_FALSE",
                [],
                target=next,
            )
        else:
            self.emit("POP_JUMP_IF_TRUE" if is_if_true else "POP_JUMP_IF_FALSE", next)

    def compileJumpIf(self, test: ast.expr, next: Block, is_if_true: bool) -> None:
        if not self.emit_readonly_checks:
            super().compileJumpIf(test, next, is_if_true)
            return

        if isinstance(test, ast.Compare) and len(test.ops) > 1:
            cleanup = self.newBlock()
            self.visit(test.left)
            prev_value = test.left
            for op, comparator in zip(test.ops[:-1], test.comparators[:-1]):
                readonlyMask = self.build_operation_mask(test, [prev_value, comparator])
                self.emitReadonlyChainedCompareStep(
                    readonlyMask, op, comparator, cleanup, always_pop=True
                )
                prev_value = comparator
            self.visit(test.comparators[-1])
            readonlyMask = self.build_operation_mask(
                test, [prev_value, test.comparators[-1]]
            )
            self.emitReadonlyCompareOp(readonlyMask, test.ops[-1])
            self.emit("POP_JUMP_IF_TRUE" if is_if_true else "POP_JUMP_IF_FALSE", next)
            end = self.newBlock()
            self.emit("JUMP_FORWARD", end)
            self.nextBlock(cleanup)
            self.emit("POP_TOP")
            if not is_if_true:
                self.emit("JUMP_FORWARD", next)
            self.nextBlock(end)
            return
        elif (
            isinstance(test, ast.UnaryOp)
            or isinstance(test, ast.BoolOp)
            or isinstance(test, ast.IfExp)
            or isinstance(test, ast.Compare)
        ):
            super().compileJumpIf(test, next, is_if_true)
            return

        self.visit(test)
        self._maybeEmitReadonlyJumpIf(test, next, is_if_true)

    def visitFor(self, node: ast.For) -> None:
        if not self.emit_readonly_checks:
            super().visitFor(node)
            return
        start = self.newBlock()
        body_start = self.newBlock()
        anchor = self.newBlock()
        after = self.newBlock()

        self.set_lineno(node)
        self.push_loop(FOR_LOOP, start, after)
        self.visit(node.iter)
        # for e1 in e2
        # emit READONLY_GET_ITER with mask: arg0 follows e2 readonlyness
        # there is no way to specify e2.__iter__() returns readonly in a for loop
        # , so we require it to be mutable
        mask = self.build_operation_mask(None, [node.iter])
        self.emit_readonly_op("GET_ITER", [mask])

        # for e1 in e2
        # emit READONLY_FOR_ITER with mask: e2.__iter__().__next__
        # arg0 is always mutable, since we assumed e2.__iter__() to be mutable
        # expected return type follows e1 readonlyness
        mask = self.build_operation_mask(node.target, [])
        self.emit_readonly_op("FOR_ITER", [mask], target=anchor)
        self.emit("JUMP_ABSOLUTE", body_start)

        self.nextBlock(start)
        self.emit("FOR_ITER", anchor)
        self.nextBlock(body_start)
        self.visit(node.target)
        self.visit(node.body)
        self.emit("JUMP_ABSOLUTE", start)
        self.nextBlock(anchor)
        self.pop_loop()

        if node.orelse:
            self.visit(node.orelse)
        self.nextBlock(after)

    def visitName(self, node: ast.Name) -> None:
        if node.id != "__function_credential__":
            super().visitName(node)
            return

        module_name = ""
        class_name = ""
        func_name = ""
        scope = self.scope
        names = []
        collecting_function_name = True

        while scope and not isinstance(scope, ModuleScope):
            if isinstance(scope, ClassScope) and collecting_function_name:
                func_name = ".".join(reversed(names))
                collecting_function_name = False
                names = [scope.name]
            else:
                names.append(scope.name)
            scope = scope.parent

        if collecting_function_name:
            func_name = ".".join(reversed(names))
        else:
            class_name = ".".join(reversed(names))

        if scope:
            assert isinstance(scope, ModuleScope)
            module_name = scope.name

        name_tuple = (module_name, class_name, func_name)
        self.emit("FUNC_CREDENTIAL", name_tuple)

    def calc_function_readonly_mask(
        self,
        node: AST,
        is_readonly_func: bool,
        returns_readonly: bool,
        readonly_nonlocal: bool,
        yields_readonly: Optional[bool],
        sends_readonly: Optional[bool],
        args: Tuple[bool, ...],
    ) -> int:
        # must be readonly function - set the msb to 1
        mask = 0
        if is_readonly_func:
            mask = mask | 0x8000_0000_0000_0000
        if readonly_nonlocal:
            mask = mask | 0x4000_0000_0000_0000
        if not returns_readonly:
            mask = mask | 0x2000_0000_0000_0000
        if not yields_readonly:
            mask = mask | 0x1000_0000_0000_0000
        if sends_readonly:
            mask = mask | 0x0800_0000_0000_0000

        # Technically this can go higher, but at 50 separate readonly arguments
        # the python code being compiled should be refactored instead.
        if len(args) >= 50:
            if is_readonly_compile_forced():
                return mask
            raise SyntaxError(
                "Cannot define more than 50 arguments on a readonly function.",
                self.syntax_error_position(node),
            )

        bit = 1
        for readonly_arg in args:
            if readonly_arg:
                mask = mask | bit

            bit = bit << 1

        return mask

    def build_function(
        self,
        node: ast.FunctionDef | ast.AsyncFunctionDef | ast.Lambda,
        gen: CodeGenerator,
    ) -> None:
        super().build_function(node, gen)
        readonly_funcs = self.binder.read_only_funcs
        if node not in readonly_funcs:
            return

        func_value = readonly_funcs[node]
        assert isinstance(func_value, FunctionValue)

        mask = self.calc_function_readonly_mask(
            node,
            is_readonly_func=True,
            returns_readonly=func_value.returns_readonly,
            readonly_nonlocal=func_value.readonly_nonlocal,
            yields_readonly=func_value.yields_readonly,
            sends_readonly=func_value.sends_readonly,
            args=tuple(x == READONLY for x in func_value.args),
        )
        self.emit_readonly_op("MAKE_FUNCTION", [mask])

    def insertReadonlyCheck(
        self, node: Optional[ast.Call], nargs: int, call_method: bool
    ) -> None:
        # node is None when the this function is (indirectly) called by any functions
        # other than visitCall().
        # Also skip the check in a non-readonly function
        if not node or not self.emit_readonly_checks:
            return

        arg_readonly = list(self.binder.is_readonly(x) for x in node.args)
        method_flag = 0

        if call_method:
            assert isinstance(node.func, ast.Attribute)
            self_readonly = self.binder.is_readonly(node.func.value)
            arg_readonly.insert(0, self_readonly)
            method_flag = 1

        mask = self.calc_function_readonly_mask(
            node,
            is_readonly_func=True,
            returns_readonly=self.binder.is_readonly(node),
            readonly_nonlocal=self.current_function_is_readonly_nonlocal,
            yields_readonly=None,
            sends_readonly=None,
            args=tuple(arg_readonly),
        )

        self.emit_readonly_op("CHECK_FUNCTION", [nargs, mask, method_flag])

    def visitAttribute(self, node: ast.Attribute) -> None:
        self.set_lineno(node)
        self.visit(node.value)
        if isinstance(node.ctx, ast.Store):
            self.emit("STORE_ATTR", self.mangle(node.attr))
            return
        elif isinstance(node.ctx, ast.Del):
            self.emit("DELETE_ATTR", self.mangle(node.attr))
            return

        # check if readonly
        binder = self.binder
        check_attr_return = not binder.is_readonly(node)
        check_attr_read = binder.is_readonly(node.value)

        if check_attr_read or check_attr_return:
            self.emit_readonly_op(
                "CHECK_LOAD_ATTR", [check_attr_return, check_attr_read]
            )

        self.emit("LOAD_ATTR", self.mangle(node.attr))


def readonly_compile(
    name: str, filename: str, tree: AST, flags: int, optimize: int
) -> CodeType:
    """
    Entry point used in non-static setting
    """
    codegen = ReadonlyCodeGenerator.make_code_gen(name, tree, filename, flags, optimize)
    return codegen.getCode()
