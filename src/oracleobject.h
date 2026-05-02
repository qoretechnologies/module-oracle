#ifndef ORACLEOBJECT_H
#define ORACLEOBJECT_H

#include "config.h"
#include <qore/Qore.h>
#include <map>
#include <string>
#include "ocilib.h"
#include "ocilib_types.h"

class QoreOracleConnection;

/*! A ocilib-like error handler to catch OCILIB raised errors
 */
void ocilib_err_handler(OCI_Error* err, ExceptionSink* xsink);

/*! OCI_ObjectGetStruct-like function for collections. It has no reason
    to go to OCILIB. We are using it only for our mixed OCI/OCILIB environment.
    \note It's used only to get atomic null for collections.
    \param obj OCI_Coll instance
    \param pp_struct a struct with OCI values pointer
    \param pp_ind a NULL struct
    \retval bool true on success
 */
boolean OCI_CollGetStruct
(
   OCI_Library *pOCILib,
   OCI_Coll *obj,
   void **pp_struct,
   void** pp_ind,
   ExceptionSink* xsink
);

// return NTY object type - ORACLE_COLLECTION or ORACLE_OBJECT
// should be called id it's sure it's a NTY (after ntyCheckType()
// and/or in SQLT_NTY cases
std::string ntyHashType(const QoreHashNode * n);

// check if is provided hash required NTY object
bool ntyCheckType(const char * tname, const QoreHashNode * n, qore_type_t t);

/* Oracle Named Types - Objects
 */

/*! Bind a special structured qore hash as an Oracle Object
    into the statement - used with %v.
    \param d reference to QoreOracleConnection with successfully set connection
    \param h qore hash node prepared with f_oracle_object()/bindOracleObject()
    \param xsink exception hanlder
    \retval OCI_Object* always initialized instance. Xsink status has to be checked
                        elsewhere. The instance has to be freed in any case.
 */
OCI_Object* objBindQore(QoreOracleConnection * d,
                        const QoreHashNode * h,
                        ExceptionSink * xsink);

/*! Bind a an Oracle Object as a placeholder for OUT variables
    into the statement - used with :name.
    Returned object is empty and has to be set with OCI call of OCIObjectBind().
    \param d reference to QoreOracleConnection with successfully set connection
    \param h qore string node with oracle type name.
    \param xsink exception hanlder
    \retval OCI_Object* always initialized instance. Xsink status has to be checked
                        elsewhere. The instance has to be freed in any case.
 */
OCI_Object* objPlaceholderQore(QoreOracleConnection * conn,
                               const char * tname,
                               ExceptionSink *xsink);

/*! Convert an Oracle Object into Qore Hash node.
    The hash is plain Qore hash - no Object-hash structure is set.
    \param obj Oracle Object instance
    \param xsink exception hanlder
    \retval AbstractQoreNode* Plain Qore hash node
 */
QoreHashNode* objToQore(QoreOracleConnection * conn,
                            OCI_Object * obj,
                            ExceptionSink *xsink);


/* Oracle Named Types - Collections
 */
/*! Bind a special structured qore hash as an Oracle Collection
    into the statement - used with %v.
    \param d reference to QoreOracleConnection with successfully set connection
    \param h qore hash node prepared with f_oracle_collection()/bindOracleCollection()
    \param xsink exception hanlder
    \retval OCI_Coll* always initialized instance. Xsink status has to be checked
                        elsewhere. The instance has to be freed in any case.
 */
OCI_Coll* collBindQore(QoreOracleConnection * d,
                       const QoreHashNode * h,
                       ExceptionSink * xsink);

/*! Bind a an Oracle Collection as a placeholder for OUT variables
    into the statement - used with :name.
    Returned collection is empty and has to be set with OCI call of OCIObjectBind().
    \param d reference to QoreOracleConnection with successfully set connection
    \param h qore string node with oracle type name.
    \param xsink exception hanlder
    \retval OCI_Coll* always initialized instance. Xsink status has to be checked
                        elsewhere. The instance has to be freed in any case.
 */
OCI_Coll* collPlaceholderQore(QoreOracleConnection * conn,
                              const char * tname,
                              ExceptionSink *xsink);

/*! Convert an Oracle Collection into Qore List node.
    The hash is plain Qore list - no Object-hash structure is set.
    \param obj Oracle Collection instance
    \param xsink exception hanlder
    \retval AbstractQoreNode* Plain Qore list node
 */
QoreListNode* collToQore(QoreOracleConnection *conn,
                         OCI_Coll * obj,
                         ExceptionSink *xsink);


#endif
