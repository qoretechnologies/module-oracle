/* -*- mode: c++; indent-tabs-mode: nil -*- */
/*
    QoreOracleBulkLoad.cpp

    Qore Programming Language

    Copyright (C) 2026 Qore Technologies, s.r.o.

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

#ifdef QDBI_METHOD_BULK_LOAD_BEGIN

#include "ocilib_internal.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

constexpr ub4 QORE_ORACLE_DIRECT_PATH_ROWS = 1024;
constexpr ub4 QORE_ORACLE_DIRECT_PATH_BUFFER_SIZE = 1024 * 1024;
constexpr int64 QORE_ORACLE_STREAM_REPORT_BYTES = 65536;

enum class QoreOracleBulkColumnKind {
    Numeric,
    Text,
    Date,
    Timestamp,
};

struct QoreOracleBulkColumn {
    std::string key;
    std::string name;
    std::string format;
    QoreOracleBulkColumnKind kind;
    ub2 sqlcode;
    ub4 max_size;
    sb2 precision;
    sb1 scale;
};

struct QoreOracleBulkCell {
    std::string data;
    bool is_null = false;
};

//! Returns true only for unquoted Oracle identifiers, optionally qualified once by a schema.
/** Quoted identifiers are valid SQL, but the embedded OCILIB 3.7 table description cache uppercases
    names before describing them.  Returning dynamic-unavailability allows `bulk_load: "auto"` to
    preserve correct behavior through the ordinary insert path.
*/
static bool qoreOracleParseSimpleIdentifier(const char* value, bool qualified, ExceptionSink* xsink,
        std::string* normalized = nullptr) {
    if (!value || !*value) {
        return false;
    }
    if (normalized) {
        normalized->clear();
    }
    bool at_start = true;
    bool saw_dot = false;
    size_t count = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value); *p; ++p, ++count) {
        if (count && !(count % 100) && qore_check_cancel(xsink, "Oracle OCI direct path identifier validation")) {
            return false;
        }
        if (*p == '.') {
            if (!qualified || saw_dot || at_start || !p[1]) {
                return false;
            }
            saw_dot = true;
            at_start = true;
            if (normalized) {
                normalized->push_back('.');
            }
            continue;
        }
        if (at_start) {
            if (!(std::isalpha(*p) || *p == '_')) {
                return false;
            }
            at_start = false;
        } else if (!(std::isalnum(*p) || *p == '_' || *p == '$' || *p == '#')) {
            return false;
        }
        if (normalized) {
            normalized->push_back(static_cast<char>(std::toupper(*p)));
        }
    }
    return !at_start;
}

static int qoreOracleGetBoolOption(const QoreHashNode* options, const char* name, bool default_value,
        bool& value, ExceptionSink* xsink) {
    value = default_value;
    if (!options) {
        return 0;
    }
    QoreValue option = options->getKeyValue(name);
    if (option.isNothing()) {
        return 0;
    }
    if (option.getType() != NT_BOOLEAN) {
        xsink->raiseException("DBI:ORACLE:DIRECT-PATH-ERROR", "OCI direct path option '%s' must be boolean", name);
        return -1;
    }
    value = option.getAsBool();
    return 0;
}

static int qoreOracleGetBufferSize(const QoreHashNode* options, ub4& value, ExceptionSink* xsink) {
    value = QORE_ORACLE_DIRECT_PATH_BUFFER_SIZE;
    if (!options) {
        return 0;
    }
    QoreValue option = options->getKeyValue("oracle_direct_path_buffer_size");
    if (option.isNothing()) {
        return 0;
    }
    if (option.getType() != NT_INT) {
        xsink->raiseException("DBI:ORACLE:DIRECT-PATH-ERROR",
            "OCI direct path option 'oracle_direct_path_buffer_size' must be an integer");
        return -1;
    }
    int64 size = option.getAsBigInt();
    if (size < 65536 || size > std::numeric_limits<ub4>::max()) {
        xsink->raiseException("DBI:ORACLE:DIRECT-PATH-ERROR",
            "OCI direct path option 'oracle_direct_path_buffer_size' must be between 65536 and %u bytes",
            std::numeric_limits<ub4>::max());
        return -1;
    }
    value = static_cast<ub4>(size);
    return 0;
}

//! Reports stream boundaries for the native path; BulkSqlUtil owns them on fallback paths.
class QoreOracleBulkStream {
public:
    QoreOracleBulkStream(Datasource* ds, bool enabled)
            : ds(ds), active(enabled && ds->sqlMutationObserverActive()) {
    }

    ~QoreOracleBulkStream() {
        if (started) {
            ExceptionSink xsink;
            ds->reportMutationStreamEnd(consumed, false, &xsink);
        }
    }

    int begin(ExceptionSink* xsink) {
        if (!active) {
            return 0;
        }
        if (ds->reportMutationStreamBegin(0, xsink)) {
            return -1;
        }
        started = true;
        return 0;
    }

    int addBytes(size_t bytes, ExceptionSink* xsink) {
        if (!started) {
            return 0;
        }
        if (bytes > static_cast<size_t>(std::numeric_limits<int64>::max() - consumed)) {
            ok = false;
            xsink->raiseException("DBI:ORACLE:DIRECT-PATH-ERROR",
                "OCI direct path payload byte counter overflow");
            return -1;
        }
        consumed += static_cast<int64>(bytes);
        if (consumed - reported < QORE_ORACLE_STREAM_REPORT_BYTES) {
            return 0;
        }
        reported = consumed;
        if (ds->reportMutationStreamProgress(consumed, xsink)) {
            ok = false;
            return -1;
        }
        return 0;
    }

    void setError() {
        ok = false;
    }

    int finish(bool success, ExceptionSink* xsink) {
        if (!started) {
            return 0;
        }
        started = false;
        ok = ok && success;
        return ds->reportMutationStreamEnd(consumed, ok, xsink);
    }

private:
    Datasource* ds;
    bool active;
    bool started = false;
    bool ok = true;
    int64 consumed = 0;
    int64 reported = 0;
};

static int qoreOracleSerializeText(QoreOracleConnection& conn, QoreValue value, std::string& data,
        ExceptionSink* xsink) {
    switch (value.getType()) {
        case NT_BOOLEAN:
        case NT_INT:
        case NT_FLOAT:
        case NT_NUMBER:
        case NT_STRING:
        case NT_DATE:
            break;
        default:
            xsink->raiseException("DBI:ORACLE:DIRECT-PATH-ERROR",
                "cannot serialize Qore type '%s' as an OCI direct path text field", value.getTypeName());
            return -1;
    }
    QoreStringValueHelper str(value, conn.ds.getQoreEncoding(), xsink);
    if (*xsink) {
        return -1;
    }
    data.assign(str->c_str(), str->size());
    return 0;
}

static int qoreOracleSerializeDate(QoreOracleConnection& conn, QoreValue value, bool timestamp,
        std::string& data, ExceptionSink* xsink) {
    if (value.getType() == NT_STRING) {
        return qoreOracleSerializeText(conn, value, data, xsink);
    }
    if (value.getType() != NT_DATE) {
        xsink->raiseException("DBI:ORACLE:DIRECT-PATH-ERROR",
            "cannot serialize Qore type '%s' as an OCI direct path %s field", value.getTypeName(),
            timestamp ? "timestamp" : "date");
        return -1;
    }
    DateTimeValueHelper date(value);
    if (date->isRelative()) {
        xsink->raiseException("DBI:ORACLE:DIRECT-PATH-ERROR",
            "relative date/time values cannot be loaded through OCI direct path");
        return -1;
    }
    qore_tm info;
    date->getInfo(conn.getTZ(), info);
    if (info.year < 1 || info.year > 9999) {
        xsink->raiseException("DBI:ORACLE:DIRECT-PATH-ERROR",
            "OCI direct path date/time values must have a year from 1 through 9999; got %d", info.year);
        return -1;
    }
    char buffer[40];
    int count;
    if (timestamp) {
        count = snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d.%06d", info.year, info.month,
            info.day, info.hour, info.minute, info.second, info.us);
    } else {
        count = snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d", info.year, info.month,
            info.day, info.hour, info.minute, info.second);
    }
    assert(count > 0 && static_cast<size_t>(count) < sizeof(buffer));
    data.assign(buffer, count);
    return 0;
}

static int qoreOracleSerializeCell(QoreOracleConnection& conn, const QoreOracleBulkColumn& column,
        QoreValue value, QoreOracleBulkCell& cell, ExceptionSink* xsink) {
    if (value.isNullOrNothing()) {
        cell.is_null = true;
        cell.data.clear();
        return 0;
    }
    cell.is_null = false;
    switch (column.kind) {
        case QoreOracleBulkColumnKind::Numeric:
            switch (value.getType()) {
                case NT_BOOLEAN:
                case NT_INT:
                case NT_FLOAT:
                case NT_NUMBER:
                case NT_STRING:
                    if (qoreOracleSerializeText(conn, value, cell.data, xsink)) {
                        return -1;
                    }
                    break;
                default:
                    xsink->raiseException("DBI:ORACLE:DIRECT-PATH-ERROR",
                        "cannot serialize Qore type '%s' as an OCI direct path numeric field",
                        value.getTypeName());
                    return -1;
            }
            break;
        case QoreOracleBulkColumnKind::Text:
            if (qoreOracleSerializeText(conn, value, cell.data, xsink)) {
                return -1;
            }
            break;
        case QoreOracleBulkColumnKind::Date:
            if (qoreOracleSerializeDate(conn, value, false, cell.data, xsink)) {
                return -1;
            }
            break;
        case QoreOracleBulkColumnKind::Timestamp:
            if (qoreOracleSerializeDate(conn, value, true, cell.data, xsink)) {
                return -1;
            }
            break;
    }
    if (cell.data.size() > column.max_size) {
        xsink->raiseException("DBI:ORACLE:DIRECT-PATH-ERROR",
            "OCI direct path value for column '%s' is %zu bytes; the column accepts at most %u bytes",
            column.key.c_str(), cell.data.size(), column.max_size);
        return -1;
    }
    return 0;
}

//! Persistent state for one OCI direct path load.
class QoreOracleBulkLoadState {
public:
    QoreOracleBulkLoadState(QoreOracleConnection& conn, bool stream_bounds)
            : conn(conn), stream(&conn.ds, stream_bounds) {
    }

    ~QoreOracleBulkLoadState() {
        if (prepared && ctx) {
            // Emergency backstop only; normal paths use terminate() so errors and stream outcomes
            // are reported to the caller.
            OCIDirPathAbort(ctx, conn.errhp);
        }
        freeHandles();
    }

    int initialize(const QoreString* table, const QoreListNode* column_list, bool parallel, bool nolog,
            ub4 buffer_size, ExceptionSink* xsink) {
        if (!qoreOracleParseSimpleIdentifier(table->c_str(), true, xsink)) {
            return *xsink ? -1 : 1;
        }
        if (column_list->size() > std::numeric_limits<ub2>::max()) {
            xsink->raiseException("DBI:ORACLE:DIRECT-PATH-ERROR",
                "OCI direct path supports at most %u columns; got %zu", std::numeric_limits<ub2>::max(),
                column_list->size());
            return -1;
        }

        OCI_TypeInfo* type_info;
        {
            QoreOracleCancelHelper cancel_helper(conn.svchp, conn.errhp);
            type_info = OCI_TypeInfoGet2(&conn.ocilib, conn.ocilib_cn, table->c_str(), OCI_TIF_TABLE, xsink);
        }
        if (*xsink) {
            return -1;
        }
        if (!type_info) {
            xsink->raiseException("DBI:ORACLE:DIRECT-PATH-ERROR",
                "Oracle returned no metadata for direct path table '%s'", table->c_str());
            return -1;
        }

        std::unordered_map<std::string, ub2> metadata_columns;
        metadata_columns.reserve(type_info->nb_cols);
        for (ub2 i = 0; i < type_info->nb_cols; ++i) {
            if (i && !(i % 100) && qore_check_cancel(xsink, "Oracle OCI direct path metadata indexing")) {
                return -1;
            }
            std::string key(type_info->cols[i].name);
            std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) {
                return static_cast<char>(std::toupper(c));
            });
            // Lower- or mixed-case metadata identifies a quoted column.  It cannot be referenced
            // by the simple unquoted identifiers accepted by this native path.
            if (key == type_info->cols[i].name) {
                metadata_columns.emplace(std::move(key), i);
            }
        }

        std::set<ub2> used_columns;
        ConstListIterator li(column_list);
        while (li.next()) {
            if (columns.size() && !(columns.size() % 100)
                && qore_check_cancel(xsink, "Oracle OCI direct path column description")) {
                return -1;
            }
            QoreValue value = li.getValue();
            if (value.getType() != NT_STRING) {
                xsink->raiseException("DBI:ORACLE:DIRECT-PATH-ERROR",
                    "OCI direct path column %zu has type '%s'; expected string", columns.size() + 1,
                    value.getTypeName());
                return -1;
            }
            // note: column names are short enough to be held in inline short string storage (ex:
            // "id"), which has no QoreStringNode; the helper must stay in scope while it is used
            QoreStringDataHelper input_name(value);
            std::string normalized_name;
            if (!qoreOracleParseSimpleIdentifier(input_name.c_str(), false, xsink, &normalized_name)) {
                return *xsink ? -1 : 1;
            }

            auto metadata_iter = metadata_columns.find(normalized_name);
            if (metadata_iter == metadata_columns.end()) {
                xsink->raiseException("DBI:ORACLE:DIRECT-PATH-ERROR",
                    "column '%s' was not found in Oracle table '%s'", input_name.c_str(), table->c_str());
                return -1;
            }
            ub2 metadata_index = metadata_iter->second;
            if (!used_columns.insert(metadata_index).second) {
                xsink->raiseException("DBI:ORACLE:DIRECT-PATH-ERROR",
                    "column '%s' occurs more than once in the OCI direct path column list", input_name.c_str());
                return -1;
            }

            const OCI_Column& metadata = type_info->cols[metadata_index];
            QoreOracleBulkColumn column;
            column.key.assign(input_name.c_str(), input_name.size());
            column.name = metadata.name;
            column.precision = metadata.prec;
            column.scale = metadata.scale;
            column.sqlcode = SQLT_CHR;
            switch (metadata.type) {
                case OCI_CDT_NUMERIC:
                    column.kind = QoreOracleBulkColumnKind::Numeric;
                    column.max_size = ORACLE_NUMBER_STR_LEN;
                    break;
                case OCI_CDT_TEXT:
                    column.kind = QoreOracleBulkColumnKind::Text;
                    column.max_size = std::max<ub4>(metadata.size, 1);
                    break;
                case OCI_CDT_DATETIME:
                    column.kind = QoreOracleBulkColumnKind::Date;
                    column.max_size = 32;
                    column.format = "YYYY-MM-DD HH24:MI:SS";
                    break;
                case OCI_CDT_TIMESTAMP:
                    if (metadata.ocode == SQLT_TIMESTAMP_TZ || metadata.ocode == SQLT_TIMESTAMP_LTZ) {
                        return 1;
                    }
                    column.kind = QoreOracleBulkColumnKind::Timestamp;
                    column.max_size = 40;
                    column.format = "YYYY-MM-DD HH24:MI:SS.FF6";
                    break;
                case OCI_CDT_RAW:
                    // RAW triggers ORA-00600 in OCIDirPathPrepare() with current Oracle
                    // servers even for documented SQLT_BIN and SQLT_CHR external data.
                    // Preserve correct behavior by using the ordinary bind path.
                    return 1;
                default:
                    // LOB, LONG, interval, object, collection, REF, and cursor columns use the
                    // ordinary bind path.  Direct path restrictions vary by client/server pair.
                    return 1;
            }
            columns.push_back(std::move(column));
        }

        if (conn.handleAlloc(reinterpret_cast<void**>(&ctx), OCI_HTYPE_DIRPATH_CTX,
                "QoreOracleBulkLoadState::initialize() direct path context", xsink)) {
            return -1;
        }

        if (conn.checkerr(OCIAttrSet(ctx, OCI_HTYPE_DIRPATH_CTX, type_info->name,
                static_cast<ub4>(strlen(type_info->name)), OCI_ATTR_NAME, conn.errhp),
                "QoreOracleBulkLoadState::initialize() table name", xsink)) {
            return -1;
        }
        if (type_info->schema && *type_info->schema
            && conn.checkerr(OCIAttrSet(ctx, OCI_HTYPE_DIRPATH_CTX, type_info->schema,
                static_cast<ub4>(strlen(type_info->schema)), OCI_ATTR_SCHEMA_NAME, conn.errhp),
                "QoreOracleBulkLoadState::initialize() schema name", xsink)) {
            return -1;
        }

        ub4 requested_rows = QORE_ORACLE_DIRECT_PATH_ROWS;
        ub2 column_count = static_cast<ub2>(columns.size());
        ub1 parallel_value = parallel;
        ub1 nolog_value = nolog;
        if (conn.checkerr(OCIAttrSet(ctx, OCI_HTYPE_DIRPATH_CTX, &requested_rows, sizeof(requested_rows),
                OCI_ATTR_NUM_ROWS, conn.errhp), "QoreOracleBulkLoadState::initialize() row count", xsink)
            || conn.checkerr(OCIAttrSet(ctx, OCI_HTYPE_DIRPATH_CTX, &column_count, sizeof(column_count),
                OCI_ATTR_NUM_COLS, conn.errhp), "QoreOracleBulkLoadState::initialize() column count", xsink)
            || conn.checkerr(OCIAttrSet(ctx, OCI_HTYPE_DIRPATH_CTX, &buffer_size, sizeof(buffer_size),
                OCI_ATTR_BUF_SIZE, conn.errhp), "QoreOracleBulkLoadState::initialize() buffer size", xsink)
            || conn.checkerr(OCIAttrSet(ctx, OCI_HTYPE_DIRPATH_CTX, &parallel_value, sizeof(parallel_value),
                OCI_ATTR_DIRPATH_PARALLEL, conn.errhp), "QoreOracleBulkLoadState::initialize() parallel option",
                xsink)
            || conn.checkerr(OCIAttrSet(ctx, OCI_HTYPE_DIRPATH_CTX, &nolog_value, sizeof(nolog_value),
                OCI_ATTR_DIRPATH_NOLOG, conn.errhp), "QoreOracleBulkLoadState::initialize() nolog option", xsink)) {
            return -1;
        }

        OCIParam* list = nullptr;
        if (conn.checkerr(OCIAttrGet(ctx, OCI_HTYPE_DIRPATH_CTX, &list, nullptr, OCI_ATTR_LIST_COLUMNS,
                conn.errhp), "QoreOracleBulkLoadState::initialize() column list", xsink)) {
            return -1;
        }
        for (ub4 i = 0; i < columns.size(); ++i) {
            if (i && !(i % 100) && qore_check_cancel(xsink, "Oracle OCI direct path column setup")) {
                return -1;
            }
            OCIParam* parameter = nullptr;
            if (conn.checkerr(OCIParamGet(list, OCI_DTYPE_PARAM, conn.errhp,
                    reinterpret_cast<void**>(&parameter), i + 1),
                    "QoreOracleBulkLoadState::initialize() column parameter", xsink)) {
                return -1;
            }
            std::unique_ptr<OCIParam, void (*)(OCIParam*)> parameter_holder(parameter, [](OCIParam* value) {
                OCIDescriptorFree(value, OCI_DTYPE_PARAM);
            });
            QoreOracleBulkColumn& column = columns[i];
            if (conn.checkerr(OCIAttrSet(parameter, OCI_DTYPE_PARAM, const_cast<char*>(column.name.c_str()),
                    static_cast<ub4>(column.name.size()), OCI_ATTR_NAME, conn.errhp),
                    "QoreOracleBulkLoadState::initialize() column name", xsink)
                || conn.checkerr(OCIAttrSet(parameter, OCI_DTYPE_PARAM, &column.sqlcode, sizeof(column.sqlcode),
                    OCI_ATTR_DATA_TYPE, conn.errhp),
                    "QoreOracleBulkLoadState::initialize() column type", xsink)
                || conn.checkerr(OCIAttrSet(parameter, OCI_DTYPE_PARAM, &column.max_size, sizeof(column.max_size),
                    OCI_ATTR_DATA_SIZE, conn.errhp),
                    "QoreOracleBulkLoadState::initialize() column size", xsink)) {
                return -1;
            }
            if (column.precision
                && conn.checkerr(OCIAttrSet(parameter, OCI_DTYPE_PARAM, &column.precision,
                    sizeof(column.precision), OCI_ATTR_PRECISION, conn.errhp),
                    "QoreOracleBulkLoadState::initialize() column precision", xsink)) {
                return -1;
            }
            if (column.scale
                && conn.checkerr(OCIAttrSet(parameter, OCI_DTYPE_PARAM, &column.scale, sizeof(column.scale),
                    OCI_ATTR_SCALE, conn.errhp),
                    "QoreOracleBulkLoadState::initialize() column scale", xsink)) {
                return -1;
            }
            if (!column.format.empty()
                && conn.checkerr(OCIAttrSet(parameter, OCI_DTYPE_PARAM, const_cast<char*>(column.format.c_str()),
                    static_cast<ub4>(column.format.size()), OCI_ATTR_DATEFORMAT, conn.errhp),
                    "QoreOracleBulkLoadState::initialize() column format", xsink)) {
                return -1;
            }
        }

        if (qore_check_cancel(xsink, "Oracle OCI direct path prepare")) {
            return -1;
        }
        {
            QoreOracleCancelHelper cancel_helper(conn.svchp, conn.errhp);
            if (conn.checkerr(OCIDirPathPrepare(ctx, conn.svchp, conn.errhp),
                    "QoreOracleBulkLoadState::initialize() OCIDirPathPrepare", xsink)) {
                return -1;
            }
        }
        prepared = true;

        if (conn.checkerr(OCIHandleAlloc(ctx, reinterpret_cast<void**>(&array),
                OCI_HTYPE_DIRPATH_COLUMN_ARRAY, 0, nullptr),
                "QoreOracleBulkLoadState::initialize() column array", xsink)
            || conn.checkerr(OCIHandleAlloc(ctx, reinterpret_cast<void**>(&stream_handle),
                OCI_HTYPE_DIRPATH_STREAM, 0, nullptr),
                "QoreOracleBulkLoadState::initialize() stream", xsink)) {
            return -1;
        }
        ub4 size = sizeof(max_rows);
        if (conn.checkerr(OCIAttrGet(array, OCI_HTYPE_DIRPATH_COLUMN_ARRAY, &max_rows, &size,
                OCI_ATTR_NUM_ROWS, conn.errhp), "QoreOracleBulkLoadState::initialize() allocated rows", xsink)) {
            return -1;
        }
        if (!max_rows) {
            xsink->raiseException("DBI:ORACLE:DIRECT-PATH-ERROR",
                "Oracle allocated a zero-row OCI direct path column array");
            return -1;
        }
        return stream.begin(xsink) ? -1 : 0;
    }

    int loadRows(const QoreHashNode* rows, ExceptionSink* xsink) {
        if (rows->size() != columns.size()) {
            stream.setError();
            xsink->raiseException("DBI:ORACLE:DIRECT-PATH-ERROR",
                "OCI direct path row block has %zu columns; expected %zu", rows->size(), columns.size());
            return -1;
        }

        int64 row_count = -1;
        std::vector<QoreValue> values;
        std::vector<const QoreListNode*> lists;
        std::vector<bool> is_list;
        values.reserve(columns.size());
        lists.reserve(columns.size());
        is_list.reserve(columns.size());
        for (size_t i = 0; i < columns.size(); ++i) {
            if (i && !(i % 100) && qore_check_cancel(xsink, "Oracle OCI direct path row validation")) {
                stream.setError();
                return -1;
            }
            bool exists = false;
            QoreValue value = rows->getKeyValueExistence(columns[i].key.c_str(), exists);
            if (!exists) {
                stream.setError();
                xsink->raiseException("DBI:ORACLE:DIRECT-PATH-ERROR",
                    "OCI direct path row block is missing column '%s'", columns[i].key.c_str());
                return -1;
            }
            if (value.getType() == NT_LIST) {
                const QoreListNode* list = value.get<const QoreListNode>();
                int64 size = list->size();
                if (row_count < 0) {
                    row_count = size;
                } else if (row_count != size) {
                    stream.setError();
                    xsink->raiseException("DBI:ORACLE:DIRECT-PATH-ERROR",
                        "OCI direct path column '%s' has " QLLD " rows; expected " QLLD,
                        columns[i].key.c_str(), size, row_count);
                    return -1;
                }
                lists.push_back(list);
                is_list.push_back(true);
            } else {
                lists.push_back(nullptr);
                is_list.push_back(false);
            }
            values.push_back(value);
        }
        if (row_count < 0) {
            row_count = 1;
        }
        if (!row_count) {
            return 0;
        }
        if (static_cast<uint64_t>(row_count) > std::numeric_limits<size_t>::max()) {
            stream.setError();
            xsink->raiseException("DBI:ORACLE:DIRECT-PATH-ERROR",
                "OCI direct path row block is too large for this platform");
            return -1;
        }

        size_t count = static_cast<size_t>(row_count);
        std::vector<std::vector<QoreOracleBulkCell>> cells(columns.size());
        size_t payload_bytes = 0;
        for (size_t column = 0; column < columns.size(); ++column) {
            if (column && !(column % 100)
                && qore_check_cancel(xsink, "Oracle OCI direct path column serialization")) {
                stream.setError();
                return -1;
            }
            cells[column].resize(count);
            for (size_t row = 0; row < count; ++row) {
                if (row && !(row % 100)
                    && qore_check_cancel(xsink, "Oracle OCI direct path value serialization")) {
                    stream.setError();
                    return -1;
                }
                QoreValue value = is_list[column] ? lists[column]->retrieveEntry(row) : values[column];
                if (qoreOracleSerializeCell(conn, columns[column], value, cells[column][row], xsink)) {
                    stream.setError();
                    return -1;
                }
                if (cells[column][row].data.size() > std::numeric_limits<size_t>::max() - payload_bytes) {
                    stream.setError();
                    xsink->raiseException("DBI:ORACLE:DIRECT-PATH-ERROR",
                        "OCI direct path block payload size overflow");
                    return -1;
                }
                payload_bytes += cells[column][row].data.size();
            }
        }

        for (size_t offset = 0; offset < count;) {
            ub4 chunk_rows = static_cast<ub4>(std::min<size_t>(max_rows, count - offset));
            if (loadChunk(cells, offset, chunk_rows, xsink)) {
                stream.setError();
                return -1;
            }
            offset += chunk_rows;
        }
        return stream.addBytes(payload_bytes, xsink);
    }

    int terminate(bool success, ExceptionSink* xsink) {
        bool cleanup_ok = true;
        if (prepared) {
            if (qore_check_cancel(xsink, success ? "Oracle OCI direct path finish" : "Oracle OCI direct path abort")) {
                success = false;
                cleanup_ok = false;
            }
            sword rc;
            {
                QoreOracleCancelHelper cancel_helper(conn.svchp, conn.errhp);
                rc = success ? OCIDirPathFinish(ctx, conn.errhp) : OCIDirPathAbort(ctx, conn.errhp);
            }
            prepared = false;
            if (conn.checkerr(rc, success ? "QoreOracleBulkLoadState::terminate() OCIDirPathFinish"
                                         : "QoreOracleBulkLoadState::terminate() OCIDirPathAbort", xsink)) {
                cleanup_ok = false;
                if (success) {
                    OCIDirPathAbort(ctx, conn.errhp);
                }
            }
        }
        if (stream.finish(success && cleanup_ok, xsink)) {
            cleanup_ok = false;
        }
        return cleanup_ok && !*xsink ? 0 : -1;
    }

private:
    int resetHandles(ExceptionSink* xsink) {
        return conn.checkerr(OCIDirPathColArrayReset(array, conn.errhp),
                    "QoreOracleBulkLoadState::resetHandles() column array", xsink)
            || conn.checkerr(OCIDirPathStreamReset(stream_handle, conn.errhp),
                    "QoreOracleBulkLoadState::resetHandles() stream", xsink)
            ? -1 : 0;
    }

    int loadStream(ExceptionSink* xsink) {
        if (qore_check_cancel(xsink, "Oracle OCI direct path stream load")) {
            return -1;
        }
        sword rc;
        {
            QoreOracleCancelHelper cancel_helper(conn.svchp, conn.errhp);
            rc = OCIDirPathLoadStream(ctx, stream_handle, conn.errhp);
        }
        if (rc == OCI_SUCCESS) {
            return 0;
        }
        if (rc == OCI_SUCCESS_WITH_INFO) {
            return conn.checkerr(rc, "QoreOracleBulkLoadState::loadStream() OCIDirPathLoadStream", xsink);
        }
        if (rc == OCI_ERROR) {
            conn.checkerr(rc, "QoreOracleBulkLoadState::loadStream() OCIDirPathLoadStream", xsink);
        } else {
            xsink->raiseException("DBI:ORACLE:DIRECT-PATH-ERROR",
                "OCIDirPathLoadStream() returned unexpected status %d", static_cast<int>(rc));
        }
        return -1;
    }

    int loadChunk(const std::vector<std::vector<QoreOracleBulkCell>>& cells, size_t offset, ub4 row_count,
            ExceptionSink* xsink) {
        if (used && resetHandles(xsink)) {
            return -1;
        }
        for (ub4 row = 0; row < row_count; ++row) {
            if (row && !(row % 100) && qore_check_cancel(xsink, "Oracle OCI direct path column array")) {
                return -1;
            }
            for (ub2 column = 0; column < columns.size(); ++column) {
                if (column && !(column % 100)
                    && qore_check_cancel(xsink, "Oracle OCI direct path column array")) {
                    return -1;
                }
                const QoreOracleBulkCell& cell = cells[column][offset + row];
                ub1* data = cell.is_null ? nullptr
                    : reinterpret_cast<ub1*>(const_cast<char*>(cell.data.data()));
                ub1 flag = cell.is_null ? OCI_DIRPATH_COL_NULL : OCI_DIRPATH_COL_COMPLETE;
                sword rc = OCIDirPathColArrayEntrySet(array, conn.errhp, row, column, data,
                    static_cast<ub4>(cell.data.size()), flag);
                if (conn.checkerr(rc, "QoreOracleBulkLoadState::loadChunk() OCIDirPathColArrayEntrySet", xsink)) {
                    return -1;
                }
            }
        }

        ub4 row_offset = 0;
        while (row_offset < row_count) {
            if (qore_check_cancel(xsink, "Oracle OCI direct path column conversion")) {
                return -1;
            }
            sword rc;
            {
                QoreOracleCancelHelper cancel_helper(conn.svchp, conn.errhp);
                rc = OCIDirPathColArrayToStream(array, ctx, stream_handle, conn.errhp, row_count, row_offset);
            }
            if (rc == OCI_SUCCESS_WITH_INFO) {
                if (conn.checkerr(rc,
                        "QoreOracleBulkLoadState::loadChunk() OCIDirPathColArrayToStream", xsink)) {
                    return -1;
                }
                rc = OCI_SUCCESS;
            } else if (rc == OCI_ERROR) {
                conn.checkerr(rc, "QoreOracleBulkLoadState::loadChunk() OCIDirPathColArrayToStream", xsink);
                return -1;
            }
            if (rc != OCI_SUCCESS && rc != OCI_CONTINUE) {
                xsink->raiseException("DBI:ORACLE:DIRECT-PATH-ERROR",
                    "OCIDirPathColArrayToStream() returned unexpected status %d", static_cast<int>(rc));
                return -1;
            }

            ub4 next_offset = row_count;
            if (rc == OCI_CONTINUE) {
                ub4 converted = 0;
                ub4 size = sizeof(converted);
                if (conn.checkerr(OCIAttrGet(array, OCI_HTYPE_DIRPATH_COLUMN_ARRAY, &converted, &size,
                        OCI_ATTR_ROW_COUNT, conn.errhp),
                        "QoreOracleBulkLoadState::loadChunk() converted row count", xsink)) {
                    return -1;
                }
                next_offset = row_offset + converted;
                if (next_offset <= row_offset || next_offset > row_count) {
                    xsink->raiseException("DBI:ORACLE:DIRECT-PATH-ERROR",
                        "Oracle reported invalid direct path conversion progress %u for offset %u of %u rows",
                        converted, row_offset, row_count);
                    return -1;
                }
            }
            if (loadStream(xsink)) {
                return -1;
            }
            row_offset = next_offset;
            if (row_offset < row_count
                && conn.checkerr(OCIDirPathStreamReset(stream_handle, conn.errhp),
                    "QoreOracleBulkLoadState::loadChunk() continuation stream reset", xsink)) {
                return -1;
            }
        }
        used = true;
        return 0;
    }

    void freeHandles() {
        if (stream_handle) {
            OCIHandleFree(stream_handle, OCI_HTYPE_DIRPATH_STREAM);
            stream_handle = nullptr;
        }
        if (array) {
            OCIHandleFree(array, OCI_HTYPE_DIRPATH_COLUMN_ARRAY);
            array = nullptr;
        }
        if (ctx) {
            OCIHandleFree(ctx, OCI_HTYPE_DIRPATH_CTX);
            ctx = nullptr;
        }
    }

    QoreOracleConnection& conn;
    OCIDirPathCtx* ctx = nullptr;
    OCIDirPathColArray* array = nullptr;
    OCIDirPathStream* stream_handle = nullptr;
    std::vector<QoreOracleBulkColumn> columns;
    ub4 max_rows = 0;
    bool prepared = false;
    bool used = false;
    QoreOracleBulkStream stream;
};

int QoreOracleConnection::bulkLoadBegin(const QoreString* table, const QoreListNode* columns,
        const QoreHashNode* options, ExceptionSink* xsink) {
    if (bulk_load) {
        xsink->raiseException("DBI:ORACLE:DIRECT-PATH-ERROR", "an OCI direct path load is already active");
        return -1;
    }

    bool stream_bounds;
    bool parallel;
    bool nolog;
    ub4 buffer_size;
    if (qoreOracleGetBoolOption(options, "stream_bounds", true, stream_bounds, xsink)
        || qoreOracleGetBoolOption(options, "oracle_direct_path_parallel", false, parallel, xsink)
        || qoreOracleGetBoolOption(options, "oracle_direct_path_nolog", false, nolog, xsink)
        || qoreOracleGetBufferSize(options, buffer_size, xsink)) {
        return -1;
    }

    std::unique_ptr<QoreOracleBulkLoadState> state(new QoreOracleBulkLoadState(*this, stream_bounds));
    int rc = state->initialize(table, columns, parallel, nolog, buffer_size, xsink);
    if (rc) {
        return rc;
    }
    bulk_load = state.release();
    return 0;
}

int QoreOracleConnection::bulkLoadRows(const QoreHashNode* rows, ExceptionSink* xsink) {
    if (!bulk_load) {
        xsink->raiseException("DBI:ORACLE:DIRECT-PATH-ERROR", "no OCI direct path load is active");
        return -1;
    }
    return bulk_load->loadRows(rows, xsink);
}

int QoreOracleConnection::bulkLoadEnd(bool success, ExceptionSink* xsink) {
    if (!bulk_load) {
        xsink->raiseException("DBI:ORACLE:DIRECT-PATH-ERROR", "no OCI direct path load is active");
        return -1;
    }
    std::unique_ptr<QoreOracleBulkLoadState> state(bulk_load);
    bulk_load = nullptr;
    return state->terminate(success, xsink);
}

#endif // QDBI_METHOD_BULK_LOAD_BEGIN
