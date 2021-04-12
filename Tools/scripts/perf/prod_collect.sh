#!/bin/bash -e

# Collect a perf sample, perf pid maps, and disassembly logs (if present) into
# a gzipped tarball on stdout.

function log {
    echo "$@" 1>&2
}

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <sample time>" 1>&2
    exit 1
fi

declare -r SAMPLE_TIME=$1

# It was determined through experimentation that for the default sample time of
# 6 seconds, a sample period of 10,000,000 for cycles and 10,000 for everything
# else provides a good tradeoff of good data without taking too long to process
# or collect.
#
# The actual constants used here are the closest prime numbers to the target
# periods. A sample period with no nontrivial factors reduces the chances of
# any regular events in the workload synchronizing with the sampling, which
# could bias the data towards those events.
declare -r CYCLES_PERIOD=1000003
declare -r DEFAULT_PERIOD=10007

# TODO(bsimmers): Make this configurable. May be tricky because of the sample
# period selection explained above.
declare -r EVENTS="cycles branch-misses L1-icache-misses L1-dcache-misses iTLB-misses dTLB-misses"

CREATED_FILES=()
FILES=()

declare -r PERF_SCRIPT_FILE=perf.samples
CREATED_FILES+=("$PERF_SCRIPT_FILE")

rm -f "$PERF_SCRIPT_FILE"

for EVENT in $EVENTS; do
    EVENT_SUFFIX=
    if [[ "$EVENT" == "cycles" || "$EVENT" == "branch-misses" ]]; then
        EVENT_SUFFIX=":pp"
    fi

    EVENT_PERIOD="$DEFAULT_PERIOD"
    if [[ "$EVENT" == "cycles" ]]; then
        EVENT_PERIOD="$CYCLES_PERIOD"
    fi

    PERF_DATA_FILE="perf.data.$EVENT"
    perf record -o "perf.data.$EVENT" -a -g -e "$EVENT$EVENT_SUFFIX" -c "$EVENT_PERIOD" -- sleep "$SAMPLE_TIME"
    CREATED_FILES+=("$PERF_DATA_FILE")

    perf script -i "$PERF_DATA_FILE" -c uwsgi --fields comm,pid,time,event,period,ip,sym >> "$PERF_SCRIPT_FILE"
done

for PID in $(pgrep -f 'async uWSGI'); do
    FILES+=("/tmp/perf-$PID.map")
done

declare -r DISAS_FILE="/tmp/cinder-disas-$(pgrep -f 'async uWSGI master').log"
if [[ -f "$DISAS_FILE" ]]; then
    FILES+=("$DISAS_FILE")
fi

tar -cz "${FILES[@]}" "${CREATED_FILES[@]}"

rm -f "${CREATED_FILES[@]}"
