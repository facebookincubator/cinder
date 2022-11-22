#!/usr/bin/env python3
import argparse
import atexit
import http.client
import json
import logging
import os
import re
import shlex
import socket
import subprocess
import sys
import tempfile
import time
from contextlib import nullcontext


# Do not change these without changing the bundled django_minimal run.py
# TODO(emacs): Pass this through to run.py via a CLI argument
host = "127.0.0.1"
port = 8000


def gitroot():
    try:
        return subprocess.check_output(
            ["git", "rev-parse", "--show-toplevel"], encoding="utf-8"
        ).strip()
    except subprocess.CalledProcessError:
        return "$HOME/local/cinder"


def default_django_dir():
    default_django_dir = "/var/tmp/django_minimal"
    if os.path.exists(default_django_dir):
        logging.info(f"django-dir: {default_django_dir}")
        return default_django_dir
    sys.stderr.write(
        f"""Error: {default_django_dir} does not exist.

You can probably fix this with:
    mkdir -p {default_django_dir}
    cd {default_django_dir}
    {gitroot()}/Tools/benchmarks/django/setup_django_minimal.sh

or by providing --django-dir to benchmark.py
"""
    )
    sys.exit(1)


def default_interpreter():
    candidates = []
    try:
        root = gitroot()
        candidates = (
            f"{root}/build-release/python",
            f"{root}/build/python",
        )
        candidates = [os.path.realpath(p) for p in candidates]
        for p in candidates:
            if os.path.exists(p):
                logging.info(f"interpreter: {p}")
                return p
    except subprocess.CalledProcessError:
        pass
    sys.stderr.write(
        f"""Error: Could not find interpreter in {candidates}
Use --interpreter to specify manually.
"""
    )
    sys.exit(1)


def kill_server():
    global server_process
    if server_process is None:
        return
    if server_process.poll() is not None:
        return
    logging.info("Killing server...")
    server_process.send_signal(subprocess.signal.SIGINT)
    try:
        server_process.wait(10)
    except subprocess.TimeoutExpired:
        server_process.send_signal(subprocess.signal.SIGKILL)
        sys.stderr.write("Warning: Failed to shut down server, sending SIGKILL")


def start_server(args, instr_atstart, callgrind_out_dir):
    env = dict(os.environ)
    env["PYTHONHASHSEED"] = "0"
    cmd = [args.interpreter, *args.interpreter_args, "run.py"]
    assert not (args.callgrind and args.bolt_record)
    if args.callgrind:
        valgrind_cmd = [
            "valgrind",
            "--tool=callgrind",
            f"--callgrind-out-file={callgrind_out_dir}/cg",
        ]
        if not instr_atstart:
            valgrind_cmd += ["--instr-atstart=no"]
        cmd = valgrind_cmd + cmd
    elif args.bolt_record or args.perf_record:
        perf_cmd = ["perf", "record", "-c", "9997", "-o", "perf.data"]
        if args.bolt_record:
            # Bolt want user-mode samples only, with last branch record active.
            perf_cmd += ["-e", "cycles:u", "-j", "any,u", "--"]
        else:
            # For human consumption, we want to include kernel samples and
            # stack traces.
            perf_cmd += ["-e", "cycles", "-g", "--"]
        cmd = perf_cmd + cmd

    if os.path.exists("/bin/setarch"):
        setarch = ["/bin/setarch", "x86_64", "--addr-no-randomize"]
        if args.isolate_cpu is not None:
            setarch += ["taskset", "--cpu-list", str(args.isolate_cpu)]
        cmd = setarch + cmd
    if args.server_log is not None:
        logfile = args.server_log
        logging.info(f">>> {' '.join(cmd)} >& {logfile}")
        log_fp = open(logfile, "wb")  # noqa: P201
        atexit.register(lambda: log_fp.close())
    else:
        log_fp = subprocess.DEVNULL
    global server_process
    logging.info(f"Running {shlex.join(cmd)}")
    server_process = subprocess.Popen(
        cmd, cwd=args.django_dir, env=env, stdout=log_fp, stderr=log_fp
    )
    atexit.register(kill_server)
    timeout = 120 if not args.callgrind else 10000
    logging.info("Waiting for open port...")
    for _ in range(timeout):
        time.sleep(1)
        logging.info("Trying again...")
        if server_process.poll() is not None:
            logging.error("Error: Server unexpectedly quit")
            sys.exit(1)
        # Check if port is open yet...
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            if sock.connect_ex((host, port)) == 0:
                break
        finally:
            sock.close()


def extract_cg_instructions(filename):
    try:
        with open(filename) as fd:
            r = re.compile(r"summary:\s*(.*)")
            for line in fd:
                m = r.match(line)
                if m:
                    return int(m.group(1))
        logging.error(f"Could not determine cg_instruction count in {filename}")
        sys.exit(1)
    except FileNotFoundError as e:
        raise Exception(
            f"Could not find cg.XYZ files. Was the _valgrind module installed?"
        ) from e


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--django-dir", default=None)
    parser.add_argument("--no-server", default=False, action="store_true")
    parser.add_argument("--interpreter", "-i", default=None)
    parser.add_argument("--interpreter-args", default=(), type=shlex.split)
    parser.add_argument("--callgrind", default=False, action="store_true")
    parser.add_argument("--callgrind-out-dir")
    parser.add_argument("--measure-startup", default=False, action="store_true")
    parser.add_argument("--server-log", default=None)
    parser.add_argument("--json", default=False, action="store_true")
    parser.add_argument("--verbose", "-v", action="store_true")
    parser.add_argument("--num-requests", "-n", default=None, type=int)
    parser.add_argument("--bolt-record", default=False, action="store_true")
    parser.add_argument("--perf-record", default=False, action="store_true")
    parser.add_argument("--isolate-cpu")
    args = parser.parse_args()
    if args.callgrind + args.bolt_record + args.perf_record > 1:
        raise RuntimeError(
            "Only one of --bolt-record, --perf-record, and --callgrind "
            "is supported at a time"
        )

    logging.basicConfig(
        level=logging.DEBUG if args.verbose else logging.WARN, format="%(message)s"
    )

    if not args.no_server:
        if args.django_dir is None:
            args.django_dir = default_django_dir()
        if args.interpreter is None:
            args.interpreter = default_interpreter()

    result_requests = {
        "benchmark": f"django_minimal_requests",
        "interpreter": args.interpreter,
        "interpreter_args": args.interpreter_args,
    }
    result_startup = {
        "benchmark": f"django_minimal_startup",
        "interpreter": args.interpreter,
        "interpreter_args": args.interpreter_args,
    }

    context = (
        tempfile.TemporaryDirectory()
        if args.callgrind_out_dir is None
        else nullcontext(args.callgrind_out_dir)
    )
    with context as callgrind_out_dir:
        os.makedirs(callgrind_out_dir, exist_ok=True)
        if not args.no_server:
            start_server(
                args,
                instr_atstart=args.measure_startup,
                callgrind_out_dir=callgrind_out_dir,
            )

        if args.num_requests is None:
            args.num_requests = 100 if not args.callgrind else 10

        logging.info(f"Making {args.num_requests} requests...")
        begin = time.perf_counter()
        conn = http.client.HTTPConnection(host, port)
        for i in range(0, args.num_requests):
            conn.request("GET", "/hello/test")
            response = conn.getresponse()
            data = response.read()
            if b"text=Hello&bottomtext=test" not in data:
                raise RuntimeError(f"Invalid data: {data}")
        conn.close()
        end = time.perf_counter()
        result_time = end - begin
        logging.info(f"time: {int(result_time*1000)}ms")

        if args.callgrind:
            kill_server()

            cg_instructions = extract_cg_instructions(f"{callgrind_out_dir}/cg.4")
            result_requests["cg_instructions"] = cg_instructions
            if args.measure_startup:
                cg_instructions = extract_cg_instructions(f"{callgrind_out_dir}/cg.1")
                result_startup["cg_instructions"] = cg_instructions
        else:
            result_requests["time_sec"] = result_time
    if args.perf_record:
        perf_data = f"{args.django_dir}/perf.data"
        result_requests["perf_data"] = perf_data
        result_startup["perf_data"] = perf_data
        logging.info(
            f"Wrote perf data. Open report with: perf report -i {args.django_dir}/perf.data"
        )

    result = [result_requests]
    if args.measure_startup:
        result += [result_startup]

    print(json.dumps(result, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
