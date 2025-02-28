/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: sw=4 ts=4 et :
 * ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Chris Jones <jones.chris.g@gmail.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

#include "prenv.h"
#include "prerror.h"
#include "prio.h"
#include "prproces.h"

#include "nsMemory.h"

#include "mozilla/CondVar.h"
#include "mozilla/Monitor.h"
#include "mozilla/Mutex.h"

#include "TestHarness.h"

using namespace mozilla;

static PRThread*
spawn(void (*run)(void*), void* arg)
{
    return PR_CreateThread(PR_SYSTEM_THREAD,
                           run,
                           arg,
                           PR_PRIORITY_NORMAL,
                           PR_GLOBAL_THREAD,
                           PR_JOINABLE_THREAD,
                           0);
}

#define PASS()                                  \
    do {                                        \
        passed(__FUNCTION__);                   \
        return NS_OK;                           \
    } while (0);


#define FAIL(why)                               \
    do {                                        \
        fail(why);                              \
        return NS_ERROR_FAILURE;                \
    } while (0);

//-----------------------------------------------------------------------------

static const char* sPathToThisBinary;
static const char* sAssertBehaviorEnv = "XPCOM_DEBUG_BREAK=abort";

class Subprocess
{
public:
    // not available until process finishes
    PRInt32 mExitCode;
    nsCString mStdout;
    nsCString mStderr;

    Subprocess(const char* aTestName) {
        // set up stdio redirection
        PRFileDesc* readStdin;  PRFileDesc* writeStdin;
        PRFileDesc* readStdout; PRFileDesc* writeStdout;
        PRFileDesc* readStderr; PRFileDesc* writeStderr;
        PRProcessAttr* pattr = PR_NewProcessAttr();

        NS_ASSERTION(pattr, "couldn't allocate process attrs");

        NS_ASSERTION(PR_SUCCESS == PR_CreatePipe(&readStdin, &writeStdin),
                     "couldn't create child stdin pipe");
        NS_ASSERTION(PR_SUCCESS == PR_SetFDInheritable(readStdin, PR_TRUE),
                     "couldn't set child stdin inheritable");
        PR_ProcessAttrSetStdioRedirect(pattr, PR_StandardInput, readStdin);

        NS_ASSERTION(PR_SUCCESS == PR_CreatePipe(&readStdout, &writeStdout),
                     "couldn't create child stdout pipe");
        NS_ASSERTION(PR_SUCCESS == PR_SetFDInheritable(writeStdout, PR_TRUE),
                     "couldn't set child stdout inheritable");
        PR_ProcessAttrSetStdioRedirect(pattr, PR_StandardOutput, writeStdout);

        NS_ASSERTION(PR_SUCCESS == PR_CreatePipe(&readStderr, &writeStderr),
                     "couldn't create child stderr pipe");
        NS_ASSERTION(PR_SUCCESS == PR_SetFDInheritable(writeStderr, PR_TRUE),
                     "couldn't set child stderr inheritable");
        PR_ProcessAttrSetStdioRedirect(pattr, PR_StandardError, writeStderr);

        // set up argv with test name to run
        char* const newArgv[3] = {
            strdup(sPathToThisBinary),
            strdup(aTestName),
            0
        };

        // make sure the child will abort if an assertion fails
        NS_ASSERTION(PR_SUCCESS == PR_SetEnv(sAssertBehaviorEnv),
                     "couldn't set XPCOM_DEBUG_BREAK env var");

        PRProcess* proc;
        NS_ASSERTION(proc = PR_CreateProcess(sPathToThisBinary,
                                             newArgv,
                                             0, // inherit environment
                                             pattr),
                     "couldn't create process");
        PR_Close(readStdin);
        PR_Close(writeStdout);
        PR_Close(writeStderr);

        mProc = proc;
        mStdinfd = writeStdin;
        mStdoutfd = readStdout;
        mStderrfd = readStderr;

        free(newArgv[0]);
        free(newArgv[1]);
        PR_DestroyProcessAttr(pattr);
    }

    void RunToCompletion(PRUint32 aWaitMs)
    {
        PR_Close(mStdinfd);

        PRPollDesc pollfds[2];
        PRInt32 nfds;
        PRBool stdoutOpen = PR_TRUE, stderrOpen = PR_TRUE;
        char buf[4096];
        PRInt32 len;

        PRIntervalTime now = PR_IntervalNow();
        PRIntervalTime deadline = now + PR_MillisecondsToInterval(aWaitMs);

        while ((stdoutOpen || stderrOpen) && now < deadline) {
            nfds = 0;
            if (stdoutOpen) {
                pollfds[nfds].fd = mStdoutfd;
                pollfds[nfds].in_flags = PR_POLL_READ;
                pollfds[nfds].out_flags = 0;
                ++nfds;
            }
            if (stderrOpen) {
                pollfds[nfds].fd = mStderrfd;
                pollfds[nfds].in_flags = PR_POLL_READ;
                pollfds[nfds].out_flags = 0;
                ++nfds;
            }

            PRInt32 rv = PR_Poll(pollfds, nfds, deadline - now);
            NS_ASSERTION(0 <= rv, PR_ErrorToName(PR_GetError()));

            if (0 == rv) {      // timeout
                Finish(PR_FALSE); // abnormal
                return;
            }

            for (PRInt32 i = 0; i < nfds; ++i) {
                if (!pollfds[i].out_flags)
                    continue;

                PRBool isStdout = mStdoutfd == pollfds[i].fd;
                
                if (PR_POLL_READ & pollfds[i].out_flags) {
                    len = PR_Read(pollfds[i].fd, buf, sizeof(buf) - 1);
                    NS_ASSERTION(0 <= len, PR_ErrorToName(PR_GetError()));
                }
                else if (PR_POLL_HUP & pollfds[i].out_flags) {
                    len = 0;
                }
                else {
                    NS_ERROR(PR_ErrorToName(PR_GetError()));
                }

                if (0 < len) {
                    buf[len] = '\0';
                    if (isStdout)
                        mStdout += buf;
                    else
                        mStderr += buf;
                }
                else {
                    if (isStdout) {
                        stdoutOpen = PR_FALSE;
                        PR_Close(mStdoutfd);
                    }
                    else {
                        stderrOpen = PR_FALSE;
                        PR_Close(mStderrfd);
                    }
                }
            }

            now = PR_IntervalNow();
        }

        Finish(!stdoutOpen && !stderrOpen && now <= deadline);
    }

private:
    void Finish(PRBool normalExit) {
        if (!normalExit) {
            PR_KillProcess(mProc);
            PR_Close(mStdoutfd);
            PR_Close(mStderrfd);
            mExitCode = -1;
            PRInt32 dummy;
            PR_WaitProcess(mProc, &dummy);
        }
        else {
            PR_WaitProcess(mProc, &mExitCode); // this had better not block ...
        }
    }

    PRProcess* mProc;
    PRFileDesc* mStdinfd;         // writeable
    PRFileDesc* mStdoutfd;        // readable
    PRFileDesc* mStderrfd;        // readable
};

//-----------------------------------------------------------------------------
// Harness for checking detector errors
bool
CheckForDeadlock(const char* test, const char* const* findTokens)
{
    Subprocess proc(test);
    proc.RunToCompletion(1000);

    if (0 == proc.mExitCode)
        return false;

    PRInt32 idx = 0;
    for (const char* const* tp = findTokens; *tp; ++tp) {
        const char* const token = *tp;
#ifdef MOZILLA_INTERNAL_API
        idx = proc.mStderr.Find(token, PR_FALSE, idx);
#else
        nsCString tokenCString(token);
        idx = proc.mStderr.Find(tokenCString, idx);
#endif
        if (-1 == idx) {
            printf("(missed token '%s' in output)\n", token);
            puts("----------------------------------\n");
            puts(proc.mStderr.get());
            puts("----------------------------------\n");
            return false;
        }
        idx += strlen(token);
    }

    return true;
}

//-----------------------------------------------------------------------------
// Single-threaded sanity tests

// Stupidest possible deadlock.
nsresult
Sanity_Child()
{
    mozilla::Mutex m1("dd.sanity.m1");
    m1.Lock();
    m1.Lock();
    return 0;                  // not reached
}

nsresult
Sanity()
{
    const char* const tokens[] = {
        "###!!! ERROR: Potential deadlock detected",
        "=== Cyclical dependency starts at\n--- Mutex : dd.sanity.m1",
        "=== Cycle completed at\n--- Mutex : dd.sanity.m1",
        "###!!! Deadlock may happen NOW!", // better catch these easy cases...
        "###!!! ASSERTION: Potential deadlock detected",
        0
    };
    if (CheckForDeadlock("Sanity", tokens)) {
        PASS();
    } else {
        FAIL("deadlock not detected");
    }
}

// Slightly less stupid deadlock.
nsresult
Sanity2_Child()
{
    mozilla::Mutex m1("dd.sanity2.m1");
    mozilla::Mutex m2("dd.sanity2.m2");
    m1.Lock();
    m2.Lock();
    m1.Lock();
    return 0;                  // not reached
}

nsresult
Sanity2()
{
    const char* const tokens[] = {
        "###!!! ERROR: Potential deadlock detected",
        "=== Cyclical dependency starts at\n--- Mutex : dd.sanity2.m1",
        "--- Next dependency:\n--- Mutex : dd.sanity2.m2",
        "=== Cycle completed at\n--- Mutex : dd.sanity2.m1",
        "###!!! Deadlock may happen NOW!", // better catch these easy cases...
        "###!!! ASSERTION: Potential deadlock detected",
        0
    };
    if (CheckForDeadlock("Sanity2", tokens)) {
        PASS();
    } else {
        FAIL("deadlock not detected");
    }
}


nsresult
Sanity3_Child()
{
    mozilla::Mutex m1("dd.sanity3.m1");
    mozilla::Mutex m2("dd.sanity3.m2");
    mozilla::Mutex m3("dd.sanity3.m3");
    mozilla::Mutex m4("dd.sanity3.m4");

    m1.Lock();
    m2.Lock();
    m3.Lock();
    m4.Lock();
    m4.Unlock();
    m3.Unlock();
    m2.Unlock();
    m1.Unlock();

    m4.Lock();
    m1.Lock();
    return 0;
}

nsresult
Sanity3()
{
    const char* const tokens[] = {
        "###!!! ERROR: Potential deadlock detected",
        "=== Cyclical dependency starts at\n--- Mutex : dd.sanity3.m1",
        "--- Next dependency:\n--- Mutex : dd.sanity3.m2",
        "--- Next dependency:\n--- Mutex : dd.sanity3.m3",
        "--- Next dependency:\n--- Mutex : dd.sanity3.m4",
        "=== Cycle completed at\n--- Mutex : dd.sanity3.m1",
        "###!!! ASSERTION: Potential deadlock detected",
        0
    };
    if (CheckForDeadlock("Sanity3", tokens)) {
        PASS();
    } else {
        FAIL("deadlock not detected");
    }
}


nsresult
Sanity4_Child()
{
    mozilla::Monitor m1("dd.sanity4.m1");
    mozilla::Mutex m2("dd.sanity4.m2");
    m1.Enter();
    m2.Lock();
    m1.Enter();
    return 0;
}

nsresult
Sanity4()
{
    const char* const tokens[] = {
        "Re-entering Monitor after acquiring other resources",
        "###!!! ERROR: Potential deadlock detected",
        "=== Cyclical dependency starts at\n--- Monitor : dd.sanity4.m1",
        "--- Next dependency:\n--- Mutex : dd.sanity4.m2",
        "=== Cycle completed at\n--- Monitor : dd.sanity4.m1",
        "###!!! ASSERTION: Potential deadlock detected",
        0
    };
    if (CheckForDeadlock("Sanity4", tokens)) {
        PASS();
    } else {
        FAIL("deadlock not detected");
    }
}

//-----------------------------------------------------------------------------
// Multithreaded tests

mozilla::Mutex* ttM1;
mozilla::Mutex* ttM2;

static void
TwoThreads_thread(void* arg)
{
    PRInt32 m1First = NS_PTR_TO_INT32(arg);
    if (m1First) {
        ttM1->Lock();
        ttM2->Lock();
        ttM2->Unlock();
        ttM1->Unlock();
    }
    else {
        ttM2->Lock();
        ttM1->Lock();
        ttM1->Unlock();
        ttM2->Unlock();
    }
}

nsresult
TwoThreads_Child()
{
    ttM1 = new mozilla::Mutex("dd.twothreads.m1");
    ttM2 = new mozilla::Mutex("dd.twothreads.m2");
    if (!ttM1 || !ttM2)
        NS_RUNTIMEABORT("couldn't allocate mutexes");

    PRThread* t1 = spawn(TwoThreads_thread, (void*) 0);
    PR_JoinThread(t1);

    PRThread* t2 = spawn(TwoThreads_thread, (void*) 1);
    PR_JoinThread(t2);

    return 0;
}

nsresult
TwoThreads()
{
    const char* const tokens[] = {
        "###!!! ERROR: Potential deadlock detected",
        "=== Cyclical dependency starts at\n--- Mutex : dd.twothreads.m2",
        "--- Next dependency:\n--- Mutex : dd.twothreads.m1",
        "=== Cycle completed at\n--- Mutex : dd.twothreads.m2",
        "###!!! ASSERTION: Potential deadlock detected",
        0
    };

    if (CheckForDeadlock("TwoThreads", tokens)) {
        PASS();
    } else {
        FAIL("deadlock not detected");
    }
}


mozilla::Mutex* cndMs[4];
const PRUint32 K = 100000;

static void
ContentionNoDeadlock_thread(void* arg)
{
    PRInt32 starti = NS_PTR_TO_INT32(arg);

    for (PRUint32 k = 0; k < K; ++k) {
        for (PRInt32 i = starti; i < (PRInt32) NS_ARRAY_LENGTH(cndMs); ++i)
            cndMs[i]->Lock();
        // comment out the next two lines for deadlocking fun!
        for (PRInt32 i = NS_ARRAY_LENGTH(cndMs) - 1; i >= starti; --i)
            cndMs[i]->Unlock();

        starti = (starti + 1) % 3;
    }
}

nsresult
ContentionNoDeadlock_Child()
{
    PRThread* threads[3];

    for (PRUint32 i = 0; i < NS_ARRAY_LENGTH(cndMs); ++i)
        cndMs[i] = new mozilla::Mutex("dd.cnd.ms");

    for (PRInt32 i = 0; i < (PRInt32) NS_ARRAY_LENGTH(threads); ++i)
        threads[i] = spawn(ContentionNoDeadlock_thread, NS_INT32_TO_PTR(i));

    for (PRUint32 i = 0; i < NS_ARRAY_LENGTH(threads); ++i)
        PR_JoinThread(threads[i]);

    for (PRUint32 i = 0; i < NS_ARRAY_LENGTH(cndMs); ++i)
        delete cndMs[i];

    return 0;
}

nsresult
ContentionNoDeadlock()
{
    const char * func = __func__;
    Subprocess proc(func);
    proc.RunToCompletion(10000);
    if (0 != proc.mExitCode) {
        printf("(expected 0 == return code, got %d)\n", proc.mExitCode);
        puts("(output)\n----------------------------------\n");
        puts(proc.mStdout.get());
        puts("----------------------------------\n");
        puts("(error output)\n----------------------------------\n");
        puts(proc.mStderr.get());
        puts("----------------------------------\n");

        FAIL("deadlock");
    }
    PASS();
}



//-----------------------------------------------------------------------------

int
main(int argc, char** argv)
{
    if (1 < argc) {
        // XXX can we run w/o scoped XPCOM?
        const char* test = argv[1];
        ScopedXPCOM xpcom(test);
        if (xpcom.failed())
            return 1;

        // running in a spawned process.  call the specificed child function.
        if (!strcmp("Sanity", test))
            return Sanity_Child();
        if (!strcmp("Sanity2", test))
            return Sanity2_Child();
        if (!strcmp("Sanity3", test))
            return Sanity3_Child();
        if (!strcmp("Sanity4", test))
            return Sanity4_Child();

        if (!strcmp("TwoThreads", test))
            return TwoThreads_Child();
        if (!strcmp("ContentionNoDeadlock", test))
            return ContentionNoDeadlock_Child();

        FAIL("unknown child test");
    }

    ScopedXPCOM xpcom("Deadlock detector correctness");
    if (xpcom.failed())
        return 1;

    // in the first invocation of this process.  we will be the "driver".
    int rv = 0;

    sPathToThisBinary = argv[0];

    if (NS_FAILED(Sanity()))
        rv = 1;
    if (NS_FAILED(Sanity2()))
        rv = 1;
    if (NS_FAILED(Sanity3()))
        rv = 1;
    if (NS_FAILED(Sanity4()))
        rv = 1;

    if (NS_FAILED(TwoThreads()))
        rv = 1;
    if (NS_FAILED(ContentionNoDeadlock()))
        rv = 1;

    return rv;
}
