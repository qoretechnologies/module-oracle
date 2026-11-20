#!/bin/sh
# Copyright (C) 2026 Qore Technologies, s.r.o.
#
# init_oracle.sh waits out the errors a starting gvenzl/oracle-free container reports and treats any other error as
# fatal at once: this checks the pattern with the error lines OCI produces, so that a start-up error that is added
# to or dropped from the list is noticed.
set -u
here=$(cd "$(dirname "$0")" && pwd)
pattern=$(sed -n 's/.*grep -E "\(QoreOracleConnection::logon[^"]*\)".*/\1/p' "$here/init_oracle.sh" | head -n 1)
if [ -z "$pattern" ]; then
    echo "FAIL: cannot find the start-up error pattern in init_oracle.sh"
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
exit $fail
