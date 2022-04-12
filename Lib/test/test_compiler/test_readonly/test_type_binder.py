from .common import ReadonlyTestBase


class TypeBinderTests(ReadonlyTestBase):
    def test_readonly_redeclare_0(self) -> None:
        code = """
        def f():
            x = 1
            x: Readonly[int] = 2
        """
        errors = self.lint(code)
        errors.check(
            errors.match("cannot re-declare the readonliness of 'x'"),
        )

    def test_readonly_redeclare_1(self) -> None:
        code = """
        def f():
            x: Readonly[int] = 1
            x: int = 2
        """
        errors = self.lint(code)
        errors.check(
            errors.match("cannot re-declare the readonliness of 'x'"),
        )

    def test_readonly_wrapper(self) -> None:
        # Might be confusing. The number 2 is narrowed to
        # readonly because `x` is declared readonly
        code = """
        def f():
            x = readonly([])
            x = 2
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_wrapper_redeclare(self) -> None:
        # x is redeclared
        code = """
        def f():
            x = readonly([])
            x: int = 2
        """
        errors = self.lint(code)
        errors.check(
            errors.match("cannot re-declare the readonliness of 'x'"),
        )

    def test_readonly_assign(self) -> None:
        code = """
        def f():
            x: Readonly[int] = 1
            x = 2
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_augassign(self) -> None:
        code = """
        def f():
            l: Readonly[List[int]] = []
            l += [1]
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "Cannot modify readonly reference 'l' via aug assign",
            ),
        )

    def test_call_readonly(self) -> None:
        code = """
        def f():
            x: Readonly[int] = 1
            f(x, 1 ,2)
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_call_readonly_kw(self) -> None:
        code = """
        def f():
            x: Readonly[int] = 1
            f(x, 1 ,2, y=1)
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "Unsupported: cannot use keyword args or"
                " star args when ANY argument is readonly"
            ),
        )

    def test_call_readonly_star_args(self) -> None:
        code = """
        def f():
            x: Readonly[List[int]] = [1, 2]
            f(1 ,2, *x)
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "Unsupported: cannot use keyword args or"
                " star args when ANY argument is readonly"
            ),
        )

    def test_readonly_class_var(self) -> None:
        code = """
        class C:
            x: Readonly[int] = 1
        """
        errors = self.lint(code)
        errors.check(
            errors.match("cannot declare 'x' readonly in class/module"),
        )

    def test_readonly_base_class(self) -> None:
        code = """
        def f():
            C: Readonly[object]
            class D(C):
                ...
        """
        errors = self.lint(code)
        errors.check(
            errors.match("cannot inherit from a readonly base class 'C'"),
        )

    def test_readonly_func_global(self) -> None:
        code = """
        x = 1
        @readonly_func
        def f():
            x += 1
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_func_closure(self) -> None:
        code = """
        def g():
            x = 1
            @readonly_closure
            @readonly_func
            def f():
                nonlocal x
                x += 1
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot modify 'x' from a closure, inside a readonly_func annotated function"
            ),
            errors.match(
                "Cannot modify readonly reference 'x' via aug assign",
            ),
        )

    def test_readonly_func_closure_1(self) -> None:
        code = """
        def g():
            x = 1
            @readonly_closure
            @readonly_func
            def f():
                nonlocal x
                x = 2
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot modify 'x' from a closure, inside a readonly_func annotated function"
            ),
        )

    def test_readonly_in_method(self) -> None:
        code = """
        class C:
            def f(self, x: Readonly[List[int]]):
                x += [1]
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "Cannot modify readonly reference 'x' via aug assign",
            ),
        )

    def test_readonly_nested_func_late_declare(self) -> None:
        code = """
        def g():
            def f():
                y: int = x
            x = readonly(1)
        """
        errors = self.lint(code)
        errors.check(
            errors.match("cannot assign readonly value to mutable 'y'"),
        )

    def test_readonly_nested_class_late_declare(self) -> None:
        code = """
        def g():
            class C:
                def f():
                    y: int = x
            x = readonly(1)
        """
        errors = self.lint(code)
        errors.check(
            errors.match("cannot assign readonly value to mutable 'y'"),
        )

    def test_readonly_triple_nested_function_late_declare(self) -> None:
        code = """
        def g():
            def h():
                def f():
                    y: int = x
            x = readonly(1)
        """
        errors = self.lint(code)
        errors.check(
            errors.match("cannot assign readonly value to mutable 'y'"),
        )

    def test_readonly_for_readonly_mutable(self) -> None:
        code = """
        def g(arr: List[int]):
            y = 0
            for a in readonly(arr):
                y = a
        """
        errors = self.lint(code)
        errors.check(
            errors.match("cannot assign readonly value to mutable 'y'"),
        )

    def test_readonly_for_readonly(self) -> None:
        code = """
        def g(arr: List[int]):
            y = readonly(0)
            for a in readonly(arr):
                y = a
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_async_for_readonly_mutable(self) -> None:
        code = """
        def g(arr):
            y = 0
            async for a in readonly(arr):
                y = a
        """
        errors = self.lint(code)
        errors.check(
            errors.match("cannot assign readonly value to mutable 'y'"),
        )

    def test_readonly_for_readonly(self) -> None:
        code = """
        def g(arr):
            y = readonly(0)
            async for a in readonly(arr):
                y = a
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_delete(self) -> None:
        code = """
        def g(arr):
            x = 0
            y = readonly(0)
            del x
            del y
        """
        errors = self.lint(code)
        errors.check(
            errors.match("Cannot explicitly delete readonly value 'y'"),
        )

    def test_readonly_raise(self) -> None:
        code = """
        def g(arr):
            y = readonly(0)
            raise y
        """
        errors = self.lint(code)
        errors.check(
            errors.match("Cannot raise readonly expression 'y'"),
        )

    def test_readonly_raise_cause(self) -> None:
        code = """
        def g(arr):
            x = 0
            y = readonly(0)
            raise x from y
        """
        errors = self.lint(code)
        errors.check(
            errors.match("Cannot raise with readonly cause 'y'"),
        )

    def test_readonly_return(self) -> None:
        code = """
        def g(arr) -> int:
            y = readonly(0)
            return y
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "Cannot return readonly expression 'y' from a function returning a mutable type"
            ),
        )

    def test_readonly_return_good(self) -> None:
        code = """
        def g(arr) -> Readonly[int]:
            y = readonly(0)
            return y
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_yield(self) -> None:
        code = """
        def g(arr) -> Iterator[int]:
            y = readonly(0)
            yield y
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "Cannot yield readonly expression 'y' from a function yielding a mutable type"
            ),
        )

    def test_readonly_yield_good(self) -> None:
        code = """
        def g(arr) -> Iterator[Readonly[int]]:
            y = readonly(0)
            yield y
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_yield_generator(self) -> None:
        code = """
        def g(arr) -> Generator[int, None, None]:
            y = readonly(0)
            yield y
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "Cannot yield readonly expression 'y' from a function yielding a mutable type"
            ),
        )

    def test_readonly_yield_generator_good(self) -> None:
        code = """
        def g(arr) -> Generator[Readonly[int], None, None]:
            y = readonly(0)
            yield y
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_yield_async_generator(self) -> None:
        code = """
        async def g(arr) -> AsyncGenerator[int, None]:
            y = readonly(0)
            yield y
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "Cannot yield readonly expression 'y' from a function yielding a mutable type"
            ),
        )

    def test_readonly_yield_async_generator_good(self) -> None:
        code = """
        async def g(arr) -> AsyncGenerator[Readonly[int], None]:
            y = readonly(0)
            yield y
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_yield_async_generator_send(self) -> None:
        code = """
        async def g(arr) -> AsyncGenerator[int, Readonly[int]]:
            y = yield
            yield y
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "Cannot yield readonly expression 'y' from a function yielding a mutable type"
            ),
        )

    def test_readonly_yield_async_generator_send(self) -> None:
        code = """
        async def g(arr) -> AsyncGenerator[Readonly[int], Readonly[int]]:
            y = yield
            yield y
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_yield_async_generator_send_readonly(self) -> None:
        code = """
        async def g(arr) -> AsyncGenerator[int, int]:
            y = readonly(0)
            y = yield
            yield y
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "Cannot yield readonly expression 'y' from a function yielding a mutable type"
            ),
        )

    def test_readonly_yield_async_generator_send_mutable(self) -> None:
        code = """
        async def g(arr) -> AsyncGenerator[int, int]:
            y = yield
            yield y
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_yield_generator_return(self) -> None:
        code = """
        def g(arr) -> Generator[int, None, int]:
            y = readonly(0)
            yield y
            x = y
            return x
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "Cannot yield readonly expression 'y' from a function yielding a mutable type"
            ),
            errors.match(
                "Cannot return readonly expression 'x' from a function returning a mutable type"
            ),
        )

    def test_readonly_yield_generator_return_good(self) -> None:
        code = """
        def g(arr) -> Generator[Readonly[int], None, Readonly[int]]:
            y = readonly(0)
            yield y
            x = y
            return x
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_yield_from(self) -> None:
        code = """
        def f() -> Generator[Readonly[int], None, Readonly[int]]: ...

        def g(arr) -> Generator[Readonly[int], None, Readonly[int]]:
            yield from f()
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "Cannot use yield from in a function that yields, sends, or returns readonly. Refactor in terms of a normal yield"
            ),
        )

    def test_readonly_yield_from_readonly(self) -> None:
        code = """
        def f() -> Generator[int, None, int]: ...

        def g(arr) -> Generator[Readonly[int], None, Readonly[int]]:
            yield from readonly(f())
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "Cannot use yield from in a function that yields, sends, or returns readonly. Refactor in terms of a normal yield"
            ),
            errors.match(
                "Cannot use yield from on a readonly expression. Rewrite in terms of a normal yield"
            ),
        )

    def test_readonly_yield_from_good(self) -> None:
        code = """
        def f() -> Generator[int, None, int]: ...

        def g(arr) -> Generator[int, None, int]:
            yield from f()
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_subscript(self) -> None:
        code = """
        def g(arr: Readonly[List[int]]):
            c = arr[4]
            d = 5
            d = c
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'd'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_subscript_slice(self) -> None:
        code = """
        def g(arr: Readonly[List[int]]):
            c = arr[4:6]
            d = 5
            d = c
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'd'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_subscript_slice_readonly(self) -> None:
        code = """
        def g(arr: List[int]):
            c = arr[readonly(4):6]
            c = readonly(5)
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'c'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_subscript_good(self) -> None:
        code = """
        def g(arr: List[int], arr2: Readonly[List[int]]):
            c = arr[readonly(4):6]
            c = 5
            d = arr2[readonly(4):6]
            e = readonly(5)
            d = e
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_subscript_assign_value(self) -> None:
        code = """
        def g(arr):
            arr[4] = readonly(5)
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "Cannot store readonly expression 'arr' in a mutable container"
            ),
        )

    def test_readonly_subscript_assign_value_readonly(self) -> None:
        code = """
        def g(arr: Readonly[List[int]]):
            arr[4] = readonly(5)
        """
        errors = self.lint(code)
        errors.check(
            errors.match("Cannot modify readonly expression 'arr' via subscript"),
        )

    def test_readonly_subscript_assign_readonly_array_value(self) -> None:
        code = """
        def g(arr: Readonly[List[int]]):
            arr[4] = 5
        """
        errors = self.lint(code)
        errors.check(
            errors.match("Cannot modify readonly expression 'arr' via subscript"),
        )

    def test_readonly_subscript_assign_good(self) -> None:
        code = """
        def g(arr: List[int]):
            arr[4] = 5
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_with(self) -> None:
        code = """
        def g(arr):
            with readonly(arr) as c:
                d = 5
                d = c
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'd'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_with_mixed(self) -> None:
        code = """
        def g(arr, arr2):
            with readonly(arr) as c, arr2 as d:
                e = 5
                e = c
                e = d
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'e'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_with_multi(self) -> None:
        code = """
        def g(arr):
            with readonly(arr) as (c, d):
                e = 4
                e = c
                e = d
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'e'. Remember to declare name as readonly explicitly"
            ),
            errors.match(
                "cannot assign readonly value to mutable 'e'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_with_non_wrapped_readonly(self) -> None:
        code = """
        def g(arr):
            arr2 = readonly(arr)
            with arr2 as c:
                d = 5
                d = c
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_with_good(self) -> None:
        code = """
        def g(arr):
            with arr as c:
                d = 5
                d = c
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_with_good2(self) -> None:
        code = """
        def g(arr):
            with readonly(arr) as c:
                d = readonly(5)
                d = c
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_async_with_good(self) -> None:
        code = """
        async def g(arr):
            async with readonly(arr) as c:
                d = readonly(5)
                d = c
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_try_except(self) -> None:
        code = """
        def g(arr):
            try:
                raise arr
            except Exception as e:
                c = e
                c = readonly(3)
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'c'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_try_except_good(self) -> None:
        code = """
        def g(arr):
            try:
                raise arr
            except Exception as e:
                c = 3
                c = e
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_builtin_fallback(self) -> None:
        code = """
        super = super

        class C:
            def g(a = print):
                a()
                exit()
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_import_alias(self) -> None:
        code = """
        import a as b
        from c import d
        import e

        def somefunc():
            lb = b
            ld = d
            le = e.f
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_import_local_multipart(self) -> None:
        code = """
        import e

        def somefunc():
            import d.f
            le = e
            ld = d
            le = d.f
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_walrus(self) -> None:
        code = """
        def g(arr):
            a = 4
            if b := a == 5:
                return b
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_walrus_readonly(self) -> None:
        code = """
        def g(arr) -> int:
            a = readonly(4)
            if (b := a) == 5:
                c = 5
                c = b
                return b
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'c'. Remember to declare name as readonly explicitly"
            ),
            errors.match(
                "Cannot return readonly expression 'b' from a function returning a mutable type"
            ),
        )

    def test_readonly_walrus_readonly(self) -> None:
        code = """
        def g(arr):
            a = 4
            if (b := readonly(a)) == 5:
                return b
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "Cannot return readonly expression 'b' from a function returning a mutable type"
            ),
        )

    def test_readonly_walrus_good(self) -> None:
        code = """
        def g(arr) -> Readonly[int]:
            a = 4
            if (b := readonly(a)) == 5:
                return b
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_attribute(self) -> None:
        code = """
        def g(a: Readonly[object]):
            b = a.b
            c = 4
            c = b
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'c'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_attribute_assign(self) -> None:
        code = """
        def g(a: Readonly[object]):
            a.b = 5
        """
        errors = self.lint(code)
        errors.check(
            errors.match("Cannot set attributes on readonly expression 'a'"),
        )

    def test_readonly_attribute_assign_readonly(self) -> None:
        code = """
        def g(a: Readonly[object]):
            a.b = readonly(5)
        """
        errors = self.lint(code)
        errors.check(
            errors.match("Cannot set attributes on readonly expression 'a'"),
        )

    def test_readonly_attribute_good(self) -> None:
        code = """
        def g(a):
            b = a.b
            c = 4
            c = b
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_list_comp_mutable(self) -> None:
        code = """
        def g(a):
            l = [i for i in range(a)]
            l = readonly(5)
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'l'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_list_comp_nested(self) -> None:
        code = """
        def g(a):
            l = [x for x in [y for y in a]]
            l = readonly(5)
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'l'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_list_comp_nested_outer_name(self) -> None:
        code = """
        def g(o):
            z = [some_global_func_after_g(e) for e in o if e is None]

        def some_global_func_after_g(a): ...
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_list_comp_readonly(self) -> None:
        code = """
        def g(a):
            l = [i for i in readonly(range(a))]
            h = 5
            h = l
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'h'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_list_comp_readonly_value(self) -> None:
        code = """
        def g(a):
            l = [readonly(i) for i in readonly(range(a))]
            h = 5
            h = l
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'h'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_list_comp_readonly_indirect(self) -> None:
        code = """
        def g(a: Readonly[object]):
            l = [i for i in a]
            h = 5
            h = l
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'h'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_list_comp_readonly_good(self) -> None:
        code = """
        def g(a: Readonly[object]):
            l = [i for i in a]
            h = readonly(5)
            h = l
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_set_comp_mutable(self) -> None:
        code = """
        def g(a):
            l = {i for i in range(a)}
            l = readonly(5)
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'l'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_set_comp_readonly(self) -> None:
        code = """
        def g(a):
            l = {i for i in readonly(range(a))}
            h = 5
            h = l
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'h'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_set_comp_readonly_value(self) -> None:
        code = """
        def g(a):
            l = {readonly(i) for i in range(a)}
            h = 5
            h = l
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'h'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_set_comp_readonly_indirect(self) -> None:
        code = """
        def g(a: Readonly[object]):
            l = {i for i in a}
            h = 5
            h = l
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'h'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_set_comp_readonly_good(self) -> None:
        code = """
        def g(a: Readonly[object]):
            l = {i for i in a}
            h = readonly(5)
            h = l
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_dict_comp_mutable(self) -> None:
        code = """
        def g(a):
            l = {i: i for i in range(a)}
            l = readonly(5)
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'l'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_dict_comp_readonly(self) -> None:
        code = """
        def g(a):
            l = {i: i for i in readonly(range(a))}
            h = 5
            h = l
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'h'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_dict_comp_readonly_key(self) -> None:
        code = """
        def g(a):
            l = {readonly(i): i for i in range(a)}
            h = 5
            h = l
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'h'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_dict_comp_readonly_value(self) -> None:
        code = """
        def g(a):
            l = {i: readonly(i) for i in range(a)}
            h = 5
            h = l
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'h'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_dict_comp_readonly_indirect(self) -> None:
        code = """
        def g(a: Readonly[object]):
            l = {i: i for i in a}
            h = 5
            h = l
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'h'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_dict_comp_readonly_good(self) -> None:
        code = """
        def g(a: Readonly[object]):
            l = {i: i for i in a}
            h = readonly(5)
            h = l
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_generator_comp_mutable(self) -> None:
        code = """
        def g(a):
            l = (i for i in range(a))
            l = readonly(5)
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'l'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_generator_comp_readonly(self) -> None:
        code = """
        def g(a):
            l = tuple(i for i in readonly(range(a)))
            h = 5
            h = l
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'h'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_generator_comp_readonly_value(self) -> None:
        code = """
        def g(a):
            l = tuple(readonly(i) for i in range(a))
            h = 5
            h = l
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'h'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_generator_comp_readonly_indirect(self) -> None:
        code = """
        def g(a: Readonly[object]):
            l = tuple(i for i in a)
            h = 5
            h = l
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'h'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_generator_comp_readonly_good(self) -> None:
        code = """
        def g(a: Readonly[object]):
            l = (i for i in a)
            h = readonly(5)
            h = l
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_annotation_bare_readonly(self) -> None:
        code = """
        def g(a: Readonly):
            h = 5
            h = a
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "Readonly is not a valid annotation. If a readonly value of unknown type is desired, use Readonly[object] instead"
            ),
        )

    def test_readonly_annotation_readonly_object(self) -> None:
        code = """
        def g(a: Readonly[object]):
            h = 5
            h = a
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'h'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_annotation_readonly_object_good(self) -> None:
        code = """
        def g(a: Readonly[object]):
            h = readonly(5)
            h = a
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_tuple(self) -> None:
        code = """
        def g():
            h: Tuple = tuple(5, 4, 3)
            h = readonly(4)
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'h'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_tuple_elem(self) -> None:
        code = """
        def g():
            h = tuple(readonly(5), 4, 3)
            h = readonly(4)
            a = 7
            a = h
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'a'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_tuple_attr(self) -> None:
        code = """
        def g(a: Readonly[Tuple[int, int, int]]):
            h = tuple(5, 4, 3)
            h = a
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'h'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_tuple_good(self) -> None:
        code = """
        def g(a: Readonly[Tuple[int, int, int]]):
            h = tuple(5, readonly(4), 3)
            a = h
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_tuplelit(self) -> None:
        code = """
        def g():
            h: Tuple = (5, 4, 3)
            h = readonly(4)
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'h'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_tuplelit_elem(self) -> None:
        code = """
        def g():
            h = (readonly(5), 4, 3)
            h = readonly(4)
            a = 7
            a = h
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'a'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_tuplelit_attr(self) -> None:
        code = """
        def g(a: Readonly[Tuple[int, int, int]]):
            h = (5, 4, 3)
            h = a
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'h'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_tuplelit_good(self) -> None:
        code = """
        def g(a: Readonly[Tuple[int, int, int]]):
            h = (5, readonly(4), 3)
            a = h
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_list(self) -> None:
        code = """
        def g():
            h: List = [5, 4, 3]
            h = readonly(4)
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'h'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_list_elem(self) -> None:
        code = """
        def g():
            h = [readonly(5), 4, 3]
            h = readonly(4)
            a = 7
            a = h
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'a'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_list_attr(self) -> None:
        code = """
        def g(a: Readonly[List[int]]):
            h = [5, 4, 3]
            h = a
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'h'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_list_good(self) -> None:
        code = """
        def g(a: Readonly[List[int]]):
            h = [5, readonly(4), 3]
            a = h
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_set(self) -> None:
        code = """
        def g():
            h: Set = {5, 4, 3}
            h = readonly(4)
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'h'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_set_elem(self) -> None:
        code = """
        def g():
            h = {readonly(5), 4, 3}
            h = readonly(4)
            a = 7
            a = h
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'a'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_set_attr(self) -> None:
        code = """
        def g(a: Readonly[Set[int]]):
            h = {5, 4, 3}
            h = a
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'h'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_set_good(self) -> None:
        code = """
        def g(a: Readonly[Set[int]]):
            h = {5, readonly(4), 3}
            a = h
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_dict(self) -> None:
        code = """
        def g():
            h: Dict = {0: 5, 1: 4, 2: 3}
            h = readonly(4)
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'h'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_dict_elem(self) -> None:
        code = """
        def g():
            h = {0: readonly(5), 1: 4, 2: 3}
            h = readonly(4)
            a = 7
            a = h
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'a'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_dict_key(self) -> None:
        code = """
        def g():
            h = {readonly(0): 5, 1: 4, 2: 3}
            h = readonly(4)
            a = 7
            a = h
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'a'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_dict_attr(self) -> None:
        code = """
        def g(a: Readonly[Dict[int, int]]):
            h = {0: 5, 1: 4, 2: 3}
            h = a
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'h'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_dict_good(self) -> None:
        code = """
        def g(a: Readonly[Dict[int, int]]):
            h = {0: 5, 1: readonly(4), 2: 3}
            a = h
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_star_expr(self) -> None:
        code = """
        def g(a: List[int]):
            b = readonly([1, 2, 3])
            *a = b
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'a'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_star_expr_good(self) -> None:
        code = """
        def g(a: Readonly[List[int]]):
            b = readonly([1, 2, 3])
            *a = b
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_star_expr_good2(self) -> None:
        code = """
        def g(a: Readonly[List[int]]):
            b = [1, 2, 3]
            *a = b
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_tuple_destructure(self) -> None:
        code = """
        def g(arr: Readonly[List[int]]):
            (a, b) = arr
            c = 5
            c = a
            d = 5
            d = b
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'c'. Remember to declare name as readonly explicitly"
            ),
            errors.match(
                "cannot assign readonly value to mutable 'd'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_tuple_destructure_good(self) -> None:
        code = """
        def g(ar: Readonly[List[int]]):
            (a, b) = arr
            c = readonly(5)
            c = a
            c = b
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_tuple_destructure_elem(self) -> None:
        code = """
        def g(i: Readonly[int], j: int):
            (a, b) = i, j
            c = 5
            c = a
            d = 5
            d = b
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'c'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_tuple_destructure_elem(self) -> None:
        code = """
        def g(i: Readonly[int], j: int):
            (a, b, c) = i, j
            d = 5
            d = a
            e = 5
            e = b
            f = 5
            f = c
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'd'. Remember to declare name as readonly explicitly"
            ),
            errors.match(
                "cannot assign readonly value to mutable 'e'. Remember to declare name as readonly explicitly"
            ),
            errors.match(
                "cannot assign readonly value to mutable 'f'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_tuple_destructure_elem_good(self) -> None:
        code = """
        def g(i: Readonly[int], j: int):
            (a, b) = i, j
            c = readonly(5)
            c = a
            d = 5
            d = b
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_list_destructure(self) -> None:
        code = """
        def g(arr: Readonly[List[int]]):
            [a, b] = arr
            c = 5
            c = a
            d = 5
            d = b
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'c'. Remember to declare name as readonly explicitly"
            ),
            errors.match(
                "cannot assign readonly value to mutable 'd'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_list_destructure_good(self) -> None:
        code = """
        def g(ar: Readonly[List[int]]):
            [a, b] = arr
            c = readonly(5)
            c = a
            c = b
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_await(self) -> None:
        code = """
        async def f() -> int: ...

        def g():
            c = await readonly(f())
            d = 5
            d = c
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'd'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_await_var(self) -> None:
        code = """
        async def f() -> int: ...

        def g():
            d = readonly(f())
            c = await d
            g = 5
            g = c
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'g'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_await_good(self) -> None:
        code = """
        async def e() -> int: ...
        async def f() -> Readonly[int]: ...

        def g():
            c = await e()
            h = 5
            h = c
            d = await readonly(f())
            j = readonly(5)
            d = j
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_boolop(self) -> None:
        code = """
        def g(a: Readonly[int], b: int):
            d = a or b
            d = readonly(5)
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'd'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_boolop_good(self) -> None:
        code = """
        def g(a: Readonly[int], b: int):
            d = readonly(a or b)
            d = readonly(5)
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_unaryop(self) -> None:
        code = """
        def g(a: Readonly[int]):
            d = -a
            d = readonly(5)
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'd'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_unaryop_good(self) -> None:
        code = """
        def g(a: Readonly[int]):
            d = readonly(-a)
            d = readonly(5)
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_compare(self) -> None:
        code = """
        def g(a: Readonly[int], b: int):
            d = a == b
            d = readonly(5)
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'd'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_compare_good(self) -> None:
        code = """
        def g(a: Readonly[int], b: int):
            d = readonly(a == b)
            d = readonly(5)
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_binop(self) -> None:
        code = """
        def g(a: Readonly[int], b: int):
            d = a + b
            f = 5
            f = d
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'f'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_binop_both(self) -> None:
        code = """
        def g(a: Readonly[int], b: Readonly[int]):
            d = a + b
            f = 5
            f = d
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'f'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_binop_call(self) -> None:
        code = """
        def g(a: int, b: int):
            d = a + readonly(b)
            f = 5
            f = d
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'f'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_binop_none(self) -> None:
        code = """
        def g(a: int, b: int):
            d = a + b
            f = readonly(5)
            d = f
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'd'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_binop_good(self) -> None:
        code = """
        def g(a: Readonly[int], b: Readonly[int]):
            d = a + b
            f = readonly(5)
            f = d
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_readonly_ifexp(self) -> None:
        code = """
        def g(a: Readonly[int], b: int, c: bool):
            d = a if c else b
            f = 5
            f = d
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'f'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_ifexp_both(self) -> None:
        code = """
        def g(a: Readonly[int], b: Readonly[int], c: bool):
            d = a if c else b
            f = 5
            f = d
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'f'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_ifexp_call(self) -> None:
        code = """
        def g(a: int, b: int, c: bool):
            d = a if c else readonly(b)
            f = 5
            f = d
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'f'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_ifexp_none(self) -> None:
        code = """
        def g(a: int, b: int, c: bool):
            d = a if c else b
            d = a + b
            f = readonly(5)
            d = f
        """
        errors = self.lint(code)
        errors.check(
            errors.match(
                "cannot assign readonly value to mutable 'd'. Remember to declare name as readonly explicitly"
            ),
        )

    def test_readonly_ifexp_good(self) -> None:
        code = """
        def g(a: Readonly[int], b: Readonly[int], c: bool):
            d = a if c else b
            f = readonly(5)
            f = d
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_nested_free_globals(self) -> None:
        code = """
        def testfunc():
            x: Dict[int, object] = {int(i): object() for i in range(1, 5)}
            return x
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])

    def test_allow_closure_modification(self) -> None:
        code = """
        @readonly_func
        def f():
            x: List[int] = []
            @readonly_func
            def g():
                nonlocal x
                x = [1]
        """
        errors = self.lint(code)
        self.assertEqual(errors.errors, [])
