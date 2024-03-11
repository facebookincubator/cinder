shadowop = set()
cinderxop = set()


def init(opname, opmap, hasname, hasjrel, hasjabs, hasconst):
    def def_op(name, op):
        opname[op] = name
        opmap[name] = op
        cinderxop.add(name)

    def name_op(name, op):
        def_op(name, op)
        hasname.append(op)

    def jrel_op(name, op):
        def_op(name, op)
        hasjrel.append(op)

    def jabs_op(name, op):
        def_op(name, op)
        hasjabs.append(op)

    def shadow_op(name, op):
        def_op(name, op)
        shadowop.add(op)

    def_op("INVOKE_METHOD", 158)
    hasconst.append(158)

    def_op("LOAD_FIELD", 159)
    hasconst.append(159)
    def_op("STORE_FIELD", 166)
    hasconst.append(166)

    def_op("BUILD_CHECKED_LIST", 168)
    hasconst.append(168)
    def_op("LOAD_TYPE", 169)
    hasconst.append(169)

    def_op("CAST", 170)
    hasconst.append(170)

    def_op("LOAD_LOCAL", 171)
    hasconst.append(171)
    def_op("STORE_LOCAL", 172)
    hasconst.append(172)

    def_op("PRIMITIVE_BOX", 174)

    jabs_op("POP_JUMP_IF_ZERO", 175)
    jabs_op("POP_JUMP_IF_NONZERO", 176)

    def_op("PRIMITIVE_UNBOX", 177)

    def_op("PRIMITIVE_BINARY_OP", 178)
    def_op("PRIMITIVE_UNARY_OP", 179)
    def_op("PRIMITIVE_COMPARE_OP", 180)
    def_op("LOAD_ITERABLE_ARG", 181)
    def_op("LOAD_MAPPING_ARG", 182)
    def_op("INVOKE_FUNCTION", 183)
    hasconst.append(183)

    jabs_op("JUMP_IF_ZERO_OR_POP", 184)
    jabs_op("JUMP_IF_NONZERO_OR_POP", 185)

    def_op("FAST_LEN", 186)
    def_op("CONVERT_PRIMITIVE", 187)

    def_op("LOAD_CLASS", 190)
    hasconst.append(190)

    def_op("INVOKE_NATIVE", 189)
    hasconst.append(189)

    def_op("BUILD_CHECKED_MAP", 191)
    hasconst.append(191)

    def_op("SEQUENCE_GET", 192)
    def_op("SEQUENCE_SET", 193)
    def_op("LIST_DEL", 194)
    def_op("REFINE_TYPE", 195)
    hasconst.append(195)
    def_op("PRIMITIVE_LOAD_CONST", 196)
    hasconst.append(196)
    def_op("RETURN_PRIMITIVE", 197)
    def_op("TP_ALLOC", 200)
    hasconst.append(200)

    shadow_op("LOAD_METHOD_UNSHADOWED_METHOD", 205)
    shadow_op("LOAD_METHOD_TYPE_METHODLIKE", 206)
    shadow_op("BUILD_CHECKED_LIST_CACHED", 207)
    shadow_op("TP_ALLOC_CACHED", 208)
    shadow_op("LOAD_ATTR_S_MODULE", 209)
    shadow_op("LOAD_METHOD_S_MODULE", 210)
    shadow_op("INVOKE_FUNCTION_CACHED", 211)
    shadow_op("INVOKE_FUNCTION_INDIRECT_CACHED", 212)
    shadow_op("BUILD_CHECKED_MAP_CACHED", 213)

    shadow_op("PRIMITIVE_STORE_FAST", 215)
    shadow_op("CAST_CACHED_OPTIONAL", 216)
    shadow_op("CAST_CACHED", 217)
    shadow_op("CAST_CACHED_EXACT", 218)
    shadow_op("CAST_CACHED_OPTIONAL_EXACT", 219)
    shadow_op("LOAD_PRIMITIVE_FIELD", 220)
    shadow_op("STORE_PRIMITIVE_FIELD", 221)
    shadow_op("LOAD_OBJ_FIELD", 222)
    shadow_op("STORE_OBJ_FIELD", 223)

    shadow_op("INVOKE_METHOD_CACHED", 224)
    shadow_op("BINARY_SUBSCR_TUPLE_CONST_INT", 225)
    shadow_op("BINARY_SUBSCR_DICT_STR", 226)
    shadow_op("BINARY_SUBSCR_LIST", 227)
    shadow_op("BINARY_SUBSCR_TUPLE", 228)
    shadow_op("BINARY_SUBSCR_DICT", 229)

    shadow_op("LOAD_METHOD_UNCACHABLE", 230)
    shadow_op("LOAD_METHOD_MODULE", 231)
    shadow_op("LOAD_METHOD_TYPE", 232)
    shadow_op("LOAD_METHOD_SPLIT_DICT_DESCR", 233)
    shadow_op("LOAD_METHOD_SPLIT_DICT_METHOD", 234)
    shadow_op("LOAD_METHOD_DICT_DESCR", 235)
    shadow_op("LOAD_METHOD_DICT_METHOD", 236)
    shadow_op("LOAD_METHOD_NO_DICT_METHOD", 237)
    shadow_op("LOAD_METHOD_NO_DICT_DESCR", 238)

    shadow_op("STORE_ATTR_SLOT", 239)
    shadow_op("STORE_ATTR_SPLIT_DICT", 240)
    shadow_op("STORE_ATTR_DESCR", 241)
    shadow_op("STORE_ATTR_UNCACHABLE", 242)
    shadow_op("STORE_ATTR_DICT", 243)

    shadow_op("LOAD_ATTR_POLYMORPHIC", 244)
    shadow_op("LOAD_ATTR_SLOT", 245)
    shadow_op("LOAD_ATTR_MODULE", 246)
    shadow_op("LOAD_ATTR_TYPE", 247)
    shadow_op("LOAD_ATTR_SPLIT_DICT_DESCR", 248)
    shadow_op("LOAD_ATTR_SPLIT_DICT", 249)
    shadow_op("LOAD_ATTR_DICT_NO_DESCR", 250)
    shadow_op("LOAD_ATTR_NO_DICT_DESCR", 251)
    shadow_op("LOAD_ATTR_DICT_DESCR", 252)
    shadow_op("LOAD_ATTR_UNCACHABLE", 253)

    shadow_op("LOAD_GLOBAL_CACHED", 254)
    shadow_op("SHADOW_NOP", 255)
