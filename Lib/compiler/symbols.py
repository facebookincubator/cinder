# pyre-unsafe
"""Module symbol-table generator"""
from __future__ import print_function

import ast
import sys

from .consts import (
    SC_CELL,
    SC_FREE,
    SC_GLOBAL_EXPLICIT,
    SC_GLOBAL_IMPLICIT,
    SC_LOCAL,
    SC_UNKNOWN,
)
from .misc import mangle
from .visitor import ASTVisitor

if sys.version_info[0] >= 3:
    long = int

MANGLE_LEN = 256

DEF_NORMAL = 1
DEF_COMP_ITER = 2


class Scope:
    # XXX how much information do I need about each name?
    def __init__(self, name, module, klass=None, lineno=0):
        self.name = name
        self.module = module
        self.lineno = lineno
        self.defs = {}
        self.uses = {}
        self.globals = {}
        self.explicit_globals = {}
        self.nonlocals = {}
        self.params = {}
        self.frees = {}
        self.cells = {}
        self.children = []
        self.parent = None
        self.coroutine = False
        self.comp_iter_target = self.comp_iter_expr = 0
        # nested is true if the class could contain free variables,
        # i.e. if it is nested within another function.
        self.nested = None
        # It's possible to define a scope (class, function) at the nested level,
        # but explicitly mark it as global. Bytecode-wise, this is handled
        # automagically, but we need to generate proper __qualname__ for these.
        self.global_scope = False
        self.generator = False
        self.klass = None
        self.suppress_jit = False
        if klass is not None:
            for i in range(len(klass)):
                if klass[i] != "_":
                    self.klass = klass[i:]
                    break

    def __repr__(self):
        return "<%s: %s>" % (self.__class__.__name__, self.name)

    def mangle(self, name):
        if self.klass is None:
            return name
        return mangle(name, self.klass)

    def add_def(self, name, kind=DEF_NORMAL):
        mangled = self.mangle(name)
        self.defs[mangled] = kind | self.defs.get(mangled, 1)

    def add_use(self, name):
        self.uses[self.mangle(name)] = 1

    def add_global(self, name):
        name = self.mangle(name)
        if name in self.uses or name in self.defs:
            pass  # XXX warn about global following def/use
        if name in self.params:
            raise SyntaxError("%s in %s is global and parameter" % (name, self.name))
        self.explicit_globals[name] = 1
        self.module.add_def(name)
        # Seems to be behavior of Py3.5, "global foo" sets foo as
        # explicit global for module too
        self.module.explicit_globals[name] = 1

    def add_free(self, name):
        self.add_frees([name])

    def add_param(self, name):
        name = self.mangle(name)
        self.defs[name] = 1
        self.params[name] = 1

    def get_names(self):
        d = {}
        d.update(self.defs)
        d.update(self.uses)
        d.update(self.globals)
        return d.keys()

    def add_child(self, child):
        self.children.append(child)
        child.parent = self

    def get_children(self):
        return self.children

    def DEBUG(self):
        print(self.name, self.nested and "nested" or "")
        print("\tglobals: ", self.globals)
        print("\texplicit_globals: ", self.explicit_globals)
        print("\tcells: ", self.cells)
        print("\tdefs: ", self.defs)
        print("\tuses: ", self.uses)
        print("\tfrees:", self.frees)

    def check_name(self, name):
        """Return scope of name.

        The scope of a name could be LOCAL, GLOBAL, FREE, or CELL.
        """
        if name in self.explicit_globals:
            return SC_GLOBAL_EXPLICIT
        if name in self.globals:
            return SC_GLOBAL_IMPLICIT
        if name in self.cells:
            return SC_CELL
        if name in self.frees:
            return SC_FREE
        if name in self.defs:
            return SC_LOCAL
        if self.nested and name in self.uses:
            return SC_FREE
        if self.nested:
            return SC_UNKNOWN
        else:
            return SC_GLOBAL_IMPLICIT

    def get_free_vars(self):
        if not self.nested:
            # If we're not nested we can't possibly have any free variables,
            # as we can't close over class variables.  The exception to this
            # rule is __class__, which we indeed can close over.
            if "__class__" in self.frees:
                return ["__class__"]
            return []

        free = {}
        free.update(self.frees)
        for name in self.uses.keys():
            if name not in self.defs and name not in self.globals:
                free[name] = 1
        return sorted(free.keys())

    def handle_children(self):
        for child in self.children:
            if child.name in self.explicit_globals:
                child.global_scope = True
            if child.nested:
                frees = child.get_free_vars()

                globals = self.add_frees(frees)
                for name in globals:
                    child.force_global(name)
            elif "__class__" in child.frees:
                self.add_frees(["__class__"])
            elif "__class__" in child.uses and "__class__" not in child.defs:
                child.frees = {"__class__"}
                self.add_frees(["__class__"])

    def force_global(self, name):
        """Force name to be global in scope.

        Some child of the current node had a free reference to name.
        When the child was processed, it was labelled a free
        variable.  Now that all its enclosing scope have been
        processed, the name is known to be a global or builtin.  So
        walk back down the child chain and set the name to be global
        rather than free.

        Be careful to stop if a child does not think the name is
        free.
        """
        self.globals[name] = 1
        if name in self.frees:
            del self.frees[name]
        for child in self.children:
            if child.check_name(name) == SC_FREE:
                child.force_global(name)

    def add_frees(self, names):
        """Process list of free vars from nested scope.

        Returns a list of names that are either 1) declared global in the
        parent or 2) undefined in a top-level parent.  In either case,
        the nested scope should treat them as globals.
        """
        child_globals = []
        for name in names:
            sc = self.check_name(name)
            if self.nested:
                if name == "__class__":
                    self.cells[name] = 1
                elif sc == SC_UNKNOWN or sc == SC_FREE or isinstance(self, ClassScope):
                    self.frees[name] = 1
                elif sc == SC_GLOBAL_IMPLICIT:
                    child_globals.append(name)
                elif isinstance(self, FunctionScope) and sc == SC_LOCAL:
                    self.cells[name] = 1
                elif sc != SC_CELL:
                    child_globals.append(name)
            else:
                if name == "__class__":
                    if isinstance(self, ClassScope):
                        self.cells[name] = 1
                        continue
                    elif self.findParentClass() is not None:
                        self.frees[name] = 1
                        continue
                if sc == SC_LOCAL:
                    self.cells[name] = 1
                elif sc != SC_CELL:
                    child_globals.append(name)
        return child_globals

    def get_cell_vars(self):
        return sorted(self.cells.keys())

    def findParentClass(self):
        parent = self.parent
        while not isinstance(parent, ClassScope):
            if parent is None:
                break
            parent = parent.parent
        return parent


class ModuleScope(Scope):
    __super_init = Scope.__init__

    def __init__(self):
        # Set lineno to 0 so it sorted guaranteedly before any other scope
        self.__super_init("global", self, lineno=0)


class FunctionScope(Scope):
    pass


class GenExprScope(FunctionScope):
    __super_init = Scope.__init__

    __counter = 1

    def __init__(self, module, klass=None, name="<genexpr>", lineno=0):
        self.__counter += 1
        self.__super_init(name, module, klass, lineno=lineno)
        self.add_param(".0")

    def get_names(self):
        keys = Scope.get_names(self)
        return keys


class LambdaScope(FunctionScope):
    __super_init = Scope.__init__

    __counter = 1

    def __init__(self, module, klass=None, lineno=0):
        self.__counter += 1
        self.__super_init("<lambda>", module, klass, lineno=lineno)


class ClassScope(Scope):
    __super_init = Scope.__init__

    def __init__(self, name, module, lineno=0):
        self.__super_init(name, module, name, lineno=lineno)


class SymbolVisitor(ASTVisitor):
    def __init__(self):
        super().__init__()
        self.scopes: Dict[ast.AST, Scope] = {}
        self.klass = None

    # node that define new scopes

    def visitModule(self, node):
        scope = self.module = self.scopes[node] = ModuleScope()
        self.visit(node.body, scope)

    def visitInteractive(self, node):
        scope = self.module = self.scopes[node] = ModuleScope()
        self.visit(node.body, scope)

    visitExpression = visitModule

    def visitFunctionDef(self, node, parent):
        if node.decorator_list:
            self.visit(node.decorator_list, parent)
        parent.add_def(node.name)
        scope = FunctionScope(node.name, self.module, self.klass, lineno=node.lineno)
        scope.coroutine = isinstance(node, ast.AsyncFunctionDef)
        scope.parent = parent
        if parent.nested or isinstance(parent, FunctionScope):
            scope.nested = 1
        self.scopes[node] = scope
        self._do_args(scope, node.args)
        if node.returns:
            self.visit(node.returns, parent)
        self.visit(node.body, scope)
        self.handle_free_vars(scope, parent)

    visitAsyncFunctionDef = visitFunctionDef

    _scope_names = {
        ast.GeneratorExp: "<genexpr>",
        ast.ListComp: "<listcomp>",
        ast.DictComp: "<dictcomp>",
        ast.SetComp: "<setcomp>",
    }

    def visitAwait(self, node, scope):
        scope.coroutine = True
        self.visit(node.value, scope)

    def visitGeneratorExp(self, node, parent):
        scope = GenExprScope(
            self.module,
            self.klass,
            name=self._scope_names[type(node)],
            lineno=node.lineno,
        )
        scope.parent = parent

        # bpo-37757: For now, disallow *all* assignment expressions in the
        # outermost iterator expression of a comprehension, even those inside
        # a nested comprehension or a lambda expression.
        scope.comp_iter_expr = parent.comp_iter_expr
        if isinstance(node, ast.GeneratorExp):
            scope.generator = True

        if (
            parent.nested
            or isinstance(parent, FunctionScope)
            or isinstance(parent, GenExprScope)
        ):
            scope.nested = 1

        parent.comp_iter_expr += 1
        self.visit(node.generators[0].iter, parent)
        parent.comp_iter_expr -= 1

        self.visitcomprehension(node.generators[0], scope, True)

        for comp in node.generators[1:]:
            self.visit(comp, scope, False)

        if isinstance(node, ast.DictComp):
            self.visit(node.value, scope)
            self.visit(node.key, scope)
        else:
            self.visit(node.elt, scope)

        self.scopes[node] = scope

        self.handle_free_vars(scope, parent)

    # Whether to generate code for comprehensions inline or as nested scope
    # is configurable, but we compute nested scopes for them unconditionally
    # TODO: this may be not correct, check.
    visitSetComp = visitGeneratorExp
    visitListComp = visitGeneratorExp
    visitDictComp = visitGeneratorExp

    def visitcomprehension(self, node, scope, is_outmost):
        if node.is_async:
            scope.coroutine = True

        scope.comp_iter_target = 1
        self.visit(node.target, scope)
        scope.comp_iter_target = 0
        if is_outmost:
            scope.add_use(".0")
        else:
            scope.comp_iter_expr += 1
            self.visit(node.iter, scope)
            scope.comp_iter_expr -= 1
        for if_ in node.ifs:
            self.visit(if_, scope)

    def visitGenExprInner(self, node, scope):
        for genfor in node.quals:
            self.visit(genfor, scope)

        self.visit(node.expr, scope)

    def visitGenExprFor(self, node, scope):
        self.visit(node.assign, scope)
        self.visit(node.iter, scope)
        for if_ in node.ifs:
            self.visit(if_, scope)

    def visitGenExprIf(self, node, scope):
        self.visit(node.test, scope)

    def visitLambda(self, node, parent):
        scope = LambdaScope(self.module, self.klass, lineno=node.lineno)
        scope.parent = parent
        # bpo-37757: For now, disallow *all* assignment expressions in the
        # outermost iterator expression of a comprehension, even those inside
        # a nested comprehension or a lambda expression.
        scope.comp_iter_expr = parent.comp_iter_expr
        if parent.nested or isinstance(parent, FunctionScope):
            scope.nested = 1
        self.scopes[node] = scope
        self._do_args(scope, node.args)
        self.visit(node.body, scope)
        self.handle_free_vars(scope, parent)

    def _do_args(self, scope, args):
        for n in args.defaults:
            self.visit(n, scope.parent)
        for n in args.kw_defaults:
            if n:
                self.visit(n, scope.parent)

        for arg in args.args:
            name = arg.arg
            scope.add_param(name)
            if arg.annotation:
                self.visit(arg.annotation, scope.parent)
        for arg in getattr(args, "posonlyargs", ()):
            name = arg.arg
            scope.add_param(name)
            if arg.annotation:
                self.visit(arg.annotation, scope.parent)
        for arg in args.kwonlyargs:
            name = arg.arg
            scope.add_param(name)
            if arg.annotation:
                self.visit(arg.annotation, scope.parent)
        if args.vararg:
            scope.add_param(args.vararg.arg)
            if args.vararg.annotation:
                self.visit(args.vararg.annotation, scope.parent)
        if args.kwarg:
            scope.add_param(args.kwarg.arg)
            if args.kwarg.annotation:
                self.visit(args.kwarg.annotation, scope.parent)

    def handle_free_vars(self, scope, parent):
        parent.add_child(scope)
        scope.handle_children()

    def visitClassDef(self, node, parent):
        if node.decorator_list:
            self.visit(node.decorator_list, parent)
        for kw in node.keywords:
            self.visit(kw.value, parent)

        parent.add_def(node.name)
        for n in node.bases:
            self.visit(n, parent)
        scope = ClassScope(node.name, self.module, lineno=node.lineno)
        # Set parent ASAP. TODO: Probably makes sense to do that for
        # other scope types either.
        scope.parent = parent
        if parent.nested or isinstance(parent, FunctionScope):
            scope.nested = 1
        doc = ast.get_docstring(node)
        if doc is not None:
            scope.add_def("__doc__")
        scope.add_def("__module__")
        scope.add_def("__qualname__")
        self.scopes[node] = scope
        prev = self.klass
        self.klass = node.name
        self.visit(node.body, scope)
        self.klass = prev
        self.handle_free_vars(scope, parent)

    # name can be a def or a use

    def visitName(self, node, scope):
        if isinstance(node.ctx, ast.Store):
            if scope.comp_iter_target:
                # This name is an iteration variable in a comprehension,
                # so check for a binding conflict with any named expressions.
                # Otherwise, mark it as an iteration variable so subsequent
                # named expressions can check for conflicts.
                if node.id in scope.nonlocals or node.id in scope.globals:
                    raise SyntaxError(
                        f"comprehension inner loop cannot rebind assignment expression target '{node.id}'"
                    )
                scope.add_def(node.id, DEF_COMP_ITER)

            scope.add_def(node.id)
        elif isinstance(node.ctx, ast.Del):
            # We do something to var, so even if we "undefine" it, it's a def.
            # Implementation-wise, delete is storing special value (usually
            # NULL) to var.
            scope.add_def(node.id)
        else:
            scope.add_use(node.id)

            if node.id == "super" and isinstance(scope, FunctionScope):
                # If super() is used, and special cell var __class__ to class
                # definition, and free var to the method. This is complicated
                # by the fact that original Python2 implementation supports
                # free->cell var relationship only if free var is defined in
                # a scope marked as "nested", which normal method in a class
                # isn't.
                scope.add_use("__class__")

    # operations that bind new names

    def visitNamedExpr(self, node, scope):
        if scope.comp_iter_expr:
            # Assignment isn't allowed in a comprehension iterable expression
            raise SyntaxError(
                "assignment expression cannot be used in a comprehension iterable expression"
            )

        name = node.target.id
        if isinstance(scope, GenExprScope):
            cur = scope
            while cur:
                if isinstance(cur, GenExprScope):
                    if cur.defs.get(name, 0) & DEF_COMP_ITER:
                        raise SyntaxError(
                            f"assignment expression cannot rebind comprehension iteration variable '{name}'"
                        )

                elif isinstance(cur, FunctionScope):
                    # If we find a FunctionBlock entry, add as GLOBAL/LOCAL or NONLOCAL/LOCAL
                    scope.frees[name] = 1
                    if name not in cur.explicit_globals:
                        scope.nonlocals[name] = 1
                    else:
                        scope.add_use(name)
                    cur.add_def(name)
                    break
                elif isinstance(cur, ModuleScope):
                    scope.globals[name] = 1
                    scope.add_use(name)
                    cur.add_def(name)
                    break
                elif isinstance(cur, ClassScope):
                    raise SyntaxError(
                        "assignment expression within a comprehension cannot be used in a class body"
                    )
                cur = cur.parent

        self.visit(node.value, scope)
        self.visit(node.target, scope)

    def visitFor(self, node, scope):
        self.visit(node.target, scope)
        self.visit(node.iter, scope)
        self.visit(node.body, scope)
        if node.orelse:
            self.visit(node.orelse, scope)

    visitAsyncFor = visitFor

    def visitImportFrom(self, node, scope):
        for alias in node.names:
            if alias.name == "*":
                continue
            scope.add_def(alias.asname or alias.name)

    def visitImport(self, node, scope):
        for alias in node.names:
            name = alias.name
            i = name.find(".")
            if i > -1:
                name = name[:i]
            scope.add_def(alias.asname or name)

    def visitGlobal(self, node, scope):
        for name in node.names:
            scope.add_global(name)

    def visitNonlocal(self, node, scope):
        # TODO: Check that var exists in outer scope
        for name in node.names:
            scope.frees[name] = 1
            scope.nonlocals[name] = 1

    def visitAssign(self, node, scope):
        """Propagate assignment flag down to child nodes.

        The Assign node doesn't itself contains the variables being
        assigned to.  Instead, the children in node.nodes are visited
        with the assign flag set to true.  When the names occur in
        those nodes, they are marked as defs.

        Some names that occur in an assignment target are not bound by
        the assignment, e.g. a name occurring inside a slice.  The
        visitor handles these nodes specially; they do not propagate
        the assign flag to their children.
        """
        for n in node.targets:
            self.visit(n, scope)
        self.visit(node.value, scope)

    def visitAnnAssign(self, node, scope):
        target = node.target
        if isinstance(target, ast.Name):
            if target.id in scope.nonlocals or target.id in scope.explicit_globals:
                is_nonlocal = target.id in scope.nonlocals
                raise SyntaxError(
                    f"annotated name '{target.id}' can't be {'nonlocal' if is_nonlocal else 'global'}"
                )
            if node.simple or node.value:
                scope.add_def(target.id)
        else:
            self.visit(node.target, scope)
        self.visit(node.annotation, scope)
        if node.value:
            self.visit(node.value, scope)

    def visitAssName(self, node, scope):
        scope.add_def(node.name)

    def visitAssAttr(self, node, scope):
        self.visit(node.expr, scope)

    def visitSubscript(self, node, scope):
        self.visit(node.value, scope)
        self.visit(node.slice, scope)

    def visitAttribute(self, node, scope):
        self.visit(node.value, scope)

    def visitSlice(self, node, scope):
        if node.lower:
            self.visit(node.lower, scope)
        if node.upper:
            self.visit(node.upper, scope)
        if node.step:
            self.visit(node.step, scope)

    def visitAugAssign(self, node, scope):
        # If the LHS is a name, then this counts as assignment.
        # Otherwise, it's just use.
        self.visit(node.target, scope)
        if isinstance(node.target, ast.Name):
            self.visit(node.target, scope)
        self.visit(node.value, scope)

    # prune if statements if tests are false

    _const_types = str, bytes, int, long, float

    def visitIf(self, node, scope):
        self.visit(node.test, scope)
        self.visit(node.body, scope)
        if node.orelse:
            self.visit(node.orelse, scope)

    # a yield statement signals a generator

    def visitYield(self, node, scope):
        scope.generator = True
        if node.value:
            self.visit(node.value, scope)

    def visitYieldFrom(self, node, scope):
        scope.generator = True
        if node.value:
            self.visit(node.value, scope)

    def visitTry(self, node, scope):
        self.visit(node.body, scope)
        # Handle exception capturing vars
        for handler in node.handlers:
            if handler.type:
                self.visit(handler.type, scope)
            if handler.name:
                scope.add_def(handler.name)
            self.visit(handler.body, scope)
        self.visit(node.orelse, scope)
        self.visit(node.finalbody, scope)


def list_eq(l1, l2):
    return sorted(l1) == sorted(l2)
