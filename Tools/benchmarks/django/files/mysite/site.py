#!/usr/bin/env python3

import os
import sys

from django.conf.urls import url
from django.http import HttpResponse


try:
    from _valgrind import callgrind_dump_stats, callgrind_start_instrumentation
except ImportError:

    def callgrind_dump_stats(description):
        pass

    def callgrind_start_instrumentation():
        pass


if os.environ.get("DJANGO_PROFILE"):
    import _profiler

    print("Enabling python profiling.")
    _profiler.install()
    profiler_dump = _profiler.dump_callgrind
else:

    def profiler_dump(filename):
        pass


try:
    from _builtins import _gc

    _gc()
except ModuleNotFoundError:
    pass


def index(request):
    return HttpResponse(
        f'<html><body><img src="https://lookaside.internalfb.com/intern/macros/attachment/?fbid=1521180484640888&attachment_id=287065748458333" alt="it\'s alive">(powered by {sys.implementation.name})</body></html>'
    )


def hello(request, name):
    return HttpResponse(
        f'<html><body><img src="https://internalfb.com/intern/memes/image/?type=2849144811805277&text=Hello&bottomtext={name}">(powered by {sys.implementation.name})</body></html>'
    )


urlpatterns = [url(r"^hello/(?P<name>.*)$", hello), url("", index)]

request_num = 0


def valgrind_middleware(get_response):
    def middleware(request):
        global request_num
        response = get_response(request)
        callgrind_dump_stats(f"request_{request_num}")
        profiler_dump(f"request_{request_num}.cg")
        callgrind_start_instrumentation()
        request_num += 1
        return response

    return middleware
