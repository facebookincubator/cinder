import dis
import glob
import inspect
from compiler.pycodegen import make_compiler
from io import StringIO
from os import path
from subprocess import PIPE, run
from types import CodeType
from unittest import TestCase


_UNSPECIFIED = object()


def get_repo_root():
    dirname = path.dirname(__file__)
    completed_process = run(
        ["git", "rev-parse", "--show-toplevel"], cwd=dirname, stdout=PIPE, stderr=PIPE
    )
    if completed_process.returncode:
        print("Error occurred", file=sys.stderr)
        sys.exit(1)
    return completed_process.stdout.strip().decode("utf8")


def glob_test(dir, pattern, adder):
    base_path = path.dirname(__file__)
    target_dir = path.join(base_path, dir)
    for fname in glob.glob(path.join(target_dir, pattern), recursive=True):
        modname = path.relpath(fname, base_path)
        adder(modname, fname)


class CompilerTest(TestCase):
    def get_disassembly_as_string(self, co):
        s = StringIO()
        dis.dis(co, file=s)
        return s.getvalue()

    def assertInBytecode(self, x, opname, argval=_UNSPECIFIED):
        """Returns instr if op is found, otherwise throws AssertionError"""
        for instr in dis.get_instructions(x):
            if instr.opname == opname:
                if argval is _UNSPECIFIED or instr.argval == argval:
                    return instr
        disassembly = self.get_disassembly_as_string(x)
        if argval is _UNSPECIFIED:
            msg = f"{opname} not found in bytecode:\n{disassembly}"
        else:
            msg = f"({opname},{argval}) not found in bytecode:\n{disassembly}"
        self.fail(msg)

    def assertNotInBytecode(self, x, opname, argval=_UNSPECIFIED):
        """Throws AssertionError if op is found"""
        for instr in dis.get_instructions(x):
            if instr.opname == opname:
                disassembly = self.get_disassembly_as_string(x)
                if argval is _UNSPECIFIED:
                    msg = f"{opname} occurs in bytecode:\n{disassembly}"
                    self.fail(msg)
                elif instr.argval == argval:
                    msg = f"({opname},{argval!r}) occurs in bytecode:\n{disassembly}"
                    self.fail(msg)

    def compile(
        self,
        code,
        generator=None,
        modname="<module>",
        optimize=0,
        peephole_enabled=True,
        ast_optimizer_enabled=True,
    ):
        code = inspect.cleandoc("\n" + code)
        gen = make_compiler(
            code,
            "",
            "exec",
            generator=generator,
            modname=modname,
            optimize=optimize,
            peephole_enabled=peephole_enabled,
            ast_optimizer_enabled=ast_optimizer_enabled,
        )
        return gen.getCode()

    def run_code(self, code, generator=None, modname="<module>", peephole_enabled=True):
        compiled = self.compile(
            code, generator, modname, peephole_enabled=peephole_enabled
        )
        d = {}
        exec(compiled, d)
        return d

    def find_code(self, code, name=None):
        consts = [
            const
            for const in code.co_consts
            if isinstance(const, CodeType) and (name is None or const.co_name == name)
        ]
        if len(consts) == 0:
            self.fail("No const available")
        elif len(consts) != 1:
            self.fail(
                "Too many consts: " + ",".join(f"'{code.co_name}'" for code in consts)
            )
        return consts[0]

    def to_graph(self, code, generator=None, ast_optimizer_enabled=True):
        code = inspect.cleandoc("\n" + code)
        gen = make_compiler(
            code,
            "",
            "exec",
            generator=generator,
            ast_optimizer_enabled=ast_optimizer_enabled,
        )
        return gen.graph

    def dump_graph(self, graph):
        io = StringIO()
        graph.dump(io)
        return io.getvalue()

    def graph_to_instrs(self, graph):
        for block in graph.getBlocks():
            yield from block.getInstructions()

    def get_consts_with_names(self, code):
        return {k: v for k, v in zip(code.co_names, code.co_consts)}

    def assertNotInGraph(self, graph, opname, argval=_UNSPECIFIED):
        for block in graph.getBlocks():
            for instr in block.getInstructions():
                if instr.opname == opname:
                    if argval == _UNSPECIFIED:
                        msg = "%s occurs in bytecode:\n%s" % (
                            opname,
                            self.dump_graph(graph),
                        )
                        self.fail(msg)
                    elif instr.oparg == argval:
                        msg = "(%s,%r) occurs in bytecode:\n%s"
                        msg = msg % (opname, argval, self.dump_graph(graph))
                        self.fail(msg)

    def assertInGraph(self, graph, opname, argval=_UNSPECIFIED):
        """Returns instr if op is found, otherwise throws AssertionError"""
        for block in graph.getBlocks():
            for instr in block.getInstructions():
                if instr.opname == opname:
                    if argval is _UNSPECIFIED or instr.oparg == argval:
                        return instr
        disassembly = self.dump_graph(graph)
        if argval is _UNSPECIFIED:
            msg = "%s not found in bytecode:\n%s" % (opname, disassembly)
        else:
            msg = "(%s,%r) not found in bytecode:\n%s"
            msg = msg % (opname, argval, disassembly)
        self.fail(msg)
