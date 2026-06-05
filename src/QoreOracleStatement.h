/* -*- mode: c++; indent-tabs-mode: nil -*- */
/*
  QoreOracleStatement.h

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

#ifndef _QORE_QOREORACLESTATEMENT_H

#define _QORE_QOREORACLESTATEMENT_H

#include "config.h"
#include "oracle-config.h"
#include <qore/Qore.h>

#include <oci.h>

class OraResultSet;
#if defined(QDBI_METHOD_SELECT_COLUMNAR) || defined(QDBI_METHOD_STMT_FETCH_COLUMNAR)
class QoreColumnarResult;
#endif

// default prefetch row count
#define PREFETCH_DEFAULT 1
// default prefetch row count for fetching all rows (fetchRows(-1) or fetchColumns(-1))
#define PREFETCH_BULK 1000
// maximum prefetch row count
#define PREFETCH_MAX 32767

struct QoreOracleSimpleStatement {
    QoreOracleConnection& conn;
    OCIStmt* stmthp;

    DLLLOCAL QoreOracleSimpleStatement(QoreOracleConnection& n_conn, OCIStmt* n_stmthp = nullptr) : conn(n_conn),
        stmthp(n_stmthp) {
    }

    DLLLOCAL ~QoreOracleSimpleStatement() {
        if (stmthp) {
            del();
        }
    }

    DLLLOCAL void del() {
        assert(stmthp);
        // free OCI handle
        OCIHandleFree(stmthp, OCI_HTYPE_STMT);
    }

    DLLLOCAL int allocate(ExceptionSink* xsink) {
        assert(!stmthp);

        if (conn.handleAlloc((dvoid**)&stmthp, OCI_HTYPE_STMT, "QoreOracleSimpleStatement::allocate()", xsink)) {
            stmthp = nullptr;
            return -1;
        }

        return 0;
    }

    DLLLOCAL operator bool() const {
        return stmthp;
    }

    DLLLOCAL int prepare(const char* sql, unsigned len, ExceptionSink* xsink) {
        return conn.checkerr(OCIStmtPrepare(stmthp, conn.errhp, (text*)sql, len, OCI_NTV_SYNTAX, OCI_DEFAULT),
            "QoreOracleSimpleStatement::prepare()", xsink);
    }

    DLLLOCAL int exec(const char* sql, unsigned len, ExceptionSink* xsink);
};

class QoreOracleStatement : public QoreOracleSimpleStatement {
protected:
    Datasource* ds;
    // for array binds
    unsigned array_size = 0;
    // current select prefetch row count
    unsigned prefetch_rows = 1;
    bool is_select = false,
        fetch_done = false,
        fetch_complete = false,
        fetch_warned = false;

    DLLLOCAL int setPrefetch(ExceptionSink* xsink, int rows = PREFETCH_DEFAULT);

public:
    DLLLOCAL QoreOracleStatement(Datasource* ds, OCIStmt* stmthp = nullptr)
            : QoreOracleSimpleStatement(ds->getPrivateDataRef<QoreOracleConnection>(), stmthp), ds(ds) {
    }

    DLLLOCAL ~QoreOracleStatement() {
    }

    DLLLOCAL void reset(ExceptionSink* xsink) {
        // free OCI handle
        if (stmthp) {
            del();
            stmthp = nullptr;
        }

        is_select = false;
        fetch_done = false;
    }

    // returns 0=OK, -1=ERROR
    DLLLOCAL int paramGet(OCIParam*& parmp, unsigned pos) {
        return OCIParamGet(stmthp, OCI_HTYPE_STMT, conn.errhp, (void**)&parmp, pos) == OCI_SUCCESS ? 0 : -1;
    }

    DLLLOCAL int attrGet(OCIParam* parmp, void* attributep, unsigned &sizep, unsigned attrtype,
            ExceptionSink* xsink) {
        return conn.checkerr(OCIAttrGet(parmp, OCI_DTYPE_PARAM, attributep, &sizep, attrtype, conn.errhp),
            "QoreOracleStatement::attrGet():param1", xsink);
    }

    DLLLOCAL int attrGet(OCIParam* parmp, void* attributep, unsigned attrtype, ExceptionSink* xsink) {
        return conn.checkerr(OCIAttrGet(parmp, OCI_DTYPE_PARAM, attributep, 0, attrtype, conn.errhp),
            "QoreOracleStatement::attrGet():param2", xsink);
    }

    DLLLOCAL int attrGet(void* attributep, unsigned &sizep, unsigned attrtype, ExceptionSink* xsink) {
        return conn.checkerr(OCIAttrGet(stmthp, OCI_HTYPE_STMT, attributep, &sizep, attrtype, conn.errhp),
            "QoreOracleStatement::attrGet():stmt1", xsink);
    }

    DLLLOCAL int attrGet(void* attributep, unsigned attrtype, ExceptionSink* xsink) {
        return conn.checkerr(OCIAttrGet(stmthp, OCI_HTYPE_STMT, attributep, 0, attrtype, conn.errhp),
            "QoreOracleStatement::attrGet():stmt2", xsink);
    }

    // returns 0=OK, -1=ERROR (exception), 1=no data
    DLLLOCAL int fetch(ExceptionSink* xsink, unsigned rows = 1) {
        int status;

        if ((status = OCIStmtFetch2(stmthp, conn.errhp, rows, OCI_FETCH_NEXT, 0, OCI_DEFAULT))) {
            if (status == OCI_NO_DATA) {
                //printd(5, "QoreOracleStatement::fetch() this=%p no more data\n", this);
                return 1;
            } else if (conn.checkerr(status, "QoreOracleStatement::fetch()", xsink)) {
                //printd(5, "QoreOracleStatement::fetch() this=%p exception\n", this);
                return -1;
            }
        }
        if (!fetch_done) {
            fetch_done = true;
        }
        return 0;
    }

    DLLLOCAL int defineDynamic(OCIDefine* defp, void* ctx, OCICallbackDefine cb, ExceptionSink* xsink) {
        return conn.checkerr(OCIDefineDynamic(defp, conn.errhp, ctx, cb),
            "QoreOracleStatement::defineDynamic()", xsink);
    }

    DLLLOCAL int defineByPos(OCIDefine*& defp, unsigned pos, void* valuep, int value_sz, unsigned short dty,
            void* indp, ExceptionSink* xsink, ub4 mode = OCI_DEFAULT) {
        return conn.checkerr(OCIDefineByPos(stmthp, &defp, conn.errhp, pos, valuep, value_sz, dty, indp, 0, 0, mode),
            "QoreOracleStatement::defineByPos()", xsink);
    }

    DLLLOCAL int bindByPos(OCIBind*& bndp, unsigned pos, void* valuep, int value_sz, unsigned short dty,
            ExceptionSink* xsink, void* indp, ub4 mode = OCI_DEFAULT) {
        return conn.checkerr(OCIBindByPos(stmthp, &bndp, conn.errhp, pos, valuep, value_sz, dty, indp, 0, 0, 0, 0,
            mode), "QoreOracleStatement::bindByPos()", xsink);
    }

    DLLLOCAL int prepare(QoreString& str, ExceptionSink* xsink) {
        int rc = QoreOracleSimpleStatement::prepare(str.c_str(), str.strlen(), xsink);
        if (!rc) {
            // see what kind of statement was prepared and set the flag accordingly
            ub2 stype;
            if (attrGet(&stype, OCI_ATTR_STMT_TYPE, xsink)) {
                return -1;
            }

            if (stype == OCI_STMT_SELECT) {
                is_select = true;
            }
        }

        return rc;
    }

    DLLLOCAL bool next(ExceptionSink* xsink) {
        return fetch(xsink) ? false : true;
    }

    DLLLOCAL int setArraySize(int pos, unsigned as, ExceptionSink* xsink) {
        if (array_size > 0) {
            if (array_size != as) {
                xsink->raiseException("BIND-ERROR", "query position %d: cannot bind a list with %d element%s "
                    "when a list of %d element%s has already been bound in the same statement; lists must be of "
                    "exactly the same size", pos, as, as == 1 ? "" : "s", array_size, array_size == 1 ? "" : "s");
                return -1;
            }
        } else {
            array_size = as;
        }
        return 0;
    }

    DLLLOCAL unsigned getArraySize() const {
        return array_size;
    }

    DLLLOCAL bool isArray() const {
        return (bool)array_size;
    }

    DLLLOCAL QoreHashNode* fetchRow(OraResultSet& columns, ExceptionSink* xsink);
    DLLLOCAL QoreHashNode* fetchSingleRow(ExceptionSink* xsink);
    DLLLOCAL QoreListNode* fetchRows(OraResultSet& columns, int rows, ExceptionSink* xsink);
    DLLLOCAL QoreListNode* fetchRows(ExceptionSink* xsink);

    DLLLOCAL void doColumns(OraResultSet& columns, QoreHashNode& h);

    DLLLOCAL QoreHashNode* fetchColumns(OraResultSet& columns, int rows, bool cols, ExceptionSink* xsink);
    DLLLOCAL QoreHashNode* fetchColumns(bool cols, ExceptionSink* xsink);
#if defined(QDBI_METHOD_SELECT_COLUMNAR) || defined(QDBI_METHOD_STMT_FETCH_COLUMNAR)
    DLLLOCAL QoreColumnarResult* fetchColumnar(OraResultSet& columns, int rows, bool cols, ExceptionSink* xsink);
#endif

    DLLLOCAL QoreHashNode* describe(OraResultSet& columns, ExceptionSink* xsink);

    DLLLOCAL Datasource* getDatasource() const {
        return ds;
    }

    DLLLOCAL const QoreEncoding* getEncoding() const {
        return ds->getQoreEncoding();
    }

    DLLLOCAL QoreOracleConnection* getData() const {
        return &conn;
    }

    DLLLOCAL int setupDateDescriptor(OCIDateTime*& odt, ExceptionSink* xsink);
};

#endif
