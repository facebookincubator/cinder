#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

import os
import setuptools

from distutils.command.build_ext import build_ext
from concurrent.futures import ThreadPoolExecutor


MODULE_DIR = os.path.dirname(os.path.realpath(__file__))

THIRD_PARTY_DIR = os.path.realpath(f"{MODULE_DIR}/ThirdParty")
CINDER_DIR = os.path.realpath(f"{MODULE_DIR}/..")
PYTHON_DIR = os.path.realpath(f"{MODULE_DIR}/../../")

INCLUDE_DIRS = [
    f"{CINDER_DIR}/Include",
    f"{PYTHON_DIR}/Include/internal",
    f"{THIRD_PARTY_DIR}/asmjit/src",
    f"{THIRD_PARTY_DIR}/fmt-8.1.1/include",
    f"{THIRD_PARTY_DIR}/i386-dis",
    f"{THIRD_PARTY_DIR}/json",
    f"{THIRD_PARTY_DIR}/parallel-hashmap",
]

JIT_SRCS = [
    "Jit/bitvector.cpp",
    "Jit/bytecode.cpp",
    "Jit/code_allocator.cpp",
    "Jit/compiler.cpp",
    "Jit/debug_info.cpp",
    "Jit/deopt.cpp",
    "Jit/deopt_patcher.cpp",
    "Jit/dict_watch.cpp",
    "Jit/disassembler.cpp",
    "Jit/frame.cpp",
    "Jit/hir/hir.cpp",
    "Jit/hir/alias_class.cpp",
    "Jit/hir/analysis.cpp",
    "Jit/hir/builder.cpp",
    "Jit/hir/memory_effects.cpp",
    "Jit/hir/optimization.cpp",
    "Jit/hir/parser.cpp",
    "Jit/hir/preload.cpp",
    "Jit/hir/printer.cpp",
    "Jit/hir/refcount_insertion.cpp",
    "Jit/hir/register.cpp",
    "Jit/hir/simplify.cpp",
    "Jit/hir/ssa.cpp",
    "Jit/hir/type.cpp",
    "Jit/inline_cache.cpp",
    "Jit/jit_context.cpp",
    "Jit/jit_flag_processor.cpp",
    "Jit/jit_gdb_support.cpp",
    "Jit/jit_list.cpp",
    "Jit/jit_rt.cpp",
    "Jit/jit_time_log.cpp",
    "Jit/live_type_map.cpp",
    "Jit/log.cpp",
    "Jit/patternmatch.c",
    "Jit/perf_jitdump.cpp",
    "Jit/profile_data.cpp",
    "Jit/pyjit.cpp",
    "Jit/runtime.cpp",
    "Jit/runtime_support.cpp",
    "Jit/strobelight_exports.cpp",
    "Jit/symbolizer.cpp",
    "Jit/type_deopt_patchers.cpp",
    "Jit/type_profiler.cpp",
    "Jit/util.cpp",
    "Jit/codegen/annotations.cpp",
    "Jit/codegen/autogen.cpp",
    "Jit/codegen/code_section.cpp",
    "Jit/codegen/copy_graph.cpp",
    "Jit/codegen/gen_asm.cpp",
    "Jit/codegen/gen_asm_utils.cpp",
    "Jit/codegen/x86_64.cpp",
    "Jit/lir/block_builder.cpp",
    "Jit/lir/dce.cpp",
    "Jit/lir/lir.cpp",
    "Jit/lir/block.cpp",
    "Jit/lir/blocksorter.cpp",
    "Jit/lir/c_helper_translations.cpp",
    "Jit/lir/c_helper_translations_auto.cpp",
    "Jit/lir/function.cpp",
    "Jit/lir/generator.cpp",
    "Jit/lir/inliner.cpp",
    "Jit/lir/instruction.cpp",
    "Jit/lir/operand.cpp",
    "Jit/lir/parser.cpp",
    "Jit/lir/postalloc.cpp",
    "Jit/lir/postgen.cpp",
    "Jit/lir/printer.cpp",
    "Jit/lir/regalloc.cpp",
    "Jit/lir/rewrite.cpp",
    "Jit/lir/verify.cpp",
    "Jit/lir/symbol_mapping.cpp",
]

I386_DASM_SRCS = [
    "ThirdParty/i386-dis/i386-dis.c",
    "ThirdParty/i386-dis/dis-buf.c",
]

ASMJIT_SRCS = [
    "ThirdParty/asmjit/src/asmjit/arm/a64assembler.cpp",
    "ThirdParty/asmjit/src/asmjit/arm/a64builder.cpp",
    "ThirdParty/asmjit/src/asmjit/arm/a64compiler.cpp",
    "ThirdParty/asmjit/src/asmjit/arm/a64emithelper.cpp",
    "ThirdParty/asmjit/src/asmjit/arm/a64formatter.cpp",
    "ThirdParty/asmjit/src/asmjit/arm/a64func.cpp",
    "ThirdParty/asmjit/src/asmjit/arm/a64instapi.cpp",
    "ThirdParty/asmjit/src/asmjit/arm/a64instdb.cpp",
    "ThirdParty/asmjit/src/asmjit/arm/a64operand.cpp",
    "ThirdParty/asmjit/src/asmjit/arm/a64rapass.cpp",
    "ThirdParty/asmjit/src/asmjit/arm/armformatter.cpp",
    "ThirdParty/asmjit/src/asmjit/core/archtraits.cpp",
    "ThirdParty/asmjit/src/asmjit/core/assembler.cpp",
    "ThirdParty/asmjit/src/asmjit/core/builder.cpp",
    "ThirdParty/asmjit/src/asmjit/core/codeholder.cpp",
    "ThirdParty/asmjit/src/asmjit/core/codewriter.cpp",
    "ThirdParty/asmjit/src/asmjit/core/compiler.cpp",
    "ThirdParty/asmjit/src/asmjit/core/constpool.cpp",
    "ThirdParty/asmjit/src/asmjit/core/cpuinfo.cpp",
    "ThirdParty/asmjit/src/asmjit/core/emithelper.cpp",
    "ThirdParty/asmjit/src/asmjit/core/emitter.cpp",
    "ThirdParty/asmjit/src/asmjit/core/emitterutils.cpp",
    "ThirdParty/asmjit/src/asmjit/core/environment.cpp",
    "ThirdParty/asmjit/src/asmjit/core/errorhandler.cpp",
    "ThirdParty/asmjit/src/asmjit/core/formatter.cpp",
    "ThirdParty/asmjit/src/asmjit/core/funcargscontext.cpp",
    "ThirdParty/asmjit/src/asmjit/core/func.cpp",
    "ThirdParty/asmjit/src/asmjit/core/globals.cpp",
    "ThirdParty/asmjit/src/asmjit/core/inst.cpp",
    "ThirdParty/asmjit/src/asmjit/core/jitallocator.cpp",
    "ThirdParty/asmjit/src/asmjit/core/jitruntime.cpp",
    "ThirdParty/asmjit/src/asmjit/core/logger.cpp",
    "ThirdParty/asmjit/src/asmjit/core/operand.cpp",
    "ThirdParty/asmjit/src/asmjit/core/osutils.cpp",
    "ThirdParty/asmjit/src/asmjit/core/ralocal.cpp",
    "ThirdParty/asmjit/src/asmjit/core/rapass.cpp",
    "ThirdParty/asmjit/src/asmjit/core/rastack.cpp",
    "ThirdParty/asmjit/src/asmjit/core/string.cpp",
    "ThirdParty/asmjit/src/asmjit/core/support.cpp",
    "ThirdParty/asmjit/src/asmjit/core/target.cpp",
    "ThirdParty/asmjit/src/asmjit/core/type.cpp",
    "ThirdParty/asmjit/src/asmjit/core/virtmem.cpp",
    "ThirdParty/asmjit/src/asmjit/core/zone.cpp",
    "ThirdParty/asmjit/src/asmjit/core/zonehash.cpp",
    "ThirdParty/asmjit/src/asmjit/core/zonelist.cpp",
    "ThirdParty/asmjit/src/asmjit/core/zonestack.cpp",
    "ThirdParty/asmjit/src/asmjit/core/zonetree.cpp",
    "ThirdParty/asmjit/src/asmjit/core/zonevector.cpp",
    "ThirdParty/asmjit/src/asmjit/x86/x86assembler.cpp",
    "ThirdParty/asmjit/src/asmjit/x86/x86builder.cpp",
    "ThirdParty/asmjit/src/asmjit/x86/x86compiler.cpp",
    "ThirdParty/asmjit/src/asmjit/x86/x86emithelper.cpp",
    "ThirdParty/asmjit/src/asmjit/x86/x86formatter.cpp",
    "ThirdParty/asmjit/src/asmjit/x86/x86func.cpp",
    "ThirdParty/asmjit/src/asmjit/x86/x86instapi.cpp",
    "ThirdParty/asmjit/src/asmjit/x86/x86instdb.cpp",
    "ThirdParty/asmjit/src/asmjit/x86/x86operand.cpp",
    "ThirdParty/asmjit/src/asmjit/x86/x86rapass.cpp",
]

STRICTM_SRCS = [
    "StrictModules/Compiler/analyzed_module.cpp",
    "StrictModules/Compiler/abstract_module_loader.cpp",
    "StrictModules/Compiler/module_info.cpp",
    "StrictModules/Compiler/stub.cpp",
    "StrictModules/Objects/base_object.cpp",
    "StrictModules/Objects/callable.cpp",
    "StrictModules/Objects/instance.cpp",
    "StrictModules/Objects/module.cpp",
    "StrictModules/Objects/numerics.cpp",
    "StrictModules/Objects/type.cpp",
    "StrictModules/Objects/constants.cpp",
    "StrictModules/Objects/exception_object.cpp",
    "StrictModules/Objects/module_type.cpp",
    "StrictModules/Objects/object_type.cpp",
    "StrictModules/Objects/string_object.cpp",
    "StrictModules/Objects/type_type.cpp",
    "StrictModules/Objects/union.cpp",
    "StrictModules/Objects/unknown.cpp",
    "StrictModules/Objects/iterable_objects.cpp",
    "StrictModules/Objects/iterator_objects.cpp",
    "StrictModules/Objects/dict_object.cpp",
    "StrictModules/Objects/function.cpp",
    "StrictModules/Objects/codeobject.cpp",
    "StrictModules/Objects/signature.cpp",
    "StrictModules/Objects/super.cpp",
    "StrictModules/Objects/property.cpp",
    "StrictModules/Objects/builtins.cpp",
    "StrictModules/Objects/lazy_object.cpp",
    "StrictModules/Objects/genericalias_object.cpp",
    "StrictModules/Objects/strict_modules_builtins.cpp",
    "StrictModules/Objects/objects.cpp",
    "StrictModules/Objects/object_interface.cpp",
    "StrictModules/symbol_table.cpp",
    "StrictModules/analyzer.cpp",
    "StrictModules/ast_visitor.cpp",
    "StrictModules/ast_preprocessor.cpp",
    "StrictModules/parser_util.cpp",
    "StrictModules/strict_module_checker_interface.cpp",
    "StrictModules/scope.cpp",
    "StrictModules/pystrictmodule.cpp",
    "StrictModules/exceptions.cpp",
    "StrictModules/error_sink.cpp",
]

MISC_SRCS = [
    "Python/classloader.c",
]

ALL_SRCS = JIT_SRCS + I386_DASM_SRCS + ASMJIT_SRCS + STRICTM_SRCS + MISC_SRCS

# Monkey-patch the ability to compile C++ files (but not C files) with
# -std=c++20 and perform compilation in parallel.
class CinderBuildExt(build_ext):
    def build_extension(self, ext):
        old_compile_func = self.compiler.compile
        old__compile_func = self.compiler._compile

        with ThreadPoolExecutor(max_workers=os.cpu_count()) as executor:
            compilation_futures = []

            def new_compile(sources, output_dir=None, macros=None,
                    include_dirs=None, debug=0, extra_preargs=None,
                    extra_postargs=None, depends=None):
                r = old_compile_func(sources, output_dir, macros,
                    include_dirs, debug, extra_preargs, extra_postargs, depends)
                for fut in compilation_futures:
                    fut.result()
                return r

            def new__compile(obj, src, ext, cc_args, extra_postargs, pp_opts):
                if src.endswith(".cpp"):
                    cc_args = cc_args + ["-std=c++20"]
                compilation_futures.append(executor.submit(old__compile_func,
                    obj, src, ext, cc_args, extra_postargs, pp_opts))

            self.compiler.compile = new_compile
            self.compiler._compile = new__compile

            return super().build_extension(ext)


with open("README.md", "r", encoding="utf-8") as fh:
    long_description = fh.read()

setuptools.setup(
    name="cindervm",
    version="0.0.3",
    author="Meta Platforms, Inc.",
    author_email="cinder@meta.com",
    description="High-performance Python runtime extensions",
    long_description=long_description,
    long_description_content_type="text/markdown",
    url="https://github.com/facebookincubator/cinder",
    cmdclass={"build_ext": CinderBuildExt},
    ext_modules=[
        setuptools.Extension(
            "cindervm",
            sources=ALL_SRCS,
            include_dirs=INCLUDE_DIRS,
            define_macros=[("FMT_HEADER_ONLY", 1), ("Py_BUILD_CORE", None)],
            extra_compile_args=["-Wno-ambiguous-reversed-operator"],
        )
    ],
    package_dir={"": "src"},
    packages=setuptools.find_packages(where="src"),
    python_requires="==3.10.*",
)
