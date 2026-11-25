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
 * $Id: typeinfo.c, v 3.7.0 2010-07-20 17:45 Vincent Rogier $
 * ------------------------------------------------------------------------ */

#include "ocilib_internal.h"

/* the deepest chain of named types that is followed in one description, and the most levels of objects and
   collections in a type: OCI_TypeInfoCheckLayout() and decoding recurse once per level of objects and collections;
   OCI_ObjectGetStructSize() and OCI_ObjectGetIndOffset() recurse once per level of objects held by value; binding
   recurses on the nesting of the bound value, which this limit does not bound, although each type named in the value
   is checked against it when it is described */
#define OCI_MAX_TYPE_DEPTH 128

/* ************************************************************************ *
 *                             PRIVATE FUNCTIONS
 * ************************************************************************ */

/* ------------------------------------------------------------------------ *
 * OCI_TypeInfoClose
 * ------------------------------------------------------------------------ */

boolean OCI_TypeInfoClose(OCI_TypeInfo *typinf)
{
    ub2 i;

    OCI_CHECK(typinf == NULL, FALSE);

    // the columns are not allocated if the description could not be made
    if (typinf->cols != NULL)
    {
        for (i=0; i < typinf->nb_cols; i++)
        {
            OCI_FREE(typinf->cols[i].name);
        }
    }

    OCI_FREE(typinf->cols);
    OCI_FREE(typinf->name);
    OCI_FREE(typinf->schema);
    OCI_FREE(typinf->offsets);

    return TRUE;
}

/* ************************************************************************ *
 *                            PUBLIC FUNCTIONS
 * ************************************************************************ */

/* ------------------------------------------------------------------------ *
 * OCI_TypeInfoGet
 * ------------------------------------------------------------------------ */
/*
OCI_TypeInfo * OCI_API OCI_TypeInfoGet(OCI_Connection *con, const mtext *name,
                                       unsigned int type)
{
   return OCI_TypeInfoGet2(&OCILib, con, name, type);
}
*/

/* ------------------------------------------------------------------------ *
 * OCI_TypeInfoCheckNameLen
 * ------------------------------------------------------------------------ */

/* the schema and the name must each fit in their buffers; a longer one is refused, not cut, as a cut name could be
   the name of another object; returns FALSE (with an exception) if one of them is too long */
static boolean OCI_TypeInfoCheckNameLen(OCI_Library *pOCILib, size_t schema_len, size_t name_len,
                                        ExceptionSink* xsink)
{
    if (schema_len > (size_t) OCI_SIZE_SQL_IDENTIFIER || name_len > (size_t) OCI_SIZE_SQL_IDENTIFIER)
    {
        if (xsink)
        {
            xsink->raiseException("ORACLE-OCI-ERROR", "the %s name (%d bytes) of the given type, table or view "
                "name is longer than the supported maximum of %d bytes",
                schema_len > (size_t) OCI_SIZE_SQL_IDENTIFIER ? "schema" : "object",
                (int) (schema_len > (size_t) OCI_SIZE_SQL_IDENTIFIER ? schema_len : name_len),
                (int) OCI_SIZE_SQL_IDENTIFIER);
        }
        OCI_RESULT(pOCILib, FALSE);
        return FALSE;
    }

    return TRUE;
}

/* ------------------------------------------------------------------------ *
 * OCI_TypeInfoRaiseTooDeep
 * ------------------------------------------------------------------------ */

/* the names are not in the message, as a schema name can be the user name of the connection */
static void OCI_TypeInfoRaiseTooDeep(ExceptionSink* xsink)
{
    if (xsink)
    {
        xsink->raiseException("ORACLE-OCI-ERROR", "a named type is nested too deeply: more than %d levels of named "
            "types", OCI_MAX_TYPE_DEPTH);
    }
}

/* ------------------------------------------------------------------------ *
 * OCI_TypeInfoCheckLayout
 * ------------------------------------------------------------------------ */

/* sets the number of levels of objects and collections in a completely described type; returns FALSE if there are
   more than OCI_MAX_TYPE_DEPTH; references are not followed when values are converted, so they are not counted, and
   Oracle refuses types that hold themselves other than through a reference (ORA-04055); the level check also bounds
   this recursion, and refuses such a cycle if one is ever seen */
static boolean OCI_TypeInfoCheckLayout(OCI_TypeInfo *typinf, unsigned int level)
{
    if (typinf->depth != 0)
    {
        return TRUE;
    }

    if (level > OCI_MAX_TYPE_DEPTH)
    {
        return FALSE;
    }

    unsigned int max_depth = 0;

    for (ub2 i = 0; i < typinf->nb_cols; i++)
    {
        OCI_TypeInfo *child = typinf->cols[i].typinf;

        if (((typinf->cols[i].type == OCI_CDT_OBJECT) || (typinf->cols[i].type == OCI_CDT_COLLECTION))
            && (child != NULL))
        {
            if (OCI_TypeInfoCheckLayout(child, level + 1) == FALSE)
            {
                return FALSE;
            }

            if (child->depth > max_depth)
            {
                max_depth = child->depth;
            }
        }
    }

    if (max_depth >= OCI_MAX_TYPE_DEPTH)
    {
        return FALSE;
    }

    typinf->depth = max_depth + 1;

    return TRUE;
}

/* ------------------------------------------------------------------------ *
 * OCI_TypeInfoFind
 * ------------------------------------------------------------------------ */

/* returns the description of the type, table or view with exactly the given schema and object names (as they are
   stored by Oracle; the schema can be empty for the schema of the connection), from the list of the connection or
   made and added to it; each name is at most OCI_SIZE_SQL_IDENTIFIER bytes; returns NULL if the object cannot be
   described, not always with an exception, so callers must check the return value */
static OCI_TypeInfo * OCI_TypeInfoFind(OCI_Library *pOCILib, OCI_Connection *con, const mtext *obj_schema,
                                       const mtext *obj_name, unsigned int type, ExceptionSink* xsink)
{
    OCI_TypeInfo *typinf = NULL;
    OCI_Item     *item   = NULL;
    OCIDescribe *dschp   = NULL;
    OCIParam *parmh1     = NULL;
    OCIParam *parmh2     = NULL;
    mtext *str           = NULL;
    //int etype            = OCI_DESC_COLUMN;
    int ptype            = 0;
    ub1 item_type        = 0;
    ub4 attr_type        = 0;
    ub4 num_type         = 0;
    boolean res          = TRUE;
    boolean found        = FALSE;
    ub2 i;

    if (type == OCI_TIF_TABLE)
        item_type = OCI_PTYPE_TABLE;
    else if (type == OCI_TIF_VIEW)
        item_type = OCI_PTYPE_VIEW;
    else if (type == OCI_TIF_TYPE)
        item_type = OCI_PTYPE_TYPE;
    else
        return NULL;

    /* first try to find it in list */

    item = con->tinfs->head;

    /* walk along the list to find the type */

    while (item != NULL)
    {
        typinf = (OCI_TypeInfo *) item->data;

        if ((typinf != NULL) && (typinf->type == type))
        {
            /* the names are compared exactly: names that differ only in case (quoted names) are the names of
               different objects */
            if ((mtscmp(typinf->name,   obj_name  ) == 0) &&
                (mtscmp(typinf->schema, obj_schema) == 0))
            {
                found = TRUE;
                break;
            }
        }

        item = item->next;
    }

    /* Not found, so create type object */

    if (found == FALSE)
    {
        item = OCI_ListAppend(pOCILib, con->tinfs, sizeof(OCI_TypeInfo));

        res = (item != NULL);

        if (res == TRUE)
        {
            typinf = (OCI_TypeInfo *) item->data;

            typinf->type        = type;
            typinf->con         = con;
            typinf->name        = mtsdup(pOCILib, obj_name);
            typinf->schema      = mtsdup(pOCILib, obj_schema);
            typinf->struct_size = 0;

            /* the names are compared when the list is searched */
            res = (typinf->name != NULL) && (typinf->schema != NULL);

            if (res == TRUE)
            {
                res = (OCI_SUCCESS == OCI_HandleAlloc2(pOCILib, pOCILib->env,
                                                      (dvoid **) (void *) &dschp,
                                                      OCI_HTYPE_DESCRIBE, (size_t) 0,
                                                      (dvoid **) NULL));
            }
        }

        if (res == TRUE)
        {
            if (type == OCI_TIF_TYPE)
            {
                void *ostr1 = NULL;
                void *ostr2 = NULL;
                int osize1  = -1;
                int osize2  = -1;

                attr_type = OCI_ATTR_LIST_TYPE_ATTRS;
                num_type  = OCI_ATTR_NUM_TYPE_ATTRS;
                ptype     = OCI_DESC_TYPE;

                ostr1 = OCI_GetInputMetaString(pOCILib, typinf->schema, &osize1);
                ostr2 = OCI_GetInputMetaString(pOCILib, typinf->name,   &osize2);

                OCI_CALL2Q
                (
		   pOCILib, res, con,

		   OCITypeByName(pOCILib->env, con->err, con->cxt,
                                 (text *) ostr1, (ub4) osize1,
                                 (text *) ostr2, (ub4) osize2,
                                 (text *) NULL, (ub4) 0,
                                 OCI_DURATION_SESSION, OCI_TYPEGET_ALL,
                                 &typinf->tdo),

		   xsink
                )

                OCI_CALL2Q
                (
                    pOCILib, res, con,

                    OCIDescribeAny(con->cxt, con->err, (void *) typinf->tdo,
                                   0, OCI_OTYPE_PTR, OCI_DEFAULT,
                                   OCI_PTYPE_TYPE, dschp),

		    xsink
                )

                OCI_ReleaseMetaString(ostr1);
                OCI_ReleaseMetaString(ostr2);
            }
            else
            {
                /* "schema.name": OCI_TypeInfoGet2() and OCI_TypeInfoGetExact2() checked both parts to be at most
                   OCI_SIZE_SQL_IDENTIFIER bytes, so the size arithmetic below cannot underflow and the name fits */
                mtext buffer[(OCI_SIZE_SQL_IDENTIFIER*2) + 2];

                size_t size = (sizeof(buffer) - 1)/sizeof(mtext);
                void *ostr1 = NULL;
                int osize1  = -1;

                attr_type = OCI_ATTR_LIST_COLUMNS;
                num_type  = OCI_ATTR_NUM_COLS;
                ptype     = OCI_DESC_TABLE;
                str       = buffer;

                str[0] = 0;

                if ((typinf->schema != NULL) && (typinf->schema[0] != 0))
                {
                    str = mtsncat(buffer, typinf->schema, size);
                    size -= mtslen(typinf->schema);
                    str = mtsncat(str, MT("."), size);
                    size -= (size_t) 1;
                }

                mtsncat(str, typinf->name, size);

                ostr1 = OCI_GetInputMetaString(pOCILib, str, &osize1);

                OCI_CALL2Q
                (
                    pOCILib, res, con,

                    OCIDescribeAny(con->cxt, con->err, (dvoid *) ostr1,
                                   (ub4) osize1, OCI_OTYPE_NAME,
                                   OCI_DEFAULT, item_type, dschp),

		    xsink
                )

                OCI_ReleaseMetaString(ostr1);
            }

            OCI_CALL2Q
            (
                pOCILib, res, con,

                OCIAttrGet(dschp, OCI_HTYPE_DESCRIBE, &parmh1,
                           NULL, OCI_ATTR_PARAM, con->err),

		xsink
            )

            /* do we need get more attributes for collections ? */

            if (type == OCI_TIF_TYPE)
            {
                OCI_CALL2Q
                (
                    pOCILib, res, con,

                    OCIAttrGet(parmh1, OCI_DTYPE_PARAM, &typinf->tcode,
                               NULL, OCI_ATTR_TYPECODE, con->err),

		    xsink
                )

            }

            if (typinf->tcode == SQLT_NCO)
            {
                typinf->nb_cols = 1;

                ptype  = OCI_DESC_COLLECTION;
                //etype  = OCI_DESC_TYPE;
                parmh2 = parmh1;

                OCI_CALL2Q
                (
                    pOCILib, res, con,

                    OCIAttrGet(parmh1, OCI_DTYPE_PARAM, &typinf->ccode,
                               NULL, OCI_ATTR_COLLECTION_TYPECODE, con->err),

		    xsink
                )
            }
            else
            {
                OCI_CALL2Q
                (
                    pOCILib, res, con,

                    OCIAttrGet(parmh1, OCI_DTYPE_PARAM, &parmh2,
                               NULL, attr_type, con->err),

		    xsink
                )

                OCI_CALL2Q
                (
                    pOCILib, res, con,

                    OCIAttrGet(parmh1, OCI_DTYPE_PARAM, &typinf->nb_cols,
                               NULL, num_type, con->err),

		    xsink
                )
            }

            /* allocates memory for cached offsets; an error above is not overwritten */

            if ((res == TRUE) && (typinf->nb_cols > 0))
            {
                typinf->offsets = (int *) OCI_MemAlloc2(pOCILib, OCI_IPC_ARRAY,
                                                       sizeof(*typinf->offsets),
                                                       (size_t) typinf->nb_cols,
                                                       FALSE);

                res = (typinf->offsets != NULL);

                if (res == TRUE)
                {
                    memset(typinf->offsets, -1, sizeof(*typinf->offsets) * typinf->nb_cols);
                }
            }

            /* allocates memory for children */

            if ((res == TRUE) && (typinf->nb_cols > 0))
            {
                typinf->cols = (OCI_Column *) OCI_MemAlloc2(pOCILib, OCI_IPC_COLUMN,
                                                           sizeof(*typinf->cols),
                                                           (size_t) typinf->nb_cols,
                                                           TRUE);

                /* describe children */

                if (typinf->cols != NULL)
                {
                    /* the chain of descriptions is limited, as each level uses the stack */
                    if (con->type_depth >= OCI_MAX_TYPE_DEPTH)
                    {
                        OCI_TypeInfoRaiseTooDeep(xsink);
                        res = FALSE;
                    }

                    con->type_depth++;

                    for (i = 0; i < typinf->nb_cols; i++)
                    {
		        res = res && OCI_ColumnDescribe2(pOCILib, &typinf->cols[i], con,
							 NULL, parmh2, i + 1, ptype, xsink);

		        res = res && OCI_ColumnMap2(pOCILib, &typinf->cols[i], NULL);

                        if (res == FALSE)
                        {
                            /* the type is not used if one of its attributes cannot be described; an exception
                               is raised if the failure did not raise one */
                            if (xsink && !*xsink)
                            {
                                /* the names are not in the message: a schema can be the user of the connection */
                                xsink->raiseException("ORACLE-OCI-ERROR", "attribute %d of a type, table or view "
                                    "cannot be described", (int) (i + 1));
                            }
                            break;
                        }
                   }

                   con->type_depth--;
                }
                else
                    res = FALSE;
            }

            /* free describe handle */

            if (dschp != NULL)
	       OCI_HandleFree2(pOCILib, dschp, OCI_HTYPE_DESCRIBE);
        }
    }

    /* when the outermost description is done, all the types that it added are complete: each one is checked, so that
       every description in the list of the connection has a known depth within the limit, whatever order the types
       were described in */

    if ((res == TRUE) && (found == FALSE) && (con->type_depth == 0))
    {
        for (OCI_Item *added = item; added != NULL; added = added->next)
        {
            if (OCI_TypeInfoCheckLayout((OCI_TypeInfo *) added->data, 1) == FALSE)
            {
                OCI_TypeInfoRaiseTooDeep(xsink);
                res = FALSE;
                break;
            }
        }
    }

   /* handle errors */

    if (res == FALSE)
    {
        // the description that was being made is removed from the list and freed; it has no references yet, so
        // OCI_TypeInfoFree2() (which frees it when its last reference is released) cannot free it; a description
        // that was found in the list is kept
        if (found == FALSE && item != NULL)
        {
            // the descriptions after it in the list were made while describing it, and can refer to it (types can
            // refer to each other); they are removed with it, so that none is left with a reference to a freed
            // description; nothing else can hold them yet
            while (item->next != NULL)
            {
                OCI_TypeInfo *later = (OCI_TypeInfo *) item->next->data;
                OCI_ListRemove(pOCILib, con->tinfs, later);
                OCI_TypeInfoClose(later);
                OCI_FREE(later);
            }

            OCI_TypeInfo *newinf = (OCI_TypeInfo *) item->data;
            // frees the list item
            OCI_ListRemove(pOCILib, con->tinfs, newinf);
            OCI_TypeInfoClose(newinf);
            OCI_FREE(newinf);
        }
        typinf = NULL;
    }

    OCI_RESULT(pOCILib, res);

    /* increment type info reference counter on success */

    if (typinf != NULL)
        typinf->refcount++;

    return typinf;
}

OCI_TypeInfo * OCI_API OCI_TypeInfoGet2(OCI_Library *pOCILib, OCI_Connection *con, const mtext *name,
					unsigned int type, ExceptionSink* xsink)
{
    mtext *str = NULL;

    mtext obj_schema[OCI_SIZE_SQL_IDENTIFIER+1];
    mtext obj_name[OCI_SIZE_SQL_IDENTIFIER+1];

    OCI_CHECK_INITIALIZED2(pOCILib, NULL);

    OCI_CHECK_PTRQ(pOCILib, OCI_IPC_CONNECTION, con, NULL, xsink);
    OCI_CHECK_PTRQ(pOCILib, OCI_IPC_STRING, name, NULL, xsink);

    /* is the schema provided in the object name ? ("schema.name"; without a name after the '.', the whole string is
       the name) */
    {
        const mtext *obj = name;
        size_t schema_len = 0;

        for (str = (mtext *) name; *str != 0; str++)
        {
            if (*str == MT('.'))
            {
                if (str[1] != 0)
                {
                    schema_len = (size_t) (str - name);
                    obj = str + 1;
                }
                break;
            }
        }

        size_t name_len = mtslen(obj);

        if (OCI_TypeInfoCheckNameLen(pOCILib, schema_len, name_len, xsink) == FALSE)
        {
            return NULL;
        }

        memcpy(obj_schema, name, schema_len * sizeof(mtext));
        obj_schema[schema_len] = 0;
        memcpy(obj_name, obj, name_len * sizeof(mtext));
        obj_name[name_len] = 0;
    }

    /* type name must be uppercase */

    for (str = obj_name; *str != 0; str++)
        *str = (mtext) mttoupper(*str);

    /* schema name must be uppercase */

    for (str = obj_schema; *str != 0; str++)
        *str = (mtext) mttoupper(*str);

    return OCI_TypeInfoFind(pOCILib, con, obj_schema, obj_name, type, xsink);
}

/* ------------------------------------------------------------------------ *
 * OCI_TypeInfoGetExact2
 * ------------------------------------------------------------------------ */

OCI_TypeInfo * OCI_API OCI_TypeInfoGetExact2(OCI_Library *pOCILib, OCI_Connection *con, const mtext *schema,
                                             size_t schema_len, const mtext *name, size_t name_len,
                                             ExceptionSink* xsink)
{
    mtext obj_schema[OCI_SIZE_SQL_IDENTIFIER+1];
    mtext obj_name[OCI_SIZE_SQL_IDENTIFIER+1];

    OCI_CHECK_INITIALIZED2(pOCILib, NULL);

    OCI_CHECK_PTRQ(pOCILib, OCI_IPC_CONNECTION, con, NULL, xsink);
    OCI_CHECK_PTRQ(pOCILib, OCI_IPC_STRING, name, NULL, xsink);

    if (schema == NULL)
    {
        schema_len = 0;
    }

    if (OCI_TypeInfoCheckNameLen(pOCILib, schema_len, name_len, xsink) == FALSE)
    {
        return NULL;
    }

    /* the names are used as they are: they are not parsed, and their case is kept */
    if (schema_len > 0)
    {
        memcpy(obj_schema, schema, schema_len * sizeof(mtext));
    }
    obj_schema[schema_len] = 0;
    memcpy(obj_name, name, name_len * sizeof(mtext));
    obj_name[name_len] = 0;

    return OCI_TypeInfoFind(pOCILib, con, obj_schema, obj_name, OCI_TIF_TYPE, xsink);
}

/* ------------------------------------------------------------------------ *
 * OCI_TypeInfoFree
 * ------------------------------------------------------------------------ */

boolean OCI_API OCI_TypeInfoFree2(OCI_Library *pOCILib, OCI_TypeInfo *typinf)
{
    boolean res = TRUE;

    OCI_CHECK_PTR(pOCILib, OCI_IPC_TYPE_INFO, typinf, FALSE);

    typinf->refcount--;

    if (typinf->refcount == 0)
    {
       OCI_ListRemove(pOCILib, typinf->con->tinfs, typinf);

        res = OCI_TypeInfoClose(typinf);

        OCI_FREE(typinf);
    }

    OCI_RESULT(pOCILib, res);

    return res;
}

/* ------------------------------------------------------------------------ *
 * OCI_TypeInfoGetType
 * ------------------------------------------------------------------------ */

unsigned int OCI_API OCI_TypeInfoGetType2(OCI_Library *pOCILib, OCI_TypeInfo *typinf)
{
    OCI_CHECK_PTR(pOCILib, OCI_IPC_TYPE_INFO, typinf, OCI_UNKNOWN);

    OCI_RESULT(pOCILib, TRUE);

    return typinf->type;
}

/* ------------------------------------------------------------------------ *
 * OCI_TypeInfoGetColumnCount
 * ------------------------------------------------------------------------ */

unsigned int OCI_API OCI_TypeInfoGetColumnCount2(OCI_Library *pOCILib, OCI_TypeInfo *typinf)
{
    OCI_CHECK_PTR(pOCILib, OCI_IPC_TYPE_INFO, typinf, 0);

    OCI_RESULT(pOCILib, TRUE);

    return typinf->nb_cols;
}

/* ------------------------------------------------------------------------ *
 * OCI_TypeInfoGetColumn
 * ------------------------------------------------------------------------ */

OCI_Column * OCI_API OCI_TypeInfoGetColumn2(OCI_Library *pOCILib, OCI_TypeInfo *typinf, unsigned int index, ExceptionSink* xsink)
{
   OCI_CHECK_PTRQ(pOCILib, OCI_IPC_TYPE_INFO, typinf, NULL, xsink);
   OCI_CHECK_BOUNDQ(pOCILib, typinf->con, index, 1,  typinf->nb_cols, NULL, xsink);

   OCI_RESULT(pOCILib, TRUE);

   return &(typinf->cols[index-1]);
}

/* ------------------------------------------------------------------------ *
 * OCI_TypeInfoGetName
 * ------------------------------------------------------------------------ */

const mtext * OCI_API OCI_TypeInfoGetName2(OCI_Library *pOCILib, OCI_TypeInfo *typinf)
{
    OCI_CHECK_PTR(pOCILib, OCI_IPC_TYPE_INFO, typinf, NULL);

    OCI_RESULT(pOCILib, TRUE);

    return typinf->name;
}
