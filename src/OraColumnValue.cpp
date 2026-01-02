/* -*- mode: c++; indent-tabs-mode: nil -*- */
/*
    OraColumnValue.cpp

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

#include "oracle.h"

static DateTimeNode* convert_date_time(unsigned char *str) {
    int year;
    if ((str[0] < 100) || (str[1] < 100))
        year = 9999;
    else
        year = (str[0] - 100) * 100 + (str[1] - 100);

    //printd(5, "convert_date_time(): %d %d = %04d-%02d-%02d %02d:%02d:%02d\n", str[0], str[1], year, str[2], str[3], str[4] - 1, str[5] - 1, str[6] - 1);
    return new DateTimeNode(year, str[2], str[3], str[4] - 1, str[5] - 1, str[6] - 1);
}

void OraColumnValue::del(ExceptionSink *xsink) {
    //printd(5, "OraColumnValue::del() this: %p dtype: %d buf.ptr: %p array: %d\n", this, dtype, buf.ptr, array);

    if (array) {
        delete buf.arraybind;
        array = false;
        return;
    }

    switch (dtype) {
        case 0:
        case SQLT_INT:
        case SQLT_UIN:
        case SQLT_FLT:
#ifdef SQLT_BFLOAT
        case SQLT_BFLOAT:
#endif
#ifdef SQLT_BDOUBLE
        case SQLT_BDOUBLE:
#endif
#ifdef SQLT_IBFLOAT
        case SQLT_IBFLOAT:
#endif
#ifdef SQLT_IBDOUBLE
        case SQLT_IBDOUBLE:
#endif
        case SQLT_DAT:
            break;

        case SQLT_CLOB:
        case SQLT_BLOB:
            if (buf.ptr)
                OCIDescriptorFree(buf.ptr, OCI_DTYPE_LOB);
            break;

        case SQLT_RDD:
            if (buf.ptr)
                OCIDescriptorFree(buf.ptr, OCI_DTYPE_ROWID);
            break;

        case SQLT_RSET:
            if (buf.ptr)
                OCIHandleFree(buf.ptr, OCI_HTYPE_STMT);
            break;

        case SQLT_TIMESTAMP:
        case SQLT_TIMESTAMP_TZ:
        case SQLT_TIMESTAMP_LTZ:
        case SQLT_DATE:
            if (buf.odt) {
                //printd(5, "OraColumnValue::del() this=%p freeing TIMESTAMP descriptor %p\n", this, buf.odt);
                OCIDescriptorFree(buf.odt, QORE_DTYPE_TIMESTAMP);
            }
            break;

        case SQLT_INTERVAL_YM:
            if (buf.oi)
                OCIDescriptorFree(buf.oi, OCI_DTYPE_INTERVAL_YM);
            break;

        case SQLT_INTERVAL_DS:
            if (buf.oi)
                OCIDescriptorFree(buf.oi, OCI_DTYPE_INTERVAL_DS);
            break;

        case SQLT_BIN:
        case SQLT_LBI:
        case SQLT_LVB: {
            QoreOracleConnection *conn = stmt.getData();
            //printd(5, "freeing binary pointer for SQLT_LVB %p\n", buf.ptr);
            conn->rawFree((OCIRaw**)&buf.ptr, xsink);
            break;
        }

        case SQLT_LNG:
            if (buf.lng)
                delete buf.lng;
            break;

        case SQLT_NTY:
            freeObject(xsink);
            break;

        default:
            if (buf.ptr) {
                //printd(5, "freeing pointer with free(%p)\n", buf.ptr);
                free(buf.ptr);
            }
            break;
    }
}

#define ORA_ROWID_LEN 25

QoreValue OraColumnValue::getValue(ExceptionSink *xsink, bool horizontal, bool destructive) {
    assert(!array);
    // SQL NULL returned
    if (ind == -1)
        return null();

    switch (dtype) {
        case SQLT_INT:
        case SQLT_UIN:
            //printd(5, "OraColumnValue::getValue() buf.i8: %lld\n", buf.i8);
            return buf.i8;

        case SQLT_FLT:
#ifdef SQLT_BFLOAT
        case SQLT_BFLOAT:
#endif
#ifdef SQLT_BDOUBLE
        case SQLT_BDOUBLE:
#endif
#ifdef SQLT_IBFLOAT
        case SQLT_IBFLOAT:
#endif
#ifdef SQLT_IBDOUBLE
        case SQLT_IBDOUBLE:
#endif
            return buf.f8;

        case SQLT_DAT:
            return convert_date_time(buf.date);

        case SQLT_TIMESTAMP:
        case SQLT_DATE:
            return stmt.getData()->getTimestamp(false, buf.odt, xsink);

        case SQLT_TIMESTAMP_TZ:
        case SQLT_TIMESTAMP_LTZ:
            return stmt.getData()->getTimestamp(true, buf.odt, xsink);

        case SQLT_INTERVAL_YM:
            return stmt.getData()->getIntervalYearMonth(buf.oi, xsink);

        case SQLT_INTERVAL_DS:
            return stmt.getData()->getIntervalDaySecond(buf.oi, xsink);

        case SQLT_BIN:
        case SQLT_LBI:
        case SQLT_LVB:
            //printd(5, "OraColumnValue::getValue() this=%p SQLT_LVB ptr=%p\n", this, buf.ptr);
            return stmt.getData()->getBinary((OCIRaw*)buf.ptr);

        case SQLT_CLOB:
            return stmt.getData()->readClob((OCILobLocator*)buf.ptr, stmt.getEncoding(), xsink);

        case SQLT_BLOB:
            return stmt.getData()->readBlob((OCILobLocator*)buf.ptr, xsink);

        case SQLT_LNG:
            return buf.lng->takeValue();

        case SQLT_RSET: {
            if (!alt_output) {
                QoreOracleStatement tstmt(stmt.getDatasource(), (OCIStmt*)buf.takePtr());
                if (horizontal)
                    return tstmt.fetchRows(xsink);
                return tstmt.fetchColumns(false, xsink);
            }
            else {
                // return an SQLStatement object
                return stmt.getDatasource()->getSQLStatementObjectForResultSet(new QorePreparedStatement(stmt.getDatasource(), (OCIStmt*)buf.takePtr()));
            }
        }

        case SQLT_NTY: {
            // Real (atomic) null handling is quite tricky here.
            // pp_ind and pp_struct are unknown C structs taken from
            // the OCI NTY object where we know only the pp_ind's first
            // member "_atomic". It holds info about NULL value of the
            // whole object. The rest content of those structs is ignored.
            // (It took ages to get those info from the docs :/)
            OCIInd *pp_ind = 0; // obtain NULL info
            void *pp_struct = 0; // used only for call. No usage for its value
            QoreOracleConnection *conn = stmt.getData();

            if (subdtype == SQLT_NTY_OBJECT) {
                OCI_ObjectGetStruct2(&conn->ocilib, buf.oraObj, (void**)&pp_struct, (void**)&pp_ind);
                //printd(5, "OraColumnValue::getValue() allocated OBJECT: %p\n", buf.oraObj);
                if (*pp_ind == OCI_IND_NULL || *pp_ind == OCI_IND_BADNULL)
                    return null();
                return objToQore(conn, buf.oraObj, xsink);
            }
            else {
                assert(subdtype == SQLT_NTY_COLLECTION);
                OCI_CollGetStruct(&conn->ocilib, buf.oraColl, (void**)&pp_struct, (void**)&pp_ind, xsink);
                if (*xsink)
                    return QoreValue();
                //printd(5, "OraColumnValue::getValue() allocated COLLECTION: %p\n", buf.oraColl);
                if (*pp_ind == OCI_IND_NULL || *pp_ind == OCI_IND_BADNULL)
                    return null();
                return collToQore(conn, buf.oraColl, xsink);
            }
            break;
        } // SQLT_NTY

#ifdef SQLT_RDD
        case SQLT_RDD: {
            SimpleRefHolder<QoreStringNode> str(new QoreStringNode(stmt.getEncoding()));
            str->reserve(ORA_ROWID_LEN);

            QoreOracleConnection *conn = stmt.getData();

            ub2 len = ORA_ROWID_LEN;
            if (conn->checkerr(OCIRowidToChar((OCIRowid *)buf.ptr, (OraText *)str->getBuffer(), &len, conn->errhp), "OraColumnValue::getValue() ROWID", xsink))
                return QoreValue();

            str->terminate(len);
            return str.release();
        }
#endif

        case SQLT_VNU:
        case SQLT_NUM: {
            int nopt = stmt.getData()->getNumberOption();
            switch (nopt) {
                case OPT_NUM_OPTIMAL:
                    return stmt.getData()->getNumberOptimal((const char*)buf.ptr);
                case OPT_NUM_STRING:
                    return doReturnString(destructive);
            }
            assert(nopt == OPT_NUM_NUMERIC);
            return new QoreNumberNode((const char*)buf.ptr);
        }
    }

    // default: treat as string
    //printd(5, "OraColumnValue::getValue() type=%d\n", dtype);
    // must be string data
    // SQLT_AFC = ANSI fixed char - we should not trim it to get eg:
    //   "foo       " for CHAR(10)
    // which is correct value. SQLT_AVC is for varchar and it's provided for
    // CHAR in PL/SQL too - it should be trimmed. See docs:
    //   subsection string_sizes CHAR and VARCHAR2 to Qore String
    if (/*dtype == SQLT_AFC ||*/ dtype == SQLT_AVC)
        remove_trailing_blanks((char *)buf.ptr);
    return doReturnString(destructive);
}

QoreStringNode* OraColumnValue::doReturnString(bool destructive) {
    assert(!array);
    if (!destructive)
        return new QoreStringNode((const char*)buf.ptr, stmt.getEncoding());
    int len = strlen((char*)buf.ptr);
    QoreStringNode* str = new QoreStringNode((char*)buf.ptr, len, len + 1, stmt.getEncoding());
    //printd(5, "OraColumnValue::doReturnString() this: %p buf.ptr: %p\n", this, buf.ptr);
    buf.ptr = nullptr;
    return str;
}

void OraColumnValue::freeObject(ExceptionSink* xsink) {
    assert(!array);
    assert(dtype == SQLT_NTY);
    assert(subdtype == SQLT_NTY_OBJECT || subdtype == SQLT_NTY_COLLECTION);

    QoreOracleConnection *conn = stmt.getData();

    // printd(5, "deleting object (OraColumn) buf.oraObj: %p, buf.oraColl: %p\n", buf.oraObj, buf.oraColl);
    // objects are allocated in bind-methods and it has to be freed in any case
    if (subdtype == SQLT_NTY_OBJECT) {
        //printd(5, "OraColumnValue::getValue() freed OBJECT: %p\n", buf.oraObj);
        if (buf.oraObj) {
            OCI_ObjectFree2(&conn->ocilib, buf.oraObj, xsink);
            buf.oraObj = nullptr;
        }
    } else {
        //printd(5, "OraColumnValue::freeObject() freed COLLECTION: %p\n", buf.oraColl);
        if (buf.oraColl) {
            OCI_CollFree2(&conn->ocilib, buf.oraColl, xsink);
            buf.oraColl = nullptr;
        }
    }
}
