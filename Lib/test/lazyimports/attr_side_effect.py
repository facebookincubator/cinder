from test.lazyimports import attribute_side_effect
from test.lazyimports.attribute_side_effect import structures

expected_copyright = "Copyright 2022 Meta Platforms, Inc."
expected_version = "1.0"
expected_struc_obj = "<class 'test.lazyimports.attribute_side_effect.structures.TestStructure'>"

assert(attribute_side_effect.__copyright__ == expected_copyright)
assert(attribute_side_effect.__version__ == expected_version)
assert(repr(attribute_side_effect.structures.TestStructure) == expected_struc_obj)
