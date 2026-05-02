/* -*- mode: c++; indent-tabs-mode: nil -*- */
/*
    QoreOracleConnection.h

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

#ifndef _QORE_ORACLEDATA_H

#define _QORE_ORACLEDATA_H

#include <stdarg.h>

#include <set>

//#define QORE_OCI_FLAGS (OCI_DEFAULT|OCI_THREADED|OCI_NO_MUTEX|OCI_OBJECT)
#define QORE_OCI_FLAGS (OCI_DEFAULT|OCI_THREADED|OCI_OBJECT|OCI_ENV_EVENTS)

#ifndef VERSION_BUF_SIZE
#define VERSION_BUF_SIZE 512
#endif

// date format used when creating OCIDateTime values with year < 2 as OCIDateTimeConstruct will fail
#define ORA_BACKUP_DATE_FMT "YYYYMMDDHH24MISSFF6"

class QoreOracleEnvironment {
protected:
    OCIEnv* envhp;

public:
    DLLLOCAL QoreOracleEnvironment() : envhp(0) {
    }

    DLLLOCAL ~QoreOracleEnvironment() {
        if (envhp)
            OCIHandleFree(envhp, OCI_HTYPE_ENV);
    }

    DLLLOCAL int init() {
        return OCIEnvCreate(&envhp, QORE_OCI_FLAGS | OCI_NO_UCB, 0, 0, 0, 0, 0, 0) == OCI_SUCCESS ? 0 : -1;
    }

    DLLLOCAL int init(unsigned short charset) {
        return OCIEnvNlsCreate(&envhp, QORE_OCI_FLAGS | OCI_NO_UCB, 0, 0, 0, 0, 0, 0, charset, charset) == OCI_SUCCESS ? 0 : -1;
    }

    DLLLOCAL int nlsNameMapToOracle(const char *name, QoreString &out) {
        return nlsNameMap(name, out, OCI_NLS_CS_IANA_TO_ORA);
    }

    DLLLOCAL int nlsNameMapToQore(const char *name, QoreString &out) {
        return nlsNameMap(name, out, OCI_NLS_CS_ORA_TO_IANA);
    }

    DLLLOCAL int nlsNameMap(const char *name, QoreString &out, int dir) {
        assert(envhp);

        out.clear();
        out.reserve(OCI_NLS_MAXBUFSZ);

        int rc = OCINlsNameMap(envhp, (oratext*)out.getBuffer(), OCI_NLS_MAXBUFSZ, (oratext*)name, dir) == OCI_SUCCESS ? 0 : -1;
        if (!rc)
            out.terminate(strlen(out.getBuffer()));
        return rc;
    }

    DLLLOCAL unsigned short nlsCharSetNameToId(const char *name) {
        assert(envhp);
        return OCINlsCharSetNameToId(envhp, (oratext*)name);
    }

    DLLLOCAL operator bool() const {
        return envhp;
    }

    DLLLOCAL OCIEnv *operator*() const {
        return envhp;
    }
};

#define OPT_NUM_OPTIMAL 0  // return numbers as int64 if it fits or "number" if not
#define OPT_NUM_STRING  1  // always return number types as strings
#define OPT_NUM_NUMERIC 2  // always return number types as "number"

// return optimal numeric values if options are supported
#define OPT_NUM_DEFAULT OPT_NUM_OPTIMAL

// forward reference
class QorePreparedStatement;

class QoreOracleConnection {
public:
    QoreOracleEnvironment env;

    OCIError* errhp;
    OCISvcCtx* svchp;
    OCIServer* srvhp;
    OCISession* usrhp;

    ub2 charsetid;
    // "fake" connection for OCILIB stuff
    OCI_Connection* ocilib_cn;
    Datasource& ds;
    bool ocilib_init;
    const AbstractQoreZoneInfo* server_tz;

    OCI_Library ocilib;

    QoreString cstr; // connection string
    int number_support;

    DLLLOCAL QoreOracleConnection(Datasource &n_ds, ExceptionSink* xsink);
    DLLLOCAL ~QoreOracleConnection();

    DLLLOCAL int checkerr(sword status, const char *query_name, ExceptionSink* xsink, bool* retry = nullptr);

    DLLLOCAL bool handleError(ExceptionSink* xsink, const char* who, bool can_retry);

    DLLLOCAL int doException(const char *query_name, text errbuf[], sb4 errcode, ExceptionSink *xsink);

    DLLLOCAL int descriptorAlloc(void **descpp, unsigned type, const char *who, ExceptionSink* xsink);

    DLLLOCAL int handleAlloc(void **descpp, unsigned type, const char *who, ExceptionSink* xsink);

    DLLLOCAL int logon(ExceptionSink* xsink);

    DLLLOCAL void clearWarnings() {
        ub4 ix = 1;
        int errcode;
        text errbuf[512];

        while (OCIErrorGet(errhp, ix, (text*)0, &errcode, errbuf, (ub4)sizeof(errbuf), OCI_HTYPE_ERROR) != OCI_NO_DATA) {
            doWarning(errcode, "ORACLE-WARNING", remove_trailing_newlines((char*)errbuf));
            ++ix;
        }
    }

    // logoff but do not process error return values
    DLLLOCAL int logoff() {
        assert(svchp);

        // free all cached typeinfo objects
        if (ocilib_cn)
            clearCache();

        int rc = OCISessionEnd(svchp, errhp, usrhp, 0);
        OCIServerDetach(srvhp, errhp, OCI_DEFAULT);
        return rc;
    }

    // clear cached objects
    DLLLOCAL void clearCache();

    DLLLOCAL int commit(ExceptionSink* xsink);
    DLLLOCAL int rollback(ExceptionSink* xsink);

    DLLLOCAL DateTimeNode* getTimestamp(bool get_tz, OCIDateTime *odt, ExceptionSink* xsink);

    DLLLOCAL DateTimeNode* getDate(OCIDate* dt);

    DLLLOCAL DateTimeNode* getIntervalYearMonth(OCIInterval *oi, ExceptionSink* xsink) {
        sb4 year, month;
        if (checkerr(OCIIntervalGetYearMonth(*env, errhp, &year, &month, oi), "QoreOracleConnection::getIntervalYearMonth()", xsink))
            return nullptr;

        return new DateTimeNode(year, month, 0, 0, 0, 0, 0, true);
    }

    DLLLOCAL DateTimeNode *getIntervalDaySecond(OCIInterval *oi, ExceptionSink* xsink) {
        //printd(5, "QoreOracleConnection::getIntervalDaySecond() using INTERVAL_DS handle %p\n", buf.oi);
        sb4 day, hour, minute, second, nanosecond;
        if (checkerr(OCIIntervalGetDaySecond(*env, errhp, &day, &hour, &minute, &second, &nanosecond, oi), "QoreOracleConnection::getIntervalDaySecond()", xsink))
            return nullptr;

        return DateTimeNode::makeRelative(0, 0, day, hour, minute, second, nanosecond / 1000);
    }

    DLLLOCAL BinaryNode *getBinary(OCIRaw *rawp) {
        BinaryNode *b = new BinaryNode;
        b->append(OCIRawPtr(*env, rawp), OCIRawSize(*env, rawp));
        return b;
    }

    DLLLOCAL int rawResize(OCIRaw **rawp, unsigned short size, ExceptionSink* xsink) {
        return checkerr(OCIRawResize(*env, errhp, size, rawp), "QoreOracleConnection::rawResize()", xsink);
    }

    DLLLOCAL int rawFree(OCIRaw **rawp, ExceptionSink* xsink) {
        return rawResize(rawp, 0, xsink);
    }

    DLLLOCAL int dateTimeConstruct(OCIDateTime *odt, const DateTime &d, ExceptionSink* xsink) {
        // get broken-down time information in the server's time zone
        qore_tm info;
        d.getInfo(getTZ(), info);

        // only use OCIDateTimeConstruct if the year > 0001
        if (info.year > 1) {
            // issue #2448 Oracle does not handle time zone information correctly for DATE values in selects
            // because we convert the date/time value to the server's expected timezone, and because Oracle
            // always assumes that timestamp values without a timezone component have the current session
            // timezone, we leave it off which also fixes the date issue

            //printd(5, "QoreOracleConnection::dateTimeConstruct(year: %d, month: %d, day: %d, hour: %d, minute: %d, second: %d, us: %d) server tz: %s\n", info.year, info.month, info.day, info.hour, info.minute, info.second, info.us, info.regionName());
            return checkerr(OCIDateTimeConstruct(*env, errhp, odt, info.year, info.month, info.day, info.hour, info.minute, info.second, (info.us * 1000), (oratext*)0, 6), "QoreOracleConnection::dateTimeConstruct()", xsink);
        }

        QoreString dstr;
        dstr.sprintf("%04d%02d%02d%02d%02d%06d", info.year, info.month, info.day, info.hour, info.minute, info.second, info.us);

        //printd(5, "QoreOracleConnection::dateTimeConstruct() d: %s (%s)\n", dstr.getBuffer(), ORA_BACKUP_DATE_FMT);

        return checkerr(OCIDateTimeFromText(*env, errhp, (OraText*)dstr.getBuffer(),
                                            dstr.strlen(), (OraText*)ORA_BACKUP_DATE_FMT,
                                            sizeof(ORA_BACKUP_DATE_FMT), 0, 0, odt), "QoreOracleConnection::dateTimeConstruct() fromText", xsink);
    }

    DLLLOCAL QoreStringNode *getServerVersion(ExceptionSink* xsink) {
        //printd(0, "QoreOracleConnection::getServerVersion() this: %p ds: %p envhp: %p svchp: %p errhp: %p\n", this, &ds, *env, svchp, errhp);
        // buffer for version information
        char version_buf[VERSION_BUF_SIZE + 1];

        // execute OCIServerVersion and check status code
        if (checkerr(OCIServerVersion(svchp, errhp, (OraText*)version_buf, VERSION_BUF_SIZE, OCI_HTYPE_SVCCTX), "QoreOracleConnection::getServerVersion()", xsink))
            return nullptr;

        return new QoreStringNode(version_buf);
    }

    DLLLOCAL BinaryNode *readBlob(OCILobLocator *lobp, ExceptionSink* xsink);
    DLLLOCAL QoreStringNode *readClob(OCILobLocator *lobp, const QoreEncoding *enc, ExceptionSink* xsink);

    DLLLOCAL int writeLob(OCILobLocator* lobp, void* bufp, oraub8 buflen, bool clob, const char* desc, ExceptionSink* xsink);

    DLLLOCAL int setOption(const char* opt, QoreValue val, ExceptionSink* xsink) {
        if (!strcasecmp(opt, DBI_OPT_NUMBER_OPT)) {
            number_support = OPT_NUM_OPTIMAL;
            return 0;
        }
        if (!strcasecmp(opt, DBI_OPT_NUMBER_STRING)) {
            number_support = OPT_NUM_STRING;
            return 0;
        }
        if (!strcasecmp(opt, DBI_OPT_NUMBER_NUMERIC)) {
            number_support = OPT_NUM_NUMERIC;
            return 0;
        }
        if (!strcasecmp(opt, DBI_OPT_TIMEZONE)) {
            assert(val.getType() == NT_STRING);
            QoreStringValueHelper str(val);
            const AbstractQoreZoneInfo* tz = find_create_timezone(str->c_str(), xsink);
            if (*xsink)
                return -1;
            server_tz = tz;
            return 0;
        }
        xsink->raiseException("ORACLE-OPTION-ERROR", "invalid option '%s'; please try again with a valid option name (syntax: option=value)", opt);
        return -1;
    }

    DLLLOCAL QoreValue getOption(const char* opt) {
        if (!strcasecmp(opt, DBI_OPT_NUMBER_OPT))
            return number_support == OPT_NUM_OPTIMAL;

        if (!strcasecmp(opt, DBI_OPT_NUMBER_STRING))
            return number_support == OPT_NUM_STRING;

        if (!strcasecmp(opt, DBI_OPT_NUMBER_NUMERIC))
            return number_support == OPT_NUM_NUMERIC;

        assert(!strcasecmp(opt, DBI_OPT_TIMEZONE));
        return new QoreStringNode(tz_get_region_name(server_tz));
    }

    DLLLOCAL const AbstractQoreZoneInfo* getTZ() const {
        return server_tz;
    }

    DLLLOCAL int getNumberOption() const {
        return number_support;
    }

    DLLLOCAL QoreValue getNumberOptimal(const char* str) const {
        // see if the value can fit in an int
        size_t len = strlen(str);
        bool sign = str[0] == '-';
        if (sign)
            --len;
        if (!strchr(str, '.')
            && (len < 19
                || (len == 19 &&
                    ((!sign && strcmp(str, "9223372036854775807") <= 0)
                    ||(sign && strcmp(str, "-9223372036854775808") <= 0)))))
            return strtoll(str, 0, 10);

        return new QoreNumberNode(str);
    }

    DLLLOCAL void doWarning(int errcode, const char* warn, const char* desc) {
        Queue* q;
        QoreHashNode* h = ds.getEventQueueHash(q, QDBI_EVENT_WARNING);
        if (!h) {
            //printd(5, "QoreOracleConnection::doWarning() this: %p %s: %s: IGNORING WARNING (h: %p q: %p)\n", this, warn, desc, h, q);
            return;
        }
        h->setKeyValue("warning", new QoreStringNode(warn), 0);
        h->setKeyValue("desc", new QoreStringNode(desc), 0);
        q->pushAndTakeRef(h);
        //printd(5, "QoreOracleConnection::doWarning() this: %p %s: %s: RAISED WARNING (q: %p size: %d)\n", this, warn, desc, q, q->size());
    }

#if 0
    DLLLOCAL void doWarningString(AbstractQoreNode* info, const char* warn, const char* fmt, ...) {
        Queue* q;
        QoreHashNode* h = ds.getEventQueueHash(q, QDBI_EVENT_WARNING);
        if (!h)
            return;

        va_list args;
        QoreStringNode* desc = new QoreStringNode;

        while (true) {
            va_start(args, fmt);
            int rc = desc->vsprintf(fmt, args);
            va_end(args);
            if (!rc)
                break;
        }

        h->setKeyValue("warning", new QoreStringNode(warn), 0);
        h->setKeyValue("desc", desc, 0);
        if (info)
            h->setKeyValue("info", info, 0);
        q->pushAndTakeRef(h);
    }
#endif

    DLLLOCAL void registerStatement(QorePreparedStatement* stmt) {
        assert(stmt_set.find(stmt) == stmt_set.end());
        stmt_set.insert(stmt);
    }

    DLLLOCAL void deregisterStatement(QorePreparedStatement* stmt) {
        stmt_set_t::iterator i = stmt_set.find(stmt);
        if (i != stmt_set.end()) {
            stmt_set.erase(i);
        }
    }

    DLLLOCAL static void descriptorFree(void *descp, unsigned type) {
        OCIDescriptorFree(descp, type);
    }

    DLLLOCAL static void handleFree(void *hndlp, unsigned type) {
        OCIHandleFree(hndlp, type);
    }

protected:
    typedef std::set<QorePreparedStatement*> stmt_set_t;
    stmt_set_t stmt_set;

    DLLLOCAL static sb4 readClobCallback(void *sp, CONST dvoid *bufp, ub4 len, ub1 piece) {
        //printd(5, "QoreOracleConnection::readClobCallback(%p, %p, %d, %d)\n", sp, bufp, len, piece);
        (reinterpret_cast<QoreStringNode *>(sp))->concat((char*)bufp, len);
        return OCI_CONTINUE;
    }

    DLLLOCAL static sb4 readBlobCallback(void *bp, CONST dvoid *bufp, ub4 len, ub1 piece) {
        //printd(5, "QoreOracleConnection::readBlobCallback(%p, %p, %d, %d)\n", bp, bufp, len, piece);
        ((BinaryNode*)bp)->append((char*)bufp, len);
        return OCI_CONTINUE;
    }
};

#endif
