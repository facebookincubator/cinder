static void *opcode_targets[256] = {
    &&_unknown_opcode,
    &&TARGET_POP_TOP,
    &&TARGET_ROT_TWO,
    &&TARGET_ROT_THREE,
    &&TARGET_DUP_TOP,
    &&TARGET_DUP_TOP_TWO,
    &&TARGET_ROT_FOUR,
    &&_unknown_opcode,
    &&_unknown_opcode,
    &&TARGET_NOP,
    &&TARGET_UNARY_POSITIVE,
    &&TARGET_UNARY_NEGATIVE,
    &&TARGET_UNARY_NOT,
    &&_unknown_opcode,
    &&_unknown_opcode,
    &&TARGET_UNARY_INVERT,
    &&TARGET_BINARY_MATRIX_MULTIPLY,
    &&TARGET_INPLACE_MATRIX_MULTIPLY,
    &&_unknown_opcode,
    &&TARGET_BINARY_POWER,
    &&TARGET_BINARY_MULTIPLY,
    &&_unknown_opcode,
    &&TARGET_BINARY_MODULO,
    &&TARGET_BINARY_ADD,
    &&TARGET_BINARY_SUBTRACT,
    &&TARGET_BINARY_SUBSCR,
    &&TARGET_BINARY_FLOOR_DIVIDE,
    &&TARGET_BINARY_TRUE_DIVIDE,
    &&TARGET_INPLACE_FLOOR_DIVIDE,
    &&TARGET_INPLACE_TRUE_DIVIDE,
    &&TARGET_GET_LEN,
    &&TARGET_MATCH_MAPPING,
    &&TARGET_MATCH_SEQUENCE,
    &&TARGET_MATCH_KEYS,
    &&TARGET_COPY_DICT_WITHOUT_KEYS,
    &&_unknown_opcode,
    &&_unknown_opcode,
    &&_unknown_opcode,
    &&_unknown_opcode,
    &&_unknown_opcode,
    &&_unknown_opcode,
    &&_unknown_opcode,
    &&_unknown_opcode,
    &&_unknown_opcode,
    &&_unknown_opcode,
    &&_unknown_opcode,
    &&_unknown_opcode,
    &&_unknown_opcode,
    &&_unknown_opcode,
    &&TARGET_WITH_EXCEPT_START,
    &&TARGET_GET_AITER,
    &&TARGET_GET_ANEXT,
    &&TARGET_BEFORE_ASYNC_WITH,
    &&_unknown_opcode,
    &&TARGET_END_ASYNC_FOR,
    &&TARGET_INPLACE_ADD,
    &&TARGET_INPLACE_SUBTRACT,
    &&TARGET_INPLACE_MULTIPLY,
    &&_unknown_opcode,
    &&TARGET_INPLACE_MODULO,
    &&TARGET_STORE_SUBSCR,
    &&TARGET_DELETE_SUBSCR,
    &&TARGET_BINARY_LSHIFT,
    &&TARGET_BINARY_RSHIFT,
    &&TARGET_BINARY_AND,
    &&TARGET_BINARY_XOR,
    &&TARGET_BINARY_OR,
    &&TARGET_INPLACE_POWER,
    &&TARGET_GET_ITER,
    &&TARGET_GET_YIELD_FROM_ITER,
    &&TARGET_PRINT_EXPR,
    &&TARGET_LOAD_BUILD_CLASS,
    &&TARGET_YIELD_FROM,
    &&TARGET_GET_AWAITABLE,
    &&TARGET_LOAD_ASSERTION_ERROR,
    &&TARGET_INPLACE_LSHIFT,
    &&TARGET_INPLACE_RSHIFT,
    &&TARGET_INPLACE_AND,
    &&TARGET_INPLACE_XOR,
    &&TARGET_INPLACE_OR,
    &&_unknown_opcode,
    &&_unknown_opcode,
    &&TARGET_LIST_TO_TUPLE,
    &&TARGET_RETURN_VALUE,
    &&TARGET_IMPORT_STAR,
    &&TARGET_SETUP_ANNOTATIONS,
    &&TARGET_YIELD_VALUE,
    &&TARGET_POP_BLOCK,
    &&_unknown_opcode,
    &&TARGET_POP_EXCEPT,
    &&TARGET_STORE_NAME,
    &&TARGET_DELETE_NAME,
    &&TARGET_UNPACK_SEQUENCE,
    &&TARGET_FOR_ITER,
    &&TARGET_UNPACK_EX,
    &&TARGET_STORE_ATTR,
    &&TARGET_DELETE_ATTR,
    &&TARGET_STORE_GLOBAL,
    &&TARGET_DELETE_GLOBAL,
    &&TARGET_ROT_N,
    &&TARGET_LOAD_CONST,
    &&TARGET_LOAD_NAME,
    &&TARGET_BUILD_TUPLE,
    &&TARGET_BUILD_LIST,
    &&TARGET_BUILD_SET,
    &&TARGET_BUILD_MAP,
    &&TARGET_LOAD_ATTR,
    &&TARGET_COMPARE_OP,
    &&TARGET_IMPORT_NAME,
    &&TARGET_IMPORT_FROM,
    &&TARGET_JUMP_FORWARD,
    &&TARGET_JUMP_IF_FALSE_OR_POP,
    &&TARGET_JUMP_IF_TRUE_OR_POP,
    &&TARGET_JUMP_ABSOLUTE,
    &&TARGET_POP_JUMP_IF_FALSE,
    &&TARGET_POP_JUMP_IF_TRUE,
    &&TARGET_LOAD_GLOBAL,
    &&TARGET_IS_OP,
    &&TARGET_CONTAINS_OP,
    &&TARGET_RERAISE,
    &&_unknown_opcode,
    &&TARGET_JUMP_IF_NOT_EXC_MATCH,
    &&TARGET_SETUP_FINALLY,
    &&_unknown_opcode,
    &&TARGET_LOAD_FAST,
    &&TARGET_STORE_FAST,
    &&TARGET_DELETE_FAST,
    &&_unknown_opcode,
    &&_unknown_opcode,
    &&TARGET_GEN_START,
    &&TARGET_RAISE_VARARGS,
    &&TARGET_CALL_FUNCTION,
    &&TARGET_MAKE_FUNCTION,
    &&TARGET_BUILD_SLICE,
    &&_unknown_opcode,
    &&TARGET_LOAD_CLOSURE,
    &&TARGET_LOAD_DEREF,
    &&TARGET_STORE_DEREF,
    &&TARGET_DELETE_DEREF,
    &&_unknown_opcode,
    &&_unknown_opcode,
    &&TARGET_CALL_FUNCTION_KW,
    &&TARGET_CALL_FUNCTION_EX,
    &&TARGET_SETUP_WITH,
    &&TARGET_EXTENDED_ARG,
    &&TARGET_LIST_APPEND,
    &&TARGET_SET_ADD,
    &&TARGET_MAP_ADD,
    &&TARGET_LOAD_CLASSDEREF,
    &&_unknown_opcode,
    &&_unknown_opcode,
    &&_unknown_opcode,
    &&TARGET_MATCH_CLASS,
    &&_unknown_opcode,
    &&TARGET_SETUP_ASYNC_WITH,
    &&TARGET_FORMAT_VALUE,
    &&TARGET_BUILD_CONST_KEY_MAP,
    &&TARGET_BUILD_STRING,
#ifdef ENABLE_CINDERX
    &&TARGET_INVOKE_METHOD,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_FIELD,
#else
    &&_unknown_opcode,
#endif
    &&TARGET_LOAD_METHOD,
    &&TARGET_CALL_METHOD,
    &&TARGET_LIST_EXTEND,
    &&TARGET_SET_UPDATE,
    &&TARGET_DICT_MERGE,
    &&TARGET_DICT_UPDATE,
#ifdef ENABLE_CINDERX
    &&TARGET_STORE_FIELD,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_SEQUENCE_REPEAT,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_BUILD_CHECKED_LIST,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_TYPE,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_CAST,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_LOCAL,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_STORE_LOCAL,
#else
    &&_unknown_opcode,
#endif
    &&_unknown_opcode,
#ifdef ENABLE_CINDERX
    &&TARGET_PRIMITIVE_BOX,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_POP_JUMP_IF_ZERO,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_POP_JUMP_IF_NONZERO,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_PRIMITIVE_UNBOX,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_PRIMITIVE_BINARY_OP,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_PRIMITIVE_UNARY_OP,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_PRIMITIVE_COMPARE_OP,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_ITERABLE_ARG,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_MAPPING_ARG,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_INVOKE_FUNCTION,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_JUMP_IF_ZERO_OR_POP,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_JUMP_IF_NONZERO_OR_POP,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_FAST_LEN,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_CONVERT_PRIMITIVE,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_CHECK_ARGS,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_INVOKE_NATIVE,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_CLASS,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_BUILD_CHECKED_MAP,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_SEQUENCE_GET,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_SEQUENCE_SET,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LIST_DEL,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_REFINE_TYPE,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_PRIMITIVE_LOAD_CONST,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_RETURN_PRIMITIVE,
#else
    &&_unknown_opcode,
#endif
    &&TARGET_LOAD_METHOD_SUPER,
    &&TARGET_LOAD_ATTR_SUPER,
#ifdef ENABLE_CINDERX
    &&TARGET_TP_ALLOC,
#else
    &&_unknown_opcode,
#endif
    &&_unknown_opcode,
    &&_unknown_opcode,
    &&_unknown_opcode,
    &&_unknown_opcode,
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_METHOD_UNSHADOWED_METHOD,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_METHOD_TYPE_METHODLIKE,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_BUILD_CHECKED_LIST_CACHED,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_TP_ALLOC_CACHED,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_ATTR_S_MODULE,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_METHOD_S_MODULE,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_INVOKE_FUNCTION_CACHED,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_INVOKE_FUNCTION_INDIRECT_CACHED,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_BUILD_CHECKED_MAP_CACHED,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_CHECK_ARGS_CACHED,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_PRIMITIVE_STORE_FAST,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_CAST_CACHED_OPTIONAL,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_CAST_CACHED,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_CAST_CACHED_EXACT,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_CAST_CACHED_OPTIONAL_EXACT,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_PRIMITIVE_FIELD,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_STORE_PRIMITIVE_FIELD,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_OBJ_FIELD,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_STORE_OBJ_FIELD,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_INVOKE_METHOD_CACHED,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_BINARY_SUBSCR_TUPLE_CONST_INT,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_BINARY_SUBSCR_DICT_STR,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_BINARY_SUBSCR_LIST,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_BINARY_SUBSCR_TUPLE,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_BINARY_SUBSCR_DICT,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_METHOD_UNCACHABLE,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_METHOD_MODULE,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_METHOD_TYPE,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_METHOD_SPLIT_DICT_DESCR,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_METHOD_SPLIT_DICT_METHOD,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_METHOD_DICT_DESCR,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_METHOD_DICT_METHOD,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_METHOD_NO_DICT_METHOD,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_METHOD_NO_DICT_DESCR,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_STORE_ATTR_SLOT,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_STORE_ATTR_SPLIT_DICT,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_STORE_ATTR_DESCR,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_STORE_ATTR_UNCACHABLE,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_STORE_ATTR_DICT,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_ATTR_POLYMORPHIC,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_ATTR_SLOT,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_ATTR_MODULE,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_ATTR_TYPE,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_ATTR_SPLIT_DICT_DESCR,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_ATTR_SPLIT_DICT,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_ATTR_DICT_NO_DESCR,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_ATTR_NO_DICT_DESCR,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_ATTR_DICT_DESCR,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_ATTR_UNCACHABLE,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_LOAD_GLOBAL_CACHED,
#else
    &&_unknown_opcode,
#endif
#ifdef ENABLE_CINDERX
    &&TARGET_SHADOW_NOP,
#else
    &&_unknown_opcode,
#endif

};
