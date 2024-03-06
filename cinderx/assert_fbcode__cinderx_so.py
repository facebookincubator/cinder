import sys

# This is not automatically loaded during startup, but we still want to check
# it's loaded from fbcode.
import _strictmodule  # noqa: unused


def assert_is_from_fbcode(module: str) -> None:
    print(f"{module}: {sys.modules[module]}")
    mod_str = str(sys.modules[module])
    if mod_str == f"<module '{module}' (static-extension)>":
        return
    elif "buck-out/" in mod_str:
        return
    raise AssertionError(f"{module} is not from fbcode")


def main() -> None:
    assert_is_from_fbcode("_cinderx")
    assert_is_from_fbcode("_strictmodule")
    assert_is_from_fbcode("_static")


if __name__ == '__main__':
    main()
