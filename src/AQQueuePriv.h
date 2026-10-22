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

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>

#include <unistd.h>

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

   //! Returns the sandbox of the code that started the current subscription, or nullptr if none was started
   /** The subscription's callbacks are called on threads that Qore did not start (the OCI notification thread
       and the subscription watchdog thread) and must run under this sandbox.
   */
   DLLLOCAL std::shared_ptr<const QoreSandboxCapture> getSandbox() const;

   //! Called by the subscription watchdog thread: restores a subscription whose connection was lost
   /** Runs under the sandbox of the code that started the subscription; if the subscription cannot be restored,
       the object is removed from the watchdog and the loss is reported with notifySubscriptionLost() on the notifier
       thread.
   */
   DLLLOCAL void checkSubscription();

   //! Calls AQQueue::onSubscriptionLost() for the subscription that checkSubscription() could not restore
   /** Runs under the sandbox of the code that started the subscription; no lock is held when onSubscriptionLost() is
       called, so it can restart the subscription.
   */
   DLLLOCAL void notifySubscriptionLost();

   //! Called by the Oracle client's notification thread for a notification of the subscription
   /** Queues the notification for the module's dispatch thread, which calls deliverAsyncMessage(): the notification
       thread holds a lock of the Oracle client while it runs, which the client also takes when a subscription is
       started or stopped, so it calls no %Qore code and takes no lock of the queue.
   */
   DLLLOCAL void postAsyncMessage();

   //! Calls AQQueue::onAsyncMessage() on the given object for a notification of the given subscription
   /** Called on the module's dispatch thread with no lock held; runs under the sandbox of the code that started the
       subscription.  Nothing is called if the subscription was stopped after the notification was queued.
   */
   DLLLOCAL void deliverAsyncMessage(QoreObject* obj, uint64_t subscription);

#ifdef DEBUG
   //! Calls the code while the calling thread holds the queue's lock; DEBUG builds only
   /** Lets the tests hold the queue's lock while a notification of its subscription is delivered.
   */
   DLLLOCAL QoreValue dbgWithLock(const ResolvedCallReferenceNode* code, ExceptionSink* xsink);
#endif

   //! Returns true in a child process created with fork() after the object was created
   /** The queue's session and subscription belong to the parent: the child shares the session's network connection
       with the parent, and the threads that serve the subscription (the Oracle client's notification thread and the
       module's watchdog thread, which can hold the object's lock) exist only in the parent.
   */
   DLLLOCAL bool isForkedChild() const {
      return getpid() != m_pid;
   }

private:
   //! the process that created the object; see isForkedChild()
   const pid_t m_pid = getpid();
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

   //! the object; written under m_sandbox_lock too, as the Oracle client's notification thread reads it
   QoreObject *m_self = nullptr;
   //! the object of a subscription that could not be restored, weakly referenced until the loss is reported
   std::atomic<QoreObject*> m_lost{nullptr};

   //! the sandbox of the code that started the current subscription; see getSandbox()
   std::shared_ptr<const QoreSandboxCapture> m_sandbox;
   //! the identifier of the active subscription, 0 if none; see deliverAsyncMessage()
   /** Kept when the watchdog restores the subscription, as the subscription continues; a new identifier is assigned
       by every call of startSubscription()
   */
   uint64_t m_subscription = 0;
   //! protects m_sandbox, m_subscription and the reads of m_self that are made without the object lock, so that the
   //! callback threads can read them while another thread holds the object lock
   mutable std::mutex m_sandbox_lock;

   DLLLOCAL bool pingUnlocked();
   DLLLOCAL void destructorUnlocked(ExceptionSink *xsink);
   //! Raises AQQUEUE-FORK-ERROR in a child process created with fork() after the object was created
   DLLLOCAL int checkProcess(const char* m, ExceptionSink* xsink) const;
   //! Abandons the queue's session and subscription in a child process created with fork(); see isForkedChild()
   DLLLOCAL void abandon();
   DLLLOCAL int checkValidUnlocked(const char* m, ExceptionSink* xsink);
   DLLLOCAL int reconnectUnlocked(ExceptionSink* xsink);
   DLLLOCAL int reconnectSubscriptionUnlocked(ExceptionSink* xsink);
   //! Stops the subscription; releases the subscription's callback thread ID if release_callback_thread is true
   /** If release_callback_thread is true, the subscription ends (see endSubscriptionUnlocked()); otherwise it is
       restored by the caller.
   */
   DLLLOCAL int stopSubscriptionUnlocked(ExceptionSink *xsink, bool release_callback_thread);
   //! Ends the subscription after it was stopped or lost: its notifications are no longer delivered, and the
   //! subscription's callback thread ID is released
   DLLLOCAL void endSubscriptionUnlocked();
   DLLLOCAL int connectUnlocked(ExceptionSink *xsink);
   DLLLOCAL void disconnectUnlocked(ExceptionSink *xsink);
   DLLLOCAL bool checkDequeueUnlocked(QoreObject* self, ExceptionSink *xsink);
};

#endif

