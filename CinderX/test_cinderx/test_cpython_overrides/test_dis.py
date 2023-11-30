import dis
import unittest

from cinderx.opcode import shadowop

class CinderX_DisTests(unittest.TestCase):
    def test_widths(self):
        for opcode, opname in enumerate(dis.opname):
            if (
                opname in (
                    'BUILD_MAP_UNPACK_WITH_CALL',
                    'BUILD_TUPLE_UNPACK_WITH_CALL',
                    'JUMP_IF_NONZERO_OR_POP',
                    'JUMP_IF_NOT_EXC_MATCH',
                )
                or opcode in shadowop
            ):
                continue
            with self.subTest(opname=opname):
                width = dis._OPNAME_WIDTH
                if opcode < dis.HAVE_ARGUMENT:
                    width += 1 + dis._OPARG_WIDTH
                self.assertLessEqual(len(opname), width)

if __name__ == "__main__":
    unittest.main()
