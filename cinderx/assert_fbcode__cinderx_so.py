import os
import sys

def main():
    sys.setdlopenflags(sys.getdlopenflags() | os.RTLD_GLOBAL)
    import _cinderx

    print(f"_cinderx: {sys.modules['_cinderx']}")
    if str(sys.modules["_cinderx"]) != "<module '_cinderx' (static-extension)>":
        raise AssertionError("_cinderx is not from fbcode")


if __name__ == '__main__':
    main()
