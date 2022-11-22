#!/bin/bash
set -eu
if [ "$(ls -A ".")" ]; then
    echo "This script should be run in an empty writable directory"
    exit 1
fi

set -x
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
CURL_FLAGS="-L"
# Setup devserver proxy config
if hash fwdproxy-config 2>/dev/null; then
    CURL_FLAGS="${CURL_FLAGS} $(fwdproxy-config curl)"
fi
for url in \
    https://files.pythonhosted.org/packages/df/d5/3e3ff673e8f3096921b3f1b79ce04b832e0100b4741573154b72b756a681/pytz-2019.1.tar.gz \
    https://files.pythonhosted.org/packages/63/c8/229dfd2d18663b375975d953e2bdc06d0eed714f93dcb7732f39e349c438/sqlparse-0.3.0.tar.gz \
    https://files.pythonhosted.org/packages/2f/96/7d56b16388e8686ef8e2cb330204f247a90e6f008849dad7ce61c9c21c84/Django-1.11.22.tar.gz \

do
    archive="$(basename "${url}")"
    if ! [ -e "$archive" ]; then
        eval "curl ${CURL_FLAGS} -o ${archive} ${url}"
    fi
    tar -xzf "${archive}"
done

ln -s Django-1.11.22/django .
ln -s pytz-2019.1/pytz .
ln -s sqlparse-0.3.0/sqlparse .

# Touch all the files, to give them a newer changed date. This helps people
# installing django in /var/tmp as it tames aggressive devserver scripts
# cleaning old files from /var/tmp.
find . -print0 | xargs -0 touch

# Copy patched mimes.type file.
ln -sf "$SCRIPT_DIR/files/mysite" .
cp "$SCRIPT_DIR/files/run.py" .
