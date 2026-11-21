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

OCI_TypeInfo * OCI_API OCI_TypeInfoGet2(OCI_Library *pOCILib, OCI_Connection *con, const mtext *name,
					unsigned int type, ExceptionSink* xsink)
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

    mtext obj_schema[OCI_SIZE_OBJ_NAME+1];
    mtext obj_name[OCI_SIZE_OBJ_NAME+1];

    OCI_CHECK_INITIALIZED2(pOCILib, NULL);

    OCI_CHECK_PTRQ(pOCILib, OCI_IPC_CONNECTION, con, NULL, xsink);
    OCI_CHECK_PTRQ(pOCILib, OCI_IPC_STRING, name, NULL, xsink);

    if (type == OCI_TIF_TABLE)
        item_type = OCI_PTYPE_TABLE;
    else if (type == OCI_TIF_VIEW)
        item_type = OCI_PTYPE_VIEW;
    else if (type == OCI_TIF_TYPE)
        item_type = OCI_PTYPE_TYPE;
    else
        return NULL;

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

        /* the schema and the name must each fit in their buffers; a longer one is refused, not cut, as a cut name
           could be the name of another object */
        if (schema_len > (size_t) OCI_SIZE_OBJ_NAME || name_len > (size_t) OCI_SIZE_OBJ_NAME)
        {
            if (xsink)
                xsink->raiseException("ORACLE-OCI-ERROR", "the schema name (%d bytes) or the object name "
                    "(%d bytes) of the given type, table or view name is longer than the supported maximum of "
                    "%d bytes", (int) schema_len, (int) name_len, (int) OCI_SIZE_OBJ_NAME);
            OCI_RESULT(pOCILib, FALSE);
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

    /* first try to find it in list */

    item = con->tinfs->head;

    /* walk along the list to find the type */

    while (item != NULL)
    {
        typinf = (OCI_TypeInfo *) item->data;

        if ((typinf != NULL) && (typinf->type == type))
        {
            if ((mtscasecmp(typinf->name,   obj_name  ) == 0) &&
                (mtscasecmp(typinf->schema, obj_schema) == 0))
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

            res = (OCI_SUCCESS == OCI_HandleAlloc2(pOCILib, pOCILib->env,
                                                  (dvoid **) (void *) &dschp,
                                                  OCI_HTYPE_DESCRIBE, (size_t) 0,
                                                  (dvoid **) NULL));
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
                /* "schema.name": both parts were checked above to be at most OCI_SIZE_OBJ_NAME characters, so the
                   size arithmetic below cannot underflow and the name fits */
                mtext buffer[(OCI_SIZE_OBJ_NAME*2) + 2];

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

            /* allocates memory for cached offsets */

            if (typinf->nb_cols > 0)
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

            if (typinf->nb_cols > 0)
            {
                typinf->cols = (OCI_Column *) OCI_MemAlloc2(pOCILib, OCI_IPC_COLUMN,
                                                           sizeof(*typinf->cols),
                                                           (size_t) typinf->nb_cols,
                                                           TRUE);

                /* describe children */

                if (typinf->cols != NULL)
                {
                    for (i = 0; i < typinf->nb_cols; i++)
                    {
		        res = res && OCI_ColumnDescribe2(pOCILib, &typinf->cols[i], con,
							 NULL, parmh2, i + 1, ptype, xsink);

		        res = res && OCI_ColumnMap2(pOCILib, &typinf->cols[i], NULL);

                        if (res == FALSE)
                            break;
                   }
                }
                else
                    res = FALSE;
            }

            /* free describe handle */

            if (dschp != NULL)
	       OCI_HandleFree2(pOCILib, dschp, OCI_HTYPE_DESCRIBE);
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
