/* -*- mode: c++; indent-tabs-mode: nil -*- */
/*
    OracleRedact.h

    Qore Programming Language

    Copyright (C) 2026 Qore Technologies, s.r.o.

    Permission is hereby granted, free of charge, to any person obtaining a
    copy of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom the
    Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
    all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
*/

#ifndef _QORE_ORACLE_REDACT_H
#define _QORE_ORACLE_REDACT_H

//! masks the connection values of a datasource (user name, password, database) in Oracle client and server messages
/** Messages of the Oracle client and server can name the connection's values: a database or service name (for
    example "ORA-12514: ... Service xxx is not registered ...", "ORA-12154: Cannot find alias xxx ..."), or the user
    name as the schema of an object (for example "ORA-00942: table or view "XXX"."T" does not exist").  Every
    occurrence of a value is replaced with MASK; the rest of the message, including its ORA- code, is kept.

    How the values are matched:
    - every value is matched only where it is not a part of a longer name: a value that starts or ends with a
      letter, digit, \c _, \c $ or \c # is not matched where the text next to it is one of those characters too, so
      that a short value does not mask parts of other words;
    - the password is matched exactly (Oracle passwords are case-sensitive), and only if it is unlikely to be text
      of a message template (see password_maskable()): Oracle messages are fixed, public templates, so masking a
      password that is also template text (for example \c 1521, \c Service or \c connect) would show the password to
      anyone who compares the message with its template.  The Oracle client and server do not show the password in
      their messages; it is masked where it can still occur, for example in SQL text that a message repeats;
    - the user name and the database values are matched without regard to the case of ASCII letters (the server
      shows unquoted names in upper case);
    - the database values are the database string as given, and the service, SID and instance names in it (of an
      Easy Connect string or a connect descriptor) or in the definition of the TNS alias that it names (given by the
      caller); the parts of a proxy user name (\c "user[client]") and the name in a quoted user name are matched as
      well;
    - message codes (for example \c ORA-00942: two to five letters, \c - and five digits) are kept whole, so that no
      value masks a part of them, unless a value occurs in it across its \c - or past its end (for example the
      password \c Abc-12345 or the service name \c ERP-20240, which are masked).

    Limits: a value that is a common word also masks that word where it stands alone in the message (for example a
    user named \c table masks \c TABLE); letters other than ASCII letters are matched exactly; and a value is only
    masked where it occurs as given (for example, not where a server shows only a part of it).
*/

#include <algorithm>
#include <cassert>
#include <string>
#include <vector>

namespace qore_oracle_redact {

//! the text that replaces a connection value in a message
static constexpr const char* MASK = "****";

//! a value to mask
struct Secret {
    std::string value;
    //! true: matched exactly; false: matched without regard to ASCII case; both are matched as whole names only
    bool exact;
};

typedef std::vector<Secret> secret_list_t;

//! returns true if \a c can be part of an Oracle name
static inline bool is_name_char(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '$'
        || c == '#';
}

static inline char ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

//! adds a value to mask; empty values are ignored
static inline void add_secret(secret_list_t& secrets, const std::string& value, bool exact) {
    if (value.empty()) {
        return;
    }
    for (const Secret& s : secrets) {
        if (s.exact == exact && s.value == value) {
            return;
        }
    }
    secrets.push_back({value, exact});
}

//! the shortest password that is masked; see password_maskable()
static constexpr size_t PASSWORD_MIN_MASK_LEN = 8;

static inline size_t message_code_len(const std::string& text, size_t pos);

//! returns true if the password can be masked without showing it, i.e. if it is unlikely
//! to be text of a message template
/** Oracle messages are fixed, public templates: words and identifiers made of letters and punctuation (for example
    \c Service, \c connect, \c error-help, \c docs.oracle.com), numbers and addresses made of digits and punctuation
    (for example \c 1521, \c 127.0.0.1), and message codes (for example \c ORA-12514); a mask where template text was
    shows that the password is that text.  Template text is made of runs of letters only or digits only, separated
    by spaces and punctuation (\c ORA-00942: \c table, \c port \c 1521, \c db/ora-12514), also where it spans several
    words.  A password is therefore masked only if it is unlikely to be template text, i.e. if it:
    - has at least PASSWORD_MIN_MASK_LEN characters;
    - has a run of letters and digits (characters between spaces and punctuation, including \c _, \c - and \c .)
      that has both letters and digits;
    - is not shaped like a message code (a defensive check: a code has no run with both letters and digits).

    It is matched as a whole name, so a password that is a part of a longer word of a message is not masked either.

    Limits:
    - a password that meets Oracle's password complexity rules is not masked if no single run has both letters and
      digits (for example \c Summer_2024 or \c My.Pass.99);
    - template words that mix letters and digits (for example \c VARCHAR2, character set names such as
      \c AL32UTF8, \c x86_64, or the names in internal errors such as ORA-00600) can make a password of template
      text maskable, for example <tt>VARCHAR2 column</tt>; such a password is not a credible secret.
*/
static inline bool password_maskable(const std::string& pass) {
    if (pass.size() < PASSWORD_MIN_MASK_LEN || message_code_len(pass, 0) == pass.size()) {
        return false;
    }
    bool letter = false, digit = false;
    for (char c : pass) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            letter = true;
        } else if (c >= '0' && c <= '9') {
            digit = true;
        } else {
            // the end of a run
            letter = digit = false;
            continue;
        }
        if (letter && digit) {
            return true;
        }
    }
    return false;
}

//! adds the values of every occurrence of the parameter \a name (ex: SERVICE_NAME) in a connect descriptor
static inline void add_descriptor_values(secret_list_t& secrets, const std::string& db, const char* name) {
    std::string udb;
    udb.reserve(db.size());
    for (char c : db) {
        udb += (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    std::string key = name;
    size_t pos = 0;
    while ((pos = udb.find(key, pos)) != std::string::npos) {
        // the parameter name must stand alone: "(SERVICE_NAME" with optional whitespace before it
        size_t b = pos;
        while (b > 0 && (udb[b - 1] == ' ' || udb[b - 1] == '\t' || udb[b - 1] == '\n' || udb[b - 1] == '\r')) {
            --b;
        }
        size_t e = pos + key.size();
        if (!b || udb[b - 1] != '(' || (e < udb.size() && is_name_char(udb[e]))) {
            pos = e;
            continue;
        }
        while (e < udb.size() && (udb[e] == ' ' || udb[e] == '\t' || udb[e] == '\n' || udb[e] == '\r')) {
            ++e;
        }
        if (e >= udb.size() || udb[e] != '=') {
            pos = e;
            continue;
        }
        ++e;
        while (e < db.size() && (db[e] == ' ' || db[e] == '\t' || db[e] == '\n' || db[e] == '\r')) {
            ++e;
        }
        size_t end = db.find(')', e);
        std::string v = db.substr(e, end == std::string::npos ? std::string::npos : end - e);
        while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\n' || v.back() == '\r')) {
            v.pop_back();
        }
        if (v.size() > 1 && v.front() == '"' && v.back() == '"') {
            v = v.substr(1, v.size() - 2);
        }
        add_secret(secrets, v, false);
        pos = e;
    }
}

//! adds the service and instance names of an Easy Connect string:
//! [[protocol:]//]host[:port][/service[:server][/instance]][?parameters]
static inline void add_ezconnect_values(secret_list_t& secrets, const std::string& db) {
    std::string s = db;
    size_t q = s.find('?');
    if (q != std::string::npos) {
        s.erase(q);
    }
    // a protocol: "tcp://" or "tcps://"
    size_t p = s.find("://");
    if (p != std::string::npos && p > 0) {
        bool letters = true;
        for (size_t i = 0; i < p; ++i) {
            if (!((s[i] >= 'A' && s[i] <= 'Z') || (s[i] >= 'a' && s[i] <= 'z'))) {
                letters = false;
                break;
            }
        }
        if (letters) {
            s.erase(0, p + 1);
        }
    }
    if (!s.compare(0, 2, "//")) {
        s.erase(0, 2);
    }
    // the host can be an IPv6 address in brackets, which contains ':'
    size_t start = 0;
    if (!s.empty() && s[0] == '[') {
        size_t rb = s.find(']');
        start = rb == std::string::npos ? s.size() : rb + 1;
    }
    size_t slash = s.find('/', start);
    if (slash == std::string::npos) {
        return;
    }
    std::string rest = s.substr(slash + 1);
    size_t slash2 = rest.find('/');
    std::string service = rest.substr(0, slash2);
    if (slash2 != std::string::npos) {
        add_secret(secrets, rest.substr(slash2 + 1), false);
    }
    size_t colon = service.find(':');
    add_secret(secrets, service.substr(0, colon), false);
}

//! returns the values to mask for a connection: its user name, password and database
/** @param user the user name, may be null
    @param pass the password, may be null
    @param db the database string (a TNS alias, a service name, an Easy Connect string or a connect descriptor), may
    be null
    @param db_names if given, other database names to mask (ex: the service name in the definition of the TNS alias
    that \a db names)
*/
static inline secret_list_t get_secrets(const char* user, const char* pass, const char* db,
        const std::vector<std::string>* db_names = nullptr) {
    secret_list_t secrets;
    // a password that may be template text is not masked, as the mask would show it
    if (pass && password_maskable(pass)) {
        add_secret(secrets, pass, true);
    }
    if (user) {
        std::string u = user;
        add_secret(secrets, u, false);
        // a quoted user name: "Name"
        if (u.size() > 2 && u.front() == '"' && u.back() == '"') {
            add_secret(secrets, u.substr(1, u.size() - 2), false);
        }
        // a proxy user: "proxy[client]"
        size_t lb = u.find('[');
        if (lb != std::string::npos) {
            add_secret(secrets, u.substr(0, lb), false);
            size_t rb = u.find(']', lb);
            add_secret(secrets, u.substr(lb + 1, rb == std::string::npos ? std::string::npos : rb - lb - 1), false);
        }
    }
    if (db) {
        std::string d = db;
        add_secret(secrets, d, false);
        size_t b = d.find_first_not_of(" \t\r\n");
        if (b != std::string::npos && d[b] == '(') {
            add_descriptor_values(secrets, d, "SERVICE_NAME");
            add_descriptor_values(secrets, d, "SID");
            add_descriptor_values(secrets, d, "INSTANCE_NAME");
        } else if (d.find('/') != std::string::npos) {
            add_ezconnect_values(secrets, d);
        }
    }
    if (db_names) {
        for (const std::string& n : *db_names) {
            add_secret(secrets, n, false);
        }
    }
    // longer values first (redact() does not depend on the order; this keeps the list stable for callers)
    std::stable_sort(secrets.begin(), secrets.end(), [](const Secret& a, const Secret& b) {
        return a.value.size() > b.value.size();
    });
    return secrets;
}

//! returns true if the text at \a pos matches \a s
static inline bool matches_at(const std::string& text, size_t pos, const Secret& s) {
    if (pos + s.value.size() > text.size()) {
        return false;
    }
    if (s.exact) {
        if (text.compare(pos, s.value.size(), s.value)) {
            return false;
        }
    } else {
        for (size_t i = 0; i < s.value.size(); ++i) {
            if (ascii_lower(text[pos + i]) != ascii_lower(s.value[i])) {
                return false;
            }
        }
    }
    // a value is matched only where it is not a part of a longer name
    if (is_name_char(s.value.front()) && pos > 0 && is_name_char(text[pos - 1])) {
        return false;
    }
    size_t end = pos + s.value.size();
    if (is_name_char(s.value.back()) && end < text.size() && is_name_char(text[end])) {
        return false;
    }
    return true;
}

//! returns the length of the message code at \a pos (ex: ORA-00942: two to five letters, '-' and five digits, as
//! a whole name), or 0 if there is none
static inline size_t message_code_len(const std::string& text, size_t pos) {
    if (pos > 0 && is_name_char(text[pos - 1])) {
        return 0;
    }
    size_t i = pos;
    while (i < text.size() && i - pos < 6
            && ((text[i] >= 'A' && text[i] <= 'Z') || (text[i] >= 'a' && text[i] <= 'z'))) {
        ++i;
    }
    if (i - pos < 2 || i - pos > 5 || i >= text.size() || text[i] != '-') {
        return 0;
    }
    ++i;
    size_t d = i;
    while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
        ++i;
    }
    if (i - d != 5 || (i < text.size() && is_name_char(text[i]))) {
        return 0;
    }
    return i - pos;
}

//! returns true if a value occurs in the message code at \a pos across its '-' or past its end, so that the text is
//! the value (ex: the service name "ERP-20240") and not only a message code
static inline bool value_overlaps_code(const std::string& text, size_t pos, size_t code_len,
        const secret_list_t& secrets) {
    size_t dash = text.find('-', pos);
    size_t end = pos + code_len;
    assert(dash != std::string::npos && dash < end);
    for (const Secret& s : secrets) {
        for (size_t start = pos; start < end; ++start) {
            size_t vend = start + s.value.size();
            if (((start <= dash && vend > dash) || vend > end) && matches_at(text, start, s)) {
                return true;
            }
        }
    }
    return false;
}

//! replaces every occurrence of the values in \a text with MASK
/** Every occurrence of every value is found first, and each run of text covered by one or more occurrences
    (including values that overlap or follow each other, ex: "foo-bar" and "bar-baz" in "foo-bar-baz") is
    replaced with one MASK, so that no part of a value remains.

    @param text the text to redact
    @param secrets the values to mask, from get_secrets()
*/
static inline void redact(std::string& text, const secret_list_t& secrets) {
    if (secrets.empty() || text.empty()) {
        return;
    }
    std::vector<bool> masked(text.size(), false);
    bool any = false;
    size_t pos = 0;
    while (pos < text.size()) {
        // a message code is kept whole, so that no value masks a part of it, unless a value occurs in it across its
        // '-' or past its end (then it is the value, ex: the password "Abc-12345" or the service "ERP-20240")
        size_t code_len = message_code_len(text, pos);
        if (code_len && !value_overlaps_code(text, pos, code_len, secrets)) {
            pos += code_len;
            continue;
        }
        for (const Secret& s : secrets) {
            if (matches_at(text, pos, s)) {
                std::fill(masked.begin() + pos, masked.begin() + pos + s.value.size(), true);
                any = true;
            }
        }
        ++pos;
    }
    if (!any) {
        return;
    }
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (!masked[i]) {
            out += text[i];
        } else if (!i || !masked[i - 1]) {
            out += MASK;
        }
    }
    text.swap(out);
}

} // namespace qore_oracle_redact

#endif // _QORE_ORACLE_REDACT_H
