#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.

import argparse
import html
import http.server
import json
import mimetypes
import os
import re
import shlex
import shutil
import socket
import ssl
import subprocess
import tempfile
import urllib.parse
from xml.etree import ElementTree as ET


TIMEOUT_SEC = 5


def run(
    cmd,
    verbose=True,
    cwd=None,
    check=True,
    capture_output=False,
    encoding="utf-8",
    **kwargs,
):
    if verbose:
        info = "$ "
        if cwd is not None:
            info += f"cd {cwd}; "
        info += " ".join(shlex.quote(c) for c in cmd)
        if capture_output:
            info += " >& ..."
        print(info)
    return subprocess.run(
        cmd,
        cwd=cwd,
        check=check,
        capture_output=capture_output,
        encoding=encoding,
        **kwargs,
    )


def annotate_assembly(ir_json, perf_annotations):
    events = perf_annotations["annotations"]
    asm = ir_json["cols"][-1]
    assert asm["name"] == "Assembly"
    for block in asm["blocks"]:
        for instr in block["instrs"]:
            instr_events = events.get(instr["address"])
            if instr_events is not None:
                instr["events"] = instr_events


TEMPLATE_DIR = os.path.dirname(os.path.realpath(__file__))
TEMPLATE_FILENAME = os.path.join(TEMPLATE_DIR, "viz.html.in")


def gen_html_from_json(ir_json, passes=None):
    with open(TEMPLATE_FILENAME, "r") as f:
        template = f.read()
    if not passes:
        passes = ["Source", "Assembly"]
    template = template.replace("@EXPANDED@", json.dumps(passes))
    return template.replace("@JSON@", json.dumps(ir_json))


# TODO(emacs): Clean up to separate HTML generation from annotated JSON
# generation
def gen_html(args):
    print("Generating HTML ...")
    json_filename = args.json
    with open(json_filename, "r") as f:
        ir_json = json.load(f)
    json_basename = os.path.basename(json_filename)
    json_dir = os.path.dirname(os.path.realpath(json_filename))
    if args.perf is not None:
        with open(args.perf, "r") as f:
            perf_annotations = json.load(f)
        annotate_assembly(ir_json, perf_annotations)
        perf_output = os.path.join(
            json_dir, json_basename.replace(".json", ".perf.json")
        )
        with open(perf_output, "w+") as f:
            json.dump(ir_json, f)
    html_output = os.path.join(json_dir, json_basename.replace(".json", ".html"))
    with open(html_output, "w+") as f:
        f.write(gen_html_from_json(ir_json))


def write_links(f, links):
    html = ET.Element("html")
    head = ET.Element("head")
    title = ET.Element("title")
    title.text = "Index"
    head.append(title)
    html.append(head)
    body = ET.Element("body")
    html.append(body)
    h1 = ET.Element("h1")
    h1.text = "Index"
    body.append(h1)
    ul = ET.Element("ol")
    body.append(ul)
    for link in links:
        li = ET.Element("li")
        a = ET.Element("a", attrib={"href": link})
        a.text = link
        li.append(a)
        ul.append(li)
    f.write(ET.tostring(html, encoding="utf-8", method="xml"))


def write_all(f, function_names):
    html = ET.Element("html")
    head = ET.Element("head")
    title = ET.Element("title")
    title.text = "All JIT functions"
    head.append(title)
    html.append(head)
    body = ET.Element("body")
    html.append(body)
    h1 = ET.Element("h1")
    h1.text = "All JIT functions, in alphabetical particular order"
    body.append(h1)
    ul = ET.Element("ol")
    body.append(ul)
    for function_name in function_names:
        li = ET.Element("li")
        a = ET.Element("a", attrib={"href": f"function_{function_name}.html"})
        a.text = function_name
        li.append(a)
        ul.append(li)
    f.write(ET.tostring(html, encoding="utf-8", method="xml"))


def write_hottest(f, perf_summary):
    html = ET.Element("html")
    head = ET.Element("head")
    title = ET.Element("title")
    title.text = "Hottest JIT functions"
    head.append(title)
    html.append(head)
    body = ET.Element("body")
    html.append(body)
    h1 = ET.Element("h1")
    h1.text = "Hottest JIT functions by cycles"
    body.append(h1)
    ul = ET.Element("ol")
    body.append(ul)
    for (function_name, cycles) in perf_summary["funcs_by_cycles"]:
        li = ET.Element("li")
        span = ET.Element("span")
        span.text = f"({cycles}) "
        li.append(span)
        a = ET.Element("a", attrib={"href": f"function_{function_name}.html"})
        a.text = function_name
        li.append(a)
        ul.append(li)
    f.write(ET.tostring(html, encoding="utf-8", method="xml"))


def removeprefix(text, prefix):
    if text.startswith(prefix):
        return text[len(prefix) :]
    return text


def removesuffix(text, suffix):
    if text.endswith(suffix):
        return text[: -len(suffix)]
    return text


def make_server_class(process_args):
    if process_args.perf is not None:
        with open(process_args.perf, "r") as f:
            perf_summary = json.load(f)
    else:
        perf_summary = None
    json_dir = process_args.json

    class IRServer(http.server.SimpleHTTPRequestHandler):
        def do_GET(self):
            self.send_response(200)
            self.send_header("Content-type", "text/html")
            self.end_headers()
            handler = self.routes.get(self.path, self.__class__.do_404)
            return handler(self)

        def do_404(self):
            if self.path.startswith("/function"):
                return self.do_function()
            self.wfile.write(f"<b>404 not found: {self.path}</b>".encode("utf-8"))

        def do_index(self):
            write_links(self.wfile, self.routes.keys())

        def do_all(self):
            function_names = sorted(
                removesuffix(removeprefix(filename, "function_"), ".json")
                for filename in os.listdir(json_dir)
                if filename.endswith(".json") and not filename.endswith(".perf.json")
            )
            write_all(self.wfile, function_names)

        def do_top(self):
            if not perf_summary:
                self.wfile.write(b"no performance data")
                return
            write_hottest(self.wfile, perf_summary)

        def do_function(self):
            match = re.findall(r"(function_.+).html", self.path)
            if not match:
                self.wfile.write(b"malformed function path")
                return
            basename = match[0]
            with open(os.path.join(json_dir, f"{basename}.json"), "r") as f:
                ir_json = json.load(f)
            if perf_summary:
                annotate_assembly(ir_json, perf_summary)
            self.wfile.write(gen_html_from_json(ir_json).encode("utf-8"))

        routes = {
            "/": do_index,
            "/all": do_all,
            "/top": do_top,
        }

    return IRServer


def make_explorer_class(process_args, prod_hostname=None):
    runtime = args.runtime
    # args.host is for listen address, whereas prod_hostname is for CSP header.
    # If it's set and non-empty, we're in a prod deployment.
    security_headers = {
        "X-Frame-Options": "SAMEORIGIN",
        "X-XSS-Protection": "1; mode=block",
        "X-Content-Type-Options": "nosniff",
    }
    if prod_hostname:
        print("Running a prod deployment...")
        # Don't set HSTS or CSP for local development
        security_headers["Content-Security-Policy"] = (
            f"default-src https://{prod_hostname}/vendor/ http://{prod_hostname}/vendor/ 'self'; "
            "img-src data: 'self';"
            "style-src 'self' 'unsafe-inline';"
            "script-src 'self' 'unsafe-inline'"
        )
        security_headers[
            "Strict-Transport-Security"
        ] = "max-age=31536000; includeSubDomains; preload"
    use_strict_compiler = args.strict

    class ExplorerServer(http.server.SimpleHTTPRequestHandler):
        def _begin_response(self, code, content_type):
            self.send_response(code)
            self.send_header("Content-type", content_type)
            for header, value in security_headers.items():
                self.send_header(header, value)

        def _get_post_params(self):
            content_length = int(self.headers["Content-Length"])
            post_data = self.rfile.read(content_length)
            bytes_params = urllib.parse.parse_qs(post_data)
            str_params = {
                param.decode("utf-8"): [value.decode("utf-8") for value in values]
                for param, values in bytes_params.items()
            }
            return str_params

        def do_GET(self):
            handler = self.routes.get(self.path, self.__class__.do_404)
            return handler(self)

        def do_POST(self):
            self.params = self._get_post_params()
            handler = self.post_routes.get(self.path, self.__class__.do_404)
            return handler(self)

        def do_404(self):
            try:
                static_file = os.path.join(TEMPLATE_DIR, self.path.lstrip("/"))
                content_type = mimetypes.guess_type(static_file)[0] or "text/html"
                with open(static_file, "rb") as f:
                    self._begin_response(200, content_type)
                    self.end_headers()
                    self.wfile.write(f.read())
            except FileNotFoundError:
                self._begin_response(404, "text/html")
                self.end_headers()
                self.wfile.write(f"<b>404 not found: {self.path}</b>".encode("utf-8"))

        def do_compile_get(self):
            self._begin_response(200, "text/html")
            self.end_headers()
            self.wfile.write(b"Waiting for input...")

        def do_helth(self):
            self._begin_response(200, "text/plain")
            self.end_headers()
            self.wfile.write(b"I'm not dead yet")

        def do_explore(self):
            self._begin_response(200, "text/html")
            self.end_headers()
            with open(os.path.join(TEMPLATE_DIR, "explorer.html.in"), "r") as f:
                template = f.read()
            try:
                version_cmd = run(
                    [runtime, "-c", "import sys; print(sys.version)"],
                    capture_output=True,
                )
                version = version_cmd.stdout.partition("\n")[0]
            except Exception:
                version = "unknown"
            template = template.replace("@VERSION@", version)
            self.wfile.write(template.encode("utf-8"))

        def _render_options(self, *options):
            result = []
            for option in options:
                result.append("-X")
                if isinstance(option, str):
                    result.append(option)
                else:
                    result.append("=".join(option))
            return result

        def do_compile_post(self):
            self._begin_response(200, "text/html")
            self.end_headers()
            passes = self.params["passes"]
            user_code = self.params["code"][0]
            use_static_python = (
                "static-python" in self.params
                and self.params["static-python"][0] == "on"
            )
            if use_strict_compiler and not use_static_python:
                # Static implies strict
                # TODO(T120976390): Put this after future imports
                user_code = "import __strict__\n" + user_code
            asm_syntax = self.params["asm-syntax"][0]
            with tempfile.TemporaryDirectory() as tmp:
                lib_name = "explorer_lib"
                with open(os.path.join(tmp, f"{lib_name}.py"), "w+") as f:
                    f.write(user_code)
                main_code_path = os.path.join(tmp, "main.py")
                with open(main_code_path, "w+") as f:
                    f.write(
                        f"from {lib_name} import *\n"
                        "import cinderjit\n"
                        "cinderjit.disable()\n"
                    )
                jitlist_path = os.path.join(tmp, "jitlist.txt")
                with open(jitlist_path, "w+") as f:
                    f.write(f"{lib_name}:test\n")
                json_dir = os.path.join(tmp, "json")
                jit_options = self._render_options(
                    "jit",
                    "jit-enable-jit-list-wildcards",
                    "jit-enable-hir-inliner",
                    "jit-shadow-frame",
                    ("jit-list-file", jitlist_path),
                    ("jit-dump-hir-passes-json", json_dir),
                    ("jit-asm-syntax", asm_syntax),
                    "usepycompiler",
                )
                timeout = ["timeout", "--signal=KILL", f"{TIMEOUT_SEC}s"]
                if use_static_python or use_strict_compiler:
                    jit_options += ["-X", "install-strict-loader"]
                try:
                    run(
                        [*timeout, runtime, *jit_options, main_code_path],
                        capture_output=True,
                        cwd=tmp,
                    )
                except subprocess.CalledProcessError as e:
                    if e.returncode == -9:
                        self.wfile.write(b"Command timed out")
                        return
                    if "SyntaxError" in e.stderr or "StrictModuleError" in e.stderr:
                        escaped = html.escape(e.stderr)
                        self.wfile.write(f"<pre>{escaped}</pre>".encode("utf-8"))
                        return
                    print(e.stderr)
                    self.wfile.write(b"Internal server error")
                    return
                # TODO(emacs): Render more than one function
                files = os.listdir(json_dir)
                if not files:
                    self.wfile.write(
                        b"No compiled code found. Did you remember to name your function <code>test</code>?"
                    )
                    return
                func_json_filename = files[0]
                with open(os.path.join(json_dir, func_json_filename), "r") as f:
                    ir_json = json.load(f)
            ir_json["cols"] = [col for col in ir_json["cols"] if col["name"] in passes]
            generated_html = gen_html_from_json(ir_json, passes)
            with_code = generated_html.replace("@CODE@", user_code)
            self.wfile.write(with_code.encode("utf-8"))

        routes = {
            "/": do_explore,
            "/compile": do_compile_get,
            "/helth": do_helth,
        }

        post_routes = {
            "/compile": do_compile_post,
        }

    return ExplorerServer


class HTTPServerIPV6(http.server.HTTPServer):
    address_family = socket.AF_INET6

    def use_tls(self, certfile, keyfile=None):
        self.socket = ssl.wrap_socket(
            self.socket,
            server_side=True,
            certfile=certfile,
            keyfile=keyfile,
            ssl_version=ssl.PROTOCOL_TLS,
        )


def gen_server(args):
    host = args.host
    port = args.port
    server_address = (host, port)
    IRServer = make_server_class(args)
    httpd = HTTPServerIPV6(server_address, IRServer)
    if args.tls_certfile:
        httpd.use_tls(args.tls_certfile, args.tls_keyfile)
    print(f"Serving traffic on {host}:{port} ...")
    httpd.serve_forever()


def gen_explorer(args):
    host = args.host
    port = args.port
    explorer_address = (host, port)
    prod_hostname = os.getenv("CINDER_EXPLORER_HOSTNAME")
    IRServer = make_explorer_class(args, prod_hostname)
    httpd = HTTPServerIPV6(explorer_address, IRServer)
    if args.tls_certfile:
        httpd.use_tls(args.tls_certfile, args.tls_keyfile)
    print(f"Serving traffic on {host}:{port} ...")
    httpd.serve_forever()


def executable_file(arg):
    if not shutil.which(arg, mode=os.F_OK | os.X_OK):
        parser.error(f"The file {arg} does not exist or is not an executable file")
    return arg


def readable_file(arg):
    if not shutil.which(arg, mode=os.F_OK):
        parser.error(f"The file {arg} does not exist or is not a readable file")
    return arg


def add_server_args(parser):
    parser.add_argument(
        "--host",
        type=str,
        help="Listen address for serving traffic",
        default="::",
    )
    parser.add_argument(
        "--port",
        type=int,
        help="Port for serving traffic",
        default=8081,
    )
    parser.add_argument(
        "--tls-certfile",
        type=readable_file,
        help="Path to TLS certificate file. If .crt, also provide --tls-keyfile.",
        default=None,
    )
    parser.add_argument(
        "--tls-keyfile",
        type=readable_file,
        help="Path to TLS key file. Use with --tls-certfile.",
        default=None,
    )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Generate an HTML visualization of Cinder IRs"
    )
    parser.add_argument("--perf", type=str, help="JSONified perf events by address")
    subparsers = parser.add_subparsers()

    # Generate HTML statically from JSON
    static_parser = subparsers.add_parser(
        "static", help="Generate a single HTML page in one shot"
    )
    static_parser.add_argument(
        "json",
        type=str,
        help="Single JSON file generated by -X jit-dump-hir-passes-json",
    )
    static_parser.set_defaults(func=gen_html)

    # Run a server to dynamically generate HTML from JSON
    server_parser = subparsers.add_parser(
        "server",
        help="Serve dynamically-generated HTML pages based on continually-updating .json files",
    )
    server_parser.set_defaults(func=gen_server)
    server_parser.add_argument(
        "json",
        type=str,
        help="Directory containing JSON IRs for functions",
    )
    add_server_args(server_parser)

    # Run a Godbolt-style Cinder Explorer
    explorer_parser = subparsers.add_parser("explorer")
    explorer_parser.add_argument(
        "--runtime",
        type=executable_file,
        help="Path to Cinder runtime used for generating JSON",
        default=os.path.expanduser("~/local/cinder/build/python"),
    )
    explorer_parser.add_argument(
        "--strict", action="store_true", help="Enforce strict modules"
    )
    add_server_args(explorer_parser)
    explorer_parser.set_defaults(func=gen_explorer)

    args = parser.parse_args()
    if not hasattr(args, "func"):
        raise Exception("Missing sub-command. See --help.")
    args.func(args)
