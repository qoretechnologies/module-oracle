/*
   +----------------------------------------------------------------------+
   |                                                                      |
   |                     OCILIB - C Driver for Oracle                     |
   |                                                                      |
   |                      (C Wrapper for Oracle OCI)                      |
   |                                                                      |
   +----------------------------------------------------------------------+
   |                      Website : http://www.ocilib.net                 |
   +----------------------------------------------------------------------+
   |               Copyright (c) 2007-2010 Vincent ROGIER                 |
   +----------------------------------------------------------------------+
   | This library is free software; you can redistribute it and/or        |
   | modify it under the terms of the GNU Lesser General Public           |
   | License as published by the Free Software Foundation; either         |
   | version 2 of the License, or (at your option) any later version.     |
   |                                                                      |
   | This library is distributed in the hope that it will be useful,      |
   | but WITHOUT ANY WARRANTY; without even the implied warranty of       |
   | MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU    |
   | Lesser General Public License for more details.                      |
   |                                                                      |
   | You should have received a copy of the GNU Lesser General Public     |
   | License along with this library; if not, write to the Free           |
   | Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.   |
   +----------------------------------------------------------------------+
   |          Author: Vincent ROGIER <vince.rogier@ocilib.net>            |
   +----------------------------------------------------------------------+
*/

/* ------------------------------------------------------------------------ *
 * $Id: ocilib_types.h, v 3.7.0 2010-07-20 17:45 Vincent Rogier $
 * ------------------------------------------------------------------------ */


#ifndef OCILIB_OCILIB_TYPES_H_INCLUDED
#define OCILIB_OCILIB_TYPES_H_INCLUDED

#include "ocilib_defs.h"

#include <qore/Qore.h>

#define NUM_FMT "9999999999999999999999999999999999.99999999999999999999999"
#define NUM_FMT_LEN (sizeof(NUM_FMT)-1)

/* ************************************************************************ *
 *                             PRIVATE TYPES
 * ************************************************************************ */

/*
 * OCI_Item : Internal list entry.
 *
 * The library needs to manage internal list of objects in order to be able to
 * free them if the application doest not.
 *
 * @note
 * Internal lists are using mutexes for resource locking in multithreaded
 * environments
 *
 */

struct OCI_Item
{
   void             *data; /* pointer to external data */
   struct OCI_Item  *next; /* next element in list */
};

typedef struct OCI_Item OCI_Item;

/*
 * OCI_List : Internal list object.
 *
 * The OCI_List object is used to maintain a collection of handles allocated
 * by user programs.
 *
 * Those handles are freed when the collection owner is destroyed.
 * So, we make sure that if OCI_Cleanup() is called, all allocated handles will
 * be destroyed even if the program does not free them.
 *
 */

struct OCI_List
{
   OCI_Item    *head;   /* pointer to first item */
   QoreThreadLock mutex;  // mutex
   ub4          count;  /* number of elements in list */
   int          type;   /* type of list item */

   DLLLOCAL OCI_List(int t) : head(0), count(0), type(t) {
   }
};

typedef struct OCI_List OCI_List;

/*
 * Server output object used to retrieve server dbms.output buffers
 *
 */

struct OCI_ServerOutput
{
    ub1                *arrbuf;    /* array buffer */
    unsigned int        arrsize;   /* array size */
    unsigned int        cursize;   /* number of filled items in the array */
    unsigned int        curpos;    /* current position in the array */
    unsigned int        lnsize;    /* line size */
    OCI_Statement      *stmt;      /* pointer to statement object (dbms_output calls) */
};

typedef struct OCI_ServerOutput OCI_ServerOutput;

/*
 * Connection trace information
 *
 */

struct OCI_TraceInfo
{
    mtext identifier[OCI_SIZE_TRACE_ID+1];
    mtext module[OCI_SIZE_TRACE_MODULE+1];
    mtext action[OCI_SIZE_TRACE_ACTION+1];
    mtext info[OCI_SIZE_TRACE_INF0+1];
};

typedef struct OCI_TraceInfo OCI_TraceInfo;

/* ************************************************************************ *
 *                             PUBLIC TYPES
 * ************************************************************************ */

struct OCI_Error
{
    boolean          raise;                   /* Error flag */
    boolean          active;                  /* to avoid recursive exceptions */
    OCI_Connection  *con;                     /* pointer to connection object */
    OCI_Statement   *stmt;                    /* pointer to statement object */
    sb4              ocode;                   /* Oracle OCI error code */
    int              icode;                   /* OCILIB internal error code */
    mtext            str[OCI_ERR_MSG_SIZE+1]; /* error message */
    unsigned int     type;                    /* OCILIB error type */
    ub4              row;                     /* Error row offset (array DML) */
    boolean          warning;                 /* is it a warning */
};

/*
 * Mutex object
 *
 * Mutexes have their own error handle to avoid conflict using OCIErrorGet()
 * from different threads
 *
 */

struct OCI_Mutex
{
    OCIThreadMutex  *handle;  /* OCI Mutex handle */
    OCIError        *err;     /* OCI Error handle */
};

/*
 * Thread object
 *
 * Threads have their own error handle to avoid conflict using OCIErrorGet()
 *
 */

struct OCI_Thread
{
    OCIThreadHandle *handle;    /* OCI Thread handle */
    OCIThreadId     *id;        /* OCI Thread ID */
    OCIError        *err;       /* OCI Error handle */
    void            *arg;       /* thread routine argument */
    POCI_THREAD      proc;      /* thread routine */
};

/*
 * Thread key object
 *
 * Thread keys have their own error handle to avoid conflict using OCIErrorGet()
 * from differents threads
 *
 */

struct OCI_ThreadKey
{
    OCIThreadKey    *handle;  /* OCI Thread key handle */
    OCIError        *err;     /* OCI Error handle */
};

typedef struct OCI_ThreadKey OCI_ThreadKey;

/*
 * OCI_Library : Internal OCILIB library encapsulation.
 *
 * It's a static, local and unique object that collects all the global variables
 * needed by the library
 *
 */

struct OCI_Library
{
    OCI_List       *cons;                   /* list of connection objects */
    OCI_List       *pools;                  /* list of pools objects */
    OCI_List       *subs;                   /* list of subscription objects */
    OCI_List       *arrs;                   /* list of arrays objects */
    OCIEnv         *env;                    /* OCI environment handle */
    OCIError       *err;                    /* OCI error handle */
    POCI_ERROR      error_handler;          /* user defined error handler */
    unsigned int    version_compile;        /* OCI version used at compile time */
    unsigned int    version_runtime;        /* OCI version used at runtime */
    boolean         use_lob_ub8;            /* use 64 bits integers for lobs ? */
    boolean         use_scrollable_cursors; /* use Oracle 9i fetch API */
    ub4             env_mode;               /* default environment mode */
    boolean         loaded;                 /* OCILIB correctly loaded ? */
    boolean         nls_utf8;               /* is UFT8 enabled for data strings ? */
    boolean         warnings_on;            /* warnings enabled ? */
    OCI_Error       lib_err;                /* Error used when OCILIB is not loaded */
    OCI_HashTable  *key_map;                /* hash table for mapping name/key */
    OCI_ThreadKey  *key_errs;               /* Thread key to store thread errors */
    unsigned int    nb_hndlp;               /* number of OCI handles allocated */
    unsigned int    nb_descp;               /* number of OCI descriptors allocated */
    unsigned int    nb_objinst;             /* number of OCI objects allocated */
    OCI_HashTable  *sql_funcs;              /* hash table handle for sql function names */
#ifdef OCI_IMPORT_RUNTIME
    LIB_HANDLE      lib_handle;             /* handle of runtime shared library */
#endif
};

typedef struct OCI_Library OCI_Library;

/*
 * Pool object
 *
 */

struct OCI_Pool
{
    OCI_List        *cons;      /* list of connection objects */
    void           *handle;     /* OCI pool handle */
    void           *authp;      /* OCI authentification handle */
    OCIError        *err;       /* OCI context handle */
    mtext           *name;      /* pool name */
    mtext           *db;        /* database */
    mtext           *user;      /* user */
    mtext           *pwd;       /* password */
    OCI_Mutex       *mutex;     /* mutex handle */
    ub4              mode;      /* session mode */
    ub4              min;       /* minimum of objects */
    ub4              max;       /* maximum of objects */
    ub4              incr;      /* increment step of objects */
    unsigned int     nb_busy;   /* number of busy objects */
    unsigned int     nb_opened; /* number of opened objects */
    unsigned int     timeout;   /* connection idle timeout */
    boolean          nowait;    /* wait to retrieve object from pool ? */
    ub4              htype;     /* handle type of pool : connection / session */
};

/*
 * Connection object
 *
 */

struct OCI_Connection
{
    mtext            *db;           /* database */
    mtext            *user;         /* user */
    mtext            *pwd;          /* password */
    OCI_List         *stmts;        /* list of statements */
    OCI_List         *trsns;        /* list of transactions */
    OCI_List         *tinfs;        /* list of type info objects */
    OCI_Transaction  *trs;          /* pointer to current transaction object */
    OCI_Pool         *pool;         /* pointer to parent pool object */
    OCI_ServerOutput *svopt;        /* Pointer to server output object */
    OCIServer        *svr;          /* OCI server handle */
    OCIError         *err;          /* OCI context handle */
    OCIEnv           *env;          /* OCI environment handle */
    OCISession       *ses;          /* OCI session handle */
    OCISvcCtx        *cxt;          /* OCI context handle */
    boolean           autocom;      /* auto commit mode */
    unsigned int      nb_files;     /* number of OCI_File opened by the connection */
    boolean           alloc_handles;/* do new need to allocate OCI handles ? */
    unsigned int      mode;         /* session mode */
    int               cstate;       /* connection state */
    void             *usrdata;      /* user data */
    mtext            *fmt_date;     /* date string format for conversion */
    mtext            *fmt_num;      /* numeric string format for conversion */
    mtext            *ver_str;      /* string  server version*/
    unsigned int      ver_num;      /* numeric server version */
    OCI_TraceInfo    *trace;        /* trace information */
    unsigned int      type_depth;   /* the number of type descriptions in progress */
    mtext            *sess_tag;     /* session tag */
    POCI_TAF_HANDLER  taf_handler;  /* failover call back */
    mtext            *db_name;      /* session tag */
    mtext            *inst_name;    /* instance name */
    mtext            *service_name; /* server service name */
    mtext            *server_name;  /* server name (hostname) */
    mtext            *domain_name; /* server domain name */
    OCI_Timestamp    *inst_startup;/* instance startup timestamp */
};

/*
 * Transaction object
 *
 */

struct OCI_Transaction
{
    OCI_Connection  *con;       /* pointer to connection object */
    OCITrans        *htr;       /* OCI transaction handle */
    unsigned int     timeout;   /* timeout */
    unsigned int     mode;      /* transaction mode */
    boolean          local;     /* is local transaction ? */
    OCI_XID          xid;       /* global transaction identifier */
};

/*
 * Column object
 *
 */

struct OCI_Column
{
    /* 0racle infos */
    ub2              ocode;     /* Oracle SQL code */
    ub2              tcode;     /* Oracle type code */
    ub2              icode;     /* Internal translated Oracle SQL code */
    ub2              size;      /* SQL Size */
    sb2              prec;      /* SQL precision 1 (number prec, leading prec) */
    sb2              prec2;     /* SQL precision 2 (fractional prec) */
    sb1              scale;     /* SQL scale */
    ub1              type;      /* internal datatype */
    ub1              null;      /* is nullable */
    ub1              charused;  /* is column size expressed in characters */
    mtext           *name;      /* column name */
    ub2              charsize;  /* SQL Size in character */
    ub1              csfrm;     /* charset form */
    ub1              dtype;     /* oracle handle type */

    /* OCILIB infos */
    ub4              bufsize;   /* element size */
    OCI_TypeInfo    *typinf;    /* user type descriptor */
    ub4              subtype;   /* object type */
};

/*
 * OCI_Buffer : Internal input/output buffer
 *
 */

struct OCI_Buffer
{
    void            *handle;   /* OCI handle (bind or define) */
    void           **data;     /* data / array of data */
    void            *inds;     /* array of indicators */
    void            *lens;     /* array of lengths */
    dtext           *tmpbuf;   /* temporary buffer for string conversion */
    unsigned int     tmpsize;  /* size of temporary buffer */
    ub4              count;    /* number of elements in the buffer */
    int              sizelen;  /* size of an element in the lens array */
    void           **obj_inds; /* array of indicators structure object */
};

typedef struct OCI_Buffer OCI_Buffer;

/*
 * OCI_Bind object
 *
 */

struct OCI_Bind
{
    OCI_Statement   *stmt;      /* pointer to statement object */
    void           **input;     /* input values */
    mtext           *name;      /* name of the bind */
    sb4              size;      /* data size */
    OCI_Buffer       buf;       /* place holder */
    ub2              dynpos;    /* index of the bind for dynamic binds */
    ub2             *plrcds;    /* PL/SQL tables return codes */
    ub4              nbelem;    /* PL/SQL tables number of elements */
    OCI_TypeInfo    *typinf;    /* for object, collection and ref */
    ub1              type;      /* internal datatype */
    ub1              subtype;   /* internal subtype */
    ub2              code;      /* SQL datatype code */
    boolean          is_array;  /* is it an array bind ? */
    ub1              alloc;     /* is buffer allocated or mapped to input */
    ub1              csfrm;     /* charset form */
}
;

/*
 * OCI_Define : Internal Resultset column data implementation
 *
 */

struct OCI_Define
{
    OCI_Resultset   *rs;           /* pointer to resultset object */
    void            *obj;          /* current OCILIB object instance */
    OCI_Column       col;          /* column object */
    OCI_Buffer       buf;          /* placeholder */
};

typedef struct OCI_Define OCI_Define;

/*
 * Resultset object
 *
 */

struct OCI_Resultset
{
    OCI_Statement   *stmt;          /* pointer to statement object */
    OCI_HashTable   *map;           /* hash table handle for mapping name/index */
    OCI_Define      *defs;          /* array of define objects */
    ub4              nb_defs;       /* number of elements */
    ub4              row_cur;       /* actual position in the array of rows */
    ub4              row_abs;       /* absolute position in the resultset */
    ub4              row_count;     /* number of rows fetched so far */
    ub4              row_fetched;   /* rows fetched by last call (scrollable) */
    boolean          eof;           /* end of resultset reached ?  */
    boolean          bof;           /* beginning of resultset reached ?  */
    ub4              fetch_size;    /* internal array size */
    sword            fetch_status;  /* internal fetch status */
};

/*
 * OCI_Define : Internal Resultset column data implementation
 *
 */

struct OCI_BatchErrors
{
    OCI_Error       *errs;         /* sub array of OCILIB errors(array DML) */
    ub4              cur;          /* current sub error index (array DML) */
    ub4              count;        /* number of errors (array DML) */
};

typedef struct OCI_BatchErrors OCI_BatchErrors;

/*
 * Statement object
 *
 */

struct OCI_Statement
{
    OCIStmt         *stmt;              /* OCI statement handle */
    ub4              hstate;            /* object variable state */
    OCI_Resultset  **rsts;              /* pointer to resultset list */
    OCI_Connection  *con;               /* pointer to connection object */
    mtext           *sql;               /* SQL statement */
    OCI_Bind       **ubinds;            /* array of user bind objects */
    OCI_Bind       **rbinds;            /* array of register bind objects */
    OCI_HashTable   *map;               /* hash table handle for mapping bind name/index */
    ub2              nb_ubinds;         /* number of elements in the bind array */
    ub2              nb_rbinds;         /* number of output binds */
    boolean          bind_reuse;        /* rebind data allowed ? */
    unsigned int     bind_mode;         /* type of binding */
    unsigned int     bind_alloc_mode;   /* type of bind allocation */
    ub4              exec_mode;         /* type of execution */
    ub4              fetch_size;        /* fetch array size */
    ub4              prefetch_size;     /* pre-fetch size */
    ub4              prefetch_mem;      /* pre-fetch memory */
    ub4              long_size;         /* default size for LONG columns */
    ub1              long_mode;         /* LONG datatype handling mode */
    ub1              status;            /* statement status */
    ub2              type;              /* type of SQL statement */
    ub4              nb_iters;          /* current number of iterations for execution */
    ub4              nb_iters_init;     /* initial number of iterations for execution */
    ub4              nb_rs;             /* number of resultsets */
    ub2              cur_rs;            /* index of the current resultset */
    ub2              dynidx;            /* bind index counter for dynamic exec */
    ub2              err_pos;           /* error position in sql statement */
    boolean          bind_array;        /* has array binds ? */
    OCI_BatchErrors *batch;             /* error handling for array DML */
};

/*
 * Internal Large object
 *
 */

struct OCI_Lob
{
    OCILobLocator   *handle;        /* OCI handle */
    ub4              hstate;        /* object variable state */
    OCI_Connection  *con;           /* pointer to connection object */
    ub4              type;          /* type of lob */
    big_uint         offset;        /* current offset for R/W */
};

/*
 * External Large object
 *
 */

struct OCI_File
{
    OCILobLocator   *handle;    /* OCI handle */
    ub4              hstate;    /* object variable state */
    OCI_Connection  *con;       /* pointer to connection object */
    mtext           *dir;       /* directory name */
    mtext           *name;      /* file name */
    ub4              type;      /* type of file */
    big_uint         offset;    /* current offset for read */
};

/*
 * Long object
 *
 */

struct OCI_Long
{
    OCI_Statement   *stmt;      /* pointer to statement object */
    ub4              hstate;    /* object variable state */
    OCI_Define      *def;       /* pointer to resultset define object */
    ub4              size;      /* size of the buffer read / written */
    unsigned int     type;      /* type of long */
    ub4              offset;    /* current offset for R/W */
    ub4              piecesize; /* size of current fetched piece */
    ub4              maxsize;   /* current offset for R/W */
    ub1             *buffer;    /* fetched buffer */
};

/*
 * Date object
 *
 */

struct OCI_Date
{
    OCIDate         *handle;    /* OCI handle */
    ub4              hstate;    /* object variable state */
    OCI_Connection  *con;       /* pointer to connection object */
    OCIError        *err;       /* OCI context handle */
    ub4              allocated; /* is handle allocated ? */
};

/*
 * Timestamp object
 *
 */

struct OCI_Timestamp
{
#if OCI_VERSION_COMPILE >= OCI_9_0
    OCIDateTime     *handle;    /* OCI handle */
#else
    void            *handle;    /* fake handle for alignment */
#endif
    ub4              hstate;    /* object variable state */
    OCI_Connection  *con;       /* pointer to connection object */
    OCIError        *err;       /* OCI context handle */
    ub4              type;      /* sub type */
};

/*
 * Interval object
 *
 */

struct OCI_Interval
{
#if OCI_VERSION_COMPILE >= OCI_9_0
    OCIInterval     *handle;    /* OCI handle */
#else
    void            *handle;    /* fake handle for alignment */
#endif
    ub4              hstate;    /* object variable state */
    OCI_Connection  *con;       /* pointer to connection object */
    OCIError        *err;       /* OCI context handle */
    ub4              type;      /* sub type */
};

/*
 * Oracle Named type object
 *
 */

struct OCI_Object
{
    void             *handle;   /* OCI handle */
    ub4               hstate;   /* object variable state */
    OCI_Connection   *con;      /* pointer to connection object */
    OCI_TypeInfo     *typinf;   /* pointer to type info object */
    void            **objs;     /* array of OCILIB sub objects */
    void             *buf;      /* buffer to store converted out string attribute */
    int               buflen;   /* buffer length */
    sb2              *tab_ind;  /* indicators for root instance */
    ub2               idx_ind;  /* instance indicator offset / indicator table */
    OCIObjectLifetime type;     /* object type */
};

/*
 * Oracle Collection Item object
 *
 */

struct OCI_Elem
{
    void            *handle;   /* OCI handle */
    ub4              hstate;   /* object variable state */
    OCI_Connection  *con;      /* pointer to connection object */
    void            *obj;      /* OCILIB sub object */
    void            *buf;      /* buffer to store converted out string attribute */
    int              buflen;   /* buffer length */
    boolean          init;     /* underlying object has been initialized ? */
    OCI_TypeInfo    *typinf;   /* object type information */
    OCIInd          *pind;     /* indicator  pointer */
    OCIInd           ind;      /* internal temporary data state indicator */
};

/*
 * Oracle Collection object
 *
 */

struct OCI_Coll
{
    OCIColl           *handle;   /* OCI handle */
    ub4                hstate;   /* object variable state */
    OCI_Connection    *con;      /* pointer to connection object */
    OCI_TypeInfo      *typinf;   /* pointer to type info object */
    OCI_Elem          *elem;     /* item object */
    sb2              *tab_ind;  /* indicators for root instance */
};

/*
 * Oracle Iterator object
 *
 */

struct OCI_Iter
{
    OCIIter           *handle;   /* OCI handle */
    OCI_Coll          *coll;     /* pointer to connection object */
    OCI_Elem          *elem;     /* item object */
    boolean            eoc;      /* end of collection */
    boolean            boc;      /* beginning of collection */
};

/*
 * Oracle REF object
 *
 */

struct OCI_Ref
{
    OCIRef            *handle;   /* OCI handle */
    ub4                hstate;   /* object variable state */
    OCI_Connection    *con;      /* pointer to connection object */
    OCI_TypeInfo      *typinf;   /* pointer to type info object */
    OCI_Object        *obj;      /* Pinned object */
    boolean            pinned;   /* is the reference pinned */
};

/*
 * Type info object
 *
 */

struct OCI_TypeInfo
{
    OCI_Connection  *con;        /* pointer to connection object */
    mtext           *name;       /* name of the type info object */
    mtext           *schema;     /* owner of the type info object */
    unsigned int     type;       /* type of type info handle */
    OCIType         *tdo;        /* datatype object type */
    ub2              tcode;      /* Oracle type code */
    ub2              ccode;      /* Oracle collection code */
    OCI_Column      *cols;       /* array of column datatype info */
    ub2              nb_cols;    /* number of columns */
    ub2              refcount;   /* reference counter */
    int             *offsets;    /* cached offsets */
    size_t           struct_size;/* cached structure size */
    unsigned int     depth;      /* levels of objects and collections, including this one; 0 until it is checked */
};

/*
 * OCI_DirPathColumn : Internal Direct Path column object
 *
 */

struct OCI_DirPathColumn
{
    ub4              format_size; /* size of the column format */
    mtext           *format;      /* date or numeric format */
    ub2              maxsize;     /* input max size */
    ub2              type;        /* column type */
    ub2              sqlcode;     /* sql type */
    ub1             *data;        /* array of data */
    ub1             *flags;       /* array of row flags */
    ub4             *lens;        /* array of lengths */
    ub2              bufsize;     /* buffer size */
    ub2              index;       /* ref index in the type info columns list */
};

typedef struct OCI_DirPathColumn OCI_DirPathColumn;

/*
 * Oracle Direct Path column object
 *
 */

struct OCI_DirPath
{
    OCI_Connection      *con;       /* pointer to connection object */
    OCI_TypeInfo        *typinf;    /* type info about table to load */
    OCIDirPathCtx       *ctx;       /* OCI DP context handle */
    OCIDirPathColArray  *arr;       /* OCI DP column array handle */
    OCIDirPathStream    *strm;      /* OCI DP stream handle */
    OCI_DirPathColumn   *cols;      /* array of column info */
    ub4                  nb_prcsd;  /* number of row processed at last call */
    ub4                  nb_loaded; /* number of row loaded so far */
    ub4                  status;    /* internal status */
    ub4                  err_row;   /* index of the row not processed at last call */
    ub4                  nb_cur;    /* current number of row to load per stream */
    ub2                  err_col;   /* index of the column not processed at last call */
    ub2                  nb_cols;   /* number of columns to load */
    ub2                  nb_rows;   /* maximum number of row to load per stream */
};

/*
 * Oracle Event object
 *
 */

struct OCI_Event
{
    OCI_Subscription    *sub;           /* OCILIB subcription handle */
    unsigned int         objname_size;  /* cached size of altered object name */
    unsigned int         rowid_size;    /* cached size of altered object row id */
    unsigned int         dbname_size;   /* cached size of the database name */
    unsigned int         type;          /* event type */
    ub4                  op;            /* event object operation */
    dtext               *objname;       /* altered object name */
    dtext               *rowid;         /* altered row id */
    dtext               *dbname;        /* database name */
};

/*
 * Oracle Notification object
 *
 */

struct OCI_Subscription
{
    OCI_Connection      *con;        /* OCILIB connection handle */
    OCISubscription     *subhp;      /* OCI subscription handle */
    OCIError            *err;        /* OCI error handle  */
    mtext               *name;       /* notification name */
    unsigned int         type;       /* notification type */
    POCI_NOTIFY          handler;    /* user callback */
    ub4                  timeout;    /* notification timetout */
    ub4                  port;       /* port to use  */
    mtext               *saved_db;   /* database for reconnection if needed */
    mtext               *saved_user; /* user for reconnection if needed */
    mtext               *saved_pwd;  /* password for reconnection if needed */
    OCI_Event            event;      /* event object for user callback */
};

/*
 * Oracle A/Q Agent
 *
 */

struct OCI_Agent
{
    OCIAQAgent     *handle;        /* OCI agent handle */
    ub4             hstate;        /* object variable state */
    OCI_Connection *con;           /* OCILIB connection handle */
    mtext          *address;       /* agent address */
    mtext          *name;          /* agent name */
};

/*
 * Oracle A/Q message
 *
 */

struct OCI_Msg
{
    OCI_TypeInfo       *typinf;        /* pointer to type info object */
    OCIAQMsgProperties *proph;         /* OCI message properties handle */
    void               *payload;       /* message payload (object handle or raw handle) */
    OCIRaw             *id;            /* message identitier */
    OCI_Date           *date;          /* enqueue date */
    mtext              *correlation;   /* correlation string */
    mtext              *except_queue;  /* exception queue name */
    OCI_Agent          *sender;        /* sender */
    OCI_Object         *obj;           /* OCILIB object handle for object payloads */
    OCIInd              ind;           /* message payload indicator pointer */
};

/*
 * Oracle A/Q enqueue
 *
 */

struct OCI_Enqueue
{
    OCI_TypeInfo    *typinf;         /* pointer to type info object */
    OCIAQEnqOptions *opth;           /* OCI enqueue options handle */
    mtext           *name;           /* queue name */
};

/*
 * Oracle A/Q Dequeue
 *
 */

struct OCI_Dequeue
{
    OCI_TypeInfo        *typinf;         /* pointer to type info object */
    OCIAQDeqOptions     *opth;           /* OCI dequeue options handle */
    mtext               *name;           /* queue name */
    mtext               *pattern;        /* queue name */
    mtext               *consumer;       /* consumer name */
    OCI_Msg             *msg;            /* message retrieved from queue */
    OCIAQAgent         **agent_list;     /* array of agents objects */
    ub4                  agent_count;    /* number of agents objects */
    OCI_Agent           *agent;          /* pointer to agent object for listen call */
    POCI_NOTIFY_AQ       callback;       /* user callback */
    OCISubscription     *subhp;          /* AQ subscription for async dequeueing */
    void                *user_ctx;       /* user context pointer */
};


/*
 * oCILIB array
 *
 */

struct OCI_Array
{
    OCI_Connection *con;
    unsigned int elem_type;
    unsigned int elem_subtype;
    unsigned int elem_size;
    unsigned int nb_elem;
    unsigned int struct_size;
    unsigned int handle_type;
    void  ** tab_obj;
    void   * mem_handle;
    void   * mem_struct;
 
};

typedef struct OCI_Array OCI_Array;


/*
 * Hash table object
 *
 */

struct OCI_HashTable
{
    OCI_HashEntry    **items;     /* array of slots */
    unsigned int       size;      /* size of the slots array */
    unsigned int       count;     /* number of used slots */
    unsigned int       type;      /* type of data */
};

/*
 * OCI_Datatype : fake dummy structure for casting object with
 * handles for more compact code
 *
 */

 struct OCI_Datatype
{
    void *handle;   /* OCI handle */
    ub4   hstate;   /* object variable state */
};

typedef struct OCI_Datatype OCI_Datatype;


/*
 * OCI_SQLCmdInfo : Oracle SQL commands code and verbs
 *
 */

 struct OCI_SQLCmdInfo
{
    unsigned int code; /* SQL command code */
    const mtext *verb;       /* SQL command verb */
};

typedef struct OCI_SQLCmdInfo OCI_SQLCmdInfo;

/* static and unique OCI_Library object */

//extern OCI_Library OCILib;
extern OCI_SQLCmdInfo SQLCmds[];

//#ifdef __cplusplus
//extern "C" {
//#endif
#if 0
}
#endif

OCI_EXPORT OCI_Error * OCI_API OCI_GetLastError2(OCI_Library *pOCILib);
OCI_EXPORT boolean OCI_API OCI_Initialize2(OCI_Library *pOCILib, OCIEnv * d_ora_env, OCIError *errhp, POCI_ERROR err_handler, const mtext *lib_path, unsigned int mode);
OCI_EXPORT boolean OCI_API OCI_Cleanup2(OCI_Library *pOCILib, ExceptionSink* xsink);
OCI_EXPORT OCI_TypeInfo * OCI_API OCI_TypeInfoGet2(OCI_Library *pOCILib, OCI_Connection *con, const mtext *name, unsigned int type, ExceptionSink* xsink);
// returns the description of the object type with exactly the given schema and type names (as Oracle stores them;
// not parsed, and their case is kept; schema_len can be 0 for the schema of the connection); the names need not be
// terminated; each is at most OCI_SIZE_OBJ_NAME characters, or an exception is raised
OCI_EXPORT OCI_TypeInfo * OCI_API OCI_TypeInfoGetExact2(OCI_Library *pOCILib, OCI_Connection *con, const mtext *schema, size_t schema_len, const mtext *name, size_t name_len, ExceptionSink* xsink);
//OCI_EXPORT boolean OCI_API OCI_ObjectSetRef2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, OCI_Ref *value);
//OCI_EXPORT boolean OCI_API OCI_ObjectGetSelfRef2(OCI_Library *pOCILib, OCI_Object *obj, OCI_Ref *ref);
OCI_EXPORT boolean OCI_API OCI_ObjectSetObject2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, OCI_Object *value, ExceptionSink* xsink);
OCI_EXPORT boolean OCI_API OCI_ObjectSetColl2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, OCI_Coll *value, ExceptionSink* xsink);
OCI_EXPORT boolean OCI_API OCI_ObjectSetInterval2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, OCI_Interval *value, ExceptionSink* xsink);
OCI_EXPORT OCI_Object * OCI_API OCI_ObjectCreate2(OCI_Library *pOCILib, OCI_Connection *con, OCI_TypeInfo *typinf, ExceptionSink* xsink);
OCI_EXPORT OCI_Object * OCI_API OCI_ObjectGetObject2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, ExceptionSink* xsink);
OCI_EXPORT boolean OCI_API OCI_ObjectFree2(OCI_Library *pOCILib, OCI_Object *obj, ExceptionSink* xsink);
OCI_EXPORT boolean OCI_API OCI_ObjectAssign2(OCI_Library *pOCILib, OCI_Object *obj, OCI_Object *obj_src, ExceptionSink* xsink);
//OCI_EXPORT int OCI_API OCI_ObjectGetRaw2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, void *buffer, unsigned int len);
//OCI_EXPORT boolean OCI_API OCI_ObjectSetRaw2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, void* value, unsigned int len);
OCI_EXPORT boolean OCI_API OCI_ObjectSetTimestamp2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, OCI_Timestamp *value, ExceptionSink* xsink);
OCI_EXPORT OCI_Object * OCI_API OCI_ElemGetObject2(OCI_Library * pOCILib, OCI_Elem *elem, ExceptionSink* xsink);
OCI_EXPORT boolean OCI_API OCI_ElemSetObject2(OCI_Library *pOCILib, OCI_Elem *elem, OCI_Object *value, ExceptionSink* xsink);
//OCI_EXPORT unsigned int OCI_API OCI_ElemGetRaw2(OCI_Library *pOCILib, OCI_Elem *elem, void *value, unsigned int len);
//OCI_EXPORT boolean OCI_API OCI_ElemSetRaw2(OCI_Library *pOCILib, OCI_Elem *elem, void* value, unsigned int len, ExceptionSink* xsink);
OCI_EXPORT const dtext * OCI_API OCI_ElemGetString2(OCI_Library *pOCILib, OCI_Elem *elem);
OCI_EXPORT boolean OCI_API OCI_ElemSetString2(OCI_Library *pOCILib, OCI_Elem *elem, const dtext *value, ExceptionSink* xsink);
OCI_EXPORT const dtext * OCI_API OCI_ObjectGetString2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, ExceptionSink* xsink);
OCI_EXPORT boolean OCI_API OCI_ObjectSetString2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, const dtext *value, ExceptionSink* xsink);
OCI_EXPORT OCI_Lob * OCI_API OCI_ObjectGetLob2(OCI_Library * pOCILib, OCI_Object *obj, const mtext *attr, ExceptionSink* xsink);
OCI_EXPORT OCI_Lob * OCI_API OCI_LobCreate2(OCI_Library *pOCILib, OCI_Connection *con, unsigned int type, ExceptionSink* xsink);
OCI_EXPORT OCI_Lob * OCI_API OCI_ElemGetLob2(OCI_Library *pOCILib, OCI_Elem *elem, ExceptionSink* xsink);
OCI_EXPORT boolean OCI_API OCI_ElemSetLob2(OCI_Library *pOCILib, OCI_Elem *elem, OCI_Lob *value, ExceptionSink* xsink);
OCI_EXPORT OCI_Coll * OCI_API OCI_CollCreate2(OCI_Library *pOCILib, OCI_TypeInfo *typinf, ExceptionSink* xsink);
OCI_EXPORT OCI_Coll * OCI_API OCI_ObjectGetColl2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, ExceptionSink* xsink);
OCI_EXPORT OCI_Coll * OCI_API OCI_ElemGetColl2(OCI_Library *pOCILib, OCI_Elem *elem, ExceptionSink* xsink);
OCI_EXPORT boolean OCI_API OCI_ElemSetColl2(OCI_Library *pOCILib, OCI_Elem *elem, OCI_Coll *value, ExceptionSink* xsink);
OCI_EXPORT boolean OCI_API OCI_CollFree2(OCI_Library *pOCILib, OCI_Coll *coll, ExceptionSink* xsink);
OCI_EXPORT OCI_Elem * OCI_API OCI_CollGetAt2(OCI_Library *pOCILib, OCI_Coll *coll, unsigned int index, ExceptionSink* xsink);
OCI_EXPORT boolean OCI_API OCI_CollAppend2(OCI_Library *pOCILib, OCI_Coll *coll, OCI_Elem *elem, ExceptionSink* xsink);
OCI_EXPORT OCI_Timestamp * OCI_API OCI_TimestampCreate2(OCI_Library *pOCILib, OCI_Connection *con, unsigned int type, ExceptionSink* xsink);
OCI_EXPORT boolean OCI_API OCI_ElemSetTimestamp2(OCI_Library *pOCILib, OCI_Elem *elem, OCI_Timestamp *value, ExceptionSink* xsink);
OCI_EXPORT boolean OCI_API OCI_TimestampAssign2(OCI_Library *pOCILib, OCI_Timestamp *tmsp, OCI_Timestamp *tmsp_src, ExceptionSink* xsink);
OCI_EXPORT boolean OCI_API OCI_TimestampConstruct2(OCI_Library *pOCILib, OCI_Timestamp *tmsp, int year,int month, int day, int hour,  int min, int sec, int fsec, const mtext *timezone, ExceptionSink* xsink);
OCI_EXPORT OCI_Timestamp * OCI_API  OCI_ElemGetTimestamp2(OCI_Library *pOCILib, OCI_Elem *elem);
OCI_EXPORT OCI_Timestamp * OCI_API OCI_ObjectGetTimestamp2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, ExceptionSink* xsink);
OCI_EXPORT OCI_Interval * OCI_API OCI_IntervalCreate2(OCI_Library *pOCILib, OCI_Connection *con, unsigned int type, ExceptionSink* xsink);
OCI_EXPORT boolean OCI_API OCI_IntervalAssign2(OCI_Library *pOCILib, OCI_Interval *itv,  OCI_Interval *itv_src, ExceptionSink* xsink);
OCI_EXPORT boolean OCI_API OCI_IntervalGetDaySecond2(OCI_Library *pOCILib, OCI_Interval *itv, int *day, int *hour, int *min, int *sec, int *fsec, ExceptionSink* xsink);
OCI_EXPORT boolean OCI_API OCI_IntervalGetYearMonth2(OCI_Library *pOCILib, OCI_Interval *itv, int *year, int *month, ExceptionSink* xsink);
OCI_EXPORT boolean OCI_API OCI_IntervalSetDaySecond2(OCI_Library *pOCILib, OCI_Interval *itv, int day,int hour, int min, int sec, int fsec, ExceptionSink* xsink);
OCI_EXPORT boolean OCI_API OCI_IntervalSetYearMonth2(OCI_Library *pOCILib, OCI_Interval *itv, int year, int month, ExceptionSink* xsink);
OCI_EXPORT OCI_Interval * OCI_API OCI_ObjectGetInterval2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, ExceptionSink* xsink);
OCI_EXPORT OCI_Interval * OCI_API OCI_ElemGetInterval2(OCI_Library *pOCILib, OCI_Elem *elem);
OCI_EXPORT boolean OCI_API OCI_ElemSetInterval2(OCI_Library *pOCILib, OCI_Elem *elem, OCI_Interval *value, ExceptionSink* xsink);
OCI_EXPORT unsigned int OCI_API OCI_CollGetSize2(OCI_Library *pOCILib, OCI_Coll *coll, ExceptionSink* xsink);
OCI_EXPORT boolean OCI_API OCI_LobIsTemporary2(OCI_Library *pOCILib, OCI_Lob *lob, ExceptionSink* xsink);
OCI_EXPORT boolean OCI_ColumnDescribe2(OCI_Library *pOCILib, OCI_Column *col, OCI_Connection *con, OCI_Statement *stmt, void *handle, int index, int ptype, ExceptionSink* xsink);
OCI_EXPORT boolean OCI_ColumnMap2(OCI_Library *pOCILib, OCI_Column *col, OCI_Statement *stmt);
OCI_EXPORT boolean OCI_KeyMapFree2(OCI_Library *pOCILib);
OCI_EXPORT void OCI_ExceptionRaise2(OCI_Library *pOCILib, OCI_Error *err, ExceptionSink* xsink);
OCI_EXPORT void OCI_ExceptionOCI2(OCI_Library *pOCILib, OCIError *p_err, OCI_Connection *con, OCI_Statement *stmt, boolean warning, ExceptionSink* xsink);
OCI_EXPORT void OCI_ExceptionNotInitialized2(OCI_Library *pOCILib, ExceptionSink* xsink);
//OCI_EXPORT void OCI_ExceptionLoadingSharedLib2(OCI_Library *pOCILib);
//OCI_EXPORT void OCI_ExceptionLoadingSymbols2(OCI_Library *pOCILib);
//OCI_EXPORT void OCI_ExceptionNotMultithreaded2(OCI_Library *pOCILib);
OCI_EXPORT void OCI_ExceptionNullPointer2(OCI_Library *pOCILib, int type, ExceptionSink* xsink);
//OCI_EXPORT void OCI_ExceptionMemory2(OCI_Library *pOCILib, int type, size_t nb_bytes, OCI_Connection *con, OCI_Statement *stmt);
OCI_EXPORT void OCI_ExceptionNotAvailable2(OCI_Library *pOCILib, OCI_Connection *con, int feature, ExceptionSink* xsink);
//OCI_EXPORT void OCI_ExceptionDatatypeNotSupported2(OCI_Library *pOCILib, OCI_Connection *con, OCI_Statement *stmt, int code);
//OCI_EXPORT void OCI_ExceptionParsingToken2(OCI_Library *pOCILib, OCI_Connection *con, OCI_Statement *stmt, mtext token);
//OCI_EXPORT void OCI_ExceptionMappingArgument2(OCI_Library *pOCILib, OCI_Connection *con, OCI_Statement *stmt, int arg);
OCI_EXPORT void OCI_ExceptionOutOfBounds2(OCI_Library *pOCILib, OCI_Connection *con, int value, ExceptionSink* xsink);
OCI_EXPORT void OCI_ExceptionUnfreedData2(OCI_Library *pOCILib, int type_elem, int nb_elem, ExceptionSink* xsink);
//OCI_EXPORT void OCI_ExceptionMaxBind2(OCI_Library *pOCILib, OCI_Statement *stmt);
OCI_EXPORT void OCI_ExceptionAttributeNotFound2(OCI_Library *pOCILib, OCI_Connection *con, const mtext *attr, ExceptionSink* xsink);
OCI_EXPORT void OCI_ExceptionMinimumValue2(OCI_Library *pOCILib, OCI_Connection *con, OCI_Statement *stmt, int min, ExceptionSink* xsink);
OCI_EXPORT void OCI_ExceptionTypeNotCompatible2(OCI_Library *pOCILib, OCI_Connection *con, ExceptionSink* xsink);
//OCI_EXPORT void OCI_ExceptionStatementState2(OCI_Library *pOCILib, OCI_Statement *stmt, int state);
//OCI_EXPORT void OCI_ExceptionStatementNotScrollable2(OCI_Library *pOCILib, OCI_Statement *stmt);
//OCI_EXPORT void OCI_ExceptionBindAlreadyUsed2(OCI_Library *pOCILib, OCI_Statement *stmt, const mtext * bind);
//OCI_EXPORT void OCI_ExceptionBindArraySize2(OCI_Library *pOCILib, OCI_Statement *stmt, unsigned int maxsize, unsigned int cursize, unsigned int newsize);
//OCI_EXPORT void OCI_ExceptionDirPathColNotFound2(OCI_Library *pOCILib, OCI_DirPath *dp, const mtext * column, const mtext *table);
//OCI_EXPORT void OCI_ExceptionDirPathState2(OCI_Library *pOCILib, OCI_DirPath *dp, int state);
//OCI_EXPORT void OCI_ExceptionOCIEnvironment2(OCI_Library *pOCILib);
//OCI_EXPORT boolean OCI_API OCI_DateFromCTime2(OCI_Library *pOCILib, OCI_Date *date, struct tm *ptm, time_t t);
OCI_EXPORT boolean OCI_API OCI_TimestampFree2(OCI_Library *pOCILib, OCI_Timestamp *tmsp);
OCI_EXPORT boolean OCI_ElemSetNumberFromString(OCI_Library *pOCILib, OCI_Elem* elem, const char* str, int size, ExceptionSink* xsink);
OCI_EXPORT boolean OCI_ElemSetNumber2(OCI_Library *pOCILib, OCI_Elem  *elem, void *value, uword size, uword flag, ExceptionSink* xsink);
OCI_EXPORT OCI_Date * OCI_API  OCI_ElemGetDate2(OCI_Library *pOCILib, OCI_Elem *elem);
OCI_EXPORT boolean OCI_API OCI_ElemSetDate2(OCI_Library *pOCILib, OCI_Elem *elem, OCI_Date *value, ExceptionSink* xsink);
//OCI_EXPORT boolean OCI_API OCI_ElemSetFile2(OCI_Library *pOCILib, OCI_Elem *elem, OCI_File *value);
//OCI_EXPORT boolean OCI_API OCI_ElemSetRef2(OCI_Library *pOCILib, OCI_Elem *elem, OCI_Ref *value);
OCI_EXPORT boolean OCI_API OCI_ElemIsNull2(OCI_Library *pOCILib, OCI_Elem *elem);
OCI_EXPORT boolean OCI_API OCI_ElemSetNull2(OCI_Library *pOCILib, OCI_Elem *elem);
OCI_EXPORT boolean OCI_ElemGetNumber2(OCI_Library *pOCILib, OCI_Elem *elem, void *value, uword size, uword flag, ExceptionSink* xsink);
OCI_EXPORT OCI_Elem * OCI_API OCI_ElemCreate2(OCI_Library *pOCILib, OCI_TypeInfo *typinf, ExceptionSink* xsink);
//OCI_EXPORT short OCI_API OCI_ElemGetShort2(OCI_Library *pOCILib, OCI_Elem *elem);
//OCI_EXPORT unsigned short OCI_API OCI_ElemGetUnsignedShort2(OCI_Library *pOCILib, OCI_Elem *elem);
//OCI_EXPORT int OCI_API OCI_ElemGetInt2(OCI_Library *pOCILib, OCI_Elem *elem);
//OCI_EXPORT unsigned int OCI_API OCI_ElemGetUnsignedInt2(OCI_Library *pOCILib, OCI_Elem *elem);
OCI_EXPORT int64 OCI_API OCI_ElemGetBigInt2(OCI_Library *pOCILib, OCI_Elem *elem, ExceptionSink* xsink);
//OCI_EXPORT big_uint OCI_API OCI_ElemGetUnsignedBigInt2(OCI_Library *pOCILib, OCI_Elem *elem);
OCI_EXPORT boolean OCI_API OCI_DatabaseShutdown2(OCI_Library *pOCILib, const mtext *db, const mtext *user, const mtext *pwd, unsigned int sess_mode, unsigned int shut_mode, unsigned int shut_flag);
OCI_EXPORT boolean OCI_NumberGet2(OCI_Library *pOCILib, OCI_Connection *con,  OCINumber *data, void *value, uword size, uword flag, ExceptionSink* xsink);
OCI_EXPORT boolean OCI_NumberSet2(OCI_Library *pOCILib, OCI_Connection *con,  OCINumber *data, void *value, uword size, uword flag, ExceptionSink* xsink);
OCI_EXPORT boolean OCI_NumberConvertStr2(OCI_Library *pOCILib, OCI_Connection *con,  OCINumber *num, const dtext *str, int str_size, const mtext* fmt, ub4 fmt_size, ExceptionSink* xsink);
OCI_EXPORT boolean OCI_NumberGetFromStr2(OCI_Library *pOCILib, OCI_Connection *con,  void *value, uword size, uword type, const dtext *str, int str_size, const mtext* fmt, ub4 fmt_size, ExceptionSink* xsink);
OCI_EXPORT boolean OCI_API OCI_TypeInfoFree2(OCI_Library *pOCILib, OCI_TypeInfo *typinf);
OCI_EXPORT unsigned int OCI_API OCI_TypeInfoGetType2(OCI_Library *pOCILib, OCI_TypeInfo *typinf);
OCI_EXPORT unsigned int OCI_API OCI_TypeInfoGetColumnCount2(OCI_Library *pOCILib, OCI_TypeInfo *typinf);
OCI_EXPORT OCI_Column * OCI_API OCI_TypeInfoGetColumn2(OCI_Library *pOCILib, OCI_TypeInfo *typinf, unsigned int index, ExceptionSink* xsink);
OCI_EXPORT const mtext * OCI_API OCI_TypeInfoGetName2(OCI_Library *pOCILib, OCI_TypeInfo *typinf);
OCI_EXPORT const mtext * OCI_API OCI_ColumnGetName2(OCI_Library *pOCILib, OCI_Column *col, ExceptionSink* xsink);
OCI_EXPORT unsigned int OCI_API OCI_ColumnGetType2(OCI_Library *pOCILib, OCI_Column *col);
OCI_EXPORT unsigned int OCI_API OCI_ColumnGetCharsetForm2(OCI_Library *pOCILib, OCI_Column *col);
OCI_EXPORT unsigned int OCI_API OCI_ColumnGetSize2(OCI_Library *pOCILib, OCI_Column *col);
OCI_EXPORT int OCI_API OCI_ColumnGetScale2(OCI_Library *pOCILib, OCI_Column *col);
OCI_EXPORT int OCI_API OCI_ColumnGetPrecision2(OCI_Library *pOCILib, OCI_Column *col);
OCI_EXPORT int OCI_API OCI_ColumnGetFractionalPrecision2(OCI_Library *pOCILib, OCI_Column *col);
OCI_EXPORT int OCI_API OCI_ColumnGetLeadingPrecision2(OCI_Library *pOCILib, OCI_Column *col);
OCI_EXPORT boolean OCI_API OCI_ColumnGetNullable2(OCI_Library *pOCILib, OCI_Column *col);
OCI_EXPORT boolean OCI_API OCI_ColumnGetCharUsed2(OCI_Library *pOCILib, OCI_Column *col);
OCI_EXPORT const mtext * OCI_API OCI_ColumnGetSQLType2(OCI_Library *pOCILib, OCI_Column *col);
OCI_EXPORT unsigned int OCI_API OCI_ColumnGetFullSQLType2(OCI_Library *pOCILib, OCI_Column *col, mtext *buffer, unsigned int len);
OCI_EXPORT OCI_TypeInfo * OCI_API OCI_ColumnGetTypeInfo2(OCI_Library *pOCILib, OCI_Column *col);
OCI_EXPORT unsigned int OCI_API OCI_ColumnGetSubType2(OCI_Library *pOCILib, OCI_Column *col);
OCI_EXPORT OCI_Error * OCI_ErrorGet2(OCI_Library *pOCILib, boolean check, boolean warning);
OCI_EXPORT int OCI_API OCI_ErrorGetInternalCode2(OCI_Error *err);
OCI_EXPORT OCI_List * OCI_ListCreate2(OCI_Library *pOCILib, int type);
void * OCI_MemAlloc2(OCI_Library *pOCILib, int ptr_type, size_t block_size, size_t block_count, boolean zero_fill);
void * OCI_MemRealloc2(OCI_Library *pOCILib, void * ptr_mem, int ptr_type, size_t block_size, size_t block_count);
sword OCI_HandleAlloc2(OCI_Library *pOCILib, CONST dvoid *parenth, dvoid **hndlpp, CONST ub4 type, CONST size_t xtramem_sz, dvoid **usrmempp);
sword OCI_HandleFree2(OCI_Library *pOCILib, dvoid *hndlp, CONST ub4 type);
sword OCI_DescriptorAlloc2(OCI_Library *pOCILib, CONST dvoid *parenth, dvoid **descpp, CONST ub4 type, CONST size_t xtramem_sz,  dvoid **usrmempp);
sword OCI_DescriptorArrayAlloc2(OCI_Library *pOCILib, CONST dvoid *parenth, dvoid **descpp, CONST ub4 type, ub4 nb_elem, CONST size_t xtramem_sz, dvoid **usrmempp);
sword OCI_DescriptorFree2(OCI_Library *pOCILib, dvoid *descp, CONST ub4 type);
sword OCI_DescriptorArrayFree2(OCI_Library *pOCILib, dvoid **descp, CONST ub4 type, ub4 nb_elem);
sword OCI_ObjectNew2(OCI_Library *pOCILib, OCIEnv *env, OCIError *err, CONST OCISvcCtx *svc, OCITypeCode typecode, OCIType *tdo, dvoid *table, OCIDuration duration, boolean value, dvoid **instance);
sword OCI_OCIObjectFree2(OCI_Library *pOCILib, OCIEnv *env, OCIError *err, dvoid *instance, ub2 flags);
int OCI_ObjectGetAttrIndex2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, int type, ExceptionSink* xsink);
boolean OCI_ObjectSetNumberFromString(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, const char* str, int size, ExceptionSink* xsink);
boolean OCI_ObjectSetNumber2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, void *value, uword size, uword flag, ExceptionSink* xsink);
boolean OCI_ObjectGetNumber2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, void *value, uword size, uword flag, ExceptionSink* xsink);
OCI_Date * OCI_API OCI_ObjectGetDate2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, ExceptionSink* xsink);
boolean OCI_API OCI_ObjectSetLob2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, OCI_Lob *value, ExceptionSink* xsink);
//boolean OCI_API OCI_ObjectSetFile2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, OCI_File *value);
boolean OCI_API OCI_ObjectSetNull2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, ExceptionSink* xsink);
boolean OCI_API OCI_ObjectIsNull2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, ExceptionSink* xsink);
OCI_TypeInfo * OCI_API OCI_ObjectGetTypeInfo2(OCI_Library *pOCILib, OCI_Object *obj);
unsigned int OCI_API OCI_ObjectGetType2(OCI_Library *pOCILib, OCI_Object *obj);
boolean OCI_API OCI_ObjectGetStruct2(OCI_Library *pOCILib, OCI_Object *obj, void **pp_struct, void** pp_ind);
//OCI_Ref * OCI_API OCI_ObjectGetRef2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr);
boolean OCI_API OCI_ObjectSetDate2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, OCI_Date *value, ExceptionSink* xsink);
OCI_Date * OCI_DateInit2(OCI_Library *pOCILib, OCI_Connection *con, OCI_Date **pdate, OCIDate *buffer, boolean allocate, boolean ansi);
OCI_Elem * OCI_ElemInit2(OCI_Library *pOCILib, OCI_Connection *con, OCI_Elem **pelem, void *handle, OCIInd *pind, OCI_TypeInfo *typinf, ExceptionSink* xsink);
char * ocistrdup2(OCI_Library *pOCILib, const char * src);
wchar_t * ociwcsdup2(OCI_Library *pOCILib, const wchar_t * src);
boolean OCI_API OCI_ObjectSetDouble2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, double value, ExceptionSink* xsink);
//boolean OCI_API OCI_ObjectSetUnsignedBigInt2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, big_uint value);
boolean OCI_API OCI_ObjectSetBigInt2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, big_int value, ExceptionSink* xsink);
//boolean OCI_API OCI_ObjectSetUnsignedInt2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, unsigned int value);
//boolean OCI_API OCI_ObjectSetInt2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, int value);
//boolean OCI_API OCI_ObjectSetUnsignedShort2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, unsigned short value);
//boolean OCI_API OCI_ObjectSetShort2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, short value);
boolean OCI_API OCI_LobFree(OCI_Library *pOCILib, OCI_Lob *lob, ExceptionSink* xsink);
unsigned int OCI_API OCI_LobGetType(OCI_Library *pOCILib, OCI_Lob *lob);
//boolean OCI_API OCI_LobSeek(OCI_Library *pOCILib, OCI_Lob *lob, big_uint offset, unsigned int mode);
//big_uint OCI_API OCI_LobGetOffset(OCI_Library *pOCILib, OCI_Lob *lob);
boolean OCI_API OCI_LobRead2(OCI_Library *pOCILib, OCI_Lob *lob, void *buffer, unsigned int *char_count, unsigned int *byte_count, ExceptionSink* xsink);
boolean OCI_API OCI_LobWrite2(OCI_Library *pOCILib, OCI_Lob *lob, void *buffer, unsigned int *char_count, unsigned int *byte_count, ExceptionSink* xsink);
//boolean OCI_API OCI_LobTruncate(OCI_Library *pOCILib, OCI_Lob *lob, big_uint size, ExceptionSink* xsink);
//big_uint OCI_API OCI_LobErase(OCI_Library *pOCILib, OCI_Lob *lob, big_uint offset, big_uint size);
big_uint OCI_API OCI_LobGetLength(OCI_Library *pOCILib, OCI_Lob *lob, ExceptionSink* xsink);
//unsigned int OCI_API OCI_LobGetChunkSize(OCI_Library *pOCILib, OCI_Lob *lob);
//boolean OCI_API OCI_LobCopy(OCI_Library *pOCILib, OCI_Lob *lob, OCI_Lob *lob_src, big_uint offset_dst, big_uint offset_src, big_uint count);
//boolean OCI_API OCI_LobCopyFromFile(OCI_Library *pOCILib, OCI_Lob *lob, OCI_File *file, big_uint offset_dst, big_uint offset_src, big_uint count);
//boolean OCI_API OCI_LobAppend2(OCI_Library *pOCILib, OCI_Lob *lob, void *buffer, unsigned int *char_count, unsigned int *byte_count, ExceptionSink* xsink);
//boolean OCI_API OCI_LobAppendLob(OCI_Library *pOCILib, OCI_Lob *lob, OCI_Lob *lob_src);
boolean OCI_API OCI_LobOpen(OCI_Library *pOCILib, OCI_Lob *lob, unsigned int mode);
boolean OCI_API OCI_LobClose(OCI_Library *pOCILib, OCI_Lob *lob);
//big_uint OCI_API OCI_LobGetMaxSize(OCI_Library *pOCILib, OCI_Lob *lob);
//boolean OCI_API OCI_LobFlush(OCI_Library *pOCILib, OCI_Lob *lob);
//boolean OCI_API OCI_LobEnableBuffering(OCI_Library *pOCILib, OCI_Lob *lob, boolean value);
OCI_Date * OCI_API OCI_DateCreate(OCI_Library *pOCILib, OCI_Connection *con);
//boolean OCI_API OCI_DateAddDays(OCI_Library *pOCILib, OCI_Date *date, int nb);
//boolean OCI_API OCI_DateAddMonths(OCI_Library *pOCILib, OCI_Date *date, int nb);
boolean OCI_API OCI_DateAssign(OCI_Library *pOCILib, OCI_Date *date, OCI_Date *date_src, ExceptionSink* xsink);
//int OCI_API OCI_DateCheck(OCI_Library *pOCILib, OCI_Date *date);
//int OCI_API OCI_DateCompare(OCI_Library *pOCILib, OCI_Date *date, OCI_Date *date2);
//int OCI_API OCI_DateDaysBetween(OCI_Library *pOCILib, OCI_Date *date, OCI_Date *date2);
//boolean OCI_API OCI_DateFromText(OCI_Library *pOCILib, OCI_Date *date, const mtext *str, const mtext *fmt);
//boolean OCI_API OCI_DateGetDate(OCI_Library *pOCILib, OCI_Date *date, int *year, int *month, int *day);
//boolean OCI_API OCI_DateGetTime(OCI_Library *pOCILib, OCI_Date *date, int *hour, int *min, int *sec);
//boolean OCI_API OCI_DateGetDateTime(OCI_Library *pOCILib, OCI_Date *date, int *year, int *month, int *day, int *hour, int *min, int *sec);
//boolean OCI_API OCI_DateLastDay(OCI_Library *pOCILib, OCI_Date *date);
//boolean OCI_API OCI_DateNextDay(OCI_Library *pOCILib, OCI_Date *date, const mtext *day);
boolean OCI_API OCI_DateSetDate(OCI_Library *pOCILib, OCI_Date *date, int year, int month, int day);
boolean OCI_API OCI_DateSetTime(OCI_Library *pOCILib, OCI_Date *date, int hour, int min, int sec);
boolean OCI_API OCI_DateSetDateTime(OCI_Library *pOCILib, OCI_Date *date, int year, int month, int day, int hour, int min, int sec);
//boolean OCI_API OCI_DateSysDate(OCI_Library *pOCILib, OCI_Date *date);
//boolean OCI_API OCI_DateToText(OCI_Library *pOCILib, OCI_Date *date, const mtext *fmt, int size, mtext *str);
//boolean OCI_API OCI_DateZoneToZone(OCI_Library *pOCILib, OCI_Date *date, const mtext *zone1, const mtext *zone2);
//boolean OCI_API OCI_DateToCTime(OCI_Library *pOCILib, OCI_Date *date, struct tm *ptm, time_t *pt);
boolean OCI_API OCI_DateFree(OCI_Library *pOCILib, OCI_Date *date);
//boolean OCI_API OCI_ElemSetShort(OCI_Library *pOCILib, OCI_Elem *elem, short value);
//boolean OCI_API OCI_ElemSetUnsignedShort(OCI_Library *pOCILib, OCI_Elem *elem, unsigned short value);
//boolean OCI_API OCI_ElemSetInt(OCI_Library *pOCILib, OCI_Elem *elem, int value);
//boolean OCI_API OCI_ElemSetUnsignedInt(OCI_Library *pOCILib, OCI_Elem *elem, unsigned int value);
boolean OCI_API OCI_ElemSetBigInt(OCI_Library *pOCILib, OCI_Elem *elem, big_int value, ExceptionSink* xsink);
//boolean OCI_API OCI_ElemSetUnsignedBigInt(OCI_Library *pOCILib, OCI_Elem *elem, big_uint value);
boolean OCI_API OCI_ElemSetDouble(OCI_Library *pOCILib, OCI_Elem *elem, double value, ExceptionSink* xsink);
boolean OCI_API OCI_ElemFree2(OCI_Library *pOCILib, OCI_Elem *elem, ExceptionSink* xsink);
double OCI_API OCI_ElemGetDouble2(OCI_Library *pOCILib, OCI_Elem *elem, ExceptionSink* xsink);
//short OCI_API OCI_ObjectGetShort2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr);
//unsigned short OCI_API OCI_ObjectGetUnsignedShort2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr);
//int OCI_API OCI_ObjectGetInt2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr);
//unsigned int OCI_API OCI_ObjectGetUnsignedInt2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr);
int64 OCI_API OCI_ObjectGetBigInt2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, ExceptionSink* xsink);
//big_uint OCI_API OCI_ObjectGetUnsignedBigInt2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr);
double OCI_API OCI_ObjectGetDouble2(OCI_Library *pOCILib, OCI_Object *obj, const mtext *attr, ExceptionSink* xsink);
boolean OCI_API OCI_IntervalFree2(OCI_Library *pOCILib, OCI_Interval *itv);

#ifdef __cplusplus
//}
#endif

#endif /* OCILIB_OCILIB_TYPES_H_INCLUDED */

