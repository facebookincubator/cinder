import sys

# This is not automatically loaded during startup, but we still want to check
# it's loaded from fbcode (and so is a static-extension).
import _strictmodule  # noqa: unused


def assert_is_static_extension(module: str) -> None:
    print(f"{module}: {sys.modules[module]}")
    if str(sys.modules[module]) != f"<module '{module}' (static-extension)>":
        raise AssertionError(f"{module} is not from fbcode")


def main() -> None:
    assert_is_static_extension("_cinderx")
    assert_is_static_extension("_strictmodule")
    assert_is_static_extension("_static")


if __name__ == '__main__':
    main()
