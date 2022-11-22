#!/usr/bin/env python3
import mimetypes
import os

import django
from django.conf import settings
from django.core import management


mimetypes.knownfiles = ["./mime.types"]


os.environ["DJANGO_SETTINGS_MODULE"] = "mysite.settings"
django.setup()
management.call_command("runserver", use_reloader=False, use_threading=False)
