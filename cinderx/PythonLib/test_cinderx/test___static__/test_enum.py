from __future__ import annotations

from __static__.enum import Enum

import itertools
import pickle
import unittest
from typing import final


@final
class Colour(Enum):
    RED = 1
    BLUE = 2


@final
class Shape(Enum):
    CIRCLE = 1
    SQUARE = 2


@final
class DummyEnum(Enum):
    BLAH = "blah"
    A = "duplicate"
    B = "duplicate"


@final
class TestEnum(unittest.TestCase):
    def test_eq(self) -> None:
        for first, second in itertools.permutations(
            [Colour.RED, Colour.BLUE, Shape.CIRCLE, Shape.SQUARE],
            2,
        ):
            with self.subTest(first=first, second=second):
                self.assertNotEqual(first, second)

    def test_eq_and_hash_pickle(self) -> None:
        red = pickle.loads(pickle.dumps(Colour.RED))
        blue = pickle.loads(pickle.dumps(Colour.BLUE))
        circle = pickle.loads(pickle.dumps(Shape.CIRCLE))
        square = pickle.loads(pickle.dumps(Shape.SQUARE))

        # Take all combinations of pickled enum + enum value
        # e.g. ((red, Colour.RED), (blue, Colour.BLUE))
        for (
            (first_argument, first_enum),
            (second_argument, second_enum),
        ) in itertools.combinations(
            [
                (red, Colour.RED),
                (blue, Colour.BLUE),
                (circle, Shape.CIRCLE),
                (square, Shape.SQUARE),
            ],
            2,
        ):
            with self.subTest(
                first_argument=first_argument,
                first_enum=first_enum,
                second_argument=second_argument,
                second_enum=second_enum,
            ):
                self.assertEqual(first_argument, first_enum)
                self.assertEqual(hash(first_argument), hash(first_enum))
                self.assertEqual(second_argument, second_enum)
                self.assertEqual(hash(second_argument), hash(second_enum))

                self.assertNotEqual(first_argument, second_argument)
                self.assertNotEqual(first_argument, second_enum)
                self.assertNotEqual(second_argument, first_enum)
                self.assertNotEqual(second_enum, first_enum)

    def test_duplicates(self) -> None:
        self.assertEqual(DummyEnum.A, DummyEnum.B)
        self.assertEqual(DummyEnum.B, DummyEnum.A)
        self.assertEqual(hash(DummyEnum.A), hash(DummyEnum.B))
        self.assertEqual(hash(DummyEnum.B), hash(DummyEnum.A))

        self.assertEqual(pickle.loads(pickle.dumps(DummyEnum.A)), DummyEnum.B)
        self.assertEqual(pickle.loads(pickle.dumps(DummyEnum.B)), DummyEnum.A)
        self.assertEqual(
            hash(pickle.loads(pickle.dumps(DummyEnum.A))), hash(DummyEnum.B)
        )
        self.assertEqual(
            hash(pickle.loads(pickle.dumps(DummyEnum.B))), hash(DummyEnum.A)
        )

    def test_iteration(self) -> None:
        self.assertEqual(set(Shape), {Shape.SQUARE, Shape.CIRCLE})
