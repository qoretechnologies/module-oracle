/*
    +-----------------------------------------------------------------------------------------+
    |                                                                                         |
    |                               OCILIB - C Driver for Oracle                              |
    |                                                                                         |
    |                                (C Wrapper for Oracle OCI)                               |
    |                                                                                         |
    |                              Website : http://www.ocilib.net                            |
    |                                                                                         |
    |             Copyright (c) 2007-2012 Vincent ROGIER <vince.rogier@ocilib.net>            |
    |                                                                                         |
    +-----------------------------------------------------------------------------------------+
    |                                                                                         |
    |             This library is free software; you can redistribute it and/or               |
    |             modify it under the terms of the GNU Lesser General Public                  |
    |             License as published by the Free Software Foundation; either                |
    |             version 2 of the License, or (at your option) any later version.            |
    |                                                                                         |
    |             This library is distributed in the hope that it will be useful,             |
    |             but WITHOUT ANY WARRANTY; without even the implied warranty of              |
    |             MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU           |
    |             Lesser General Public License for more details.                             |
    |                                                                                         |
    |             You should have received a copy of the GNU Lesser General Public            |
    |             License along with this library; if not, write to the Free                  |
    |             Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.          |
    |                                                                                         |
    +-----------------------------------------------------------------------------------------+
*/

/* --------------------------------------------------------------------------------------------- *
 * $Id: dequeue.c, Vincent Rogier $
 * --------------------------------------------------------------------------------------------- */

#include "ocilib_internal.h"

/* ********************************************************************************************* *
 *                            PUBLIC FUNCTIONS
 * ********************************************************************************************* */

/* --------------------------------------------------------------------------------------------- *
 * OCI_DequeueCreate
 * --------------------------------------------------------------------------------------------- */

OCI_Dequeue * OCI_API OCI_DequeueCreate
(
    OCI_Library*   pOCILib,
    OCI_TypeInfo*  typinf,
    const mtext*   name,
    ExceptionSink* xsink
)
{
    OCI_Dequeue *dequeue = NULL;
    boolean res          = TRUE;

    OCI_CHECK_INITIALIZED2(pOCILib, NULL);

    OCI_CHECK_PTR(pOCILib, OCI_IPC_TYPE_INFO, typinf, NULL);
    OCI_CHECK_PTR(pOCILib, OCI_IPC_STRING, name, NULL);

    /* allocate dequeue structure */

    dequeue = (OCI_Dequeue *) OCI_MemAlloc2(pOCILib, OCI_IPC_DEQUEUE, sizeof(*dequeue), (size_t) 1, TRUE);

    if (dequeue != NULL)
    {
        dequeue->typinf = typinf;
        dequeue->name   = mtsdup(pOCILib, name);

	/* set user context pointer to NULL */
	dequeue->user_ctx = NULL;

        /* allocate dequeue options descriptor */

        res = (OCI_SUCCESS == OCI_DescriptorAlloc2(pOCILib, (dvoid * ) pOCILib->env,
                                                  (dvoid **) &dequeue->opth,
                                                  OCI_DTYPE_AQDEQ_OPTIONS,
                                                  (size_t) 0, (dvoid **) NULL));

        /* create local message for OCI_DequeueGet() */

        if (res == TRUE)
        {
	   dequeue->msg = OCI_MsgCreate(pOCILib, dequeue->typinf, xsink);
        }

        res = (dequeue->msg != NULL);
    }
    else
    {
        res = FALSE;
    }

    /* check for failure */

    if (res == FALSE)
    {
       OCI_DequeueFree(pOCILib, dequeue, xsink);
       dequeue = NULL;
    }

    return dequeue;
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DequeueFree
 * --------------------------------------------------------------------------------------------- */

boolean OCI_API OCI_DequeueFree
(
    OCI_Library *pOCILib,
    OCI_Dequeue *dequeue,
    ExceptionSink *xsink
)
{
    OCI_CHECK_PTR(pOCILib, OCI_IPC_DEQUEUE, dequeue, FALSE);

    /* free local message  */

    if (dequeue->msg != NULL)
    {
       OCI_MsgFree(pOCILib, dequeue->msg, xsink);
    }

    /* free local agent  */

    if (dequeue->agent != NULL)
    {
        OCI_AgentFree(pOCILib, dequeue->agent);
    }

    /* free OCI descriptor */

    OCI_DescriptorFree2(pOCILib, (dvoid *) dequeue->opth, OCI_DTYPE_AQDEQ_OPTIONS);

    /* free strings  */

    OCI_FREE(dequeue->name);
    OCI_FREE(dequeue->pattern);
    OCI_FREE(dequeue->consumer);

    /* free misc. */

    OCI_FREE(dequeue->agent_list);

    OCI_FREE(dequeue);

    return TRUE;
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DequeueListen
 * --------------------------------------------------------------------------------------------- */
/*
OCI_Agent * OCI_API OCI_DequeueListen
(
   OCI_Library *pOCILib,
    OCI_Dequeue *dequeue,
    int          timeout
)
{
    boolean res        = TRUE;
    OCI_Agent *agent   = NULL;
    OCIAQAgent *handle = NULL;

    OCI_CHECK_PTR(pOCILib, OCI_IPC_DEQUEUE, dequeue, NULL);

    // listen only if OCI_DequeueSetAgentList has been called

    if (dequeue->agent_list != NULL)
    {
        sword ret;
        sb4 code;

        ret =  OCIAQListen(dequeue->typinf->con->cxt, pOCILib->err,
                           dequeue->agent_list, (ub4) dequeue->agent_count,
                           (sb4) timeout, &handle, OCI_DEFAULT);

        // check returned error code

        if (ret == OCI_ERROR)
        {
            OCIErrorGet((dvoid *) pOCILib->err, (ub4) 1,
                        (OraText *) NULL, &code, (OraText *) NULL, (ub4) 0,
                        (ub4) OCI_HTYPE_ERROR);

            // raise error only if the call has not been timed out

            if (code != OCI_ERR_AQ_LISTEN_TIMEOUT)
            {
                OCI_ExceptionOCI2(pOCILib, pOCILib->err, dequeue->typinf->con, NULL, FALSE);

                res = FALSE;
            }
        }

        // init local agent object

        if ((res == TRUE) && (ret == OCI_SUCCESS) && (handle != NULL))
        {
            agent = OCI_AgentInit(pOCILib, dequeue->typinf->con, &dequeue->agent, handle, NULL, NULL);
        }
    }

    OCI_RESULT(pOCILib, res);

    return agent;
}
*/

/* --------------------------------------------------------------------------------------------- *
 * OCI_DequeueGet
 * --------------------------------------------------------------------------------------------- */

OCI_Msg * OCI_API OCI_DequeueGet
(
    OCI_Library *pOCILib,
    OCI_Dequeue *dequeue,
    ExceptionSink* xsink
)
{
    boolean   res      = TRUE;
    void     *ostr     = NULL;
    int       osize    = -1;
    OCI_Msg  *msg      = NULL;
    sword     ret      = OCI_SUCCESS;
    void     *p_ind    = NULL;

    OCI_CHECK_PTR(pOCILib, OCI_IPC_DEQUEUE, dequeue, NULL);

    /* reset message */

    res = OCI_MsgReset(pOCILib, dequeue->msg, xsink);

    //printd(5, "OCI_DequeueGet() OCI_MsgReset res: %d\n", res);

    if (res == TRUE)
    {
        ostr  = OCI_GetInputMetaString(pOCILib, dequeue->name, &osize);

        if (dequeue->typinf->tcode == OCI_UNKNOWN)
        {
            p_ind = &dequeue->msg->ind;
        }

        /* dequeue message */

        ret = OCIAQDeq(dequeue->typinf->con->cxt, pOCILib->err,
                       (OraText*)ostr, dequeue->opth, dequeue->msg->proph, dequeue->typinf->tdo,
                       &dequeue->msg->payload, (void **) &p_ind, &dequeue->msg->id, OCI_DEFAULT);

        OCI_ReleaseMetaString(ostr);

        /* check returned error code */

        if (ret == OCI_ERROR)
        {
            sb4 code = 0;

            OCIErrorGet((dvoid *) pOCILib->err, (ub4) 1,
                        (OraText *) NULL, &code, (OraText *) NULL, (ub4) 0,
                        (ub4) OCI_HTYPE_ERROR);

            /* raise error only if the call has not been timed out */

            if (code != OCI_ERR_AQ_DEQUEUE_TIMEOUT)
            {
	       OCI_ExceptionOCI2(pOCILib, pOCILib->err, dequeue->typinf->con, NULL, FALSE, xsink);

	       res = FALSE;
            }
        }
    }

    /* reset message */

    if ((res == TRUE) && (ret == OCI_SUCCESS))
    {
        /* get payload */

        if (dequeue->typinf->tcode != OCI_UNKNOWN)
        {
            if ((p_ind != NULL) && ((*(OCIInd *) p_ind) != OCI_IND_NULL))
            {
                dequeue->msg->ind = *(OCIInd *) p_ind;

                dequeue->msg->obj = OCI_ObjectInit(pOCILib, dequeue->typinf->con,
                                                   (OCI_Object **) &dequeue->msg->obj,
                                                   dequeue->msg->payload, dequeue->typinf,
                                                   NULL, -1, TRUE, xsink);

                res = dequeue->msg->obj != NULL;

		//printd(5, "OCI_DequeueGet() OCI_ObjectInit res: %d\n", res);
            }
        }
    }

    /* on success return internla message handle */

    if ((res == TRUE) && (ret == OCI_SUCCESS))
    {
        msg = dequeue->msg;
    }

    OCI_RESULT(pOCILib, res);

    //printd(5, "OCI_DequeueGet() returning msg: %p\n", msg);

    return msg;
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DequeueGetConsumerName
 * --------------------------------------------------------------------------------------------- */
/*
const mtext * OCI_API OCI_DequeueGetConsumer
(
    OCI_Library *pOCILib,
    OCI_Dequeue *dequeue
)
{
    boolean res = TRUE;

    OCI_CHECK_PTR(pOCILib, OCI_IPC_DEQUEUE, dequeue, NULL);

    if (dequeue->consumer == NULL)
    {
        res = OCI_StringGetFromAttrHandle(pOCILib, dequeue->typinf->con,
                                          dequeue->opth,
                                          OCI_DTYPE_AQDEQ_OPTIONS,
                                          OCI_ATTR_CONSUMER_NAME,
                                          &dequeue->consumer);
    }

    OCI_RESULT(pOCILib, res);

    return dequeue->consumer;
}
*/
/* --------------------------------------------------------------------------------------------- *
 * OCI_DequeueSetConsumerName
 * --------------------------------------------------------------------------------------------- */

#if 0
boolean OCI_API OCI_DequeueSetConsumer
(
    OCI_Library *pOCILib,
    OCI_Dequeue *dequeue,
    const mtext *consumer
)
{
    boolean res = TRUE;

    OCI_CHECK_PTR(pOCILib, OCI_IPC_DEQUEUE, dequeue, FALSE);

    res =  OCI_StringSetToAttrHandle(pOCILib,
    								 dequeue->typinf->con,
                                     dequeue->opth,
                                     OCI_DTYPE_AQDEQ_OPTIONS,
                                     OCI_ATTR_CONSUMER_NAME,
                                     &dequeue->consumer,
                                     consumer);

    OCI_RESULT(pOCILib, res);

    return res;
}
#endif

/* --------------------------------------------------------------------------------------------- *
 * OCI_DequeueGetCorrelation
 * --------------------------------------------------------------------------------------------- */
#if 0
const mtext * OCI_API OCI_DequeueGetCorrelation
(
    OCI_Dequeue *dequeue
)
{
    boolean res = TRUE;

    OCI_CHECK_PTR(pOCILib, OCI_IPC_DEQUEUE, dequeue, NULL);

    if (dequeue->pattern == NULL)
    {
        res = OCI_StringGetFromAttrHandle(pOCILib, dequeue->typinf->con,
                                          dequeue->opth,
                                          OCI_DTYPE_AQDEQ_OPTIONS,
                                          OCI_ATTR_CORRELATION,
                                          &dequeue->pattern);
    }

    OCI_RESULT(pOCILib, res);

    return dequeue->pattern;
}
#endif
/* --------------------------------------------------------------------------------------------- *
 * OCI_DequeueSetCorrelation
 * --------------------------------------------------------------------------------------------- */
#if 0
boolean OCI_API OCI_DequeueSetCorrelation
(
    OCI_Dequeue *dequeue,
    const mtext *pattern
)
{
    boolean res = TRUE;

    OCI_CHECK_PTR(pOCILib, OCI_IPC_DEQUEUE, dequeue, FALSE);

    res =  OCI_StringSetToAttrHandle(pOCILib, dequeue->typinf->con,
                                     dequeue->opth,
                                     OCI_DTYPE_AQDEQ_OPTIONS,
                                     OCI_ATTR_CORRELATION,
                                     &dequeue->pattern,
                                     pattern);

    OCI_RESULT(pOCILib, res);

    return res;
}
#endif
/* --------------------------------------------------------------------------------------------- *
 * OCI_DequeueGetRelativeMsgID
 * --------------------------------------------------------------------------------------------- */
#if 0
boolean OCI_API OCI_DequeueGetRelativeMsgID
(
    OCI_Dequeue  *dequeue,
    void         *id,
    unsigned int *len
)
{
    boolean res   = TRUE;
    OCIRaw *value = NULL;

    OCI_CHECK_PTR(OCI_IPC_DEQUEUE, dequeue, FALSE);
    OCI_CHECK_PTR(OCI_IPC_VOID,    id,      FALSE);
    OCI_CHECK_PTR(OCI_IPC_VOID,    len,     FALSE);

    OCI_CALL2
    (
        res, dequeue->typinf->con,

        OCIAttrGet((dvoid *) dequeue->opth,
                   (ub4    ) OCI_DTYPE_AQDEQ_OPTIONS,
                   (dvoid *) &value,
                   (ub4   *) NULL,
                   (ub4    ) OCI_ATTR_DEQ_MSGID,
                   pOCILib->err)
    )

    if (value != NULL)
    {
        ub4 raw_len = 0;

        raw_len = OCIRawSize(pOCILib->env, value);

        if (*len > raw_len)
            *len = raw_len;

        memcpy(id, OCIRawPtr(pOCILib->env, value), (size_t) (*len));
    }
    else
    {
        *len = 0;
    }

    OCI_RESULT(res);

    return res;
}
#endif
/* --------------------------------------------------------------------------------------------- *
 * OCI_DequeueSetRelativeMsgID
 * --------------------------------------------------------------------------------------------- */
#if 0
boolean OCI_API OCI_DequeueSetRelativeMsgID
(
    OCI_Dequeue  *dequeue,
    const void   *id,
    unsigned int  len
)
{
    boolean res   = TRUE;
    OCIRaw *value = NULL;

    OCI_CHECK_PTR(OCI_IPC_DEQUEUE, dequeue, FALSE);

    OCI_CALL2
    (
        res, dequeue->typinf->con,

        OCIRawAssignBytes(pOCILib->env, pOCILib->err,
                          (ub1*) id, (ub4) len, (OCIRaw **) &value)
    )

    OCI_CALL2
    (
        res, dequeue->typinf->con,

        OCIAttrSet((dvoid *) dequeue->opth,
                   (ub4    ) OCI_DTYPE_AQDEQ_OPTIONS,
                   (dvoid *) &value,
                   (ub4    ) 0,
                   (ub4    ) OCI_ATTR_DEQ_MSGID,
                   pOCILib->>err)
    )

    OCI_RESULT(res);

    return res;
}
#endif
/* --------------------------------------------------------------------------------------------- *
 * OCI_DequeueGetVisibility
 * --------------------------------------------------------------------------------------------- */
/*
unsigned int OCI_API OCI_DequeueGetVisibility
(
    OCI_Library *pOCILib,
    OCI_Dequeue *dequeue
)
{
    boolean res = TRUE;
    ub4 ret     = 0;

    OCI_CHECK_PTR(pOCILib, OCI_IPC_DEQUEUE, dequeue, 0);

    OCI_CALL2
    (
    	pOCILib, res, dequeue->typinf->con,

        OCIAttrGet((dvoid *) dequeue->opth,
                   (ub4    ) OCI_DTYPE_AQDEQ_OPTIONS,
                   (dvoid *) &ret,
                   (ub4   *) NULL,
                   (ub4    ) OCI_ATTR_VISIBILITY,
                   pOCILib->err)
    )

    OCI_RESULT(pOCILib, res);

    return (int) ret;
}
*/

/* --------------------------------------------------------------------------------------------- *
 * OCI_DequeueSetVisibility
 * --------------------------------------------------------------------------------------------- */
/*
boolean OCI_API OCI_DequeueSetVisibility
(
    OCI_Library *pOCILib,
    OCI_Dequeue *dequeue,
    unsigned int visibility
)
{
    boolean res = TRUE;
    ub4 value   = (ub4) visibility;

    OCI_CHECK_PTR(pOCILib, OCI_IPC_DEQUEUE, dequeue, FALSE);

    OCI_CALL2
    (
    	pOCILib, res, dequeue->typinf->con,

        OCIAttrSet((dvoid *) dequeue->opth,
                   (ub4    ) OCI_DTYPE_AQDEQ_OPTIONS,
                   (dvoid *) &value,
                   (ub4    ) 0,
                   (ub4    ) OCI_ATTR_VISIBILITY,
                   pOCILib->err)
    )

    OCI_RESULT(pOCILib, res);

    return res;
}
*/

/* --------------------------------------------------------------------------------------------- *
 * OCI_DequeueGetMode
 * --------------------------------------------------------------------------------------------- */

/*
unsigned int OCI_API OCI_DequeueGetMode
(
    OCI_Library *pOCILib,
    OCI_Dequeue *dequeue
)
{
    boolean res = TRUE;
    ub4 ret     = 0;

    OCI_CHECK_PTR(pOCILib, OCI_IPC_DEQUEUE, dequeue, 0);

    OCI_CALL2
    (
    	pOCILib, res, dequeue->typinf->con,

        OCIAttrGet((dvoid *) dequeue->opth,
                   (ub4    ) OCI_DTYPE_AQDEQ_OPTIONS,
                   (dvoid *) &ret,
                   (ub4   *) NULL,
                   (ub4    ) OCI_ATTR_DEQ_MODE,
                   pOCILib->err)
    )

    OCI_RESULT(pOCILib, res);

    return (int) ret;
}
*/

/* --------------------------------------------------------------------------------------------- *
 * OCI_DequeueSetMode
 * --------------------------------------------------------------------------------------------- */

/*
boolean OCI_API OCI_DequeueSetMode
(
    OCI_Library *pOCILib,
    OCI_Dequeue *dequeue,
    unsigned int mode
)
{
    boolean res = TRUE;
    ub4 value   = (ub4) mode;

    OCI_CHECK_PTR(pOCILib, OCI_IPC_DEQUEUE, dequeue, FALSE);

    OCI_CALL2
    (
    	pOCILib, res, dequeue->typinf->con,

        OCIAttrSet((dvoid *) dequeue->opth,
                   (ub4    ) OCI_DTYPE_AQDEQ_OPTIONS,
                   (dvoid *) &value,
                   (ub4    ) 0,
                   (ub4    ) OCI_ATTR_DEQ_MODE,
                   pOCILib->err)
    )

    OCI_RESULT(pOCILib, res);

    return res;
}
*/

/* --------------------------------------------------------------------------------------------- *
 * OCI_DequeueGetNavigation
 * --------------------------------------------------------------------------------------------- */
/*
unsigned int OCI_API OCI_DequeueGetNavigation
(
    OCI_Library *pOCILib,
    OCI_Dequeue *dequeue
)
{
    boolean res = TRUE;
    ub4 ret     = 0;

    OCI_CHECK_PTR(pOCILib, OCI_IPC_DEQUEUE, dequeue, 0);

    OCI_CALL2
    (
    	pOCILib, res, dequeue->typinf->con,

        OCIAttrGet((dvoid *) dequeue->opth,
                   (ub4    ) OCI_DTYPE_AQDEQ_OPTIONS,
                   (dvoid *) &ret,
                   (ub4   *) NULL,
                   (ub4    ) OCI_ATTR_NAVIGATION,
                   pOCILib->err)
    )

    OCI_RESULT(pOCILib, res);

    return (int) ret;
}
*/

/* --------------------------------------------------------------------------------------------- *
 * OCI_DequeueSetNavigation
 * --------------------------------------------------------------------------------------------- */
/*
boolean OCI_API OCI_DequeueSetNavigation
(
    OCI_Library *pOCILib,
    OCI_Dequeue *dequeue,
    unsigned int position
)
{
    boolean res = TRUE;
    ub4 value   = (ub4) position;

    OCI_CHECK_PTR(pOCILib, OCI_IPC_DEQUEUE, dequeue, FALSE);

    OCI_CALL2
    (
    	pOCILib, res, dequeue->typinf->con,

        OCIAttrSet((dvoid *) dequeue->opth,
                   (ub4    ) OCI_DTYPE_AQDEQ_OPTIONS,
                   (dvoid *) &value,
                   (ub4    ) 0,
                   (ub4    ) OCI_ATTR_NAVIGATION,
                   pOCILib->err)
    )

    OCI_RESULT(pOCILib, res);

    return res;
}
*/

/* --------------------------------------------------------------------------------------------- *
 * OCI_DequeueGetWaitTime
 * --------------------------------------------------------------------------------------------- */
/*
int OCI_API OCI_DequeueGetWaitTime
(
    OCI_Library *pOCILib,
    OCI_Dequeue *dequeue
)
{
    boolean res = TRUE;
    sb4 ret     = 0;

    OCI_CHECK_PTR(pOCILib, OCI_IPC_DEQUEUE, dequeue, 0);

    OCI_CALL2
    (
    	pOCILib, res, dequeue->typinf->con,

        OCIAttrGet((dvoid *) dequeue->opth,
                   (ub4    ) OCI_DTYPE_AQDEQ_OPTIONS,
                   (dvoid *) &ret,
                   (ub4   *) NULL,
                   (ub4    )  OCI_ATTR_WAIT,
                   pOCILib->err)
    )

    OCI_RESULT(pOCILib, res);

    return (int) ret;
}
*/

/* --------------------------------------------------------------------------------------------- *
 * OCI_DequeueSetWaitTime
 * --------------------------------------------------------------------------------------------- */
boolean OCI_API OCI_DequeueSetWaitTime
(
    OCI_Library*   pOCILib,
    OCI_Dequeue*   dequeue,
    int            timeout,
    ExceptionSink* xsink
)
{
    boolean res = TRUE;
    sb4 value   = (ub4) timeout;

    OCI_CHECK_PTR(pOCILib, OCI_IPC_DEQUEUE, dequeue, FALSE);

    OCI_CALL2Q
    (
    	pOCILib, res, dequeue->typinf->con,

        OCIAttrSet((dvoid *) dequeue->opth,
                   (ub4    ) OCI_DTYPE_AQDEQ_OPTIONS,
                   (dvoid *) &value,
                   (ub4    ) 0,
                   (ub4    )  OCI_ATTR_WAIT,
                   pOCILib->err),

	xsink
    )

    OCI_RESULT(pOCILib, res);

    return res;
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DequeueSetAgentList
 * --------------------------------------------------------------------------------------------- */

/*
boolean OCI_API OCI_DequeueSetAgentList
(
    OCI_Library *pOCILib,
    OCI_Dequeue *dequeue,
    OCI_Agent  **consumers,
    unsigned int count
)
{
    boolean res = TRUE;

    OCI_CHECK_PTR(pOCILib, OCI_IPC_ENQUEUE, dequeue, FALSE);

    OCI_FREE(dequeue->agent_list);

    if ((consumers != NULL) && (count > 0))
    {
        dequeue->agent_list = (OCIAQAgent **) OCI_MemAlloc2(pOCILib, OCI_IPC_ARRAY,  sizeof(OCIAQAgent *),
                                                           count, FALSE);

        if (dequeue->agent_list != NULL)
        {
            unsigned int i;

            for(i = 0; i < count; i++)
            {
                dequeue->agent_list[i] = consumers[i]->handle;
            }

            dequeue->agent_count = (ub4) count;
        }
        else
        {
            res = FALSE;
        }
    }

    OCI_RESULT(pOCILib, res);

    return res;
}
*/

/* --------------------------------------------------------------------------------------------- *
 * OCI_DequeueSubscribe
 * --------------------------------------------------------------------------------------------- */

OCI_EXPORT boolean OCI_API  OCI_DequeueSubscribe
(
    OCI_Library *pOCILib,
    OCI_Dequeue    *dequeue,
    unsigned int    port,
    unsigned int    timeout,
    POCI_NOTIFY_AQ  callback,
    ExceptionSink* xsink
)
{
    boolean res             = TRUE;
    ub4     oci_port        = (ub4) port;
    ub4     oci_timeout     = (ub4) timeout;
    ub4     oci_namespace   = OCI_SUBSCR_NAMESPACE_AQ;
    ub4     oci_protocol    = OCI_SUBSCR_PROTO_OCI;
    ub4     oci_msgpres     = OCI_SUBSCR_PRES_DEFAULT;

    OCI_Connection *con = NULL;

    OCI_CHECK_INITIALIZED2(pOCILib, FALSE);
    OCI_CHECK_DATABASE_NOTIFY_ENABLED(pOCILib, FALSE, xsink);

    OCI_CHECK_PTR(pOCILib, OCI_IPC_DEQUEUE, dequeue, FALSE);

    con = dequeue->typinf->con;

    /* the subscription name ("[schema.]queue[:consumer]") must fit in its buffer below; a longer one is refused, not
       cut, as a cut name could be the name of another queue */
    if (dequeue->name != NULL)
    {
        size_t len = mtslen(dequeue->name);

        if (dequeue->consumer != NULL)
            len += (size_t) 1 + mtslen(dequeue->consumer);

        if (len > (size_t) OCI_SIZE_AQ_SUBSCR_NAME)
        {
            if (xsink)
                xsink->raiseException("ORACLE-OCI-ERROR", "the AQ subscription name (%d bytes) is longer than "
                    "the supported maximum of %d bytes", (int) len, (int) OCI_SIZE_AQ_SUBSCR_NAME);
            OCI_RESULT(pOCILib, FALSE);
            return FALSE;
        }
    }

    /* clear any previous subscription */

    OCI_DequeueUnsubscribe(pOCILib, dequeue, xsink);

    /* allocate subcription handle */

    res = (OCI_SUCCESS == OCI_HandleAlloc2(pOCILib, pOCILib->env,
                                          (dvoid **) (void *) &dequeue->subhp,
                                          OCI_HTYPE_SUBSCRIPTION, (size_t) 0,
                                          (dvoid **) NULL));

    /* set port number */

    if (oci_port > 0)
    {
        OCI_CALL3Q
        (
            pOCILib, res, con->err,

            OCIAttrSet((dvoid *) dequeue->subhp, (ub4) OCI_HTYPE_SUBSCRIPTION,
		       (dvoid *) &oci_port, (ub4) sizeof (oci_port),
		       (ub4) OCI_ATTR_SUBSCR_PORTNO, con->err),

	    xsink
        )
    }

    /* set timeout */

    if (oci_timeout > 0)
    {
        OCI_CALL3Q
        (
            pOCILib, res, con->err,

            OCIAttrSet((dvoid *) dequeue->subhp, (ub4) OCI_HTYPE_SUBSCRIPTION,
		       (dvoid *) &oci_timeout, (ub4) sizeof (oci_timeout),
		       (ub4) OCI_ATTR_SUBSCR_TIMEOUT, con->err),

	    xsink
        )
    }

    /* set name  */

    if (dequeue->name != NULL)
    {
        /* for AQ subscription, the name should be "[shema.]queue[:consumer]" */

        mtext buffer[OCI_SIZE_AQ_SUBSCR_NAME + 1];

        mtext *str  = NULL;

        void *ostr  = NULL;
        int osize   = -1;

        /* the length was checked above */
        size_t name_len = mtslen(dequeue->name);
        size_t pos = name_len;

        memcpy(buffer, dequeue->name, name_len * sizeof(mtext));

        if (dequeue->consumer != NULL)
        {
            size_t consumer_len = mtslen(dequeue->consumer);

            buffer[pos++] = MT(':');
            memcpy(buffer + pos, dequeue->consumer, consumer_len * sizeof(mtext));
            pos += consumer_len;
        }

        assert(pos <= (size_t) OCI_SIZE_AQ_SUBSCR_NAME);
        buffer[pos] = 0;

        /* queue name must be uppercase */

        for (str =  buffer; *str != 0; str++)
        {
            *str = (mtext) mttoupper(*str);
        }

        ostr = OCI_GetInputMetaString(pOCILib, buffer, &osize);

        OCI_CALL3Q
        (
            pOCILib, res, con->err,

            OCIAttrSet((dvoid *) dequeue->subhp, (ub4) OCI_HTYPE_SUBSCRIPTION,
		       (dvoid *) ostr, (ub4) osize,
		       (ub4) OCI_ATTR_SUBSCR_NAME, con->err),

	    xsink
        )

        OCI_ReleaseMetaString(ostr);

	//printd(5, "OCI_DequeueSubscribe() name: '%s' res: %d\n", dequeue->name, (int)res);
    }

    /* set namespace  */

    OCI_CALL3Q
    (
        pOCILib, res, con->err,

        OCIAttrSet((dvoid *) dequeue->subhp, (ub4) OCI_HTYPE_SUBSCRIPTION,
		   (dvoid *) &oci_namespace, (ub4) sizeof(oci_namespace),
		   (ub4) OCI_ATTR_SUBSCR_NAMESPACE, con->err),

	xsink
    )

    //printd(5, "OCI_DequeueSubscribe() set namespace res: %d\n", (int)res);

    /* set context pointer to dequeue structure */

    OCI_CALL3Q
    (
        pOCILib, res, con->err,

        OCIAttrSet((dvoid *) dequeue->subhp, (ub4) OCI_HTYPE_SUBSCRIPTION,
		   (dvoid *) dequeue, (ub4) 0,
		   (ub4) OCI_ATTR_SUBSCR_CTX, con->err),

	xsink
    )

    //printd(5, "OCI_DequeueSubscribe() set context to dequeue res: %d\n", (int)res);

    /* internal callback handler */

    OCI_CALL3Q
    (
        pOCILib, res, con->err,

        OCIAttrSet((dvoid *) dequeue->subhp, (ub4) OCI_HTYPE_SUBSCRIPTION,
		   (dvoid *) OCI_ProcNotifyMessages, (ub4) 0,
		   (ub4) OCI_ATTR_SUBSCR_CALLBACK, con->err),

	xsink
    )

    //printd(5, "OCI_DequeueSubscribe() set internal callback handler res: %d\n", (int)res);

    /* set protocol  */

    OCI_CALL3Q
    (
        pOCILib, res, con->err,

        OCIAttrSet((dvoid *) dequeue->subhp, (ub4) OCI_HTYPE_SUBSCRIPTION,
		   (dvoid *) &oci_protocol, (ub4) sizeof(oci_protocol),
		   (ub4) OCI_ATTR_SUBSCR_RECPTPROTO, con->err),

	xsink
    )

    //printd(5, "OCI_DequeueSubscribe() set protocol res: %d\n", (int)res);

    /* set presentation  */

    OCI_CALL3Q
    (
       pOCILib, res, con->err,

       OCIAttrSet((dvoid *) dequeue->subhp, (ub4) OCI_HTYPE_SUBSCRIPTION,
		  (dvoid *) &oci_msgpres, (ub4) sizeof(oci_msgpres),
		  (ub4) OCI_ATTR_SUBSCR_RECPTPRES, con->err),

       xsink
    )

    //printd(5, "OCI_DequeueSubscribe() set presentation res: %d\n", (int)res);

    /* all attributes set, let's register the subscription ! */

    /* set callback in anticipation of success */
    dequeue->callback = callback;

    OCI_CALL3Q
    (
        pOCILib, res, con->err,

        OCISubscriptionRegister(con->cxt, &dequeue->subhp, (ub2) 1, con->err,(ub4) OCI_DEFAULT),

	xsink
    )

    //printd(5, "OCI_DequeueSubscribe() register subscription res: %d\n", (int)res);

    /* set callback on success */

    if (!res)
    {
        /* clear callback on failure */

        dequeue->callback = 0;

        /* clear subscription on failure */

        OCI_DequeueUnsubscribe(pOCILib, dequeue, xsink);
    }

    OCI_RESULT(pOCILib, res);

    //printd(5, "OCI_DequeueSubscribe() res: %d\n", (int)res);

    return res;
}

/* --------------------------------------------------------------------------------------------- *
 * OCI_DequeueUnsubscribe
 * --------------------------------------------------------------------------------------------- */

OCI_EXPORT boolean OCI_API OCI_DequeueUnsubscribe
(
    OCI_Library *pOCILib,
    OCI_Dequeue *dequeue,
    ExceptionSink* xsink
)
{
    boolean res = TRUE;

    OCI_CHECK_DATABASE_NOTIFY_ENABLED(pOCILib, FALSE, xsink);

    OCI_CHECK_PTR(pOCILib, OCI_IPC_DEQUEUE, dequeue, FALSE);

    dequeue->callback = NULL;

    if (dequeue->subhp != NULL)
    {
        /* unregister the subscription */

        OCI_CALL3Q
        (
            pOCILib, res, dequeue->typinf->con->err,

            OCISubscriptionUnRegister(dequeue->typinf->con->cxt, dequeue->subhp,
                                      pOCILib->err,(ub4) OCI_DEFAULT),

	    xsink
        )

        /* free OCI handle */

        OCI_HandleFree2(pOCILib, (dvoid *) dequeue->subhp, OCI_HTYPE_SUBSCRIPTION);

        dequeue->subhp = NULL;
    }

    OCI_RESULT(pOCILib, res);

    return res;
}
