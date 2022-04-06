from __future__ import annotations

import ast
from ast import AST
from types import CodeType
from typing import Optional, Tuple, Type, cast, Union

from ..opcodes import opcode
from ..pyassem import PyFlowGraph, PyFlowGraphCinder
from ..pycodegen import (
    CodeGenerator,
    CinderCodeGenerator,
    FuncOrLambda,
    CompNode,
)
from ..symbols import (
    ClassScope,
    FunctionScope,
    ModuleScope,
    Scope,
    CinderSymbolVisitor,
    SymbolVisitor,
)
from .type_binder import ReadonlyTypeBinder, TReadonlyTypes
from .types import READONLY, FunctionValue


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
        peephole_enabled: bool = True,
        ast_optimizer_enabled: bool = True,
    ) -> ReadonlyCodeGenerator:
        s = cls._SymbolVisitor()
        s.visit(tree)
        binder = ReadonlyTypeBinder(tree, filename, s)
        graph = cls.flow_graph(
            module_name,
            filename,
            s.scopes[tree],
            peephole_enabled=peephole_enabled,
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

    def visitUnaryOp(self, node: ast.UnaryOp) -> None:
        if not self.emit_readonly_checks:
            super().visitUnaryOp(node)
            return

        self.update_lineno(node)
        self.visit(node.operand)
        op = self._unary_opcode[type(node.op)]

        readonlyMask = 0x80
        if self.binder.is_readonly(node):
            readonlyMask |= 0x40
        if self.binder.is_readonly(node.operand):
            readonlyMask |= 0x01

        self.emit_readonly_op(op, readonlyMask)

    def visitBinOp(self, node: ast.BinOp) -> None:
        if not self.emit_readonly_checks:
            super().visitBinOp(node)
            return

        self.update_lineno(node)
        self.visit(node.left)
        self.visit(node.right)
        op = self._binary_opcode[type(node.op)]

        readonlyMask = 0x80
        if self.binder.is_readonly(node):
            readonlyMask |= 0x40
        if self.binder.is_readonly(node.left):
            readonlyMask |= 0x01
        if self.binder.is_readonly(node.right):
            readonlyMask |= 0x02

        self.emit_readonly_op(op, readonlyMask)

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

    def emit_readonly_op(self, opname: str, arg: object) -> None:
        op = opcode.readonlyop[opname]
        self.emit("READONLY_OPERATION", (op, arg))

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
        self.emit_readonly_op("MAKE_FUNCTION", mask)

    def insertReadonlyCheck(self, node: Optional[ast.Call], nargs: int) -> None:
        # node is None when the this function is (indirectly) called by any functions
        # other than visitCall().
        # Also skip the check in a non-readonly function
        if not node or not self.emit_readonly_checks:
            return

        mask = self.calc_function_readonly_mask(
            node,
            is_readonly_func=True,
            returns_readonly=self.binder.is_readonly(node),
            readonly_nonlocal=self.current_function_is_readonly_nonlocal,
            yields_readonly=None,
            sends_readonly=None,
            args=tuple(self.binder.is_readonly(x) for x in node.args),
        )

        self.emit_readonly_op("CHECK_FUNCTION", (nargs, mask))

def readonly_compile(
    name: str, filename: str, tree: AST, flags: int, optimize: int
) -> CodeType:
    """
    Entry point used in non-static setting
    """
    codegen = ReadonlyCodeGenerator.make_code_gen(name, tree, filename, flags, optimize)
    return codegen.getCode()
