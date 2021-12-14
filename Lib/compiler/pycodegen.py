# Portions copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
# pyre-unsafe
from __future__ import annotations

import ast
import importlib.util
import itertools
import marshal
import os
import sys
from ast import AST, ClassDef
from builtins import compile as builtin_compile
from contextlib import contextmanager
from typing import Union

from . import consts, future, misc, pyassem, symbols
from .consts import (
    CO_ASYNC_GENERATOR,
    CO_COROUTINE,
    CO_GENERATOR,
    CO_NESTED,
    CO_VARARGS,
    CO_VARKEYWORDS,
    SC_CELL,
    SC_FREE,
    SC_GLOBAL_EXPLICIT,
    SC_GLOBAL_IMPLICIT,
    SC_LOCAL,
    PyCF_MASK_OBSOLETE,
    PyCF_ONLY_AST,
    PyCF_SOURCE_IS_UTF8,
)
from .optimizer import AstOptimizer
from .pyassem import PyFlowGraph
from .symbols import Scope, SymbolVisitor
from .unparse import to_expr
from .visitor import ASTVisitor, walk

TYPE_CHECKING = False
if TYPE_CHECKING:
    from typing import List, Optional, Sequence, Union, Type, Tuple

try:
    import _parser  # pyre-ignore[21]
except ImportError:
    parse_callable = builtin_compile
else:
    parse_callable = _parser.parse


callfunc_opcode_info = {
    # (Have *args, Have **args) : opcode
    (0, 0): "CALL_FUNCTION",
    (1, 0): "CALL_FUNCTION_VAR",
    (0, 1): "CALL_FUNCTION_KW",
    (1, 1): "CALL_FUNCTION_VAR_KW",
}

# This is a super set of all Python versions
LOOP = 1
EXCEPT = 2
TRY_FINALLY = 3
END_FINALLY = 4
WHILE_LOOP = 5
FOR_LOOP = 6
TRY_FINALLY_BREAK = 7
WITH = 8
ASYNC_WITH = 9
HANDLER_CLEANUP = 10

_ZERO = (0).to_bytes(4, "little")

_DEFAULT_MODNAME = sys.intern("<module>")


FuncOrLambda = Union[ast.FunctionDef, ast.AsyncFunctionDef, ast.Lambda]
CompNode = Union[ast.SetComp, ast.DictComp, ast.ListComp]


def make_header(mtime, size):
    return _ZERO + mtime.to_bytes(4, "little") + size.to_bytes(4, "little")


def compileFile(filename, display=0, compiler=None, modname=_DEFAULT_MODNAME):
    # compile.c uses marshal to write a long directly, with
    # calling the interface that would also generate a 1-byte code
    # to indicate the type of the value.  simplest way to get the
    # same effect is to call marshal and then skip the code.
    fileinfo = os.stat(filename)

    with open(filename, "U") as f:
        buf = f.read()
    code = compile(buf, filename, "exec", compiler=compiler, modname=modname)
    with open(filename + "c", "wb") as f:
        hdr = make_header(int(fileinfo.st_mtime), fileinfo.st_size)
        f.write(importlib.util.MAGIC_NUMBER)
        f.write(hdr)
        marshal.dump(code, f)


def compile(
    source,
    filename,
    mode,
    flags=0,
    dont_inherit=None,
    optimize=-1,
    compiler=None,
    modname=_DEFAULT_MODNAME,
):
    """Replacement for builtin compile() function"""
    if dont_inherit is not None:
        raise RuntimeError("not implemented yet")

    result = make_compiler(source, filename, mode, flags, optimize, compiler, modname)
    if flags & PyCF_ONLY_AST:
        return result
    return result.getCode()


def parse(source, filename, mode, flags):
    return parse_callable(source, filename, mode, flags | PyCF_ONLY_AST)


def make_compiler(
    source,
    filename,
    mode,
    flags=0,
    optimize=-1,
    generator=None,
    modname=_DEFAULT_MODNAME,
    peephole_enabled=True,
    ast_optimizer_enabled=True,
):
    if mode not in ("single", "exec", "eval"):
        raise ValueError("compile() mode must be 'exec', 'eval' or 'single'")

    if generator is None:
        generator = get_default_generator()

    if flags & ~(consts.PyCF_MASK | PyCF_MASK_OBSOLETE | consts.PyCF_COMPILE_MASK):
        raise ValueError("compile(): unrecognised flags", hex(flags))

    flags |= PyCF_SOURCE_IS_UTF8

    if isinstance(source, ast.AST):
        tree = source
    else:
        tree = parse(source, filename, mode, flags & consts.PyCF_MASK)

    if flags & PyCF_ONLY_AST:
        return tree

    optimize = sys.flags.optimize if optimize == -1 else optimize

    return generator.make_code_gen(
        modname,
        tree,
        filename,
        flags,
        optimize,
        peephole_enabled=peephole_enabled,
        ast_optimizer_enabled=ast_optimizer_enabled,
    )


def is_const(node):
    is_const_node = isinstance(
        node,
        (ast.Num, ast.Str, ast.Ellipsis, ast.Bytes, ast.NameConstant, ast.Constant),
    )
    is_debug = isinstance(node, ast.Name) and node.id == "__debug__"
    return is_const_node or is_debug


def all_items_const(seq, begin, end):
    for item in seq[begin:end]:
        if not is_const(item):
            return False
    return True


CONV_STR = ord("s")
CONV_REPR = ord("r")
CONV_ASCII = ord("a")


class CodeGenerator(ASTVisitor):
    """Defines basic code generator for Python bytecode

    This class is an abstract base class.  Concrete subclasses must
    define an __init__() that defines self.graph and then calls the
    __init__() defined in this class.
    """

    optimized = 0  # is namespace access optimized?
    __initialized = None
    class_name = None  # provide default for instance variable
    future_flags = 0
    flow_graph = pyassem.PyFlowGraph
    _SymbolVisitor = symbols.SymbolVisitor

    def __init__(
        self,
        parent: Optional[CodeGenerator],
        node: AST,
        symbols: SymbolVisitor,
        graph: PyFlowGraph,
        flags=0,
        optimization_lvl=0,
    ):
        super().__init__()
        self.module_gen = self if parent is None else parent.module_gen
        self.tree = node
        self.symbols = symbols
        self.graph = graph
        self.scopes = symbols.scopes
        self.setups = misc.Stack()
        self.last_lineno = None
        self._setupGraphDelegation()
        self.interactive = False
        self.graph.setFlag(self.module_gen.future_flags)
        self.scope = self.scopes[node]
        self.flags = flags
        self.optimization_lvl = optimization_lvl
        self.strip_docstrings = optimization_lvl == 2
        self.__with_count = 0
        self.did_setup_annotations = False
        self._qual_name = None

    def _setupGraphDelegation(self):
        self.emit = self.graph.emit
        self.newBlock = self.graph.newBlock
        self.nextBlock = self.graph.nextBlock

    def getCode(self):
        """Return a code object"""
        return self.graph.getCode()

    def set_qual_name(self, qualname):
        pass

    @contextmanager
    def noEmit(self):
        self.graph.do_not_emit_bytecode += 1
        try:
            yield
        finally:
            self.graph.do_not_emit_bytecode -= 1

    @contextmanager
    def noOp(self):
        yield

    def maybeEmit(self, test):
        return self.noOp() if test else self.noEmit()

    def mangle(self, name):
        if self.class_name is not None:
            return misc.mangle(name, self.class_name)
        else:
            return name

    def get_module(self):
        raise RuntimeError("should be implemented by subclasses")

    # Next five methods handle name access

    def storeName(self, name):
        self._nameOp("STORE", name)

    def loadName(self, name):
        self._nameOp("LOAD", name)

    def delName(self, name):
        self._nameOp("DELETE", name)

    def _nameOp(self, prefix, name):
        name = self.mangle(name)
        scope = self.scope.check_name(name)
        if scope == SC_LOCAL:
            if not self.optimized:
                self.emit(prefix + "_NAME", name)
            else:
                self.emit(prefix + "_FAST", name)
        elif scope == SC_GLOBAL_EXPLICIT:
            self.emit(prefix + "_GLOBAL", name)
        elif scope == SC_GLOBAL_IMPLICIT:
            if not self.optimized:
                self.emit(prefix + "_NAME", name)
            else:
                self.emit(prefix + "_GLOBAL", name)
        elif scope == SC_FREE or scope == SC_CELL:
            if isinstance(self.scope, symbols.ClassScope):
                if prefix == "STORE" and name not in self.scope.nonlocals:
                    self.emit(prefix + "_NAME", name)
                    return

            if isinstance(self.scope, symbols.ClassScope) and prefix == "LOAD":
                self.emit(prefix + "_CLASSDEREF", name)
            else:
                self.emit(prefix + "_DEREF", name)
        else:
            raise RuntimeError("unsupported scope for var %s: %d" % (name, scope))

    def _implicitNameOp(self, prefix, name):
        """Emit name ops for names generated implicitly by for loops

        The interpreter generates names that start with a period or
        dollar sign.  The symbol table ignores these names because
        they aren't present in the program text.
        """
        if self.optimized:
            self.emit(prefix + "_FAST", name)
        else:
            self.emit(prefix + "_NAME", name)

    def set_lineno(self, node):
        if hasattr(node, "lineno"):
            self.graph.lineno = node.lineno
            self.graph.lineno_set = False

    def update_lineno(self, node):
        if hasattr(node, "lineno") and node.lineno != self.graph.lineno:
            self.set_lineno(node)

    def skip_docstring(self, body):
        """Given list of statements, representing body of a function, class,
        or module, skip docstring, if any.
        """
        if (
            body
            and isinstance(body[0], ast.Expr)
            and isinstance(body[0].value, ast.Str)
        ):
            return body[1:]
        return body

    # The first few visitor methods handle nodes that generator new
    # code objects.  They use class attributes to determine what
    # specialized code generators to use.

    def visitInteractive(self, node):
        self.interactive = True
        self.visit(node.body)
        self.emit("LOAD_CONST", None)
        self.emit("RETURN_VALUE")

    def findFutures(self, node):
        future_flags = self.flags & consts.PyCF_MASK
        for feature in future.find_futures(node):
            if feature == "barry_as_FLUFL":
                future_flags |= consts.CO_FUTURE_BARRY_AS_BDFL
            elif feature == "annotations":
                future_flags |= consts.CO_FUTURE_ANNOTATIONS
        return future_flags

    def visitModule(self, node: ast.Module) -> None:
        self.future_flags = self.findFutures(node)
        self.graph.setFlag(self.future_flags)

        if node.body:
            self.set_lineno(node.body[0])

        # Set current line number to the line number of first statement.
        # This way line number for SETUP_ANNOTATIONS will always
        # coincide with the line number of first "real" statement in module.
        # If body is empy, then lineno will be set later in assemble.
        if self.findAnn(node.body):
            self.emit("SETUP_ANNOTATIONS")
            self.did_setup_annotations = True
        doc = self.get_docstring(node)
        if doc is not None:
            self.emit("LOAD_CONST", doc)
            self.storeName("__doc__")
        self.startModule()
        self.visit(self.skip_docstring(node.body))

        # See if the was a live statement, to later set its line number as
        # module first line. If not, fall back to first line of 1.
        if not self.graph.first_inst_lineno:
            self.graph.first_inst_lineno = 1

        self.emit_module_return(node)

    def startModule(self) -> None:
        pass

    def emit_module_return(self, node: ast.Module) -> None:
        self.emit("LOAD_CONST", None)
        self.emit("RETURN_VALUE")

    def visitExpression(self, node):
        self.visit(node.body)
        self.emit("RETURN_VALUE")

    def visitFunctionDef(self, node):
        self.set_lineno(node)
        self.visitFunctionOrLambda(node)

    visitAsyncFunctionDef = visitFunctionDef

    def visitJoinedStr(self, node):
        self.update_lineno(node)
        for value in node.values:
            self.visit(value)
        if len(node.values) != 1:
            self.emit("BUILD_STRING", len(node.values))

    def visitFormattedValue(self, node):
        self.update_lineno(node)
        self.visit(node.value)

        if node.conversion == CONV_STR:
            oparg = pyassem.FVC_STR
        elif node.conversion == CONV_REPR:
            oparg = pyassem.FVC_REPR
        elif node.conversion == CONV_ASCII:
            oparg = pyassem.FVC_ASCII
        else:
            assert node.conversion == -1, str(node.conversion)
            oparg = pyassem.FVC_NONE

        if node.format_spec:
            self.visit(node.format_spec)
            oparg |= pyassem.FVS_HAVE_SPEC
        self.emit("FORMAT_VALUE", oparg)

    def visitLambda(self, node):
        self.update_lineno(node)
        self.visitFunctionOrLambda(node)

    def processBody(self, node, body, gen):
        if isinstance(body, list):
            for stmt in body:
                gen.visit(stmt)
        else:
            gen.visit(body)

    def _visitAnnotation(self, node):
        if self.module_gen.future_flags & consts.CO_FUTURE_ANNOTATIONS:
            self.emit("LOAD_CONST", to_expr(node))
        else:
            self.visit(node)

    def generate_function(
        self,
        node: FuncOrLambda,
        name: str,
        first_lineno: int,
    ) -> CodeGenerator:
        gen = self.make_func_codegen(node, node.args, name, first_lineno)
        body = node.body

        self.processBody(node, body, gen)

        gen.finishFunction()

        return gen

    def build_function(
        self,
        node: ast.FunctionDef | ast.AsyncFunctionDef | ast.Lambda,
        gen: CodeGenerator,
    ) -> None:
        flags = 0
        if node.args.defaults:
            for default in node.args.defaults:
                self.visitDefault(default)
                flags |= 0x01
            self.emit("BUILD_TUPLE", len(node.args.defaults))

        kwdefaults = []
        for kwonly, default in zip(node.args.kwonlyargs, node.args.kw_defaults):
            if default is not None:
                kwdefaults.append(self.mangle(kwonly.arg))
                self.visitDefault(default)

        if kwdefaults:
            self.emit("LOAD_CONST", tuple(kwdefaults))
            self.emit("BUILD_CONST_KEY_MAP", len(kwdefaults))
            flags |= 0x02

        if self.build_annotations(node):
            flags |= 0x04

        self._makeClosure(gen, flags)

    def build_annotations(
        self, node: ast.FunctionDef | ast.AsyncFunctionDef | ast.Lambda
    ) -> bool:
        ann_args = self.annotate_args(node.args)
        # Cannot annotate return type for lambda
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) and node.returns:
            self._visitAnnotation(node.returns)
            ann_args.append("return")
        if ann_args:
            self.emit("LOAD_CONST", tuple(ann_args))
            self.emit("BUILD_CONST_KEY_MAP", len(ann_args))
            return True

        return False

    def visitFunctionOrLambda(
        self, node: ast.FunctionDef | ast.AsyncFunctionDef | ast.Lambda
    ) -> None:
        if isinstance(node, ast.Lambda):
            name = sys.intern("<lambda>")
            ndecorators, first_lineno = 0, node.lineno
        else:
            name = node.name
            if node.decorator_list:
                for decorator in node.decorator_list:
                    self.visit(decorator)
                ndecorators = len(node.decorator_list)
                first_lineno = node.decorator_list[0].lineno
            else:
                ndecorators = 0
                first_lineno = node.lineno

        gen = self.generate_function(node, name, first_lineno)

        self.build_function(node, gen)

        for _ in range(ndecorators):
            self.emit("CALL_FUNCTION", 1)

        if not isinstance(node, ast.Lambda):
            self.storeName(gen.graph.name)

    def visitDefault(self, node: ast.expr) -> None:
        self.visit(node)

    def annotate_args(self, args: ast.arguments) -> List[str]:
        ann_args = []
        for arg in args.args:
            self.annotate_arg(arg, ann_args)
        for arg in args.posonlyargs:
            self.annotate_arg(arg, ann_args)
        if arg := args.vararg:
            self.annotate_arg(arg, ann_args)
        for arg in args.kwonlyargs:
            self.annotate_arg(arg, ann_args)
        if arg := args.kwarg:
            self.annotate_arg(arg, ann_args)
        return ann_args

    def annotate_arg(self, arg: ast.arg, ann_args: List[str]):
        if arg.annotation:
            self._visitAnnotation(arg.annotation)
            ann_args.append(self.mangle(arg.arg))

    def visitClassDef(self, node: ast.ClassDef) -> None:
        self.set_lineno(node)
        first_lineno = None
        immutability_flag = self.find_immutability_flag(node)
        for decorator in node.decorator_list:
            if first_lineno is None:
                first_lineno = decorator.lineno
            self.visit(decorator)

        gen = self.make_class_codegen(node, first_lineno or node.lineno)
        gen.emit("LOAD_NAME", "__name__")
        gen.storeName("__module__")
        gen.emit("LOAD_CONST", gen.get_qual_prefix(gen) + gen.name)
        gen.storeName("__qualname__")
        if gen.findAnn(node.body):
            gen.did_setup_annotations = True
            gen.emit("SETUP_ANNOTATIONS")

        doc = gen.get_docstring(node)
        if doc is not None:
            gen.update_lineno(node.body[0])
            gen.emit("LOAD_CONST", doc)
            gen.storeName("__doc__")

        self.walkClassBody(node, gen)

        gen.graph.startExitBlock()
        if "__class__" in gen.scope.cells:
            gen.emit("LOAD_CLOSURE", "__class__")
            gen.emit("DUP_TOP")
            gen.emit("STORE_NAME", "__classcell__")
        else:
            gen.emit("LOAD_CONST", None)
        gen.emit("RETURN_VALUE")

        self.emit("LOAD_BUILD_CLASS")
        self._makeClosure(gen, 0)
        self.emit("LOAD_CONST", node.name)

        self._call_helper(2, node.bases, node.keywords)

        for _ in range(len(node.decorator_list)):
            self.emit("CALL_FUNCTION", 1)

        self.register_immutability(node, immutability_flag)
        self.post_process_and_store_name(node)

    def find_immutability_flag(self, node: ClassDef) -> bool:
        return False

    def register_immutability(self, node: ClassDef, flag: bool) -> None:
        """
        Note: make sure this do not have side effect on the
        stack, assumes class is on the stack
        """

    def post_process_and_store_name(self, node: ClassDef) -> None:
        self.storeName(node.name)

    def walkClassBody(self, node: ClassDef, gen: "CodeGenerator"):
        walk(self.skip_docstring(node.body), gen)

    # The rest are standard visitor methods

    # The next few implement control-flow statements

    def visitIf(self, node):
        self.set_lineno(node)
        test = node.test
        test_const = self.get_bool_const(test)

        # Emulate co_firstlineno behavior of C compiler
        if test_const is False and not node.orelse:
            self.graph.maybeEmitSetLineno()

        end = self.newBlock("if_end")
        orelse = None
        if node.orelse:
            orelse = self.newBlock("if_else")

        if test_const is None:
            self.compileJumpIf(test, orelse or end, False)

        with self.maybeEmit(test_const is not False):
            self.nextBlock()
            self.visit(node.body)

        if node.orelse:
            if test_const is None:
                self.emit("JUMP_FORWARD", end)
            with self.maybeEmit(test_const is not True):
                self.nextBlock(orelse)
                self.visit(node.orelse)

        self.nextBlock(end)

    def visitWhile(self, node):
        self.set_lineno(node)

        test_const = self.get_bool_const(node.test)
        loop = self.newBlock("while_loop")
        else_ = self.newBlock("while_else")
        after = self.newBlock("while_after")

        self.push_loop(WHILE_LOOP, loop, after)

        if test_const is False:
            with self.noEmit():
                self.visit(node.test)
                self.visit(node.body)
            self.pop_loop()
            if node.orelse:
                self.visit(node.orelse)
            self.nextBlock(after)
            return
        elif test_const is True:
            # emulate co_firstlineno behavior of C compiler
            self.graph.maybeEmitSetLineno()

        self.nextBlock(loop)

        with self.maybeEmit(test_const is not True):
            self.compileJumpIf(node.test, else_ or after, False)

        self.nextBlock(label="while_body")
        self.visit(node.body)
        self.emit("JUMP_ABSOLUTE", loop)

        with self.maybeEmit(test_const is not True):
            self.nextBlock(else_ or after)  # or just the POPs if not else clause

        self.pop_loop()
        if node.orelse:
            self.visit(node.orelse)
        self.nextBlock(after)

    def push_loop(self, kind, start, end):
        self.setups.push(Entry(kind, start, end))

    def pop_loop(self):
        self.setups.pop()

    def visitFor(self, node):
        start = self.newBlock()
        anchor = self.newBlock()
        after = self.newBlock()

        self.set_lineno(node)
        self.push_loop(FOR_LOOP, start, after)
        self.visit(node.iter)
        self.emit("GET_ITER")

        self.nextBlock(start)
        self.emit("FOR_ITER", anchor)
        self.visit(node.target)
        self.visit(node.body)
        self.emit("JUMP_ABSOLUTE", start)
        self.nextBlock(anchor)
        self.pop_loop()

        if node.orelse:
            self.visit(node.orelse)
        self.nextBlock(after)

    def visitAsyncFor(self, node):
        start = self.newBlock("async_for_try")
        except_ = self.newBlock("except")
        end = self.newBlock("end")

        self.set_lineno(node)
        self.visit(node.iter)
        self.emit("GET_AITER")

        self.nextBlock(start)

        self.setups.push(Entry(FOR_LOOP, start, end))
        self.emit("SETUP_FINALLY", except_)
        self.emit("GET_ANEXT")
        self.emit("LOAD_CONST", None)
        self.emit("YIELD_FROM")
        self.emit("POP_BLOCK")
        self.visit(node.target)
        self.visit(node.body)
        self.emit("JUMP_ABSOLUTE", start)
        self.setups.pop()

        self.nextBlock(except_)
        self.emit("END_ASYNC_FOR")
        if node.orelse:
            self.visit(node.orelse)
        self.nextBlock(end)

    def visitBreak(self, node):
        self.set_lineno(node)
        for b in reversed(self.setups):
            self.unwind_setup_entry(b, 0)
            if b.kind == WHILE_LOOP or b.kind == FOR_LOOP:
                self.emit("JUMP_ABSOLUTE", b.exit)
                return
        raise SyntaxError("'break' outside loop", self.syntax_error_position(node))

    def visitContinue(self, node):
        self.set_lineno(node)

        for e in reversed(self.setups):
            if e.kind in (FOR_LOOP, WHILE_LOOP):
                self.emit("JUMP_ABSOLUTE", e.block)
                return
            self.unwind_setup_entry(e, 0)
        raise SyntaxError(
            "'continue' not properly in loop", self.syntax_error_position(node)
        )

    def syntax_error_position(self, node):
        import linecache

        source_line = linecache.getline(self.graph.filename, node.lineno)
        return self.graph.filename, node.lineno, node.col_offset, source_line or None

    def syntax_error(self, msg, node):
        import linecache

        source_line = linecache.getline(self.graph.filename, node.lineno)
        return SyntaxError(
            msg,
            (self.graph.filename, node.lineno, node.col_offset, source_line or None),
        )

    def compileJumpIfPop(self, test, label, is_if_true):
        self.visit(test)
        self.emit(
            "JUMP_IF_TRUE_OR_POP" if is_if_true else "JUMP_IF_FALSE_OR_POP", label
        )

    def visitTest(self, node, is_if_true: bool):
        end = self.newBlock()
        for child in node.values[:-1]:
            self.compileJumpIfPop(child, end, is_if_true)
            self.nextBlock()
        self.visit(node.values[-1])
        self.nextBlock(end)

    def visitBoolOp(self, node):
        self.visitTest(node, type(node.op) == ast.Or)

    _cmp_opcode = {
        ast.Eq: "==",
        ast.NotEq: "!=",
        ast.Lt: "<",
        ast.LtE: "<=",
        ast.Gt: ">",
        ast.GtE: ">=",
        ast.Is: "is",
        ast.IsNot: "is not",
        ast.In: "in",
        ast.NotIn: "not in",
    }

    def compileJumpIf(self, test, next, is_if_true):
        if isinstance(test, ast.UnaryOp):
            if isinstance(test.op, ast.Not):
                # Compile to remove not operation
                self.compileJumpIf(test.operand, next, not is_if_true)
                return
        elif isinstance(test, ast.BoolOp):
            is_or = isinstance(test.op, ast.Or)
            skip_jump = next
            if is_if_true != is_or:
                skip_jump = self.newBlock()

            for node in test.values[:-1]:
                self.compileJumpIf(node, skip_jump, is_or)

            self.compileJumpIf(test.values[-1], next, is_if_true)

            if skip_jump is not next:
                self.nextBlock(skip_jump)
            return
        elif isinstance(test, ast.IfExp):
            end = self.newBlock("end")
            orelse = self.newBlock("orelse")
            # Jump directly to orelse if test matches
            self.compileJumpIf(test.test, orelse, 0)
            # Jump directly to target if test is true and body is matches
            self.compileJumpIf(test.body, next, is_if_true)
            self.emit("JUMP_FORWARD", end)
            # Jump directly to target if test is true and orelse matches
            self.nextBlock(orelse)
            self.compileJumpIf(test.orelse, next, is_if_true)

            self.nextBlock(end)
            return
        elif isinstance(test, ast.Compare):
            if len(test.ops) > 1:
                cleanup = self.newBlock()
                self.visit(test.left)
                for op, comparator in zip(test.ops[:-1], test.comparators[:-1]):
                    self.emitChainedCompareStep(
                        op, comparator, cleanup, always_pop=True
                    )
                self.visit(test.comparators[-1])
                self.emit("COMPARE_OP", self._cmp_opcode[type(test.ops[-1])])
                self.emit(
                    "POP_JUMP_IF_TRUE" if is_if_true else "POP_JUMP_IF_FALSE", next
                )
                end = self.newBlock()
                self.emit("JUMP_FORWARD", end)
                self.nextBlock(cleanup)
                self.emit("POP_TOP")
                if not is_if_true:
                    self.emit("JUMP_FORWARD", next)
                self.nextBlock(end)
                return

        self.visit(test)
        self.emit("POP_JUMP_IF_TRUE" if is_if_true else "POP_JUMP_IF_FALSE", next)
        return True

    def visitIfExp(self, node):
        endblock = self.newBlock()
        elseblock = self.newBlock()
        self.compileJumpIf(node.test, elseblock, False)
        self.visit(node.body)
        self.emit("JUMP_FORWARD", endblock)
        self.nextBlock(elseblock)
        self.visit(node.orelse)
        self.nextBlock(endblock)

    def emitChainedCompareStep(self, op, value, cleanup, always_pop=False):
        self.visit(value)
        self.emit("DUP_TOP")
        self.emit("ROT_THREE")
        self.defaultEmitCompare(op)
        self.emit(
            "POP_JUMP_IF_FALSE" if always_pop else "JUMP_IF_FALSE_OR_POP", cleanup
        )
        self.nextBlock(label="compare_or_cleanup")

    def defaultEmitCompare(self, op):
        self.emit("COMPARE_OP", self._cmp_opcode[type(op)])

    def visitCompare(self, node):
        self.update_lineno(node)
        self.visit(node.left)
        cleanup = self.newBlock("cleanup")
        for op, code in zip(node.ops[:-1], node.comparators[:-1]):
            self.emitChainedCompareStep(op, code, cleanup)
        # now do the last comparison
        if node.ops:
            op = node.ops[-1]
            code = node.comparators[-1]
            self.visit(code)
            self.defaultEmitCompare(op)
        if len(node.ops) > 1:
            end = self.newBlock("end")
            self.emit("JUMP_FORWARD", end)
            self.nextBlock(cleanup)
            self.emit("ROT_TWO")
            self.emit("POP_TOP")
            self.nextBlock(end)

    def get_qual_prefix(self, gen):
        prefix = ""
        if gen.scope.global_scope:
            return prefix
        # Construct qualname prefix
        parent = gen.scope.parent
        while not isinstance(parent, symbols.ModuleScope):
            # Only real functions use "<locals>", nested scopes like
            # comprehensions don't.
            if parent.is_function_scope:
                prefix = parent.name + ".<locals>." + prefix
            else:
                prefix = parent.name + "." + prefix
            if parent.global_scope:
                break
            parent = parent.parent
        return prefix

    def _makeClosure(self, gen, flags):
        prefix = ""
        if not isinstance(gen.tree, ast.ClassDef):
            prefix = self.get_qual_prefix(gen)

        frees = gen.scope.get_free_vars()
        if frees:
            for name in frees:
                self.emit("LOAD_CLOSURE", name)
            self.emit("BUILD_TUPLE", len(frees))
            flags |= 0x08

        gen.set_qual_name(prefix + gen.name)
        self.emit("LOAD_CONST", gen)
        self.emit("LOAD_CONST", prefix + gen.name)  # py3 qualname
        self.emit("MAKE_FUNCTION", flags)

    def visitDelete(self, node):
        self.set_lineno(node)
        self.visit(node.targets)

    def conjure_arguments(self, args: List[ast.arg]) -> ast.arguments:
        return ast.arguments([], args, None, [], [], None, [])

    def compile_comprehension(
        self,
        node: CompNode,
        name: str,
        elt: ast.expr,
        val: ast.expr | None,
        opcode: str,
        oparg: object = 0,
    ) -> None:
        args = self.conjure_arguments([ast.arg(".0", None)])
        self.update_lineno(node)
        gen = self.make_func_codegen(node, args, name, node.lineno)

        if opcode:
            gen.emit(opcode, oparg)

        gen.compile_comprehension_generator(
            node.generators, 0, elt, val, type(node), True
        )

        if not isinstance(node, ast.GeneratorExp):
            gen.emit("RETURN_VALUE")

        gen.finishFunction()

        self._makeClosure(gen, 0)

        # precomputation of outmost iterable
        self.visit(node.generators[0].iter)
        if node.generators[0].is_async:
            self.emit("GET_AITER")
        else:
            self.emit("GET_ITER")
        self.emit("CALL_FUNCTION", 1)

        if gen.scope.coroutine and type(node) is not ast.GeneratorExp:
            self.emit("GET_AWAITABLE")
            self.emit("LOAD_CONST", None)
            self.emit("YIELD_FROM")

    def visitGeneratorExp(self, node):
        self.compile_comprehension(node, sys.intern("<genexpr>"), node.elt, None, None)

    def visitListComp(self, node):
        self.compile_comprehension(
            node, sys.intern("<listcomp>"), node.elt, None, "BUILD_LIST"
        )

    def visitSetComp(self, node):
        self.compile_comprehension(
            node, sys.intern("<setcomp>"), node.elt, None, "BUILD_SET"
        )

    def visitDictComp(self, node):
        self.compile_comprehension(
            node, sys.intern("<dictcomp>"), node.key, node.value, "BUILD_MAP"
        )

    def compile_comprehension_generator(
        self, generators, gen_index, elt, val, type, outermost_gen_is_param
    ):
        if generators[gen_index].is_async:
            self.compile_async_comprehension(
                generators, gen_index, elt, val, type, outermost_gen_is_param
            )
        else:
            self.compile_sync_comprehension(
                generators, gen_index, elt, val, type, outermost_gen_is_param
            )

    def compile_async_comprehension(
        self, generators, gen_index, elt, val, type, outermost_gen_is_param
    ):
        start = self.newBlock("start")
        except_ = self.newBlock("except")
        if_cleanup = self.newBlock("if_cleanup")

        gen = generators[gen_index]
        if gen_index == 0 and outermost_gen_is_param:
            self.loadName(".0")
        else:
            self.visit(gen.iter)
            self.emit("GET_AITER")

        self.nextBlock(start)
        self.emit("SETUP_FINALLY", except_)
        self.emit("GET_ANEXT")
        self.emit("LOAD_CONST", None)
        self.emit("YIELD_FROM")
        self.emit("POP_BLOCK")
        self.visit(gen.target)

        for if_ in gen.ifs:
            self.compileJumpIf(if_, if_cleanup, False)
            self.newBlock()

        gen_index += 1
        if gen_index < len(generators):
            self.compile_comprehension_generator(
                generators, gen_index, elt, val, type, False
            )
        elif type is ast.GeneratorExp:
            self.visit(elt)
            self.emit("YIELD_VALUE")
            self.emit("POP_TOP")
        elif type is ast.ListComp:
            self.visit(elt)
            self.emit("LIST_APPEND", gen_index + 1)
        elif type is ast.SetComp:
            self.visit(elt)
            self.emit("SET_ADD", gen_index + 1)
        elif type is ast.DictComp:
            self.compile_dictcomp_element(elt, val)
            self.emit("MAP_ADD", gen_index + 1)
        else:
            raise NotImplementedError("unknown comprehension type")

        self.nextBlock(if_cleanup)
        self.emit("JUMP_ABSOLUTE", start)

        self.nextBlock(except_)
        self.emit("END_ASYNC_FOR")

    def compile_sync_comprehension(
        self, generators, gen_index, elt, val, type, outermost_gen_is_param
    ):
        start = self.newBlock("start")
        skip = self.newBlock("skip")
        if_cleanup = self.newBlock("if_cleanup")
        anchor = self.newBlock("anchor")

        gen = generators[gen_index]
        if gen_index == 0 and outermost_gen_is_param:
            self.loadName(".0")
        else:
            self.visit(gen.iter)
            self.emit("GET_ITER")

        self.nextBlock(start)
        self.emit("FOR_ITER", anchor)
        self.nextBlock()
        self.visit(gen.target)

        for if_ in gen.ifs:
            self.compileJumpIf(if_, if_cleanup, False)
            self.newBlock()

        gen_index += 1
        if gen_index < len(generators):
            self.compile_comprehension_generator(
                generators, gen_index, elt, val, type, False
            )
        else:
            if type is ast.GeneratorExp:
                self.visit(elt)
                self.emit("YIELD_VALUE")
                self.emit("POP_TOP")
            elif type is ast.ListComp:
                self.visit(elt)
                self.emit("LIST_APPEND", gen_index + 1)
            elif type is ast.SetComp:
                self.visit(elt)
                self.emit("SET_ADD", gen_index + 1)
            elif type is ast.DictComp:
                self.compile_dictcomp_element(elt, val)
                self.emit("MAP_ADD", gen_index + 1)
            else:
                raise NotImplementedError("unknown comprehension type")

            self.nextBlock(skip)
        self.nextBlock(if_cleanup)
        self.emit("JUMP_ABSOLUTE", start)
        self.nextBlock(anchor)

    def compile_dictcomp_element(self, elt, val):
        self.visit(elt)
        self.visit(val)

    # exception related

    def visitAssert(self, node):
        # XXX would be interesting to implement this via a
        # transformation of the AST before this stage
        if not self.optimization_lvl:
            end = self.newBlock()
            self.set_lineno(node)
            # XXX AssertionError appears to be special case -- it is always
            # loaded as a global even if there is a local name.  I guess this
            # is a sort of renaming op.
            self.nextBlock()
            self.compileJumpIf(node.test, end, True)

            self.nextBlock()
            self.emit("LOAD_GLOBAL", "AssertionError")
            if node.msg:
                self.visit(node.msg)
                self.emit("CALL_FUNCTION", 1)
                self.emit("RAISE_VARARGS", 1)
            else:
                self.emit("RAISE_VARARGS", 1)
            self.nextBlock(end)

    def visitRaise(self, node):
        self.set_lineno(node)
        n = 0
        if node.exc:
            self.visit(node.exc)
            n = n + 1
        if node.cause:
            self.visit(node.cause)
            n = n + 1
        self.emit("RAISE_VARARGS", n)

    def visitTry(self, node):
        self.set_lineno(node)
        if node.finalbody:
            if node.handlers:
                self.emit_try_finally(
                    node,
                    lambda: self.visitTryExcept(node),
                    lambda: self.visit(node.finalbody),
                )
            else:
                self.emit_try_finally(
                    node,
                    lambda: self.visit(node.body),
                    lambda: self.visit(node.finalbody),
                )
            return

        self.visitTryExcept(node)

    def visitTryExcept(self, node):
        body = self.newBlock("try_body")
        except_ = self.newBlock("try_handlers")
        orElse = self.newBlock("try_else")
        end = self.newBlock("try_end")

        self.emit("SETUP_FINALLY", except_)
        self.nextBlock(body)

        self.setups.push(Entry(EXCEPT, body, None))
        self.visit(node.body)
        self.emit("POP_BLOCK")
        self.setups.pop()

        self.emit("JUMP_FORWARD", orElse)
        self.nextBlock(except_)

        last = len(node.handlers) - 1
        for i in range(len(node.handlers)):
            handler = node.handlers[i]
            expr = handler.type
            target = handler.name
            body = handler.body
            self.set_lineno(handler)
            except_ = self.newBlock(f"try_except_{i}")
            if expr:
                self.emit("DUP_TOP")
                self.visit(expr)
                self.emit("COMPARE_OP", "exception match")
                self.emit("POP_JUMP_IF_FALSE", except_)
            elif i < last:
                raise SyntaxError(
                    "default 'except:' must be last",
                    self.syntax_error_position(handler),
                )
            else:
                self.set_lineno(handler)
            self.emit("POP_TOP")
            if target:
                cleanup_end = self.newBlock(f"try_cleanup_end{i}")
                cleanup_body = self.newBlock(f"try_cleanup_body{i}")
                self.storeName(target)
                self.emit("POP_TOP")
                self.emit("SETUP_FINALLY", cleanup_end)
                self.nextBlock(cleanup_body)
                self.setups.push(Entry(HANDLER_CLEANUP, cleanup_body, cleanup_end))
                self.visit(body)
                self.emit("POP_BLOCK")
                self.emit("BEGIN_FINALLY")
                self.setups.pop()

                self.nextBlock(cleanup_end)
                self.setups.push(Entry(END_FINALLY, cleanup_end, None))
                self.emit("LOAD_CONST", None)
                self.storeName(target)
                self.delName(target)
                self.emit("END_FINALLY")
                self.emit("POP_EXCEPT")
                self.setups.pop()
            else:
                cleanup_body = self.newBlock(f"try_cleanup_body{i}")
                self.emit("POP_TOP")
                self.emit("POP_TOP")
                self.nextBlock(cleanup_body)
                self.setups.push(Entry(HANDLER_CLEANUP, cleanup_body, None))
                self.visit(body)
                self.emit("POP_EXCEPT")
                self.setups.pop()
            self.emit("JUMP_FORWARD", end)
            self.nextBlock(except_)

        self.emit("END_FINALLY")
        self.nextBlock(orElse)
        self.visit(node.orelse)
        self.nextBlock(end)

    def emit_except_local(self, handler: ast.ExceptHandler):
        target = handler.name
        if target:
            self.update_lineno(handler.type)
            self.storeName(target)
        else:
            self.emit("POP_TOP")
        self.emit("POP_TOP")

    def emit_try_finally(self, node, try_body, finalbody, except_protect=False):
        body = self.newBlock("try_finally_body")
        end = self.newBlock("try_finally_end")

        self.set_lineno(node)
        break_finally = True

        # compile FINALLY_END out of order to match CPython
        with self.graph.new_compile_scope() as compile_end_finally:
            self.nextBlock(end)

            self.setups.push(Entry(END_FINALLY, end, end))
            finalbody()
            self.emit("END_FINALLY")
            break_finally = self.setups[-1].exit is None
            if break_finally:
                self.emit("POP_TOP")
            self.setups.pop()

        self.set_lineno(node)
        if break_finally:
            self.emit("LOAD_CONST", None)
        self.emit("SETUP_FINALLY", end)
        self.nextBlock(body)
        self.setups.push(
            Entry(TRY_FINALLY_BREAK if break_finally else TRY_FINALLY, body, end)
        )
        try_body()
        self.emit("POP_BLOCK")
        self.emit("BEGIN_FINALLY")
        self.setups.pop()

        self.graph.apply_from_scope(compile_end_finally)

    def visitWith_(self, node, kind, pos=0):
        item = node.items[pos]

        block = self.newBlock("with_block")
        finally_ = self.newBlock("with_finally")
        self.visit(item.context_expr)
        if kind == ASYNC_WITH:
            self.emit("BEFORE_ASYNC_WITH")
            self.emit("GET_AWAITABLE")
            self.emit("LOAD_CONST", None)
            self.emit("YIELD_FROM")
            self.emit("SETUP_ASYNC_WITH", finally_)
        else:
            self.emit("SETUP_WITH", finally_)

        self.nextBlock(block)
        self.setups.push(Entry(kind, block, finally_))
        if item.optional_vars:
            self.visit(item.optional_vars)
        else:
            self.emit("POP_TOP")

        if pos + 1 < len(node.items):
            self.visitWith_(node, kind, pos + 1)
        else:
            self.visit(node.body)

        self.emit("POP_BLOCK")
        self.emit("BEGIN_FINALLY")
        self.setups.pop()

        self.nextBlock(finally_)

        self.setups.push(Entry(END_FINALLY, finally_, None))
        self.emit("WITH_CLEANUP_START")
        if kind == ASYNC_WITH:
            self.emit("GET_AWAITABLE")
            self.emit("LOAD_CONST", None)
            self.emit("YIELD_FROM")

        self.emit("WITH_CLEANUP_FINISH")
        self.emit("END_FINALLY")
        self.setups.pop()

    def visitWith(self, node):
        self.set_lineno(node)
        self.visitWith_(node, WITH, 0)

    def visitAsyncWith(self, node, pos=0):
        if not self.scope.coroutine:
            raise SyntaxError(
                "'async with' outside async function", self.syntax_error_position(node)
            )
        self.visitWith_(node, ASYNC_WITH, 0)

    # misc

    def visitExpr(self, node):
        self.set_lineno(node)
        # CPy3.6 discards lots of constants
        if self.interactive:
            self.visit(node.value)
            self.emit("PRINT_EXPR")
        elif not is_const(node.value):
            self.visit(node.value)
            self.emit("POP_TOP")

    def visitNum(self, node):
        self.update_lineno(node)
        self.emit("LOAD_CONST", node.n)

    def visitStr(self, node):
        self.update_lineno(node)
        self.emit("LOAD_CONST", node.s)

    def visitBytes(self, node):
        self.update_lineno(node)
        self.emit("LOAD_CONST", node.s)

    def visitNameConstant(self, node):
        self.update_lineno(node)
        self.emit("LOAD_CONST", node.value)

    def visitConst(self, node):
        self.update_lineno(node)
        self.emit("LOAD_CONST", node.value)

    def visitConstant(self, node: ast.Constant):
        self.update_lineno(node)
        self.emit("LOAD_CONST", node.value)

    def visitKeyword(self, node):
        self.emit("LOAD_CONST", node.name)
        self.visit(node.expr)

    def visitGlobal(self, node):
        self.set_lineno(node)
        # no code to generate

    def visitNonlocal(self, node):
        self.set_lineno(node)
        # no code to generate

    def visitName(self, node):
        self.update_lineno(node)
        if isinstance(node.ctx, ast.Store):
            self.storeName(node.id)
        elif isinstance(node.ctx, ast.Del):
            self.delName(node.id)
        elif node.id == "__debug__":
            self.emit("LOAD_CONST", not bool(self.optimization_lvl))
        else:
            self.loadName(node.id)

    def visitPass(self, node):
        self.set_lineno(node)

    def visitImport(self, node):
        self.set_lineno(node)
        level = 0
        for alias in node.names:
            name = alias.name
            asname = alias.asname
            self.emit("LOAD_CONST", level)
            self.emit("LOAD_CONST", None)
            self.emit("IMPORT_NAME", self.mangle(name))
            mod = name.split(".")[0]
            if asname:
                self.emitImportAs(name, asname)
            else:
                self.storeName(mod)

    def visitImportFrom(self, node):
        self.set_lineno(node)
        level = node.level
        fromlist = tuple(alias.name for alias in node.names)
        self.emit("LOAD_CONST", level)
        self.emit("LOAD_CONST", fromlist)
        self.emit("IMPORT_NAME", node.module or "")
        for alias in node.names:
            name = alias.name
            asname = alias.asname
            if name == "*":
                self.namespace = 0
                self.emit("IMPORT_STAR")
                # There can only be one name w/ from ... import *
                assert len(node.names) == 1
                return
            else:
                self.emit("IMPORT_FROM", name)
                self.storeName(asname or name)
        self.emit("POP_TOP")

    def emitImportAs(self, name: str, asname: str):
        elts = name.split(".")
        if len(elts) == 1:
            self.storeName(asname)
            return
        first = True
        for elt in elts[1:]:
            if not first:
                self.emit("ROT_TWO")
                self.emit("POP_TOP")
            self.emit("IMPORT_FROM", elt)
            first = False
        self.storeName(asname)
        self.emit("POP_TOP")

    def visitAttribute(self, node):
        self.update_lineno(node)
        self.visit(node.value)
        if isinstance(node.ctx, ast.Store):
            self.emit("STORE_ATTR", self.mangle(node.attr))
        elif isinstance(node.ctx, ast.Del):
            self.emit("DELETE_ATTR", self.mangle(node.attr))
        else:
            self.emit("LOAD_ATTR", self.mangle(node.attr))

    # next five implement assignments

    def visitAssign(self, node):
        self.set_lineno(node)
        self.visit(node.value)
        dups = len(node.targets) - 1
        for i in range(len(node.targets)):
            elt = node.targets[i]
            if i < dups:
                self.emit("DUP_TOP")
            if isinstance(elt, ast.AST):
                self.visit(elt)

    def checkAnnExpr(self, node):
        self.visit(node)
        self.emit("POP_TOP")

    def checkAnnSlice(self, node):
        if isinstance(node, ast.Index):
            self.checkAnnExpr(node.value)
        else:
            if node.lower:
                self.checkAnnExpr(node.lower)
            if node.upper:
                self.checkAnnExpr(node.upper)
            if node.step:
                self.checkAnnExpr(node.step)

    def checkAnnSubscr(self, node):
        if isinstance(node, (ast.Index, ast.Slice)):
            self.checkAnnSlice(node)
        elif isinstance(node, ast.ExtSlice):
            for v in node.dims:
                self.checkAnnSlice(v)

    def checkAnnotation(self, node):
        if isinstance(self.tree, (ast.Module, ast.ClassDef)):
            self.checkAnnExpr(node.annotation)

    def findAnn(self, stmts):
        for stmt in stmts:
            if isinstance(stmt, (ast.ClassDef, ast.FunctionDef, ast.AsyncFunctionDef)):
                # Don't recurse into definitions looking for annotations
                continue
            elif isinstance(stmt, ast.AnnAssign):
                return True
            elif isinstance(stmt, (ast.stmt, ast.ExceptHandler)):
                for field in stmt._fields:
                    child = getattr(stmt, field)
                    if isinstance(child, list):
                        if self.findAnn(child):
                            return True

        return False

    def emitStoreAnnotation(self, name: str, annotation: ast.expr):
        assert self.did_setup_annotations

        self._visitAnnotation(annotation)
        self.emit("LOAD_NAME", "__annotations__")
        mangled = self.mangle(name)
        self.emit("LOAD_CONST", mangled)
        self.emit("STORE_SUBSCR")

    def visitAnnAssign(self, node):
        self.set_lineno(node)
        if node.value:
            self.visit(node.value)
            self.visit(node.target)
        if isinstance(node.target, ast.Name):
            # If we have a simple name in a module or class, store the annotation
            if node.simple and isinstance(self.tree, (ast.Module, ast.ClassDef)):
                self.emitStoreAnnotation(node.target.id, node.annotation)
        elif isinstance(node.target, ast.Attribute):
            if not node.value:
                self.checkAnnExpr(node.target.value)
        elif isinstance(node.target, ast.Subscript):
            if not node.value:
                self.checkAnnExpr(node.target.value)
                self.checkAnnSubscr(node.target.slice)
        else:
            raise SystemError(
                f"invalid node type {type(node).__name__} for annotated assignment"
            )

        if not node.simple:
            self.checkAnnotation(node)

    def visitAssName(self, node):
        if node.flags == "OP_ASSIGN":
            self.storeName(node.name)
        elif node.flags == "OP_DELETE":
            self.set_lineno(node)
            self.delName(node.name)
        else:
            print("oops", node.flags)
            assert 0

    def visitAssAttr(self, node):
        self.visit(node.expr)
        if node.flags == "OP_ASSIGN":
            self.emit("STORE_ATTR", self.mangle(node.attrname))
        elif node.flags == "OP_DELETE":
            self.emit("DELETE_ATTR", self.mangle(node.attrname))
        else:
            print("warning: unexpected flags:", node.flags)
            print(node)
            assert 0

    def _visitAssSequence(self, node, op="UNPACK_SEQUENCE"):
        if findOp(node) != "OP_DELETE":
            self.emit(op, len(node.nodes))
        for child in node.nodes:
            self.visit(child)

    visitAssTuple = _visitAssSequence
    visitAssList = _visitAssSequence

    # augmented assignment

    def visitAugAssign(self, node):
        self.set_lineno(node)
        if isinstance(node.target, ast.Attribute):
            self.emitAugAttribute(node)
        elif isinstance(node.target, ast.Name):
            self.emitAugName(node)
        else:
            self.emitAugSubscript(node)

    _augmented_opcode = {
        ast.Add: "INPLACE_ADD",
        ast.Sub: "INPLACE_SUBTRACT",
        ast.Mult: "INPLACE_MULTIPLY",
        ast.MatMult: "INPLACE_MATRIX_MULTIPLY",
        ast.Div: "INPLACE_TRUE_DIVIDE",
        ast.FloorDiv: "INPLACE_FLOOR_DIVIDE",
        ast.Mod: "INPLACE_MODULO",
        ast.Pow: "INPLACE_POWER",
        ast.RShift: "INPLACE_RSHIFT",
        ast.LShift: "INPLACE_LSHIFT",
        ast.BitAnd: "INPLACE_AND",
        ast.BitXor: "INPLACE_XOR",
        ast.BitOr: "INPLACE_OR",
    }

    def emitAugRHS(self, node):
        self.visit(node.value)
        self.emit(self._augmented_opcode[type(node.op)])

    def emitAugName(self, node):
        target = node.target
        self.loadName(target.id)
        self.emitAugRHS(node)
        self.storeName(target.id)

    def emitAugAttribute(self, node):
        target = node.target
        self.visit(target.value)
        self.emit("DUP_TOP")
        self.emit("LOAD_ATTR", self.mangle(target.attr))
        self.emitAugRHS(node)
        self.emit("ROT_TWO")
        self.emit("STORE_ATTR", self.mangle(target.attr))

    def emitAugSubscript(self, node):
        self.visitSubscript(node.target, 1)
        self.emitAugRHS(node)
        self.emit("ROT_THREE")
        self.emit("STORE_SUBSCR")

    def visitExec(self, node):
        self.visit(node.expr)
        if node.locals is None:
            self.emit("LOAD_CONST", None)
        else:
            self.visit(node.locals)
        if node.globals is None:
            self.emit("DUP_TOP")
        else:
            self.visit(node.globals)
        self.emit("EXEC_STMT")

    def compiler_subkwargs(self, kwargs, begin, end):
        nkwargs = end - begin
        if nkwargs > 1:
            for i in range(begin, end):
                self.visit(kwargs[i].value)
            self.emit("LOAD_CONST", tuple(arg.arg for arg in kwargs[begin:end]))
            self.emit("BUILD_CONST_KEY_MAP", nkwargs)
        else:
            for i in range(begin, end):
                self.emit("LOAD_CONST", kwargs[i].arg)
                self.visit(kwargs[i].value)
            self.emit("BUILD_MAP", nkwargs)

    def _call_helper(self, argcnt, args, kwargs):
        mustdictunpack = any(arg.arg is None for arg in kwargs)
        nelts = len(args)
        nkwelts = len(kwargs)
        # the number of tuples and dictionaries on the stack
        nsubkwargs = nsubargs = 0
        nseen = argcnt  # the number of positional arguments on the stack
        for arg in args:
            if isinstance(arg, ast.Starred):
                if nseen:
                    self.emit("BUILD_TUPLE", nseen)
                    nseen = 0
                    nsubargs += 1
                self.visit(arg.value)
                nsubargs += 1
            else:
                self.visit(arg)
                nseen += 1

        if nsubargs or mustdictunpack:
            if nseen:
                self.emit("BUILD_TUPLE", nseen)
                nsubargs += 1
            if nsubargs > 1:
                self.emit("BUILD_TUPLE_UNPACK_WITH_CALL", nsubargs)
            elif nsubargs == 0:
                self.emit("BUILD_TUPLE", 0)

            nseen = 0  # the number of keyword arguments on the stack following
            for i, kw in enumerate(kwargs):
                if kw.arg is None:
                    if nseen:
                        # A keyword argument unpacking.
                        self.compiler_subkwargs(kwargs, i - nseen, i)
                        nsubkwargs += 1
                        nseen = 0
                    self.visit(kw.value)
                    nsubkwargs += 1
                else:
                    nseen += 1
            if nseen:
                self.compiler_subkwargs(kwargs, nkwelts - nseen, nkwelts)
                nsubkwargs += 1
            if nsubkwargs > 1:
                self.emit("BUILD_MAP_UNPACK_WITH_CALL", nsubkwargs)
            self.emit("CALL_FUNCTION_EX", int(nsubkwargs > 0))
        elif nkwelts:
            for kw in kwargs:
                self.visit(kw.value)
            self.emit("LOAD_CONST", tuple(arg.arg for arg in kwargs))
            self.emit("CALL_FUNCTION_KW", nelts + nkwelts + argcnt)
        else:
            self.emit("CALL_FUNCTION", nelts + argcnt)

    def visitCall(self, node):
        if (
            node.keywords
            or not isinstance(node.func, ast.Attribute)
            or not isinstance(node.func.ctx, ast.Load)
            or any(isinstance(arg, ast.Starred) for arg in node.args)
        ):
            # We cannot optimize this call
            self.visit(node.func)
            self._call_helper(0, node.args, node.keywords)
            return

        self.update_lineno(node)
        self.visit(node.func.value)
        self.emit("LOAD_METHOD", self.mangle(node.func.attr))
        for arg in node.args:
            self.visit(arg)
        self.emit("CALL_METHOD", len(node.args))

    def visitPrint(self, node, newline=0):
        self.set_lineno(node)
        if node.dest:
            self.visit(node.dest)
        for child in node.nodes:
            if node.dest:
                self.emit("DUP_TOP")
            self.visit(child)
            if node.dest:
                self.emit("ROT_TWO")
                self.emit("PRINT_ITEM_TO")
            else:
                self.emit("PRINT_ITEM")
        if node.dest and not newline:
            self.emit("POP_TOP")

    def visitPrintnl(self, node):
        self.visitPrint(node, newline=1)
        if node.dest:
            self.emit("PRINT_NEWLINE_TO")
        else:
            self.emit("PRINT_NEWLINE")

    def checkReturn(self, node):
        if not isinstance(self.tree, (ast.FunctionDef, ast.AsyncFunctionDef)):
            raise SyntaxError(
                "'return' outside function", self.syntax_error_position(node)
            )
        elif self.scope.coroutine and self.scope.generator and node.value:
            raise SyntaxError(
                "'return' with value in async generator",
                self.syntax_error_position(node),
            )

    def visitReturn(self, node):
        self.checkReturn(node)

        self.set_lineno(node)

        preserve_tos = bool(node.value and not isinstance(node.value, ast.Constant))
        if preserve_tos:
            self.visit(node.value)
        self.unwind_setup_entries(preserve_tos)
        if not node.value:
            self.emit("LOAD_CONST", None)
        elif not preserve_tos:
            self.visit(node.value)

        self.emit("RETURN_VALUE")

    def visitYield(self, node):
        if not isinstance(
            self.tree,
            (ast.FunctionDef, ast.AsyncFunctionDef, ast.Lambda, ast.GeneratorExp),
        ):
            raise SyntaxError(
                "'yield' outside function", self.syntax_error_position(node)
            )
        self.update_lineno(node)
        if node.value:
            self.visit(node.value)
        else:
            self.emit("LOAD_CONST", None)
        self.emit("YIELD_VALUE")

    def visitYieldFrom(self, node):
        if not isinstance(
            self.tree,
            (ast.FunctionDef, ast.AsyncFunctionDef, ast.Lambda, ast.GeneratorExp),
        ):
            raise SyntaxError(
                "'yield' outside function", self.syntax_error_position(node)
            )
        elif self.scope.coroutine:
            raise SyntaxError(
                "'yield from' inside async function", self.syntax_error_position(node)
            )

        self.update_lineno(node)
        self.visit(node.value)
        self.emit("GET_YIELD_FROM_ITER")
        self.emit("LOAD_CONST", None)
        self.emit("YIELD_FROM")

    def visitAwait(self, node):
        self.update_lineno(node)
        self.visit(node.value)
        self.emit("GET_AWAITABLE")
        self.emit("LOAD_CONST", None)
        self.emit("YIELD_FROM")

    # slice and subscript stuff
    def visitSubscript(self, node, aug_flag=None):
        self.update_lineno(node)
        self.visit(node.value)
        self.visit(node.slice)
        if isinstance(node.ctx, ast.Load):
            self.emit("BINARY_SUBSCR")
        elif isinstance(node.ctx, ast.Store):
            if aug_flag:
                self.emit("DUP_TOP_TWO")
                self.emit("BINARY_SUBSCR")
            else:
                self.emit("STORE_SUBSCR")
        elif isinstance(node.ctx, ast.Del):
            self.emit("DELETE_SUBSCR")
        else:
            assert 0

    # binary ops

    def binaryOp(self, node, op):
        self.visit(node.left)
        self.visit(node.right)
        self.emit(op)

    _binary_opcode = {
        ast.Add: "BINARY_ADD",
        ast.Sub: "BINARY_SUBTRACT",
        ast.Mult: "BINARY_MULTIPLY",
        ast.MatMult: "BINARY_MATRIX_MULTIPLY",
        ast.Div: "BINARY_TRUE_DIVIDE",
        ast.FloorDiv: "BINARY_FLOOR_DIVIDE",
        ast.Mod: "BINARY_MODULO",
        ast.Pow: "BINARY_POWER",
        ast.LShift: "BINARY_LSHIFT",
        ast.RShift: "BINARY_RSHIFT",
        ast.BitOr: "BINARY_OR",
        ast.BitXor: "BINARY_XOR",
        ast.BitAnd: "BINARY_AND",
    }

    def visitBinOp(self, node):
        self.update_lineno(node)
        self.visit(node.left)
        self.visit(node.right)
        op = self._binary_opcode[type(node.op)]
        self.emit(op)

    # unary ops

    def unaryOp(self, node, op):
        self.visit(node.operand)
        self.emit(op)

    _unary_opcode = {
        ast.Invert: "UNARY_INVERT",
        ast.USub: "UNARY_NEGATIVE",
        ast.UAdd: "UNARY_POSITIVE",
        ast.Not: "UNARY_NOT",
    }

    def visitUnaryOp(self, node):
        self.update_lineno(node)
        self.unaryOp(node, self._unary_opcode[type(node.op)])

    def visitBackquote(self, node):
        return self.unaryOp(node, "UNARY_CONVERT")

    # object constructors

    def visitEllipsis(self, node):
        self.update_lineno(node)
        self.emit("LOAD_CONST", Ellipsis)

    def _visitUnpack(self, node):
        before = 0
        after = 0
        starred = None
        for elt in node.elts:
            if isinstance(elt, ast.Starred):
                if starred is not None:
                    raise SyntaxError(
                        "two starred expressions in assignment",
                        self.syntax_error_position(elt),
                    )
                elif before >= 256 or len(node.elts) - before - 1 >= (1 << 31) >> 8:
                    raise SyntaxError(
                        "too many expressions in star-unpacking assignment",
                        self.syntax_error_position(elt),
                    )
                starred = elt.value
            elif starred:
                after += 1
            else:
                before += 1
        if starred:
            self.emit("UNPACK_EX", after << 8 | before)
        else:
            self.emit("UNPACK_SEQUENCE", before)

    def hasStarred(self, elts):
        for elt in elts:
            if isinstance(elt, ast.Starred):
                return True
        return False

    def _visitSequence(self, node, build_op, build_inner_op, build_ex_op, ctx):
        self.update_lineno(node)
        if isinstance(ctx, ast.Store):
            self._visitUnpack(node)
            starred_load = False
        else:
            starred_load = self.hasStarred(node.elts)

        chunks = 0
        in_chunk = 0

        def out_chunk():
            nonlocal chunks, in_chunk
            if in_chunk:
                self.emit(build_inner_op, in_chunk)
                in_chunk = 0
                chunks += 1

        for elt in node.elts:
            if starred_load:
                if isinstance(elt, ast.Starred):
                    out_chunk()
                    chunks += 1
                else:
                    in_chunk += 1

            if isinstance(elt, ast.Starred):
                self.visit(elt.value)
            else:
                self.visit(elt)
        # Output trailing chunk, if any
        out_chunk()

        if isinstance(ctx, ast.Load):
            if starred_load:
                self.emit(build_ex_op, chunks)
            else:
                self.emit(build_op, len(node.elts))

    def visitStarred(self, node):
        if isinstance(node.ctx, ast.Store):
            raise SyntaxError(
                "starred assignment target must be in a list or tuple",
                self.syntax_error_position(node),
            )
        else:
            raise SyntaxError(
                "can't use starred expression here", self.syntax_error_position(node)
            )

    def visitTuple(self, node):
        self._visitSequence(
            node, "BUILD_TUPLE", "BUILD_TUPLE", "BUILD_TUPLE_UNPACK", node.ctx
        )

    def visitList(self, node):
        self._visitSequence(
            node, "BUILD_LIST", "BUILD_TUPLE", "BUILD_LIST_UNPACK", node.ctx
        )

    def visitSet(self, node):
        self._visitSequence(
            node, "BUILD_SET", "BUILD_SET", "BUILD_SET_UNPACK", ast.Load()
        )

    def visitSlice(self, node):
        num = 2
        if node.lower:
            self.visit(node.lower)
        else:
            self.emit("LOAD_CONST", None)
        if node.upper:
            self.visit(node.upper)
        else:
            self.emit("LOAD_CONST", None)
        if node.step:
            self.visit(node.step)
            num += 1
        self.emit("BUILD_SLICE", num)

    def visitExtSlice(self, node):
        for d in node.dims:
            self.visit(d)
        self.emit("BUILD_TUPLE", len(node.dims))

    def visitNamedExpr(self, node: ast.NamedExpr):
        self.update_lineno(node)
        self.visit(node.value)
        self.emit("DUP_TOP")
        self.visit(node.target)

    # Create dict item by item. Saves interp stack size at the expense
    # of bytecode size/speed.
    def visitDict_by_one(self, node):
        self.update_lineno(node)
        self.emit("BUILD_MAP", 0)
        for k, v in zip(node.keys, node.values):
            self.emit("DUP_TOP")
            self.visit(k)
            self.visit(v)
            self.emit("ROT_THREE")
            self.emit("STORE_SUBSCR")

    def _const_value(self, node):
        if isinstance(node, (ast.NameConstant, ast.Constant)):
            return node.value
        elif isinstance(node, ast.Num):
            return node.n
        elif isinstance(node, (ast.Str, ast.Bytes)):
            return node.s
        elif isinstance(node, ast.Ellipsis):
            return ...
        else:
            assert isinstance(node, ast.Name) and node.id == "__debug__"
            return not self.optimized

    def get_bool_const(self, node):
        """Return True if node represent constantly true value, False if
        constantly false value, and None otherwise (non-constant)."""
        if isinstance(node, ast.Num):
            return bool(node.n)
        if isinstance(node, ast.NameConstant):
            return bool(node.value)
        if isinstance(node, ast.Str):
            return bool(node.s)
        if isinstance(node, ast.Name):
            if node.id == "__debug__":
                return not bool(self.optimization_lvl)
        if isinstance(node, ast.Constant):
            return bool(node.value)

    def compile_subdict(self, node, begin, end):
        n = end - begin
        if n > 1 and all_items_const(node.keys, begin, end):
            for i in range(begin, end):
                self.visit(node.values[i])

            self.emit(
                "LOAD_CONST", tuple(self._const_value(x) for x in node.keys[begin:end])
            )
            self.emit("BUILD_CONST_KEY_MAP", n)
        else:
            for i in range(begin, end):
                self.visit(node.keys[i])
                self.visit(node.values[i])

            self.emit("BUILD_MAP", n)

    def visitDict(self, node):
        self.update_lineno(node)
        containers = elements = 0
        is_unpacking = False

        for i, (k, v) in enumerate(zip(node.keys, node.values)):
            is_unpacking = k is None
            if elements == 0xFFFF or (elements and is_unpacking):
                self.compile_subdict(node, i - elements, i)
                containers += 1
                elements = 0

            if is_unpacking:
                self.visit(v)
                containers += 1
            else:
                elements += 1

        if elements or containers == 0:
            self.compile_subdict(node, len(node.keys) - elements, len(node.keys))
            containers += 1

        if containers > 1 or is_unpacking:
            self.emit("BUILD_MAP_UNPACK", containers)

    def unwind_setup_entry(self, e: Entry, preserve_tos: int) -> None:
        if e.kind == WHILE_LOOP:
            return
        if e.kind == END_FINALLY:
            e.exit = None
            self.emit("POP_FINALLY", preserve_tos)
            if preserve_tos:
                self.emit("ROT_TWO")
            self.emit("POP_TOP")
        elif e.kind == FOR_LOOP:
            if preserve_tos:
                self.emit("ROT_TWO")
            self.emit("POP_TOP")
        elif e.kind == EXCEPT:
            self.emit("POP_BLOCK")
        elif e.kind == TRY_FINALLY:
            self.emit("POP_BLOCK")
            self.emit("CALL_FINALLY", e.exit)
        elif e.kind == TRY_FINALLY_BREAK:
            self.emit("POP_BLOCK")
            if preserve_tos:
                self.emit("ROT_TWO")
                self.emit("POP_TOP")
                self.emit("CALL_FINALLY", e.exit)
            else:
                self.emit("CALL_FINALLY", e.exit)
                self.emit("POP_TOP")
        elif e.kind in (WITH, ASYNC_WITH):
            self.emit("POP_BLOCK")
            if preserve_tos:
                self.emit("ROT_TWO")
            self.emit("BEGIN_FINALLY")
            self.emit("WITH_CLEANUP_START")
            if e.kind == ASYNC_WITH:
                self.emit("GET_AWAITABLE")
                self.emit("LOAD_CONST", None)
                self.emit("YIELD_FROM")
            self.emit("WITH_CLEANUP_FINISH")
            self.emit("POP_FINALLY", 0)
        elif e.kind == HANDLER_CLEANUP:
            if preserve_tos:
                self.emit("ROT_FOUR")
            if e.exit:
                self.emit("POP_BLOCK")
                self.emit("POP_EXCEPT")
                self.emit("CALL_FINALLY", e.exit)
            else:
                self.emit("POP_EXCEPT")
        else:
            raise Exception(f"Unexpected kind {e.kind}")

    def unwind_setup_entries(self, preserve_tos: bool) -> None:
        for e in reversed(self.setups):
            self.unwind_setup_entry(e, preserve_tos)

    @property
    def name(self):
        if isinstance(self.tree, (ast.FunctionDef, ast.ClassDef, ast.AsyncFunctionDef)):
            return self.tree.name
        elif isinstance(self.tree, ast.SetComp):
            return "<setcomp>"
        elif isinstance(self.tree, ast.ListComp):
            return "<listcomp>"
        elif isinstance(self.tree, ast.DictComp):
            return "<dictcomp>"
        elif isinstance(self.tree, ast.GeneratorExp):
            return "<genexpr>"
        elif isinstance(self.tree, ast.Lambda):
            return "<lambda>"

    def finishFunction(self):
        if self.graph.current.returns:
            return
        self.graph.startExitBlock()
        if not isinstance(self.tree, ast.Lambda):
            self.emit("LOAD_CONST", None)
        self.emit("RETURN_VALUE")

    def make_child_codegen(
        self,
        tree: FuncOrLambda | CompNode | ast.ClassDef,
        graph: PyFlowGraph,
        codegen_type: Optional[Type[CodeGenerator]] = None,
    ) -> CodeGenerator:
        if codegen_type is None:
            codegen_type = type(self)
        return codegen_type(
            self,
            tree,
            self.symbols,
            graph,
            flags=self.flags,
            optimization_lvl=self.optimization_lvl,
        )

    def make_func_codegen(
        self,
        func: FuncOrLambda | CompNode,
        func_args: ast.arguments,
        name: str,
        first_lineno: int,
    ) -> CodeGenerator:
        filename = self.graph.filename
        symbols = self.symbols
        class_name = self.class_name

        graph = self.make_function_graph(
            func, func_args, filename, symbols.scopes, class_name, name, first_lineno
        )
        res = self.make_child_codegen(func, graph)
        res.optimized = 1
        res.class_name = class_name
        return res

    def make_class_codegen(
        self, klass: ast.ClassDef, first_lineno: int
    ) -> CodeGenerator:
        filename = self.graph.filename
        symbols = self.symbols

        scope = symbols.scopes[klass]
        graph = self.flow_graph(
            klass.name,
            filename,
            scope,
            optimized=0,
            klass=True,
            docstring=self.get_docstring(klass),
            firstline=first_lineno,
            peephole_enabled=self.graph.peephole_enabled,
        )

        res = self.make_child_codegen(klass, graph)
        res.class_name = klass.name
        return res

    def make_function_graph(
        self,
        func: FuncOrLambda | CompNode,
        func_args: ast.arguments,
        filename: str,
        scopes,
        class_name: str,
        name: str,
        first_lineno: int,
    ) -> PyFlowGraph:
        args = [
            misc.mangle(elt.arg, class_name)
            for elt in itertools.chain(func_args.posonlyargs, func_args.args)
        ]
        kwonlyargs = [misc.mangle(elt.arg, class_name) for elt in func_args.kwonlyargs]

        starargs = []
        if va := func_args.vararg:
            starargs.append(va.arg)
        if kw := func_args.kwarg:
            starargs.append(kw.arg)

        scope = scopes[func]
        graph = self.flow_graph(
            name,
            filename,
            scope,
            flags=self.get_graph_flags(func, func_args, scope),
            args=args,
            kwonlyargs=kwonlyargs,
            starargs=starargs,
            optimized=1,
            docstring=self.get_docstring(func),
            firstline=first_lineno,
            peephole_enabled=self.graph.peephole_enabled,
            posonlyargs=len(func_args.posonlyargs),
        )

        return graph

    def get_docstring(
        self, func: ast.Module | ast.ClassDef | FuncOrLambda | CompNode
    ) -> Optional[str]:
        doc = None
        if (
            not isinstance(
                func,
                (ast.Lambda, ast.ListComp, ast.SetComp, ast.DictComp, ast.GeneratorExp),
            )
            and not self.strip_docstrings
        ):
            doc = get_docstring(func)
        return doc

    def get_graph_flags(
        self, func: FuncOrLambda | CompNode, func_args: ast.arguments, scope: Scope
    ) -> int:
        flags = 0

        if func_args.vararg:
            flags = flags | CO_VARARGS
        if func_args.kwarg:
            flags = flags | CO_VARKEYWORDS
        if scope.nested:
            flags = flags | CO_NESTED
        if scope.generator and not scope.coroutine:
            flags = flags | CO_GENERATOR
        if not scope.generator and scope.coroutine:
            flags = flags | CO_COROUTINE
        if scope.generator and scope.coroutine:
            flags = flags | CO_ASYNC_GENERATOR

        return flags

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
    ):
        if ast_optimizer_enabled:
            tree = cls.optimize_tree(optimize, tree)
        s = cls._SymbolVisitor()
        walk(tree, s)

        graph = cls.flow_graph(
            module_name, filename, s.scopes[tree], peephole_enabled=peephole_enabled
        )
        code_gen = cls(None, tree, s, graph, flags, optimize)
        code_gen._qual_name = module_name
        walk(tree, code_gen)
        return code_gen

    @classmethod
    def optimize_tree(cls, optimize: int, tree: AST):
        return AstOptimizer(optimize=optimize > 0).visit(tree)

    def visit(self, node: Union[Sequence[AST], AST], *args):
        # Note down the old line number
        could_be_multiline_expr = isinstance(node, ast.expr)
        old_lineno = self.graph.lineno if could_be_multiline_expr else None

        ret = super().visit(node, *args)

        if old_lineno is not None and old_lineno != self.graph.lineno:
            self.graph.lineno = old_lineno
            self.graph.lineno_set = False

        return ret


class Entry:
    kind: int
    block: pyassem.Block
    exit: Optional[pyassem.Block]

    def __init__(self, kind, block, exit):
        self.kind = kind
        self.block = block
        self.exit = exit


class CinderCodeGenerator(CodeGenerator):
    flow_graph = pyassem.PyFlowGraphCinder
    _SymbolVisitor = symbols.CinderSymbolVisitor

    def set_qual_name(self, qualname):
        self._qual_name = qualname

    def getCode(self):
        code = super().getCode()
        from cinder import _set_qualname

        _set_qualname(code, self._qual_name)

        return code

    def _nameOp(self, prefix, name) -> None:
        if (
            prefix == "LOAD"
            and name == "super"
            and isinstance(self.scope, symbols.FunctionScope)
        ):
            scope = self.scope.check_name(name)
            if scope in (SC_GLOBAL_EXPLICIT, SC_GLOBAL_IMPLICIT):
                self.scope.suppress_jit = True
        super()._nameOp(prefix, name)

    def _is_super_call(self, node):
        if (
            not isinstance(node, ast.Call)
            or not isinstance(node.func, ast.Name)
            or node.func.id != "super"
            or node.keywords
        ):
            return False

        # check that 'super' only appear as implicit global:
        # it is not defined in local or modules scope
        if self.scope.check_name("super") != SC_GLOBAL_IMPLICIT or (
            self.module_gen.scope.check_name("super") != SC_GLOBAL_IMPLICIT
            and self.module_gen.scope.check_name("super") != SC_LOCAL
        ):
            return False

        if len(node.args) == 2:
            return True
        if len(node.args) == 0:
            if len(self.scope.params) == 0:
                return False
            return self.scope.check_name("__class__") == SC_FREE

        return False

    def _emit_args_for_super(self, super_call, attr):
        if len(super_call.args) == 0:
            self.loadName("__class__")
            self.loadName(next(iter(self.scope.params)))
        else:
            for arg in super_call.args:
                self.visit(arg)
        return (self.mangle(attr), len(super_call.args) == 0)

    def visitAttribute(self, node):
        if isinstance(node.ctx, ast.Load) and self._is_super_call(node.value):
            self.emit("LOAD_GLOBAL", "super")
            load_arg = self._emit_args_for_super(node.value, node.attr)
            self.emit("LOAD_ATTR_SUPER", load_arg)
        else:
            super().visitAttribute(node)

    def visitCall(self, node):
        self.update_lineno(node)
        if (
            not isinstance(node.func, ast.Attribute)
            or not isinstance(node.func.ctx, ast.Load)
            or node.keywords
            or any(isinstance(arg, ast.Starred) for arg in node.args)
        ):
            # We cannot optimize this call
            return super().visitCall(node)

        if self._is_super_call(node.func.value):
            self.emit("LOAD_GLOBAL", "super")
            load_arg = self._emit_args_for_super(node.func.value, node.func.attr)
            self.emit("LOAD_METHOD_SUPER", load_arg)
            for arg in node.args:
                self.visit(arg)
            self.emit("CALL_METHOD", len(node.args))
            return

        self.visit(node.func.value)
        self.emit("LOAD_METHOD", self.mangle(node.func.attr))
        for arg in node.args:
            self.visit(arg)

        self.emit("CALL_METHOD", len(node.args))

    def findFutures(self, node):
        future_flags = super().findFutures(node)
        for feature in future.find_futures(node):
            if feature == "eager_imports":
                future_flags |= consts.CO_FUTURE_EAGER_IMPORTS
            elif feature == "lazy_imports":
                future_flags |= consts.CO_FUTURE_LAZY_IMPORTS
        return future_flags

    def compile_comprehension(self, node, name, elt, val, opcode, oparg=0):
        self.update_lineno(node)
        # fetch the scope that correspond to comprehension
        scope = self.scopes[node]
        if scope.inlined:
            # for inlined comprehension process with current generator
            gen = self
        else:
            gen = self.make_func_codegen(
                node, self.conjure_arguments([ast.arg(".0", None)]), name, node.lineno
            )

        if opcode:
            gen.emit(opcode, oparg)

        gen.compile_comprehension_generator(
            node.generators, 0, elt, val, type(node), not scope.inlined
        )

        if scope.inlined:
            # collect list of defs that were introduced by comprehension
            # note that we need to exclude:
            # - .0 parameter since it is used
            # - non-local names (typically named expressions), they are
            #   defined in enclosing scope and thus should not be deleted
            to_delete = [
                v
                for v in scope.defs
                if v != ".0" and v not in scope.nonlocals and v not in scope.cells
            ]
            # sort names to have deterministic deletion order
            to_delete.sort()
            for v in to_delete:
                self.delName(v)
            return

        if not isinstance(node, ast.GeneratorExp):
            gen.emit("RETURN_VALUE")

        gen.finishFunction()

        self._makeClosure(gen, 0)

        # precomputation of outmost iterable
        self.visit(node.generators[0].iter)
        if node.generators[0].is_async:
            self.emit("GET_AITER")
        else:
            self.emit("GET_ITER")
        self.emit("CALL_FUNCTION", 1)

        if gen.scope.coroutine and type(node) is not ast.GeneratorExp:
            self.emit("GET_AWAITABLE")
            self.emit("LOAD_CONST", None)
            self.emit("YIELD_FROM")

    def annotate_arg(self, arg: ast.arg, ann_args: List[str]):
        if arg.annotation:
            name = self.mangle(arg.arg)
            self.emit("LOAD_CONST", name)
            self._visitAnnotation(arg.annotation)
            ann_args.append(name)

    def build_annotations(
        self, node: ast.FunctionDef | ast.AsyncFunctionDef | ast.Lambda
    ) -> bool:
        ann_args = self.annotate_args(node.args)
        # Cannot annotate return type for lambda
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)) and node.returns:
            self.emit("LOAD_CONST", "return")
            self._visitAnnotation(node.returns)
            ann_args.append("return")
        if ann_args:
            self.emit("BUILD_TUPLE", len(ann_args) * 2)
            return True

        return False


def get_default_generator():

    if "cinder" in sys.version:
        return CinderCodeGenerator

    return CodeGenerator


def get_docstring(
    node: ast.Module | ast.ClassDef | ast.FunctionDef | ast.AsyncFunctionDef,
) -> str | None:
    if (
        node.body
        and (b0 := node.body[0])
        and isinstance(b0, ast.Expr)
        and (b0v := b0.value)
        and isinstance(b0v, ast.Str)
    ):
        return b0v.s


def findOp(node):
    """Find the op (DELETE, LOAD, STORE) in an AssTuple tree"""
    v = OpFinder()
    v.VERBOSE = 0
    walk(node, v)
    return v.op


class OpFinder:
    def __init__(self):
        self.op = None

    def visitAssName(self, node):
        if self.op is None:
            self.op = node.flags
        elif self.op != node.flags:
            raise ValueError("mixed ops in stmt")

    visitAssAttr = visitAssName
    visitSubscript = visitAssName


PythonCodeGenerator = get_default_generator()


if __name__ == "__main__":
    for file in sys.argv[1:]:
        compileFile(file)
