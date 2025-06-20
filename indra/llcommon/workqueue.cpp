/**
 * @file   workqueue.cpp
 * @author Nat Goodspeed
 * @date   2021-10-06
 * @brief  Implementation for WorkQueue.
 *
 * $LicenseInfo:firstyear=2021&license=viewerlgpl$
 * Copyright (c) 2021, Linden Research, Inc.
 * $/LicenseInfo$
 */

// Precompiled header
#include "linden_common.h"
// associated header
#include "workqueue.h"
// STL headers
// std headers
// external library headers
// other Linden headers
#include "llapp.h"
#include "llcoros.h"
#include LLCOROS_MUTEX_HEADER
#include "llerror.h"
#include "llexception.h"
#include "stringize.h"

using Mutex = LLCoros::Mutex;
using Lock  = LLCoros::LockType;

/*****************************************************************************
*   WorkQueueBase
*****************************************************************************/
LL::WorkQueueBase::WorkQueueBase(const std::string& name):
    super(makeName(name))
{
    // TODO: register for "LLApp" events so we can implicitly close() on
    // viewer shutdown.
}

void LL::WorkQueueBase::runUntilClose()
{
    try
    {
        for (;;)
        {
            LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD;
            callWork(pop_());
        }
    }
    catch (const Closed&)
    {
    }
}

bool LL::WorkQueueBase::runPending()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD;
    for (Work work; tryPop_(work); )
    {
        callWork(work);
    }
    return ! done();
}

bool LL::WorkQueueBase::runOne()
{
    Work work;
    if (tryPop_(work))
    {
        callWork(work);
    }
    return ! done();
}

bool LL::WorkQueueBase::runUntil(const TimePoint& until)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD;
    // Should we subtract some slop to allow for typical Work execution time?
    // How much slop?
    // runUntil() is simply a time-bounded runPending().
    for (Work work; TimePoint::clock::now() < until && tryPop_(work); )
    {
        callWork(work);
    }
    return ! done();
}

std::string LL::WorkQueueBase::makeName(const std::string& name)
{
    if (! name.empty())
        return name;

    static U32 discriminator = 0;
    static Mutex mutex;
    U32 num;
    {
        // Protect discriminator from concurrent access by different threads.
        // It can't be thread_local, else two racing threads will come up with
        // the same name.
        Lock lk(mutex);
        num = discriminator++;
    }
    return STRINGIZE("WorkQueue" << num);
}

namespace
{
#if LL_WINDOWS

    static const U32 STATUS_MSC_EXCEPTION = 0xE06D7363; // compiler specific

    U32 exception_filter(U32 code, struct _EXCEPTION_POINTERS* exception_infop)
    {
        if (LLApp::instance()->reportCrashToBugsplat((void*)exception_infop))
        {
            // Handled
            return EXCEPTION_CONTINUE_SEARCH;
        }
        else if (code == STATUS_MSC_EXCEPTION)
        {
            // C++ exception, go on
            return EXCEPTION_CONTINUE_SEARCH;
        }
        else
        {
            // handle it, convert to std::exception
            return EXCEPTION_EXECUTE_HANDLER;
        }

        return EXCEPTION_CONTINUE_SEARCH;
    }

    void cpphandle(const LL::WorkQueueBase::Work& work)
    {
        // SE and C++ can not coexists, thus two handlers
        try
        {
            work();
        }
        catch (const LLContinueError&)
        {
            // Any uncaught exception derived from LLContinueError will be caught
            // here and logged. This coroutine will terminate but the rest of the
            // viewer will carry on.
            LOG_UNHANDLED_EXCEPTION(STRINGIZE("LLContinue in work queue"));
        }
    }

    void sehandle(const LL::WorkQueueBase::Work& work)
    {
        __try
        {
            // handle stop and continue exceptions first
            cpphandle(work);
        }
        __except (exception_filter(GetExceptionCode(), GetExceptionInformation()))
        {
            // convert to C++ styled exception
            char integer_string[512];
            sprintf(integer_string, "SEH, code: %lu\n", GetExceptionCode());
            throw std::exception(integer_string);
        }
    }
#endif // LL_WINDOWS
} // anonymous namespace

void LL::WorkQueueBase::callWork(const Work& work)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_THREAD;

#ifdef LL_WINDOWS
    // can not use __try directly, toplevel requires unwinding, thus use of a wrapper
    sehandle(work);
#else // LL_WINDOWS
    try
    {
        work();
    }
    catch (LLContinueError&)
    {
        LOG_UNHANDLED_EXCEPTION(getKey());
    }
    catch (...)
    {
        // Stash any other kind of uncaught exception to be rethrown by main thread.
        LL_WARNS("LLCoros") << "Capturing and rethrowing uncaught exception in WorkQueueBase "
            << getKey() << LL_ENDL;

        LL::WorkQueue::ptr_t main_queue = LL::WorkQueue::getInstance("mainloop");
        main_queue->post(
            // Bind the current exception, rethrow it in main loop.
            [exc = std::current_exception()]() { std::rethrow_exception(exc); });
    }
#endif // else LL_WINDOWS
}

void LL::WorkQueueBase::error(const std::string& msg)
{
    LL_ERRS("WorkQueue") << msg << LL_ENDL;
}

void LL::WorkQueueBase::checkCoroutine(const std::string& method)
{
    // By convention, the default coroutine on each thread has an empty name
    // string. See also LLCoros::logname().
    if (LLCoros::getName().empty())
    {
        LLTHROW(Error("Do not call " + method + " from a thread's default coroutine"));
    }
}

/*****************************************************************************
*   WorkQueue
*****************************************************************************/
LL::WorkQueue::WorkQueue(const std::string& name, size_t capacity):
    super(name),
    mQueue(capacity)
    //<3T:TommyTheTerrible> Adding atomic boolean for blockingconcurrentqueue state.
    ,mRunning(true)
    //</3T>
{
}

void LL::WorkQueue::close()
{
    /*<3T:TommyTheTerrible> Set the atomic boolean to false so threads will leave their while loops.
    mQueue.close();
    */
    mRunning = false;
    //</3T>
}

size_t LL::WorkQueue::size()
{
    /*<3T:TommyTheTerrible> Return the approximate size of the blockingconcurrentqueue.
    return mQueue.size();
    */
    return mQueue.size_approx();
    //</3T>
}

bool LL::WorkQueue::isClosed()
{
    /*<3T:TommyTheTerrible> Return whether blockingconcurrentqueue state is not running.
    return mQueue.isClosed();
    */
    return !mRunning;
    //</3T>
}

bool LL::WorkQueue::done()
{
    /*<3T:TommyTheTerrible> Return whether blockingconcurrentqueue state is not running and queue is empty.
    return mQueue.done();
    */
    return (!mRunning && mQueue.size_approx() == 0);
    //</3T>
}

bool LL::WorkQueue::post(const Work& callable)
{
    /*<3T:TommyTheTerrible> Add work to blockingconcurrentqueue.
    return mQueue.pushIfOpen(callable);
    */
    return mQueue.enqueue(callable);
    //</3T>
}

bool LL::WorkQueue::tryPost(const Work& callable)
{
    /*<3T:TommyTheTerrible> Try adding work to blockingconcurrentqueue.
    return mQueue.tryPush(callable);
    */
    return mQueue.try_enqueue(callable);
    //</3T>
}

LL::WorkQueue::Work LL::WorkQueue::pop_()
{
    /*<3T:TommyTheTerrible> Wait for work if queue is running, throw interrupt when running state set off.
    return mQueue.pop();
    */
    LL::WorkQueue::Work work;
    while (mRunning.load(std::memory_order_acquire))
    {
        if (mQueue.wait_dequeue_timed(work, 1000))
            return work;
    }
    LLTHROW(LLThreadSafeQueueInterrupt());
    //</3T>
}

bool LL::WorkQueue::tryPop_(Work& work)
{
    /*<3T:TommyTheTerrible> Try retrieving a single work task.
    return mQueue.tryPop(work);
    */
    return mQueue.try_dequeue(work);
    //</3T>
}

/*****************************************************************************
*   WorkSchedule
*****************************************************************************/
LL::WorkSchedule::WorkSchedule(const std::string& name, size_t capacity):
    super(name),
    mQueue(capacity)
{
}

void LL::WorkSchedule::close()
{
    mQueue.close();
}

size_t LL::WorkSchedule::size()
{
    return mQueue.size();
}

bool LL::WorkSchedule::isClosed()
{
    return mQueue.isClosed();
}

bool LL::WorkSchedule::done()
{
    return mQueue.done();
}

bool LL::WorkSchedule::post(const Work& callable)
{
    // Use TimePoint::clock::now() instead of TimePoint's representation of
    // the epoch because this WorkSchedule may contain a mix of past-due
    // TimedWork items and TimedWork items scheduled for the future. Sift this
    // new item into the correct place.
    return post(callable, TimePoint::clock::now());
}

bool LL::WorkSchedule::post(const Work& callable, const TimePoint& time)
{
    return mQueue.pushIfOpen(TimedWork(time, callable));
}

bool LL::WorkSchedule::tryPost(const Work& callable)
{
    return tryPost(callable, TimePoint::clock::now());
}

bool LL::WorkSchedule::tryPost(const Work& callable, const TimePoint& time)
{
    return mQueue.tryPush(TimedWork(time, callable));
}

LL::WorkSchedule::Work LL::WorkSchedule::pop_()
{
    return std::get<0>(mQueue.pop());
}

bool LL::WorkSchedule::tryPop_(Work& work)
{
    return mQueue.tryPop(work);
}
