/* -*- mode: c++; indent-tabs-mode: nil -*- */
/*
    QoreOracleConnection.cpp

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

#include "ocilib_internal.h"

// ensure that numeric values are returned with no thousands separator and a dot decimal separator
// despite the locale because we currently retrieve number values as strings
static char session_sql[] = "alter session set nls_numeric_characters = \". \"";

QoreOracleConnection::QoreOracleConnection(Datasource &n_ds, ExceptionSink *xsink)
  : errhp(0), svchp(0), srvhp(0), usrhp(0), ocilib_cn(0), ds(n_ds), ocilib_init(false),
    server_tz(currentTZ()),
#ifdef QDBI_METHOD_BULK_LOAD_BEGIN
    bulk_load(nullptr),
#endif
    number_support(OPT_NUM_DEFAULT) {
   // locking is done on the level above with the Datasource class

   int port = ds.getPort();

   if (port)
      cstr.sprintf("//%s:%d/%s", ds.getHostName(), port, ds.getDBName());
   else
      cstr.concat(ds.getDBName());

   //printd(5, "QoreOracleConnection::QoreOracleConnection(): user: %s pass: %s db: %s (oracle encoding: %s) xsink: %d\n", ds.getUsername(), ds.getPassword(), cstr.getBuffer(), ds.getDBEncoding() ? ds.getDBEncoding() : "(none)", (bool)*xsink);

   bool set_charset = false;

   QoreString encoding;

   {
      // create temporary environment handle
      QoreOracleEnvironment tmpenv;
      tmpenv.init();

      if (ds.getDBEncoding()) {
         set_charset = true;

         // get character set ID
         charsetid = tmpenv.nlsCharSetNameToId(ds.getDBEncoding());
      } else { // get Oracle character set name from OS character set name
         if (tmpenv.nlsNameMapToOracle(QCS_DEFAULT->getCode(), encoding) != OCI_SUCCESS) {
            xsink->raiseException("DBI:ORACLE:UNKNOWN-CHARACTER-ENCODING", "cannot map default OS encoding '%s' to Oracle character encoding", QCS_DEFAULT->getCode());
            return;
         }
         ds.setDBEncoding(encoding.getBuffer());
         ds.setQoreEncoding(QCS_DEFAULT);

         // get character set ID
         charsetid = tmpenv.nlsCharSetNameToId(encoding.getBuffer());
         // printd(5, "QoreOracleConnection::QoreOracleConnection() setting Oracle encoding to '%s' from default OS encoding '%s'\n", charset, QCS_DEFAULT->getCode());
      }
   }

   if (!charsetid) {
      xsink->raiseException("DBI:ORACLE:UNKNOWN-CHARACTER-ENCODING", "this installation of Oracle does not support the '%s' character encoding", ds.getDBEncoding());
      return;
   }

   // printd(5, "Oracle character encoding '%s' has ID %d, OCI_FLAGS=%d\n", charset, charsetid, OCI_FLAGS);
   // create environment with default character set
   if (env.init(charsetid)) {
      xsink->raiseException("DBI:ORACLE:OPEN-ERROR", "error creating new environment handle with encoding '%s'", ds.getDBEncoding());
      return;
   }

   //printd(5, "QoreOracleConnection::QoreOracleConnection() ds=%p allocated envhp=%p\n", ds, *env);

   // map the Oracle character set to a qore character set
   if (set_charset) {
      // map Oracle character encoding name to QORE/OS character encoding name
      if (!env.nlsNameMapToQore(ds.getDBEncoding(), encoding)) {
         //printd(5, "QoreOracleConnection::QoreOracleConnection() Oracle character encoding '%s' mapped to '%s' character encoding\n", ds.getDBEncoding(), encoding.getBuffer());
         assert(encoding.strlen());
         ds.setQoreEncoding(encoding.getBuffer());
      }
      else {
         xsink->raiseException("DBI:ORACLE:OPEN-ERROR", "error mapping Oracle character encoding '%s' to a qore encoding: unknown encoding", ds.getDBEncoding());
         return;
      }
   }

   // cannot use handleAlloc() here as we are allocating errhp now
   if (OCIHandleAlloc(*env, (dvoid **) &errhp, OCI_HTYPE_ERROR, 0, 0) != OCI_SUCCESS) {
      xsink->raiseException("DBI:ORACLE:OPEN-ERROR", "failed to allocate error handle for connection");
      return;
   }

   if (OCIHandleAlloc(*env, (dvoid **) &svchp, OCI_HTYPE_SVCCTX, 0, 0) != OCI_SUCCESS) {
       xsink->raiseException("DBI:ORACLE:OPEN-ERROR", "failed to allocate service handle for connection");
       return;
   }

   if (OCIHandleAlloc(*env, (dvoid **) &srvhp, OCI_HTYPE_SERVER, 0, 0) != OCI_SUCCESS) {
       xsink->raiseException("DBI:ORACLE:OPEN-ERROR", "failed to allocate server handle for connection");
       return;
   }

   if (OCIHandleAlloc(*env, (dvoid **) &usrhp, OCI_HTYPE_SESSION, 0, 0) != OCI_SUCCESS) {
       xsink->raiseException("DBI:ORACLE:OPEN-ERROR", "failed to allocate session handle for connection");
       return;
   }

   // first clear warnings before logging in again
   clearWarnings();
   //printd(5, "QoreOracleConnection::QoreOracleConnection() about to call OCILogon()\n");
   if (logon(xsink))
      return;
   // check and clear warnings again
   clearWarnings();

   //printd(5, "QoreOracleConnection::QoreOracleConnection() datasource %p for DB=%s open (envhp=%p)\n", &ds, cstr.getBuffer(), *env);

   if (!OCI_Initialize2(&ocilib, *env, errhp, ocilib_err_handler, 0, QORE_OCI_FLAGS)) {
      xsink->raiseException("DBI:ORACLE:OPEN-ERROR", "failed to allocate OCILIB support handlers");
      return;
   }

   ocilib_init = true;

   //printd(5, "ocilib=%p mode=%d\n", &ocilib, ocilib.env_mode);

   ocilib_cn = new OCI_Connection;
   // fake the OCI_Connection
   ocilib_cn->err = errhp;
   ocilib_cn->cxt = svchp;
   ocilib_cn->tinfs = OCI_ListCreate2(&ocilib, OCI_IPC_TYPE_INFO);

   ocilib_cn->svr = srvhp;                        // OCI server handle
   ocilib_cn->ses = usrhp;                        // OCI session handle

   // then reset unused attributes
   ocilib_cn->db = 0;
   ocilib_cn->user = 0;                           // user
   ocilib_cn->pwd = 0;                            // password
   ocilib_cn->stmts = 0;                          // list of statements
   ocilib_cn->trsns = 0;                          // list of transactions

   ocilib_cn->trs = 0;                            // pointer to current transaction object
   ocilib_cn->pool = 0;                           // pointer to connection pool parent
   ocilib_cn->svopt = 0;                          // Pointer to server output object
   ocilib_cn->autocom = false;                    // auto commit mode
   ocilib_cn->nb_files = 0;                       // number of OCI_File opened by the connection
   ocilib_cn->mode = 0;                           // session mode
   ocilib_cn->cstate = 0;                         // connection state
   ocilib_cn->usrdata = 0;                        // user data

   ocilib_cn->fmt_date = 0;                       // date string format for conversion
   ocilib_cn->fmt_num = 0;                        // numeric string format for conversion
   ocilib_cn->ver_str = 0;                        // string  server version
   ocilib_cn->ver_num = ocilib.version_runtime;   // numeric server version
   ocilib_cn->trace = 0;                          // trace information

   //printd(5, "QoreOracleConnection::QoreOracleConnection() this: %p ds: %p envhp: %p svchp: %p errhp: %p xsink: %d\n", this, &ds, *env, svchp, errhp, (bool)*xsink);
}

QoreOracleConnection::~QoreOracleConnection() {
   //printd(5, "QoreOracleConnection::~QoreOracleConnection() this=%p ds=%p envhp=%p svchp=%p ocilib envhp=%p\n", this, &ds, *env, svchp, ocilib.env);
   //printd(5, "QoreOracleConnection::~QoreOracleConnection(): connection to %s closed.\n", ds.getDBName());
   //printd(5, "QoreOracleConnection::~QoreOracleConnection(): svchp, errhp: %p, %p\n", svchp, errhp);
#ifdef QDBI_METHOD_BULK_LOAD_BEGIN
   if (bulk_load) {
      ExceptionSink xsink;
      bulkLoadEnd(false, &xsink);
   }
#endif

   if (svchp)
      logoff();

   if (ocilib_cn) {
      if (ocilib_cn->tinfs)
         OCI_ListFree(&ocilib, ocilib_cn->tinfs);

      OCI_FREE(ocilib_cn->fmt_num);

      if (ocilib_cn->trace)
         OCI_MemFree(ocilib_cn->trace);

      delete ocilib_cn;
   }

   if (ocilib_init) {
      ExceptionSink xsink;
      OCI_Cleanup2(&ocilib, &xsink);
#ifndef DEBUG
      // ignore cleanup exceptions in non-debugging mode
      xsink.clear();
#endif
   }

   if (errhp)
      OCIHandleFree(errhp, OCI_HTYPE_ERROR);

   if (srvhp)
       OCIHandleFree(srvhp, (ub4) OCI_HTYPE_SERVER);
   if (usrhp)
       OCIHandleFree(usrhp, (ub4) OCI_HTYPE_SESSION);
   if (svchp)
       OCIHandleFree(svchp, OCI_HTYPE_SVCCTX);
}

void QoreOracleConnection::clearCache() {
   OCI_ListForEach(&ocilib, ocilib_cn->tinfs, (boolean (*)(void *)) OCI_TypeInfoClose);
   OCI_ListClear(&ocilib, ocilib_cn->tinfs);
}

int QoreOracleConnection::checkerr(sword status, const char* query_name, ExceptionSink* xsink, bool* retry) {
    sb4 errcode = 0;

    //printd(5, "QoreOracleConnection::checkerr(%p, %d, %s, isEvent=%d)\n", errhp, status, query_name ? query_name : "none", *xsink);
    switch (status) {
        case OCI_SUCCESS:
            return 0;

        case OCI_SUCCESS_WITH_INFO: {
            text errbuf[512];

            OCIErrorGet((dvoid *)errhp, (ub4) 1, (text *) NULL, &errcode, errbuf, (ub4) sizeof(errbuf), OCI_HTYPE_ERROR);
            //printd(5, "WARNING: %s returned OCI_SUCCESS_WITH_INFO: %s\n", query_name ? query_name : "<unknown>", remove_trailing_newlines((char *)errbuf));
            // ignore SUCCESS_WITH_INFO codes
            return 0;
        }

        case OCI_ERROR: {
            bool retry_flag = handleError(xsink, query_name, (bool)retry);
            if (retry) {
                assert(!*retry);
                *retry = retry_flag;
            }
            break;
        }

        case OCI_INVALID_HANDLE:
            if (query_name)
                xsink->raiseException("DBI:ORACLE:OCI-INVALID-HANDLE", "%s@%s: %s: an invalid OCI handle was used", ds.getUsername(), ds.getDBName(), query_name);
            else
                xsink->raiseException("DBI:ORACLE:OCI-INVALID-HANDLE", "%s@%s: an invalid OCI handle was used", ds.getUsername(), ds.getDBName());
            break;

        case OCI_NEED_DATA:
            xsink->raiseException("DBI:ORACLE:OCI-NEED-DATA", "Oracle OCI error");
            break;

        case OCI_NO_DATA:
            xsink->raiseException("DBI:ORACLE:OCI-NODATA", "Oracle OCI error");
            break;

        case OCI_STILL_EXECUTING:
            xsink->raiseException("DBI:ORACLE:OCI-STILL-EXECUTING", "Oracle OCI error");
            break;

        case OCI_CONTINUE:
            xsink->raiseException("DBI:ORACLE:OCI-CONTINUE", "Oracle OCI error");
            break;

        default:
            xsink->raiseException("DBI:ORACLE:UNKNOWN-ERROR", "unknown OCI error code %d", status);
            break;
    }
    return -1;
}

bool QoreOracleConnection::handleError(ExceptionSink* xsink, const char* who, bool can_retry) {
    // see if we have a lost connection from the error code
    int ping = -1;

    // get and save error information
    sb4 errcode;
    text errbuf[512];
    OCIErrorGet((dvoid*)errhp, (ub4)1, (text*)nullptr, &errcode, errbuf, (ub4)sizeof(errbuf), OCI_HTYPE_ERROR);
    // ORA-03113 and ORA-03114 are lost connections
    // issue #802: ORA-01041 is only raised due to a bug in Oracle pre 12c
    if (errcode == 3113 || errcode == 3114 || errcode == 1041)
        ping = 0;

    //dbg();
    // see if server is connected
    if (ping == -1 && ocilib_init) {
        ExceptionSink xsink2;
        ping = OCI_Ping(&ocilib, ocilib_cn, &xsink2);
        // do not allow ping exceptions to be propagated to the caller
        xsink2.clear();
    }

    if (!ping) {
        // if there is at least one SQLStatement active on the connection, there will be a transaction in place
        // therefore in all cases all statements will be invalidated and closed when we lose a connection
        // we can only recover a plain "exec" or "select" call
        if (ds.activeTransaction()) {
            xsink->raiseException("DBI:ORACLE:TRANSACTION-ERROR", "connection to Oracle database server %s@%s lost " \
                "while in a transaction; transaction has been lost", ds.getUsername(), ds.getDBName());
        }

        // reset current statement state while the driver-specific context data is still present
        for (auto& i : stmt_set) {
            i->clear(xsink);
        }
        // free and reset statement states for all active statements while the driver-specific context data is still present
        ds.connectionLost(xsink);

        if (can_retry) {
            // try to reconnect
            logoff();

            //printd(5, "QoreOracleStatement::execute() about to execute OCILogon() for reconnect (trans: %d)\n", ds->activeTransaction());
            if (logon(xsink)) {
                //printd(5, "QoreOracleStatement::execute() conn: %p reconnect failed, marking connection as closed\n", &conn);
                // free state completely
                for (auto& i : stmt_set) {
                    i->reset(xsink);
                }
                // close datasource and remove private data
                ds.connectionAborted(xsink);
                return false;
            }

            // clear warnings
            clearWarnings();

            // don't execute again if any exceptions have occured, including if the connection was aborted while in a transaction
            if (*xsink) {
                // close all statements and remove private data but leave datasource open
                ds.connectionRecovered(xsink);
                return false;
            }

            for (auto& i : stmt_set) {
                // try to recreate the statement context
                if (i->rebindAbortedConnection(xsink))
                    return false;
            }

            return true;
        }

        for (auto& i : stmt_set) {
            i->reset(xsink);
        }
        // close datasource and remove private data
        ds.connectionAborted(xsink);
        return false;
    }

    //printd(5, "QoreOracleStatement::execute() error, but it's connected; status: %d who: %s\n", status, who);
    doException(who, errbuf, errcode, xsink);
    return false;
}

int QoreOracleConnection::doException(const char *query_name, text errbuf[], sb4 errcode, ExceptionSink *xsink) {
    // add ORA-xxxxx code to exception in arg hash in the "alterr" key
    QoreHashNode* arg = new QoreHashNode(autoTypeInfo);
    arg->setKeyValue("alterr", new QoreStringNodeMaker("OCI-%05d", (int)errcode), xsink);
    if (query_name)
       xsink->raiseExceptionArg("DBI:ORACLE:OCI-ERROR", arg, "%s@%s: %s: %s", ds.getUsername(), ds.getDBName(), query_name, remove_trailing_newlines((char *)errbuf));
    else
       xsink->raiseExceptionArg("DBI:ORACLE:OCI-ERROR", arg, "%s@%s: %s", ds.getUsername(), ds.getDBName(), remove_trailing_newlines((char*)errbuf));
    return -1;
}

int QoreOracleConnection::logon(ExceptionSink *xsink) {
   const std::string &user = ds.getUsernameStr();
   const std::string &pass = ds.getPasswordStr();

   // format potential db link string: host[:port]/SID or SID only
   QoreString dblink;
   if (ds.getHostName() && strlen(ds.getHostName())) {
       dblink.concat(ds.getHostName());
       if (ds.getPort()) {
           dblink.concat(":");
           dblink.sprintf("%d", ds.getPort());
       }
       dblink.concat("/");
   }
   dblink.concat(ds.getDBName());

   int e;

   // user session login creds.
   e = checkerr(OCIAttrSet(usrhp, OCI_HTYPE_SESSION, (text *)user.c_str(), user.size(), OCI_ATTR_USERNAME, errhp), "QoreOracleConnection::logon() Set username", xsink);
   if (e) return -1;

   e = checkerr(OCIAttrSet(usrhp, OCI_HTYPE_SESSION, (text *)pass.c_str(), pass.size(), OCI_ATTR_PASSWORD, errhp), "QoreOracleConnection::logon() Set password", xsink);
   if (e) return -1;

   // Check for interrupt before server attach
   if (qore_check_cancel(xsink)) {
      return -1;
   }

   /* attach to the server - use default host? */
   e = checkerr(OCIServerAttach(srvhp, errhp, (text *)dblink.getBuffer(), dblink.size(), (ub4) OCI_DEFAULT), "QoreOracleConnection::logon() server attach", xsink);
   if (e) return -1;

   /* set the server attribute in the service context */
   e = checkerr(OCIAttrSet(svchp, OCI_HTYPE_SVCCTX, srvhp, 0, OCI_ATTR_SERVER, errhp), "QoreOracleConnection::logon() server to service context", xsink);
   if (e) return -1;

   // Check for interrupt before session begin
   if (qore_check_cancel(xsink)) {
      return -1;
   }

   /* log on */
   e = checkerr(OCISessionBegin(svchp, errhp, usrhp, OCI_CRED_RDBMS, OCI_DEFAULT), "QoreOracleConnection::logon() session begin", xsink);
   if (e) return -1;

   /* set the session attribute in the service context */
   e = checkerr(OCIAttrSet(svchp, OCI_HTYPE_SVCCTX, usrhp, 0, OCI_ATTR_SESSION, errhp), "QoreOracleConnection::logon() set the session attribute in the service context", xsink);
   if (e) return -1;

//      return checkerr(OCILogon(*env, errhp, &svchp, (text *)user.c_str(), user.size(), (text *)pass.c_str(), pass.size(), (text *)cstr.getBuffer(), cstr.strlen()), "QoreOracleConnection::logon()", xsink);

   //printd(5, "QoreOracleConnection::logon() %s/%s@%s succeeded\n", user.c_str(), pass.c_str(), dblink.getBuffer());

   // make sure we can read number characters
   QoreOracleSimpleStatement stmt(*this);
   return stmt.exec(session_sql, sizeof(session_sql), xsink);
}

int QoreOracleConnection::descriptorAlloc(void** descpp, unsigned type, const char* who, ExceptionSink* xsink) {
   return checkerr(OCIDescriptorAlloc(*env, descpp, type, 0, 0), who, xsink);
}

int QoreOracleConnection::handleAlloc(void** hndlpp, unsigned type, const char* who, ExceptionSink* xsink) {
   return checkerr(OCIHandleAlloc(*env, hndlpp, type, 0, 0), who, xsink);
}

int QoreOracleConnection::commit(ExceptionSink* xsink) {
   if (qore_check_cancel(xsink)) {
      return -1;
   }
   sword rc;
   {
      QoreOracleCancelHelper cancel_helper(svchp, errhp);
      rc = OCITransCommit(svchp, errhp, (ub4) 0);
   }
   return checkerr(rc, "QoreOracleConnection:commit()", xsink);
}

int QoreOracleConnection::rollback(ExceptionSink* xsink) {
#ifdef QDBI_METHOD_BULK_LOAD_BEGIN
   // ManagedDatasource rolls back before it invokes the low-level close callback.  OCI rejects an
   // ordinary transaction rollback while a direct path transaction is active, so terminate that
   // protocol first.  The core's later close backstop is harmless because bulk_load is already null.
   if (bulk_load && bulkLoadEnd(false, xsink)) {
      return -1;
   }
#endif
   if (qore_check_cancel(xsink)) {
      return -1;
   }
   sword rc;
   {
      QoreOracleCancelHelper cancel_helper(svchp, errhp);
      rc = OCITransRollback(svchp, errhp, (ub4) 0);
   }
   return checkerr(rc, "QoreOracleConnection:rollback()", xsink);
}

DateTimeNode* QoreOracleConnection::getDate(OCIDate* dt) {
    sb2 year;
    ub1 month, day;
    OCIDateGetDate(dt, &year, &month, &day);

    ub1 hour, minute, second;
    OCIDateGetTime(dt, &hour, &minute, &second);

    return DateTimeNode::makeAbsolute(getTZ(), year, month, day, hour, minute, second, 0);
}

DateTimeNode* QoreOracleConnection::getTimestamp(bool get_tz, OCIDateTime *odt, ExceptionSink *xsink) {
    //printd(5, "QoreOracleConnection::getTimestamp() using TIMESTAMP handle %p\n", odt);
    sb2 year;
    ub1 month, day;
    if (checkerr(OCIDateTimeGetDate(*env, errhp, odt, &year, &month, &day), "OCIDateTimeGetDate()", xsink))
        return nullptr;

    ub1 hour, minute, second;
    ub4 ns; // nanoseconds
    if (checkerr(OCIDateTimeGetTime(*env, errhp, odt, &hour, &minute, &second, &ns), "OCIDateTimeGetTime()", xsink))
        return nullptr;

    const AbstractQoreZoneInfo *zone;
    if (!get_tz)
        zone = getTZ();
    else {
        // try to get time zone from date value
        // time zone offset, hour and minute
        sb1 oh = 0, om = 0;
        sword err = OCIDateTimeGetTimeZoneOffset(*env, errhp, odt, &oh, &om);
        if (err == OCI_SUCCESS) {
            //printd(5, "err=%d, oh=%d, om=%d, se=%d\n", err, (int)oh, (int)om, oh * 3600 + om * 60);
            zone = findCreateOffsetZone(oh * 3600 + om * 60);
        }
        else {
            //printd(5, "QoreOracleConnection::getTimestamp() this=%p time zone retrieval failed (%04d-%02d-%02d %02d:%02d:%02d)\n", this, year, month, day, hour, minute, second);
            // no time zone info, assume local time
            zone = getTZ();
        }
    }
    return DateTimeNode::makeAbsolute(zone, year, month, day, hour, minute, second, ns / 1000);
}

BinaryNode *QoreOracleConnection::readBlob(OCILobLocator *lobp, ExceptionSink *xsink) {
    // check for interrupt before LOB read
    if (qore_check_cancel(xsink)) {
        return nullptr;
    }
    // retrieve *LOB data
    void *dbuf = malloc(LOB_BLOCK_SIZE);
    ON_BLOCK_EXIT(free, dbuf);
    ub4 amt = 0;

    SimpleRefHolder<BinaryNode> b(new BinaryNode);
    // read LOB data in streaming callback mode with cancel helper for interruptibility
    sword rc;
    {
        QoreOracleCancelHelper cancel_helper(svchp, errhp);
        rc = OCILobRead(svchp, errhp, lobp, &amt, 1, dbuf, LOB_BLOCK_SIZE, *b, readBlobCallback, 0, 0);
    }
    if (checkerr(rc, "QoreOracleConnection::readBlob()", xsink))
        return nullptr;
    return b.release();
}

QoreStringNode *QoreOracleConnection::readClob(OCILobLocator *lobp, const QoreEncoding *enc, ExceptionSink *xsink) {
    // check for interrupt before LOB read
    if (qore_check_cancel(xsink)) {
        return nullptr;
    }
    void *dbuf = malloc(LOB_BLOCK_SIZE);
    ON_BLOCK_EXIT(free, dbuf);
    ub4 amt = 0;

    QoreStringNodeHolder str(new QoreStringNode(enc));
    // read LOB data in streaming callback mode with cancel helper for interruptibility
    sword rc;
    {
        QoreOracleCancelHelper cancel_helper(svchp, errhp);
        rc = OCILobRead(svchp, errhp, lobp, &amt, 1, dbuf, LOB_BLOCK_SIZE, *str, readClobCallback, (ub2)charsetid, 0);
    }
    if (checkerr(rc, "QoreOracleConnection::readClob()", xsink))
        return nullptr;
    return str.release();
}

int QoreOracleConnection::writeLob(OCILobLocator* lobp, void* bufp, oraub8 buflen, bool clob, const char* desc, ExceptionSink* xsink) {
    // check for interrupt before LOB write
    if (qore_check_cancel(xsink)) {
        return -1;
    }
#ifdef HAVE_OCILOBWRITE2
    oraub8 amtp = buflen;
    if (buflen <= LOB_BLOCK_SIZE) {
        sword rc;
        {
            QoreOracleCancelHelper cancel_helper(svchp, errhp);
            rc = OCILobWrite2(svchp, errhp, lobp, &amtp, 0, 1, bufp, buflen, OCI_ONE_PIECE, 0, 0, charsetid, SQLCS_IMPLICIT);
        }
        return checkerr(rc, desc, xsink);
    }

    // retrieve *LOB data
    void* dbuf = malloc(LOB_BLOCK_SIZE);
    ON_BLOCK_EXIT(free, dbuf);

    oraub8 offset = 0;
    while (true) {
        // check for interrupt periodically during chunked LOB write
        if (offset && qore_check_cancel(xsink)) {
            return -1;
        }
        ub1 piece;
        oraub8 len = buflen - offset;
        if (len > LOB_BLOCK_SIZE) {
            len = LOB_BLOCK_SIZE;
            piece = offset ? OCI_NEXT_PIECE : OCI_FIRST_PIECE;
            //printd(5, "QoreOracleConnection::writeLob() piece = %s\n", offset ? "OCI_NEXT_PIECE" : "OCI_FIRST_PIECE");
        }
        else {
            piece = OCI_LAST_PIECE;
            //printd(5, "QoreOracleConnection::writeLob() piece = OCI_LAST_PIECE\n");
        }

        // copy data to buffer
        memcpy(dbuf, ((char*)bufp) + offset, len);

        sword rc;
        {
            QoreOracleCancelHelper cancel_helper(svchp, errhp);
            rc = OCILobWrite2(svchp, errhp, lobp, &amtp, 0, 1, dbuf, len, piece, 0, 0, charsetid, SQLCS_IMPLICIT);
        }
        //printd(5, "QoreOracleConnection::writeLob() offset: "QLLD" len: "QLLD" amtp: "QLLD" total: "QLLD" rc: %d\n", offset, len, amtp, buflen, (int)rc);
        if (piece == OCI_LAST_PIECE) {
            if (rc != OCI_SUCCESS) {
                checkerr(rc, desc, xsink);
                return -1;
            }
            break;
        }
        if (rc != OCI_NEED_DATA) {
            checkerr(rc, desc, xsink);
            assert(*xsink);
            return -1;
        }
        offset += len;
    }
#else
    ub4 amtp = buflen;
    if (buflen <= LOB_BLOCK_SIZE) {
        sword rc;
        {
            QoreOracleCancelHelper cancel_helper(svchp, errhp);
            rc = OCILobWrite(svchp, errhp, lobp, &amtp, 1, bufp, buflen, OCI_ONE_PIECE, 0, 0, charsetid, SQLCS_IMPLICIT);
        }
        return checkerr(rc, desc, xsink);
    }

    // retrieve *LOB data
    void* dbuf = malloc(LOB_BLOCK_SIZE);
    ON_BLOCK_EXIT(free, dbuf);

    ub4 offset = 0;
    while (true) {
        // check for interrupt periodically during chunked LOB write
        if (offset && qore_check_cancel(xsink)) {
            return -1;
        }
        ub1 piece;
        ub4 len = buflen - offset;
        if (len > LOB_BLOCK_SIZE) {
            len = LOB_BLOCK_SIZE;
            piece = offset ? OCI_NEXT_PIECE : OCI_FIRST_PIECE;
            //printd(5, "QoreOracleConnection::writeLob() piece = %s\n", offset ? "OCI_NEXT_PIECE" : "OCI_FIRST_PIECE");
        }
        else {
            piece = OCI_LAST_PIECE;
            //printd(5, "QoreOracleConnection::writeLob() piece = OCI_LAST_PIECE\n");
        }

        // copy data to buffer
        memcpy(dbuf, ((char*)bufp) + offset, len);

        sword rc;
        {
            QoreOracleCancelHelper cancel_helper(svchp, errhp);
            rc = OCILobWrite(svchp, errhp, lobp, &amtp, 1, dbuf, len, piece, 0, 0, charsetid, SQLCS_IMPLICIT);
        }
        //printd(5, "QoreOracleConnection::writeLob() offset: "QLLD" len: "QLLD" amtp: "QLLD" total: "QLLD" rc: %d\n", offset, len, amtp, buflen, (int)rc);
        if (piece == OCI_LAST_PIECE) {
            if (rc != OCI_SUCCESS) {
                checkerr(rc, desc, xsink);
                return -1;
            }
            break;
        }
        if (rc != OCI_NEED_DATA) {
            checkerr(rc, desc, xsink);
            assert(*xsink);
            return -1;
        }
        offset += len;
    }
#endif
    return 0;
}
