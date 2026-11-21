/*  oracle-redact.cpp Copyright (C) 2026 Qore Technologies, s.r.o.

    Unit tests of the masking of connection values in Oracle messages (src/OracleRedact.h, qore#6991); the header
    has no dependencies, so the test needs neither Qore nor an Oracle client.  Run by oracle-redact.qtest and
    test_oracle_redact.py.
*/

#include "../src/OracleRedact.h"

#include <cstdio>
#include <string>

using namespace qore_oracle_redact;

static int failures = 0;
static int checks = 0;

static void check(const char* user, const char* pass, const char* db, const std::string& in,
        const std::string& expected, const char* label) {
    ++checks;
    std::string text = in;
    redact(text, get_secrets(user, pass, db));
    if (text != expected) {
        ++failures;
        printf("FAILED: %s\n  input:    %s\n  expected: %s\n  actual:   %s\n", label, in.c_str(), expected.c_str(),
            text.c_str());
    }
}

int main() {
    // the cases of qore#6991
    check("u_SECRET1", "p_SECRET2", "d_SECRET3",
        "ORA-12514: Cannot connect to database. Service d_SECRET3 is not registered with the listener at host "
        "127.0.0.1 port 1521.",
        "ORA-12514: Cannot connect to database. Service **** is not registered with the listener at host "
        "127.0.0.1 port 1521.", "unknown service");
    check("u_SECRET1", "p_SECRET2", "d_SECRET3",
        "ORA-12154: Cannot connect to database. Cannot find alias d_SECRET3 in /x/tnsnames.ora.",
        "ORA-12154: Cannot connect to database. Cannot find alias **** in /x/tnsnames.ora.", "unknown alias");
    check("u_SECRET1", "p_SECRET2", "FREEPDB1",
        "ORA-00942: table or view \"U_SECRET1\".\"T\" does not exist\nHelp: https://docs.oracle.com/error-help/db/"
        "ora-00942/",
        "ORA-00942: table or view \"****\".\"T\" does not exist\nHelp: https://docs.oracle.com/error-help/db/"
        "ora-00942/", "schema in upper case");

    // a password that is unlikely to be template text is matched exactly, as a whole name
    check("scott", "Tiger123", "orcl", "x Tiger123 (Tiger123) xTiger123 Tiger1234", "x **** (****) xTiger123 Tiger1234",
        "password as a whole name");
    check("scott", "Tiger123", "orcl", "TIGER123 tiger123", "TIGER123 tiger123", "password case-sensitive");
    check("scott", "s3cr3t!pw", "orcl", "ORA-00900: invalid SQL statement: alter user x identified by s3cr3t!pw",
        "ORA-00900: invalid SQL statement: alter user x identified by ****", "password in SQL text");

    // a password that may be template text is not masked, as the mask would show it (qore#6991)
    check("scott", "Service", "orcl", "ORA-12514: Service svc is not registered",
        "ORA-12514: Service svc is not registered", "password that is a template word");
    check("scott", "1521", "orcl", "port 1521", "port 1521", "password that is a template number");
    check("scott", "connect_identifier", "orcl", "could not resolve the connect_identifier",
        "could not resolve the connect_identifier", "long password of letters and punctuation");
    check("scott", "127.0.0.10", "orcl", "host 127.0.0.10", "host 127.0.0.10",
        "long password of digits and punctuation");
    check("scott", "Abc1234", "orcl", "Abc1234", "Abc1234", "password shorter than 8 characters");
    check("scott", "ORA-12514", "orcl", "ORA-12514: x", "ORA-12514: x", "password shaped like a message code");
    // template text that spans several words
    check("scott", "00942: table", "orcl", "ORA-00942: table or view does not exist",
        "ORA-00942: table or view does not exist", "password of a code number and a word");
    check("scott", "ORA-00942: table", "orcl", "ORA-00942: table or view does not exist",
        "ORA-00942: table or view does not exist", "password of a code and a word");
    check("scott", "port 1521", "orcl", "at host h port 1521.", "at host h port 1521.",
        "password of a word and a number");
    check("scott", "db/ora-12514", "orcl", "https://docs.oracle.com/error-help/db/ora-12514/",
        "https://docs.oracle.com/error-help/db/ora-12514/", "password of help URL text");
    check("scott", "PORT=1521", "orcl", "(PORT=1521)", "(PORT=1521)", "password of descriptor text");
    {
        ++checks;
        bool ok = !password_maskable("") && !password_maskable("abcdefgh") && !password_maskable("12345678")
            && !password_maskable("a1b2c3d") && password_maskable("a1b2c3d4") && password_maskable("Abc-12345x")
            && !password_maskable("Abc-12345") && !password_maskable("error-help")
            && !password_maskable("docs.oracle.com") && !password_maskable("10.0.0.123")
            && !password_maskable("00942: table") && !password_maskable("port 1521")
            && password_maskable("s3cr3t!pw") && password_maskable("pass 12x4");
        if (!ok) {
            ++failures;
            printf("FAILED: password_maskable()\n");
        }
    }

    // names are matched without regard to case, as whole names only
    check("scott", "pw", "orcl", "SCOTT scott Scott", "**** **** ****", "user any case");
    check("scott", "pw", "orcl", "SCOTTY xscott scott_1 scott$ scott# $scott",
        "SCOTTY xscott scott_1 scott$ scott# $scott",
        "user inside a longer name");
    check("scott", "pw", "orcl", "(scott) 'scott'. scott.t", "(****) '****'. ****.t", "user next to punctuation");
    check("a", "pw", "orcl", "a table at A.B", "**** table at ****.B", "one-letter user");

    // message codes are kept
    check("ora", "pw", "tns", "ORA-12154: TNS-03505: ora-12154 ora tns", "ORA-12154: TNS-03505: ora-12154 **** ****",
        "user and db named like a code prefix");
    check("scott", "ORA", "orcl", "ORA-00942 ORA", "ORA-00942 ORA", "password named like a code prefix");
    check("scott", "12514", "orcl", "ORA-12514: port 12514 x12514", "ORA-12514: port 12514 x12514",
        "password named like a code number");
    check("scott", "pass12514", "orcl", "ORA-12514: pass12514", "ORA-12514: ****", "password with a code number");
    check("12514", "pw", "00942", "ORA-12514 ORA-00942 12514 00942", "ORA-12514 ORA-00942 **** ****",
        "names named like a code number");
    check("scott", "Abc-12345x", "orcl", "Abc-12345x xAbc-12345x", "**** xAbc-12345x",
        "password that starts like a code");
    check("prod", "pw", "orcl", "PROD-1 PROD-123456 prod-12345", "****-1 ****-123456 prod-12345",
        "only a five-digit code is kept");
    check("scott", "pw", "ERP-20240", "Service ERP-20240 is not registered; erp-20240",
        "Service **** is not registered; ****",
        "a database named like a code");
    check("scott", "pw", "erp-20240.example.com", "Service erp-20240.example.com is not registered",
        "Service **** is not registered", "a database starting like a code");
    check("scott", "00942: tab1", "orcl", "ORA-00942: tab1 x", "ORA-**** x", "a password running past a code");
    check("scott", "pw", "dbhost:1521/svc", "dbhost:1521/svc dbhost 1521", "**** dbhost 1521",
        "a host or port on its own is not masked");
    {
        ++checks;
        std::vector<std::string> names = {"APP-12345"};
        std::string text = "ORA-12514: Service APP-12345 is not registered";
        redact(text, get_secrets("scott", "pw", "alias1", &names));
        if (text != "ORA-12514: Service **** is not registered") {
            ++failures;
            printf("FAILED: alias service named like a code: %s\n", text.c_str());
        }
    }

    // longer values are masked first
    check("db", "pw", "db_prod", "db_prod db", "**** ****", "longer value first");
    check("user", "user1pass", "orcl", "user1pass user", "**** ****", "password containing the user name");

    // database values: Easy Connect strings and connect descriptors
    check("scott", "pw", "dbhost:1521/svc.example.com",
        "Service svc.example.com is not registered at host dbhost port 1521; dbhost:1521/svc.example.com",
        "Service **** is not registered at host dbhost port 1521; ****", "Easy Connect service");
    check("scott", "pw", "//dbhost/svc:dedicated/inst1", "svc inst1 dbhost", "**** **** dbhost",
        "Easy Connect server type and instance");
    check("scott", "pw", "[::1]:1521/svc?connect_timeout=5", "svc ::1", "**** ::1", "Easy Connect with IPv6 host");
    check("scott", "pw", "tcps://dbhost:2484/svc", "Service svc on dbhost", "Service **** on dbhost",
        "Easy Connect with the tcps protocol");
    check("scott", "pw", "tcp://dbhost/svc/inst", "svc inst dbhost tcp", "**** **** dbhost tcp",
        "Easy Connect with the tcp protocol and an instance");
    check("scott", "pw",
        "(DESCRIPTION=(ADDRESS=(PROTOCOL=TCP)(HOST=h)(PORT=1521))(CONNECT_DATA=(service_name = SvcA )(SID=sid1)"
        "(INSTANCE_NAME=\"inst2\")))",
        "Service svca and sid1 and inst2 on h", "Service **** and **** and **** on h", "connect descriptor values");
    check("scott", "pw", "(DESCRIPTION=(XSERVICE_NAME=n1)(CONNECT_DATA=(MYSID=n2)))", "n1 n2 descriptor",
        "n1 n2 descriptor", "only whole descriptor parameter names");

    // proxy users
    check("proxy[client]", "pw", "orcl", "PROXY and CLIENT and proxy[client]", "**** and **** and ****",
        "proxy user parts");

    // quoted user names
    check("\"Scott\"", "pw", "orcl", "\"Scott\" Scott SCOTT", "**** **** ****", "quoted user name");

    // other database names: the service of the TNS alias
    {
        ++checks;
        std::vector<std::string> names = {"FREEPDB1", ""};
        std::string text = "ORA-12514: Service FREEPDB1 is not registered; alias oracle";
        redact(text, get_secrets("scott", "pw", "oracle", &names));
        if (text != "ORA-12514: Service **** is not registered; alias ****") {
            ++failures;
            printf("FAILED: alias service names: %s\n", text.c_str());
        }
    }

    // overlapping values: every value is masked whole
    check("abc", "pw", "abcdef", "abcdef abc", "**** ****", "overlapping names");
    check("bar-baz", "pw", "foo-bar", "foo-bar-baz; foo-bar; bar-baz", "****; ****; ****",
        "values that overlap each other");
    check("scott", "-", "orcl", "ORA-00942 a-b", "ORA-00942 a-b", "a password that is a dash");
    check("svc", "svc_pw12", "svc", "svc_pw12 svc", "**** ****", "a password containing a name");

    // empty and missing values
    check(nullptr, nullptr, nullptr, "nothing to mask", "nothing to mask", "no values");
    check("", "", "", "nothing to mask", "nothing to mask", "empty values");
    check("scott", "pw", "orcl", "", "", "empty text");

    // a mask is not matched again
    check("*", "pw", "orcl", "a * b", "a **** b", "a value that is part of the mask");

    // non-ASCII letters are matched exactly
    check("\xc3\xa9t\xc3\xa9", "pw", "orcl", "\xc3\xa9t\xc3\xa9 \xc3\x89T\xc3\x89", "**** \xc3\x89T\xc3\x89",
        "non-ASCII letters");

    printf("%d checks, %d failed\n", checks, failures);
    if (!failures) {
        printf("all redaction checks passed\n");
    }
    return failures ? 1 : 0;
}
