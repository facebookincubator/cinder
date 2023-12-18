# Portions copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)
# pyre-unsafe

import ast
from typing import Any, Callable, Dict, List, Optional, Type


PR_TUPLE = 0
PR_TEST = 1  # 'if'-'else', 'lambda'
PR_OR = 2  # 'or'
PR_AND = 3  # 'and'
PR_NOT = 4  # 'not'
PR_CMP = 5  # '<', '>', '==', '>=', '<=', '!=' 'in', 'not in', 'is', 'is not'
PR_EXPR = 6
PR_BOR = PR_EXPR  # '|'
PR_BXOR = 7  # '^'
PR_BAND = 8  # '&'
PR_SHIFT = 9  # '<<', '>>'
PR_ARITH = 10  # '+', '-'
PR_TERM = 11  # '*', '@', '/', '%', '//'
PR_FACTOR = 12  # unary '+', '-', '~'
PR_POWER = 13  # '**'
PR_AWAIT = 14  # 'await'
PR_ATOM = 15


def get_op(node: ast.cmpop) -> str:
    if isinstance(node, ast.Is):
        return " is "
    elif isinstance(node, ast.IsNot):
        return " is not "
    elif isinstance(node, ast.In):
        return " in "
    elif isinstance(node, ast.NotIn):
        return " not in "
    elif isinstance(node, ast.Lt):
        return " < "
    elif isinstance(node, ast.Gt):
        return " > "
    elif isinstance(node, ast.LtE):
        return " <= "
    elif isinstance(node, ast.GtE):
        return " >= "
    elif isinstance(node, ast.Eq):
        return " == "
    elif isinstance(node, ast.NotEq):
        return " != "
    else:
        return "unknown op: " + type(node).__name__


def get_unop(node: ast.unaryop) -> str:
    if isinstance(node, ast.UAdd):
        return "+"
    elif isinstance(node, ast.USub):
        return "-"
    elif isinstance(node, ast.Not):
        return "not "
    elif isinstance(node, ast.Invert):
        return "~"
    return "<unknown unary op>"


def _format_name(node: ast.Name, level: int) -> str:
    return node.id


def _format_compare(node: ast.Compare, level: int) -> str:
    return parens(
        level,
        PR_CMP,
        to_expr(node.left, PR_CMP + 1)
        + "".join(
            (
                get_op(op) + to_expr(comp, PR_CMP + 1)
                for comp, op in zip(node.comparators, node.ops)
            )
        ),
    )


def _format_nameconstant(node: ast.NameConstant, level: int) -> str:
    if node.value is None:
        return "None"
    elif node.value is True:
        return "True"
    elif node.value is False:
        return "False"
    return "<unknown constant>"


def _format_num(node: ast.Num, level: int) -> str:
    return repr(node.n)


def _format_str(node: ast.Str, level: int) -> str:
    return repr(node.s)


def _format_attribute(node: ast.Attribute, level: int) -> str:
    value = to_expr(node.value, PR_ATOM)
    const = node.value
    if (isinstance(const, ast.Constant) and isinstance(const.value, int)) or type(
        const
    ) is ast.Num:
        value += " ."
    else:
        value += "."
    return value + node.attr


def _format_tuple(node: ast.Tuple, level: int) -> str:
    if not node.elts:
        return "()"
    elif len(node.elts) == 1:
        return parens(level, PR_TUPLE, to_expr(node.elts[0]) + ",")

    return parens(level, PR_TUPLE, ", ".join(to_expr(elm) for elm in node.elts))


def _format_list(node: ast.List, level: int) -> str:
    return "[" + ", ".join(to_expr(elm) for elm in node.elts) + "]"


def _format_kw(node: ast.keyword):
    if node.arg:
        return f"{node.arg}={to_expr(node.value)}"
    return f"**{to_expr(node.value)}"
    pass


def _format_call(node: ast.Call, level: int) -> str:
    args = [to_expr(arg) for arg in node.args] + [
        _format_kw(arg) for arg in node.keywords
    ]
    return to_expr(node.func, PR_TEST) + "(" + ", ".join(args) + ")"


def _format_unaryop(node: ast.UnaryOp, level: int) -> str:
    tgt_level = PR_FACTOR
    if isinstance(node.op, ast.Not):
        tgt_level = PR_NOT
    return parens(
        level, tgt_level, get_unop(node.op) + to_expr(node.operand, tgt_level)
    )


BIN_OPS = {
    ast.Add: (" + ", PR_ARITH),
    ast.Sub: (" - ", PR_ARITH),
    ast.Mult: (" * ", PR_TERM),
    ast.MatMult: (" @ ", PR_TERM),
    ast.Div: (" / ", PR_TERM),
    ast.Mod: (" % ", PR_TERM),
    ast.LShift: (" << ", PR_SHIFT),
    ast.RShift: (" >> ", PR_SHIFT),
    ast.BitOr: (" | ", PR_BOR),
    ast.BitXor: (" ^ ", PR_BXOR),
    ast.BitAnd: (" & ", PR_BAND),
    ast.FloorDiv: (" // ", PR_TERM),
    ast.Pow: (" ** ", PR_POWER),
}


def _format_binaryop(node: ast.BinOp, level: int) -> str:
    tgt_level = PR_FACTOR

    # pyre-fixme[6]: Expected `Type[typing.Union[_ast.Add, _ast.BitAnd, _ast.BitOr,
    #  _ast.BitXor, _ast.Div, _ast.FloorDiv, _ast.LShift, _ast.MatMult, _ast.Mod,
    #  _ast.Mult, _ast.Pow, _ast.RShift, _ast.Sub]]` for 1st param but got
    #  `Type[_ast.operator]`.
    op, tgt_level = BIN_OPS[type(node.op)]
    rassoc = 0
    if isinstance(node.op, ast.Pow):
        rassoc = 1
    return parens(
        level,
        tgt_level,
        to_expr(node.left, tgt_level + rassoc)
        + op
        + to_expr(node.right, tgt_level + (1 - rassoc)),
    )


def _format_subscript(node: ast.Subscript, level: int) -> str:
    return f"{to_expr(node.value, PR_ATOM)}[{to_expr(node.slice, PR_TUPLE)}]"


def _format_yield(node: ast.Yield, level: int) -> str:
    raise SyntaxError("'yield expression' can not be used within an annotation")


def _format_yield_from(node: ast.YieldFrom, level: int) -> str:
    raise SyntaxError("'yield expression' can not be used within an annotation")


def _format_dict(node: ast.Dict, level: int) -> str:
    return (
        "{"
        + ", ".join(
            to_expr(k) + ": " + to_expr(v) for k, v in zip(node.keys, node.values)
        )
        + "}"
    )


def _format_comprehension(node: ast.comprehension) -> str:
    header = " for "
    if node.is_async:
        header = " async for "

    res = (
        header
        + to_expr(node.target, PR_TUPLE)
        + " in "
        + to_expr(node.iter, PR_TEST + 1)
    )
    for if_ in node.ifs:
        res += " if " + to_expr(if_, PR_TEST + 1)
    return res


def parens(level: int, target_lvl: int, value: str) -> str:
    if level > target_lvl:
        return f"({value})"
    return value


def _format_await(node: ast.Await, level: int):
    return parens(level, PR_AWAIT, "await " + to_expr(node.value, PR_ATOM))


def _format_starred(node: ast.Starred, level: int):
    return "*" + to_expr(node.value, PR_EXPR)


def _format_boolop(node: ast.BoolOp, level: int) -> str:
    if isinstance(node.op, ast.And):
        name = " and "
        tgt_level = PR_AND
    else:
        name = " or "
        tgt_level = PR_OR

    return parens(
        level, tgt_level, name.join(to_expr(n, tgt_level + 1) for n in node.values)
    )


def _format_arguments(node: ast.arguments) -> str:
    res = []
    for i, arg in enumerate(node.args):
        if i:
            res.append(", ")
        res.append(arg.arg)
        if i < len(node.defaults):
            res.append("=")
            res.append(to_expr(node.defaults[i]))

    if node.vararg or node.kwonlyargs:
        if node.args:
            res.append(", ")
        res.append("*")
        vararg = node.vararg
        if vararg:
            res.append(vararg.arg)

    for i, arg in enumerate(node.kwonlyargs):
        if res:
            res.append(", ")
        res.append(arg.arg)
        if i < len(node.kw_defaults) and node.kw_defaults[i]:
            res.append("=")
            res.append(to_expr(node.kw_defaults[i]))

    return "".join(res)


def _format_lambda(node: ast.Lambda, level: int) -> str:
    value = "lambda "
    if not node.args.args:
        value = "lambda"
    value += _format_arguments(node.args)
    value += ": " + to_expr(node.body, PR_TEST)

    return parens(level, PR_TEST, value)


def _format_if_exp(node: ast.IfExp, level: int) -> str:
    body = to_expr(node.body, PR_TEST + 1)
    orelse = to_expr(node.orelse, PR_TEST)
    test = to_expr(node.test, PR_TEST + 1)
    return parens(level, PR_TEST, f"{body} if {test} else {orelse}")


def _format_set(node: ast.Set, level: int) -> str:
    return "{" + ", ".join(to_expr(elt, PR_TEST) for elt in node.elts) + "}"


def _format_comprehensions(nodes: List[ast.comprehension]) -> str:
    return "".join(_format_comprehension(n) for n in nodes)


def _format_set_comp(node: ast.SetComp, level: int) -> str:
    return "{" + to_expr(node.elt) + _format_comprehensions(node.generators) + "}"


def _format_list_comp(node: ast.ListComp, level: int) -> str:
    return "[" + to_expr(node.elt) + _format_comprehensions(node.generators) + "]"


def _format_dict_comp(node: ast.DictComp, level: int) -> str:
    return (
        "{"
        + to_expr(node.key)
        + ": "
        + to_expr(node.value)
        + _format_comprehensions(node.generators)
        + "}"
    )


def _format_gen_exp(node: ast.GeneratorExp, level: int) -> str:
    return "(" + to_expr(node.elt) + _format_comprehensions(node.generators) + ")"


def format_fstring_elt(res: List[str], elt: ast.expr, is_format_spec: bool):
    if isinstance(elt, ast.Str):
        res.append(elt.s)
    elif isinstance(elt, ast.Constant):
        res.append(elt.value)
    elif isinstance(elt, ast.JoinedStr):
        res.append(format_joinedstr(elt, PR_TEST, is_format_spec))
    elif isinstance(elt, ast.FormattedValue):
        expr = to_expr(elt.value, PR_TEST + 1)
        if expr.startswith("{"):
            # Expression starts with a brace, we need an extra space
            res.append("{ ")
        else:
            res.append("{")
        res.append(expr)
        conversion = elt.conversion
        if conversion is not None and conversion != -1:
            res.append("!")
            res.append(chr(conversion))
        format_spec = elt.format_spec
        if format_spec is not None:
            res.append(":")
            format_fstring_elt(res, format_spec, True)
        res.append("}")


def format_joinedstr(node: ast.JoinedStr, level: int, is_format_spec=False) -> str:
    res = []
    for elt in node.values:
        format_fstring_elt(res, elt, is_format_spec)

    joined = "".join(res)
    if is_format_spec:
        return joined
    return f"f{repr(joined)}"


def _format_slice(node: ast.Slice, level: int):
    res = ""
    if node.lower is not None:
        res += to_expr(node.lower)
    res += ":"
    if node.upper is not None:
        res += to_expr(node.upper)
    if node.step:
        res += ":"
        res += to_expr(node.step)
    return res


def _format_constant(node: ast.Constant, level: int):
    if node.value is Ellipsis:
        return "..."

    return repr(node.value)


_FORMATTERS: Dict[Type, Callable[[Any, int], str]] = {
    ast.BoolOp: _format_boolop,
    ast.BinOp: _format_binaryop,
    ast.UnaryOp: _format_unaryop,
    ast.Lambda: _format_lambda,
    ast.IfExp: _format_if_exp,
    ast.Dict: _format_dict,
    ast.Set: _format_set,
    ast.GeneratorExp: _format_gen_exp,
    ast.ListComp: _format_list_comp,
    ast.SetComp: _format_set_comp,
    ast.DictComp: _format_dict_comp,
    ast.Yield: _format_yield,
    ast.YieldFrom: _format_yield_from,
    ast.Await: _format_await,
    ast.Compare: _format_compare,
    ast.Call: _format_call,
    ast.Constant: _format_constant,
    ast.Num: _format_num,
    ast.Str: _format_str,
    ast.JoinedStr: format_joinedstr,
    ast.FormattedValue: None,
    ast.Bytes: lambda node, level: repr(node.s),
    ast.Ellipsis: lambda node, level: "...",
    ast.NameConstant: _format_nameconstant,
    ast.Attribute: _format_attribute,
    ast.Subscript: _format_subscript,
    ast.Starred: _format_starred,
    ast.Name: _format_name,
    ast.List: _format_list,
    ast.Tuple: _format_tuple,
    ast.Slice: _format_slice,
}


def to_expr(node: Optional[ast.AST], level=PR_TEST) -> str:
    if node is None:
        return ""
    formatter = _FORMATTERS.get(type(node))
    if formatter is not None:
        return formatter(node, level)

    return "<unsupported node: " + type(node).__name__ + ">"
