/* -*- mode: c++; indent-tabs-mode: nil -*- */
/*
  AQQueuePriv.h

  Qore Programming Language

  Copyright (C) 2003 - 2026 Qore Technologies, sro

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

#ifndef AQQUEUEPRIV_H
#define AQQUEUEPRIV_H

#include <qore/Qore.h>
#include "oracle.h"
#include "ocilib_internal.h"

class AQMessagePriv;

DLLLOCAL extern qore_classid_t CID_AQQUEUE;
DLLLOCAL extern QoreClass* QC_AQQUEUE;

class AQQueuePriv : public AbstractPrivateData {
protected:
   DLLLOCAL virtual ~AQQueuePriv();

public:
   DLLLOCAL AQQueuePriv(Datasource *ds, const QoreStringNode* tname, const QoreStringNode* qname, ExceptionSink *xsink);
   DLLLOCAL void destructor(ExceptionSink* xsink);

   DLLLOCAL void connect(ExceptionSink *xsink);
   DLLLOCAL void disconnect(ExceptionSink *xsink);

   DLLLOCAL OCI_TypeInfo* typeInfo() { return m_typeInfo; }
   DLLLOCAL QoreOracleConnection* connection() { return m_conn; }
   DLLLOCAL DateTimeNode *getDate(OCIDate* handle) { return m_conn->getDate(handle); }

   DLLLOCAL bool commit(ExceptionSink *xsink);
   DLLLOCAL bool rollback(ExceptionSink *xsink);

   DLLLOCAL bool post(AQMessagePriv *message, ExceptionSink *xsink);
   DLLLOCAL bool post(const QoreHashNode *h, ExceptionSink *xsink);

   DLLLOCAL QoreObject* getMessage(QoreObject* self, int timeout, ExceptionSink *xsink);

   DLLLOCAL void startSubscription(QoreObject *qo, unsigned port, int64 timeout, ExceptionSink *xsink);

   // returns -1 if an exception was raised, 0 if not: note: no error is raised if there is no active subscription
   DLLLOCAL int stopSubscription(ExceptionSink *xsink);

   DLLLOCAL QoreObject* qoreObject() { return m_self; }

   //! Called by the subscription watchdog thread: restores a subscription whose connection was lost
   /** If the subscription cannot be restored, the object is removed from the watchdog and
       AQQueue::onSubscriptionLost() is called.  No lock is held when onSubscriptionLost() is called, so it can
       restart the subscription.
   */
   DLLLOCAL void checkSubscription();

private:
   // mutex for atomicity
   QoreThreadLock l;
   QoreOracleConnection *m_conn;
   Datasource *m_ds;

   QoreStringNode* m_tname;
   QoreStringNode* m_qname;
   OCI_TypeInfo *m_typeInfo;
   OCI_Enqueue *m_enqueue;
   OCI_Dequeue *m_dequeue;

   // port for automatically reconnecting subscriptions
   int m_port;
   // timeout for automatically reconnecting subscriptions
   int64 m_timeout;

   bool m_hasSubscription, valid;

   QoreObject *m_self = nullptr;

   DLLLOCAL bool pingUnlocked();
   DLLLOCAL void destructorUnlocked(ExceptionSink *xsink);
   DLLLOCAL int checkValidUnlocked(const char* m, ExceptionSink* xsink);
   DLLLOCAL int reconnectUnlocked(ExceptionSink* xsink);
   DLLLOCAL int reconnectSubscriptionUnlocked(ExceptionSink* xsink);
   //! Stops the subscription; releases the subscription's callback thread ID if release_callback_thread is true
   DLLLOCAL int stopSubscriptionUnlocked(ExceptionSink *xsink, bool release_callback_thread);
   DLLLOCAL int connectUnlocked(ExceptionSink *xsink);
   DLLLOCAL void disconnectUnlocked(ExceptionSink *xsink);
   DLLLOCAL bool checkDequeueUnlocked(QoreObject* self, ExceptionSink *xsink);
};

#endif

