# Portions copyright (c) Meta Platforms, Inc. and affiliates. (http://www.meta.com)
# pyre-unsafe


class Set:
    def __init__(self):
        self.elts = {}

    def __len__(self):
        return len(self.elts)

    def __contains__(self, elt):
        return elt in self.elts

    def add(self, elt):
        self.elts[elt] = elt

    def elements(self):
        return self.elts.keys()

    def has_elt(self, elt):
        return elt in self.elts

    def remove(self, elt):
        del self.elts[elt]

    def copy(self):
        c = Set()
        c.elts.update(self.elts)
        return c


class Stack(list):
    def push(self, elt):
        self.append(elt)

    def top(self):
        return self[-1]


MANGLE_LEN = 256  # magic constant from compile.c


def mangle(name, klass):
    if klass is None:
        return name
    if not name.startswith("__"):
        return name
    if len(name) + 2 >= MANGLE_LEN:
        return name
    # TODO: Probably need to split and mangle recursively?
    if "." in name:
        return name
    if name.endswith("__"):
        return name
    try:
        i = 0
        while klass[i] == "_":
            i = i + 1
    except IndexError:
        return name
    klass = klass[i:]

    tlen = len(klass) + len(name)
    if tlen > MANGLE_LEN:
        klass = klass[: MANGLE_LEN - tlen]

    return "_%s%s" % (klass, name)
