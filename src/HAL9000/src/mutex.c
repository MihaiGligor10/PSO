#include "HAL9000.h"
#include "thread_internal.h"
#include "mutex.h"
#include "thread.h"

#define MUTEX_MAX_RECURSIVITY_DEPTH         MAX_BYTE

_No_competing_thread_
void
MutexInit(
    OUT         PMUTEX      Mutex,
    IN          BOOLEAN     Recursive
)
{
    ASSERT(NULL != Mutex);

    memzero(Mutex, sizeof(MUTEX));

    LockInit(&Mutex->MutexLock);

    InitializeListHead(&Mutex->WaitingList);

    Mutex->MaxRecursivityDepth = Recursive ? MUTEX_MAX_RECURSIVITY_DEPTH : 1;
}

ACQUIRES_EXCL_AND_REENTRANT_LOCK(*Mutex)
REQUIRES_NOT_HELD_LOCK(*Mutex)
void
MutexAcquire(
    INOUT       PMUTEX      Mutex
)
{
    INTR_STATE dummyState;
    INTR_STATE oldState;
    PTHREAD pCurrentThread = GetCurrentThread();
  

    ASSERT(NULL != Mutex);
    ASSERT(NULL != pCurrentThread);

    if (pCurrentThread == Mutex->Holder)
    {
        ASSERT(Mutex->CurrentRecursivityDepth < Mutex->MaxRecursivityDepth);

        Mutex->CurrentRecursivityDepth++;
        return;
    }

    oldState = CpuIntrDisable();

    LockAcquire(&Mutex->MutexLock, &dummyState);
    if (NULL == Mutex->Holder)
    {
        Mutex->Holder = pCurrentThread;
        Mutex->CurrentRecursivityDepth = 1;
    }

    while (Mutex->Holder != pCurrentThread)
    {
        InsertOrderedList(&Mutex->WaitingList, &pCurrentThread->ReadyList, ThreadComparePriorityReadyList, NULL);
        ThreadTakeBlockLock();
        LockRelease(&Mutex->MutexLock, dummyState);
        /// /////////////////////////////////////////////
        pCurrentThread->WaitedMutex = Mutex;
        if (ThreadGetPriority(pCurrentThread) > ThreadGetPriority(Mutex->Holder))
        {
            ThreadDonatePriority(pCurrentThread);
        }
        /// /////////////////////////////////////////////
        ThreadBlock();
        LockAcquire(&Mutex->MutexLock, &dummyState);
    }

    _Analysis_assume_lock_acquired_(*Mutex);
    //InsertTailList(&pCurrentThread->AcquiredMutexesList, &Mutex->AcquiredMutexListElem);
    LockRelease(&Mutex->MutexLock, dummyState);

    CpuIntrSetState(oldState);
}

RELEASES_EXCL_AND_REENTRANT_LOCK(*Mutex)
REQUIRES_EXCL_LOCK(*Mutex)
void
MutexRelease(
    INOUT       PMUTEX      Mutex
)
{
    INTR_STATE oldState;
    PLIST_ENTRY pEntry;

    ASSERT(NULL != Mutex);
    ASSERT(GetCurrentThread() == Mutex->Holder);

    if (Mutex->CurrentRecursivityDepth > 1)
    {
        Mutex->CurrentRecursivityDepth--;
        return;
    }

    pEntry = NULL;

    LockAcquire(&Mutex->MutexLock, &oldState);
    /*
    THREAD_PRIORITY maxim = Mutex->Holder->RealPriority;

    for (PLIST_ENTRY e = Mutex->Holder->AcquiredMutexesList.Flink; e != &Mutex->Holder->AcquiredMutexesList; e = e->Flink)
    {
        PMUTEX m = CONTAINING_RECORD(e, MUTEX, WaitingList);

        for (PLIST_ENTRY e2 = m->WaitingList.Flink; e2 != &m->WaitingList; e2 = e2->Flink)
        {
            PTHREAD t = CONTAINING_RECORD(e, THREAD, Priority);
            if (maxim < t->Priority)
            {
                maxim = t->Priority;
            }
        }
    }

    Mutex->Holder->Priority = maxim;
*/

   // ThreadRecomputePriority(Mutex);

    pEntry = RemoveHeadList(&Mutex->WaitingList);
    if (pEntry != &Mutex->WaitingList)
    {
        PTHREAD pThread = CONTAINING_RECORD(pEntry, THREAD, ReadyList);

        // wakeup first thread
        Mutex->Holder = pThread;
        Mutex->CurrentRecursivityDepth = 1;
        ThreadUnblock(pThread);
    }
    else
    {
        Mutex->Holder = NULL;
    }

    _Analysis_assume_lock_released_(*Mutex);

    LockRelease(&Mutex->MutexLock, oldState);
}
