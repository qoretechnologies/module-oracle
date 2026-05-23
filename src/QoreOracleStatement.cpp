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

#if defined(QDBI_METHOD_SELECT_COLUMNAR) || defined(QDBI_METHOD_STMT_FETCH_COLUMNAR)
#include <qore/QoreBufferNode.h>
#include <qore/QoreColumnarResult.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <unordered_map>
#endif

#if defined(QDBI_METHOD_SELECT_COLUMNAR) || defined(QDBI_METHOD_STMT_FETCH_COLUMNAR)
namespace {
struct OracleColumnarStorage {
    std::vector<int64> int_values;
    std::vector<double> float_values;
    std::vector<uint8_t> validity;
};

static size_t oracle_columnar_bitmap_size(size_t size) {
    return (size + 7) / 8;
}

static void oracle_columnar_set_validity_bit(std::vector<uint8_t>& validity, size_t index, bool valid) {
    size_t byte = index / 8;
    if (byte >= validity.size()) {
        validity.resize(byte + 1, 0);
    }

    uint8_t mask = uint8_t(1) << (index % 8);
    if (valid) {
        validity[byte] |= mask;
    } else {
        validity[byte] &= ~mask;
    }
}

static bool oracle_columnar_is_valid(const std::vector<uint8_t>& validity, size_t index) {
    if (validity.empty()) {
        return true;
    }
    size_t byte = index / 8;
    return byte < validity.size() && (validity[byte] & (uint8_t(1) << (index % 8)));
}

static bool oracle_columnar_parse_int64(const char* str, int64& value) {
    if (!str || !*str || strchr(str, '.') || strchr(str, 'e') || strchr(str, 'E')) {
        return false;
    }

    errno = 0;
    char* end = nullptr;
    long long rv = strtoll(str, &end, 10);
    if (errno == ERANGE || !end || *end) {
        return false;
    }

    value = static_cast<int64>(rv);
    return true;
}

enum class OracleColumnarKind {
    Int64,
    Float64,
    NumberOptimal,
    List,
};

class OracleColumnarBuilder {
public:
    OracleColumnarBuilder(OraColumnBuffer* n_column, std::string n_name, int number_option, ExceptionSink* xsink)
        : column(n_column), name(std::move(n_name)), list(xsink) {
        switch (column->dtype) {
            case SQLT_INT:
            case SQLT_UIN:
                kind = OracleColumnarKind::Int64;
                storage.reset(new OracleColumnarStorage);
                break;

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
                kind = OracleColumnarKind::Float64;
                storage.reset(new OracleColumnarStorage);
                break;

            case SQLT_NUM:
                if (number_option == OPT_NUM_OPTIMAL) {
                    kind = OracleColumnarKind::NumberOptimal;
                    storage.reset(new OracleColumnarStorage);
                    break;
                }
                // fall through

            default:
                kind = OracleColumnarKind::List;
                list = new QoreListNode(autoTypeInfo);
                break;
        }
    }

    const char* getName() const {
        return name.c_str();
    }

    int append(ExceptionSink* xsink) {
        if (kind == OracleColumnarKind::List) {
            return appendList(xsink);
        }

        if (column->ind == -1) {
            appendNull();
            return 0;
        }

        switch (kind) {
            case OracleColumnarKind::Int64:
                storage->int_values.push_back(column->buf.i8);
                appendValid();
                return 0;

            case OracleColumnarKind::Float64:
                storage->float_values.push_back(column->buf.f8);
                appendValid();
                return 0;

            case OracleColumnarKind::NumberOptimal: {
                int64 value;
                if (oracle_columnar_parse_int64((const char*)column->buf.ptr, value)) {
                    storage->int_values.push_back(value);
                    appendValid();
                    return 0;
                }

                if (fallbackToList(xsink)) {
                    return -1;
                }
                return appendList(xsink);
            }

            case OracleColumnarKind::List:
                break;
        }

        assert(false);
        return -1;
    }

    QoreValue finish(ExceptionSink* xsink) {
        if (kind == OracleColumnarKind::List) {
            return list.release();
        }

        assert(storage);
        QoreBufferElementType element_type = kind == OracleColumnarKind::Float64
            ? QoreBufferElementType::Float64
            : QoreBufferElementType::Int64;
        bool nullable = null_count > 0;
        const void* data = element_type == QoreBufferElementType::Float64
            ? static_cast<const void*>(storage->float_values.empty() ? nullptr : storage->float_values.data())
            : static_cast<const void*>(storage->int_values.empty() ? nullptr : storage->int_values.data());
        const uint8_t* validity = nullable && !storage->validity.empty() ? storage->validity.data() : nullptr;
        return QoreBufferNode::wrapExternalStorage(element_type, nullable, row_count, data, validity, storage,
            null_count, xsink);
    }

private:
    void ensureValidity() {
        if (!storage->validity.empty()) {
            storage->validity.resize(oracle_columnar_bitmap_size(row_count + 1), 0);
            return;
        }

        storage->validity.resize(oracle_columnar_bitmap_size(row_count + 1), 0xff);
    }

    void appendNull() {
        ensureValidity();
        oracle_columnar_set_validity_bit(storage->validity, row_count, false);
        switch (kind) {
            case OracleColumnarKind::Float64:
                storage->float_values.push_back(0.0);
                break;
            default:
                storage->int_values.push_back(0);
                break;
        }
        ++null_count;
        ++row_count;
    }

    void appendValid() {
        if (!storage->validity.empty()) {
            oracle_columnar_set_validity_bit(storage->validity, row_count, true);
        }
        ++row_count;
    }

    int fallbackToList(ExceptionSink* xsink) {
        assert(kind == OracleColumnarKind::NumberOptimal);
        assert(storage);

        list = new QoreListNode(autoTypeInfo);
        for (size_t i = 0; i < row_count; ++i) {
            if (i && !(i % 100) && qore_check_cancel(xsink)) {
                return -1;
            }

            if (oracle_columnar_is_valid(storage->validity, i)) {
                list->push(storage->int_values[i], xsink);
            } else {
                list->push(null(), xsink);
            }
            if (*xsink) {
                return -1;
            }
        }

        kind = OracleColumnarKind::List;
        storage.reset();
        null_count = 0;
        return 0;
    }

    int appendList(ExceptionSink* xsink) {
        ValueHolder value(column->getValue(false, xsink), xsink);
        if (*xsink) {
            return -1;
        }

        list->push(value.release(), xsink);
        if (*xsink) {
            return -1;
        }
        ++row_count;
        return 0;
    }

    OraColumnBuffer* column;
    std::string name;
    OracleColumnarKind kind = OracleColumnarKind::List;
    std::shared_ptr<OracleColumnarStorage> storage;
    ReferenceHolder<QoreListNode> list;
    size_t row_count = 0;
    int64_t null_count = 0;
};

static std::string oracle_columnar_unique_name(const QoreString& name, std::unordered_map<std::string, unsigned>& seen) {
    std::string key(name.c_str());
    unsigned index = seen[key]++;
    if (!index) {
        return key;
    }

    QoreStringMaker tmp("%s_%u", key.c_str(), index);
    return tmp.c_str();
}
}
#endif

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

#if defined(QDBI_METHOD_SELECT_COLUMNAR) || defined(QDBI_METHOD_STMT_FETCH_COLUMNAR)
QoreColumnarResult* QoreOracleStatement::fetchColumnar(OraResultSet& resultset, int rows, bool cols,
        ExceptionSink* xsink) {
    if (fetch_warned) {
        xsink->raiseException("ORACLE-SELECT-COLUMNAR-ERROR", "SQLStatement::fetchColumnar() called after the end of "
            "data already received");
        return nullptr;
    }

    if (fetch_complete) {
        assert(!cols);
        fetch_warned = true;
        return new QoreColumnarResult;
    }

    if (setPrefetch(xsink, rows)) {
        return nullptr;
    }

    if (resultset.define("QoreOracleStatement::fetchColumnar():define", xsink)) {
        return nullptr;
    }

    std::unordered_map<std::string, unsigned> seen;
    std::vector<std::unique_ptr<OracleColumnarBuilder>> builders;
    builders.reserve(resultset.clist.size());
    size_t column_index = 0;
    for (clist_t::iterator i = resultset.clist.begin(), e = resultset.clist.end(); i != e; ++i) {
        if (column_index && !(column_index % 100) && qore_check_cancel(xsink)) {
            return nullptr;
        }
        builders.emplace_back(new OracleColumnarBuilder(*i, oracle_columnar_unique_name((*i)->name, seen),
            getData()->getNumberOption(), xsink));
        ++column_index;
    }

    int num_rows = 0;
    while (next(xsink)) {
        if ((num_rows % 100) == 0 && qore_check_cancel(xsink)) {
            return nullptr;
        }

        column_index = 0;
        for (std::vector<std::unique_ptr<OracleColumnarBuilder>>::iterator i = builders.begin(), e = builders.end();
                i != e; ++i) {
            if (column_index && !(column_index % 100) && qore_check_cancel(xsink)) {
                return nullptr;
            }
            if ((*i)->append(xsink)) {
                return nullptr;
            }
            ++column_index;
        }

        ++num_rows;
        if (rows > 0 && num_rows == rows) {
            break;
        }
    }

    if (*xsink) {
        return nullptr;
    }

    if (!fetch_done) {
        fetch_done = true;
    }
    if (num_rows < rows) {
        fetch_complete = true;
    }

    ReferenceHolder<QoreHashNode> desc(describe(resultset, xsink), xsink);
    if (*xsink) {
        return nullptr;
    }

    ReferenceHolder<QoreHashNode> columns(new QoreHashNode(autoTypeInfo), xsink);
    column_index = 0;
    for (std::vector<std::unique_ptr<OracleColumnarBuilder>>::iterator i = builders.begin(), e = builders.end();
            i != e; ++i) {
        if (column_index && !(column_index % 100) && qore_check_cancel(xsink)) {
            return nullptr;
        }
        ValueHolder value((*i)->finish(xsink), xsink);
        if (*xsink) {
            return nullptr;
        }

        columns->setKeyValue((*i)->getName(), value.release(), xsink);
        if (*xsink) {
            return nullptr;
        }
        ++column_index;
    }

    return QoreColumnarResult::fromColumnHash(*columns, *desc, xsink);
}
#endif

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
        case SQLT_UIN:
            col->setKeyValue(typestr, NT_INT, xsink);
            col->setKeyValue(dbtypestr, new QoreStringNode("INTEGER"), xsink);
            col->setKeyValue(maxsizestr, w->maxsize, xsink);
            break;
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
