import argparse
import subprocess
import sys


def run_fuzzer(code_str: str, subprocesses: int) -> None:
    subprocess_arr = []
    for i in range(subprocesses):
        subprocess_arr.append(
            subprocess.Popen(
                [
                    sys.executable,
                    "-X",
                    "jit",
                    __file__.replace("executor.py", "fuzzer.py"),
                    "--codestr",
                    code_str,
                ]
            )
        )
    for i in subprocess_arr:
        i.wait()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--codestr", help="code string to be passed into the fuzzer")
    parser.add_argument("--subprocesses", help="number of subprocesses")
    args = parser.parse_args()
    if args.codestr and args.subprocesses:
        run_fuzzer(args.codestr, int(args.subprocesses))
