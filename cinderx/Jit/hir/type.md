# HIR Type System

## Overview

`jit::hir::Type` represents the type of an HIR value. This document is targeted at developers writing code dealing with HIR, to give an introduction to the types and operations available with `Type`.

## Terms

Some of these terms only make sense in the context of other terms, or in the larger context of `Type` as a whole. Don’t feel obligated to read and understand everything the first time through this section.

* *predefined type* - A type that is supported natively by `Type` and can be a member of arbitrary union types. Examples include `Long`, `Bytes`, `BaseException`, and `CInt32`.
* *primitive type* - A predefined type that represents a type for a primitive C value, rather than a Python object. Examples include `CInt32` (`int`) and `CBool` (`bool`).
* *lifetime/mortality* - Python objects in Cinder can be "immortal", which means they live forever and are not reference counted. The concept of an object being mortal or immortal is referred to as its lifetime or its mortality.
* *specialization* - Additional data added to a predefined `Type` to make it more specific.
* *type specialization* - A `PyTypeObject*` stored in a `Type`.
* *exact type specialization* - A type specialization flagged to exclude subtypes of the given `PyTypeObject*`.
* *object specialization* - A `PyObject*` stored in a `Type`. A `Type` with an object specialization is similar to *literal types* in other type systems.
* *primitive specialization* - A specialization for a primitive type, holding a C value (`int` for `CInt32`, `bool` for `CBool`, etc.). Like object specializations, primitive specializations create literal types (holding unboxed C values, rather than `PyObject*`s). At the moment, all primitive specializations are for integral/double types, but that may change in the future.

## Semantics

A `Type` has three parts: an arbitrary union of predefined types, lifetime information (whether an object is mortal, immortal, either, or neither), and an optional specialization. A specialization can be a `PyTypeObject*`, a `PyObject*`, or another C type (like `int` or `bool`). The set of values represented by a `Type` is the intersection of the union, the lifetime information, and the specialization.

Common set operations are supported, including equality/inequality (`==`/`!=`), subtype (`<=`), strict subtype (`<`), union (`|`), intersection (`&`), and subtraction (`-`). This is discussed in more detail later, but users of `Type` should be aware that when the result of a set operation cannot be expressed by `Type`'s internal representation, the returned `Type` will be slightly larger than the true result. This ensures that the JIT never thinks a value has a more precise type than we can prove.

### Notation

- Unions are written using `{Member1|Member2|...|MemberN}`.
- Specializations are indicated inside square brackets: `SomeType[Specialization]`.
- Exact type specializations are indicated with a `:Exact` suffix on the type name: `SomeType[Specialization:Exact]`).

### Predefined Types

Predefined `Type`s can be represented without using a specialization, and a `Type` can hold an arbitrary union of any of these types. The set of predefined `Type`s is similar to, but not exactly the same as, Python’s built-in types: some types, such as `NotImplementedType` and `ellipsis` do not have predefined `Type`s, and must be represented using specializations.

* `Top` is the set of all `Type`s and the top of the hierarchy.
* `Object` is the set of all Python-visible object types.
* `MortalFoo` is all objects in `Foo` that are mortal.
* `ImmortalFoo` is all objects in `Foo` that are immortal.
* `FooExact` represents *exactly* `Foo`, excluding any subtypes. This is only defined for types that can be subclassed (excluding things like `Bool`, `Code`, `Frame`, etc.).
* `FooUser` represents user-defined types that inherit from `Foo` and no other built-in type (except for `Object`, which all types inherit from).
* `Primitive` is the set of all primitive types, which are only exposed to Python code in Static Python modules. They are also used to work with the return values of functions like `int PyObject_IsTrue(PyObject*)`.
*  `Bottom` is the empty type, with no values. It is a strict subtype of all other types.

The main hierarchy of predefined `Type`s is shown here (excluding `Bottom`):

```
Top
+-- Object
|   +-- ObjectExact
|   +-- ObjectUser
|   +-- BaseException
|   |   +-- BaseExceptionExact
|   |   +-- BaseExceptionUser
|   +-- Bytes
|   |   +-- BytesExact
|   |   +-- BytesUser
|   +-- Dict
|   |   +-- DictExact
|   |   +-- DictUser
|   +-- Float
|   |   +-- FloatExact
|   |   +-- FloatUser
|   +-- List
|   |   +-- ListExact
|   |   +-- ListUser
|   +-- Long
|   |   +-- LongExact
|   |   +-- LongUser
|   |   +-- Bool
|   +-- Tuple
|   |   +-- TupleExact
|   |   +-- TupleUser
|   +-- Type
|   |   +-- TypeExact
|   |   +-- TypeUser
|   +-- Unicode
|   |   +-- UnicodeExact
|   |   +-- UnicodeUser
|   +-- Code
|   +-- Frame
|   +-- Func
|   +-- NoneType
|   +-- Slice
+-- Primitive
    +-- Nullptr
    +-- CBool
    +-- CDouble
    +-- CInt8
    +-- CInt16
    +-- CInt32
    +-- CInt64
    +-- CUInt8
    +-- CUInt16
    +-- CUInt32
    +-- CUInt64
```

Mortality was not included in the chart above because it doesn't fit nicely into that textual representation. Every `Object` subtype can be mortal or immortal, and the mini-hierarchy for a type like `Dict` can be visualized in one of two ways:
```
+-- Dict
    +-- MortalDict
    |   +-- MortalDictExact
    |   +-- MortalDictUser
    +-- ImmortalDict
        +-- ImmortalDictExact
        +-- ImmortalDictUser
```

```
+-- Dict
    +-- DictExact
    |   +-- MortalDictExact
    |   +-- ImmortalDictExact
    +-- DictUser
        +-- MortalDictUser
        +-- ImmortalDictUser
```

There is also a predefined type named `User`, which represents all user-defined types (and doesn’t fit nicely into the above diagram). It exists to support multiple inheritance (explained in the next section).

```
User
+-- ObjectUser
+-- BaseExceptionUser
+-- BytesUser
+-- DictUser
+-- FloatUser
+-- ListUser
+-- LongUser
+-- TupleUser
+-- TypeUser
+-- UnicodeUser
```

Finally, nullability is explicitly represented in `Type`. While a `PyObject*` can be `nullptr`, `Object` represents a non-null `PyObject*`. This means that `{Object|Nullptr}` must be used as the type of a value that could be `nullptr` to indicate a raised exception, for example. Since this pattern is so pervasive, we also define `OptT` for every `Object` subtype `T`. `OptT` means "optional T" and is equal to `{T|Nullptr}`.

### Multiple Inheritance

`ObjectUser` represents all user-defined types that don’t have a non-`Object` predefined base type. However, these types can also have subtypes that are subtypes of other predefined types due to Python’s support for multiple inheritance. To handle this, the standard representation of a user-defined class `MyClass` is `User[MyClass]`. This allows for subtypes of `MyClass` to also be subtypes of `LongUser`, `BytesUser`, etc. At the same time, once a class has a non-object predefined base, representing it as `LongUser[MyInt]` or `BytesUser[MyBytes]` disallows further subtypes from also inheriting from a different predefined base, reflecting Python’s restrictions on incompatible base type layouts.

### Specialization

Given the following types and values:

```
class MyClass: pass
class MySubclass(MyClass): pass
class MyInt(int): pass
class MyClassInt(MyInt, MyClass): pass
class MyBytes(bytes): pass

my_obj = MyClass()
my_obj2 = MyClass()
my_subobj = MySubclass()
an_int = 5
my_int = MyInt()
my_class_int = MyClassInt()
a_bytes = b'hello'
my_bytes = MyBytes()
```

The following examples illustrate how various `Type`s relate to each other in the presence of specializations:

```
User[MyClass] < User
User[MyClass:Exact] < User[MyClass]
User[MySubclass] < User[MyClass]
!(User[MySubclass] < User[MyClass:Exact])
LongUser[MyInt] < LongUser
LongUser[MyClassInt] < User[MyClass]
LongUser[MyClassInt] < LongUser[MyInt]
BytesUser[MyBytes] < BytesUser

ObjectUser[my_obj] < User[MyClass]
ObjectUser[my_obj] < User[MyClass:Exact]
ObjectUser[my_subobj] < User[MyClass]
!(ObjectUser[my_subobj] < User[MyClass:Exact])
ObjectUser[my_subobj] < User[MySubclass]
ObjectUser[my_subobj] < User[MySubclass:Exact]

# Object specializations of predefined types always use an *Exact type,
# since they represent a specific instance of that type.
LongExact[an_int] < LongExact
LongUser[my_int] < LongUser
LongUser[my_int] < LongUser[MyInt]
LongUser[my_class_int] < LongUser[MyClassInt]
LongUser[my_class_int] < LongUser[MyInt]
LongUser[my_class_int] < User[MyClass]

BytesExact[a_bytes] < BytesExact
BytesUser[my_bytes] < BytesUser[MyBytes]

# Types that inherit both from MyClass and int:
User[MyClass] & Long == LongUser[MyClass]

ObjectUser[my_obj] & User[MyClass] == ObjectUser[my_obj]
ObjectUser[my_obj] & ObjectUser[my_obj2] == Bottom
ObjectUser[my_subobj] & ObjectUser[my_obj] == Bottom
ObjectUser[my_obj] & LongUser[my_int] == Bottom
```

All of the set operations are specialization-aware, with the important caveat that since a `Type` can only hold one specialization, some types cannot be represented in a `Type`.

### Representation limitations

**When the exact result of an operation cannot be represented, the result is the smallest `Type` that is a supertype of the actual result**. In practice, this usually means dropping a specialization or losing lifetime information. Clients must be aware of this restriction and take it into account. Most of the time, it simply means the type of a value is slightly wider than the best type we can prove, which client code should tolerate anyway.

Continuing from the examples in the previous section:

```
User[MyClass] | User[MySubclass] == User[MyClass]
User[MyClass] | LongUser[MyInt] == User

ObjectUser[my_obj] | User[MyClass] == User[MyClass]
ObjectUser[my_subobj] | User[MyClass] == User[MyClass]
ObjectUser[my_subobj] | User[MySubclass] == User[MySubclass]
ObjectUser[my_obj] | ObjectUser[my_obj] == ObjectUser[my_obj]
ObjectUser[my_obj] | ObjectUser[my_obj2] == ObjectUser[MyClass]
ObjectUser[my_subobj] | ObjectUser[my_obj] == ObjectUser[MyClass]
LongUser[my_class_int] | LongUser[MyInt] == LongUser[MyInt]
ObjectUser[my_obj] | LongUser[my_int] == {ObjectUser|LongUser}

User[MyClass] & User[MySubclass] == User[MySubclass]
LongUser[MyInt] & BytesUser[MyBytes] == Bottom

# We can't mix mortal and immortal types:
MortalDict | ImmortalLong == Dict | Long
{Dict|Long} - MortalDict == ImmortalDict | Long == {Dict|Long}
```

### Metaclasses

**Note**: This section on metaclasses uses `type` objects as object specializations. To  indicate this, object specializations in a `Type` subtype are given a `:obj` suffix.

Python classes may be instances of user-defined subtypes of `type`, using [metaclasses](https://docs.python.org/3/reference/datamodel.html#metaclasses). `Type` is a predefined type that can be specialized like any other type, which provides support for working with metaclasses. This includes the ability to give `Type` an object specialization.

Given the following types and values:

```
class Metaclass(type): pass
class MyClassWithMeta(metaclass=Metaclass): pass
meta_obj = MyClassWithMeta()
```

And the following `hir::Type`s:
* `TypeUser[Metaclass]`: `Metaclass` as a type: all objects with type `Metaclass` (or a subtype). Note that this does *not* include `meta_obj`, since `MyClassWithMeta` is an instance of `Metaclass`, not a subtype of it.
* `TypeExact[Metaclass:obj]`: The `Metaclass` object, as a normal object. Its only subtypes are itself and `Bottom`.
* `User[MyClassWithMeta]`: `MyClassWithMeta` as a type.
* `TypeUser[MyClassWithMeta:obj]`: `MyClassWithMeta` as an object. This is `TypeUser` because while `MyClassWithMeta` is a type, it's an instance of a user-defined `type` subtype, not `type` itself.

The following relationships hold:

```
TypeUser[MyClassWithMeta:obj] < TypeUser[Metaclass]
!(ObjectUser[meta_obj] <= TypeUser[Metaclass])
ObjectUser[meta_obj] < User[MyClassWithMeta]
```

### Intersections and multiple inheritance

Another consequence of multiple inheritance is that the intersection between two apparently-unrelated classes must be non-empty when they don’t have incompatible base classes. Using the same classes we defined above, we can see that the intersection of `User[MyClass]` and `LongUser[MyInt]` should be a supertype of `LongUser[MyClassInt]`, since `MyClassInt` inherits from both `MyClass` and `MyInt`. We can’t represent “types that inherit from both `MyClass` and `MyInt`", since a `Type` can only hold one specialization. We also can’t use `LongUser[MyClassInt]` as the intersection, because the result has to allow for more potential subtypes that don’t yet exist.

As with unions that can’t be represented in a single `Type`, we return the smallest `Type` that is a supertype of the actual result. In this case, however, there isn’t a unique `Type` that fits this requirement, since there’s no way to know whether `MyClass` (and its subtypes) or `MyInt` (and its subtypes) has a smaller cardinality. So, we can pick arbitrarily between `LongUser[MyClass]` and `LongUser[MyInt]`. Both are supertypes of all types that inherit from both `MyClass` and `MyInt`, and we cannot construct a suitable `Type` that is smaller than either one.

To ensure that the intersection operation remains commutative, we pick the type with the name that comes lexicographically first. Further ties are broken using implementation-defined criteria (for now, pointer address of the `PyTypeObject*`).

```
User[MyClass] & LongUser[MyInt] == LongUser[MyClass]
LongUser[MyInt] & User[MyClass] == LongUser[MyClass]
LongUser[MyIntClass] < LongUser[MyClass]
User[MyClass] & BytesUser[MyBytes] == BytesUser[MyBytes]

# We can restrict MyClass to exclude subclasses of non-object builtin types:
User[MyClass] & ObjectUser = ObjectUser[MyClass]
ObjectUser[MyClass] & LongUser[MyInt] == Bottom

LongUser[MyInt] & BytesUser[MyBytes] == Bottom
```

An important consequence of this is that `T1 & T2` might not be a subtype of both `T1` and `T2`. Since client code must tolerate any `Type` being wider than expected, this is rarely an issue in practice.

### Subtyping

All subtyping relationships between `Type`s use the same definition as `PyType_IsSubtype()`. Importantly, this means `__subclasscheck__()` and `__instancecheck__()` are never considered; only a type’s MRO is consulted. This shouldn’t impact most of the use cases for `Type` in the JIT (like attribute lookup), but it does mean that any attempt to optimize calls to `isinstance()` or `issubclass()` will need to be careful. This restriction is unlikely to change, since invoking arbitrary Python code in the compiler is a recipe for disaster.

### Primitive `Type`s

The subtypes of `Primitive` also support specialization. None of the primitive types are related to each other, and none of them are related to `Object` subtypes that represent similar types or values:

```
CInt32[123] < CInt
!(CInt32[123] < Long)
!(CInt32[123] < Long[123])
!(CInt32[123] < CInt64)
!(CInt32 < CInt64)
CBool[true] < CBool
CBool[false] < Primitive
```

## Implementation

### Internal Representation

`Type` is 16 bytes, and is meant to be cheaply copied and always passed by value. It has multiple components:

1. A bitset representing the arbitrary union of predefined types. Every leaf type in the main hierarchy diagram above has a bit assigned to it, meaning predefined types like `Long` have three bits set: `LongExact`, `Bool`, and `LongUser`.
2. A bitset encoding the lifetime of the objects represented by the `Type`: `kLifetimeTop` for `Object` subtypes with unknown mortality, `kLifetimeMortal` and `kLifetimeImmortal` for `Object` subtypes with known mortality, or `kLifetimeBottom` for `Bottom` and primitive types.
3. A specialization kind: one of `kSpecTop`, `kSpecObject`, `kSpecTypeExact`, `kSpecType`, `kSpecInt`, `kSpecDouble`, or `kSpecBottom`. This indicates how to interpret the next component.
4. A `union` containing a `PyTypeObject*`, a `PyObject*`, an `intptr_t`, and a `double`. When the specialization kind is `kSpecTop` or `kSpecBottom`, this will be `0`. Otherwise, this holds the specialization’s value.

As mentioned previously, the set of values represented by a `Type` is the intersection of the different components. Here are a few examples of this in practice:

* `Long` is `({LongExact|LongUser|Bool}, kLifetimeTop, kSpecTop, 0)`
* `MortalBytes` is `({BytesUser|BytesExact}, kLifetimeMortal, kSpecTop, 0)`
* `CInt32[5]` is `(CInt32, kLifetimeBottom, kSpecInt, 5)`
* `LongUser[SomeClass]` is `(LongUser, kLifetimeTop, kSpecType, <SomeClass PyTypeObject*>)`
* `(BytesUser, _, kSpecInt, _)` is invalid, because `kSpecInt` is only valid for the primitive types.
* `(CInt32, _, kSpecObject, _)` is also invalid, because `kSpecObject`, `kSpecType`, and `kSpecExact` are only valid for `Object` subtypes.

## API/Usage

### Creating `Type`s

`Type` does not have any constructors meant for public use; all `Type`s should be created using one of the following methods:

1. Copy one of the constants corresponding to a predefined type. These are the name of the type prefixed with `T`, so `Int` is `jit::hir::TLong`, `Bytes` is `jit::hir::TBytes`, `Object` is `jit::hir::TObject`, etc.
2. Create a type from a C object:
    1. `Type::fromType(PyTypeObject* ty)`: create a `Type` representing `ty`. If `ty` is a predefined type, the result will be one of the predefined types (e.g., `fromType(&PyList_Type) == TList`). Otherwise, `ty` will have a type specialization. For example, if you have a user-defined type in `PyTypeObject* my_class`, `fromType(my_class)` will return `TUser` specialized with `my_class`. If `my_class` is a subtype of `dict`, it will return `TDictUser` specialized with `my_class`.
    2. `Type::fromTypeExact(PyTypeObject* ty)`: like `fromType()`, but the resulting `Type` has `kSpecTypeExact` as its specialization kind. This means it represents *exactly* `ty`, excluding any subtypes. This makes `operator<=` on the resulting type similar to `PyFoo_CheckExact()`.
    3. `Type::fromObject(PyObject* obj)`: create a `Type` representing `obj`‘s type, specialized with `obj` itself. While `Type` supports holding an object of any type, it will most commonly be used with simple objects that appear in places like `co_consts`, like `LongExact[1234]` or `UnicodeExact["hello!"]`. `Type` has a trivial destructor and will never own a reference to either objects or types, so client code must ensure that the relevant objects outlive the `Type` they’re referenced by. These objects are usually going to be used by long-lived JITted code, so this shouldn’t be a concern in practice.
    4. `Type::fromC{Bool,Int,Int64}({bool,int,int64_t} val)`: create a `Type` specialized with the given value. It will be one of the predefined `Type`s `CBool`, `CInt32`, or `CInt64`, respectively.
3. Combine two existing `Type`s with the set operators `|`, `&`, and `-`, which are described in detail above.

### Comparing `Type`s

Using `==` to compare two `Type`s should be very rare. Most questions we want to ask about an object’s type, `ty`, fit one of two forms:

1. *Is `ty` equal to `other_ty` or one of its subtypes?* - For this, use the subtype (`ty <= other_ty`) or strict subtype (`ty < other_ty`) operators.
2. *Could `ty` be  `other_ty` or one of its subtypes?* - This is equivalent to asking “is the intersection between `ty` and `other_ty` non-empty?“. This is common enough that there is a shorthand version of it: `ty.couldBe(other_ty)`.

## Non-features

### Mixed-specialization unions

As shown above, unifying a specialized `Type` with almost any other `Type` will lose that specialization, unless one is a subtype of the other. This is partially due to the fact that `Type` only has room for one specialization at a time, but also due to the artificial requirement that the specialization, if present, applies equally to all of the bits present in the main bitset.

For example, consider `UserInt[MyInt] | Unicode`. If we simply took `{UserInt|Unicode}` and specialized it with `MyInt`, it would be clear that the specialization only applies to `UserInt` and not to `Unicode`, since `MyInt` can’t be a subtype of `Unicode`. However, properly handling this conditional specialization gets messy and is rarely useful in practice. So, the union of `UserInt[MyInt]` and `Unicode` loses the specialization and becomes `{UserInt|Unicode}`.

### Least common ancestor in unions

Consider the following class hierarchy:

```
class Vehicle: pass
class Car(Vehicle): pass
class Motorcycle(Vehicle): pass
```

Unifying `User[Car]` or `User[Motorcycle]` with `User[Vehicle]` will give `User[Vehicle]`, as expected. However, unifying `User[Car]` and `User[Motorcycle]` will give `User`, not `User[Vehicle]` as you might expect. This was an intentional tradeoff: finding the least common ancestor of two classes during unification is certainly possible, but not worth the extra computation given how rarely it’s expected to come up. As usual, if we run into this in real-world code, we can revisit this decision.
