#!/bin/bash
# Copyright (C) 2026 Qore Technologies, s.r.o.
#
# init_oracle.sh waits for the Oracle service with oracle_wait_for_db() (oracle_wait.sh).  This checks:
# - the errors that a starting gvenzl/oracle-free container reports are waited out, and any other error is fatal at
#   once, with the error lines OCI produces, so that a start-up error that is added to or dropped from the list is
#   noticed
# - the outcome of a wait for sequences of connection attempts: a start, a service container that exits after its
#   listener answered, a listener that never answers, a fatal error
set -u
here=$(cd "$(dirname "$0")" && pwd)
. "$here/oracle_wait.sh"
pattern=$(sed -n 's/.*grep -E "\(QoreOracleConnection::logon[^"]*\)".*/\1/p' "$here/oracle_wait.sh" | head -n 1)
if [ -z "$pattern" ]; then
    echo "FAIL: cannot find the start-up error pattern in oracle_wait.sh"
    exit 1
fi
fail=0
check() {
    expected=$1
    line="DBI:ORACLE:OCI-ERROR: system@oracle: QoreOracleConnection::logon() session begin: $2"
    if echo "$line" | grep -E "$pattern" > /dev/null 2>&1; then got=wait; else got=fatal; fi
    if [ "$got" != "$expected" ]; then
        echo "FAIL: $2: expected $expected, got $got"
        fail=1
    else
        echo "ok: $2 -> $got"
    fi
}
for code in 01017 01033 01034 01089 01109 03113 12514 12528 12541; do
    check wait "ORA-$code: reported while the container starts"
done
# anything else is fatal at once
for code in 00942 01031 12154 12170 12545 28000 03114 03135; do
    check fatal "ORA-$code: not a start-up error"
done

# the scripted connection attempts: one per line of $script, "OK" for a successful attempt, else the ORA code of the
# error; the probe runs in a subshell, so the number of attempts made is kept in a file
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
script=
probe() {
    local n
    n=$(cat "$work/n")
    echo $((n + 1)) > "$work/n"
    local step
    step=$(echo "$script" | sed -n "$((n + 1))p")
    if [ "$step" = OK ]; then
        echo "Oracle Database 23ai Free"
        return 0
    fi
    if [ -z "$step" ]; then
        # past the end of the script: no listener
        step=ORA-12541
    fi
    echo "DBI:ORACLE:OCI-ERROR: system@oracle: QoreOracleConnection::logon() server attach: $step: scripted"
    return 1
}
# scenario <name> <expected status> <expected attempts> <max tries> <steps...>
scenario() {
    local name=$1 expected=$2 attempts=$3 tries=$4
    shift 4
    script=$(printf '%s\n' "$@")
    echo 0 > "$work/n"
    oracle_wait_for_db probe "$tries" 0 > "$work/out" 2>&1
    local rc=$?
    local n
    n=$(cat "$work/n")
    if [ $rc != "$expected" ] || [ "$n" != "$attempts" ]; then
        echo "FAIL: $name: expected status $expected after $attempts attempts, got $rc after $n:"
        sed 's/^/    /' "$work/out"
        fail=1
    else
        echo "ok: $name -> status $rc after $n attempts"
    fi
}
scenario "a start" 0 4 60 ORA-12541 ORA-12514 ORA-01109 OK
scenario "a start after a listener error that ORA-12541 does not follow twice" 0 5 60 \
    ORA-12514 ORA-12541 ORA-12514 ORA-12541 OK
# job 239973: the service container exited after its listener had answered
scenario "the container exits after the listener answered" 1 4 60 ORA-12541 ORA-12514 ORA-12541 ORA-12541
scenario "the container exits while the database opens" 1 5 60 ORA-12514 ORA-01033 ORA-01109 ORA-12541 ORA-12541
# no listener at all: the deadline applies (max tries + 1 attempts)
scenario "the listener never answers" 2 4 3 ORA-12541
scenario "the database never opens" 2 4 3 ORA-01109 ORA-01109 ORA-01109 ORA-01109
# the listener gone once when the deadline is reached is the deadline; gone twice is an exited container
scenario "a listener gone once at the deadline" 2 3 2 ORA-12514 ORA-12514 ORA-12541
scenario "a listener gone twice at the deadline" 1 3 2 ORA-12514 ORA-12541 ORA-12541
scenario "an error that is not a start-up error" 3 2 60 ORA-12541 ORA-28000
scenario "an attempt without an Oracle error" 3 1 60 "x"
exit $fail
