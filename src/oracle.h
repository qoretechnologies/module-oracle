/* -*- mode: c++; indent-tabs-mode: nil -*- */
/*
  oracle.h

  Qore Programming Language

  Copyright (C) 2003 - 2022 David Nichols

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

#ifndef QORE_ORACLE_H

#define QORE_ORACLE_H

#include "config.h"
#include "oracle-config.h"
#include "oracleobject.h"
#include <qore/Qore.h>
#include <qore/QoreSandboxManager.h>

#include <atomic>
#include <vector>
#include <string>

#include <oci.h>

#include "ocilib_types.h"

//! Support defines to enumerate SQLT_NTY subtype for ORACLE_OBJECT and ORACLE_COLLECTION
#define SQLT_NTY_NONE 0
#define SQLT_NTY_OBJECT 1
#define SQLT_NTY_COLLECTION 2

#include "OraColumnValue.h"
#include "QoreOracleConnection.h"
#include "QoreOracleStatement.h"
#include "QorePreparedStatement.h"
#include "OraResultSet.h"

//! RAII helper for Oracle statement cancellation
/** Registers a cancel callback with the sandbox manager before blocking operations.
    When requestInterrupt() is called, the callback will use OCIBreak() to cancel the operation.
*/
class QoreOracleCancelHelper {
public:
    DLLLOCAL QoreOracleCancelHelper(OCISvcCtx* svchp, OCIError* errhp);
    DLLLOCAL ~QoreOracleCancelHelper();

    // Non-copyable
    QoreOracleCancelHelper(const QoreOracleCancelHelper&) = delete;
    QoreOracleCancelHelper& operator=(const QoreOracleCancelHelper&) = delete;

private:
    // Use atomic pointers for thread safety with callback invocation
    std::atomic<OCISvcCtx*> svchp;
    std::atomic<OCIError*> errhp;
    QoreSandboxManagerHelper smh;
};

#define ORACLE_OBJECT "OracleObject"
#define ORACLE_COLLECTION "OracleCollection"

#define MAXINT32 2147483647   // 2^^32 - 1

#define ORA_RAW_SIZE 65535

/* note that some operations will work with VARCHAR2 values up to 32767 bytes long, however
   others will fail - basically VARCHAR2 columns cannot hold values > 4000 bytes and anything
   else should be a CLOB in order to make everything work best
*/
#define LOB_THRESHOLD 4000

// with 10g on Linux the streaming *lob callback function would
// never get more than 1024 bytes of data at a time, however with a 9i
// server and client on Solaris, it would not work unless my
// buffer size was at least twice as big as my CLOB!
#ifdef NEED_ORACLE_LOB_WORKAROUND
#define LOB_BLOCK_SIZE 128*1024
#else
#define LOB_BLOCK_SIZE 16384
#endif

// timestamp binding type
#ifdef _QORE_HAS_TIME_ZONES
// use timestamp with time zone if qore supports time zones
#define QORE_SQLT_TIMESTAMP SQLT_TIMESTAMP_TZ
#define QORE_DTYPE_TIMESTAMP  OCI_DTYPE_TIMESTAMP_TZ
#else
#define QORE_SQLT_TIMESTAMP SQLT_TIMESTAMP
#define QORE_DTYPE_TIMESTAMP  OCI_DTYPE_TIMESTAMP
#endif

// maximum string size for an oracle number
#define ORACLE_NUMBER_STR_LEN 127

#endif
