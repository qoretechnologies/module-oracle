/* -*- mode: c++; indent-tabs-mode: nil -*- */
/*
    QoreOracleStatement.cpp

    Qore Programming Language

    Copyright (C) 2003 - 2026 Qore Technologies, s.r.o.

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

#include "oracle.h"
#include "ocilib/ocilib_internal.h"

int QoreOracleStatement::setupDateDescriptor(OCIDateTime*& odt, ExceptionSink* xsink) {
    if (conn.descriptorAlloc((dvoid**)&odt, QORE_DTYPE_TIMESTAMP, "QoreOracleStatement::setupDateDecriptor()", xsink))
        return -1;
    return 0;
}

int QoreOracleStatement::setPrefetch(ExceptionSink* xsink, int rows) {
    unsigned prefetch = rows < 0 ? PREFETCH_BULK : (rows ? rows : PREFETCH_DEFAULT);
    if (prefetch > PREFETCH_MAX)
        prefetch = PREFETCH_MAX;
    if (prefetch == prefetch_rows)
        return 0;

    // set the new prefetch row count
    int rc = conn.checkerr(OCIAttrSet(stmthp, OCI_HTYPE_STMT, &prefetch, 0, OCI_ATTR_PREFETCH_ROWS, conn.errhp),
        "QoreOracleStatement::setPrefetch()", xsink);
    if (!rc) {
        prefetch_rows = prefetch;
        //printd(5, "prefetch set to %d\n", prefetch_rows);
        return 0;
    }

    return -1;
}

int QoreOracleSimpleStatement::exec(const char* sql, unsigned len, ExceptionSink* xsink) {
    //printd(5, "QoreOracleSimpleStatement::exec: '%s' (%d)\n", sql, len);
    if (!stmthp && allocate(xsink)) {
        return -1;
    }

    if (prepare(sql, len, xsink)) {
        return -1;
    }

    // Check for interrupt before query execution
    if (qore_check_cancel(xsink)) {
        return -1;
    }

    int status;
    {
        // Register cancel callback for interruptible execution
        QoreOracleCancelHelper cancel_helper(conn.svchp, conn.errhp);
        status = OCIStmtExecute(conn.svchp, stmthp, conn.errhp, 1, 0, 0, 0, OCI_DEFAULT);
    }
    return conn.checkerr(status, "QoreOracleSimpleStatement::exec", xsink);
}

QoreHashNode* QoreOracleStatement::fetchRow(OraResultSet& resultset, ExceptionSink* xsink) {
    if (!fetch_done) {
        xsink->raiseException("ORACLE-FETCH-ROW-ERROR", "call SQLStatement::next() before calling "
            "SQLStatement::fetchRow()");
        return nullptr;
    }

    // set up hash for row
    ReferenceHolder<QoreHashNode> h(new QoreHashNode(autoTypeInfo), xsink);

    // copy data or perform per-value processing if needed
    for (clist_t::iterator i = resultset.clist.begin(), e = resultset.clist.end(); i != e; ++i) {
        OraColumnBuffer *w = *i;
        // assign value to hash
        QoreValue n = w->getValue(true, xsink);
        if (*xsink) {
            assert(!n);
            return nullptr;
        }
        HashAssignmentHelper hah(**h, w->name.c_str());
        // if we have a duplicate column
        if (!hah.get().isNothing()) {
            // find a unique column name
            unsigned num = 1;
            while (true) {
                QoreStringMaker tmp("%s_%d", w->name.c_str(), num);
                hah.reassign(tmp.c_str());
                if (!hah.get().isNothing()) {
                    ++num;
                    continue;
                }
                break;
            }
        }

        hah.assign(n, xsink);
        if (*xsink) {
            return nullptr;
        }
    }

    return h.release();
}

QoreListNode* QoreOracleStatement::fetchRows(ExceptionSink* xsink) {
    OraResultSetHelper resultset(*this, "QoreOracleStatement::fetchRows():params", xsink);
    if (*xsink) {
        return nullptr;
    }

    return fetchRows(**resultset, -1, xsink);
}

QoreListNode* QoreOracleStatement::fetchRows(OraResultSet& resultset, int rows, ExceptionSink* xsink) {
    if (fetch_warned) {
        xsink->raiseException("ORACLE-SELECT-ROWS-ERROR", "SQLStatement::fetchRows() called after the end of data "
            "already received");
        return nullptr;
    }

    ReferenceHolder<QoreListNode> l(new QoreListNode(autoTypeInfo), xsink);

    if (fetch_complete) {
        fetch_warned = true;
        return l.release();
    }

    if (setPrefetch(xsink, rows)) {
        return nullptr;
    }

    // setup temporary row to accept values
    if (resultset.define("QoreOracleStatement::fetchRows():define", xsink)) {
        return nullptr;
    }

    // now finally fetch the data
    int row_count = 0;
    while (next(xsink)) {
        // Check for interrupt periodically during fetch (every 100 rows)
        if ((row_count % 100) == 0 && qore_check_cancel(xsink)) {
            return nullptr;
        }
        ++row_count;

        QoreHashNode* h = fetchRow(resultset, xsink);
        if (!h) {
            return nullptr;
        }

        // add row to list
        l->push(h, xsink);

        if (rows > 0 && l->size() == static_cast<size_t>(rows)) {
            break;
        }
    }
    //printd(2, "QoreOracleStatement::fetchRows(): %d column(s), %d row(s) retrieved as output\n", resultset.size(),
    //  l->size());
    if (!*xsink) {
        if (!fetch_done) {
            fetch_done = true;
        }
        if ((int)l->size() < rows) {
            fetch_complete = true;
        }
        return l.release();
    }
    return nullptr;
}

QoreHashNode* QoreOracleStatement::fetchSingleRow(ExceptionSink* xsink) {
    OraResultSetHelper resultset(*this, "QoreOracleStatement::fetchRow():params", xsink);
    if (*xsink)
        return nullptr;

    if (setPrefetch(xsink))
        return nullptr;

    ReferenceHolder<QoreHashNode> rv(xsink);

    // setup temporary row to accept values
    if (resultset->define("QoreOracleStatement::fetchRows():define", xsink))
        return nullptr;

    //printd(2, "QoreOracleStatement::fetchRow(): %d column(s) retrieved as output\n", resultset->size());

    // now finally fetch the data
    if (!next(xsink))
        return nullptr;

    rv = fetchRow(**resultset, xsink);
    if (!rv)
        return nullptr;

    if (!fetch_done)
        fetch_done = true;

    if (next(xsink)) {
        xsink->raiseExceptionArg("DBI-SELECT-ROW-ERROR", rv.release(), "SQL passed to selectRow() returned more than "
            "1 row");
        return nullptr;
    }

    return rv.release();
}

void QoreOracleStatement::doColumns(OraResultSet& resultset, QoreHashNode& h) {
    // create hash elements for each column, assign empty list
    for (clist_t::iterator i = resultset.clist.begin(), e = resultset.clist.end(); i != e; ++i) {
        //printd(5, "QoreOracleStatement::fetchColumns() allocating list for '%s' column\n", w->name);
        h.setKeyValue((*i)->name.c_str(), new QoreListNode(autoTypeInfo), 0);
    }
}

// retrieve results from statement and return hash
QoreHashNode* QoreOracleStatement::fetchColumns(bool cols, ExceptionSink* xsink) {
    OraResultSetHelper resultset(*this, "QoreOracleStatement::fetchColumns():params", xsink);
    if (*xsink)
        return nullptr;

    return fetchColumns(**resultset, -1, cols, xsink);
}

// retrieve results from statement and return hash
QoreHashNode* QoreOracleStatement::fetchColumns(OraResultSet& resultset, int rows, bool cols, ExceptionSink* xsink) {
    if (fetch_warned) {
        xsink->raiseException("ORACLE-SELECT-COLUMNS-ERROR", "SQLStatement::fetchColumns() called after the end of "
            "data already received");
        return nullptr;
    }

    // allocate result hash for result value
    ReferenceHolder<QoreHashNode> h(new QoreHashNode(autoTypeInfo), xsink);

    if (fetch_complete) {
        assert(!cols);
        fetch_warned = true;
        return h.release();
    }

    if (setPrefetch(xsink, rows))
        return nullptr;

    // setup temporary row to accept values
    if (resultset.define("QoreOracleStatement::fetchColumns():define", xsink))
        return nullptr;

    int num_rows = 0;

    // tracks the column size for when columns are duplicated
    unsigned csize = 0;

    if (cols)
        doColumns(resultset, **h);

    // now finally fetch the data
    while (next(xsink)) {
        if (h->empty())
            doColumns(resultset, **h);

        // copy data or perform per-value processing if needed
        for (unsigned i = 0; i < resultset.clist.size(); ++i) {
            OraColumnBuffer* w = resultset.clist[i];
            // get pointer to value of target node
            QoreListNode* l = h->getKeyValue(w->name, xsink).get<QoreListNode>();
            if (!l)
                break;

            if (!i)
                csize = l->size();
            else {
                // see if we have a duplicated column
                if (l->size() > csize) {
                    // find a unique column name
                    unsigned num = 1;
                    QoreListNode* al;
                    while (true) {
                        QoreStringMaker tmp("%s_%d", w->name.c_str(), num);
                        al = h->getKeyValue(tmp.c_str(), xsink).get<QoreListNode>();
                        if (!al) {
                            al = new QoreListNode(autoTypeInfo);
                            h->setKeyValue(tmp.c_str(), al, xsink);
                            break;
                        }
                        else if (al->size() == csize)
                            break;
                        ++num;
                    }
                    l = al;
                }
            }

            QoreValue n = w->getValue(false, xsink);
            if (*xsink) {
                assert(!n);
                break;
            }

            l->push(n, xsink);
            if (*xsink)
                break;
        }

        ++num_rows;
        if (rows > 0 && num_rows == rows)
            break;
    }
    //printd(2, "QoreOracleStatement::fetchColumns(rows: %d): %d column(s), %d row(s) retrieved as output\n", rows,
    //  resultset.size(), num_rows);
    if (!*xsink) {
        if (!fetch_done)
            fetch_done = true;
        if (num_rows < rows)
            fetch_complete = true;
        return h.release();
    }
    return nullptr;
}

QoreHashNode* QoreOracleStatement::describe(OraResultSet& resultset, ExceptionSink* xsink) {
    // set up hash for row
    ReferenceHolder<QoreHashNode> h(new QoreHashNode(autoTypeInfo), xsink);
    QoreString namestr("name");
    QoreString maxsizestr("maxsize");
    QoreString typestr("type");
    QoreString dbtypestr("native_type");
    QoreString internalstr("internal_id");

    int charSize = ds->getQoreEncoding() == QCS_UTF8 ? 4 : 1;

    // copy data or perform per-value processing if needed
    for (clist_t::iterator i = resultset.clist.begin(), e = resultset.clist.end(); i != e; ++i) {
        OraColumnBuffer *w = *i;
        ReferenceHolder<QoreHashNode> col(new QoreHashNode(autoTypeInfo), xsink);
        col->setKeyValue(namestr, new QoreStringNode(w->name), xsink);
        col->setKeyValue(internalstr, w->dtype, xsink);
        switch (w->dtype) {
        case SQLT_CHR:
            col->setKeyValue(typestr, NT_STRING, xsink);
            col->setKeyValue(dbtypestr, new QoreStringNode("VARCHAR2"), xsink);
            col->setKeyValue(maxsizestr, w->maxsize/charSize, xsink);
            break;
        case SQLT_NUM:
            col->setKeyValue(typestr, NT_NUMBER, xsink);
            col->setKeyValue(dbtypestr, new QoreStringNode("NUMBER"), xsink);
            col->setKeyValue(maxsizestr, w->maxsize, xsink);
            break;
        case SQLT_INT:
            col->setKeyValue(typestr, NT_INT, xsink);
            col->setKeyValue(dbtypestr, new QoreStringNode("INTEGER"), xsink);
            col->setKeyValue(maxsizestr, w->maxsize, xsink);
            break;
        case SQLT_FLT:
            col->setKeyValue(typestr, NT_FLOAT, xsink);
            col->setKeyValue(dbtypestr, new QoreStringNode("FLOAT"), xsink);
            col->setKeyValue(maxsizestr, w->maxsize, xsink);
            break;
        case SQLT_AFC:
        case SQLT_AVC:
            col->setKeyValue(typestr, NT_STRING, xsink);
            col->setKeyValue(dbtypestr, new QoreStringNode("CHAR"), xsink);
            col->setKeyValue(maxsizestr, w->maxsize/charSize, xsink);
            break;
        case SQLT_CLOB:
            col->setKeyValue(typestr, NT_STRING, xsink);
            col->setKeyValue(dbtypestr, new QoreStringNode("CLOB"), xsink);
            col->setKeyValue(maxsizestr, w->maxsize/charSize, xsink);
            break;
        case SQLT_BLOB:
            col->setKeyValue(typestr, NT_BINARY, xsink);
            col->setKeyValue(dbtypestr, new QoreStringNode("BLOB"), xsink);
            col->setKeyValue(maxsizestr, w->maxsize/charSize, xsink);
            break;
        case SQLT_NTY:
            col->setKeyValue(typestr, NT_HASH, xsink);
            col->setKeyValue(dbtypestr, new QoreStringNode("NAMED DATATYPE"), xsink);
            col->setKeyValue(maxsizestr, w->maxsize, xsink);
            break;
        case SQLT_DATE:
        case SQLT_DAT:
            col->setKeyValue(typestr, NT_DATE, xsink);
            col->setKeyValue(dbtypestr, new QoreStringNode("DATE"), xsink);
            col->setKeyValue(maxsizestr, w->maxsize, xsink);
            break;
        case SQLT_TIMESTAMP:
            col->setKeyValue(typestr, NT_DATE, xsink);
            col->setKeyValue(dbtypestr, new QoreStringNode("TIMESTAMP"), xsink);
            col->setKeyValue(maxsizestr, w->maxsize, xsink);
            break;
        case SQLT_TIMESTAMP_TZ:
            col->setKeyValue(typestr, NT_DATE, xsink);
            col->setKeyValue(dbtypestr, new QoreStringNode("TIMESTAMP WITH ZONE"), xsink);
            col->setKeyValue(maxsizestr, w->maxsize, xsink);
            break;
        case SQLT_TIMESTAMP_LTZ:
            col->setKeyValue(typestr, NT_DATE, xsink);
            col->setKeyValue(dbtypestr, new QoreStringNode("TIMESTAMP WITH LOCAL TIME ZONE"), xsink);
            col->setKeyValue(maxsizestr, w->maxsize, xsink);
            break;
        case SQLT_INTERVAL_YM:
            col->setKeyValue(typestr, NT_DATE, xsink);
            col->setKeyValue(dbtypestr, new QoreStringNode("INTERVAL YEAR TO MONTH"), xsink);
            col->setKeyValue(maxsizestr, w->maxsize, xsink);
            break;
        case SQLT_INTERVAL_DS:
            col->setKeyValue(typestr, NT_DATE, xsink);
            col->setKeyValue(dbtypestr, new QoreStringNode("INTERVAL DAY TO SECOND"), xsink);
            col->setKeyValue(maxsizestr, w->maxsize, xsink);
            break;
        case SQLT_RDD:
            col->setKeyValue(typestr, NT_STRING, xsink);
            col->setKeyValue(dbtypestr, new QoreStringNode("ROWID"), xsink);
            col->setKeyValue(maxsizestr, w->maxsize, xsink);
            break;
        default:
            col->setKeyValue(typestr, -1, xsink);
            col->setKeyValue(dbtypestr, new QoreStringNode("n/a"), xsink);
            col->setKeyValue(maxsizestr, w->maxsize, xsink);
            break;
        } // switch

        h->setKeyValue(w->name, col.release(), xsink);
        if (*xsink)
            return nullptr;
    }

    return h.release();
}
