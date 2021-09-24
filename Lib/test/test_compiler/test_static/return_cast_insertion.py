from .common import StaticTestBase


class ReturnCastInsertionTests(StaticTestBase):
    def test_no_cast_to_object(self) -> None:
        """We never cast to object, for object or dynamic or no annotation."""
        for is_async in [True, False]:
            for ann in ["object", "unknown", None]:
                with self.subTest(ann=ann, is_async=is_async):
                    prefix = "async " if is_async else ""
                    full_ann = f" -> {ann}" if ann else ""
                    codestr = f"""
                        {prefix}def f(x){full_ann}:
                            return x
                    """
                    f_code = self.find_code(self.compile(codestr), "f")
                    self.assertNotInBytecode(f_code, "CAST")
