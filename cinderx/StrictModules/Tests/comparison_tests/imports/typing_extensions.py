# Copyright (c) Meta Platforms, Inc. and affiliates.
from typing import _SpecialForm, _type_check, _GenericAlias, Protocol


def _dict_new(cls, *args, **kwargs):
    return dict(*args, **kwargs)


def _typeddict_new(cls, typename, fields=None, *, total=True, **kwargs):
    if fields is None:
        fields = kwargs
    elif kwargs:
        raise TypeError(
            "TypedDict takes either a dict or keyword arguments," " but not both"
        )

    ns = {"__annotations__": dict(fields), "__total__": total}
    return _TypedDictMeta(typename, (), ns)


def _check_fails(cls, other):
    raise TypeError("TypedDict does not support instance and class checks")


class _TypedDictMeta(type):
    def __new__(cls, name, bases, ns, total=True):
        ns["__new__"] = _typeddict_new if name == "TypedDict" else _dict_new
        tp_dict = super(_TypedDictMeta, cls).__new__(cls, name, (dict,), ns)

        anns = ns.get("__annotations__", {})
        msg = "TypedDict('Name', {f0: t0, f1: t1, ...}); each t must be a type"
        anns = {n: _type_check(tp, msg) for n, tp in anns.items()}
        required = set(anns if total else ())
        optional = set(() if total else anns)

        for base in bases:
            base_anns = base.__dict__.get("__annotations__", {})
            anns.update(base_anns)
            if getattr(base, "__total__", True):
                required.update(base_anns)
            else:
                optional.update(base_anns)

        tp_dict.__annotations__ = anns
        tp_dict.__required_keys__ = frozenset(required)
        tp_dict.__optional_keys__ = frozenset(optional)
        if not hasattr(tp_dict, "__total__"):
            tp_dict.__total__ = total
        return tp_dict

    __instancecheck__ = __subclasscheck__ = _check_fails


class TypedDict(dict, metaclass=_TypedDictMeta):
    pass


class _LiteralForm(_SpecialForm, _root=True):
    __module__ = "typing_extensions"

    def __repr__(self):
        return "typing_extensions." + self._name

    def __getitem__(self, parameters):
        return _GenericAlias(self, parameters)


Literal = _LiteralForm("Literal", doc="")
