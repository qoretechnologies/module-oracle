/* -*- mode: c++; indent-tabs-mode: nil -*- */
/*
    OraResultSet.h

    Qore Programming Language

    Copyright (C) 2003 - 2022 Qore Technologies, s.r.o.

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef _ORARESULTSET_H

#define _ORARESULTSET_H

class OraColumnBuffer : public OraColumnValue {
public:
    QoreString name;
    int maxsize;
    OCIDefine *defp;     // define handle
    ub2 charlen;
    sb2 precision;
    sb1 scale;
    QoreString subdtypename;

    DLLLOCAL OraColumnBuffer(QoreOracleStatement &stmt, const char *n, int len, int ms, ub2 dt, ub2 n_charlen,
            sb2 n_precision = 0, sb1 n_scale = 0, int subdt = SQLT_NTY_NONE, QoreString subdttn = "")
        : OraColumnValue(stmt, dt, subdt), name(n, len, stmt.getEncoding()), maxsize(ms), defp(0),
            charlen(n_charlen), precision(n_precision), scale(n_scale), subdtypename(subdttn) {
        name.tolwr();
    }

    DLLLOCAL ~OraColumnBuffer() {
        assert(!defp);
    }

    DLLLOCAL void del(ExceptionSink *xsink) {
        // printf("DLLLOCAL void del(Datasource *ds, ExceptionSink *xsink)\n");
        if (defp) {
            OraColumnValue::del(xsink);
            OCIHandleFree(defp, OCI_HTYPE_DEFINE);
            defp = nullptr;
        }
    }

    DLLLOCAL QoreValue getValue(bool horizontal, ExceptionSink *xsink) {
        return OraColumnValue::getValue(xsink, horizontal, false);
    }
};

typedef std::vector<OraColumnBuffer *> clist_t;

class OraResultSet {
protected:
    QoreOracleStatement &stmt;
    bool defined;

public:
    clist_t clist;

    DLLLOCAL OraResultSet(QoreOracleStatement &n_stmt, const char *str, ExceptionSink *xsink);

    DLLLOCAL ~OraResultSet() {
        assert(clist.empty());
    }

    DLLLOCAL void del(ExceptionSink *xsink) {
        for (clist_t::iterator i = clist.begin(), e = clist.end(); i != e; ++i) {
            (*i)->del(xsink);
            delete (*i);
        }
        clist.clear();
        defined = false;
    }

    DLLLOCAL void add(const char *name, int nlen, int maxsize, ub2 dtype, ub2 char_len, sb2 precision = 0,
            sb1 scale = 0, int subtype=SQLT_NTY_NONE, QoreString subdtn = "") {
        OraColumnBuffer *c = new OraColumnBuffer(stmt, name, nlen, maxsize, dtype, char_len, precision, scale,
            subtype, subdtn);

        clist.push_back(c);
        // printd(5, "column: '%s'\n", c->name);
        //printd(0, "OraResultSet::add() %2d name='%s' (max %d) type=%d\n", size(), c->name, c->maxsize, dtype);
    }

    DLLLOCAL unsigned size() const {
        return clist.size();
    }

    DLLLOCAL int define(const char *str, ExceptionSink *xsink);
};

class OraResultSetHelper {
protected:
    OraResultSet *col;
    ExceptionSink *xsink;

public:
    DLLLOCAL OraResultSetHelper(QoreOracleStatement &stmt, const char *str, ExceptionSink *n_xsink) : col(new OraResultSet(stmt, str, n_xsink)), xsink(n_xsink) {
    }

    DLLLOCAL ~OraResultSetHelper() {
        col->del(xsink);
        delete col;
    }

    DLLLOCAL OraResultSet *operator*() { return col; }
    DLLLOCAL OraResultSet *operator->() { return col; }
};

#endif
