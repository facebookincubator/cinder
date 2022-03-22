#!/usr/bin/env python3
# Copyright (c) Facebook, Inc. and its affiliates. (http://www.facebook.com)

import argparse
import json
import os
import sys
import socket
import http.server
import re
from xml.etree import ElementTree as ET


def annotate_assembly(ir_json, perf_annotations):
    event_names = perf_annotations["events"]
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


def gen_html_from_json(ir_json):
    with open(TEMPLATE_FILENAME, "r") as f:
        template = f.read()
    return template.replace("@JSON@", json.dumps(ir_json))


# TODO(emacs): Clean up to separate HTML generation from annotated JSON
# generation
def gen_html(args):
    print("Generating HTML ...")
    json_filename = args.json
    with open(json_filename, "r") as f:
        ir_json = json.load(f)
    json_basename = os.path.basename(json_filename)
    if args.perf is not None:
        with open(args.perf, "r") as f:
            perf_annotations = json.load(f)
        annotate_assembly(ir_json, perf_annotations)
        perf_output = os.path.join(
            json_dir, json_basename.replace(".json", ".perf.json")
        )
        with open(perf_output, "w+") as f:
            json.dump(ir_json, f)
    json_dir = os.path.dirname(os.path.realpath(json_filename))
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


class HTTPServerIPV6(http.server.HTTPServer):
    address_family = socket.AF_INET6


def gen_server(args):
    host = args.host
    port = args.port
    server_address = (host, port)
    IRServer = make_server_class(args)
    httpd = HTTPServerIPV6(server_address, IRServer)
    print(f"Serving traffic on {host}:{port} ...")
    httpd.serve_forever()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Generate an HTML visualization of Cinder IRs"
    )
    parser.add_argument("--perf", type=str, help="JSONified perf events by address")
    subparsers = parser.add_subparsers()
    static_parser = subparsers.add_parser(
        "static", help="Generate a single HTML page in one shot"
    )
    static_parser.add_argument(
        "json",
        type=str,
        help="Single JSON file generated by -X jit-dump-hir-passes-json",
    )
    static_parser.set_defaults(func=gen_html)
    server_parser = subparsers.add_parser(
        "server",
        help="Serve dynamically-generated HTML pages based on continually-updating .json files",
    )
    server_parser.set_defaults(func=gen_server)
    server_parser.add_argument(
        "--host",
        type=str,
        help="Listen address for serving traffic",
        default="::",
    )
    server_parser.add_argument(
        "--port",
        type=int,
        help="Port for serving traffic",
        default=8081,
    )
    server_parser.add_argument(
        "json",
        type=str,
        help="Directory containing JSON IRs for functions",
    )
    args = parser.parse_args()
    if not hasattr(args, "func"):
        raise Exception("Missing sub-command. See --help.")
    args.func(args)
