# Copyright (c) Meta, Inc. and its affiliates. All Rights Reserved
# File added for Lazy Imports

import self

used_overriden_import = False

def replaced_import(*args):
    global used_overriden_import
    used_overriden_import = True

exec("import os; os", {"__builtins__": {"__import__": replaced_import}})

self.assertTrue(used_overriden_import)
