#!/bin/bash
# Copyright (C) 2026 Qore Technologies, s.r.o.
#
# ci_source_env() (ci_env.sh) sources a file that holds credentials.  This checks that:
# - the values never reach a shell trace, and neither does the toggling of the trace
# - the caller's tracing state is kept, whether it was on or off, also when the file nests another ci_source_env call
# - the variables are exported to child processes
# - "set -e" stays in effect inside the file, and the status of the file is returned without it
set -u
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
sentinel=FAKE-SENTINEL-NOT-A-CREDENTIAL
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0

printf 'export CI_ENV_TEST_VALUE=%s\n' "$sentinel" > "$work/env.sh"
printf '. "%s/ci_env.sh"\nci_source_env "%s/env.sh"\nexport CI_ENV_TEST_NESTED=1\n' "$here" "$work" > "$work/nested.sh"
printf 'false\nexport CI_ENV_TEST_AFTER=1\n' > "$work/fails.sh"
printf 'export CI_ENV_TEST_VALUE=%s\nreturn 7\n' "$sentinel" > "$work/status.sh"

# expect <name> <expected status> <expected output> <script>: the script's stdout is compared, its trace is searched
expect() {
    local name=$1 expected_rc=$2 expected_out=$3 script=$4 out rc
    out=$(bash -c ". \"$here/ci_env.sh\"; $script" 2> "$work/trace")
    rc=$?
    if [ "$rc" != "$expected_rc" ] || [ "$out" != "$expected_out" ]; then
        echo "FAIL: $name: expected status $expected_rc and output '$expected_out', got $rc and '$out'"
        fail=1
    elif grep -q -F "$sentinel" "$work/trace"; then
        echo "FAIL: $name: the value is in the trace"
        fail=1
    elif grep -q -E 'set [-+]x|ci_source_env_' "$work/trace"; then
        echo "FAIL: $name: the toggling of the trace is in the trace"
        fail=1
    else
        echo "ok: $name"
    fi
}

# the expected value is read from a file: on the command line, it would be in the caller's trace
printf '%s\n' "$sentinel" > "$work/expected"
child='bash -c "[ \"\$CI_ENV_TEST_VALUE\" = \"\$(cat '$work'/expected)\" ] && echo exported"'
flag='case $- in *x*) echo on ;; *) echo off ;; esac'

expect "tracing stays on" 0 on "set -x; ci_source_env '$work/env.sh'; { $flag; } 2>/dev/null"
expect "tracing stays off" 0 off "ci_source_env '$work/env.sh'; $flag"
expect "exported to a child with tracing on" 0 exported "set -x; ci_source_env '$work/env.sh'; $child"
expect "exported to a child with tracing off" 0 exported "ci_source_env '$work/env.sh'; $child"
expect "a nested call keeps tracing on" 0 on "set -x; ci_source_env '$work/nested.sh'; { $flag; } 2>/dev/null"
expect "a nested call keeps tracing off" 0 off "ci_source_env '$work/nested.sh'; $flag"
expect "a nested call exports" 0 exported "set -x; ci_source_env '$work/nested.sh'; $child"
expect "set -e applies inside the file" 1 "" "set -e; ci_source_env '$work/fails.sh'; echo continued"
expect "set -e applies to a missing file" 1 "" "set -e; ci_source_env '$work/missing.sh' 2>/dev/null; echo continued"
expect "the status is returned without set -e" 0 "7 on" \
    "set -x; ci_source_env '$work/status.sh'; { rc=\$?; echo \"\$rc \$(case \$- in *x*) echo on ;; *) echo off ;; esac)\"; } 2>/dev/null"
exit $fail
