import sys

from nqueens_static_lib import bench_n_queens

if __name__ == "__main__":
    num_iterations = 1
    if len(sys.argv) > 1:
        num_iterations = int(sys.argv[1])
    queen_count = 8
    for _ in range(num_iterations):
        res = bench_n_queens(queen_count)
        assert len(res) == 92
