#!/usr/bin/env qore
# -*- mode: qore; indent-tabs-mode: nil -*-
# Copyright (C) 2026 Qore Technologies, s.r.o.
#
# Child process of connect-targets.qtest: opens each datasource given (a JSON list of hashes with "db" and, if set,
# "host" and "port") from a sandboxed Program whose network policy allows everything but DeniedAddress, and prints a
# JSON list with the result of each: {"ok": true} if it connected, else the error and its description.  It runs in
# a process of its own so that the test can give it the environment that the Oracle client reads (TNS_ADMIN, HOME,
# ORACLE_HOME).

%modern
%requires oracle
%requires json

#! the address that the sandbox denies
const DeniedAddress = "10.255.255.2";

#! the sandboxed code: it opens a datasource that it creates
const SandboxedSource = "
hash<auto> sub try_open(hash<auto> conn) {
    try {
        Datasource ds({\"type\": \"oracle\", \"user\": \"qtest\", \"pass\": \"qtest\", \"db\": conn.db}
            + (conn.host ? {\"host\": conn.host, \"port\": conn.port} : {}));
        ds.open();
        ds.close();
        return {\"ok\": True};
    } catch (hash<ExceptionInfo> ex) {
        return {\"err\": ex.err, \"desc\": ex.desc};
    }
}
";

Program p(PO_NEW_STYLE);
p.parse(SandboxedSource, "sandboxed");
SandboxManager sm();
sm.setNetworkDefaultPolicy(True);
sm.addNetworkDeniedIPRange(DeniedAddress + "/32");
p.setSandboxManager(sm);

list<auto> rv = ();
foreach hash<auto> conn in (parse_json(ARGV[0])) {
    rv += p.callFunction("try_open", conn);
}
print(make_json(rv) + "\n");
