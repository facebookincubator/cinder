
"""
opcode module - potentially shared between dis and other modules which
operate on bytecodes (e.g. peephole optimizers).
"""

__all__ = ["cmp_op", "hasconst", "hasname", "hasjrel", "hasjabs",
           "haslocal", "hascompare", "hasfree", "opname", "opmap",
           "HAVE_ARGUMENT", "EXTENDED_ARG", "hasnargs"]

# It's a chicken-and-egg I'm afraid:
# We're imported before _opcode's made.
# With exception unheeded
# (stack_effect is not needed)
# Both our chickens and eggs are allayed.
#     --Larry Hastings, 2013/11/23

try:
    from _opcode import stack_effect
    __all__.append('stack_effect')
except ImportError:
    pass

cmp_op = ('<', '<=', '==', '!=', '>', '>=')

hasconst = []
hasname = []
hasjrel = []
hasjabs = []
haslocal = []
hascompare = []
hasfree = []
hasnargs = [] # unused
shadowop = set()

opmap = {}
opname = ['<%r>' % (op,) for op in range(256)]

def def_op(name, op):
    opname[op] = name
    opmap[name] = op

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


# Instruction opcodes for compiled code
# Blank lines correspond to available opcodes

def_op('POP_TOP', 1)
def_op('ROT_TWO', 2)
def_op('ROT_THREE', 3)
def_op('DUP_TOP', 4)
def_op('DUP_TOP_TWO', 5)
def_op('ROT_FOUR', 6)

def_op('NOP', 9)
def_op('UNARY_POSITIVE', 10)
def_op('UNARY_NEGATIVE', 11)
def_op('UNARY_NOT', 12)

def_op('UNARY_INVERT', 15)
def_op('BINARY_MATRIX_MULTIPLY', 16)
def_op('INPLACE_MATRIX_MULTIPLY', 17)

def_op('BINARY_POWER', 19)
def_op('BINARY_MULTIPLY', 20)

def_op('BINARY_MODULO', 22)
def_op('BINARY_ADD', 23)
def_op('BINARY_SUBTRACT', 24)
def_op('BINARY_SUBSCR', 25)
def_op('BINARY_FLOOR_DIVIDE', 26)
def_op('BINARY_TRUE_DIVIDE', 27)
def_op('INPLACE_FLOOR_DIVIDE', 28)
def_op('INPLACE_TRUE_DIVIDE', 29)
def_op('GET_LEN', 30)
def_op('MATCH_MAPPING', 31)
def_op('MATCH_SEQUENCE', 32)
def_op('MATCH_KEYS', 33)
def_op('COPY_DICT_WITHOUT_KEYS', 34)

def_op('WITH_EXCEPT_START', 49)
def_op('GET_AITER', 50)
def_op('GET_ANEXT', 51)
def_op('BEFORE_ASYNC_WITH', 52)

def_op('END_ASYNC_FOR', 54)
def_op('INPLACE_ADD', 55)
def_op('INPLACE_SUBTRACT', 56)
def_op('INPLACE_MULTIPLY', 57)

def_op('INPLACE_MODULO', 59)
def_op('STORE_SUBSCR', 60)
def_op('DELETE_SUBSCR', 61)
def_op('BINARY_LSHIFT', 62)
def_op('BINARY_RSHIFT', 63)
def_op('BINARY_AND', 64)
def_op('BINARY_XOR', 65)
def_op('BINARY_OR', 66)
def_op('INPLACE_POWER', 67)
def_op('GET_ITER', 68)
def_op('GET_YIELD_FROM_ITER', 69)
def_op('PRINT_EXPR', 70)
def_op('LOAD_BUILD_CLASS', 71)
def_op('YIELD_FROM', 72)
def_op('GET_AWAITABLE', 73)
def_op('LOAD_ASSERTION_ERROR', 74)
def_op('INPLACE_LSHIFT', 75)
def_op('INPLACE_RSHIFT', 76)
def_op('INPLACE_AND', 77)
def_op('INPLACE_XOR', 78)
def_op('INPLACE_OR', 79)

def_op('LIST_TO_TUPLE', 82)
def_op('RETURN_VALUE', 83)
def_op('IMPORT_STAR', 84)
def_op('SETUP_ANNOTATIONS', 85)
def_op('YIELD_VALUE', 86)
def_op('POP_BLOCK', 87)

def_op('POP_EXCEPT', 89)

HAVE_ARGUMENT = 90              # Opcodes from here have an argument:

name_op('STORE_NAME', 90)       # Index in name list
name_op('DELETE_NAME', 91)      # ""
def_op('UNPACK_SEQUENCE', 92)   # Number of tuple items
jrel_op('FOR_ITER', 93)
def_op('UNPACK_EX', 94)
name_op('STORE_ATTR', 95)       # Index in name list
name_op('DELETE_ATTR', 96)      # ""
name_op('STORE_GLOBAL', 97)     # ""
name_op('DELETE_GLOBAL', 98)    # ""
def_op('ROT_N', 99)
def_op('LOAD_CONST', 100)       # Index in const list
hasconst.append(100)
name_op('LOAD_NAME', 101)       # Index in name list
def_op('BUILD_TUPLE', 102)      # Number of tuple items
def_op('BUILD_LIST', 103)       # Number of list items
def_op('BUILD_SET', 104)        # Number of set items
def_op('BUILD_MAP', 105)        # Number of dict entries
name_op('LOAD_ATTR', 106)       # Index in name list
def_op('COMPARE_OP', 107)       # Comparison operator
hascompare.append(107)
name_op('IMPORT_NAME', 108)     # Index in name list
name_op('IMPORT_FROM', 109)     # Index in name list
jrel_op('JUMP_FORWARD', 110)    # Number of bytes to skip
jabs_op('JUMP_IF_FALSE_OR_POP', 111) # Target byte offset from beginning of code
jabs_op('JUMP_IF_TRUE_OR_POP', 112)  # ""
jabs_op('JUMP_ABSOLUTE', 113)        # ""
jabs_op('POP_JUMP_IF_FALSE', 114)    # ""
jabs_op('POP_JUMP_IF_TRUE', 115)     # ""
name_op('LOAD_GLOBAL', 116)     # Index in name list
def_op('IS_OP', 117)
def_op('CONTAINS_OP', 118)
def_op('RERAISE', 119)

jabs_op('JUMP_IF_NOT_EXC_MATCH', 121)
jrel_op('SETUP_FINALLY', 122)   # Distance to target address

def_op('LOAD_FAST', 124)        # Local variable number
haslocal.append(124)
def_op('STORE_FAST', 125)       # Local variable number
haslocal.append(125)
def_op('DELETE_FAST', 126)      # Local variable number
haslocal.append(126)

def_op('GEN_START', 129)        # Kind of generator/coroutine
def_op('RAISE_VARARGS', 130)    # Number of raise arguments (1, 2, or 3)
def_op('CALL_FUNCTION', 131)    # #args
def_op('MAKE_FUNCTION', 132)    # Flags
def_op('BUILD_SLICE', 133)      # Number of items

def_op('LOAD_CLOSURE', 135)
hasfree.append(135)
def_op('LOAD_DEREF', 136)
hasfree.append(136)
def_op('STORE_DEREF', 137)
hasfree.append(137)
def_op('DELETE_DEREF', 138)
hasfree.append(138)

def_op('CALL_FUNCTION_KW', 141)  # #args + #kwargs
def_op('CALL_FUNCTION_EX', 142)  # Flags
jrel_op('SETUP_WITH', 143)
def_op('EXTENDED_ARG', 144)
EXTENDED_ARG = 144
def_op('LIST_APPEND', 145)
def_op('SET_ADD', 146)
def_op('MAP_ADD', 147)
def_op('LOAD_CLASSDEREF', 148)
hasfree.append(148)

def_op('MATCH_CLASS', 152)

jrel_op('SETUP_ASYNC_WITH', 154)
def_op('FORMAT_VALUE', 155)
def_op('BUILD_CONST_KEY_MAP', 156)
def_op('BUILD_STRING', 157)

name_op('LOAD_METHOD', 160)
def_op('CALL_METHOD', 161)
def_op('LIST_EXTEND', 162)
def_op('SET_UPDATE', 163)
def_op('DICT_MERGE', 164)
def_op('DICT_UPDATE', 165)

# Cinder-specific opcodes

def_op("INVOKE_METHOD", 158)
hasconst.append(158)

def_op("LOAD_FIELD", 159)
hasconst.append(159)
def_op("STORE_FIELD", 166)
hasconst.append(166)

def_op("SEQUENCE_REPEAT", 167)

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

def_op("CHECK_ARGS", 188)
hasconst.append(188)

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
def_op("LOAD_METHOD_SUPER", 198)
hasconst.append(198)
def_op("LOAD_ATTR_SUPER", 199)
hasconst.append(199)
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
shadow_op("CHECK_ARGS_CACHED", 214)
shadow_op("PRIMITIVE_STORE_FAST", 215)
shadow_op("CAST_CACHED_OPTIONAL", 216)
shadow_op("CAST_CACHED", 217)
shadow_op("CAST_CACHED_EXACT", 218)
shadow_op("CAST_CACHED_OPTIONAL_EXACT", 219)
shadow_op("LOAD_PRIMITIVE_FIELD", 220)
shadow_op("STORE_PRIMITIVE_FIELD", 221)
shadow_op("LOAD_OBJ_FIELD", 222)
shadow_op("STORE_OBJ_FIELD", 223)

shadow_op('INVOKE_METHOD_CACHED', 224)
shadow_op('BINARY_SUBSCR_TUPLE_CONST_INT', 225)
shadow_op('BINARY_SUBSCR_DICT_STR', 226)
shadow_op('BINARY_SUBSCR_LIST', 227)
shadow_op('BINARY_SUBSCR_TUPLE', 228)
shadow_op('BINARY_SUBSCR_DICT', 229)

shadow_op('LOAD_METHOD_UNCACHABLE', 230)
shadow_op('LOAD_METHOD_MODULE', 231)
shadow_op('LOAD_METHOD_TYPE', 232)
shadow_op('LOAD_METHOD_SPLIT_DICT_DESCR', 233)
shadow_op('LOAD_METHOD_SPLIT_DICT_METHOD', 234)
shadow_op('LOAD_METHOD_DICT_DESCR', 235)
shadow_op('LOAD_METHOD_DICT_METHOD', 236)
shadow_op('LOAD_METHOD_NO_DICT_METHOD', 237)
shadow_op('LOAD_METHOD_NO_DICT_DESCR', 238)

shadow_op('STORE_ATTR_SLOT', 239)
shadow_op('STORE_ATTR_SPLIT_DICT', 240)
shadow_op('STORE_ATTR_DESCR', 241)
shadow_op('STORE_ATTR_UNCACHABLE', 242)
shadow_op('STORE_ATTR_DICT', 243)

shadow_op('LOAD_ATTR_POLYMORPHIC', 244)
shadow_op('LOAD_ATTR_SLOT', 245)
shadow_op('LOAD_ATTR_MODULE', 246)
shadow_op('LOAD_ATTR_TYPE', 247)
shadow_op('LOAD_ATTR_SPLIT_DICT_DESCR', 248)
shadow_op('LOAD_ATTR_SPLIT_DICT', 249)
shadow_op('LOAD_ATTR_DICT_NO_DESCR', 250)
shadow_op('LOAD_ATTR_NO_DICT_DESCR', 251)
shadow_op('LOAD_ATTR_DICT_DESCR', 252)
shadow_op('LOAD_ATTR_UNCACHABLE', 253)

shadow_op('LOAD_GLOBAL_CACHED', 254)
shadow_op('SHADOW_NOP', 255)

del def_op, name_op, jrel_op, jabs_op
