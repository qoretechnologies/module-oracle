/* -*- mode: c++; indent-tabs-mode: nil -*- */
/*
    OraclePeerCheck.h

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

#ifndef _QORE_ORACLE_PEER_CHECK_H
#define _QORE_ORACLE_PEER_CHECK_H

/** @file OraclePeerCheck.h checks the server that the Oracle client actually connected to before the login

    A listener can answer the client's connect request with a REDIRECT to another address (a RAC SCAN listener
    redirects to the local listener of a node, for example), which the client follows inside OCIServerAttach(),
    without a way to refuse it.  So for a connection chosen by code under a sandbox that restricts the network, the
    address that the client is connected to after OCIServerAttach() is checked against the network policy of each
    such sandbox, before OCISessionBegin() sends the credentials; a connection to an address that the policy does not
    allow, or whose address cannot be determined, is detached and refused with \c NETWORK-ACCESS-DENIED.

    The address is that of the TCP sockets that the attach opened: the sockets of the process before and after it
    are compared (Linux: \c /proc/self/fd), and the peer of each new TCP socket is checked (\c getpeername()); a
    socket that another thread opened at the same time is checked as well, which can only refuse a connection, never
    allow one.  On other platforms a restricted connection is refused before it is attached.

    The residual risk: before the check, the client has opened a TCP connection to each redirect target (a chain of
    redirects reaches each host of the chain) and sent it the connect request (the connect descriptor), but no
    credentials; a redirect to a TCPS address makes the TLS handshake there before the check, with the client's
    wallet certificate if one is configured; and only the direct peer is checked, so a redirect to an allowed relay
    (Connection Manager, \c SOURCE_ROUTE) that forwards elsewhere is not detected.
*/

#if defined(QDBI_METHOD_GET_CONNECT_TARGETS) \
    && !(defined(QORE_HAS_DBI_CHECK_CONNECT_PATH) && defined(_QORE_HAS_DATASOURCE_CONNECT_CHOOSERS))
// the driver reports its destinations, so the connections that a sandbox restricts must also be checked after a
// redirect of the listener; never build the one without the other
#error "this Qore has QDBI_METHOD_GET_CONNECT_TARGETS but not the APIs that the check of the connected address needs"
#endif

#if defined(QORE_HAS_DBI_CHECK_CONNECT_PATH) && defined(_QORE_HAS_DATASOURCE_CONNECT_CHOOSERS)
#define QORE_ORACLE_PEER_CHECK 1

#include <dirent.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <utility>
#include <vector>

class OraclePeerCheck {
public:
    //! called before the attach: returns 0 if the attach may go ahead, -1 if an exception was raised
    DLLLOCAL int start(Datasource& ds, ExceptionSink* xsink) {
        active = qore_dbi_connect_restricted(&ds);
        if (!active) {
            return 0;
        }
#ifdef __linux__
        sockets(before);
        return 0;
#else
        xsink->raiseException("NETWORK-ACCESS-DENIED", "the Oracle connection cannot be checked against the sandbox "
            "on this platform: the address that the client connects to (which a listener can redirect) cannot be "
            "determined");
        return -1;
#endif
    }

    //! called after the attach and before the login: returns 0 if every peer is allowed, -1 if an exception was
    //! raised (the caller detaches the server before anything else)
    DLLLOCAL int check(Datasource& ds, ExceptionSink* xsink) {
        if (!active) {
            return 0;
        }
#ifdef __linux__
        std::set<std::pair<unsigned long, int>> after;
        sockets(after);
        std::set<unsigned long> old_inodes;
        for (const auto& s : before) {
            old_inodes.insert(s.first);
        }
        QoreDatasourceConnectChoosers choosers;
        if (ds.getConnectChoosers(choosers, xsink)) {
            return -1;
        }
        int checked = 0;
        for (const auto& s : after) {
            if (old_inodes.count(s.first)) {
                continue;
            }
            int fd = s.second;
            int type = 0;
            socklen_t tl = sizeof type;
            if (getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &tl) || type != SOCK_STREAM) {
                continue;
            }
            struct sockaddr_storage addr;
            socklen_t len = sizeof addr;
            if (getpeername(fd, reinterpret_cast<struct sockaddr*>(&addr), &len)
                    || (addr.ss_family != AF_INET && addr.ss_family != AF_INET6)) {
                continue;
            }
            ++checked;
            // the policy of every sandbox that governed code that chose the connection must allow the address: every
            // entry with a policy manager, as the Qore library checks the destinations before the connection (an entry
            // that allows uncontrolled APIs can still restrict the network); an entry without one restricts nothing
            for (size_t i = 0, e = choosers.size(); i < e; ++i) {
                QoreSandboxManager* mgr = choosers.getPolicyManagerRef(i);
                if (!mgr) {
                    continue;
                }
                bool ok = mgr->checkNetworkAccess(reinterpret_cast<struct sockaddr*>(&addr), len, QSEC_NET_TCP,
                    xsink);
                mgr->deref(xsink);
                if (!ok) {
                    if (*xsink) {
                        xsink->appendLastDescription(" (the Oracle client was connected there, possibly by a "
                            "redirect of the listener; the connection is refused before the login)");
                    }
                    return -1;
                }
            }
        }
        if (!checked) {
            xsink->raiseException("NETWORK-ACCESS-DENIED", "the address that the Oracle client connected to cannot be "
                "determined, so the connection cannot be checked against the sandbox");
            return -1;
        }
        return 0;
#else
        return 0;
#endif
    }

private:
    //! true if the connection is restricted by a sandbox
    bool active = false;
    //! the socket inodes and their descriptors before the attach
    std::set<std::pair<unsigned long, int>> before;

#ifdef __linux__
    //! the sockets of the process: (inode, descriptor)
    DLLLOCAL static void sockets(std::set<std::pair<unsigned long, int>>& rv) {
        DIR* d = opendir("/proc/self/fd");
        if (!d) {
            return;
        }
        int dfd = dirfd(d);
        while (struct dirent* e = readdir(d)) {
            char* end;
            long fd = strtol(e->d_name, &end, 10);
            if (*end || fd == dfd) {
                continue;
            }
            char link[64];
            char path[64];
            snprintf(path, sizeof path, "/proc/self/fd/%ld", fd);
            ssize_t n = readlink(path, link, sizeof link - 1);
            if (n <= 0) {
                continue;
            }
            link[n] = '\0';
            unsigned long ino;
            if (sscanf(link, "socket:[%lu]", &ino) == 1) {
                rv.insert(std::make_pair(ino, (int)fd));
            }
        }
        closedir(d);
    }
#endif
};

#endif
#endif
