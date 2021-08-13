# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
from __future__ import annotations

import ast
import sys
from ast import (
    AST,
    ClassDef,
    FunctionDef,
    AsyncFunctionDef,
    NodeVisitor,
    ImportFrom,
    Module,
    Name,
)
from types import CodeType
from typing import Optional, List, Mapping

# pyre-fixme [21]
from _strictmodule import MUTABLE_DECORATOR

from .. import symbols
from ..optimizer import AstOptimizer
from ..pyassem import PyFlowGraph
from ..pyassem import PyFlowGraphCinder
from ..pycodegen import (
    CodeGenerator,
    CinderCodeGenerator,
    FOR_LOOP,
    END_FINALLY,
    TRY_FINALLY_BREAK,
    TRY_FINALLY,
    Entry,
)
from ..symbols import SymbolVisitor
from .common import FIXED_MODULES

# We need to gate the immutability codegen for
# migration reasons
enable_strict_features: bool = False


def is_mutable(node: AST) -> bool:
    return isinstance(node, Name) and node.id == MUTABLE_DECORATOR


class FindClassDef(NodeVisitor):
    def __init__(self) -> None:
        self.has_class = False

    def check(self, node: AST) -> None:
        self.generic_visit(node)

    def visit_ClassDef(self, node: ClassDef) -> None:
        for n in node.decorator_list:
            if is_mutable(n):
                # do not consider this class for freezing
                break
        else:
            self.has_class = True
        for stmt in node.body:
            # also consider inner classes
            self.visit(stmt)

    def visit_FunctionDef(self, node: FunctionDef) -> None:
        # do not go into func body
        pass

    def visit_AsyncFunctionDef(self, node: AsyncFunctionDef) -> None:
        # do not go into func body
        pass


class StrictCodeGenerator(CinderCodeGenerator):
    flow_graph = PyFlowGraphCinder
    class_list_name: str = "<classes>"

    def __init__(
        self,
        parent: Optional[CodeGenerator],
        node: AST,
        symbols: SymbolVisitor,
        graph: PyFlowGraph,
        flags: int = 0,
        optimization_lvl: int = 0,
    ) -> None:
        super().__init__(parent, node, symbols, graph, flags, optimization_lvl)
        self.has_class: bool = self.has_classDef(node)
        self.made_class_list = False

    def is_functionScope(self) -> bool:
        """Whether the generator has any parent function scope"""
        scope = self.scope
        while scope is not None:
            if isinstance(scope, symbols.FunctionScope):
                return True
            scope = scope.parent
        return False

    def has_classDef(self, node: AST) -> bool:
        visitor = FindClassDef()
        visitor.check(node)
        return visitor.has_class

    def emit_load_fixed_methods(self) -> None:
        # load and store <strict-modules>
        self.emit("LOAD_NAME", "<fixed-modules>")
        self.emit("LOAD_CONST", "__strict__")
        self.emit("BINARY_SUBSCR")
        self.emit("DUP_TOP")
        self.emit("STORE_GLOBAL", "<strict-modules>")
        self.emit("LOAD_CONST", "freeze_type")
        self.emit("BINARY_SUBSCR")
        self.emit("STORE_GLOBAL", "<freeze-type>")

    def emit_append_class_list(self) -> None:
        """
        Assumes the class to append is TOS1.
        Assumes `<classes>` is at TOS
        Pops that class and append to `<classes>`
        Do not Leave `<classes>` on the stack
        """
        self.emit("ROT_TWO")
        self.emit("LIST_APPEND", 1)
        self.emit("POP_TOP")

    def emit_create_class_list(self) -> None:
        """create and store an empty list `<classes>`"""
        self.emit("BUILD_LIST", 0)
        op = "STORE_FAST" if self.is_functionScope() else "STORE_GLOBAL"
        self.emit(op, self.class_list_name)
        self.made_class_list = True

    def emit_freeze_class_list(self) -> None:
        """
        create a for loop that iterates on all of `<classes>`
        assign the value to `<class>`, and call
        freeze_type on `<class>`
        """

        start = self.newBlock()
        anchor = self.newBlock()
        after = self.newBlock()

        self.push_loop(FOR_LOOP, start, after)
        self.emit_load_class_list()
        self.emit("GET_ITER")

        self.nextBlock(start)
        # for <class> in <classes>
        # we don't actually need to assign to <class>
        self.emit("FOR_ITER", anchor)
        self.emit("LOAD_GLOBAL", "<freeze-type>")
        # argument need to be top most
        self.emit("ROT_TWO")
        self.emit("CALL_FUNCTION", 1)
        # discard the result of call
        self.emit("POP_TOP")

        self.emit("JUMP_ABSOLUTE", start)
        self.nextBlock(anchor)
        self.pop_loop()
        self.nextBlock(after)

    def emit_load_class_list(self) -> None:
        """load `<classes>` on top of stack"""
        op = "LOAD_FAST" if self.is_functionScope() else "LOAD_GLOBAL"
        self.emit(op, self.class_list_name)

    def find_immutability_flag(self, node: ClassDef) -> bool:
        if not enable_strict_features:
            return super().find_immutability_flag(node)
        old_size = len(node.decorator_list)
        node.decorator_list = [d for d in node.decorator_list if not is_mutable(d)]
        return old_size == len(node.decorator_list)

    def register_immutability(self, node: ClassDef, flag: bool) -> None:
        if not enable_strict_features:
            return super().register_immutability(node, flag)
        if self.has_class and flag:
            self.emit("DUP_TOP")
            self.emit_load_class_list()
            self.emit_append_class_list()
        super().register_immutability(node, flag)

    def processBody(self, node: AST, body: List[AST] | AST, gen: CodeGenerator) -> None:
        if not enable_strict_features:
            return super().processBody(node, body, gen)
        if (
            isinstance(node, (FunctionDef, AsyncFunctionDef))
            and isinstance(gen, StrictCodeGenerator)
            and gen.has_class
        ):
            # initialize the <classes> list
            if not gen.made_class_list:
                gen.emit_create_class_list()
            # create a try + finally structure where we freeze all classes
            # in the finally block
            try_body = gen.newBlock("try_finally_body")
            end = gen.newBlock("try_finally_end")
            break_finally = True

            # copied logic from emit_try_finally
            with gen.graph.new_compile_scope() as compile_end_finally:
                gen.nextBlock(end)
                gen.setups.push(Entry(END_FINALLY, end, end))
                gen.emit_freeze_class_list()
                gen.emit("END_FINALLY")
                break_finally = gen.setups[-1].exit is None
                if break_finally:
                    gen.emit("POP_TOP")
                gen.setups.pop()

            if break_finally:
                gen.emit("LOAD_CONST", None)
            gen.emit("SETUP_FINALLY", end)
            gen.nextBlock(try_body)
            gen.setups.push(
                Entry(
                    TRY_FINALLY_BREAK if break_finally else TRY_FINALLY, try_body, end
                )
            )
            # normal function body here
            super().processBody(node, body, gen)
            gen.emit("POP_BLOCK")
            gen.emit("BEGIN_FINALLY")
            gen.setups.pop()

            gen.graph.apply_from_scope(compile_end_finally)
        else:
            super().processBody(node, body, gen)

    def startModule(self) -> None:
        if not enable_strict_features:
            return super().startModule()
        self.emit_load_fixed_methods()
        if self.has_class and not self.made_class_list:
            self.emit_create_class_list()
        super().startModule()

    def emit_module_return(self, node: ast.Module) -> None:
        if not enable_strict_features:
            return super().emit_module_return(node)
        if self.has_class:
            self.emit_freeze_class_list()
        super().emit_module_return(node)

    def emit_import_fixed_modules(
        self, node: ImportFrom, mod_name: str, mod: Mapping[str, object]
    ) -> None:
        new_names = [n for n in node.names if n.name not in mod]
        if new_names:
            # We have names we don't know about, keep them around...
            new_node = ImportFrom(mod_name, new_names, node.level)
            super().visitImportFrom(new_node)

        # Load the module into TOS...
        self.emit("LOAD_NAME", "<fixed-modules>")
        self.emit("LOAD_CONST", mod_name)
        self.emit("BINARY_SUBSCR")  # TOS = mod

        # Store all of the imported names from the module
        for _i, name in enumerate(node.names):
            var_name = name.name
            asname = name.asname or var_name
            value = mod.get(var_name)
            if value is not None:
                # duplicate TOS (mod)
                self.emit("DUP_TOP")
                # var name
                self.emit("LOAD_CONST", var_name)
                self.emit("BINARY_SUBSCR")
                self.emit("STORE_GLOBAL", asname)
        # remove TOS (mod)
        self.emit("POP_TOP")

    def visitImportFrom(self, node: ImportFrom) -> None:
        if not enable_strict_features:
            return super().visitImportFrom(node)
        if node.level == 0 and node.module is not None and node.module in FIXED_MODULES:
            module_name = node.module
            assert module_name is not None
            mod = FIXED_MODULES[module_name]
            self.emit_import_fixed_modules(node, module_name, mod)
            return
        super().visitImportFrom(node)


def strict_compile(name: str, filename: str, tree: AST, optimize: int = 0) -> CodeType:
    code_gen = StrictCodeGenerator.make_code_gen(
        name, tree, filename, flags=0, optimize=optimize
    )
    return code_gen.getCode()
