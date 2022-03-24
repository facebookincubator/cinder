#!/usr/bin/env python3
# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)
"""
N-body benchmark from the Computer Language Benchmarks Game.

This is intended to support Unladen Swallow's pyperf.py. Accordingly, it has been
modified from the Shootout version:
- Accept standard Unladen Swallow benchmark options.
- Run report_energy()/advance() in a loop.
- Reimplement itertools.combinations() to work with older Python versions.

Pulled from:
http://benchmarksgame.alioth.debian.org/u64q/program.php?test=nbody&lang=python3&id=1

Contributed by Kevin Carson.
Modified by Tupteq, Fredrik Johansson, and Daniel Nanz.
"""
import __static__
from __static__ import double, CheckedList, CheckedDict, box

__contact__ = "collinwinter@google.com (Collin Winter)"
DEFAULT_ITERATIONS = 20000
DEFAULT_REFERENCE = "sun"


def combinations(l):
    """Pure-Python implementation of itertools.combinations(l, 2)."""
    result = []
    for x in range(len(l) - 1):
        ls = l[x + 1 :]
        for y in ls:
            result.append((l[x], y))
    return result


PI = 3.14159265358979323
SOLAR_MASS = 4 * PI * PI
DAYS_PER_YEAR = 365.24

class Vector:
    def __init__(self, x: double, y: double, z: double):
         self.x: double = x
         self.y: double = y
         self.z: double = z

    def __repr__(self):
        return f"Vector({box(self.x), box(self.y), box(self.z)})"

class Body:
    def __init__(self, pos: Vector, v: Vector, mass: double):
        self.pos: Vector = pos
        self.v: Vector = v
        self.mass: double = mass

    def __repr__(self):
        return f"Body({self.pos}, {self.v}, {box(self.mass)})"


BODIES = CheckedDict[str, Body]({
    "sun": Body(Vector(0.0, 0.0, 0.0), Vector(0.0, 0.0, 0.0), double(SOLAR_MASS)),
    "jupiter": Body(
        Vector(4.84143144246472090e00, -1.16032004402742839e00, -1.03622044471123109e-01),
        Vector(
            1.66007664274403694e-03 * double(DAYS_PER_YEAR),
            7.69901118419740425e-03 * double(DAYS_PER_YEAR),
            -6.90460016972063023e-05 * double(DAYS_PER_YEAR),
        ),
        9.54791938424326609e-04 * double(SOLAR_MASS),
    ),
    "saturn": Body(
        Vector(8.34336671824457987e00, 4.12479856412430479e00, -4.03523417114321381e-01),
        Vector(
            -2.76742510726862411e-03 * double(DAYS_PER_YEAR),
            4.99852801234917238e-03 * double(DAYS_PER_YEAR),
            2.30417297573763929e-05 * double(DAYS_PER_YEAR),
        ),
        2.85885980666130812e-04 * double(SOLAR_MASS),
    ),
    "uranus": Body(
        Vector(1.28943695621391310e01, -1.51111514016986312e01, -2.23307578892655734e-01),
        Vector(
            2.96460137564761618e-03 * double(DAYS_PER_YEAR),
            2.37847173959480950e-03 * double(DAYS_PER_YEAR),
            -2.96589568540237556e-05 * double(DAYS_PER_YEAR),
        ),
        4.36624404335156298e-05 * double(SOLAR_MASS),
    ),
    "neptune": Body(
        Vector(1.53796971148509165e01, -2.59193146099879641e01, 1.79258772950371181e-01),
        Vector(
            2.68067772490389322e-03 * double(DAYS_PER_YEAR),
            1.62824170038242295e-03 * double(DAYS_PER_YEAR),
            -9.51592254519715870e-05 * double(DAYS_PER_YEAR),
        ),
        5.15138902046611451e-05 * double(SOLAR_MASS),
    ),
})


SYSTEM = CheckedList[Body](BODIES.values())
PAIRS = combinations(SYSTEM)


def advance(dt: double, n, bodies: CheckedList[Body]=SYSTEM, pairs=PAIRS):
    for i in range(n):  # noqa: B007
        b1: Body
        b2: Body
        for (b1, b2) in pairs:
            pos1 = b1.pos
            pos2 = b2.pos
            dx: double = pos1.x - pos2.x
            dy: double = pos1.y - pos2.y
            dz: double = pos1.z - pos2.z
            mag = dt * ((dx * dx + dy * dy + dz * dz) ** (double(-1.5)))
            b1m = b1.mass * mag
            b2m = b2.mass * mag
            v1 = b1.v
            v2 = b2.v
            v1.x -= dx * b2m
            v1.y -= dy * b2m
            v1.z -= dz * b2m
            v2.x += dx * b1m
            v2.y += dy * b1m
            v2.z += dz * b1m
        for body in bodies:  # noqa: B007
            r = body.pos
            v = body.v
            r.x += dt * v.x
            r.y += dt * v.y
            r.z += dt * v.z


def report_energy(bodies=SYSTEM, pairs=PAIRS, e: double=0.0) -> double:
    b1: Body
    b2: Body
    body: Body
    for (b1, b2) in pairs:  # noqa: B007
        pos1 = b1.pos
        pos2 = b2.pos
        dx = pos1.x - pos2.x
        dy = pos1.y - pos2.y
        dz = pos1.z - pos2.z
        e -= (b1.mass * b2.mass) / ((dx * dx + dy * dy + dz * dz) ** 0.5)
    for body in bodies:
        v = body.v
        e += body.mass * (v.x * v.x + v.y * v.y + v.z * v.z) / 2.0
    return e


def offset_momentum(ref: Body, bodies, px: double=0.0, py: double=0.0, pz: double=0.0):
    body: Body
    for body in bodies:  # noqa: B007
        v: Vector = body.v
        m: double = body.mass
        px -= v.x * m
        py -= v.y * m
        pz -= v.z * m

    m = ref.mass
    v = ref.v
    v.x = px / m
    v.y = py / m
    v.z = pz / m


def bench_nbody(loops, reference, iterations):
    # Set up global state
    offset_momentum(BODIES[reference], SYSTEM)

    range_it = range(loops)
    for _ in range_it:
        report_energy(SYSTEM, PAIRS)
        advance(0.01, iterations, SYSTEM, PAIRS)
        report_energy(SYSTEM, PAIRS)

def run():
    num_loops = 5
    bench_nbody(num_loops, DEFAULT_REFERENCE, DEFAULT_ITERATIONS)


if __name__ == "__main__":
    import sys

    num_loops = 5
#    if len(sys.argv) > 1:
#        num_loops = int(sys.argv[1])

    bench_nbody(num_loops, DEFAULT_REFERENCE, DEFAULT_ITERATIONS)
