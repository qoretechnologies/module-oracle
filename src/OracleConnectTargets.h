/* -*- mode: c++; indent-tabs-mode: nil -*- */
/*
    OracleConnectTargets.h

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

#ifndef _QORE_ORACLE_CONNECT_TARGETS_H
#define _QORE_ORACLE_CONNECT_TARGETS_H

/** @file OracleConnectTargets.h the network destinations of an Oracle connection, for the sandbox of the code that
    chose it (QDBI_METHOD_GET_CONNECT_TARGETS)

    The connect identifier of a datasource is one of:
    - a host and port set in the datasource, with the database name as the service: \c host:port/dbname;
    - a connect descriptor: <tt>(DESCRIPTION=...)</tt> or <tt>(DESCRIPTION_LIST=...)</tt>;
    - an Easy Connect string: <tt>[tcp://]host[:port][,host[:port]...][/service[:server]][/instance]</tt>;
    - a TNS alias, resolved here from \c tnsnames.ora as the Oracle client resolves it, only when the naming methods
      in \c sqlnet.ora (\c NAMES.DIRECTORY_PATH) use \c TNSNAMES first.

    Each is represented as a connect descriptor, whose \c ADDRESS entries, in order, are the destinations; every
    address of a failover or load-balancing list is one.  A connection that the sandbox checked connects with that
    descriptor, each \c ADDRESS replaced by one \c ADDRESS for each checked numeric address of its host, so that what
    is checked is what the client connects to (also when it reconnects).

    What cannot be determined exactly is refused with \c NETWORK-ACCESS-DENIED: an alias that is not found, or that
    another naming method (LDAP, a names server) or a default domain would resolve, or whose definitions differ
    between the files that the client reads; a protocol other than TCP (\c TCPS: the certificate check of the server
    uses the host name, which a numeric address would replace; IPC, BEQ); an \c HTTPS_PROXY; Easy Connect parameters
    (<tt>?...</tt>) or an Easy Connect string without a service.
*/

#include <dlfcn.h>
#include <sys/stat.h>

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace qore_oracle_targets {

//! the default port of an Oracle listener
static constexpr int DefaultPort = 1521;

//! a parameter of a connect descriptor: <tt>(NAME=value)</tt> or <tt>(NAME=(...)(...))</tt>
struct Param {
    std::string name;
    std::string value;
    std::vector<Param> children;
    bool has_children = false;
};

//! a destination: a host and a port
struct Target {
    std::string host;
    int port = DefaultPort;
};

static inline std::string upper(const std::string& s) {
    std::string rv(s);
    for (char& c : rv) {
        c = toupper(static_cast<unsigned char>(c));
    }
    return rv;
}

static inline std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) {
        return std::string();
    }
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

//! parses one parameter at s[i] (at its '('); returns false with err set if it is malformed
static inline bool parse_param(const std::string& s, size_t& i, Param& p, std::string& err) {
    auto skip = [&]() {
        while (i < s.size() && isspace(static_cast<unsigned char>(s[i]))) {
            ++i;
        }
    };
    skip();
    if (i >= s.size() || s[i] != '(') {
        err = "expected '(' in the connect descriptor";
        return false;
    }
    ++i;
    size_t eq = s.find('=', i);
    if (eq == std::string::npos) {
        err = "expected '=' in the connect descriptor";
        return false;
    }
    p.name = upper(trim(s.substr(i, eq - i)));
    if (p.name.empty() || p.name.find_first_of("()") != std::string::npos) {
        err = "invalid parameter name in the connect descriptor";
        return false;
    }
    i = eq + 1;
    skip();
    if (i < s.size() && s[i] == '(') {
        p.has_children = true;
        while (true) {
            skip();
            if (i >= s.size()) {
                err = "unbalanced parentheses in the connect descriptor";
                return false;
            }
            if (s[i] == ')') {
                ++i;
                return true;
            }
            Param c;
            if (!parse_param(s, i, c, err)) {
                return false;
            }
            p.children.push_back(std::move(c));
        }
    }
    // a value: up to the closing parenthesis, which a quoted part may contain
    std::string v;
    bool quoted = false;
    while (i < s.size() && (quoted || s[i] != ')')) {
        if (s[i] == '"') {
            quoted = !quoted;
        } else if (!quoted && s[i] == '(') {
            err = "unexpected '(' in a value of the connect descriptor";
            return false;
        }
        v += s[i++];
    }
    if (i >= s.size()) {
        err = "unbalanced parentheses in the connect descriptor";
        return false;
    }
    ++i;
    p.value = trim(v);
    return true;
}

//! parses a connect descriptor, which must be a single parameter
static inline bool parse_descriptor(const std::string& s, Param& root, std::string& err) {
    size_t i = 0;
    if (!parse_param(s, i, root, err)) {
        return false;
    }
    if (!trim(s.substr(i)).empty()) {
        err = "unexpected text after the connect descriptor";
        return false;
    }
    if (root.name != "DESCRIPTION" && root.name != "DESCRIPTION_LIST") {
        err = "the connect descriptor does not start with DESCRIPTION or DESCRIPTION_LIST";
        return false;
    }
    return true;
}

//! renders a parameter as a connect descriptor
static inline void render(const Param& p, std::string& out) {
    out += '(';
    out += p.name;
    out += '=';
    if (p.has_children) {
        for (const Param& c : p.children) {
            render(c, out);
        }
    } else {
        out += p.value;
    }
    out += ')';
}

static inline const Param* find_child(const Param& p, const char* name) {
    for (const Param& c : p.children) {
        if (c.name == name) {
            return &c;
        }
    }
    return nullptr;
}

//! collects the destinations of the ADDRESS parameters, in order; returns false with err set if one cannot be checked
static inline bool collect_targets(const Param& p, std::vector<Target>& targets, std::string& err) {
    if (p.name == "HTTPS_PROXY" || p.name == "HTTPS_PROXY_PORT") {
        err = "a connection through an HTTPS proxy cannot be checked against the sandbox";
        return false;
    }
    if (p.name == "ADDRESS") {
        // a proxy can be given in the address itself
        if (find_child(p, "HTTPS_PROXY") || find_child(p, "HTTPS_PROXY_PORT")) {
            err = "a connection through an HTTPS proxy cannot be checked against the sandbox";
            return false;
        }
        const Param* proto = find_child(p, "PROTOCOL");
        std::string protocol = proto ? upper(proto->value) : std::string();
        if (protocol == "TCPS") {
            err = "a TCPS connection cannot be checked against the sandbox yet: the client would connect to the "
                "checked numeric addresses, but the server's certificate is checked against the host name";
            return false;
        }
        if (protocol != "TCP") {
            err = "only TCP connections can be checked against the sandbox (the address has protocol '"
                + (proto ? proto->value : std::string("<none>")) + "')";
            return false;
        }
        const Param* host = find_child(p, "HOST");
        if (!host || host->value.empty() || host->has_children) {
            err = "an ADDRESS of the connect descriptor has no HOST";
            return false;
        }
        Target t;
        t.host = host->value;
        if (t.host.size() > 1 && t.host.front() == '"' && t.host.back() == '"') {
            t.host = t.host.substr(1, t.host.size() - 2);
        }
        if (const Param* port = find_child(p, "PORT")) {
            char* end;
            long n = strtol(port->value.c_str(), &end, 10);
            if (port->value.empty() || *end || n <= 0 || n > 65535) {
                err = "invalid PORT '" + port->value + "' in the connect descriptor";
                return false;
            }
            t.port = (int)n;
        }
        targets.push_back(t);
        return true;
    }
    for (const Param& c : p.children) {
        if (!collect_targets(c, targets, err)) {
            return false;
        }
    }
    return true;
}

//! replaces each ADDRESS (in order) with one ADDRESS per checked address; i is the index of the next ADDRESS
static inline void pin_addresses(Param& p, const std::vector<std::vector<std::string>>& addresses, size_t& i) {
    std::vector<Param> children;
    for (Param& c : p.children) {
        if (c.name == "ADDRESS") {
            for (const std::string& addr : addresses[i]) {
                Param a = c;
                for (Param& ac : a.children) {
                    if (ac.name == "HOST") {
                        // an IPv6 address is a valid HOST value as it is
                        ac.value = addr;
                    }
                }
                children.push_back(std::move(a));
            }
            ++i;
        } else {
            pin_addresses(c, addresses, i);
            children.push_back(std::move(c));
        }
    }
    p.children = std::move(children);
}

static inline Param make_value(const char* name, const std::string& value) {
    Param p;
    p.name = name;
    p.value = value;
    return p;
}

//! parses an Easy Connect string into a connect descriptor
static inline bool parse_ezconnect(const std::string& in, Param& root, std::string& err) {
    std::string s = trim(in);
    if (s.find('?') != std::string::npos) {
        err = "Easy Connect parameters ('?...') cannot be checked against the sandbox";
        return false;
    }
    std::string protocol = "TCP";
    std::string us = upper(s);
    if (us.compare(0, 6, "TCP://") == 0) {
        s = s.substr(6);
    } else if (us.compare(0, 7, "TCPS://") == 0) {
        protocol = "TCPS";
        s = s.substr(7);
    } else if (s.compare(0, 2, "//") == 0) {
        s = s.substr(2);
    }
    // the hosts, up to the first '/' that is not in an IPv6 address
    size_t slash = std::string::npos;
    for (size_t i = 0, depth = 0; i < s.size(); ++i) {
        if (s[i] == '[') {
            ++depth;
        } else if (s[i] == ']') {
            if (depth) {
                --depth;
            }
        } else if (s[i] == '/' && !depth) {
            slash = i;
            break;
        }
    }
    std::string hosts = s.substr(0, slash);
    std::string rest = slash == std::string::npos ? std::string() : s.substr(slash + 1);
    std::string service, server, instance;
    if (!rest.empty()) {
        size_t s2 = rest.find('/');
        service = rest.substr(0, s2);
        if (s2 != std::string::npos) {
            instance = rest.substr(s2 + 1);
        }
        size_t c = service.find(':');
        if (c != std::string::npos) {
            server = service.substr(c + 1);
            service = service.substr(0, c);
        }
    }
    if (service.empty()) {
        err = "an Easy Connect string without a service cannot be checked against the sandbox";
        return false;
    }
    // host[:port] entries separated by ','; a port applies to the hosts before it that have none
    Param list;
    list.name = "ADDRESS_LIST";
    list.has_children = true;
    std::vector<size_t> pending;
    size_t start = 0;
    while (start <= hosts.size()) {
        size_t comma = hosts.find(',', start);
        std::string h = trim(hosts.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        std::string host, port;
        if (!h.empty() && h[0] == '[') {
            size_t e = h.find(']');
            if (e == std::string::npos) {
                err = "invalid IPv6 address in the Easy Connect string";
                return false;
            }
            host = h.substr(1, e - 1);
            if (e + 1 < h.size()) {
                if (h[e + 1] != ':') {
                    err = "invalid host in the Easy Connect string";
                    return false;
                }
                port = h.substr(e + 2);
            }
        } else {
            size_t c = h.find(':');
            host = h.substr(0, c);
            if (c != std::string::npos) {
                port = h.substr(c + 1);
            }
        }
        if (host.empty()) {
            err = "a host of the Easy Connect string is empty";
            return false;
        }
        Param a;
        a.name = "ADDRESS";
        a.has_children = true;
        a.children.push_back(make_value("PROTOCOL", protocol));
        a.children.push_back(make_value("HOST", host));
        list.children.push_back(std::move(a));
        pending.push_back(list.children.size() - 1);
        if (!port.empty()) {
            for (size_t i : pending) {
                list.children[i].children.push_back(make_value("PORT", port));
            }
            pending.clear();
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    for (size_t i : pending) {
        list.children[i].children.push_back(make_value("PORT", std::to_string(DefaultPort)));
    }
    Param cd;
    cd.name = "CONNECT_DATA";
    cd.has_children = true;
    cd.children.push_back(make_value("SERVICE_NAME", service));
    if (!server.empty()) {
        cd.children.push_back(make_value("SERVER", server));
    }
    if (!instance.empty()) {
        cd.children.push_back(make_value("INSTANCE_NAME", instance));
    }
    root = Param();
    root.name = "DESCRIPTION";
    root.has_children = true;
    root.children.push_back(std::move(list));
    root.children.push_back(std::move(cd));
    return true;
}

//! an entry of a tnsnames.ora or sqlnet.ora file: <tt>name[, name...] = value</tt>
struct FileEntry {
    std::vector<std::string> names;
    std::string value;
};

//! parses a tnsnames.ora or sqlnet.ora file; returns false if it cannot be read
static inline bool read_ora_file(const std::string& path, std::vector<FileEntry>& entries) {
    std::ifstream f(path);
    if (!f) {
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    // without comments
    std::string s;
    {
        std::string line;
        std::istringstream in(ss.str());
        while (std::getline(in, line)) {
            size_t h = line.find('#');
            s += (h == std::string::npos ? line : line.substr(0, h));
            s += '\n';
        }
    }
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && isspace(static_cast<unsigned char>(s[i]))) {
            ++i;
        }
        if (i >= s.size()) {
            break;
        }
        size_t eq = s.find('=', i);
        if (eq == std::string::npos) {
            break;
        }
        FileEntry e;
        std::string names = s.substr(i, eq - i);
        for (size_t p = 0; p <= names.size();) {
            size_t c = names.find(',', p);
            std::string n = trim(names.substr(p, c == std::string::npos ? std::string::npos : c - p));
            if (!n.empty()) {
                e.names.push_back(upper(n));
            }
            if (c == std::string::npos) {
                break;
            }
            p = c + 1;
        }
        i = eq + 1;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
            ++i;
        }
        // a parenthesized value often starts on the next line
        if (i < s.size() && (s[i] == '\n' || s[i] == '\r')) {
            size_t j = i;
            while (j < s.size() && isspace(static_cast<unsigned char>(s[j]))) {
                ++j;
            }
            if (j < s.size() && s[j] == '(') {
                i = j;
            }
        }
        if (i < s.size() && s[i] == '(') {
            // a parenthesized value, which can span lines
            int depth = 0;
            size_t b = i;
            for (; i < s.size(); ++i) {
                if (s[i] == '(') {
                    ++depth;
                } else if (s[i] == ')' && !--depth) {
                    ++i;
                    break;
                }
            }
            e.value = s.substr(b, i - b);
        } else {
            size_t nl = s.find('\n', i);
            e.value = trim(s.substr(i, nl == std::string::npos ? std::string::npos : nl - i));
            i = nl == std::string::npos ? s.size() : nl + 1;
        }
        entries.push_back(std::move(e));
    }
    return true;
}

//! returns the descriptor text without whitespace, upper-cased, to compare two definitions
static inline std::string normalize(const std::string& s) {
    std::string rv;
    for (char c : s) {
        if (!isspace(static_cast<unsigned char>(c))) {
            rv += toupper(static_cast<unsigned char>(c));
        }
    }
    return rv;
}

static inline bool file_exists(const std::string& path) {
    struct stat st;
    return !stat(path.c_str(), &st) && S_ISREG(st.st_mode);
}

//! the directory of the loaded Oracle client library, whose network/admin directory an Instant Client reads
static inline std::string client_library_dir() {
    Dl_info info;
    if (dladdr(reinterpret_cast<void*>(&OCIEnvCreate), &info) && info.dli_fname) {
        std::string p(info.dli_fname);
        size_t s = p.rfind('/');
        if (s != std::string::npos) {
            return p.substr(0, s);
        }
    }
    return std::string();
}

//! the directories and files where the Oracle client reads tnsnames.ora and sqlnet.ora
static inline void config_files(const char* name, const char* user_name, std::vector<std::string>& files) {
    if (const char* home = getenv("HOME")) {
        std::string p = std::string(home) + "/" + user_name;
        if (file_exists(p)) {
            files.push_back(p);
        }
    }
    std::vector<std::string> dirs;
    if (const char* tns_admin = getenv("TNS_ADMIN")) {
        if (*tns_admin) {
            dirs.push_back(tns_admin);
        }
    }
    std::string ic = client_library_dir();
    if (!ic.empty()) {
        dirs.push_back(ic + "/network/admin");
    }
    if (const char* oh = getenv("ORACLE_HOME")) {
        if (*oh) {
            dirs.push_back(std::string(oh) + "/network/admin");
        }
    }
    for (const std::string& d : dirs) {
        std::string p = d + "/" + name;
        if (file_exists(p)) {
            bool dup = false;
            for (const std::string& f : files) {
                dup = dup || f == p;
            }
            if (!dup) {
                files.push_back(p);
            }
        }
    }
}

//! finds the definitions of an alias in a tnsnames.ora file and the files that it includes (IFILE)
static inline bool find_alias(const std::string& path, const std::string& alias, std::vector<std::string>& defs,
        int depth, std::string& err) {
    if (depth > 8) {
        err = "IFILE nesting is too deep in '" + path + "'";
        return false;
    }
    std::vector<FileEntry> entries;
    if (!read_ora_file(path, entries)) {
        err = "cannot read '" + path + "'";
        return false;
    }
    for (const FileEntry& e : entries) {
        if (e.names.size() == 1 && e.names[0] == "IFILE") {
            std::string inc = e.value;
            if (inc.size() > 1 && inc.front() == '"' && inc.back() == '"') {
                inc = inc.substr(1, inc.size() - 2);
            }
            if (!inc.empty() && inc[0] != '/') {
                size_t s = path.rfind('/');
                inc = path.substr(0, s + 1) + inc;
            }
            if (!find_alias(inc, alias, defs, depth + 1, err)) {
                return false;
            }
            continue;
        }
        for (const std::string& n : e.names) {
            if (n == alias) {
                defs.push_back(e.value);
            }
        }
    }
    return true;
}

//! resolves a TNS alias as the Oracle client would, or refuses it; returns false with err set if it cannot
static inline bool resolve_alias(const std::string& alias, Param& root, std::string& err) {
    // the naming methods and the default domain, from every sqlnet.ora that the client may read
    std::vector<std::string> sqlnet;
    config_files("sqlnet.ora", ".sqlnet.ora", sqlnet);
    for (const std::string& f : sqlnet) {
        std::vector<FileEntry> entries;
        if (!read_ora_file(f, entries)) {
            err = "cannot read '" + f + "'";
            return false;
        }
        for (const FileEntry& e : entries) {
            for (const std::string& n : e.names) {
                if (n == "NAMES.DIRECTORY_PATH") {
                    std::string v = upper(e.value);
                    size_t b = v.find_first_not_of("( \t");
                    if (b == std::string::npos || v.compare(b, 8, "TNSNAMES") != 0) {
                        // the messages do not name the alias, which is the datasource's database name
                        err = "the naming methods in '" + f + "' (NAMES.DIRECTORY_PATH = " + e.value + ") do not "
                            "resolve the TNS alias from tnsnames.ora first";
                        return false;
                    }
                } else if (n == "NAMES.DEFAULT_DOMAIN" && alias.find('.') == std::string::npos
                        && !trim(e.value).empty()) {
                    err = "'" + f + "' sets NAMES.DEFAULT_DOMAIN, with which the client looks up the TNS alias with "
                        "the domain '" + trim(e.value) + "' appended instead of the TNS alias";
                    return false;
                }
            }
        }
    }
    std::vector<std::string> tnsnames;
    config_files("tnsnames.ora", ".tnsnames.ora", tnsnames);
    std::vector<std::string> defs;
    std::string ualias = upper(alias);
    for (const std::string& f : tnsnames) {
        if (!find_alias(f, ualias, defs, 0, err)) {
            return false;
        }
    }
    if (defs.empty()) {
        err = "the TNS alias is not defined in any tnsnames.ora that the Oracle client reads";
        return false;
    }
    for (size_t i = 1; i < defs.size(); ++i) {
        if (normalize(defs[i]) != normalize(defs[0])) {
            err = "the TNS alias has different definitions in the tnsnames.ora files that the "
                "Oracle client reads";
            return false;
        }
    }
    if (!parse_descriptor(defs[0], root, err)) {
        err = "the TNS alias: " + err;
        return false;
    }
    return true;
}

//! returns the connect descriptor of the datasource's connect identifier; returns false with err set if it cannot
static inline bool get_descriptor(const char* hostname, int port, const char* dbname, Param& root, std::string& err) {
    std::string db = dbname ? trim(dbname) : std::string();
    if (hostname && *hostname) {
        std::string ez = std::string(hostname) + (port ? ":" + std::to_string(port) : std::string()) + "/" + db;
        return parse_ezconnect(ez, root, err);
    }
    if (db.empty()) {
        err = "the datasource has no database name";
        return false;
    }
    if (db[0] == '(') {
        return parse_descriptor(db, root, err);
    }
    if (db.find_first_of("/:[,") != std::string::npos) {
        return parse_ezconnect(db, root, err);
    }
    return resolve_alias(db, root, err);
}

#ifdef QDBI_METHOD_GET_CONNECT_TARGETS
//! returns the destinations of a connection of the datasource (QDBI_METHOD_GET_CONNECT_TARGETS)
static inline QoreListNode* get_connect_targets(Datasource* ds, ExceptionSink* xsink) {
    Param root;
    std::vector<Target> targets;
    std::string err;
    if (!get_descriptor(ds->getHostName(), ds->getPort(), ds->getDBName(), root, err)
            || !collect_targets(root, targets, err)) {
        xsink->raiseException("NETWORK-ACCESS-DENIED", "the destinations of the Oracle connection cannot be "
            "checked against the sandbox: %s", err.c_str());
        return nullptr;
    }
    if (targets.empty()) {
        xsink->raiseException("NETWORK-ACCESS-DENIED", "the Oracle connect descriptor has no ADDRESS");
        return nullptr;
    }
    ReferenceHolder<QoreListNode> rv(new QoreListNode(autoTypeInfo), xsink);
    for (const Target& t : targets) {
        ReferenceHolder<QoreHashNode> h(new QoreHashNode(autoTypeInfo), xsink);
        h->setKeyValue("host", new QoreStringNode(t.host), xsink);
        h->setKeyValue("port", t.port, xsink);
        rv->push(h.release(), xsink);
    }
    return rv.release();
}

//! sets \a out to the connect descriptor of a connection that was checked against the sandbox: each ADDRESS is
//! replaced by one ADDRESS for each checked numeric address of its host; returns 0 for success, -1 if an exception
//! was raised
static inline int checked_connect_string(const Datasource& ds, const QoreListNode* checked, std::string& out,
        ExceptionSink* xsink) {
    Param root;
    std::vector<Target> targets;
    std::string err;
    if (!get_descriptor(ds.getHostName(), ds.getPort(), ds.getDBName(), root, err)
            || !collect_targets(root, targets, err)) {
        xsink->raiseException("NETWORK-ACCESS-DENIED", "the destinations of the Oracle connection cannot be "
            "checked against the sandbox: %s", err.c_str());
        return -1;
    }
    // the destinations are determined again (a configuration file could have changed) and must be the checked ones
    if (targets.size() != checked->size()) {
        xsink->raiseException("NETWORK-ACCESS-DENIED", "the destinations of the Oracle connection changed after "
            "they were checked against the sandbox");
        return -1;
    }
    std::vector<std::vector<std::string>> addresses;
    for (size_t i = 0; i < targets.size(); ++i) {
        QoreValue v = checked->retrieveEntry(i);
        const QoreHashNode* h = v.getType() == NT_HASH ? v.get<const QoreHashNode>() : nullptr;
        QoreValue host = h ? h->getKeyValue("host") : QoreValue();
        QoreValue addrs = h ? h->getKeyValue("addresses") : QoreValue();
        if (host.getType() != NT_STRING || host.get<const QoreStringNode>()->compare(targets[i].host.c_str())
                || h->getKeyValue("port").getAsBigInt() != targets[i].port || addrs.getType() != NT_LIST) {
            xsink->raiseException("NETWORK-ACCESS-DENIED", "the destinations of the Oracle connection changed after "
                "they were checked against the sandbox");
            return -1;
        }
        std::vector<std::string> list;
        ConstListIterator li(addrs.get<const QoreListNode>());
        while (li.next()) {
            QoreValue a = li.getValue();
            if (a.getType() == NT_STRING) {
                list.push_back(a.get<const QoreStringNode>()->c_str());
            }
        }
        if (list.empty()) {
            xsink->raiseException("NETWORK-ACCESS-DENIED", "no checked address for the Oracle destination %s:%d",
                targets[i].host.c_str(), targets[i].port);
            return -1;
        }
        addresses.push_back(std::move(list));
    }
    size_t i = 0;
    pin_addresses(root, addresses, i);
    out.clear();
    render(root, out);
    return 0;
}
#endif

}  // namespace qore_oracle_targets

#endif
