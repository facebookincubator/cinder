from __future__ import annotations

import inspect
from typing import Iterable, Mapping, Sequence, Tuple, Type

from .type_code import set_type_code, TYPED_INT64


def eq_method(self: Enum, other: Enum) -> bool:
    return self.value == other or (
        getattr(other, "value", None) is not None and self.value == other.value
    )


def hash_method(self: Enum) -> int:
    return hash(self.value)


TYPE_NEW = type.__new__
SENTINEL = object()


class EnumMeta(type):
    @staticmethod
    def is_hashable(obj: object) -> bool:
        try:
            hash(obj)
            return True
        except TypeError:
            return False

    def __new__(
        cls, classname: str, parents: Tuple[type, ...], dct: Mapping[str, object]
    ) -> Type[Enum]:
        attributes = {}
        members = {}

        for name, value in dct.items():
            if (
                name.startswith("_")
                or inspect.isroutine(value)
                or inspect.isdatadescriptor(value)
            ):
                attributes[name] = value
            else:
                members[name] = value

        attributes.update({"__members__": {}, "__reversed_map__": {}})
        if len(members) != len(
            {members[k] for k in members if EnumMeta.is_hashable(members[k])}
        ):
            attributes["__eq__"] = eq_method
            attributes["__hash__"] = hash_method

        klass = super(EnumMeta, cls).__new__(cls, classname, parents, attributes)

        for name, value in members.items():
            option = klass(name=name, value=value)
            klass.__members__[name] = option
            if EnumMeta.is_hashable(option):
                klass.__reversed_map__[option] = option
            if EnumMeta.is_hashable(value):
                klass.__reversed_map__[value] = option
            setattr(klass, name, option)
        return klass

    def __len__(self) -> int:
        return len(self.__members__)

    def __getitem__(self, attribute: str) -> Enum:
        return self.__members__[attribute]

    def __iter__(self) -> Iterable[Enum]:
        return iter(self.__members__.values())

    def __call__(self, *args, **kwargs) -> Enum:
        if len(args) == 1:
            attribute = args[0]
            return self._get_by_value(attribute)

        name = kwargs["name"]
        value = kwargs["value"]
        instance = self.__new__(self, value)
        instance.name = name
        instance.value = value
        return instance

    def _get_by_value(self, value: str) -> Enum:
        res = self.__reversed_map__.get(value, SENTINEL)
        if res is not SENTINEL:
            return res

        raise ValueError(
            f"Enum type {self.__name__} has no attribute with value {value!r}"
        )


class Enum(metaclass=EnumMeta):
    def __init__(self, value: object) -> None:
        self.value = value

    def __dir__(self) -> Sequence[str]:
        return ["name", "value"]

    def __str__(self) -> str:
        return f"{type(self).__name__}.{self.name}"

    def __repr__(self) -> str:
        return f"<{type(self).__name__}.{self.name}: {self.value}>"

    def __reduce_ex__(self, proto: int) -> Tuple[Type[object], Tuple[object]]:
        return self.__class__, (self.value,)


class IntEnum(Enum, int):
    pass


class StringEnumMeta(EnumMeta):
    """Like the regular EnumMeta, but parses string/binary inputs to __call__
    as text (to match text literals used in StringEnum)."""

    def _get_by_value(self, attribute: str | bytes) -> StringEnum:
        return super(StringEnumMeta, self)._get_by_value(
            attribute.decode("utf-8") if isinstance(attribute, bytes) else attribute
        )


class StringEnum(Enum, str, metaclass=StringEnumMeta):
    def __str__(self) -> str:
        return f"{self.value}"


def unique(enumeration: Type[Enum]) -> Type[Enum]:
    """
    Class decorator for enumerations ensuring unique member values
    """
    duplicates = []
    for name, member in enumeration.__members__.items():
        if name != member.name:
            duplicates.append((name, member.name))
    if duplicates:
        raise ValueError(f"duplicate values found in {enumeration!r}")
    return enumeration
