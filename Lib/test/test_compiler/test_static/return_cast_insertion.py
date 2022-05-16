from .common import StaticTestBase


class ReturnCastInsertionTests(StaticTestBase):
    def test_no_cast_to_object(self) -> None:
        """We never cast to object, for object or dynamic or no annotation."""
        for is_async in [True, False]:
            for ann in ["object", "open", None]:
                with self.subTest(ann=ann, is_async=is_async):
                    prefix = "async " if is_async else ""
                    full_ann = f" -> {ann}" if ann else ""
                    codestr = f"""
                        {prefix}def f(x){full_ann}:
                            return x
                    """
                    f_code = self.find_code(self.compile(codestr), "f")
                    self.assertNotInBytecode(f_code, "CAST")

    def test_annotated_method_does_not_cast_lower(self) -> None:
        codestr = f"""
            def f() -> str:
                return 'abc'.lower()
        """
        f_code = self.find_code(self.compile(codestr), "f")
        self.assertNotInBytecode(f_code, "CAST")
        self.assertInBytecode(f_code, "REFINE_TYPE")

    def test_annotated_method_does_not_cast_upper(self) -> None:
        codestr = f"""
            def f() -> str:
                return 'abc'.upper()
        """
        f_code = self.find_code(self.compile(codestr), "f")
        self.assertNotInBytecode(f_code, "CAST")
        self.assertInBytecode(f_code, "REFINE_TYPE")

    def test_annotated_method_does_not_cast_isdigit(self) -> None:
        codestr = f"""
            def f() -> bool:
                return 'abc'.isdigit()
        """
        f_code = self.find_code(self.compile(codestr), "f")
        self.assertNotInBytecode(f_code, "CAST")
        self.assertInBytecode(f_code, "REFINE_TYPE")

    def test_annotated_method_does_not_cast_known_subclass(self) -> None:
        codestr = f"""
            class C(str):
                pass

            def f() -> bool:
                return C('abc').isdigit()
        """
        f_code = self.find_code(self.compile(codestr), "f")
        self.assertNotInBytecode(f_code, "CAST")
        self.assertInBytecode(f_code, "REFINE_TYPE")

    def test_annotated_method_casts_arbitrary_subclass(self) -> None:
        codestr = f"""
            def f(x: str) -> bool:
                return x.isdigit()
        """
        f_code = self.find_code(self.compile(codestr), "f")
        self.assertInBytecode(f_code, "CAST")
        self.assertNotInBytecode(f_code, "REFINE_TYPE")

    def test_annotated_method_does_not_cast_if_valid_on_subclasses(self) -> None:
        codestr = f"""
            from __static__ import ContextDecorator
            class C(ContextDecorator):
                pass

            def f() -> ContextDecorator:
                return C()._recreate_cm()
        """
        f_code = self.find_code(self.compile(codestr), "f")
        self.assertNotInBytecode(f_code, "CAST")
        # TODO(emacs): How do I get a REFINE_TYPE here?
        # self.assertInBytecode(f_code, "REFINE_TYPE")
