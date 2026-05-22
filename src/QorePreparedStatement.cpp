/* -*- mode: c++; indent-tabs-mode: nil -*- */
/*
    QorePreparedStatement.cpp

    Qore Programming Language

    Copyright (C) 2006 - 2026 Qore Technologies, s.r.o.

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

#if defined(QDBI_METHOD_SELECT_COLUMNAR) || defined(QDBI_METHOD_STMT_FETCH_COLUMNAR)
#include <qore/QoreColumnarResult.h>
#endif

#include <stdlib.h>
#include <memory>

//------------------------------------------------------------------------------
// QoreOracleCancelHelper implementation
//------------------------------------------------------------------------------

QoreOracleCancelHelper::QoreOracleCancelHelper(OCISvcCtx* svchp, OCIError* errhp)
    : svchp(svchp), errhp(errhp) {
    if (smh && svchp && errhp) {
        // Register cancel callback
        smh->registerCancelCallback(this, [this]() -> bool {
            // Load pointers atomically - they may be set to nullptr by destructor
            OCISvcCtx* svc = this->svchp.load(std::memory_order_acquire);
            OCIError* err = this->errhp.load(std::memory_order_acquire);
            if (svc && err) {
                // OCIBreak cancels the current OCI operation
                sword status = OCIBreak(svc, err);
                return status == OCI_SUCCESS;
            }
            return false;
        });
    }
}

QoreOracleCancelHelper::~QoreOracleCancelHelper() {
    // Set pointers to nullptr atomically before unregistering to prevent
    // use-after-free if a callback is currently being invoked
    svchp.store(nullptr, std::memory_order_release);
    errhp.store(nullptr, std::memory_order_release);
    if (smh) {
        smh->unregisterCancelCallback(this);
    }
}

// OCI callback function for dynamic binds
static sb4 ora_dynamic_bind_callback(void* ictxp, OCIBind* bindp, ub4 iter, ub4 index, void** bufpp, ub4* alenp,
        ub1* piecep, void** indp) {
    AbstractDynamicArrayBindData* arraybind = (AbstractDynamicArrayBindData*)ictxp;
    //printd(5, "ora_dynamic_bind_callback() arraybind: %p iter: %d index: %d\n", arraybind, iter, index);
    arraybind->bindCallback(bindp, iter, bufpp, alenp, piecep, indp);
    return OCI_CONTINUE;
}

// OCI callback function for dynamic binds for output data where no input data is provided
static sb4 ora_dynamic_bind_nodata_callback(void* ictxp, OCIBind* bindp, ub4 iter, ub4 index, void** bufpp,
        ub4* alenp, ub1* piecep, void** indp) {
    AbstractDynamicArrayBindData* arraybind = (AbstractDynamicArrayBindData*)ictxp;
    //printd(5, "ora_dynamic_bind_callback() arraybind: %p iter: %d index: %d\n", arraybind, iter, index);
    arraybind->bindNoDataCallback(bindp, iter, bufpp, alenp, piecep, indp);
    return OCI_CONTINUE;
}

// OCI callback function for dynamic binds
static sb4 ora_dynamic_bind_placeholder_callback(void* ictxp, OCIBind* bindp, ub4 iter, ub4 index, void** bufpp,
        ub4** alenp, ub1* piecep, void** indp, ub2** rcodepp) {
    AbstractDynamicArrayBindData* arraybind = (AbstractDynamicArrayBindData*)ictxp;
    //printd(5, "ora_dynamic_bind_callback() arraybind: %p iter: %d index: %d\n", arraybind, iter, index);
    arraybind->bindPlaceholderCallback(bindp, iter, bufpp, alenp, piecep, indp, rcodepp);
    return OCI_CONTINUE;
}

int OraBindNode::setPlaceholder(QoreValue v, ExceptionSink* xsink) {
    //printd(5, "OraBindNode::setPlaceholder() this: %p v: (%s)\n", this, v.getTypeName());
    resetPlaceholder(xsink, false);

    assert(!array);

    // assume string if no argument passed
    if (v.isNothing()) {
        setPlaceholderIntern(-1, "string", xsink);
        return 0;
    }

    qore_type_t vtype = v.getType();
    if (vtype == NT_HASH) {
        const QoreHashNode* h = v.get<const QoreHashNode>();

        int size = -1;
        QoreValue t = h->getKeyValue("value");
        if (t) {
            assert(!value);
            value = t.refSelf();
        } else {
            // get and check size
            QoreValue sz = h->getKeyValue("size");
            size = sz.isNothing() ? -1 : (int)sz.getAsBigInt();
        }

        // get and check data type
        t = h->getKeyValue("type");
        if (!t) {
            if (value) {
                setPlaceholderIntern(size, value.getTypeName(), xsink);
                return 0;
            }
            xsink->raiseException("BIND-ERROR", "missing 'type' key in placeholder hash");
            return -1;
        }

        if (t.getType() != NT_STRING) {
            xsink->raiseException("BIND-ERROR", "expecting type name as value of 'type' key, got '%s'",
                t.getTypeName());
            return -1;
        }
        QoreStringValueHelper str(t);

        //QoreStringValueHelper strdebug(v);
        //printd(5, "OraBindNode::setPlaceholder() adding placeholder name=%s, size=%d, type=%s, value=%s\n",
        //  tstr.c_str(), size, str->c_str(), strdebug->c_str());
        setPlaceholderIntern(size, str->c_str(), xsink);
    } else if (vtype == NT_STRING) {
        QoreStringValueHelper str(v);
        setPlaceholderIntern(-1, str->c_str(), xsink);
    } else if (vtype == NT_INT) {
        setPlaceholderIntern(v.getAsBigInt(), "string", xsink);
    } else {
        xsink->raiseException("BIND-ERROR", "expecting string or hash for placeholder description, got '%s'",
            v.getTypeName());
        return -1;
    }

    return 0;
}

void OraBindNode::clearPlaceholder(ExceptionSink* xsink) {
    if (dtype) {
        // free buffer data if any
        del(xsink);

        if (alt_output) {
            alt_output = false;
        }
        dtype = 0;
    }
}

void OraBindNode::resetPlaceholder(ExceptionSink* xsink, bool free_name) {
    data.resetPlaceholder(free_name);
    clearPlaceholder(xsink);
}

int OraBindNode::set(QoreValue v, ExceptionSink* xsink) {
    if (isValue()) {
        resetValue(xsink);
        setValue(v, xsink);
        return *xsink ? -1 : 0;
    }

    return setPlaceholder(v, xsink);
}

void OraBindNode::clear(ExceptionSink* xsink, bool free_name) {
    if (isValue()) {
        resetValue(xsink);
    } else {
        clearPlaceholder(xsink);
    }
}

void OraBindNode::reset(ExceptionSink* xsink, bool free_name) {
    if (value) {
        value.discard(xsink);
        value = QoreValue();
    }

    if (isValue())
        resetValue(xsink);
    else
        resetPlaceholder(xsink, free_name);
}

int OraBindNode::setupDateDescriptor(ExceptionSink* xsink) {
    dtype = QORE_SQLT_TIMESTAMP;
    buf.odt = NULL;

    return stmt.setupDateDescriptor(buf.odt, xsink);
}

int OraBindNode::bindDate(int pos, ExceptionSink* xsink) {
    return stmt.bindByPos(bndp, pos, &buf.odt, 0, QORE_SQLT_TIMESTAMP, xsink, pIndicator);
}

void OraBindNode::bind(int pos, ExceptionSink* xsink) {
    //printd(5, "OBN::bind() pos: %d type: %s value: (%s)\n", pos, isValue() ? "value" : "placeholder",
    //  value.getTypeName());
    if (isValue()) {
        bindValue(xsink, pos, value);
        return;
    }

    // TODO/FIXME: NTY is not possible to bind as IN OUT yet
    if (data.isType(ORACLE_OBJECT) || data.isType(ORACLE_COLLECTION)) {
        bindPlaceholder(pos, xsink);
        return;
    }

    if (!value.isNullOrNothing()) {
        if (stmt.isArray()) {
            xsink->raiseException("BIND-ERROR", "position %d: cannot bind in/out values of type '%s' with array "
                "binds", pos, value.getTypeName());
            return;
        }

        // convert value to declared buffer type and then do bind with value
        if (data.isType("string") || data.isType("clob")) {
            QoreStringNodeValueHelper tmp(value);
            bindValue(xsink, pos, *tmp, false);
            //printd(5, "OraBindNode::bind(%d, str='%s' '%s')\n", pos, tmp->c_str(),
            //  value.getType() == NT_STRING ? reinterpret_cast<const QoreStringNode*>(value)->c_str()
            //  : value.getTypeName());
        } else if (data.isType("date")) {
            DateTimeNodeValueHelper tmp(value);
            bindValue(xsink, pos, *tmp, false);
        } else if (data.isType("binary") || data.isType("blob")) {
            if (value.getType() == NT_BINARY)
                bindValue(xsink, pos, value, false);
            else {
                QoreStringNodeValueHelper tmp(value);
                if (!tmp->empty()) {
                    SimpleRefHolder<BinaryNode> bt(new BinaryNode((void*)tmp->c_str(), tmp->size()));
                    bindValue(xsink, pos, *bt, false);
                }
                else {
                    bindPlaceholder(pos, xsink);
                }
            }
        } else if (data.isType("integer")) {
            if (value.getType() == NT_INT)
                bindValue(xsink, pos, value, false);
            else {
                bindValue(xsink, pos, value.getAsBigInt(), false);
            }
        } else if (data.isType("float")) {
            if (value.getType() == NT_FLOAT)
                bindValue(xsink, pos, value, false);
            else {
                bindValue(xsink, pos, value.getAsFloat(), false);
            }
        } else {
            xsink->raiseException("BIND-ERROR", "type '%s' is not supported for SQL binding by value and "
                "placeholder (eg IN OUT)", data.ph_type);
        }
        return;
    }

    bindPlaceholder(pos, xsink);
}

template <typename T>
class PtrVec {
protected:
    // store pointers to char, we use the low bit to flag values that should not be free'ed
    typedef std::vector<size_t> ptrvec_t;
    ptrvec_t ptrvec;

public:
    DLLLOCAL ~PtrVec() {
        for (ptrvec_t::iterator i = ptrvec.begin(), e = ptrvec.end(); i != e; ++i) {
            if (!(*i))
                continue;
            // ignore ptrs with the low bit set
            if (!((*i) & 1))
                free((T*)*i);
        }
    }

    DLLLOCAL const T* get(unsigned i) const {
        size_t v = ptrvec[i];
        if (v & 1)
            v &= ~1;
        return (T*)v;
    }

    DLLLOCAL T* take(unsigned i) {
        size_t v = ptrvec[i];
        assert(!(v & 1));
        ptrvec[i] = 0;
        return (T*)v;
    }

    DLLLOCAL void setDynamic(T* p) {
        ptrvec.push_back((size_t)p);
    }

    DLLLOCAL void setDynamic(int i, T* p) {
        ptrvec[i] = (size_t)p;
    }

    DLLLOCAL void setStatic(const T* p) {
        ptrvec.push_back(((size_t)p) | 1);
    }

    DLLLOCAL size_t size() const {
        return ptrvec.size();
    }

    DLLLOCAL void resize(size_t size) {
        ptrvec.resize(size);
    }

    DLLLOCAL void clear() {
        ptrvec.clear();
    }
};

typedef PtrVec<char> StrVec;

class DynamicArrayBindString : public AbstractDynamicArrayBindData {
protected:
    typedef std::vector<ub4> ub4_list_t;
    ub4_list_t alen_list;
    qore_type_t expected_type;
    const char* expected_type_name;
    // max len assigned to placeholder buffers
    unsigned ph_len = 0;

    StrVec strvec;

public:
    DLLLOCAL DynamicArrayBindString(const QoreListNode* l, qore_type_t et = NT_STRING, const char* etn = "string")
            : AbstractDynamicArrayBindData(l), expected_type(et), expected_type_name(etn) {
    }

    DLLLOCAL virtual ~DynamicArrayBindString() {
    }

    DLLLOCAL virtual int setupBindImpl(OraBindNode& bn, int pos, bool in_only, ExceptionSink* xsink) {
        alen_list.resize(l->size());
        strvec.clear();

        unsigned max = 0;

        const QoreEncoding* enc = bn.stmt.getEncoding();

        ConstListIterator li(l);
        while (li.next()) {
            QoreValue n = li.getValue();
            qore_type_t t = n.getType();

            if (t == NT_NOTHING || t == NT_NULL) {
                ind_list[li.index()] = -1;
                strvec.setDynamic(0);
                continue;
            }

            assert(!ind_list[li.index()]);

            if (t == NT_STRING) {
                QoreStringValueHelper str(n);
                if (in_only && !str.is_temp() && (str->getEncoding() == enc)) {
                    if (str->size() + 1 > max)
                        max = str->size() + 1;
                    strvec.setStatic(str->c_str());
                    alen_list[li.index()] = str->size() + 1;
                } else {
                    // convert to the db encoding
                    TempEncodingHelper nstr(*str, enc, xsink);
                    if (*xsink)
                        return -1;
                    if (nstr->size() + 1 > max)
                        max = nstr->size() + 1;
                    alen_list[li.index()] = nstr->size() + 1;
                    strvec.setDynamic(nstr.giveBuffer());
                }
            } else {
                QoreStringValueHelper nstr(n);
                if (nstr->size() + 1 > max)
                    max = nstr->size() + 1;
                alen_list[li.index()] = nstr->size() + 1;

                std::unique_ptr<QoreString> tstr(nstr.giveString());
                strvec.setDynamic(tstr->giveBuffer());
            }
        }

        // bind as a string
        bn.dtype = SQLT_STR;
        bn.stmt.bindByPos(bn.bndp, pos, 0, max, SQLT_STR, xsink, 0, OCI_DATA_AT_EXEC);

        assert(strvec.size() == l->size());
        //printd(5, "DynamicArrayBindString::setupBind() this: %p size: %d\n", this, (int)l->size());
        return 0;
    }

    DLLLOCAL virtual void bindCallbackImpl(OCIBind* bindp, ub4 iter, void** bufpp, ub4* alenp) {
        *bufpp = (void*)strvec.get(iter);
        *alenp = alen_list[iter];
        //printd(5, "DynamicArrayBindString::bindCallbackImpl() ix: %d bufpp: %p (%s) len: %d\n", iter, bufpp,
        //  strvec.get(iter), alen_list[iter]);
    }

    DLLLOCAL virtual int setupOutputBindImpl(OraBindNode& bn, int pos, ExceptionSink* xsink) {
        strvec.resize(bn.stmt.getArraySize());
        alen_list.resize(bn.stmt.getArraySize());

        bn.dtype = SQLT_STR;
        bn.stmt.bindByPos(bn.bndp, pos, 0, DBI_DEFAULT_STR_LEN + 1, SQLT_STR, xsink, 0, OCI_DATA_AT_EXEC);

        return 0;
    }

    DLLLOCAL virtual void bindPlaceholderCallbackImpl(OCIBind* bindp, ub4 iter, void** bufpp, ub4** alenp) {
        switch (expected_type) {
            case NT_INT: ph_len = 33; break;
            case NT_NUMBER: ph_len = 100; break;
            default: ph_len = DBI_DEFAULT_STR_LEN + 1; break;
        }
        strvec.setDynamic(iter, (char*)malloc(sizeof(char) * ph_len));
        *bufpp = (void*)strvec.get(iter);
        alen_list[iter] = ph_len;
        *alenp = &alen_list[iter];
        //printd(5, "DynamicArrayBindString::bindCallbackImpl() ix: %d bufpp: %p (%s) len: %d\n", iter, bufpp,
        //  strvec.get(iter), alen_list[iter]);
    }

    DLLLOCAL virtual AbstractQoreNode* getOutputValueImpl(ExceptionSink* xsink, OraBindNode& bn, bool destructive) {
        ReferenceHolder<QoreListNode> l(new QoreListNode(autoTypeInfo), xsink);
        assert(ind_list.size() == strvec.size());
        const QoreEncoding* enc = bn.stmt.getEncoding();
        for (unsigned i = 0; i < strvec.size(); ++i) {
            if (ind_list[i]) {
                l->push(null(), xsink);
                continue;
            }
            QoreValue v;
            switch (expected_type) {
                case NT_INT: {
                    const char* buf = strvec.get(i);
                    v = strtoll(buf, 0, 10);
                    break;
                }
                case NT_NUMBER: {
                    int nopt = bn.stmt.getData()->getNumberOption();
                    switch (nopt) {
                        case OPT_NUM_OPTIMAL:
                            v = bn.stmt.getData()->getNumberOptimal(strvec.get(i));
                            break;
                        case OPT_NUM_STRING:
                            v = new QoreStringNode(strvec.get(i), bn.stmt.getEncoding());
                            break;
                        case OPT_NUM_NUMERIC:
                            v = new QoreNumberNode(strvec.get(i));
                            break;
                        default:
                            assert(false);
                    }
                    break;
                }
                default: {
                    char* buf = strvec.take(i);
                    v = buf ? new QoreStringNode(buf, strlen(buf), ph_len, enc) : new QoreStringNode(enc);
                    break;
                }
            }
            l->push(v, xsink);
        }
        return l.release();
    }

    DLLLOCAL virtual int resetImpl(ExceptionSink* xsink) {
        strvec.clear();
        return 0;
    }
};

class DynamicArrayBindFloat : public AbstractDynamicArrayBindData {
protected:
    typedef std::vector<double> doublevec_t;
    doublevec_t vec;

public:
    DLLLOCAL DynamicArrayBindFloat(const QoreListNode* n_l) : AbstractDynamicArrayBindData(n_l) {
    }

    DLLLOCAL virtual ~DynamicArrayBindFloat() {
    }

    DLLLOCAL virtual int setupBindImpl(OraBindNode& bn, int pos, bool in_only, ExceptionSink* xsink) {
        vec.resize(l->size());

        ConstListIterator li(l);
        while (li.next()) {
            QoreValue n = li.getValue();
            qore_type_t t = n.getType();

            if (t == NT_NOTHING || t == NT_NULL) {
                ind_list[li.index()] = -1;
                continue;
            }

            if (t != NT_FLOAT) {
                xsink->raiseException("ARRAY-BIND-ERROR", "found type \"%s\" in list element " QLLD " (starting "
                    "from 0) expecting type \"float\"; all list elements must be of the same type to effect an array "
                    "bind", n.getTypeName(), li.index());
                return -1;
            }

            assert(!ind_list[li.index()]);

            vec[li.index()] = n.getAsFloat();
        }

#if defined(SQLT_BDOUBLE) && defined(USE_NEW_NUMERIC_TYPES)
        bn.dtype = SQLT_BDOUBLE;
        bn.stmt.bindByPos(bn.bndp, pos, 0, sizeof(double), SQLT_BDOUBLE, xsink, 0, OCI_DATA_AT_EXEC);
#else
        bn.dtype = SQLT_FLT;
        bn.stmt.bindByPos(bn.bndp, pos, 0, sizeof(double), SQLT_FLT, xsink, 0, OCI_DATA_AT_EXEC);
#endif

        return 0;
    }

    DLLLOCAL virtual void bindCallbackImpl(OCIBind* bindp, ub4 iter, void** bufpp, ub4* alenp) {
        *bufpp = (void*)&vec[iter];
        *alenp = sizeof(double);
    }

    DLLLOCAL virtual int setupOutputBindImpl(OraBindNode& bn, int pos, ExceptionSink* xsink) {
        assert(false);
        return 0;
    }

    DLLLOCAL virtual void bindPlaceholderCallbackImpl(OCIBind* bindp, ub4 iter, void** bufpp, ub4** alenp) {
        assert(false);
    }

    DLLLOCAL virtual AbstractQoreNode* getOutputValueImpl(ExceptionSink* xsink, OraBindNode& bn, bool destructive) {
        assert(false);
        return 0;
    }

    DLLLOCAL virtual int resetImpl(ExceptionSink* xsink) {
        vec.clear();
        return 0;
    }
};

class DynamicArrayBindDate : public AbstractDynamicArrayBindData {
protected:
    typedef std::vector<OCIDateTime*> datevec_t;
    datevec_t vec;

    DLLLOCAL void resetIntern() {
        for (datevec_t::iterator i = vec.begin(), e = vec.end(); i != e; ++i) {
            if ((*i))
                OCIDescriptorFree(*i, QORE_DTYPE_TIMESTAMP);
        }
        vec.clear();
    }

public:
    DLLLOCAL DynamicArrayBindDate(const QoreListNode* n_l) : AbstractDynamicArrayBindData(n_l) {
    }

    DLLLOCAL virtual ~DynamicArrayBindDate() {
        resetIntern();
    }

    DLLLOCAL virtual int setupBindImpl(OraBindNode& bn, int pos, bool in_only, ExceptionSink* xsink) {
        vec.resize(l->size());

        QoreOracleConnection* conn = (QoreOracleConnection*)bn.stmt.getData();

        ConstListIterator li(l);
        while (li.next()) {
            QoreValue n = li.getValue();
            qore_type_t t = n.getType();

            if (t == NT_NOTHING || t == NT_NULL) {
                ind_list[li.index()] = -1;
                assert(!vec[li.index()]);
                continue;
            }

            if (t != NT_DATE) {
                xsink->raiseException("ARRAY-BIND-ERROR", "found type \"%s\" in list element " QLLD " (starting from "
                    "0) expecting type \"date\"; all list elements must be of the same type to effect an array bind",
                    n.getTypeName(), li.index());
                return -1;
            }

            const DateTimeNode* d = n.get<const DateTimeNode>();

            assert(!ind_list[li.index()]);

            // acquire date descriptor
            if (bn.stmt.setupDateDescriptor(vec[li.index()], xsink))
                return -1;

            if (conn->dateTimeConstruct(vec[li.index()], *d, xsink))
                return -1;
        }

        bn.dtype = QORE_SQLT_TIMESTAMP;
        bn.stmt.bindByPos(bn.bndp, pos, 0, 0, QORE_SQLT_TIMESTAMP, xsink, 0, OCI_DATA_AT_EXEC);

        //printd(5, "DynamicArrayBindDate::setupBind() this: %p size: %d\n", this, (int)l->size());
        return 0;
    }

    DLLLOCAL virtual void bindCallbackImpl(OCIBind* bindp, ub4 iter, void** bufpp, ub4* alenp) {
        *bufpp = (void*)vec[iter];
        *alenp = 0;
    }

    DLLLOCAL virtual int setupOutputBindImpl(OraBindNode& bn, int pos, ExceptionSink* xsink) {
        assert(false);
        return 0;
    }

    DLLLOCAL virtual void bindPlaceholderCallbackImpl(OCIBind* bindp, ub4 iter, void** bufpp, ub4** alenp) {
        assert(false);
    }

    DLLLOCAL virtual AbstractQoreNode* getOutputValueImpl(ExceptionSink* xsink, OraBindNode& bn, bool destructive) {
        assert(false);
        return 0;
    }

    DLLLOCAL virtual int resetImpl(ExceptionSink* xsink) {
        resetIntern();
        return 0;
    }
};

class DynamicArrayBindBool : public AbstractDynamicArrayBindData {
protected:
    typedef std::vector<char> boolvec_t;
    boolvec_t vec;

public:
    DLLLOCAL DynamicArrayBindBool(const QoreListNode* n_l) : AbstractDynamicArrayBindData(n_l) {
    }

    DLLLOCAL virtual ~DynamicArrayBindBool() {
    }

    DLLLOCAL virtual int setupBindImpl(OraBindNode& bn, int pos, bool in_only, ExceptionSink* xsink) {
        vec.resize(l->size());

        ConstListIterator li(l);
        while (li.next()) {
            QoreValue n = li.getValue();
            qore_type_t t = n.getType();

            if (t == NT_NOTHING || t == NT_NULL) {
                ind_list[li.index()] = -1;
                continue;
            }

            if (t != NT_BOOLEAN) {
                xsink->raiseException("ARRAY-BIND-ERROR", "found type \"%s\" in list element " QLLD " (starting from "
                    "0) expecting type \"boolean\"; all list elements must be of the same type to effect an array "
                    "bind", n.getTypeName(), li.index());
                return -1;
            }

            assert(!ind_list[li.index()]);

            vec[li.index()] = n.getAsBool();
        }

        bn.dtype = SQLT_INT;
        bn.stmt.bindByPos(bn.bndp, pos, 0, sizeof(char), SQLT_INT, xsink, 0, OCI_DATA_AT_EXEC);

        return 0;
    }

    DLLLOCAL virtual void bindCallbackImpl(OCIBind* bindp, ub4 iter, void** bufpp, ub4* alenp) {
        *bufpp = (void*)&vec[iter];
        *alenp = sizeof(char);
    }

    DLLLOCAL virtual void bindPlaceholderCallbackImpl(OCIBind* bindp, ub4 iter, void** bufpp, ub4** alenp) {
        assert(false);
    }

    DLLLOCAL virtual int setupOutputBindImpl(OraBindNode& bn, int pos, ExceptionSink* xsink) {
        // currently not used for output binding
        assert(false);
        return 0;
    }

    DLLLOCAL virtual AbstractQoreNode* getOutputValueImpl(ExceptionSink* xsink, OraBindNode& bn, bool destructive) {
        // currently not used for output binding
        assert(false);
        return 0;
    }

    DLLLOCAL virtual int resetImpl(ExceptionSink* xsink) {
        vec.clear();
        return 0;
    }
};

/*
typedef PtrVec<void> BinVec;

class DynamicArrayBindBinary : public AbstractDynamicArrayBindData {
public:
    DLLLOCAL DynamicArrayBindBinary(const QoreListNode* n_l) : AbstractDynamicArrayBindData(n_l) {
    }

    DLLLOCAL virtual ~DynamicArrayBindBinary() {
    }

    DLLLOCAL virtual int setupBindImpl(OraBindNode& bn, int pos, bool in_only, ExceptionSink* xsink) {
        alen_list.resize(l->size());

        unsigned max = 0;

        ConstListIterator li(l);
        while (li.next()) {
            const AbstractQoreNode* n = li.getValue();
            qore_type_t t = get_node_type(n);

            if (t == NT_NOTHING || t == NT_NULL) {
                ind_list[li.index()] = -1;
                binvec.setDynamic(0);
                continue;
            }

            if (t != NT_BINARY) {
                xsink->raiseException("ARRAY-BIND-ERROR", "found type \"%s\" in list element " QLLD " (starting from "
                    "0) expecting type \"binary\"; all list elements must be of the same type to effect an array "
                    "bind", n.getTypeName(), li.index());
                return -1;
            }

            assert(!ind_list[li.index()]);

            const BinaryNode* b = reinterpret_cast<const BinaryNode*>(n);

            if (b->size() > max)
                max = b->size();

            alen_list[li.index()] = b->size();

            if (in_only)
                binvec.setStatic(b->getPtr());
            else {
                void* p = malloc(b->size());
                if (!p) {
                xsink->outOfMemory();
                return -1;
                }
                memcpy(p, b->getPtr(), b->size());
                binvec.setDynamic(p);
            }
        }

        bn.dtype = SQLT_BIN;
        bn.stmt.bindByPos(bn.bndp, pos, 0, max, SQLT_BIN, xsink, 0, OCI_DATA_AT_EXEC);

        assert(binvec.size() == l->size());
        //printd(5, "DynamicArrayBindBinary::setupBind() this: %p size: %d\n", this, (int)l->size());
        return 0;
    }

    DLLLOCAL virtual void bindCallbackImpl(OCIBind* bindp, ub4 iter, void** bufpp, ub4* alenp) {
        *bufpp = (void*)binvec.get(iter);
        *alenp = alen_list[iter];
        //printd(5, "DynamicArrayBindBinary::bindCallbackImpl() ix: %d bufpp: %p (%s) len: %d\n", iter, bufpp,
        //  binvec.get(iter), alen_list[iter]);
    }

    DLLLOCAL virtual int setupOutputBindImpl(OraBindNode& bn, int pos, ExceptionSink* xsink) {
        assert(false);
        return 0;
    }

    DLLLOCAL virtual void bindPlaceholderCallbackImpl(OCIBind* bindp, ub4 iter, void** bufpp, ub4** alenp) {
        assert(false);
    }

    DLLLOCAL virtual AbstractQoreNode* getOutputValueImpl(ExceptionSink* xsink, OraBindNode& bn, bool destructive) {
        assert(false);
        return 0;
    }

    DLLLOCAL virtual int resetImpl(ExceptionSink* xsink) {
        binvec.clear();
        return 0;
    }

protected:
    typedef std::vector<ub4> ub4_list_t;
    ub4_list_t alen_list;

    BinVec binvec;
};
*/

class DynamicArrayBindBinaryGenericLob : public AbstractDynamicArrayBindData {
public:
    DLLLOCAL DynamicArrayBindBinaryGenericLob(const QoreListNode* l, bool blob)
            : AbstractDynamicArrayBindData(l), lob_type(blob ? SQLT_BLOB : SQLT_CLOB),
            t(blob ? NT_BINARY : NT_STRING), tname(blob ? "binary" : "string") {
        clear();
    }

    DLLLOCAL virtual ~DynamicArrayBindBinaryGenericLob() {
    }

    DLLLOCAL virtual int setupBindImpl(OraBindNode& bn, int pos, bool in_only, ExceptionSink* xsink) {
        lhvec.resize(l->size());
        lavec.resize(l->size());

        assert(!conn);
        conn = bn.stmt.getData();

        const QoreEncoding* enc = bn.stmt.getEncoding();

        ConstListIterator li(l);
        while (li.next()) {
            size_t ind = li.index();

            QoreValue n = li.getValue();
            qore_type_t t = n.getType();

            if (t == NT_NOTHING || t == NT_NULL) {
                ind_list[ind] = -1;
                lhvec[ind] = 0;
                continue;
            }

            if (t != this->t) {
                xsink->raiseException("ARRAY-BIND-ERROR", "found type \"%s\" in list element " QLLD " (starting from "
                    "0) expecting type \"%s\"; all list elements must be of the same type to effect an array "
                    "bind", n.getTypeName(), ind, tname);
                return -1;
            }

            // allocate LOB descriptor
            if (conn->descriptorAlloc((dvoid**)&lhvec[ind], OCI_DTYPE_LOB,
                "DynamicArrayBindBinaryGenericLob::setupBindImpl() alloc LOB descriptor", xsink)) {
                return -1;
            }

            assert(lhvec[ind]);
            assert(!ind_list[ind]);

            // create temporary LOB
            if (conn->checkerr(OCILobCreateTemporary(conn->svchp, conn->errhp, lhvec[ind], OCI_DEFAULT, OCI_DEFAULT,
                lob_type == SQLT_BLOB ? OCI_TEMP_BLOB : OCI_TEMP_CLOB, FALSE, OCI_DURATION_SESSION),
                "DynamicArrayBindBinaryGenericLob::setupBindImpl() create temporary LOB", xsink)) {
                return -1;
            }

            lavec[ind] = true;

            void* ptr;
            size_t size;
            if (getBindData(ptr, size, n, enc, xsink)) {
                assert(*xsink);
                return -1;
            }
            //printd(5, "DynamicArrayBindBinaryGenericLob::setupBindImpl() %lu/%lu: descr: %p p: %p len: %lu\n", ind,
            //    l->size(), lhvec[ind], b->getPtr(), b->size());

            // write the buffer data into the CLOB
            if (conn->writeLob(lhvec[ind], ptr, size, true,
                "DynamicArrayBindBinaryGenericLob::setupBindImpl() write LOB", xsink)) {
                return -1;
            }
        }

        bn.dtype = lob_type;
        bn.stmt.bindByPos(bn.bndp, pos, 0, sizeof(OCILobLocator*), lob_type, xsink, 0, OCI_DATA_AT_EXEC);

        //printd(5, "DynamicArrayBindBinaryBlob::setupBind() this: %p size: %d (%s)\n", this, (int)l->size(),
        //    lob_type == SQLT_BLOB ? "BLOB" : "CLOB");
        return 0;
    }

    DLLLOCAL virtual void bindCallbackImpl(OCIBind* bindp, ub4 iter, void** bufpp, ub4* alenp) {
        *bufpp = (void*)lhvec[iter];
        *alenp = 0;
        //printd(5, "DynamicArrayBindBinaryBlob::bindCallbackImpl() ix: %d bufpp: %p (%p)\n", iter, bufpp,
        //    lhvec[iter]);
    }

    DLLLOCAL virtual int setupOutputBindImpl(OraBindNode& bn, int pos, ExceptionSink* xsink) {
        assert(false);
        return 0;
    }

    DLLLOCAL virtual void bindPlaceholderCallbackImpl(OCIBind* bindp, ub4 iter, void** bufpp, ub4** alenp) {
        assert(false);
    }

    DLLLOCAL virtual AbstractQoreNode* getOutputValueImpl(ExceptionSink* xsink, OraBindNode& bn, bool destructive) {
        assert(false);
        return 0;
    }

    DLLLOCAL virtual int resetImpl(ExceptionSink* xsink) {
        clear();
        return 0;
    }

protected:
    typedef std::vector<ub4> ub4_list_t;

    // type of a vector of LOB handles
    typedef std::vector<OCILobLocator*> lhvec_t;
    lhvec_t lhvec;

    // a vector of bools
    typedef std::vector<char> boolvec_t;
    // "lob allocated" vector
    boolvec_t lavec;

    int lob_type;
    qore_type_t t;
    const char* tname;

    QoreOracleConnection* conn = nullptr;

    DLLLOCAL void clear() {
        for (size_t i = 0; i < lhvec.size(); ++i) {
            if (lhvec[i]) {
                if (lavec[i]) {
                    OCILobFreeTemporary(conn->svchp, conn->errhp, lhvec[i]);
                }
                OCIDescriptorFree(lhvec[i], OCI_DTYPE_LOB);
            }
        }
        lhvec.clear();
        lavec.clear();
        conn = nullptr;
    }

    DLLLOCAL virtual int getBindData(void*& ptr, size_t& size, const QoreValue& n, const QoreEncoding* enc,
            ExceptionSink* xsink) = 0;
};

class DynamicArrayBindBinaryBlob : public DynamicArrayBindBinaryGenericLob {
public:
    DLLLOCAL DynamicArrayBindBinaryBlob(const QoreListNode* l) : DynamicArrayBindBinaryGenericLob(l, true) {
    }

    DLLLOCAL virtual int getBindData(void*& ptr, size_t& size, const QoreValue& n, const QoreEncoding* enc,
            ExceptionSink* xsink) {
        const BinaryNode* b = n.get<const BinaryNode>();
        ptr = (void*)b->getPtr();
        size = b->size();
        return 0;
    }
};

class DynamicArrayBindClob : public DynamicArrayBindBinaryGenericLob {
public:
    DLLLOCAL DynamicArrayBindClob(const QoreListNode* l) : DynamicArrayBindBinaryGenericLob(l, false) {
    }

    DLLLOCAL virtual int getBindData(void*& ptr, size_t& size, const QoreValue& n, const QoreEncoding* enc,
            ExceptionSink* xsink) {
        QoreStringValueHelper str(n);

        if (!str.is_temp() && str->getEncoding() == enc) {
            ptr = (void*)str->c_str();
            size = str->size();
            return 0;
        }

        // convert to the db encoding
        TempEncodingHelper nstr(*str, enc, xsink);
        if (*xsink) {
            return -1;
        }
        size = nstr->size();
        char* buf = nstr.giveBuffer();
        ptr = (void*)buf;
        strvec.setDynamic(buf);
        return 0;
    }

protected:
    StrVec strvec;
};

class AbstractDynamicSingleValue : public AbstractDynamicArrayBindData {
public:
    DLLLOCAL AbstractDynamicSingleValue() : AbstractDynamicArrayBindData(0) {
    }

    DLLLOCAL virtual ~AbstractDynamicSingleValue() {
    }

    DLLLOCAL virtual void bindPlaceholderCallbackImpl(OCIBind* bindp, ub4 iter, void** bufpp, ub4** alenp) {
        assert(false);
    }

    DLLLOCAL virtual int setupOutputBindImpl(OraBindNode& bn, int pos, ExceptionSink* xsink) {
        // currently not used for output binding
        assert(false);
        return 0;
    }

    DLLLOCAL virtual AbstractQoreNode* getOutputValueImpl(ExceptionSink* xsink, OraBindNode& bn, bool destructive) {
        // currently not used for output binding
        assert(false);
        return 0;
    }

    DLLLOCAL virtual int resetImpl(ExceptionSink* xsink) {
        return 0;
    }
};

class DynamicSingleValueNull : public AbstractDynamicSingleValue {
protected:

public:
    DLLLOCAL DynamicSingleValueNull() {
    }

    DLLLOCAL virtual ~DynamicSingleValueNull() {
    }

    DLLLOCAL virtual int setupBindImpl(OraBindNode& bn, int pos, bool in_only, ExceptionSink* xsink) {
        ind_list[0] = -1;

        bn.dtype = SQLT_STR;
        bn.stmt.bindByPos(bn.bndp, pos, 0, sizeof(char), SQLT_STR, xsink, 0, OCI_DATA_AT_EXEC);

        return 0;
    }

    DLLLOCAL virtual void bindCallbackImpl(OCIBind* bindp, ub4 iter, void** bufpp, ub4* alenp) {
        *bufpp = 0;
        *alenp = 0;
    }
};

class DynamicSingleValueInt : public AbstractDynamicSingleValue {
protected:
    int64 v;

public:
    DLLLOCAL DynamicSingleValueInt(int64 n_v) : v(n_v) {
    }

    DLLLOCAL virtual ~DynamicSingleValueInt() {
    }

    DLLLOCAL virtual int setupBindImpl(OraBindNode& bn, int pos, bool in_only, ExceptionSink* xsink) {
        ind_list[0] = 0;

        bn.dtype = SQLT_INT;
        bn.stmt.bindByPos(bn.bndp, pos, 0, sizeof(int64), SQLT_INT, xsink, 0, OCI_DATA_AT_EXEC);

        return 0;
    }

    DLLLOCAL virtual void bindCallbackImpl(OCIBind* bindp, ub4 iter, void** bufpp, ub4* alenp) {
        *bufpp = &v;
        *alenp = sizeof(int64);
    }
};

class DynamicSingleValueFloat : public AbstractDynamicSingleValue {
protected:
    double f;

public:
    DLLLOCAL DynamicSingleValueFloat(double n_f) : f(n_f) {
    }

    DLLLOCAL virtual ~DynamicSingleValueFloat() {
    }

    DLLLOCAL virtual int setupBindImpl(OraBindNode& bn, int pos, bool in_only, ExceptionSink* xsink) {
        ind_list[0] = 0;

#if defined(SQLT_BDOUBLE) && defined(USE_NEW_NUMERIC_TYPES)
        bn.dtype = SQLT_BDOUBLE;
        bn.stmt.bindByPos(bn.bndp, pos, 0, sizeof(double), SQLT_BDOUBLE, xsink, 0, OCI_DATA_AT_EXEC);
#else
        bn.dtype = SQLT_FLT;
        bn.stmt.bindByPos(bn.bndp, pos, 0, sizeof(double), SQLT_FLT, xsink, 0, OCI_DATA_AT_EXEC);
#endif

        return 0;
    }

    DLLLOCAL virtual void bindCallbackImpl(OCIBind* bindp, ub4 iter, void** bufpp, ub4* alenp) {
        *bufpp = &f;
        *alenp = sizeof(double);
    }
};

class DynamicSingleValueString : public AbstractDynamicSingleValue {
protected:
    QoreStringNode* str;
    bool deref;

public:
    DLLLOCAL DynamicSingleValueString(QoreStringNode* s, bool dr) : str(s), deref(dr) {
    }

    DLLLOCAL virtual ~DynamicSingleValueString() {
        if (deref)
            str->deref();
    }

    DLLLOCAL virtual int setupBindImpl(OraBindNode& bn, int pos, bool in_only, ExceptionSink* xsink) {
        ind_list[0] = 0;

        bn.dtype = SQLT_STR;
        bn.stmt.bindByPos(bn.bndp, pos, 0, str->size() + 1, SQLT_STR, xsink, 0, OCI_DATA_AT_EXEC);

        return 0;
    }

    DLLLOCAL virtual void bindCallbackImpl(OCIBind* bindp, ub4 iter, void** bufpp, ub4* alenp) {
        *bufpp = (void*)str->c_str();
        *alenp = str->size() + 1;
    }
};

class DynamicSingleValueDate : public AbstractDynamicSingleValue {
protected:
    const DateTimeNode* d;
    OCIDateTime* dt;

public:
    DLLLOCAL DynamicSingleValueDate(const DateTimeNode* n_d) : d(n_d), dt(0) {
    }

    DLLLOCAL virtual ~DynamicSingleValueDate() {
    }

    DLLLOCAL virtual int setupBindImpl(OraBindNode& bn, int pos, bool in_only, ExceptionSink* xsink) {
        ind_list[0] = 0;

        QoreOracleConnection* conn = (QoreOracleConnection*)bn.stmt.getData();

        // acquire date descriptor
        if (bn.stmt.setupDateDescriptor(dt, xsink))
            return -1;

        if (conn->dateTimeConstruct(dt, *d, xsink))
            return -1;

        bn.dtype = QORE_SQLT_TIMESTAMP;
        bn.stmt.bindByPos(bn.bndp, pos, 0, 0, QORE_SQLT_TIMESTAMP, xsink, 0, OCI_DATA_AT_EXEC);

        return 0;
    }

    DLLLOCAL virtual void bindCallbackImpl(OCIBind* bindp, ub4 iter, void** bufpp, ub4* alenp) {
        *bufpp = (void*)dt;
        *alenp = 0;
    }
};

class DynamicSingleValueBinary : public AbstractDynamicSingleValue {
protected:
    const BinaryNode* b;
    bool lob_allocated;
    OCILobLocator* loc;
    QoreOracleConnection* conn;

public:
    DLLLOCAL DynamicSingleValueBinary(const BinaryNode* n_b) : b(n_b), lob_allocated(false), loc(0), conn(0) {
    }

    DLLLOCAL virtual ~DynamicSingleValueBinary() {
        if (loc) {
            if (lob_allocated) {
                assert(conn);
                OCILobFreeTemporary(conn->svchp, conn->errhp, loc);
            }
            OCIDescriptorFree(loc, OCI_DTYPE_LOB);
        }
    }

    DLLLOCAL virtual int setupBindImpl(OraBindNode& bn, int pos, bool in_only, ExceptionSink* xsink) {
        ind_list[0] = 0;

        if (b->size() > LOB_THRESHOLD) {
            assert(!conn);
            conn = bn.stmt.getData();

            // allocate LOB descriptor
            if (conn->descriptorAlloc((dvoid**)&loc, OCI_DTYPE_LOB, "DynamicSingleValueBinary::setupBindImpl() alloc "
                "LOB descriptor", xsink)) {
                return -1;
            }

            // create temporary BLOB
            if (conn->checkerr(OCILobCreateTemporary(conn->svchp, conn->errhp, loc, OCI_DEFAULT, OCI_DEFAULT,
                OCI_TEMP_BLOB, FALSE, OCI_DURATION_SESSION),
                "DynamicSingleValueBinary::setupBindImpl() create temporary BLOB", xsink)) {
                return -1;
            }

            lob_allocated = true;

            // write the buffer data into the CLOB
            if (conn->writeLob(loc, (void*)b->getPtr(), b->size(), true, "DynamicSingleValueBinary::setupBindImpl() "
                "write LOB", xsink)) {
                return -1;
            }

            bn.dtype = SQLT_BLOB;
            bn.stmt.bindByPos(bn.bndp, pos, 0, sizeof(OCILobLocator*), SQLT_BLOB, xsink, 0, OCI_DATA_AT_EXEC);
        } else {
            bn.dtype = SQLT_BIN;
            bn.stmt.bindByPos(bn.bndp, pos, 0, b->size(), SQLT_BIN, xsink, 0, OCI_DATA_AT_EXEC);
        }

        return 0;
    }

    DLLLOCAL virtual void bindCallbackImpl(OCIBind* bindp, ub4 iter, void** bufpp, ub4* alenp) {
        if (loc) {
            *bufpp = (void*)loc;
            *alenp = 0;
        } else {
            *bufpp = (void*)b->getPtr();
            *alenp = (ub4)b->size();
        }
    }
};

void OraBindNode::bindListValue(ExceptionSink* xsink, int pos, QoreValue v, bool in_only) {
    array = true;
    buf.arraybind = 0;

    //printd(5, "OraBindNode::bindListValue() this: %p array: %d\n", this, array);

    qore_type_t t = v.getType();
    if (t != NT_LIST) {
        switch (t) {
            case NT_NOTHING:
            case NT_NULL:
                buf.arraybind = new DynamicSingleValueNull();
                break;

            case NT_INT:
                buf.arraybind = new DynamicSingleValueInt(v.getAsBigInt());
                break;

            case NT_BOOLEAN:
                buf.arraybind = new DynamicSingleValueInt((int)v.getAsBool() ? 1 : 0);
                break;

            case NT_FLOAT:
                buf.arraybind = new DynamicSingleValueFloat(v.getAsFloat());
                break;

            case NT_NUMBER: {
                const QoreNumberNode* n = v.get<const QoreNumberNode>();
                QoreStringNode* tstr = new QoreStringNode(stmt.getEncoding());
                n->getStringRepresentation(*tstr);
                buf.arraybind = new DynamicSingleValueString(tstr, true);
                break;
            }

            case NT_DATE: {
                const DateTimeNode* d = v.get<const DateTimeNode>();
                buf.arraybind = new DynamicSingleValueDate(d);
                break;
            }

            case NT_STRING: {
                QoreStringNodeValueHelper bstr(v);
                if (bstr->getEncoding() != stmt.getEncoding()) {
                    QoreStringNode* tmp = bstr->convertEncoding(stmt.getEncoding(), xsink);
                    if (*xsink)
                        return;
                    buf.arraybind = new DynamicSingleValueString(tmp, true);
                }
                else if (bstr.is_temp()) {
                    buf.arraybind = new DynamicSingleValueString(bstr.getReferencedValue(), true);
                } else {
                    buf.arraybind = new DynamicSingleValueString(const_cast<QoreStringNode*>(*bstr), false);
                }
                break;
            }

            case NT_BINARY:
                buf.arraybind = new DynamicSingleValueBinary(v.get<const BinaryNode>());
                break;

            default:
                xsink->raiseException("ORACLE-BIND-VALUE-ERROR", "single-value type '%s' is not supported for SQL "
                    "array binding", v.getTypeName());
                return;
        }
    } else {
        // process list and get element type
        t = NT_NOTHING;
        const char* type_name = 0;

        const QoreListNode*l = v.get<const QoreListNode>();

        bool clob = false;
        bool strresolved = false;

        // first get data type for bind and set up array bind data object
        ConstListIterator li(l);
        while (li.next()) {
            QoreValue n = li.getValue();
            qore_type_t nt = n.getType();
            if (nt == NT_NOTHING || nt == NT_NULL)
                continue;

            if (type_name) {
                if (t != nt) {
                    xsink->raiseException("ORACLE-BIND-VALUE-ERROR", "mixed types in list bind; got type \"%s\" in "
                        "list of type \"%s\"", n.getTypeName(), type_name);
                    return;
                }
            } else {
                t = nt;
                type_name = n.getTypeName();
            }

            switch (t) {
                case NT_STRING: {
                    if (!strresolved) {
                        ConstListIterator li0(l);
                        while (li0.next()) {
                            QoreValue n = li0.getValue();
                            if (n.getType() != NT_STRING) {
                                continue;
                            }
                            QoreStringValueHelper str(n);
                            if (str->size() >= 32768) {
                                clob = true;
                                break;
                            }
                        }
                        strresolved = true;
                    }
                    if (clob) {
                        buf.arraybind = new DynamicArrayBindClob(l);
                    } else {
                        buf.arraybind = new DynamicArrayBindString(l);
                    }
                    break;
                }

                case NT_INT: {
                    buf.arraybind = new DynamicArrayBindString(l, NT_NUMBER, "number");
                    break;
                }

                case NT_NUMBER: {
                    buf.arraybind = new DynamicArrayBindString(l, NT_NUMBER, "number");
                    break;
                }

                case NT_FLOAT: {
                    buf.arraybind = new DynamicArrayBindFloat(l);
                    break;
                }

                case NT_DATE: {
                    buf.arraybind = new DynamicArrayBindDate(l);
                    break;
                }

                case NT_BOOLEAN: {
                    buf.arraybind = new DynamicArrayBindBool(l);
                    break;
                }

                case NT_BINARY: {
                    buf.arraybind = new DynamicArrayBindBinaryBlob(l);
                    break;
                }

                default:
                    xsink->raiseException("ORACLE-BIND-VALUE-ERROR", "type '%s' is not supported for SQL array "
                        "binding", n.getTypeName());
                return;
            }
            break;
        }

        // this is possible if the entire list is NOTHING or NULL
        if (t == NT_NOTHING) {
            assert(!buf.arraybind);
            buf.arraybind = new DynamicSingleValueNull();
        }
    }

    assert(buf.arraybind);
    if (buf.arraybind->setupBind(*this, pos, in_only, xsink)) {
        return;
    }
    // execute OCIBindDynamic()
    QoreOracleConnection* conn = stmt.getData();
    conn->checkerr(OCIBindDynamic(bndp, conn->errhp, (void*)buf.arraybind, ora_dynamic_bind_callback, 0, 0),
        "OraBindNode::bindListValue()", xsink);
    //printd(5, "OraBindNode::bindListValue() OCIBindDynamic t: %d\n", t);
}

void OraBindNode::bindValue(ExceptionSink* xsink, int pos, QoreValue v, bool in_only) {
    QoreOracleConnection* conn = stmt.getData();

    ind = 0;

    //printd(5, "OraBindNode::bindValue() type: %s\n", v.getTypeName());

    // process list binds first
    if (stmt.isArray()) {
        bindListValue(xsink, pos, v, in_only);
        return;
    }

    qore_type_t ntype = v.getType();

    // bind a NULL
    if (ntype == NT_NOTHING || ntype == NT_NULL) {
        stmt.bindByPos(bndp, pos, 0, 0, SQLT_STR, xsink, pIndicator);
        return;
    }

    if (ntype == NT_STRING) {
        QoreStringValueHelper bstr(v);

        qore_size_t len;

        // convert to target encoding if necessary
        TempEncodingHelper nstr(*bstr, stmt.getEncoding(), xsink);
        if (*xsink)
            return;

        if (in_only) {
            len = nstr->size();
            buf.ptr = (void*)nstr->c_str();
            if (nstr.is_temp()) {
                QoreString* saved = new QoreString(nstr.giveBuffer(), len, len + 1, nstr->getEncoding());
                buf.ptr = (void*)saved->c_str();
                data.save(saved);
            } else if (bstr.is_temp()) {
                QoreString* saved = new QoreString(bstr.giveBuffer(), len, len + 1, bstr->getEncoding());
                buf.ptr = (void*)saved->c_str();
                data.save(saved);
            }
        } else {
            qore_size_t mx = data.ph_maxsize > 0 ? data.ph_maxsize : 0;
            nstr.makeTemp();

            if (mx <= 0)
                mx = DBI_DEFAULT_STR_LEN;
            if (mx > nstr->capacity()) {
                QoreString* tstr = const_cast<QoreString*>(*nstr);
                tstr->allocate(mx);
            }

            len = nstr->capacity() - 1;
            buf.ptr = (void*)nstr.giveBuffer();
        }

        // bind it
        if ((len + 1) > LOB_THRESHOLD && in_only) {
            //printd(5, "binding string %p len: " QLLD " as CLOB\n", buf.ptr, len);
            // bind as a CLOB
            dtype = SQLT_CLOB;

            // allocate LOB descriptor
            if (conn->descriptorAlloc((dvoid**)&strlob, OCI_DTYPE_LOB, "OraBindNode::bindValue() alloc LOB descriptor", xsink))
                return;

            // create temporary CLOB
            if (conn->checkerr(OCILobCreateTemporary(conn->svchp, conn->errhp, strlob, OCI_DEFAULT, OCI_DEFAULT, OCI_TEMP_CLOB, FALSE, OCI_DURATION_SESSION),
                                "OraBindNode::bindValue() create temporary CLOB", xsink))
                return;

            lob_allocated = true;

            // write the buffer data into the CLOB
            if (conn->writeLob(strlob, buf.ptr, len, true, "OraBindNode::bindValue() write CLOB", xsink))
                return;

            stmt.bindByPos(bndp, pos, &strlob, 0, SQLT_CLOB, xsink, pIndicator);
        } else {
            dtype = SQLT_STR;
            // bind as a string
            stmt.bindByPos(bndp, pos, buf.ptr, len + 1, SQLT_STR, xsink, pIndicator);
            //printd(5, "OraBindNode::bindValue() this: %p size: %d '%s'\n", this, len + 1, buf.ptr);
        }

        //printd(5, "OraBindNode::bindValue() this: %p, buf.ptr: %p\n", this, buf.ptr);
        return;
    }

    if (ntype == NT_DATE) {
        const DateTimeNode* d = v.get<const DateTimeNode>();

        if (setupDateDescriptor(xsink))
            return;

        if (conn->dateTimeConstruct(buf.odt, *d, xsink))
            return;

        bindDate(pos, xsink);
        return;
    }

    if (ntype == NT_BINARY) {
        const BinaryNode* b = v.get<const BinaryNode>();

        qore_size_t len = b->size();

        if (len > LOB_THRESHOLD && in_only) {
            //printd(5, "binding binary %p len: " QLLD " as BLOB\n", buf.ptr, len);
            // bind as a BLOB
            dtype = SQLT_BLOB;

            buf.ptr = (void*)b->getPtr();

            // allocate LOB descriptor
            if (conn->descriptorAlloc((dvoid**)&strlob, OCI_DTYPE_LOB, "OraBindNode::bindValue() alloc LOB descriptor", xsink))
                return;

            // create temporary BLOB
            if (conn->checkerr(OCILobCreateTemporary(conn->svchp, conn->errhp, strlob, OCI_DEFAULT, OCI_DEFAULT, OCI_TEMP_BLOB, FALSE, OCI_DURATION_SESSION),
                                "OraBindNode::bindValue() create temporary BLOB", xsink))
                return;

            lob_allocated = true;

            // write the buffer data into the CLOB
            if (conn->writeLob(strlob, buf.ptr, len, true, "OraBindNode::bindValue() write LOB", xsink))
                return;

            stmt.bindByPos(bndp, pos, &strlob, 0, SQLT_BLOB, xsink, pIndicator);
        } else {
            dtype = SQLT_BIN;

            //printd(5, "OraBindNode::bindValue() BLOB ptr: %p size: " QLLD "\n", b->getPtr(), b->size());

            if (!in_only) {
                // bind a copy of the value in case of in/out variables
                SimpleRefHolder<BinaryNode> tb(b->copy());
                assert(tb->size() == len);
                buf.ptr = (void*)tb->giveBuffer();
            } else
                buf.ptr = (void*)b->getPtr();

            stmt.bindByPos(bndp, pos, buf.ptr, len, SQLT_BIN, xsink, pIndicator);
        }
        return;
    }

    if (ntype == NT_BOOLEAN) {
        dtype = SQLT_INT;
        buf.i8 = v.getAsBool();

        stmt.bindByPos(bndp, pos, &buf.i8, sizeof(int64), SQLT_INT, xsink, pIndicator);
        return;
    }

    if (ntype == NT_INT) {
        int64 i = v.getAsBigInt();
        if (i <= MAXINT32 && i >= -MAXINT32) {
            dtype = SQLT_INT;
            buf.i8 = (int64)(int)i;

            stmt.bindByPos(bndp, pos, &buf.i8, sizeof(int64), SQLT_INT, xsink, pIndicator);
        } else { // bind as a string value
            dtype = SQLT_STR;

            QoreString* tstr = new QoreString(stmt.getEncoding());
            tstr->sprintf(QLLD, i);
            data.save(tstr);

            //printd(5, "binding number '%s'\n", buf.ptr);
            stmt.bindByPos(bndp, pos, (char*)tstr->c_str(), tstr->strlen() + 1, SQLT_STR, xsink, pIndicator);
        }
        return;
    }

    if (ntype == NT_NUMBER) {
        const QoreNumberNode* n = v.get<const QoreNumberNode>();
        // bind as a string value
        dtype = SQLT_STR;

        QoreString* tstr = new QoreString(stmt.getEncoding());
        n->getStringRepresentation(*tstr);
        data.save(tstr);

        //printd(5, "binding number '%s'\n", buf.ptr);
        stmt.bindByPos(bndp, pos, (char* )tstr->c_str(), tstr->strlen() + 1, SQLT_STR, xsink, pIndicator);
        return;
    }

    if (ntype == NT_FLOAT) {
        buf.f8 = v.getAsFloat();
#if defined(SQLT_BDOUBLE) && defined(USE_NEW_NUMERIC_TYPES)
        dtype = SQLT_BDOUBLE;
        stmt.bindByPos(bndp, pos, &buf.f8, sizeof(double), SQLT_BDOUBLE, xsink, pIndicator);
#else
        dtype = SQLT_FLT;
        stmt.bindByPos(bndp, pos, &buf.f8, sizeof(double), SQLT_FLT, xsink, pIndicator);
#endif
        return;
    }

    if (ntype == NT_HASH) {
        //printd(5, "hash structure in the Oracle IN attribute\n");
        const QoreHashNode* h = v.get<const QoreHashNode>();
        if (!h->existsKey("type") || !h->existsKey("^oratype^") || !h->existsKey("^values^")) {
            xsink->raiseException("ORACLE-BIND-VALUE-ERROR",
                "Plain hash/list cannot be bound as an Oracle Object/Collection; use "
                "bindOracleObject()/bindOracleCollection()");
            return;
        }

        QoreValue qv = h->getKeyValue("type");
        if (qv.getType() != NT_STRING) {
            xsink->raiseException("ORACLE-BIND-VALUE-ERROR", "the 'type' key in a bind hash must be assigned to a "
                "string type name; got type '%s' instead", qv.getTypeName());
            return;
        }

        QoreStringValueHelper t(qv);
        if (t->compare(ORACLE_OBJECT) == 0) {
            //printd(5, "binding hash as an oracle object\n");
            subdtype = SQLT_NTY_OBJECT;
            dtype = SQLT_NTY;
            buf.oraObj = objBindQore(conn, h, xsink);
            if (*xsink || !buf.oraObj)
                return;

            stmt.bindByPos(bndp, pos, 0, 0, SQLT_NTY, xsink, pIndicator);

            // TODO/FIXME: Indicator variables would allow to handle IN/OUT parameters too
            conn->checkerr(OCIBindObject(bndp, conn->errhp, buf.oraObj->typinf->tdo, (void**)&buf.oraObj->handle, 0,
                (void**)&buf.oraObj->tab_ind, 0), // NULL struct
                "OraBindNode::bindValue() OCIBindObject", xsink);
            return;
        } else if (t->compare(ORACLE_COLLECTION) == 0) {
            //printd(5, "binding list as an oracle collection\n");
            subdtype = SQLT_NTY_COLLECTION;
            dtype = SQLT_NTY;
            buf.oraColl = collBindQore(conn, h, xsink);
            if (*xsink || !buf.oraColl)
                return;

            stmt.bindByPos(bndp, pos, 0, 0, SQLT_NTY, xsink, pIndicator);

            // TODO/FIXME: examine "Indicator Variables for Named Data Types" for collections. It crashes with values
            // now.
            conn->checkerr(OCIBindObject(bndp, conn->errhp, buf.oraColl->typinf->tdo, (void**)&buf.oraColl->handle, 0,
                0/*(void**)&buf.oraObj->tab_ind*/, 0), // NULL struct
                "OraBindNode::bindValue() OCIBindObject", xsink);
            return;
        } else {
            xsink->raiseException("ORACLE-BIND-VALUE-ERROR",
                "Only Objects (hash) or collections (list) are allowed; use "
                "bindOracleObject()/bindOracleCollection()");
            return;
        }
    }

    xsink->raiseException("ORACLE-BIND-VALUE-ERROR", "type '%s' is not supported for SQL binding", v.getTypeName());
}

void OraBindNode::bindPlaceholder(int pos, ExceptionSink* xsink) {
    QoreOracleConnection* conn = stmt.getData();

    //printd(5, "OraBindNode::bindPlaceholder() this: %p, conn: %p, pos: %d type: %s, size: %d buf.ptr: %p\n", this, conn, pos, data.ph_type, data.ph_maxsize, buf.ptr);

    if (stmt.isArray()) {
        // do a dynamic bind for output values when binding arrays
        array = true;
        buf.arraybind = 0;

        if (data.isType("string"))
            buf.arraybind = new DynamicArrayBindString(0);
        else if (data.isType("integer"))
            buf.arraybind = new DynamicArrayBindString(0, NT_NUMBER, "number");
        else if (data.isType("number"))
            buf.arraybind = new DynamicArrayBindString(0, NT_NUMBER, "number");
        // not yet implemented
        /*
        else if (data.isType("date"))
            buf.arraybind = new DynamicArrayBindDate(0);
        else if (data.isType("binary"))
            buf.arraybind = new DynamicArrayBindBinary(0);
        else if (data.isType("float"))
            buf.arraybind = new DynamicArrayBindFloat(0);
        */
        else {
            xsink->raiseException("ORACLE-BIND-VALUE-ERROR", "type '%s' is not supported for SQL binding for array "
                "output buffers", data.ph_type);
            return;
        }

        assert(buf.arraybind);
        if (buf.arraybind->setupOutputBind(*this, pos, xsink))
            return;

        QoreOracleConnection* conn = stmt.getData();
        conn->checkerr(OCIBindDynamic(bndp, conn->errhp, (void*)buf.arraybind, ora_dynamic_bind_nodata_callback,
            (void*)buf.arraybind, ora_dynamic_bind_placeholder_callback), "OraBindNode::bindPlaceholder()", xsink);

        return;
    }

    if (data.isType("string")) {
        if (data.ph_maxsize < 0) {
            data.ph_maxsize = DBI_DEFAULT_STR_LEN;
            // HACK: trim shorter values, but just trim the values
            //       with a fixed 512 byte size because the size was unknown
            //       at bind time - if it's returned with the maximum size.
            //       By nature of pl/sql we can never return the right values
            //       for CHAR values anyway. Because we don't know the original
            //       size and currently have no way of finding it out.
            dtype = SQLT_AVC; // fake the "varchar2"
        } else {
            // simply malloc some space for sending to the new node
            dtype = SQLT_STR;
        }
        buf.ptr = malloc(sizeof(char) * (data.ph_maxsize + 1));

        if (value) {
            QoreStringValueHelper str(value, stmt.getEncoding(), xsink);
            if (*xsink)
                return;

            strncpy((char*)buf.ptr, str->c_str(), data.ph_maxsize);
            ((char*)buf.ptr)[data.ph_maxsize] = '\0';
        } else
            ((char* )buf.ptr)[0] = '\0';

        stmt.bindByPos(bndp, pos, buf.ptr, data.ph_maxsize + 1, SQLT_STR, xsink, &ind);
    } else if (data.isType("date")) {
        //printd(5, "OraBindNode::bindPlaceholder() this: %p, timestamp dtype: %d\n", this, QORE_SQLT_TIMESTAMP);
        if (setupDateDescriptor(xsink))
            return;

        if (value) {
            DateTimeNodeValueHelper d(value);

            if (conn->dateTimeConstruct(buf.odt, **d, xsink))
                return;
        }

        //conn->checkerr(OCIBindByPos(stmthp, &bndp, conn->errhp, pos, &buf.odt, 0, QORE_SQLT_TIMESTAMP, &ind,
        //  (ub2 *)NULL, (ub2 *)NULL, (ub4)0, (ub4 *)NULL, OCI_DEFAULT), "OraBindNode::bindPlaceholder() timestamp",
        //  xsink);
        if (bindDate(pos, xsink))
            return;
    } else if (data.isType("binary")) {
        dtype = SQLT_LVB;
        buf.ptr = 0;

        if (value) {
            if (value.getType() == NT_BINARY) {
                const BinaryNode* bin = value.get<const BinaryNode>();
                // if too big, raise an exception (not likely to happen)
                if (bin->size() > 0x7fffffff) {
                    xsink->raiseException("BIND-ERROR", "value passed for binding is " QLLD " bytes long, which is "
                        "too big to bind as a long binary value, maximum size is %d bytes", bin->size(), 0x7fffffff);
                    return;
                }
                size_t size = bin->size() + sizeof(int);
                if (ORA_RAW_SIZE > size)
                    size = ORA_RAW_SIZE;

                data.ph_maxsize = size;

                if (conn->checkerr(OCIRawAssignBytes(*conn->env, conn->errhp, (const ub1*)bin->getPtr(), bin->size(),
                    (OCIRaw**)&buf.ptr), "OraBindNode::bindPlaceholder() bind binary value", xsink)) {
                    return;
                }
            } else {
                QoreStringValueHelper str(value, stmt.getEncoding(), xsink);
                if (*xsink)
                    return;
                // if too big, raise an exception (not likely to happen)
                if (str->strlen() > 0x7fffffff) {
                    xsink->raiseException("BIND-ERROR", "value passed for binding is " QLLD " bytes long, which is "
                        "too big to bind as a long binary value, maximum size is %d bytes", str->strlen(),
                        0x7fffffff);
                    return;
                }
                size_t size = str->strlen() + sizeof(int);
                if (ORA_RAW_SIZE > size)
                    size = ORA_RAW_SIZE;

                data.ph_maxsize = size;

                if (conn->checkerr(OCIRawAssignBytes(*conn->env, conn->errhp, (const ub1*)str->c_str(), str->strlen(),
                    (OCIRaw**)&buf.ptr), "OraBindNode::bindPlaceholder() bind binary value from string", xsink)) {
                    return;
                }
            }
        } else {
            data.ph_maxsize = ORA_RAW_SIZE;
            buf.ptr = 0;

            if (conn->checkerr(OCIRawResize(*conn->env, conn->errhp, ORA_RAW_SIZE, (OCIRaw**)&buf.ptr),
                "OraBindNode::bindPlaceholder() setup binary placeholder", xsink)) {
                return;
            }
        }

        stmt.bindByPos(bndp, pos, buf.ptr, ORA_RAW_SIZE, SQLT_LVB, xsink, &ind);
    } else if (data.isType("clob")) {
        dtype = SQLT_CLOB;
        buf.ptr = NULL;

        if (conn->descriptorAlloc(&buf.ptr, OCI_DTYPE_LOB, "OraBindNode::bindPlaceholder() allocate clob descriptor",
            xsink)) {
            return;
        }

        // printd(5, "OraBindNode::bindPlaceholder() got LOB locator handle %p\n", buf.ptr);
        stmt.bindByPos(bndp, pos, &buf.ptr, 0, SQLT_CLOB, xsink, &ind);
    } else if (data.isType("blob")) {
        dtype = SQLT_BLOB;
        buf.ptr = NULL;

        conn->descriptorAlloc(&buf.ptr, OCI_DTYPE_LOB, "OraBindNode::bindPlaceholder() allocate blob descriptor",
            xsink);

        if (*xsink) {
            return;
        }
        // printd(5, "bindPalceholder() got LOB locator handle %p\n", buf.ptr);

        stmt.bindByPos(bndp, pos, &buf.ptr, 0, SQLT_BLOB, xsink, &ind);
    } else if (data.isType("integer")) {
        dtype = SQLT_INT;

        buf.i8 = value.getAsBigInt();

        stmt.bindByPos(bndp, pos, &buf.i8, sizeof(int64), SQLT_INT, xsink, &ind);
    } else if (data.isType("float")) {
        buf.f8 = value.getAsFloat();

#if defined(SQLT_BDOUBLE) && defined(USE_NEW_NUMERIC_TYPES)
        dtype = SQLT_BDOUBLE;
        stmt.bindByPos(bndp, pos, &buf.f8, sizeof(double), SQLT_BDOUBLE, xsink, &ind);
#else
        dtype = SQLT_FLT;
        stmt.bindByPos(bndp, pos, &buf.f8, sizeof(double), SQLT_FLT, xsink, &ind);
#endif
    } else if (data.isType("number")) {
        dtype = SQLT_NUM;
        buf.ptr = 0;

        size_t size;
        if (value) {
            QoreStringValueHelper str(value, stmt.getEncoding(), xsink);
            if (*xsink)
                return;

            size = str->strlen();
            if (size < 100)
                size = 100;
            buf.ptr = malloc(sizeof(char) * (size + 1));
            strcpy((char*)buf.ptr, str->c_str());
        } else {
            size = 100;
            buf.ptr = malloc(sizeof(char) * (size + 1));
            ((char*)buf.ptr)[0] = '\0';
        }

        // we actually bind as a string
        stmt.bindByPos(bndp, pos, buf.ptr, size, SQLT_STR, xsink, &ind);
    } else if (data.isType("hash")) {
        dtype = SQLT_RSET;

        // allocate statement handle for result list
        if (conn->handleAlloc(&buf.ptr, OCI_HTYPE_STMT, "OraBindNode::bindPlaceHolder() allocate statement handle",
            xsink)) {
            buf.ptr = nullptr;
        } else {
            stmt.bindByPos(bndp, pos, &buf.ptr, 0, SQLT_RSET, xsink, &ind);
        }
    } else if (data.isType("resultset")) {
        dtype = SQLT_RSET;

        // allocate statement handle for result list
        if (conn->handleAlloc(&buf.ptr, OCI_HTYPE_STMT, "OraBindNode::bindPlaceHolder() allocate statement handle",
            xsink)) {
            buf.ptr = nullptr;
        } else {
            stmt.bindByPos(bndp, pos, &buf.ptr, 0, SQLT_RSET, xsink, &ind);
            alt_output = true;
        }
    } else if (data.isType(ORACLE_OBJECT)) {
        subdtype = SQLT_NTY_OBJECT;
        dtype = SQLT_NTY;

#if 0
        const QoreHashNode*  h = reinterpret_cast<const QoreHashNode*>(value);
        if (h->existsKey("^values^"))
            buf.oraObj = objBindQore(conn, h, xsink); // IN/OUT
        else {
            const QoreStringNode* str = reinterpret_cast<const QoreStringNode*>(h->getKeyValue("value"));
            buf.oraObj = objPlaceholderQore(conn, str->c_str(), xsink); // IN
        }
#endif
        // It seems the value is string instead of hash for IN only NTY since the IN/OUT implementation
        // Qorus #802 Oracle NTY binding by placeholder ends with BIND-ERROR: type 'OracleCollection' is not
        // supported for SQL binding by value and placeholder (eg IN OUT)
        switch (value.getType()) {
            case NT_STRING: {
                QoreStringValueHelper str(value);
                buf.oraObj = objPlaceholderQore(conn, str->c_str(), xsink); // IN
                break;
            }
            case NT_HASH: {
                const QoreHashNode* h = value.get<QoreHashNode>();
                if (h->existsKey("^values^"))
                    buf.oraObj = objBindQore(conn, h, xsink); // IN/OUT
                else
                    xsink->raiseException("BIND-ERROR", "Unable to bind hash as Object without key '^values^'");
                break;
            }
            default:
                xsink->raiseException("BIND-ERROR", "Unable to bind Object from '%s' type", value.getTypeName());
        }

        if (*xsink)
            return;

        stmt.bindByPos(bndp, pos, 0, 0, SQLT_NTY, xsink, &ind);

        // TODO/FIXME: examine "Indicator Variables for Named Data Types" for collections OUT. Using it overrides all
        // values.
        conn->checkerr(OCIBindObject(bndp, conn->errhp, buf.oraObj->typinf->tdo, (void**)&buf.oraObj->handle, 0,
            0/*&buf.oraObj->handle*/, 0), // NULL struct
            "OraBindNode::bindPlaceholder() OCIBindObject", xsink);
        //printd(5, "OraBindNode::bindValue() object '%s' tdo=0x%x handle=0x%x\n", h->c_str(),
        //  buf.oraObj->typinf->tdo, buf.oraObj->handle);
    } else if (data.isType(ORACLE_COLLECTION)) {
        subdtype = SQLT_NTY_COLLECTION;
        dtype = SQLT_NTY;

        // It seems the value is string instead of hash for IN only NTY since the IN/OUT implementation
        // Qorus #802 Oracle NTY binding by placeholder ends with BIND-ERROR: type 'OracleCollection' is not
        // supported for SQL binding by value and placeholder (eg IN OUT)
        switch (value.getType()) {
            case NT_STRING: {
                QoreStringValueHelper str(value);
                buf.oraColl = collPlaceholderQore(conn, str->c_str(), xsink); // IN
                break;
            }
            case NT_HASH: {
                const QoreHashNode* h = value.get<QoreHashNode>();
                if (h->existsKey("^values^"))
                    buf.oraColl = collBindQore(conn, h, xsink); // IN/OUT
                else
                    xsink->raiseException("BIND-ERROR", "Unable to bind hash as Collection without key "
                        "'^values^'");
                break;
            }
            default:
                xsink->raiseException("BIND-ERROR", "Unable to bind Collection from '%s' type",
                    value.getTypeName());
        }

        if (*xsink)
            return;

        stmt.bindByPos(bndp, pos,  0, 0, SQLT_NTY, xsink, &ind);

        // TODO/FIXME: examine "Indicator Variables for Named Data Types" for collections OUT. Using it overrides all
        // values.
        conn->checkerr(OCIBindObject(bndp, conn->errhp, buf.oraColl->typinf->tdo, (void**)&buf.oraColl->handle, 0,
            0/*(void**)&buf.oraObj->tab_ind*/, 0), // NULL struct
            "OraBindNode::bindPlaceholder() OCIBindObject collection", xsink);
        //assert(false);
    } else {
        //printd(5, "OraBindNode::bindPlaceholder(ds: %p, pos: %d) type: %s, size: %d)\n", ds, pos, data.ph.type,
        //  data.ph.maxsize);
        xsink->raiseException("BIND-ERROR", "type '%s' is not supported for SQL binding", data.ph_type);
    }
}

void OraBindNode::resetValue(ExceptionSink* xsink) {
    if (!dtype) {
        assert(data.tmp_type == OBT_NONE);
        return;
    }

    data.resetBind();

    if (array) {
        delete buf.arraybind;
        array = false;
        return;
    }

    if (strlob) {
        if (lob_allocated) {
            QoreOracleConnection* conn = stmt.getData();
            //printd(5, "deallocating temporary clob\n");
            conn->checkerr(OCILobFreeTemporary(conn->svchp, conn->errhp, strlob), "OraBindNode::resetValue() free "
                "temporary LOB", xsink);
            lob_allocated = false;
        }
        //printd(5, "freeing lob descriptor\n");
        OCIDescriptorFree(strlob, OCI_DTYPE_LOB);
        strlob = nullptr;
    } else if (dtype == SQLT_NTY) {
        freeObject(xsink);
    } else if (dtype == QORE_SQLT_TIMESTAMP) {
        if (buf.odt) {
            //printd(5, "OraBindNode::resetValue() freeing timestamp descriptor type %d ptr %p\n",
            //  QORE_DTYPE_TIMESTAMP, buf.odt);
            OCIDescriptorFree(buf.odt, QORE_DTYPE_TIMESTAMP);
            buf.odt = nullptr;
        }
    }

    dtype = 0;
}

QoreValue OraBindNode::getValue(bool horizontal, ExceptionSink* xsink) {
    //printd(5, "AbstractQoreNode* OraBindNode::getValue() %d\n", indicator);
    if (array) {
        return buf.arraybind->getOutputValue(xsink, *this, true);
    }

    if (indicator != 0) {
        return null();
    }

    return OraColumnValue::getValue(xsink, horizontal, true);
}

int QorePreparedStatement::execute(ExceptionSink* xsink, const char* who, int oci_flags) {
    assert(conn.svchp);

    // Check for interrupt before query execution
    if (qore_check_cancel(xsink)) {
        return -1;
    }

    ub4 iters;
    if (is_select) {
        iters = 0;
    } else {
        iters = !array_size ? 1 : array_size;
    }

    int status;
    {
        // Register cancel callback for interruptible execution
        QoreOracleCancelHelper cancel_helper(conn.svchp, conn.errhp);
        status = OCIStmtExecute(conn.svchp, stmthp, conn.errhp, iters, 0, 0, 0, OCI_DEFAULT | oci_flags);
    }

    //printd(5, "QoreOracleStatement::execute() stmthp: %p status: %d (OCI_ERROR: %d)\n", stmthp, status, OCI_ERROR);
    if (status == OCI_ERROR) {
        if (!conn.handleError(xsink, who, true)) {
            assert(*xsink);
            return -1;
        }

        assert(!*xsink);

        // Check for interrupt before re-executing query after reconnection
        if (qore_check_cancel(xsink)) {
            return -1;
        }

        //printd(5, "QoreOracleStatement::execute() returned from OCILogon() status: %d\n", status);
        {
            // Register cancel callback for interruptible execution (retry after reconnect)
            QoreOracleCancelHelper cancel_helper(conn.svchp, conn.errhp);
            status = OCIStmtExecute(conn.svchp, stmthp, conn.errhp, iters, 0, 0, 0, OCI_DEFAULT | oci_flags);
        }
        if (status && conn.checkerr(status, who, xsink)) {
            return -1;
        }
    } else if (status && conn.checkerr(status, who, xsink))
        return -1;

    return 0;
}

int QorePreparedStatement::exec(ExceptionSink* xsink) {
    return execute(xsink, "QorePreparedStatement::exec()");
}

int QorePreparedStatement::execDescribe(ExceptionSink* xsink) {
    //printd(5, "QorePreparedStatement::execDescribe() this: %p\n", this);
    return execute(xsink, "QorePreparedStatement::execDescribe()", OCI_DESCRIBE_ONLY);
}

void QorePreparedStatement::clear(ExceptionSink* xsink) {
    //printd(5, "QorePreparedStatement::clear() this: %p\n", this);

    QoreOracleStatement::reset(xsink);

    // clear all nodes without deleting the values
    for (node_list_t::iterator i = node_list.begin(), e = node_list.end(); i != e; ++i) {
        (*i)->clear(xsink);
    }

    if (columns) {
        columns->del(xsink);
        delete columns;
        columns = nullptr;
        array_size = 0;
    }

    defined = false;
}

void QorePreparedStatement::reset(ExceptionSink* xsink) {
    QoreOracleStatement::reset(xsink);

    for (node_list_t::iterator i = node_list.begin(), e = node_list.end(); i != e; ++i) {
        (*i)->reset(xsink);
        delete *i;
    }

    node_list.clear();

    if (columns) {
        columns->del(xsink);
        delete columns;
        columns = nullptr;
        array_size = 0;
    }

    defined = false;
    hasOutput = false;

    if (str) {
        delete str;
        str = nullptr;
    }

    if (args_copy) {
        args_copy->deref(xsink);
        args_copy = nullptr;
    }

   conn.deregisterStatement(this);
}

int QorePreparedStatement::define(ExceptionSink* xsink) {
    if (defined) {
        xsink->raiseException("DBI:ORACLE-DEFINE-ERROR", "SQLStatement::define() called twice for the same query");
        return -1;
    }

    if (!columns) {
        columns = new OraResultSet(*this, "QorePreparedStatement::define()", xsink);
        if (*xsink)
            return -1;
    }

    defined = true;
    conn.registerStatement(this);
    columns->define("QorePreparedStatement::define()", xsink);
    return *xsink ? -1 : 0;
}

int QorePreparedStatement::prepare(const QoreString& sql, const QoreListNode* args, bool parse,
        ExceptionSink* xsink) {
    assert(!str);
    // create copy of string and convert encoding if necessary
    str = sql.convertEncoding(getEncoding(), xsink);
    if (*xsink)
        return -1;

    //printd(4, "QorePreparedStatement::prepare() ds: %p, conn: %p, SQL='%s', args: %d\n", ds, conn, str->c_str(),
    //  args ? args->size() : 0);

    // process query string and setup bind value list
    if (parse) {
        parseQuery(args, xsink);
        if (*xsink)
            return -1;
    }

    if (allocate(xsink))
        return -1;

    if (QoreOracleStatement::prepare(*str, xsink))
        return -1;

    if (!node_list.empty() && bindOracle(xsink))
        return -1;

    return 0;
}

int QorePreparedStatement::bindOracle(ExceptionSink* xsink) {
    // reset array_size before new bind. setArraySize uses the 1st value posted as main size
    if (array_size)
        array_size = 0;
    for (unsigned i = 0, end = node_list.size(); i < end; ++i) {
        OraBindNode* w = node_list[i];
        if (!w->isValue())
            continue;

        // scan for array binds
        if (w->value.getType() == NT_LIST) {
            // issue #2154: do not allow array binds with select statements
            if (is_select) {
                xsink->raiseException("BIND-ERROR", "cannot perform an array bind with a select statement");
                return -1;
            }
            const QoreListNode* l = w->value.get<const QoreListNode>();
            if (setArraySize(i + 1, l->size(), xsink)) {
                return -1;
            }
        }
    }

    int pos = 1;
    for (node_list_t::iterator i = node_list.begin(), e = node_list.end(); i != e; ++i) {
        (*i)->bind(pos, xsink);

        if (*xsink) {
            return -1;
        }

        ++pos;
    }
    //printd(5, "QorePreparedStatement::bindOracle() bound %d position(s), statement handle: %p\n", pos - 1, stmthp);

    return 0;
}

int QorePreparedStatement::bind(const QoreListNode* args, ExceptionSink* xsink) {
    int pos = 1;
    for (unsigned i = 0, end = node_list.size(); i < end; ++i) {
        OraBindNode* w = node_list[i];

        // get bind argument
        QoreValue v = args ? args->retrieveEntry(i) : QoreValue();

        if (w->set(v, xsink))
            return -1;
        ++pos;
    }

    if (bindOracle(xsink))
        return -1;

    return 0;
}

int QorePreparedStatement::bindPlaceholders(const QoreListNode* args, ExceptionSink* xsink) {
    unsigned arg_offset = 0;
    for (unsigned i = 0, end = node_list.size(); i < end; ++i) {
        OraBindNode* w = node_list[i];
        if (w->isValue())
            continue;

        // get bind argument
        QoreValue v = args ? args->retrieveEntry(arg_offset++) : QoreValue();

        if (w->set(v, xsink))
            return -1;
    }

    if (bindOracle(xsink))
        return -1;

    return 0;
}

int QorePreparedStatement::bindValues(const QoreListNode* args, ExceptionSink* xsink) {
    unsigned arg_offset = 0;
    for (unsigned i = 0, end = node_list.size(); i < end; ++i) {
        OraBindNode* w = node_list[i];
        if (!w->isValue())
            continue;

        // get bind argument
        QoreValue v = args ? args->retrieveEntry(arg_offset++) : QoreValue();

        if (w->set(v, xsink))
            return -1;
    }

    if (bindOracle(xsink))
        return -1;

    return 0;
}

#define QODC_LINE 1
#define QODC_BLOCK 2

void QorePreparedStatement::parseQuery(const QoreListNode* args, ExceptionSink* xsink) {
    //printd(5, "parseQuery() args: %p str: %s\n", args, str->c_str());

    char quote = 0;

    const char* p = str->c_str();
    unsigned index = 0;
    QoreString tmp(getEncoding());

    int comment = 0;

    while (*p) {
        if (!quote) {
            if (!comment) {
                if ((*p) == '-' && (*(p+1)) == '-') {
                    comment = QODC_LINE;
                    p += 2;
                    continue;
                }

                if ((*p) == '/' && (*(p+1)) == '*') {
                    comment = QODC_BLOCK;
                    p += 2;
                    continue;
                }
            } else {
                if (comment == QODC_LINE) {
                    if ((*p) == '\n' || ((*p) == '\r'))
                        comment = 0;
                    ++p;
                    continue;
                }

                assert(comment == QODC_BLOCK);
                if ((*p) == '*' && (*(p+1)) == '/') {
                    comment = 0;
                    p += 2;
                    continue;
                }

                ++p;
                continue;
            }

            if ((*p) == '%' && (p == str->c_str() || !isalnum(*(p-1)))) { // found value marker
                QoreValue v = args ? args->retrieveEntry(index++) : QoreValue();

                int offset = p - str->c_str();

                ++p;
                if ((*p) == 'd') {
                    DBI_concat_numeric(&tmp, v);
                    str->replace(offset, 2, tmp.c_str());
                    p = str->c_str() + offset + tmp.strlen();
                    tmp.clear();
                    continue;
                }
                if ((*p) == 's') {
                    if (DBI_concat_string(&tmp, v, xsink))
                        break;
                    str->replace(offset, 2, tmp.c_str());
                    p = str->c_str() + offset + tmp.strlen();
                    tmp.clear();
                    continue;
                }
                if ((*p) != 'v') {
                    xsink->raiseException("DBI-EXEC-PARSE-EXCEPTION", "invalid value specification (expecting '%v' "
                        "or '%%d', got %%%c)", *p);
                    break;
                }
                ++p;
                if (isalpha(*p)) {
                    xsink->raiseException("DBI-EXEC-PARSE-EXCEPTION", "invalid value specification (expecting '%v' "
                        "or '%%d', got %%v%c*)", *p);
                    break;
                }

                // replace value marker with generated name
                tmp.sprintf(":qdodvrs___%d", node_list.size());
                str->replace(offset, 2, tmp.c_str());
                p = str->c_str() + offset + tmp.strlen();
                tmp.clear();

                //   printd(5, "QorePreparedStatement::parseQuery() newstr: %s\n", str->c_str());
                //   printd(5, "QorePreparedStatement::parseQuery() adding value type: %s\n", v.getTypeName());
                add(v);
                continue;
            }

            if ((*p) == ':') { // found placeholder marker
                ++p;
                if (!isalpha(*p))
                    continue;

                // get placeholder name
                QoreString tstr;
                while (isalnum(*p) || (*p) == '_')
                    tstr.concat(*(p++));

                // add default placeholder
                OraBindNode* n = add(tstr.giveBuffer(), -1);

                QoreValue v = args ? args->retrieveEntry(index++) : QoreValue();

                if (n->set(v, xsink))
                    break;
                continue;
            }

            // allow quoting of ':' and '%' characters
            if ((*p) == '\\' && (*(p+1) == ':' || *(p+1) == '%')) {
                str->splice(p - str->c_str(), 1, xsink);
                p += 2;
            }
        }

        if (((*p) == '\'') || ((*p) == '\"')) {
            if (!quote)
                quote = *p;
            else if (quote == (*p))
                quote = 0;
            ++p;
            continue;
        }

        ++p;
    }

    // save args if appropriate
    if (args && !ds->isInTransaction() && !*xsink) {
        if (args_copy)
            args_copy->deref(xsink);
        args_copy = args->listRefSelf();
    }
}

QoreHashNode* QorePreparedStatement::getOutputHash(bool rows, ExceptionSink* xsink) {
    ReferenceHolder<QoreHashNode> h(new QoreHashNode(autoTypeInfo), xsink);

    for (node_list_t::iterator i = node_list.begin(), e = node_list.end(); i != e; ++i) {
        //printd(5, "QorePreparedStatement::getOutputHash() this: %p i: %p '%s' (%s) dtype: %d\n", this, *i,
        //  (*i)->data.getName(), (*i)->data.ph_type, (*i)->dtype);
        if ((*i)->isPlaceholder())
            h->setKeyValue((*i)->data.getName(), (*i)->getValue(rows, xsink), xsink);
    }

    return *xsink ? nullptr : h.release();
}

QoreHashNode* QorePreparedStatement::selectRow(ExceptionSink* xsink) {
    if (!is_select) {
        xsink->raiseException("DBI-SELECT-ROW-ERROR", "the SQL passed to the selectRow() method is not a "
            "select statement");
        return nullptr;
    }

    if (exec(xsink))
        return nullptr;

    return fetchSingleRow(xsink);
}

QoreValue QorePreparedStatement::execWithPrologue(ExceptionSink* xsink, bool rows, bool cols) {
    if (exec(xsink)) {
        return QoreValue();
    }

    ValueHolder rv(xsink);

    // if there are output variables, then fix values if necessary and return
    if (is_select) {
        if (rows) {
            rv = QoreOracleStatement::fetchRows(xsink);
        }
        else {
            rv = QoreOracleStatement::fetchColumns(cols, xsink);
        }

        if (*xsink) {
            return QoreValue();
        }
    } else if (hasOutput) {
        rv = getOutputHash(rows, xsink);
    } else {
        // get row count
        int rc = affectedRows(xsink);
        rv = *xsink ? QoreValue() : QoreValue(rc);
    }

    // commit transaction if autocommit set for datasource
    if (ds->getAutoCommit()) {
        getData()->commit(xsink);
    }

    return *xsink ? QoreValue() : rv.release();
}

#ifdef QDBI_METHOD_SELECT_TYPED
QoreValue QorePreparedStatement::execWithPrologueTyped(ExceptionSink* xsink, bool rows) {
    if (exec(xsink)) {
        return QoreValue();
    }

    ValueHolder rv(xsink);

    if (is_select) {
        OraResultSetHelper resultset(*this, "QorePreparedStatement::execWithPrologueTyped():params", xsink);
        if (*xsink) {
            return QoreValue();
        }

        ReferenceHolder<QoreHashNode> desc(QoreOracleStatement::describe(**resultset, xsink), xsink);
        if (*xsink) {
            return QoreValue();
        }

        if (rows) {
            ReferenceHolder<QoreListNode> data(QoreOracleStatement::fetchRows(**resultset, -1, xsink), xsink);
            if (*xsink) {
                return QoreValue();
            }

            QoreListNode* typed = qore_dbi_make_typed_select_rows_result(ds, *data, *desc, xsink);
            rv = typed ? QoreValue(typed) : QoreValue();
        } else {
            ReferenceHolder<QoreHashNode> data(QoreOracleStatement::fetchColumns(**resultset, -1, true, xsink),
                xsink);
            if (*xsink) {
                return QoreValue();
            }

            QoreHashNode* typed = qore_dbi_make_typed_select_result(ds, *data, *desc, xsink);
            rv = typed ? QoreValue(typed) : QoreValue();
        }

        if (*xsink) {
            return QoreValue();
        }
    } else if (hasOutput) {
        rv = getOutputHash(rows, xsink);
    } else {
        int rc = affectedRows(xsink);
        rv = *xsink ? QoreValue() : QoreValue(rc);
    }

    if (ds->getAutoCommit()) {
        getData()->commit(xsink);
    }

    return *xsink ? QoreValue() : rv.release();
}
#endif

#ifdef QDBI_METHOD_SELECT_COLUMNAR
QoreColumnarResult* QorePreparedStatement::execWithPrologueColumnar(ExceptionSink* xsink) {
    if (!is_select) {
        xsink->raiseException("COLUMNAR-RESULT-ERROR",
            "Datasource::selectColumnar() requires an SQL statement returning result columns");
        return nullptr;
    }

    if (exec(xsink)) {
        return nullptr;
    }

    OraResultSetHelper resultset(*this, "QorePreparedStatement::execWithPrologueColumnar():params", xsink);
    if (*xsink) {
        return nullptr;
    }

    ReferenceHolder<QoreHashNode> desc(QoreOracleStatement::describe(**resultset, xsink), xsink);
    if (*xsink) {
        return nullptr;
    }

    ReferenceHolder<QoreHashNode> data(QoreOracleStatement::fetchColumns(**resultset, -1, true, xsink), xsink);
    if (*xsink) {
        return nullptr;
    }

    ReferenceHolder<QoreColumnarResult> rv(QoreColumnarResult::fromColumnHash(*data, *desc, xsink), xsink);
    if (*xsink) {
        return nullptr;
    }

    if (ds->getAutoCommit()) {
        getData()->commit(xsink);
    }

    return *xsink ? nullptr : rv.release();
}
#endif

int QorePreparedStatement::affectedRows(ExceptionSink* xsink) {
    int rc = 0;
    getData()->checkerr(OCIAttrGet(stmthp, OCI_HTYPE_STMT, &rc, 0, OCI_ATTR_ROW_COUNT, getData()->errhp),
        "QorePreparedStatement::affectedRows()", xsink);
    return rc;
}

QoreHashNode* QorePreparedStatement::fetchRow(ExceptionSink* xsink) {
    assert(columns);
    return QoreOracleStatement::fetchRow(*columns, xsink);
}

QoreListNode* QorePreparedStatement::fetchRows(int rows, ExceptionSink* xsink) {
    assert(columns);
    return QoreOracleStatement::fetchRows(*columns, rows, xsink);
}

QoreHashNode* QorePreparedStatement::fetchColumns(int rows, ExceptionSink* xsink) {
    assert(columns);
    return QoreOracleStatement::fetchColumns(*columns, rows, false, xsink);
}

QoreHashNode* QorePreparedStatement::describe(ExceptionSink* xsink) {
    assert(columns);
    return QoreOracleStatement::describe(*columns, xsink);
}
