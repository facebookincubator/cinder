# Static Python (Programming Patterns)

By necessity, Static Python discourages or disallows certain patterns
that are common in Python. These patterns are highly dynamic (i.e,
cannot be understood or optimized by statically analyzing the code).

This document discusses such patterns, and provides reasonable
alternatives to them.

## 1. Using a class-var to override instance-var

Example:

```python
class Parent:
    def __init__(self) -> None:
        self.is_serializable: bool = True


class Child(Parent):
    is_serializable = False
```

### Why is it used?

To avoid having to declare an [\_\_init\_\_]{.title-ref} method on the
subclass, with potentially more boilerplate (passing arguments, etc).

### Why is it bad?

This pattern is confusing. Can you tell (without running the code), what
would be printed by this snippet?

``` python
print(Child().is_serializable)
print(Child.is_serializable)
```

Is that even the intended result? :)

### Solutions

1.  Explicitly define an `__init__()` method in the
    `Child` class, that sets the instance attribute to the
    desired value. Example:

    > ```python
    > class Parent:
    >     def __init__(self) -> None:
    >         self.is_serializable: bool = True
    >
    >
    > class Child(Parent):
    >     def __init__(self) -> None:
    >         self.is_serializable: bool = False
    > ```
