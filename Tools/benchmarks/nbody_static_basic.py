import sys

from nbody_static_basic_lib import bench_nbody, DEFAULT_REFERENCE, DEFAULT_ITERATIONS

if __name__ == "__main__":
    num_iterations = 5
    if len(sys.argv) > 1:
        num_iterations = int(sys.argv[1])
    bench_nbody(num_iterations, DEFAULT_REFERENCE, DEFAULT_ITERATIONS)
