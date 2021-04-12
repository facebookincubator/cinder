#ifndef JIT_CODE_GEN_H
#define JIT_CODE_GEN_H

#include "Python.h"

#include "Jit/code_allocator.h"

/*
 * This defines the public interface of the JIT backend.
 *
 * It's responsible for allocating executable memory from a code allocator
 * and generating machine code.
 *
 * This layer should remain focused solely on code generation and should not
 * interact with the runtime. All state needed for code generation should
 * be supplied by higher layers. Similarly, any out-of-band guards that need
 * to be put in place in order for specialized code to execute safely should
 * be handled at a higher layer.
 */

struct CodeGen;

/*
 * Create a new code generator.
 *
 * Returns NULL on error.
 */
CodeGen* CodeGen_New();

/*
 * Generate a specialized slot function for a tp_call function that avoids the
 * lookups each time it's called.
 *
 * Returns NULL on error.
 */
ternaryfunc
CodeGen_GenCallSlot(CodeGen* codegen, PyTypeObject* type, PyObject* call_func);

/*
 * Generate a specialized slot function for a reprfunc (tp_repr or tp_str) that
 * avoids the method lookup each time it is called.
 * repr_func - The reprfunc method that should be called.
 *
 * Returns NULL on error.
 */
reprfunc CodeGen_GenReprFuncSlot(
    CodeGen* codegen,
    PyTypeObject* type,
    PyObject* repr_func);

getattrofunc CodeGen_GenGetAttrSlot(
    CodeGen* codegen,
    PyTypeObject* type,
    PyObject* call_func);

descrgetfunc CodeGen_GenGetDescrSlot(
    CodeGen* codegen,
    PyTypeObject* type,
    PyObject* get_func);

void CodeGen_Free(CodeGen* codegen);

extern int g_gdb_stubs_support;

#endif /* JIT_CODE_GEN_H */
