/* -*- mode: c++; indent-tabs-mode: nil -*- */
/*
    oracleobject.cpp

    Oracle OCI Interface to Qore DBI layer

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

#include "oracleobject.h"
#include "oracle.h"
#include "ocilib_types.h"

void ocilib_err_handler(OCI_Error *err, ExceptionSink* xsink) {
   if (xsink && !err->warning) {
      QoreStringNode* desc = new QoreStringNode();
      int ocicode = OCI_ErrorGetOCICode(err);
      if (ocicode)
         desc->sprintf("ORA-%05d: ", ocicode);
      int ocilibcode = OCI_ErrorGetInternalCode2(err);
      if (ocilibcode)
         desc->sprintf("ocilib error %d: ", ocilibcode);
      desc->concat(OCI_ErrorGetString(err));
      desc->trim_trailing();
      xsink->raiseException("ORACLE-OCI-ERROR", desc);
   }
   else {
      // TODO/FIXME: xsink handling here.
      printf("internal OCILIB error:\n"
             "  code  : ORA-%05i\n"
             "  icode : %d\n"
             "  msg   : %s\n",
             //"  sql   : %s\n",
             OCI_ErrorGetOCICode(err),
             OCI_ErrorGetInternalCode2(err),
             OCI_ErrorGetString(err)
             //OCI_GetSql(OCI_ErrorGetStatement(err))
             //"<not available>"
         );
      assert(false);
   }
};

/* ------------------------------------------------------------------------ *
 * OCI_CollGetStruct
 * ------------------------------------------------------------------------ */
#include "ocilib_internal.h"
boolean OCI_API OCI_CollGetStruct(OCI_Library* pOCILib, OCI_Coll* obj, void** pp_struct, void** pp_ind, ExceptionSink* xsink) {
   OCI_CHECK_PTRQ(pOCILib, OCI_IPC_OBJECT, obj, FALSE, xsink);

   OCI_RESULT(pOCILib, TRUE);

   *pp_struct = (void *) obj->handle;

   if (pp_ind)
      *pp_ind = (void *) obj->tab_ind;

   OCI_RESULT(pOCILib, TRUE);

   return TRUE;
}

// return NTY object type - ORACLE_COLLECTION or ORACLE_OBJECT
// should be called id it's sure it's a NTY (after ntyCheckType()
// and/or in SQLT_NTY cases
std::string ntyHashType(const QoreHashNode* n) {
    if (!n) {
        return {};
    }

    QoreValue qv = n->getKeyValue("type");
    if (qv.getType() != NT_STRING) {
        return {};
    }

    QoreStringValueHelper str(qv);
    return std::string(str->c_str(), str->size());
}

// check if is the hash really oracle NTY object
bool ntyCheckType(const char * tname, const QoreHashNode * n, qore_type_t t) {
    if (n->getKeyValue("type").getType() != NT_STRING) {
        return false;
    }
    if (n->getKeyValue("^oratype^").getType() != NT_STRING) {
        return false;
    }
    if (n->getKeyValue("^values^").getType() != t) {
        return false;
    }

//     printf("ntyCheckType() ^oratype^ %s\n", s->getBuffer());
    std::string givenName = ntyHashType(n);
//     printf("ntyCheckType(%s, %s) strcmp(givenName, tname) != 0 => %d\n", givenName, tname, strcmp(givenName, tname) != 0);
    return !givenName.empty() && !strcmp(givenName.c_str(), tname);
}

OCI_Object* objPlaceholderQore(QoreOracleConnection * conn, const char * tname, ExceptionSink *xsink) {
    OCI_TypeInfo * info = OCI_TypeInfoGet2(&conn->ocilib, conn->ocilib_cn, tname, OCI_TIF_TYPE, xsink);
    if (!info) {
        if (!*xsink)
            xsink->raiseException("ORACLE-OBJECT-ERROR", "could not get type info for object type '%s'", tname);
        return nullptr;
    }
    OCI_Object * obj = OCI_ObjectCreate2(&conn->ocilib, conn->ocilib_cn, info, xsink);
    if (!obj && !*xsink)
        xsink->raiseException("ORACLE-OBJECT-ERROR", "could not create placeholder buffer for object type '%s'", tname);
    return obj;
}

class ObjectHolder {
public:
   OCI_Library* pOCILib;
   OCI_Object* obj;
   ExceptionSink* xsink;

   DLLLOCAL ObjectHolder(OCI_Library* p, OCI_Object* o,ExceptionSink* xs ) : pOCILib(p), obj(o), xsink(xs) {
   }

   DLLLOCAL ~ObjectHolder() {
      if (obj)
         OCI_ObjectFree2(pOCILib, obj, xsink);
   }

   DLLLOCAL operator bool() const {
      return obj;
   }

   DLLLOCAL OCI_Object* operator*() {
      return obj;
   }

   DLLLOCAL OCI_Object* release() {
      OCI_Object* ptr = obj;
      obj = 0;
      return ptr;
   }
};

class CollHolder {
public:
   OCI_Library* pOCILib;
   OCI_Coll* coll;
   ExceptionSink* xsink;

   DLLLOCAL CollHolder(OCI_Library* p, OCI_Coll* c, ExceptionSink* xs) : pOCILib(p), coll(c), xsink(xs) {
   }

   DLLLOCAL ~CollHolder() {
      if (coll)
         OCI_CollFree2(pOCILib, coll, xsink);
   }

   DLLLOCAL operator bool() const {
      return coll;
   }

   DLLLOCAL OCI_Coll* operator*() {
      return coll;
   }

   DLLLOCAL OCI_Coll* release() {
      OCI_Coll* ptr = coll;
      coll = 0;
      return ptr;
   }
};

OCI_Object* objBindQore(QoreOracleConnection * d, const QoreHashNode * h, ExceptionSink * xsink) {
//     QoreNodeAsStringHelper str(h, 1, xsink);
//     printf("obj hash= %s\n", str->getBuffer());

    if (!ntyCheckType(ORACLE_OBJECT, h, NT_HASH)) {
        xsink->raiseException("BIND-ORACLE-OBJECT-ERROR",
                              "Bind value was not created with bindOracleObject()");
        return nullptr;
    }

    const QoreHashNode* th = h->getKeyValue("^values^").get<const QoreHashNode>();
    QoreStringValueHelper tname_holder(h->getKeyValue("^oratype^"));
    const char* tname = tname_holder->c_str();

    OCI_TypeInfo * info = OCI_TypeInfoGet2(&d->ocilib, d->ocilib_cn, tname, OCI_TIF_TYPE, xsink);
    if (!info) {
        if (!*xsink)
            xsink->raiseException("BIND-NTY-ERROR", "No type '%s' defined in the DB", tname);
            return nullptr;
    }
    ObjectHolder obj(&d->ocilib, OCI_ObjectCreate2(&d->ocilib, d->ocilib_cn, info, xsink), xsink);
    if (!obj) {
        if (!*xsink)
            xsink->raiseException("BIND-NTY-ERROR", "unable to create object of type '%s' for bind", tname);
        return nullptr;
    }

    int n = OCI_TypeInfoGetColumnCount2(&d->ocilib, info);
    if (!n) {
        xsink->raiseException("BIND-NTY-ERROR", "unable to retrieve attribute info for object of type '%s' for bind", tname);
        return nullptr;
    }
    for (int i = 1; i <= n; ++i) {
        OCI_Column* col = OCI_TypeInfoGetColumn2(&d->ocilib, info, i, xsink);
        if (*xsink)
            return nullptr;
        if (!col) {
            xsink->raiseException("BIND-NTY-ERROR", "failed to retrieve column information for column %d for object type '%s'", 1, tname);
            return nullptr;
        }

        const char* cname = OCI_ColumnGetName2(&d->ocilib, col, xsink);
        if (*xsink)
            return nullptr;
        if (!cname) {
            xsink->raiseException("BIND-NTY-ERROR", "failed to retrieve column information for column %d", 1);
            return nullptr;
        }

//         printf("Binding attribute: %s (%d/%d)\n", cname, i, n);
        // This test has been removed to allow binding of filtered hashes (only required attributes)
        //if (! th->existsKey(cname)) {
            //xsink->raiseException("BIND-NTY-ERROR", "Key %s (case sensitive) does not exist in the object hash", cname);
            //return nullptr;
        //}
        bool e;
        QoreValue val = th->getKeyValueExistence(cname, e);
        if (!e) {
            QoreString cn(cname);
            cn.tolwr();
            val = th->getKeyValueExistence(cn.c_str(), e);
        }

        if (!e || val.isNullOrNothing()) {
            if (!OCI_ObjectSetNull2(&d->ocilib, *obj, cname, xsink)) {
                if (!*xsink)
                    xsink->raiseException("BIND-NTY-ERROR", "unable to set object attribute %s.%s to NULL", tname, cname);
                return nullptr;
            }
            continue;
        }

        switch (col->ocode) {
            case SQLT_LNG: // long
            case SQLT_AFC:
            case SQLT_AVC:
            case SQLT_STR:
            case SQLT_CHR: {
                // strings
                QoreStringValueHelper str(val, d->ds.getQoreEncoding(), xsink);
                if (*xsink)
                    return nullptr;
                if (str->empty()) {
                    if (!OCI_ObjectSetNull2(&d->ocilib, *obj, cname, xsink)) {
                        if (!*xsink)
                            xsink->raiseException("BIND-NTY-ERROR", "unable to set object attribute %s.%s to NULL", tname, cname);
                        return nullptr;
                    }
                }
                else {
                    if (!OCI_ObjectSetString2(&d->ocilib, *obj, cname, str->empty() ? 0 : str->getBuffer(), xsink)) {
                        if (!*xsink)
                            xsink->raiseException("BIND-NTY-ERROR", "unable to assign a value to string object attribute %s.%s", tname, cname);
                        return nullptr;
                    }
                }
                break;
            }

            case SQLT_NUM: {
                switch (val.getType()) {
                   case NT_STRING: {
                        QoreStringValueHelper str(val);
                        if (!OCI_ObjectSetNumberFromString(&d->ocilib, *obj, cname, str->getBuffer(), (int)str->size(), xsink)) {
                            if (!*xsink)
                                xsink->raiseException("BIND-NTY-ERROR", "NUMBER: unable to assign a string value to object attribute %s.%s", tname, cname);
                            return nullptr;
                        }
                        break;
                   }
                   case NT_FLOAT: {
                        if (!OCI_ObjectSetDouble2(&d->ocilib, *obj, cname, val.getAsFloat(), xsink)) {
                            if (!*xsink)
                                xsink->raiseException("BIND-NTY-ERROR", "NUMBER: unable to assign a float value to object attribute %s.%s", tname, cname);
                            return nullptr;
                        }
                        break;
                   }
                   case NT_INT: {
                        if (!OCI_ObjectSetBigInt2(&d->ocilib, *obj, cname, val.getAsBigInt(), xsink)) {
                            if (!*xsink)
                                xsink->raiseException("BIND-NTY-ERROR", "NUMBER: unable to assign an integer value to object attribute %s.%s", tname, cname);
                            return nullptr;
                        }
                        break;
                   }
                   case NT_NUMBER: {
                        const QoreNumberNode* n = val.get<const QoreNumberNode>();
                        QoreString str;
                        n->getStringRepresentation(str);
                        if (!OCI_ObjectSetNumberFromString(&d->ocilib, *obj, cname, str.getBuffer(), (int)str.size(), xsink)) {
                            if (!*xsink)
                                xsink->raiseException("BIND-NTY-ERROR", "NUMBER: unable to assign a string value to object attribute %s.%s", tname, cname);
                            return nullptr;
                        }
                        break;
                   }
                   default:
                        xsink->raiseException("BIND-NTY-ERROR", "NUMBER: unable to bind value of Qore type: '%s' to object attribute %s.%s", val.getTypeName(), tname, cname);
                        return nullptr;
                }
                break;
            }

            case SQLT_INT:
                //printd(5, "objBindQore() binding int64: %lld\n", val.getAsBigInt());
                if (!OCI_ObjectSetBigInt2(&d->ocilib, *obj, cname, val.getAsBigInt(), xsink)) {
                    if (!*xsink)
                        xsink->raiseException("BIND-NTY-ERROR", "unable to assign a value to integer object attribute %s.%s", tname, cname);
                    return nullptr;
                }
                break;

            case SQLT_FLT:
#if OCI_VERSION_COMPILE >= OCI_10_1
            case SQLT_BFLOAT:
            case SQLT_IBFLOAT:
            case SQLT_BDOUBLE:
            case SQLT_IBDOUBLE:
#endif
                    // float
                if (!OCI_ObjectSetDouble2(&d->ocilib, *obj, cname, val.getAsFloat(), xsink)) {
                    if (!*xsink)
                        xsink->raiseException("BIND-NTY-ERROR", "unable to assign a value to double-precision object attribute %s.%s", tname, cname);
                    return nullptr;
                }
                break;

            case SQLT_DAT:
            case SQLT_ODT:
            case SQLT_DATE:
#if OCI_VERSION_COMPILE >= OCI_9_0
            case SQLT_TIMESTAMP:
            case SQLT_TIMESTAMP_TZ:
            case SQLT_TIMESTAMP_LTZ:
            case SQLT_INTERVAL_YM:
            case SQLT_INTERVAL_DS:
#endif
            {
                // date
                DateTimeValueHelper dn(val);

                if (col->type == OCI_CDT_TIMESTAMP) {
                    //printd(0, "bind %p: %s.%s: %d: binding timestamp\n", &obj, tname, cname, i);
                    OCI_Timestamp* dt = OCI_TimestampCreate2(&d->ocilib, d->ocilib_cn, col->subtype, xsink);
                    if (!dt) {
                        if (!*xsink)
                            xsink->raiseException("BIND-NTY-ERROR", "failed to create timestamp object for object attribute %s.%s", tname, cname);
                        return nullptr;
                    }
                    ON_BLOCK_EXIT(OCI_TimestampFree2, &d->ocilib, dt);
                    if (!OCI_TimestampConstruct2(&d->ocilib, dt,
                                                    dn->getYear(), dn->getMonth(), dn->getDay(),
                                                    dn->getHour(), dn->getMinute(), dn->getSecond(),
                                                    (dn->getMicrosecond() * 1000),
                                                    0, xsink)) {
                        if (!*xsink)
                            xsink->raiseException("BIND-NTY-ERROR", "failed to create timestamp from date/time value for object attribute %s.%s", tname, cname);
                        return nullptr;
                    }
                    if (!OCI_ObjectSetTimestamp2(&d->ocilib, *obj, cname, dt, xsink)) {
                        if (!*xsink)
                            xsink->raiseException("BIND-NTY-ERROR", "failed to set object timestamp attribute %s.%s", tname, cname);
                        return nullptr;
                    }
                    break;
                }

                if (col->type == OCI_CDT_DATETIME) {
                    //printd(0, "bind %p: %s.%s: %d: binding datetime\n", &obj, tname, cname, i);
                    OCI_Date* dt = OCI_DateCreate(&d->ocilib, d->ocilib_cn);
                    if (!dt) {
                        if (!*xsink)
                            xsink->raiseException("BIND-NTY-ERROR", "failed to create date/time object for object attribute %s.%s", tname, cname);
                        return nullptr;
                    }
                    ON_BLOCK_EXIT(OCI_DateFree, &d->ocilib, dt);
                    if (!OCI_DateSetDateTime(&d->ocilib, dt,
                                                dn->getYear(), dn->getMonth(), dn->getDay(),
                                                dn->getHour(), dn->getMinute(), dn->getSecond())) {
                        if (!*xsink)
                            xsink->raiseException("BIND-NTY-ERROR", "failed to create date/time value for object attribute %s.%s", tname, cname);
                        return nullptr;
                    }
                    if (!OCI_ObjectSetDate2(&d->ocilib, *obj, cname, dt, xsink)) {
                        if (!*xsink)
                            xsink->raiseException("BIND-NTY-ERROR", "failed to set object date/time attribute %s.%s", tname, cname);
                        return nullptr;
                    }
                    break;
                }

                // intervals
                if (col->type == OCI_CDT_INTERVAL) {
                    if (col->ocode == SQLT_INTERVAL_YM) {
                        //printd(0, "bind %p: %s.%s: %d: binding interval ym\n", &obj, tname, cname, i);
                        OCI_Interval* dt = OCI_IntervalCreate2(&d->ocilib, d->ocilib_cn, OCI_INTERVAL_YM, xsink);
                        if (!dt) {
                            if (!*xsink)
                                xsink->raiseException("BIND-NTY-ERROR", "failed to create interval object for object attribute %s.%s", tname, cname);
                            return nullptr;
                        }
                        ON_BLOCK_EXIT(OCI_IntervalFree2, &d->ocilib, dt);
                        if (!OCI_IntervalSetYearMonth2(&d->ocilib, dt, dn->getYear(), dn->getMonth(), xsink)) {
                            if (!*xsink)
                                xsink->raiseException("BIND-NTY-ERROR", "failed to set interval year/month value for object attribute %s.%s", tname, cname);
                            return nullptr;
                        }
                        if (!OCI_ObjectSetInterval2(&d->ocilib, *obj, cname, dt, xsink)) {
                            if (!*xsink)
                                xsink->raiseException("BIND-NTY-ERROR", "failed to set interval value for object attribute %s.%s", tname, cname);
                            return nullptr;
                        }
                        break;
                    }

                    //printd(0, "bind %p %s.%s: %d: binding interval ds\n", &obj, tname, cname, i);

                    // SQLT_INTERVAL_DS
                    OCI_Interval* dt = OCI_IntervalCreate2(&d->ocilib, d->ocilib_cn, OCI_INTERVAL_DS, xsink);
                    if (!dt) {
                        if (!*xsink)
                            xsink->raiseException("BIND-NTY-ERROR", "failed to create interval object for object attribute %s.%s", tname, cname);
                        return nullptr;
                    }
                    ON_BLOCK_EXIT(OCI_IntervalFree2, &d->ocilib, dt);
                    if (!OCI_IntervalSetDaySecond2(&d->ocilib, dt,
                                                   dn->getDay(),
                                                   dn->getHour(), dn->getMinute(), dn->getSecond(),
                                                   (dn->getMicrosecond() * 1000), xsink)) {
                        if (!*xsink)
                            xsink->raiseException("BIND-NTY-ERROR", "failed to set interval day/second value for object attribute %s.%s", tname, cname);
                        return nullptr;
                    }
                    if (!OCI_ObjectSetInterval2(&d->ocilib, *obj, cname, dt, xsink)) {
                        if (!*xsink)
                            xsink->raiseException("BIND-NTY-ERROR", "failed to set interval value for object attribute %s.%s", tname, cname);
                        return nullptr;
                    }
                    break;
                }

                xsink->raiseException("BIND-NTY-ERROR", "Unknown DATE-like argument for %s.%s (type: %d)", tname, cname, col->type);
                return nullptr;
            }

//             case SQLT_RDD:
//             case SQLT_RID:
                // rowid

//             case SQLT_BIN:
//             case SQLT_LBI:
                // raw/bin

//             case SQLT_RSET:
                // resultset

//             case SQLT_CUR:
                // cusror

            case SQLT_CLOB:
            case SQLT_BLOB: {
                qore_type_t ntype = val.getType();
                if (ntype == NT_BINARY) {
                    const BinaryNode * bn = val.get<const BinaryNode>();
                    unsigned int size = bn->size();
                    if (size) {
                        OCI_Lob* l = OCI_LobCreate2(&d->ocilib, d->ocilib_cn, OCI_BLOB, xsink);
                        if (!l) {
                            if (!*xsink)
                                xsink->raiseException("BIND-NTY-ERROR", "failed to create BLOB for binding to NTY object attribute %s.%s", tname, cname);
                            return nullptr;
                        }
                        ON_BLOCK_EXIT(OCI_LobFree, &d->ocilib, l, xsink);
                        if (!OCI_LobWrite2(&d->ocilib, l, (void*)bn->getPtr(), &size, &size, xsink)) {
                            if (!*xsink)
                                xsink->raiseException("BIND-NTY-ERROR", "failed to write to BLOB for binding to NTY object attribute %s.%s", tname, cname);
                            return nullptr;
                        }
                        if (!OCI_ObjectSetLob2(&d->ocilib, *obj, cname, l, xsink)) {
                            if (!*xsink)
                                xsink->raiseException("BIND-NTY-ERROR", "failed to set BLOB attribute for NTY object %s.%s", tname, cname);
                            return nullptr;
                        }
                        break;
                    }
                }
                else {
                    QoreStringValueHelper str(val, d->ds.getQoreEncoding(), xsink);
                    if (*xsink)
                        return nullptr;
                    unsigned int sizelen = str->strlen();
                    if (sizelen) {
                        // clobs
                        OCI_Lob* l = OCI_LobCreate2(&d->ocilib, d->ocilib_cn, OCI_CLOB, xsink);
                        if (!l) {
                            if (!*xsink)
                                xsink->raiseException("BIND-NTY-ERROR", "failed to create CLOB for binding to NTY object attribute %s.%s", tname, cname);
                            return nullptr;
                        }
                        ON_BLOCK_EXIT(OCI_LobFree, &d->ocilib, l, xsink);
                        unsigned int size = str->length();
                        if (!OCI_LobWrite2(&d->ocilib, l, (void*)str->getBuffer(), &size, &sizelen, xsink)) {
                            if (!*xsink)
                                xsink->raiseException("BIND-NTY-ERROR", "failed to write to CLOB for binding to NTY object attribute %s.%s", tname, cname);
                            return nullptr;
                        }
                        if (!OCI_ObjectSetLob2(&d->ocilib, *obj, cname, l, xsink)) {
                            if (!*xsink)
                                xsink->raiseException("BIND-NTY-ERROR", "failed to set BLOB attribute for NTY object %s.%s", tname, cname);
                            return nullptr;
                        }
                        break;
                    }
                }
                if (!OCI_ObjectSetNull2(&d->ocilib, *obj, cname, xsink)) {
                   if (!*xsink)
                      xsink->raiseException("BIND-NTY-ERROR", "unable to set object attribute %s.%s to NULL", tname, cname);
                   return 0;
                }
                break;
            }

//             case SQLT_BFILE:
                // bfile

//             case SQLT_CFILE:
                // cfile

//             case SQLT_REF:
                // ref

#if OCI_VERSION_COMPILE >= OCI_9_0
            case SQLT_PNTY:
#endif
            case SQLT_NTY: {
                const QoreHashNode* n = val.getType() == NT_HASH ? val.get<const QoreHashNode>() : nullptr;
                std::string t = ntyHashType(n);
                if (t == ORACLE_OBJECT) {
                    ObjectHolder o(&d->ocilib, objBindQore(d, n, xsink), xsink);
                    if (!o) {
                        assert(*xsink);
                        return nullptr;
                    }
                    if (!OCI_ObjectSetObject2(&d->ocilib, *obj, cname, *o, xsink)) {
                        if (!*xsink)
                            xsink->raiseException("BIND-NTY-ERROR", "unable to bind object of type '%s' to attribute %s.%s",
                                t.c_str(), tname, cname);
                        return nullptr;
                    }
                }
                else if (t == ORACLE_COLLECTION) {
                    CollHolder o(&d->ocilib, collBindQore(d, n, xsink), xsink);
                    if (!o) {
                        assert(*xsink);
                        return nullptr;
                    }
                    if (!OCI_ObjectSetColl2(&d->ocilib, *obj, cname, *o, xsink)) {
                        if (!*xsink)
                            xsink->raiseException("BIND-NTY-ERROR", "unable to bind collection of type '%s' to attribute %s.%s", t.c_str(), tname, cname);
                        return nullptr;
                    }
                }
                else {
                    xsink->raiseException("BIND-NTY-ERROR", "Unknown NTY-like argument for %s.%s (type: %d)", tname, cname, col->type);
                    return nullptr;
                }
                if (*xsink)
                    return nullptr;

                break;
            }

            default:
               xsink->raiseException("BIND-NTY-ERROR", "unknown datatype to bind as an attribute (unsupported): %s.%s", tname, cname);
                return 0;
        } // switch
    }

    return obj.release();
}

#define ORA_NUM_BUFSIZE 100
// here we use the special "text minimum" format
#define ORA_NUM_FORMAT "TM"
#define ORA_NUM_FORMAT_SIZE (sizeof(ORA_NUM_FORMAT)-1)

const char* get_typinf_schema(OCI_Object* obj) {
    if (!obj)
        return "unknown object";
    if (!obj->typinf)
        return "unknown type info";
    if (!obj->typinf->schema || !strcmp(obj->typinf->schema, ""))
        return "<current schema>";
    return obj->typinf->schema;
}

const char* get_typinf_name(OCI_Object* obj) {
    if (!obj)
        return "unknown object";
    if (!obj->typinf)
        return "unknown type info";
    if (!obj->typinf->name)
        return "unknown name";
    return obj->typinf->name;
}

QoreHashNode* objToQore(QoreOracleConnection* conn, OCI_Object* obj, ExceptionSink* xsink) {
    ReferenceHolder<QoreHashNode> rv(new QoreHashNode(autoTypeInfo), xsink);

    int n = OCI_TypeInfoGetColumnCount2(&conn->ocilib, obj->typinf);
    if (!n) {
        xsink->raiseException("FETCH-NTY-ERROR", "unable to retrieve attribute info for object: %s.%s", get_typinf_schema(obj), get_typinf_name(obj));
        return nullptr;
    }
    for (int i = 1; i <= n; ++i) {
        OCI_Column* col = OCI_TypeInfoGetColumn2(&conn->ocilib, obj->typinf, i, xsink);
        if (!col) {
            if (!*xsink)
                xsink->raiseException("FETCH-NTY-ERROR", "Object of type %s.%s failed to retrieve column information for column %d", get_typinf_schema(obj), get_typinf_name(obj), i);
            return nullptr;
        }

        const char* cname = OCI_ColumnGetName2(&conn->ocilib, col, xsink);
        if (!cname) {
            if (!*xsink)
                xsink->raiseException("FETCH-NTY-ERROR", "Object of type %s.%s failed to retrieve column name for column %d (%s)", get_typinf_schema(obj), get_typinf_name(obj), i, col->name);
            return nullptr;
        }

        {
            bool b = OCI_ObjectIsNull2(&conn->ocilib, obj, cname, xsink);
            if (*xsink)
                return nullptr;
            if (b) {
                rv->setKeyValue(cname, null(), xsink);
                continue;
            }
        }

        switch (col->ocode) {
            case SQLT_LNG: // long
            case SQLT_AFC:
            case SQLT_AVC:
            case SQLT_STR:
            case SQLT_CHR: {
                // strings
                const char* str = OCI_ObjectGetString2(&conn->ocilib, obj, cname, xsink);
                if (!str) {
                    if (!*xsink)
                        xsink->raiseException("FETCH-NTY-ERROR", "Object of type %s.%s failed to retrieve string value for attribute '%s'", get_typinf_schema(obj), get_typinf_name(obj), cname);
                    return nullptr;
                }
                rv->setKeyValue(cname, new QoreStringNode(str, conn->ds.getQoreEncoding()), xsink);
                break;
            }

            case SQLT_NUM: {
                int index = OCI_ObjectGetAttrIndex2(&conn->ocilib, obj, cname, OCI_CDT_NUMERIC, xsink);
                if (index < 0) {
                    if (!*xsink)
                        xsink->raiseException("FETCH-NTY-ERROR", "Object of type %s.%s failed to retrieve object attribute index for attribute'%s'", get_typinf_schema(obj), get_typinf_name(obj), cname);
                    return nullptr;
                }
                OCIInd* ind = 0;
                OCINumber* num = (OCINumber*)OCI_ObjectGetAttr(obj, index, &ind);
                if (!num || *ind == OCI_IND_NULL) {
                    rv->setKeyValue(cname, &Null, xsink);
                    break;
                }

                char buf[ORA_NUM_BUFSIZE + 1];
                ub4 bs = ORA_NUM_BUFSIZE;
                if (conn->checkerr(OCINumberToText(conn->errhp, num, (const OraText*)ORA_NUM_FORMAT, ORA_NUM_FORMAT_SIZE, 0, 0, &bs, (OraText*)buf),
                                "objToQore() converting NUMERIC value to text", xsink))
                    return nullptr;
                buf[bs] = 0;

                // convert oracle numeric value to Qore value according to connection options
                QoreValue nv;
                int nopt = conn->getNumberOption();
                switch (nopt) {
                    case OPT_NUM_OPTIMAL:
                        nv = conn->getNumberOptimal(buf);
                        break;
                    case OPT_NUM_STRING:
                        nv = new QoreStringNode(buf, conn->ds.getQoreEncoding());
                        break;
                    case OPT_NUM_NUMERIC:
                        nv = new QoreNumberNode(buf);
                        break;
                    default:
                        assert(false);
                }
                rv->setKeyValue(cname, nv, xsink);
                break;
            }

            case SQLT_INT: {
                int64 i = OCI_ObjectGetBigInt2(&conn->ocilib, obj, cname, xsink);
                if (*xsink)
                    return nullptr;
                //printd(5, "objToQore() i: %lld\n", i);
                rv->setKeyValue(cname, i, xsink);
                break;
            }

            case SQLT_FLT:
    #if OCI_VERSION_COMPILE >= OCI_10_1
            case SQLT_BFLOAT:
            case SQLT_IBFLOAT:
            case SQLT_BDOUBLE:
            case SQLT_IBDOUBLE:
    #endif
            {
                // float
                double d = OCI_ObjectGetDouble2(&conn->ocilib, obj, cname, xsink);
                if (*xsink)
                    return nullptr;
                rv->setKeyValue(cname, d, xsink);
                break;
            }

            case SQLT_DAT:
            case SQLT_ODT:
            case SQLT_DATE:
    #if OCI_VERSION_COMPILE >= OCI_9_0
            case SQLT_TIMESTAMP:
            case SQLT_TIMESTAMP_TZ:
            case SQLT_TIMESTAMP_LTZ:
            case SQLT_INTERVAL_YM:
            case SQLT_INTERVAL_DS:
    #endif
            {
                // timestamps-like dates
                if (col->type == OCI_CDT_TIMESTAMP) {
                    OCI_Timestamp *dt = OCI_ObjectGetTimestamp2(&conn->ocilib, obj, cname, xsink);
                    if (!dt) {
                        if (!*xsink)
                            xsink->raiseException("FETCH-NTY-ERROR", "Object of type %s.%s failed to retrieve timestamp attribute '%s'", get_typinf_schema(obj), get_typinf_name(obj), cname);
                        return nullptr;
                    }
                    // only SQLT_TIMESTAMP gets the default TZ
                    rv->setKeyValue(cname, conn->getTimestamp(col->ocode != SQLT_TIMESTAMP, dt->handle, xsink), xsink);
                }
                // pure DATE like
                else if (col->type == OCI_CDT_DATETIME) {
                    OCI_Date* dt = OCI_ObjectGetDate2(&conn->ocilib, obj, cname, xsink);
                    if (!dt) {
                        if (!*xsink)
                            xsink->raiseException("FETCH-NTY-ERROR", "Object of type %s.%s failed to retrieve date attribute '%s'", get_typinf_schema(obj), get_typinf_name(obj), cname);
                        return nullptr;
                    }
                    DateTimeNode* dn = conn->getDate(dt->handle);
                    rv->setKeyValue(cname, dn, xsink);
                }
                // intervals
                else if (col->type == OCI_CDT_INTERVAL) {
                    OCI_Interval* dt = OCI_ObjectGetInterval2(&conn->ocilib, obj, cname, xsink);
                    if (!dt) {
                        if (!*xsink)
                            xsink->raiseException("FETCH-NTY-ERROR", "Object of type %s.%s failed to retrieve date attribute '%s'", get_typinf_schema(obj), get_typinf_name(obj), cname);
                        return nullptr;
                    }

                    if (col->ocode == SQLT_INTERVAL_YM) {
                        int y, m;
                        if (!OCI_IntervalGetYearMonth2(&conn->ocilib, dt, &y, &m, xsink)) {
                            if (!*xsink)
                                xsink->raiseException("FETCH-NTY-ERROR", "Object of type %s.%s failed to retrieve year/month interval data for attribute '%s'", get_typinf_schema(obj), get_typinf_name(obj), cname);
                            return nullptr;
                        }
                        rv->setKeyValue(cname, new DateTimeNode(y, m, 0, 0, 0, 0, 0, true), xsink);
                    }
                    else  {
                        // SQLT_INTERVAL_DS
                        int d, h, mi, s, fs;
                        if (!OCI_IntervalGetDaySecond2(&conn->ocilib, dt, &d, &h, &mi, &s, &fs, xsink)) {
                            if (!*xsink)
                                xsink->raiseException("FETCH-NTY-ERROR", "Object of type %s.%s failed to retrieve day/second interval data for attribute '%s'", get_typinf_schema(obj), get_typinf_name(obj), cname);
                            return nullptr;
                        }
                        rv->setKeyValue(cname, DateTimeNode::makeRelative(0, 0, d, h, mi, s, fs / 1000), xsink);
                    }
                }
                else {
                    xsink->raiseException("FETCH-NTY-ERROR", "Object of type %s.%s Unknown DATE-like argument for %s (type: %d)", get_typinf_schema(obj), get_typinf_name(obj), cname, col->type);
                    return nullptr;
                }
                break;
            }

//             case SQLT_RDD:
//             case SQLT_RID:
            // rowid

//             case SQLT_BIN:
//             case SQLT_LBI:
            // raw/bin

//             case SQLT_RSET:
            // resultset

//             case SQLT_CUR:
            // cusror

            case SQLT_CLOB:
            case SQLT_BLOB: {
                OCI_Lob * l = OCI_ObjectGetLob2(&conn->ocilib, obj, cname, xsink);
                if (!l) {
                    if (!*xsink)
                        xsink->raiseException("FETCH-NTY-ERROR", "Object of type %s.%s failed to retrieve LOB attribute '%s'", get_typinf_schema(obj), get_typinf_name(obj), cname);
                    return nullptr;
                }
                ON_BLOCK_EXIT(OCI_LobFree, &conn->ocilib, l, xsink);

                // The returned value is in bytes for BLOBS and characters for CLOBS/NCLOBs

                int64 tl = (int64)OCI_LobGetLength(&conn->ocilib, l, xsink);
                if (!tl) {
                    if (!*xsink)
                        xsink->raiseException("FETCH-NTY-ERROR", "Object of type %s.%s failed to retrieve length for LOB attribute '%s'", get_typinf_schema(obj), get_typinf_name(obj), cname);
                    return nullptr;
                }
                if (tl >= (1ll << 32)) {
                    xsink->raiseException("FETCH-NTY-ERROR", "Object of type %s.%s cannot use OCILib LOB APIs to read a LOB of size " QLLD, get_typinf_schema(obj), get_typinf_name(obj), tl);
                    return nullptr;
                }

                const QoreEncoding* enc = conn->ds.getQoreEncoding();
                bool is_clob = col->ocode == SQLT_CLOB;
                unsigned int char_len = (unsigned int)tl;
                unsigned int byte_len = is_clob ? char_len * enc->getMaxCharWidth() : char_len;

                SimpleRefHolder<BinaryNode> b(new BinaryNode);
                if (b->preallocate(byte_len)) {
                    xsink->raiseException("FETCH-NTY-ERROR", "Object of type %s.%s failed to allocate %d bytes for LOB attribute '%s'", get_typinf_schema(obj), get_typinf_name(obj), byte_len, cname);
                    return nullptr;
                }

                if (!OCI_LobRead2(&conn->ocilib, l, (void*)b->getPtr(), &char_len, &byte_len, xsink)) {
                    if (!*xsink)
                        xsink->raiseException("FETCH-NTY-ERROR", "Object of type %s.%s failed to fetch %d bytes for LOB attribute '%s'", get_typinf_schema(obj), get_typinf_name(obj), byte_len, cname);
                    return nullptr;
                }

                if (is_clob)
                    rv->setKeyValue(cname, new QoreStringNode((char*)b->giveBuffer(), byte_len, byte_len, enc), xsink);
                else
                    rv->setKeyValue(cname, b.release(), xsink);

                break;
            }

//             case SQLT_BFILE:
                // bfile

//             case SQLT_CFILE:
                // cfile

//             case SQLT_REF:
                // ref

#if OCI_VERSION_COMPILE >= OCI_9_0
            case SQLT_PNTY:
#endif
            case SQLT_NTY:
                if (col->typinf->ccode) {
                    // collection
                    OCI_Coll* c = OCI_ObjectGetColl2(&conn->ocilib, obj, cname, xsink);
                    if (!c) {
                        if (!*xsink)
                            xsink->raiseException("FETCH-NTY-ERROR", "Object of type %s.%s failed to fetch collection attribute '%s'", get_typinf_schema(obj), get_typinf_name(obj), cname);
                        return nullptr;
                    }
                    QoreListNode* n = collToQore(conn, c, xsink);
                    if (*xsink) {
                        assert(!n);
                        return nullptr;
                    }
                    rv->setKeyValue(cname, n, xsink);
                } else {
                    // object
                    OCI_Object* o = OCI_ObjectGetObject2(&conn->ocilib, obj, cname, xsink);
                    if (!o) {
                        if (!*xsink)
                            xsink->raiseException("FETCH-NTY-ERROR", "Object of type %s.%s failed to fetch object attribute '%s'", get_typinf_schema(obj), get_typinf_name(obj), cname);
                        return nullptr;
                    }
                    QoreHashNode* n = objToQore(conn, o, xsink);
                    if (*xsink) {
                        assert(!n);
                        return nullptr;
                    }
                    rv->setKeyValue(cname, n, xsink);
                }
                break;

            default:
                xsink->raiseException("FETCH-NTY-ERROR", "Object of type %s.%s unknown datatype to fetch as an attribute (unsupported): %s", get_typinf_schema(obj), get_typinf_name(obj), col->typinf->name);
                return nullptr;
        } // switch
    }

    return rv.release();
}

OCI_Coll* collBindQore(QoreOracleConnection * d, const QoreHashNode * h, ExceptionSink * xsink) {
//     QoreNodeAsStringHelper str(h, 1, xsink);
//     printf("list= %s\n", str->getBuffer());

   if (!ntyCheckType(ORACLE_COLLECTION, h, NT_LIST)) {
      xsink->raiseException("BIND-ORACLE-COLLECTION-ERROR",
                            "Bind value was not created with bindOracleCollection()");
      return 0;
   }

   const QoreListNode* th = h->getKeyValue("^values^").get<const QoreListNode>();
   QoreStringValueHelper tname_holder(h->getKeyValue("^oratype^"));
   const char* tname = tname_holder->c_str();

   OCI_TypeInfo * info = OCI_TypeInfoGet2(&d->ocilib, d->ocilib_cn, tname, OCI_TIF_TYPE, xsink);
   if (!info) {
      if (!*xsink)
         xsink->raiseException("BIND-NTY-ERROR",
                               "No type '%s' defined in the DB while attempting to bind a collection",
                               tname);
      return 0;
   }

//     printf("onfo: %d\n", info->nb_cols);
   CollHolder obj(&d->ocilib, OCI_CollCreate2(&d->ocilib, info, xsink), xsink);
   if (!obj) {
      if (!*xsink)
         xsink->raiseException("BIND-NTY-ERROR", "unable to create collection of type '%s' for bind", tname);
      return 0;
   }
   OCI_Column *col = OCI_TypeInfoGetColumn2(&d->ocilib, info, 1, xsink);
   if (!col) {
      if (!*xsink)
         xsink->raiseException("BIND-NTY-ERROR", "failed to retrieve column information for column %d while attempting to bind collection of type '%s'", 1, tname);
      return 0;
   }

//     const char * cname = OCI_GetColumnName(col);
//     printf("Binding attribute: %s\n", cname);
   OCI_Elem * e = OCI_ElemCreate2(&d->ocilib, info, xsink);
   if (!e) {
      if (!*xsink)
         xsink->raiseException("BIND-NTY-ERROR", "failed to create element for binding collection '%s'", tname);
      return 0;
   }
   ON_BLOCK_EXIT(OCI_ElemFree2, &d->ocilib, e, xsink);

   for (size_t i = 0; i < th->size(); ++i) {
      QoreValue val = th->retrieveEntry(i);

      if (val.isNullOrNothing()) {
         if (!OCI_ElemSetNull2(&d->ocilib, e)) {
            xsink->raiseException("BIND-NTY-ERROR", "failed to set element to NULL for collection '%s'", tname);
            return 0;
         }
         if (!OCI_CollAppend2(&d->ocilib, *obj, e, xsink)) {
            if (!*xsink)
               xsink->raiseException("BIND-NTY-ERROR", "failed to append NULL element to collection '%s'", tname);
            return 0;
         }
         continue;
      }

      switch (col->ocode) {
         case SQLT_LNG: // long
         case SQLT_AFC:
         case SQLT_AVC:
         case SQLT_STR:
         case SQLT_CHR: {
            // strings
            QoreStringValueHelper str(val, d->ds.getQoreEncoding(), xsink);
            if (*xsink)
               return 0;
//                 printf("val str: %s\n", str->getBuffer());
            if (str->empty()) {
               if (!OCI_ElemSetNull2(&d->ocilib, e)) {
                  xsink->raiseException("BIND-NTY-ERROR", "failed to set element to NULL for collection '%s'", tname);
                  return 0;
               }
            }
            else {
               if (!OCI_ElemSetString2(&d->ocilib, e, str->getBuffer(), xsink)) {
                  if (!*xsink)
                     xsink->raiseException("BIND-NTY-ERROR", "failed to assign string value to element for collection '%s'", tname);
                  return 0;
               }
               //printf("OCI_ElemSetString %s\n", OCI_ElemGetString(e));
            }
            break;
         }

         case SQLT_NUM:
            switch (val.getType()) {
               case NT_STRING: {
                  QoreStringValueHelper str(val);
                  if (!OCI_ElemSetNumberFromString(&d->ocilib, e, str->getBuffer(), (int)str->size(), xsink)) {
                     if (!*xsink)
                        xsink->raiseException("BIND-NTY-ERROR", "NUMBER: unable to assign a string value to element for collection '%s'", tname);
                     return 0;
                  }
                  break;
               }
               case NT_FLOAT: {
                  if (!OCI_ElemSetDouble(&d->ocilib, e, val.getAsFloat(), xsink)) {
                     if (!*xsink)
                        xsink->raiseException("BIND-NTY-ERROR", "NUMBER: unable to assign a float value to element for collection '%s'", tname);
                     return 0;
                  }
                  break;
               }
               case NT_INT: {
                  if (!OCI_ElemSetBigInt(&d->ocilib, e, val.getAsBigInt(), xsink)) {
                     if (!*xsink)
                        xsink->raiseException("BIND-NTY-ERROR", "NUMBER: unable to assign a value to integer element for collection '%s'", tname);
                     return 0;
                  }
                  break;
               }
               case NT_NUMBER: {
                  const QoreNumberNode* n = val.get<const QoreNumberNode>();
                  QoreString str;
                  n->getStringRepresentation(str);
                  if (!OCI_ElemSetNumberFromString(&d->ocilib, e, str.getBuffer(), (int)str.size(), xsink)) {
                     if (!*xsink)
                        xsink->raiseException("BIND-NTY-ERROR", "NUMBER: unable to assign an arbitrary-precision number value to element for collection '%s'", tname);
                     return 0;
                  }
                  break;
               }
               default:
                  xsink->raiseException("BIND-NTY-ERROR", "NUMBER: unable to bind value of Qore type: '%s' element for collection '%s'", val.getTypeName(), tname);
                  return 0;
                  break;
            }
            break;

         case SQLT_INT:
            if (!OCI_ElemSetBigInt(&d->ocilib, e, val.getAsBigInt(), xsink)) {
               if (!*xsink)
                  xsink->raiseException("BIND-NTY-ERROR", "failed to assign integer value to element for collection '%s'", tname);
               return 0;
            }
            break;

         case SQLT_FLT:
#if OCI_VERSION_COMPILE >= OCI_10_1
         case SQLT_BFLOAT:
         case SQLT_IBFLOAT:
         case SQLT_BDOUBLE:
         case SQLT_IBDOUBLE:
#endif
            // float
            if (!OCI_ElemSetDouble(&d->ocilib, e, val.getAsFloat(), xsink)) {
               if (!*xsink)
                  xsink->raiseException("BIND-NTY-ERROR", "failed to assign double-precision value to element for collection '%s'", tname);
               return 0;
            }
            break;


         case SQLT_DAT:
         case SQLT_ODT:
         case SQLT_DATE:
#if OCI_VERSION_COMPILE >= OCI_9_0
         case SQLT_TIMESTAMP:
         case SQLT_TIMESTAMP_TZ:
         case SQLT_TIMESTAMP_LTZ:
         case SQLT_INTERVAL_YM:
         case SQLT_INTERVAL_DS:
#endif
         {
            // we have to bind date node here
            DateTimeValueHelper dn(val);
            if (col->type == OCI_CDT_TIMESTAMP) {
               OCI_Timestamp* dt = OCI_TimestampCreate2(&d->ocilib, d->ocilib_cn, col->subtype, xsink);
               if (!dt) {
                  if (!*xsink)
                     xsink->raiseException("BIND-NTY-ERROR", "failed to create timestamp object for collection '%s'", tname);
                  return 0;
               }
               ON_BLOCK_EXIT(OCI_TimestampFree2, &d->ocilib, dt);
               if (!OCI_TimestampConstruct2(&d->ocilib, dt,
                                            dn->getYear(), dn->getMonth(), dn->getDay(),
                                            dn->getHour(), dn->getMinute(), dn->getSecond(),
                                            (dn->getMicrosecond() * 1000),
                                            0, xsink)) {
                  if (!*xsink)
                     xsink->raiseException("BIND-NTY-ERROR", "failed to create timestamp from date/time value for collection '%s'", tname);
                  return 0;
               }
               if (!OCI_ElemSetTimestamp2(&d->ocilib, e, dt, xsink)) {
                  if (!*xsink)
                     xsink->raiseException("BIND-NTY-ERROR", "failed to set timestamp attribute for collection '%s'", tname);
                  return 0;
               }
            }
            else if (col->type == OCI_CDT_DATETIME) {
               OCI_Date * dt = OCI_DateCreate(&d->ocilib, d->ocilib_cn);
               if (!dt) {
                  xsink->raiseException("BIND-NTY-ERROR", "failed to create date/time object for collection %s", tname);
                  return 0;
               }
               ON_BLOCK_EXIT(OCI_DateFree, &d->ocilib, dt);
               if (!OCI_DateSetDateTime(&d->ocilib, dt,
                                        dn->getYear(), dn->getMonth(), dn->getDay(),
                                        dn->getHour(), dn->getMinute(), dn->getSecond())) {
                  if (!*xsink)
                     xsink->raiseException("BIND-NTY-ERROR", "failed to create date/time value for collection '%s'", tname);
                  return 0;
               }
               if (!OCI_ElemSetDate2(&d->ocilib, e, dt, xsink)) {
                  if (!*xsink)
                     xsink->raiseException("BIND-NTY-ERROR", "failed to set date/time attribute for collection '%s'", tname);
                  return 0;
               }
            }
            // intervals
            else if (col->type == OCI_CDT_INTERVAL) {
               if (col->ocode == SQLT_INTERVAL_YM) {
                  OCI_Interval* dt = OCI_IntervalCreate2(&d->ocilib, d->ocilib_cn, OCI_INTERVAL_YM, xsink);
                  if (!dt) {
                     if (!*xsink)
                        xsink->raiseException("BIND-NTY-ERROR", "failed to create interval object for collection '%s'", tname);
                     return 0;
                  }
                  ON_BLOCK_EXIT(OCI_IntervalFree2, &d->ocilib, dt);
                  if (!OCI_IntervalSetYearMonth2(&d->ocilib, dt, dn->getYear(), dn->getMonth(), xsink)) {
                     if (!*xsink)
                        xsink->raiseException("BIND-NTY-ERROR", "failed to set interval year/month value for collection '%s'", tname);
                     return 0;
                  }
                  if (!OCI_ElemSetInterval2(&d->ocilib, e, dt, xsink)) {
                     if (!*xsink)
                        xsink->raiseException("BIND-NTY-ERROR", "failed to set interval value for collection '%s'", tname);
                     return 0;
                  }
               }
               else {
                  // SQLT_INTERVAL_DS
                  OCI_Interval* dt = OCI_IntervalCreate2(&d->ocilib, d->ocilib_cn, OCI_INTERVAL_DS, xsink);
                  if (!dt) {
                     if (!*xsink)
                        xsink->raiseException("BIND-NTY-ERROR", "failed to create interval object for collection '%s'", tname);
                     return 0;
                  }
                  ON_BLOCK_EXIT(OCI_IntervalFree2, &d->ocilib, dt);
                  if (!OCI_IntervalSetDaySecond2(&d->ocilib, dt,
                                                 dn->getDay(),
                                                 dn->getHour(), dn->getMinute(), dn->getSecond(),
                                                 (dn->getMicrosecond() * 1000), xsink)) {
                     if (!*xsink)
                        xsink->raiseException("BIND-NTY-ERROR", "failed to set interval day/second value for collection '%s'", tname);
                     return 0;
                  }
                  if (!OCI_ElemSetInterval2(&d->ocilib, e, dt, xsink)) {
                     if (!*xsink)
                        xsink->raiseException("BIND-NTY-ERROR", "failed to set interval value for collection '%s'", tname);
                     return 0;
                  }
               }
            }
            else {
               xsink->raiseException("BIND-NTY-ERROR", "Unknown DATE-like argument (type: %d)", col->type);
               return 0;
            }
            break;
         }


//             case SQLT_RDD:
//             case SQLT_RID:
         // rowid

//             case SQLT_BIN:
//             case SQLT_LBI:
         // raw/bin

//             case SQLT_RSET:
         // resultset

//             case SQLT_CUR:
         // cusror

         case SQLT_CLOB:
         case SQLT_BLOB: {
            qore_type_t ntype = val.getType();
            if (ntype == NT_BINARY) {
               const BinaryNode * bn = val.get<const BinaryNode>();
               unsigned int size = bn->size();
               if (size) {
                  OCI_Lob* l = OCI_LobCreate2(&d->ocilib, d->ocilib_cn, OCI_BLOB, xsink);
                  if (!l) {
                     if (!*xsink)
                        xsink->raiseException("BIND-NTY-ERROR", "failed to create BLOB for binding to collection '%s'", tname);
                     return 0;
                  }
                  ON_BLOCK_EXIT(OCI_LobFree, &d->ocilib, l, xsink);
                  if (!OCI_LobWrite2(&d->ocilib, l, (void*)bn->getPtr(), &size, &size, xsink)) {
                     if (!*xsink)
                        xsink->raiseException("BIND-NTY-ERROR", "failed to write to BLOB for binding to collection '%s'", tname);
                     return 0;
                  }
                  if (!OCI_ElemSetLob2(&d->ocilib, e, l, xsink)) {
                     if (!*xsink)
                        xsink->raiseException("BIND-NTY-ERROR", "failed to set BLOB attribute for collection '%s'", tname);
                     return 0;
                  }
                  break;
               }
            }
            else {
               QoreStringValueHelper str(val, d->ds.getQoreEncoding(), xsink);
               if (*xsink)
                  return 0;
               unsigned int sizelen = str->strlen();
               if (sizelen) {
                  // clobs
                  OCI_Lob* l = OCI_LobCreate2(&d->ocilib, d->ocilib_cn, OCI_CLOB, xsink);
                  if (!l) {
                     if (!*xsink)
                        xsink->raiseException("BIND-NTY-ERROR", "failed to create CLOB for binding to collection '%s'", tname);
                     return 0;
                  }
                  ON_BLOCK_EXIT(OCI_LobFree, &d->ocilib, l, xsink);
                  unsigned int size = str->length();
                  if (!OCI_LobWrite2(&d->ocilib, l, (void*)str->getBuffer(), &size, &sizelen, xsink)) {
                     if (!*xsink)
                        xsink->raiseException("BIND-NTY-ERROR", "failed to write to CLOB for binding to collection '%s'", tname);
                     return 0;
                  }
                  if (!OCI_ElemSetLob2(&d->ocilib, e, l, xsink)) {
                     if (!*xsink)
                        xsink->raiseException("BIND-NTY-ERROR", "failed to set CLOB attribute for collection '%s'", tname);
                     return 0;
                  }
                  break;
               }
            }
            if (!OCI_ElemSetNull2(&d->ocilib, e)) {
               xsink->raiseException("BIND-NTY-ERROR", "unable to set LOB element to NULL for collection '%s'", tname);
               return 0;
            }
            break;
         }

//             case SQLT_BFILE:
            // bfile

//             case SQLT_CFILE:
            // cfile

//             case SQLT_REF:
            // ref

#if OCI_VERSION_COMPILE >= OCI_9_0
         case SQLT_PNTY:
#endif
         case SQLT_NTY: {
            const QoreHashNode * n = val.getType() == NT_HASH ? val.get<const QoreHashNode>() : nullptr;
            std::string t = ntyHashType(n);
            if (t == ORACLE_OBJECT) {
               ObjectHolder o(&d->ocilib, objBindQore(d, n, xsink), xsink);
               if (!o) {
                  assert(*xsink);
                  return 0;
               }
	               if (!OCI_ElemSetObject2(&d->ocilib, e, *o, xsink)) {
	                  if (!*xsink)
	                     xsink->raiseException("BIND-NTY-ERROR", "unable to bind object of type '%s' to element",
	                        t.c_str());
	                  return 0;
	               }
	            }
	            else if (t == ORACLE_COLLECTION) {
               CollHolder o(&d->ocilib, collBindQore(d, n, xsink), xsink);
               if (!o) {
                  assert(*xsink);
                  return 0;
               }
               if (!OCI_ElemSetColl2(&d->ocilib, e, *o, xsink)) {
                  if (!*xsink)
                     xsink->raiseException("BIND-NTY-ERROR", "unable to bind collection of type '%s' to element", t.c_str());
                  return 0;
               }
            }
            else {
               xsink->raiseException("BIND-NTY-ERROR", "Unknown NTY-like argument (type: %d)", col->type);
               return 0;
            }
            if (*xsink)
               return 0;

            break;
         }

         default:
            xsink->raiseException("BIND-COLLECTION-ERROR", "unknown datatype to bind as an attribute (unsupported): %s", col->typinf->name);
            return 0;
      } // switch

      if (!OCI_CollAppend2(&d->ocilib, *obj, e, xsink)) {
         if (!*xsink)
            xsink->raiseException("BIND-NTY-ERROR", "failed to append element to collection '%s'", tname);
         return 0;
      }
   }

   return obj.release();
}

const char* get_typinf_schema(OCI_Coll* obj) {
    if (!obj)
        return "unknown collection";
    if (!obj->typinf)
        return "unknown type info";
    if (!obj->typinf->schema || !strcmp(obj->typinf->schema, ""))
        return "<current schema>";
    return obj->typinf->schema;
}

const char* get_typinf_name(OCI_Coll* obj) {
    if (!obj)
        return "unknown collection";
    if (!obj->typinf)
        return "unknown type info";
    if (!obj->typinf->name)
        return "unknown name";
    return obj->typinf->name;
}

QoreListNode* collToQore(QoreOracleConnection* conn, OCI_Coll* obj, ExceptionSink *xsink) {
    ReferenceHolder<QoreListNode> rv(new QoreListNode(autoTypeInfo), xsink);

    int count = OCI_CollGetSize2(&conn->ocilib, obj, xsink);
    if (*xsink)
        return nullptr;
    OCI_Column *col = OCI_TypeInfoGetColumn2(&conn->ocilib, obj->typinf, 1, xsink);
    if (!col) {
        if (!*xsink)
            xsink->raiseException("FETCH-NTY-ERROR", "Collection %s.%s failed to retrieve column type information for column %d", get_typinf_schema(obj), get_typinf_name(obj), 1);
        return nullptr;
    }

    for (int i = 1; i <= count; ++i) {
        OCI_Elem* e = OCI_CollGetAt2(&conn->ocilib, obj, i, xsink);
        if (!e) {
            if (!*xsink)
                xsink->raiseException("FETCH-NTY-ERROR", "Collection %s.%s failed to retrieve collection element %d", get_typinf_schema(obj), get_typinf_name(obj), i);
            return nullptr;
        }

        if (OCI_ElemIsNull2(&conn->ocilib, e)) {
            rv->setEntry(rv->size(), null(), xsink);
            continue;
        }

        switch (col->ocode) {
            case SQLT_LNG: // long
            case SQLT_AFC:
            case SQLT_AVC:
            case SQLT_STR:
            case SQLT_CHR: {
                // strings
                const char* str = OCI_ElemGetString2(&conn->ocilib, e);
                if (!str) {
                    xsink->raiseException("FETCH-NTY-ERROR", "Collection %s.%s failed to retrieve string value for collection element %d", get_typinf_schema(obj), get_typinf_name(obj), i);
                    return nullptr;
                }
                rv->setEntry(rv->size(), new QoreStringNode(str, conn->ds.getQoreEncoding()), xsink);
                break;
            }

            case SQLT_NUM:
                if (col->scale == -127 && col->prec > 0) {
                    double d = OCI_ElemGetDouble2(&conn->ocilib, e, xsink);
                    if (*xsink)
                        return nullptr;
                    // float
                    rv->setEntry(rv->size(), d, xsink);
                    break;
                }
                // else fall through to SQLT_INT

            case SQLT_INT: {
                int64 i = OCI_ElemGetBigInt2(&conn->ocilib, e, xsink);
                if (*xsink)
                    return nullptr;
                //printd(5, "collToQore() i: %lld\n", i);
                rv->setEntry(rv->size(), i, xsink);
                break;
            }

            case SQLT_FLT:
#if OCI_VERSION_COMPILE >= OCI_10_1
            case SQLT_BFLOAT:
            case SQLT_IBFLOAT:
            case SQLT_BDOUBLE:
            case SQLT_IBDOUBLE:
#endif
            {
                double d = OCI_ElemGetDouble2(&conn->ocilib, e, xsink);
                if (*xsink)
                    return nullptr;
                // float
                rv->setEntry(rv->size(), d, xsink);
                break;
            }

            case SQLT_DAT:
            case SQLT_ODT:
            case SQLT_DATE:
#if OCI_VERSION_COMPILE >= OCI_9_0
            case SQLT_TIMESTAMP:
            case SQLT_TIMESTAMP_TZ:
            case SQLT_TIMESTAMP_LTZ:
            case SQLT_INTERVAL_YM:
            case SQLT_INTERVAL_DS:
#endif
            {
                // timestamps-like dates
                if (col->type == OCI_CDT_TIMESTAMP) {
                    OCI_Timestamp* dt = OCI_ElemGetTimestamp2(&conn->ocilib, e);
                    if (!dt) {
                        xsink->raiseException("FETCH-NTY-ERROR", "Collection %s.%s failed to retrieve TIMESTAMP element for collection", get_typinf_schema(obj), get_typinf_name(obj));
                        return nullptr;
                    }

                    // only SQLT_TIMESTAMP gets the default TZ
                    rv->setEntry(rv->size(), conn->getTimestamp(col->ocode != SQLT_TIMESTAMP, dt->handle, xsink), xsink);
                }
                // pure DATE like
                else if (col->type == OCI_CDT_DATETIME) {
                    OCI_Date* dt = OCI_ElemGetDate2(&conn->ocilib, e);
                    if (!dt) {
                        if (!*xsink)
                            xsink->raiseException("FETCH-NTY-ERROR", "Collection %s.%s failed to retrieve DATE element for collection", get_typinf_schema(obj), get_typinf_name(obj));
                        return nullptr;
                    }
                    rv->setEntry(rv->size(), conn->getDate(dt->handle), xsink);
                }
                // intervals
                else if (col->type == OCI_CDT_INTERVAL) {
                    OCI_Interval* dt = OCI_ElemGetInterval2(&conn->ocilib, e);
                    if (!dt) {
                        xsink->raiseException("FETCH-NTY-ERROR", "Collection %s.%s failed to retrieve INTERVAL element for collection", get_typinf_schema(obj), get_typinf_name(obj));
                        return nullptr;
                    }
                    if (col->ocode == SQLT_INTERVAL_YM) {
                        int y, m;
                        if (!OCI_IntervalGetYearMonth2(&conn->ocilib, dt, &y, &m, xsink)) {
                            if (!*xsink)
                                xsink->raiseException("FETCH-NTY-ERROR", "Collection %s.%s failed to retrieve INTERVAL element year/month information for collection", get_typinf_schema(obj), get_typinf_name(obj));
                            return nullptr;
                        }
                        rv->setEntry(rv->size(), new DateTimeNode(y, m, 0, 0, 0, 0, 0, true), xsink);
                    }
                    else  {
                        // SQLT_INTERVAL_DS
                        int d, h, mi, s, fs;
                        if (!OCI_IntervalGetDaySecond2(&conn->ocilib, dt, &d, &h, &mi, &s, &fs, xsink)) {
                            if (!*xsink)
                                xsink->raiseException("FETCH-NTY-ERROR", "Collection %s.%s failed to retrieve INTERVAL element day/second information for collection", get_typinf_schema(obj), get_typinf_name(obj));
                            return nullptr;
                        }
                        rv->setEntry(rv->size(), DateTimeNode::makeRelative(0, 0, d, h, mi, s, fs / 1000), xsink);
                    }
                }
                else {
                    xsink->raiseException("FETCH-NTY-ERROR", "Collection %s.%s Unknown DATE-like argument (code: %d)", get_typinf_schema(obj), get_typinf_name(obj), col->ocode);
                    return 0;
                }
                break;
            }

//             case SQLT_RDD:
//             case SQLT_RID:
         // rowid

//             case SQLT_BIN:
//             case SQLT_LBI:
         // raw/bin

//             case SQLT_RSET:
         // resultset

//             case SQLT_CUR:
         // cusror

            case SQLT_CLOB:
            case SQLT_BLOB: {
                OCI_Lob* l = OCI_ElemGetLob2(&conn->ocilib, e, xsink);
                if (!l) {
                    if (!*xsink)
                        xsink->raiseException("FETCH-NTY-ERROR", "Collection %s.%s failed to retrieve LOB attribute for collection", get_typinf_schema(obj), get_typinf_name(obj));
                    return nullptr;
                }
                ON_BLOCK_EXIT(OCI_LobFree, &conn->ocilib, l, xsink);

                // The returned value is in bytes for BLOBS and characters for CLOBS/NCLOBs

                int64 tl = (int64)OCI_LobGetLength(&conn->ocilib, l, xsink);
                if (!tl) {
                    if (!*xsink)
                        xsink->raiseException("FETCH-NTY-ERROR", "Collection %s.%s failed to retrieve LOB length for collection", get_typinf_schema(obj), get_typinf_name(obj));
                    return 0;
                }
                if (tl >= (1ll << 32)) {
                    xsink->raiseException("FETCH-NTY-ERROR", "Collection %s.%s cannot use OCILib LOB APIs to read a LOB of size " QLLD, get_typinf_schema(obj), get_typinf_name(obj), tl);
                    return nullptr;
                }

                const QoreEncoding* enc = conn->ds.getQoreEncoding();
                bool is_clob = col->ocode == SQLT_CLOB;
                unsigned int char_len = (unsigned int)tl;
                unsigned int byte_len = is_clob ? char_len * enc->getMaxCharWidth() : char_len;

                SimpleRefHolder<BinaryNode> b(new BinaryNode);
                if (b->preallocate(byte_len)) {
                    xsink->raiseException("FETCH-NTY-ERROR", "Collection %s.%s failed to allocate %d bytes for collection", get_typinf_schema(obj), get_typinf_name(obj), byte_len);
                    return nullptr;
                }

                if (!OCI_LobRead2(&conn->ocilib, l, (void*)b->getPtr(), &char_len, &byte_len, xsink)) {
                    if (!*xsink)
                        xsink->raiseException("FETCH-NTY-ERROR", "Collection %s.%s failed to fetch %d bytes for collection", get_typinf_schema(obj), get_typinf_name(obj), byte_len);
                    return nullptr;
                }

                if (is_clob)
                    rv->setEntry(rv->size(), new QoreStringNode((char*)b->giveBuffer(), byte_len, byte_len, enc), xsink);
                else
                    rv->setEntry(rv->size(), b.release(), xsink);

                break;
            }

//             case SQLT_BFILE:
            // bfile

//             case SQLT_CFILE:
            // cfile

//             case SQLT_REF:
            // ref

#if OCI_VERSION_COMPILE >= OCI_9_0
            case SQLT_PNTY:
#endif
            case SQLT_NTY:
                if (col->typinf->ccode) {
                    // collection
                    OCI_Coll* coll = OCI_ElemGetColl2(&conn->ocilib, e, xsink);
                    if (!coll) {
                        if (!*xsink)
                            xsink->raiseException("FETCH-NTY-ERROR", "Collection %s.%s failed to retrieve COLLECTION element", get_typinf_schema(obj), get_typinf_name(obj));
                        return nullptr;
                    }
                    QoreListNode* v = collToQore(conn, coll, xsink);
                    if (*xsink) {
                        assert(!v);
                        return nullptr;
                    }
                    rv->setEntry(rv->size(), v, xsink);
                } else {
                    // object
                    OCI_Object* obj = OCI_ElemGetObject2(&conn->ocilib, e, xsink);
                    if (!obj) {
                        if (!*xsink) {
                            xsink->raiseException("FETCH-NTY-ERROR", "Collection %s.%s failed to retrieve NTY element", get_typinf_schema(obj), get_typinf_name(obj));
                            return nullptr;
                        }
                        return nullptr;
                    }
                    QoreHashNode* v = objToQore(conn, obj, xsink);
                    if (*xsink) {
                        assert(!v);
                        return nullptr;
                    }
                    rv->setEntry(rv->size(), v, xsink);
                }
                break;

            default:
                xsink->raiseException("FETCH-NTY-ERROR", "Collection %s.%s unknown datatype to fetch as an attribute: %d (define SQLT_...)", get_typinf_schema(obj), get_typinf_name(obj), col->ocode);
                return nullptr;
        } // switch

    }

    assert(!*xsink);
    return rv.release();
}

OCI_Coll* collPlaceholderQore(QoreOracleConnection *conn, const char * tname, ExceptionSink *xsink) {
    OCI_TypeInfo* info = OCI_TypeInfoGet2(&conn->ocilib, conn->ocilib_cn, tname, OCI_TIF_TYPE, xsink);
    if (!info) {
        if (!*xsink)
            xsink->raiseException("PLACEHOLDER-ORACLE-COLLECTION-ERROR", "No type '%s' defined in the DB", tname);
        return nullptr;
    }
    OCI_Coll* obj = OCI_CollCreate2(&conn->ocilib, info, xsink);
    if (!obj && !*xsink)
        xsink->raiseException("ORACLE-COLLECTION-ERROR", "could not create placeholder buffer for object type '%s'", tname);
    return obj;
}
