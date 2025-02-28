/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=2 et lcs=trail\:.,tab\:>~ :
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
 * The Original Code is Oracle Corporation code.
 *
 * The Initial Developer of the Original Code is
 *  Oracle Corporation
 * Portions created by the Initial Developer are Copyright (C) 2004
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Vladimir Vukicevic <vladimir.vukicevic@oracle.com>
 *   Brett Wilson <brettw@gmail.com>
 *   Shawn Wilsher <me@shawnwilsher.com>
 *   Lev Serebryakov <lev@serebryakov.spb.ru>
 *   Drew Willcoxon <adw@mozilla.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

#include <stdio.h>

#include "nsError.h"
#include "nsIMutableArray.h"
#include "nsHashSets.h"
#include "nsAutoPtr.h"
#include "nsIFile.h"
#include "nsIPrefService.h"
#include "nsIPrefBranch.h"
#include "nsThreadUtils.h"
#include "nsAutoLock.h"

#include "mozIStorageAggregateFunction.h"
#include "mozIStorageFunction.h"

#include "mozStorageAsyncStatementExecution.h"
#include "mozStorageSQLFunctions.h"
#include "mozStorageConnection.h"
#include "mozStorageService.h"
#include "mozStorageStatement.h"
#include "mozStorageArgValueArray.h"
#include "mozStoragePrivateHelpers.h"
#include "mozStorageStatementData.h"
#include "SQLCollations.h"

#include "prlog.h"
#include "prprf.h"

#ifdef PR_LOGGING
PRLogModuleInfo* gStorageLog = nsnull;
#endif

namespace mozilla {
namespace storage {

#define PREF_TS_SYNCHRONOUS "toolkit.storage.synchronous"

////////////////////////////////////////////////////////////////////////////////
//// Variant Specialization Functions (variantToSQLiteT)

static int
sqlite3_T_int(sqlite3_context *aCtx,
              int aValue)
{
  ::sqlite3_result_int(aCtx, aValue);
  return SQLITE_OK;
}

static int
sqlite3_T_int64(sqlite3_context *aCtx,
                sqlite3_int64 aValue)
{
  ::sqlite3_result_int64(aCtx, aValue);
  return SQLITE_OK;
}

static int
sqlite3_T_double(sqlite3_context *aCtx,
                 double aValue)
{
  ::sqlite3_result_double(aCtx, aValue);
  return SQLITE_OK;
}

static int
sqlite3_T_text(sqlite3_context *aCtx,
               const nsCString &aValue)
{
  ::sqlite3_result_text(aCtx,
                        aValue.get(),
                        aValue.Length(),
                        SQLITE_TRANSIENT);
  return SQLITE_OK;
}

static int
sqlite3_T_text16(sqlite3_context *aCtx,
                 const nsString &aValue)
{
  ::sqlite3_result_text16(aCtx,
                          aValue.get(),
                          aValue.Length() * 2, // Number of bytes.
                          SQLITE_TRANSIENT);
  return SQLITE_OK;
}

static int
sqlite3_T_null(sqlite3_context *aCtx)
{
  ::sqlite3_result_null(aCtx);
  return SQLITE_OK;
}

static int
sqlite3_T_blob(sqlite3_context *aCtx,
               const void *aData,
               int aSize)
{
  ::sqlite3_result_blob(aCtx, aData, aSize, NS_Free);
  return SQLITE_OK;
}

#include "variantToSQLiteT_impl.h"

////////////////////////////////////////////////////////////////////////////////
//// Local Functions

namespace {
#ifdef PR_LOGGING
void tracefunc (void *aClosure, const char *aStmt)
{
  PR_LOG(gStorageLog, PR_LOG_DEBUG, ("sqlite3_trace on %p for '%s'", aClosure,
                                     aStmt));
}
#endif

struct FFEArguments
{
    nsISupports *target;
    bool found;
};
PLDHashOperator
findFunctionEnumerator(const nsACString &aKey,
                       nsISupports *aData,
                       void *aUserArg)
{
  FFEArguments *args = static_cast<FFEArguments *>(aUserArg);
  if (aData == args->target) {
    args->found = PR_TRUE;
    return PL_DHASH_STOP;
  }
  return PL_DHASH_NEXT;
}

void
basicFunctionHelper(sqlite3_context *aCtx,
                    int aArgc,
                    sqlite3_value **aArgv)
{
  void *userData = ::sqlite3_user_data(aCtx);

  mozIStorageFunction *func = static_cast<mozIStorageFunction *>(userData);

  nsRefPtr<ArgValueArray> arguments(new ArgValueArray(aArgc, aArgv));
  if (!arguments)
      return;

  nsCOMPtr<nsIVariant> result;
  if (NS_FAILED(func->OnFunctionCall(arguments, getter_AddRefs(result)))) {
    NS_WARNING("User function returned error code!");
    ::sqlite3_result_error(aCtx,
                           "User function returned error code",
                           -1);
    return;
  }
  if (variantToSQLiteT(aCtx, result) != SQLITE_OK) {
    NS_WARNING("User function returned invalid data type!");
    ::sqlite3_result_error(aCtx,
                           "User function returned invalid data type",
                           -1);
  }
}

void
aggregateFunctionStepHelper(sqlite3_context *aCtx,
                            int aArgc,
                            sqlite3_value **aArgv)
{
  void *userData = ::sqlite3_user_data(aCtx);
  mozIStorageAggregateFunction *func =
    static_cast<mozIStorageAggregateFunction *>(userData);

  nsRefPtr<ArgValueArray> arguments(new ArgValueArray(aArgc, aArgv));
  if (!arguments)
    return;

  if (NS_FAILED(func->OnStep(arguments)))
    NS_WARNING("User aggregate step function returned error code!");
}

void
aggregateFunctionFinalHelper(sqlite3_context *aCtx)
{
  void *userData = ::sqlite3_user_data(aCtx);
  mozIStorageAggregateFunction *func =
    static_cast<mozIStorageAggregateFunction *>(userData);

  nsRefPtr<nsIVariant> result;
  if (NS_FAILED(func->OnFinal(getter_AddRefs(result)))) {
    NS_WARNING("User aggregate final function returned error code!");
    ::sqlite3_result_error(aCtx,
                           "User aggregate final function returned error code",
                           -1);
    return;
  }

  if (variantToSQLiteT(aCtx, result) != SQLITE_OK) {
    NS_WARNING("User aggregate final function returned invalid data type!");
    ::sqlite3_result_error(aCtx,
                           "User aggregate final function returned invalid data type",
                           -1);
  }
}


} // anonymous namespace

////////////////////////////////////////////////////////////////////////////////
//// Connection

Connection::Connection(Service *aService)
: sharedAsyncExecutionMutex("Connection::sharedAsyncExecutionMutex")
, threadOpenedOn(do_GetCurrentThread())
, mDBConn(nsnull)
, mAsyncExecutionMutex(nsAutoLock::NewLock("AsyncExecutionMutex"))
, mAsyncExecutionThreadShuttingDown(PR_FALSE)
, mDBMutex("Connection::mDBMutex")
, mTransactionInProgress(PR_FALSE)
, mProgressHandler(nsnull)
, mStorageService(aService)
{
  mFunctions.Init();
}

Connection::~Connection()
{
  (void)Close();
  nsAutoLock::DestroyLock(mAsyncExecutionMutex);
}

NS_IMPL_THREADSAFE_ISUPPORTS1(
  Connection,
  mozIStorageConnection
)

already_AddRefed<nsIEventTarget>
Connection::getAsyncExecutionTarget()
{
  nsAutoLock mutex(mAsyncExecutionMutex);

  // If we are shutting down the asynchronous thread, don't hand out any more
  // references to the thread.
  if (mAsyncExecutionThreadShuttingDown)
    return nsnull;

  if (!mAsyncExecutionThread) {
    nsresult rv = ::NS_NewThread(getter_AddRefs(mAsyncExecutionThread));
    if (NS_FAILED(rv)) {
      NS_WARNING("Failed to create async thread.");
      return nsnull;
    }
  }

  nsIEventTarget *eventTarget;
  NS_ADDREF(eventTarget = mAsyncExecutionThread);
  return eventTarget;
}

nsresult
Connection::initialize(nsIFile *aDatabaseFile)
{
  NS_ASSERTION (!mDBConn, "Initialize called on already opened database!");
  NS_ENSURE_TRUE(mAsyncExecutionMutex, NS_ERROR_OUT_OF_MEMORY);

  int srv;
  nsresult rv;

  mDatabaseFile = aDatabaseFile;

  if (aDatabaseFile) {
    nsAutoString path;
    rv = aDatabaseFile->GetPath(path);
    NS_ENSURE_SUCCESS(rv, rv);

    srv = ::sqlite3_open(NS_ConvertUTF16toUTF8(path).get(), &mDBConn);
  }
  else {
    // in memory database requested, sqlite uses a magic file name
    srv = ::sqlite3_open(":memory:", &mDBConn);
  }
  if (srv != SQLITE_OK) {
    mDBConn = nsnull;
    return convertResultCode(srv);
  }

  // Properly wrap the database handle's mutex.
  mDBMutex.initWithMutex(sqlite3_db_mutex(mDBConn));

#ifdef PR_LOGGING
  if (!gStorageLog)
    gStorageLog = ::PR_NewLogModule("mozStorage");

  ::sqlite3_trace(mDBConn, tracefunc, this);

  nsCAutoString leafName(":memory");
  if (aDatabaseFile)
    (void)aDatabaseFile->GetNativeLeafName(leafName);
  PR_LOG(gStorageLog, PR_LOG_NOTICE, ("Opening connection to '%s' (%p)",
                                      leafName.get(), this));
#endif

  // Register our built-in SQL functions.
  srv = registerFunctions(mDBConn);
  if (srv != SQLITE_OK) {
    ::sqlite3_close(mDBConn);
    mDBConn = nsnull;
    return convertResultCode(srv);
  }

  // Register our built-in SQL collating sequences.
  srv = registerCollations(mDBConn, mStorageService);
  if (srv != SQLITE_OK) {
    ::sqlite3_close(mDBConn);
    mDBConn = nsnull;
    return convertResultCode(srv);
  }

  // Execute a dummy statement to force the db open, and to verify if it is
  // valid or not.
  sqlite3_stmt *stmt;
  srv = ::sqlite3_prepare_v2(mDBConn, "SELECT * FROM sqlite_master", -1, &stmt,
                             NULL);
  if (srv == SQLITE_OK) {
    srv = ::sqlite3_step(stmt);

    if (srv == SQLITE_DONE || srv == SQLITE_ROW)
        srv = SQLITE_OK;
    ::sqlite3_finalize(stmt);
  }

  if (srv != SQLITE_OK) {
    ::sqlite3_close(mDBConn);
    mDBConn = nsnull;

    return convertResultCode(srv);
  }

  // Set the synchronous PRAGMA, according to the pref
  nsCOMPtr<nsIPrefBranch> pref(do_GetService(NS_PREFSERVICE_CONTRACTID));
  PRInt32 synchronous = 1; // Default to NORMAL if pref not set
  if (pref)
    (void)pref->GetIntPref(PREF_TS_SYNCHRONOUS, &synchronous);

  switch (synchronous) {
    case 2:
      (void)ExecuteSimpleSQL(NS_LITERAL_CSTRING(
          "PRAGMA synchronous = FULL;"));
      break;
    case 0:
      (void)ExecuteSimpleSQL(NS_LITERAL_CSTRING(
          "PRAGMA synchronous = OFF;"));
      break;
    case 1:
    default:
      (void)ExecuteSimpleSQL(NS_LITERAL_CSTRING(
          "PRAGMA synchronous = NORMAL;"));
      break;
  }

  return NS_OK;
}

nsresult
Connection::databaseElementExists(enum DatabaseElementType aElementType,
                                  const nsACString &aElementName,
                                  PRBool *_exists)
{
  if (!mDBConn) return NS_ERROR_NOT_INITIALIZED;

  nsCAutoString query("SELECT name FROM sqlite_master WHERE type = '");
  switch (aElementType) {
    case INDEX:
      query.Append("index");
      break;
    case TABLE:
      query.Append("table");
      break;
  }
  query.Append("' AND name ='");
  query.Append(aElementName);
  query.Append("'");

  sqlite3_stmt *stmt;
  int srv = ::sqlite3_prepare_v2(mDBConn, query.get(), -1, &stmt, NULL);
  if (srv != SQLITE_OK)
    return convertResultCode(srv);

  srv = ::sqlite3_step(stmt);
  // we just care about the return value from step
  (void)::sqlite3_finalize(stmt);

  if (srv == SQLITE_ROW) {
    *_exists = PR_TRUE;
    return NS_OK;
  }
  if (srv == SQLITE_DONE) {
    *_exists = PR_FALSE;
    return NS_OK;
  }

  return convertResultCode(srv);
}

bool
Connection::findFunctionByInstance(nsISupports *aInstance)
{
  mDBMutex.assertCurrentThreadOwns();
  FFEArguments args = { aInstance, false };
  mFunctions.EnumerateRead(findFunctionEnumerator, &args);
  return args.found;
}

/* static */ int
Connection::sProgressHelper(void *aArg)
{
  Connection *_this = static_cast<Connection *>(aArg);
  return _this->progressHandler();
}

int
Connection::progressHandler()
{
  mDBMutex.assertCurrentThreadOwns();
  if (mProgressHandler) {
    PRBool result;
    nsresult rv = mProgressHandler->OnProgress(this, &result);
    if (NS_FAILED(rv)) return 0; // Don't break request
    return result ? 1 : 0;
  }
  return 0;
}

////////////////////////////////////////////////////////////////////////////////
//// mozIStorageConnection

NS_IMETHODIMP
Connection::Close()
{
  if (!mDBConn)
    return NS_ERROR_NOT_INITIALIZED;

#ifdef PR_LOGGING
  nsCAutoString leafName(":memory");
  if (mDatabaseFile)
      (void)mDatabaseFile->GetNativeLeafName(leafName);
  PR_LOG(gStorageLog, PR_LOG_NOTICE, ("Closing connection to '%s'",
                                      leafName.get()));
#endif

  // Flag that we are shutting down the async thread, so that
  // getAsyncExecutionTarget knows not to expose/create the async thread.
  {
    nsAutoLock mutex(mAsyncExecutionMutex);
    mAsyncExecutionThreadShuttingDown = PR_TRUE;
  }
  // Shutdown the async thread if it exists.  (Because we just set the flag,
  // we are the only code that is going to be touching this variable from here
  // on out.)
  if (mAsyncExecutionThread) {
    mAsyncExecutionThread->Shutdown();
    mAsyncExecutionThread = nsnull;
  }

#ifdef DEBUG
  // Notify about any non-finalized statements.
  sqlite3_stmt *stmt = NULL;
  while ((stmt = ::sqlite3_next_stmt(mDBConn, stmt))) {
    char *msg = ::PR_smprintf("SQL statement '%s' was not finalized",
                              ::sqlite3_sql(stmt));
    NS_WARNING(msg);
    ::PR_smprintf_free(msg);
  }
#endif

  int srv = ::sqlite3_close(mDBConn);
  NS_ASSERTION(srv == SQLITE_OK,
               "sqlite3_close failed. There are probably outstanding statements that are listed above!");

  mDBConn = NULL;
  return convertResultCode(srv);
}

NS_IMETHODIMP
Connection::GetConnectionReady(PRBool *_ready)
{
  *_ready = (mDBConn != nsnull);
  return NS_OK;
}

NS_IMETHODIMP
Connection::GetDatabaseFile(nsIFile **_dbFile)
{
  if (!mDBConn) return NS_ERROR_NOT_INITIALIZED;

  NS_IF_ADDREF(*_dbFile = mDatabaseFile);

  return NS_OK;
}

NS_IMETHODIMP
Connection::GetLastInsertRowID(PRInt64 *_id)
{
  if (!mDBConn) return NS_ERROR_NOT_INITIALIZED;

  sqlite_int64 id = ::sqlite3_last_insert_rowid(mDBConn);
  *_id = id;

  return NS_OK;
}

NS_IMETHODIMP
Connection::GetLastError(PRInt32 *_error)
{
  if (!mDBConn) return NS_ERROR_NOT_INITIALIZED;

  *_error = ::sqlite3_errcode(mDBConn);

  return NS_OK;
}

NS_IMETHODIMP
Connection::GetLastErrorString(nsACString &_errorString)
{
  if (!mDBConn) return NS_ERROR_NOT_INITIALIZED;

  const char *serr = ::sqlite3_errmsg(mDBConn);
  _errorString.Assign(serr);

  return NS_OK;
}

NS_IMETHODIMP
Connection::GetSchemaVersion(PRInt32 *_version)
{
  if (!mDBConn) return NS_ERROR_NOT_INITIALIZED;

  nsCOMPtr<mozIStorageStatement> stmt;
  (void)CreateStatement(NS_LITERAL_CSTRING("PRAGMA user_version"),
                        getter_AddRefs(stmt));
  NS_ENSURE_TRUE(stmt, NS_ERROR_OUT_OF_MEMORY);

  *_version = 0;
  PRBool hasResult;
  if (NS_SUCCEEDED(stmt->ExecuteStep(&hasResult)) && hasResult)
    *_version = stmt->AsInt32(0);

  return NS_OK;
}

NS_IMETHODIMP
Connection::SetSchemaVersion(PRInt32 aVersion)
{
  if (!mDBConn) return NS_ERROR_NOT_INITIALIZED;

  nsCAutoString stmt(NS_LITERAL_CSTRING("PRAGMA user_version = "));
  stmt.AppendInt(aVersion);

  return ExecuteSimpleSQL(stmt);
}

NS_IMETHODIMP
Connection::CreateStatement(const nsACString &aSQLStatement,
                            mozIStorageStatement **_stmt)
{
  NS_ENSURE_ARG_POINTER(_stmt);
  if (!mDBConn) return NS_ERROR_NOT_INITIALIZED;

  nsRefPtr<Statement> statement(new Statement());
  NS_ENSURE_TRUE(statement, NS_ERROR_OUT_OF_MEMORY);

  nsresult rv = statement->initialize(this, aSQLStatement);
  NS_ENSURE_SUCCESS(rv, rv);

  Statement *rawPtr;
  statement.forget(&rawPtr);
  *_stmt = rawPtr;
  return NS_OK;
}

NS_IMETHODIMP
Connection::ExecuteSimpleSQL(const nsACString &aSQLStatement)
{
  if (!mDBConn) return NS_ERROR_NOT_INITIALIZED;

  int srv = ::sqlite3_exec(mDBConn, PromiseFlatCString(aSQLStatement).get(),
                           NULL, NULL, NULL);
  return convertResultCode(srv);
}

nsresult
Connection::ExecuteAsync(mozIStorageStatement **aStatements,
                         PRUint32 aNumStatements,
                         mozIStorageStatementCallback *aCallback,
                         mozIStoragePendingStatement **_handle)
{
  nsTArray<StatementData> stmts(aNumStatements);
  for (PRUint32 i = 0; i < aNumStatements; i++) {
    Statement *stmt = static_cast<Statement *>(aStatements[i]);

    // Obtain our StatementData.
    StatementData data;
    nsresult rv = stmt->getAsynchronousStatementData(data);
    NS_ENSURE_SUCCESS(rv, rv);

    NS_ASSERTION(::sqlite3_db_handle(stmt->nativeStatement()) == mDBConn,
                 "Statement must be from this database connection!");

    // Now append it to our array.
    NS_ENSURE_TRUE(stmts.AppendElement(data), NS_ERROR_OUT_OF_MEMORY);
  }

  // Dispatch to the background
  return AsyncExecuteStatements::execute(stmts, this, aCallback, _handle);
}

NS_IMETHODIMP
Connection::TableExists(const nsACString &aTableName,
                        PRBool *_exists)
{
    return databaseElementExists(TABLE, aTableName, _exists);
}

NS_IMETHODIMP
Connection::IndexExists(const nsACString &aIndexName,
                        PRBool* _exists)
{
    return databaseElementExists(INDEX, aIndexName, _exists);
}

NS_IMETHODIMP
Connection::GetTransactionInProgress(PRBool *_inProgress)
{
  if (!mDBConn) return NS_ERROR_NOT_INITIALIZED;

  SQLiteMutexAutoLock lockedScope(mDBMutex);
  *_inProgress = mTransactionInProgress;
  return NS_OK;
}

NS_IMETHODIMP
Connection::BeginTransaction()
{
  return BeginTransactionAs(mozIStorageConnection::TRANSACTION_DEFERRED);
}

NS_IMETHODIMP
Connection::BeginTransactionAs(PRInt32 aTransactionType)
{
  if (!mDBConn) return NS_ERROR_NOT_INITIALIZED;

  SQLiteMutexAutoLock lockedScope(mDBMutex);
  if (mTransactionInProgress)
    return NS_ERROR_FAILURE;
  nsresult rv;
  switch(aTransactionType) {
    case TRANSACTION_DEFERRED:
      rv = ExecuteSimpleSQL(NS_LITERAL_CSTRING("BEGIN DEFERRED"));
      break;
    case TRANSACTION_IMMEDIATE:
      rv = ExecuteSimpleSQL(NS_LITERAL_CSTRING("BEGIN IMMEDIATE"));
      break;
    case TRANSACTION_EXCLUSIVE:
      rv = ExecuteSimpleSQL(NS_LITERAL_CSTRING("BEGIN EXCLUSIVE"));
      break;
    default:
      return NS_ERROR_ILLEGAL_VALUE;
  }
  if (NS_SUCCEEDED(rv))
    mTransactionInProgress = PR_TRUE;
  return rv;
}

NS_IMETHODIMP
Connection::CommitTransaction()
{
  if (!mDBConn) return NS_ERROR_NOT_INITIALIZED;

  SQLiteMutexAutoLock lockedScope(mDBMutex);
  if (!mTransactionInProgress)
    return NS_ERROR_FAILURE;
  nsresult rv = ExecuteSimpleSQL(NS_LITERAL_CSTRING("COMMIT TRANSACTION"));
  if (NS_SUCCEEDED(rv))
    mTransactionInProgress = PR_FALSE;
  return rv;
}

NS_IMETHODIMP
Connection::RollbackTransaction()
{
  if (!mDBConn) return NS_ERROR_NOT_INITIALIZED;

  SQLiteMutexAutoLock lockedScope(mDBMutex);
  if (!mTransactionInProgress)
    return NS_ERROR_FAILURE;
  nsresult rv = ExecuteSimpleSQL(NS_LITERAL_CSTRING("ROLLBACK TRANSACTION"));
  if (NS_SUCCEEDED(rv))
    mTransactionInProgress = PR_FALSE;
  return rv;
}

NS_IMETHODIMP
Connection::CreateTable(const char *aTableName,
                        const char *aTableSchema)
{
  if (!mDBConn) return NS_ERROR_NOT_INITIALIZED;

  char *buf = ::PR_smprintf("CREATE TABLE %s (%s)", aTableName, aTableSchema);
  if (!buf)
    return NS_ERROR_OUT_OF_MEMORY;

  int srv = ::sqlite3_exec(mDBConn, buf, NULL, NULL, NULL);
  ::PR_smprintf_free(buf);

  return convertResultCode(srv);
}

NS_IMETHODIMP
Connection::CreateFunction(const nsACString &aFunctionName,
                           PRInt32 aNumArguments,
                           mozIStorageFunction *aFunction)
{
  if (!mDBConn) return NS_ERROR_NOT_INITIALIZED;

  // Check to see if this function is already defined.  We only check the name
  // because a function can be defined with the same body but different names.
  SQLiteMutexAutoLock lockedScope(mDBMutex);
  NS_ENSURE_FALSE(mFunctions.Get(aFunctionName, NULL), NS_ERROR_FAILURE);

  int srv = ::sqlite3_create_function(mDBConn,
                                      nsPromiseFlatCString(aFunctionName).get(),
                                      aNumArguments,
                                      SQLITE_ANY,
                                      aFunction,
                                      basicFunctionHelper,
                                      NULL,
                                      NULL);
  if (srv != SQLITE_OK)
    return convertResultCode(srv);

  NS_ENSURE_TRUE(mFunctions.Put(aFunctionName, aFunction),
                 NS_ERROR_OUT_OF_MEMORY);

  return NS_OK;
}

NS_IMETHODIMP
Connection::CreateAggregateFunction(const nsACString &aFunctionName,
                                    PRInt32 aNumArguments,
                                    mozIStorageAggregateFunction *aFunction)
{
  if (!mDBConn) return NS_ERROR_NOT_INITIALIZED;

  // Check to see if this function name is already defined.
  SQLiteMutexAutoLock lockedScope(mDBMutex);
  NS_ENSURE_FALSE(mFunctions.Get(aFunctionName, NULL), NS_ERROR_FAILURE);

  // Because aggregate functions depend on state across calls, you cannot have
  // the same instance use the same name.  We want to enumerate all functions
  // and make sure this instance is not already registered.
  NS_ENSURE_FALSE(findFunctionByInstance(aFunction), NS_ERROR_FAILURE);

  int srv = ::sqlite3_create_function(mDBConn,
                                      nsPromiseFlatCString(aFunctionName).get(),
                                      aNumArguments,
                                      SQLITE_ANY,
                                      aFunction,
                                      NULL,
                                      aggregateFunctionStepHelper,
                                      aggregateFunctionFinalHelper);
  if (srv != SQLITE_OK)
    return convertResultCode(srv);

  NS_ENSURE_TRUE(mFunctions.Put(aFunctionName, aFunction),
                 NS_ERROR_OUT_OF_MEMORY);

  return NS_OK;
}

NS_IMETHODIMP
Connection::RemoveFunction(const nsACString &aFunctionName)
{
  if (!mDBConn) return NS_ERROR_NOT_INITIALIZED;

  SQLiteMutexAutoLock lockedScope(mDBMutex);
  NS_ENSURE_TRUE(mFunctions.Get(aFunctionName, NULL), NS_ERROR_FAILURE);

  int srv = ::sqlite3_create_function(mDBConn,
                                      nsPromiseFlatCString(aFunctionName).get(),
                                      0,
                                      SQLITE_ANY,
                                      NULL,
                                      NULL,
                                      NULL,
                                      NULL);
  if (srv != SQLITE_OK)
    return convertResultCode(srv);

  mFunctions.Remove(aFunctionName);

  return NS_OK;
}

NS_IMETHODIMP
Connection::SetProgressHandler(PRInt32 aGranularity,
                               mozIStorageProgressHandler *aHandler,
                               mozIStorageProgressHandler **_oldHandler)
{
  if (!mDBConn) return NS_ERROR_NOT_INITIALIZED;

  // Return previous one
  SQLiteMutexAutoLock lockedScope(mDBMutex);
  NS_IF_ADDREF(*_oldHandler = mProgressHandler);

  if (!aHandler || aGranularity <= 0) {
    aHandler = nsnull;
    aGranularity = 0;
  }
  mProgressHandler = aHandler;
  ::sqlite3_progress_handler(mDBConn, aGranularity, sProgressHelper, this);

  return NS_OK;
}

NS_IMETHODIMP
Connection::RemoveProgressHandler(mozIStorageProgressHandler **_oldHandler)
{
  if (!mDBConn) return NS_ERROR_NOT_INITIALIZED;

  // Return previous one
  SQLiteMutexAutoLock lockedScope(mDBMutex);
  NS_IF_ADDREF(*_oldHandler = mProgressHandler);

  mProgressHandler = nsnull;
  ::sqlite3_progress_handler(mDBConn, 0, NULL, NULL);

  return NS_OK;
}

} // namespace storage
} // namespace mozilla
