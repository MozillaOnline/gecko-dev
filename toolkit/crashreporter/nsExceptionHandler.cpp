/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* ***** BEGIN LICENSE BLOCK *****
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
 * The Original Code is Mozilla Breakpad integration
 *
 * The Initial Developer of the Original Code is
 * Ted Mielczarek <ted.mielczarek@gmail.com>
 * Portions created by the Initial Developer are Copyright (C) 2006
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Josh Aas <josh@mozilla.com>
 *  Justin Dolske <dolske@mozilla.com>
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

#include "nsExceptionHandler.h"

#if defined(XP_WIN32)
#ifdef WIN32_LEAN_AND_MEAN
#undef WIN32_LEAN_AND_MEAN
#endif

#include "nsIWindowsRegKey.h"
#if defined(MOZ_IPC)
#  include "client/windows/crash_generation/crash_generation_server.h"
#endif
#include "client/windows/handler/exception_handler.h"
#include <DbgHelp.h>
#include <string.h>
#elif defined(XP_MACOSX)
#include "client/mac/handler/exception_handler.h"
#include <string>
#include <Carbon/Carbon.h>
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#include "mac_utils.h"
#elif defined(XP_LINUX)
#include "nsDirectoryServiceUtils.h"
#include "nsDirectoryServiceDefs.h"
#include "nsIINIParser.h"
#if defined(MOZ_IPC)
#  include "client/linux/crash_generation/client_info.h"
#  include "client/linux/crash_generation/crash_generation_server.h"
#endif
#include "client/linux/handler/exception_handler.h"
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#elif defined(XP_SOLARIS)
#include "client/solaris/handler/exception_handler.h"
#include <fcntl.h>
#include <sys/types.h>
#include <unistd.h>
#else
#error "Not yet implemented for this platform"
#endif // defined(XP_WIN32)

#include <stdlib.h>
#include <time.h>
#include <prenv.h>
#include <prio.h>
#include <prmem.h>
#include "mozilla/Mutex.h"
#include "nsDebug.h"
#include "nsCRT.h"
#include "nsILocalFile.h"
#include "nsIFileStreams.h"
#include "nsDataHashtable.h"
#include "nsInterfaceHashtable.h"
#include "prprf.h"
#include "nsIXULAppInfo.h"

#if defined(MOZ_IPC)
using google_breakpad::CrashGenerationServer;
using google_breakpad::ClientInfo;

using mozilla::Mutex;
using mozilla::MutexAutoLock;
#endif

namespace CrashReporter {

#ifdef XP_WIN32
typedef wchar_t XP_CHAR;
#define CONVERT_UTF16_TO_XP_CHAR(x) x
#define CONVERT_XP_CHAR_TO_UTF16(x) x
#define XP_STRLEN(x) wcslen(x)
#define CRASH_REPORTER_FILENAME "crashreporter.exe"
#define PATH_SEPARATOR "\\"
#define XP_PATH_SEPARATOR L"\\"
// sort of arbitrary, but MAX_PATH is kinda small
#define XP_PATH_MAX 4096
// "<reporter path>" "<minidump path>"
#define CMDLINE_SIZE ((XP_PATH_MAX * 2) + 6)
#ifdef _USE_32BIT_TIME_T
#define XP_TTOA(time, buffer, base) ltoa(time, buffer, base)
#else
#define XP_TTOA(time, buffer, base) _i64toa(time, buffer, base)
#endif
#else
typedef char XP_CHAR;
#define CONVERT_UTF16_TO_XP_CHAR(x) NS_ConvertUTF16toUTF8(x)
#define CONVERT_XP_CHAR_TO_UTF16(x) NS_ConvertUTF8toUTF16(x)
#define XP_STRLEN(x) strlen(x)
#define CRASH_REPORTER_FILENAME "crashreporter"
#define PATH_SEPARATOR "/"
#define XP_PATH_SEPARATOR "/"
#define XP_PATH_MAX PATH_MAX
#define XP_TTOA(time, buffer, base) sprintf(buffer, "%ld", time)
#endif // XP_WIN32

static const XP_CHAR dumpFileExtension[] = {'.', 'd', 'm', 'p',
                                            '\0'}; // .dmp
static const XP_CHAR extraFileExtension[] = {'.', 'e', 'x', 't',
                                             'r', 'a', '\0'}; // .extra

static google_breakpad::ExceptionHandler* gExceptionHandler = nsnull;

static XP_CHAR* crashReporterPath;

// if this is false, we don't launch the crash reporter
static bool doReport = true;

// if this is true, we pass the exception on to the OS crash reporter
static bool showOSCrashReporter = false;

// The time of the last recorded crash, as a time_t value.
static time_t lastCrashTime = 0;
// The pathname of a file to store the crash time in
static XP_CHAR lastCrashTimeFilename[XP_PATH_MAX] = {0};

// these are just here for readability
static const char kCrashTimeParameter[] = "CrashTime=";
static const int kCrashTimeParameterLen = sizeof(kCrashTimeParameter)-1;

static const char kTimeSinceLastCrashParameter[] = "SecondsSinceLastCrash=";
static const int kTimeSinceLastCrashParameterLen =
                                     sizeof(kTimeSinceLastCrashParameter)-1;

// this holds additional data sent via the API
static nsDataHashtable<nsCStringHashKey,nsCString>* crashReporterAPIData_Hash;
static nsCString* crashReporterAPIData = nsnull;
static nsCString* notesField = nsnull;

#if defined(MOZ_IPC)
// OOP crash reporting
static CrashGenerationServer* crashServer; // chrome process has this

#  if defined(XP_WIN)
// If crash reporting is disabled, we hand out this "null" pipe to the
// child process and don't attempt to connect to a parent server.
static const char kNullNotifyPipe[] = "-";
static char* childCrashNotifyPipe;

#  elif defined(XP_LINUX)
static int serverSocketFd = -1;
static int clientSocketFd = -1;
static const int kMagicChildCrashReportFd = 42;
#  endif

// |dumpMapLock| must protect all access to |pidToMinidump|.
static Mutex* dumpMapLock;
typedef nsInterfaceHashtable<nsUint32HashKey, nsIFile> ChildMinidumpMap;
static ChildMinidumpMap* pidToMinidump;

#endif  // MOZ_IPC

static XP_CHAR*
Concat(XP_CHAR* str, const XP_CHAR* toAppend, int* size)
{
  int appendLen = XP_STRLEN(toAppend);
  if (appendLen >= *size) appendLen = *size - 1;

  memcpy(str, toAppend, appendLen * sizeof(XP_CHAR));
  str += appendLen;
  *str = '\0';
  *size -= appendLen;

  return str;
}

bool MinidumpCallback(const XP_CHAR* dump_path,
                      const XP_CHAR* minidump_id,
                      void* context,
#ifdef XP_WIN32
                      EXCEPTION_POINTERS* exinfo,
                      MDRawAssertionInfo* assertion,
#endif
                      bool succeeded)
{
  bool returnValue = showOSCrashReporter ? false : succeeded;

  XP_CHAR minidumpPath[XP_PATH_MAX];
  int size = XP_PATH_MAX;
  XP_CHAR* p = Concat(minidumpPath, dump_path, &size);
  p = Concat(p, XP_PATH_SEPARATOR, &size);
  p = Concat(p, minidump_id, &size);
  Concat(p, dumpFileExtension, &size);

  XP_CHAR extraDataPath[XP_PATH_MAX];
  size = XP_PATH_MAX;
  p = Concat(extraDataPath, dump_path, &size);
  p = Concat(p, XP_PATH_SEPARATOR, &size);
  p = Concat(p, minidump_id, &size);
  Concat(p, extraFileExtension, &size);

  // calculate time since last crash (if possible), and store
  // the time of this crash.
  time_t crashTime = time(NULL);
  time_t timeSinceLastCrash = 0;
  // stringified versions of the above
  char crashTimeString[32];
  int crashTimeStringLen = 0;
  char timeSinceLastCrashString[32];
  int timeSinceLastCrashStringLen = 0;

  XP_TTOA(crashTime, crashTimeString, 10);
  crashTimeStringLen = strlen(crashTimeString);
  if (lastCrashTime != 0) {
    timeSinceLastCrash = crashTime - lastCrashTime;
    XP_TTOA(timeSinceLastCrash, timeSinceLastCrashString, 10);
    timeSinceLastCrashStringLen = strlen(timeSinceLastCrashString);
  }
  // write crash time to file
  if (lastCrashTimeFilename[0] != 0) {
#if defined(XP_WIN32)
    HANDLE hFile = CreateFile(lastCrashTimeFilename, GENERIC_WRITE, 0,
                              NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                              NULL);
    if(hFile != INVALID_HANDLE_VALUE) {
      DWORD nBytes;
      WriteFile(hFile, crashTimeString, crashTimeStringLen, &nBytes, NULL);
      CloseHandle(hFile);
    }
#elif defined(XP_UNIX)
    int fd = open(lastCrashTimeFilename,
                  O_WRONLY | O_CREAT | O_TRUNC,
                  0600);
    if (fd != -1) {
      ssize_t ignored = write(fd, crashTimeString, crashTimeStringLen);
      (void)ignored;
      close(fd);
    }
#endif
  }

#if defined(XP_WIN32)
  XP_CHAR cmdLine[CMDLINE_SIZE];
  size = CMDLINE_SIZE;
  p = Concat(cmdLine, L"\"", &size);
  p = Concat(p, crashReporterPath, &size);
  p = Concat(p, L"\" \"", &size);
  p = Concat(p, minidumpPath, &size);
  Concat(p, L"\"", &size);

  if (!crashReporterAPIData->IsEmpty()) {
    // write out API data
    HANDLE hFile = CreateFile(extraDataPath, GENERIC_WRITE, 0,
                              NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                              NULL);
    if(hFile != INVALID_HANDLE_VALUE) {
      DWORD nBytes;
      WriteFile(hFile, crashReporterAPIData->get(),
                crashReporterAPIData->Length(), &nBytes, NULL);
      WriteFile(hFile, kCrashTimeParameter, kCrashTimeParameterLen,
                &nBytes, NULL);
      WriteFile(hFile, crashTimeString, crashTimeStringLen, &nBytes, NULL);
      WriteFile(hFile, "\n", 1, &nBytes, NULL);
      if (timeSinceLastCrash != 0) {
        WriteFile(hFile, kTimeSinceLastCrashParameter,
                  kTimeSinceLastCrashParameterLen, &nBytes, NULL);
        WriteFile(hFile, timeSinceLastCrashString, timeSinceLastCrashStringLen,
                  &nBytes, NULL);
        WriteFile(hFile, "\n", 1, &nBytes, NULL);
      }
      CloseHandle(hFile);
    }
  }

  if (!doReport) {
    return returnValue;
  }

  STARTUPINFO si;
  PROCESS_INFORMATION pi;

  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESHOWWINDOW;
  si.wShowWindow = SW_SHOWNORMAL;
  ZeroMemory(&pi, sizeof(pi));

  if (CreateProcess(NULL, (LPWSTR)cmdLine, NULL, NULL, FALSE, 0,
                    NULL, NULL, &si, &pi)) {
    CloseHandle( pi.hProcess );
    CloseHandle( pi.hThread );
  }
  // we're not really in a position to do anything if the CreateProcess fails
  TerminateProcess(GetCurrentProcess(), 1);
#elif defined(XP_UNIX)
  if (!crashReporterAPIData->IsEmpty()) {
    // write out API data
    int fd = open(extraDataPath,
                  O_WRONLY | O_CREAT | O_TRUNC,
                  0666);

    if (fd != -1) {
      // not much we can do in case of error
      ssize_t ignored = write(fd, crashReporterAPIData->get(),
                              crashReporterAPIData->Length());
      ignored = write(fd, kCrashTimeParameter, kCrashTimeParameterLen);
      ignored = write(fd, crashTimeString, crashTimeStringLen);
      ignored = write(fd, "\n", 1);
      if (timeSinceLastCrash != 0) {
        ignored = write(fd, kTimeSinceLastCrashParameter,
                        kTimeSinceLastCrashParameterLen);
        ignored = write(fd, timeSinceLastCrashString,
                        timeSinceLastCrashStringLen);
        ignored = write(fd, "\n", 1);
      }
      close (fd);
    }
  }

  if (!doReport) {
    return returnValue;
  }

  pid_t pid = fork();

  if (pid == -1)
    return false;
  else if (pid == 0) {
    // need to clobber this, as libcurl might load NSS,
    // and we want it to load the system NSS.
    unsetenv("LD_LIBRARY_PATH");
    (void) execl(crashReporterPath,
                 crashReporterPath, minidumpPath, (char*)0);
    _exit(1);
  }
#endif

 return returnValue;
}

#ifdef XP_WIN
/**
 * Filters out floating point exceptions which are handled by nsSigHandlers.cpp
 * and should not be handled as crashes.
 */
static bool FPEFilter(void* context, EXCEPTION_POINTERS* exinfo,
                      MDRawAssertionInfo* assertion)
{
  PEXCEPTION_RECORD e = (PEXCEPTION_RECORD)exinfo->ExceptionRecord;
  switch (e->ExceptionCode) {
    case STATUS_FLOAT_DENORMAL_OPERAND:
    case STATUS_FLOAT_DIVIDE_BY_ZERO:
    case STATUS_FLOAT_INEXACT_RESULT:
    case STATUS_FLOAT_INVALID_OPERATION:
    case STATUS_FLOAT_OVERFLOW:
    case STATUS_FLOAT_STACK_CHECK:
    case STATUS_FLOAT_UNDERFLOW:
    case STATUS_FLOAT_MULTIPLE_FAULTS:
    case STATUS_FLOAT_MULTIPLE_TRAPS:
      return false; // Don't write minidump, continue exception search
  }
  return true;
}
#endif // XP_WIN

nsresult SetExceptionHandler(nsILocalFile* aXREDirectory,
                             bool force/*=false*/)
{
  nsresult rv;

  if (gExceptionHandler)
    return NS_ERROR_ALREADY_INITIALIZED;

  const char *envvar = PR_GetEnv("MOZ_CRASHREPORTER_DISABLE");
  if (envvar && *envvar && !force)
    return NS_OK;

  // this environment variable prevents us from launching
  // the crash reporter client
  envvar = PR_GetEnv("MOZ_CRASHREPORTER_NO_REPORT");
  if (envvar && *envvar)
    doReport = false;

  // allocate our strings
  crashReporterAPIData = new nsCString();
  NS_ENSURE_TRUE(crashReporterAPIData, NS_ERROR_OUT_OF_MEMORY);

  crashReporterAPIData_Hash =
    new nsDataHashtable<nsCStringHashKey,nsCString>();
  NS_ENSURE_TRUE(crashReporterAPIData_Hash, NS_ERROR_OUT_OF_MEMORY);

  rv = crashReporterAPIData_Hash->Init();
  NS_ENSURE_SUCCESS(rv, rv);

  notesField = new nsCString();
  NS_ENSURE_TRUE(notesField, NS_ERROR_OUT_OF_MEMORY);

  // locate crashreporter executable
  nsCOMPtr<nsIFile> exePath;
  rv = aXREDirectory->Clone(getter_AddRefs(exePath));
  NS_ENSURE_SUCCESS(rv, rv);

#if defined(XP_MACOSX)
  exePath->Append(NS_LITERAL_STRING("crashreporter.app"));
  exePath->Append(NS_LITERAL_STRING("Contents"));
  exePath->Append(NS_LITERAL_STRING("MacOS"));
#endif

  exePath->AppendNative(NS_LITERAL_CSTRING(CRASH_REPORTER_FILENAME));

#ifdef XP_WIN32
  nsString crashReporterPath_temp;
  exePath->GetPath(crashReporterPath_temp);

  crashReporterPath = ToNewUnicode(crashReporterPath_temp);
#else
  nsCString crashReporterPath_temp;
  exePath->GetNativePath(crashReporterPath_temp);

  crashReporterPath = ToNewCString(crashReporterPath_temp);
#endif

  // get temp path to use for minidump path
#if defined(XP_WIN32)
  nsString tempPath;

  // first figure out buffer size
  int pathLen = GetTempPath(0, NULL);
  if (pathLen == 0)
    return NS_ERROR_FAILURE;

  tempPath.SetLength(pathLen);
  GetTempPath(pathLen, (LPWSTR)tempPath.BeginWriting());
#elif defined(XP_MACOSX)
  nsCString tempPath;
  FSRef fsRef;
  OSErr err = FSFindFolder(kUserDomain, kTemporaryFolderType,
                           kCreateFolder, &fsRef);
  if (err != noErr)
    return NS_ERROR_FAILURE;

  char path[PATH_MAX];
  OSStatus status = FSRefMakePath(&fsRef, (UInt8*)path, PATH_MAX);
  if (status != noErr)
    return NS_ERROR_FAILURE;

  tempPath = path;

#elif defined(XP_UNIX)
  // we assume it's always /tmp on unix systems
  nsCString tempPath = NS_LITERAL_CSTRING("/tmp/");
#else
#error "Implement this for your platform"
#endif

  // now set the exception handler
  gExceptionHandler = new google_breakpad::
    ExceptionHandler(tempPath.get(),
#ifdef XP_WIN
                     FPEFilter,
#else
                     nsnull,
#endif
                     MinidumpCallback,
                     nsnull,
#if defined(XP_WIN32)
                     google_breakpad::ExceptionHandler::HANDLER_ALL);
#else
                     true);
#endif

  if (!gExceptionHandler)
    return NS_ERROR_OUT_OF_MEMORY;

  // store application start time
  char timeString[32];
  XP_TTOA(time(NULL), timeString, 10);
  AnnotateCrashReport(NS_LITERAL_CSTRING("StartupTime"),
                      nsDependentCString(timeString));

#if defined(XP_MACOSX)
  // On OS X, many testers like to see the OS crash reporting dialog
  // since it offers immediate stack traces.  We allow them to set
  // a default to pass exceptions to the OS handler.
  showOSCrashReporter = PassToOSCrashReporter();
#endif

  return NS_OK;
}

bool GetEnabled()
{
  return gExceptionHandler != nsnull;
}

bool GetMinidumpPath(nsAString& aPath)
{
  if (!gExceptionHandler)
    return false;

  aPath = CONVERT_XP_CHAR_TO_UTF16(gExceptionHandler->dump_path().c_str());
  return true;
}

nsresult SetMinidumpPath(const nsAString& aPath)
{
  if (!gExceptionHandler)
    return NS_ERROR_NOT_INITIALIZED;

  gExceptionHandler->set_dump_path(CONVERT_UTF16_TO_XP_CHAR(aPath).BeginReading());

  return NS_OK;
}

static nsresult
WriteDataToFile(nsIFile* aFile, const nsACString& data)
{
  nsCOMPtr<nsILocalFile> localFile = do_QueryInterface(aFile);
  NS_ENSURE_TRUE(localFile, NS_ERROR_FAILURE);

  PRFileDesc* fd;
  nsresult rv = localFile->OpenNSPRFileDesc(PR_WRONLY | PR_CREATE_FILE, 00600,
                                            &fd);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = NS_OK;
  if (PR_Write(fd, data.Data(), data.Length()) == -1) {
    rv = NS_ERROR_FAILURE;
  }
  PR_Close(fd);
  return rv;
}

static nsresult
GetFileContents(nsIFile* aFile, nsACString& data)
{
  nsCOMPtr<nsILocalFile> localFile = do_QueryInterface(aFile);
  NS_ENSURE_TRUE(localFile, NS_ERROR_FAILURE);

  PRFileDesc* fd;
  nsresult rv = localFile->OpenNSPRFileDesc(PR_RDONLY, 0, &fd);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = NS_OK;
  PRInt32 filesize = PR_Available(fd);
  if (filesize <= 0) {
    rv = NS_ERROR_FILE_NOT_FOUND;
  }
  else {
    data.SetLength(filesize);
    if (PR_Read(fd, data.BeginWriting(), filesize) == -1) {
      rv = NS_ERROR_FAILURE;
    }
  }
  PR_Close(fd);
  return rv;
}

// Function typedef for initializing a piece of data that we
// don't already have.
typedef nsresult (*InitDataFunc)(nsACString&);

// Attempt to read aFile's contents into aContents, if aFile
// does not exist, create it and initialize its contents
// by calling aInitFunc for the data.
static nsresult
GetOrInit(nsIFile* aDir, const nsACString& filename,
          nsACString& aContents, InitDataFunc aInitFunc)
{
  PRBool exists;

  nsCOMPtr<nsIFile> dataFile;
  nsresult rv = aDir->Clone(getter_AddRefs(dataFile));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = dataFile->AppendNative(filename);
  NS_ENSURE_SUCCESS(rv, rv);

  rv = dataFile->Exists(&exists);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!exists) {
    if (aInitFunc) {
      // get the initial value and write it to the file
      rv = aInitFunc(aContents);
      NS_ENSURE_SUCCESS(rv, rv);
      rv = WriteDataToFile(dataFile, aContents);
    }
    else {
      // didn't pass in an init func
      rv = NS_ERROR_FAILURE;
    }
  }
  else {
    // just get the file's contents
    rv = GetFileContents(dataFile, aContents);
  }

  return rv;
}

// Init the "install time" data.  We're taking an easy way out here
// and just setting this to "the time when this version was first run".
static nsresult
InitInstallTime(nsACString& aInstallTime)
{
  time_t t = time(NULL);
  char buf[16];
  sprintf(buf, "%ld", t);
  aInstallTime = buf;

  return NS_OK;
}

// Annotate the crash report with a Unique User ID and time
// since install.  Also do some prep work for recording
// time since last crash, which must be calculated at
// crash time.
// If any piece of data doesn't exist, initialize it first.
nsresult SetupExtraData(nsILocalFile* aAppDataDirectory,
                        const nsACString& aBuildID)
{
  nsCOMPtr<nsIFile> dataDirectory;
  nsresult rv = aAppDataDirectory->Clone(getter_AddRefs(dataDirectory));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = dataDirectory->AppendNative(NS_LITERAL_CSTRING("Crash Reports"));
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool exists;
  rv = dataDirectory->Exists(&exists);
  NS_ENSURE_SUCCESS(rv, rv);

  if (!exists) {
    rv = dataDirectory->Create(nsIFile::DIRECTORY_TYPE, 0700);
    NS_ENSURE_SUCCESS(rv, rv);
  }

#if defined(XP_WIN32)
  nsAutoString dataDirEnv(NS_LITERAL_STRING("MOZ_CRASHREPORTER_DATA_DIRECTORY="));

  nsAutoString dataDirectoryPath;
  rv = dataDirectory->GetPath(dataDirectoryPath);
  NS_ENSURE_SUCCESS(rv, rv);

  dataDirEnv.Append(dataDirectoryPath);

  _wputenv(dataDirEnv.get());
#else
  // Save this path in the environment for the crash reporter application.
  nsCAutoString dataDirEnv("MOZ_CRASHREPORTER_DATA_DIRECTORY=");

  nsCAutoString dataDirectoryPath;
  rv = dataDirectory->GetNativePath(dataDirectoryPath);
  NS_ENSURE_SUCCESS(rv, rv);

  dataDirEnv.Append(dataDirectoryPath);

  char* env = ToNewCString(dataDirEnv);
  NS_ENSURE_TRUE(env, NS_ERROR_OUT_OF_MEMORY);

  PR_SetEnv(env);
#endif

  nsCAutoString data;
  if(NS_SUCCEEDED(GetOrInit(dataDirectory,
                            NS_LITERAL_CSTRING("InstallTime") + aBuildID,
                            data, InitInstallTime)))
    AnnotateCrashReport(NS_LITERAL_CSTRING("InstallTime"), data);

  // this is a little different, since we can't init it with anything,
  // since it's stored at crash time, and we can't annotate the
  // crash report with the stored value, since we really want
  // (now - LastCrash), so we just get a value if it exists,
  // and store it in a time_t value.
  if(NS_SUCCEEDED(GetOrInit(dataDirectory, NS_LITERAL_CSTRING("LastCrash"),
                            data, NULL))) {
    lastCrashTime = (time_t)atol(data.get());
  }

  // not really the best place to init this, but I have the path I need here
  nsCOMPtr<nsIFile> lastCrashFile;
  rv = dataDirectory->Clone(getter_AddRefs(lastCrashFile));
  NS_ENSURE_SUCCESS(rv, rv);

  rv = lastCrashFile->AppendNative(NS_LITERAL_CSTRING("LastCrash"));
  NS_ENSURE_SUCCESS(rv, rv);
  memset(lastCrashTimeFilename, 0, sizeof(lastCrashTimeFilename));

#if defined(XP_WIN32)
  nsAutoString filename;
  rv = lastCrashFile->GetPath(filename);
  NS_ENSURE_SUCCESS(rv, rv);

  if (filename.Length() < XP_PATH_MAX)
    wcsncpy(lastCrashTimeFilename, filename.get(), filename.Length());
#else
  nsCAutoString filename;
  rv = lastCrashFile->GetNativePath(filename);
  NS_ENSURE_SUCCESS(rv, rv);

  if (filename.Length() < XP_PATH_MAX)
    strncpy(lastCrashTimeFilename, filename.get(), filename.Length());
#endif

  return NS_OK;
}

static void OOPDeinit();

nsresult UnsetExceptionHandler()
{
  delete gExceptionHandler;

  // do this here in the unlikely case that we succeeded in allocating
  // our strings but failed to allocate gExceptionHandler.
  if (crashReporterAPIData_Hash) {
    delete crashReporterAPIData_Hash;
    crashReporterAPIData_Hash = nsnull;
  }

  if (crashReporterAPIData) {
    delete crashReporterAPIData;
    crashReporterAPIData = nsnull;
  }

  if (notesField) {
    delete notesField;
    notesField = nsnull;
  }

  if (crashReporterPath) {
    NS_Free(crashReporterPath);
    crashReporterPath = nsnull;
  }

  if (!gExceptionHandler)
    return NS_ERROR_NOT_INITIALIZED;

  gExceptionHandler = nsnull;

#ifdef MOZ_IPC
  OOPDeinit();
#endif

  return NS_OK;
}

static void ReplaceChar(nsCString& str, const nsACString& character,
                        const nsACString& replacement)
{
  nsCString::const_iterator start, end;

  str.BeginReading(start);
  str.EndReading(end);

  while (FindInReadable(character, start, end)) {
    PRInt32 pos = end.size_backward();
    str.Replace(pos - 1, 1, replacement);

    str.BeginReading(start);
    start.advance(pos + replacement.Length() - 1);
    str.EndReading(end);
  }
}

static PRBool DoFindInReadable(const nsACString& str, const nsACString& value)
{
  nsACString::const_iterator start, end;
  str.BeginReading(start);
  str.EndReading(end);

  return FindInReadable(value, start, end);
}

static PLDHashOperator EnumerateEntries(const nsACString& key,
                                        nsCString entry,
                                        void* userData)
{
  crashReporterAPIData->Append(key + NS_LITERAL_CSTRING("=") + entry +
                               NS_LITERAL_CSTRING("\n"));
  return PL_DHASH_NEXT;
}

nsresult AnnotateCrashReport(const nsACString& key, const nsACString& data)
{
  if (!gExceptionHandler)
    return NS_ERROR_NOT_INITIALIZED;

  if (DoFindInReadable(key, NS_LITERAL_CSTRING("=")) ||
      DoFindInReadable(key, NS_LITERAL_CSTRING("\n")))
    return NS_ERROR_INVALID_ARG;

  if (DoFindInReadable(data, NS_LITERAL_CSTRING("\0")))
    return NS_ERROR_INVALID_ARG;

  nsCString escapedData(data);

  // escape backslashes
  ReplaceChar(escapedData, NS_LITERAL_CSTRING("\\"),
              NS_LITERAL_CSTRING("\\\\"));
  // escape newlines
  ReplaceChar(escapedData, NS_LITERAL_CSTRING("\n"),
              NS_LITERAL_CSTRING("\\n"));

  nsresult rv = crashReporterAPIData_Hash->Put(key, escapedData);
  NS_ENSURE_SUCCESS(rv, rv);

  // now rebuild the file contents
  crashReporterAPIData->Truncate(0);
  crashReporterAPIData_Hash->EnumerateRead(EnumerateEntries,
                                           crashReporterAPIData);

  return NS_OK;
}

nsresult AppendAppNotesToCrashReport(const nsACString& data)
{
  if (!gExceptionHandler)
    return NS_ERROR_NOT_INITIALIZED;

  if (DoFindInReadable(data, NS_LITERAL_CSTRING("\0")))
    return NS_ERROR_INVALID_ARG;

  notesField->Append(data);
  return AnnotateCrashReport(NS_LITERAL_CSTRING("Notes"), *notesField);
}

// Returns true if found, false if not found.
bool GetAnnotation(const nsACString& key, nsACString& data)
{
  if (!gExceptionHandler)
    return false;

  nsCAutoString entry;
  if (!crashReporterAPIData_Hash->Get(key, &entry))
    return false;

  data = entry;
  return true;
}

bool GetServerURL(nsACString& aServerURL)
{
  if (!gExceptionHandler)
    return false;

  return GetAnnotation(NS_LITERAL_CSTRING("ServerURL"), aServerURL);
}

nsresult SetServerURL(const nsACString& aServerURL)
{
  // store server URL with the API data
  // the client knows to handle this specially
  return AnnotateCrashReport(NS_LITERAL_CSTRING("ServerURL"),
                             aServerURL);
}

nsresult
SetRestartArgs(int argc, char** argv)
{
  if (!gExceptionHandler)
    return NS_OK;

  int i;
  nsCAutoString envVar;
  char *env;
  for (i = 0; i < argc; i++) {
    envVar = "MOZ_CRASHREPORTER_RESTART_ARG_";
    envVar.AppendInt(i);
    envVar += "=";
#if defined(XP_UNIX) && !defined(XP_MACOSX)
    // we'd like to run the script around the binary
    // instead of the binary itself, so remove the -bin
    // if it exists on the first argument
    int arg_len = 0;
    if (i == 0 &&
        (arg_len = strlen(argv[i])) > 4 &&
        strcmp(argv[i] + arg_len - 4, "-bin") == 0) {
      envVar.Append(argv[i], arg_len - 4);
    } else
#endif
    {
      envVar += argv[i];
    }

    // PR_SetEnv() wants the string to be available for the lifetime
    // of the app, so dup it here
    env = ToNewCString(envVar);
    if (!env)
      return NS_ERROR_OUT_OF_MEMORY;

    PR_SetEnv(env);
  }

  // make sure the arg list is terminated
  envVar = "MOZ_CRASHREPORTER_RESTART_ARG_";
  envVar.AppendInt(i);
  envVar += "=";

  // PR_SetEnv() wants the string to be available for the lifetime
  // of the app, so dup it here
  env = ToNewCString(envVar);
  if (!env)
    return NS_ERROR_OUT_OF_MEMORY;

  PR_SetEnv(env);

  // make sure we save the info in XUL_APP_FILE for the reporter
  const char *appfile = PR_GetEnv("XUL_APP_FILE");
  if (appfile && *appfile) {
    envVar = "MOZ_CRASHREPORTER_RESTART_XUL_APP_FILE=";
    envVar += appfile;
    env = ToNewCString(envVar);
    PR_SetEnv(env);
  }

  return NS_OK;
}

#ifdef XP_WIN32
nsresult WriteMinidumpForException(EXCEPTION_POINTERS* aExceptionInfo)
{
  if (!gExceptionHandler)
    return NS_ERROR_NOT_INITIALIZED;

  return gExceptionHandler->WriteMinidumpForException(aExceptionInfo) ? NS_OK : NS_ERROR_FAILURE;
}
#endif

#ifdef XP_MACOSX
nsresult AppendObjCExceptionInfoToAppNotes(void *inException)
{
  nsCAutoString excString;
  GetObjCExceptionInfo(inException, excString);
  AppendAppNotesToCrashReport(excString);
  return NS_OK;
}
#endif

/*
 * Combined code to get/set the crash reporter submission pref on
 * different platforms.
 */
static nsresult PrefSubmitReports(PRBool* aSubmitReports, bool writePref)
{
  nsresult rv;
#if defined(XP_WIN32)
  /*
   * NOTE! This needs to stay in sync with the preference checking code
   *       in toolkit/crashreporter/client/crashreporter_win.cpp
   */
  nsCOMPtr<nsIXULAppInfo> appinfo =
    do_GetService("@mozilla.org/xre/app-info;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCAutoString appVendor, appName;
  rv = appinfo->GetVendor(appVendor);
  NS_ENSURE_SUCCESS(rv, rv);
  rv = appinfo->GetName(appName);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsIWindowsRegKey> regKey
    (do_CreateInstance("@mozilla.org/windows-registry-key;1", &rv));
  NS_ENSURE_SUCCESS(rv, rv);

  nsCAutoString regPath;

  regPath.AppendLiteral("Software\\");
  if(!appVendor.IsEmpty()) {
    regPath.Append(appVendor);
    regPath.AppendLiteral("\\");
  }
  regPath.Append(appName);
  regPath.AppendLiteral("\\Crash Reporter");

  // If we're saving the pref value, just write it to ROOT_KEY_CURRENT_USER
  // and we're done.
  if (writePref) {
    rv = regKey->Open(nsIWindowsRegKey::ROOT_KEY_CURRENT_USER,
                      NS_ConvertUTF8toUTF16(regPath),
                      nsIWindowsRegKey::ACCESS_SET_VALUE);
    NS_ENSURE_SUCCESS(rv, rv);

    PRUint32 value = *aSubmitReports ? 1 : 0;
    rv = regKey->WriteIntValue(NS_LITERAL_STRING("SubmitCrashReport"), value);
    regKey->Close();
    return rv;
  }

  // We're reading the pref value, so we need to first look under
  // ROOT_KEY_LOCAL_MACHINE to see if it's set there, and then fall back to
  // ROOT_KEY_CURRENT_USER. If it's not set in either place, the pref defaults
  // to "true".
  PRUint32 value;
  rv = regKey->Open(nsIWindowsRegKey::ROOT_KEY_LOCAL_MACHINE,
                    NS_ConvertUTF8toUTF16(regPath),
                    nsIWindowsRegKey::ACCESS_QUERY_VALUE);
  if (NS_SUCCEEDED(rv)) {
    rv = regKey->ReadIntValue(NS_LITERAL_STRING("SubmitCrashReport"), &value);
    regKey->Close();
    if (NS_SUCCEEDED(rv)) {
      *aSubmitReports = !!value;
      return NS_OK;
    }
  }

  rv = regKey->Open(nsIWindowsRegKey::ROOT_KEY_CURRENT_USER,
                    NS_ConvertUTF8toUTF16(regPath),
                    nsIWindowsRegKey::ACCESS_QUERY_VALUE);
  if (NS_FAILED(rv)) {
    *aSubmitReports = PR_TRUE;
    return NS_OK;
  }
  
  rv = regKey->ReadIntValue(NS_LITERAL_STRING("SubmitCrashReport"), &value);
  // default to true on failure
  if (NS_FAILED(rv)) {
    value = 1;
    rv = NS_OK;
  }
  regKey->Close();

  *aSubmitReports = !!value;
  return NS_OK;
#elif defined(XP_MACOSX)
  // TODO: Implement for OSX (bug 542379)
  return NS_ERROR_NOT_IMPLEMENTED;
#elif defined(XP_UNIX)
  /*
   * NOTE! This needs to stay in sync with the preference checking code
   *       in toolkit/crashreporter/client/crashreporter_linux.cpp
   */
  nsCOMPtr<nsIFile> reporterINI;
  rv = NS_GetSpecialDirectory("UAppData", getter_AddRefs(reporterINI));
  NS_ENSURE_SUCCESS(rv, rv);
  reporterINI->AppendNative(NS_LITERAL_CSTRING("Crash Reports"));
  reporterINI->AppendNative(NS_LITERAL_CSTRING("crashreporter.ini"));

  PRBool exists;
  rv = reporterINI->Exists(&exists);
  NS_ENSURE_SUCCESS(rv, rv);
  if (!exists) {
    if (!writePref) {
        // If reading the pref, default to true if .ini doesn't exist.
        *aSubmitReports = PR_TRUE;
        return NS_OK;
    }
    // Create the file so the INI processor can write to it.
    rv = reporterINI->Create(nsIFile::NORMAL_FILE_TYPE, 0600);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  nsCOMPtr<nsIINIParserFactory> iniFactory =
    do_GetService("@mozilla.org/xpcom/ini-processor-factory;1", &rv);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMPtr<nsILocalFile> localFile = do_QueryInterface(reporterINI);
  NS_ENSURE_TRUE(localFile, NS_ERROR_FAILURE);
  nsCOMPtr<nsIINIParser> iniParser;
  rv = iniFactory->CreateINIParser(localFile,
                                   getter_AddRefs(iniParser));
  NS_ENSURE_SUCCESS(rv, rv);

  // If we're writing the pref, just set and we're done.
  if (writePref) {
    nsCOMPtr<nsIINIParserWriter> iniWriter = do_QueryInterface(iniParser);
    NS_ENSURE_TRUE(iniWriter, NS_ERROR_FAILURE);

    rv = iniWriter->SetString(NS_LITERAL_CSTRING("Crash Reporter"),
                              NS_LITERAL_CSTRING("SubmitReport"),
                              *aSubmitReports ?  NS_LITERAL_CSTRING("1") :
                                                 NS_LITERAL_CSTRING("0"));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = iniWriter->WriteFile(NULL);
    return rv;
  }
  
  nsCAutoString submitReportValue;
  rv = iniParser->GetString(NS_LITERAL_CSTRING("Crash Reporter"),
                            NS_LITERAL_CSTRING("SubmitReport"),
                            submitReportValue);

  // Default to "true" if the pref can't be found.
  if (NS_FAILED(rv))
    *aSubmitReports = PR_TRUE;
  else if (submitReportValue.EqualsASCII("0"))
    *aSubmitReports = PR_FALSE;
  else
    *aSubmitReports = PR_TRUE;

  return NS_OK;
#else
  return NS_ERROR_NOT_IMPLEMENTED;
#endif
}

nsresult GetSubmitReports(PRBool* aSubmitReports)
{
    return PrefSubmitReports(aSubmitReports, false);
}

nsresult SetSubmitReports(PRBool aSubmitReports)
{
    return PrefSubmitReports(&aSubmitReports, true);
}


#if defined(MOZ_IPC)
static PLDHashOperator EnumerateChildAnnotations(const nsACString& key,
                                                 nsCString entry,
                                                 void* userData)
{
  // blacklist of entries from the parent process that we don't want to
  // submit with the child process
  static const char* kBlacklist[] = {
    "FramePoisonBase",
    "FramePoisonSize",
    "StartupTime",
    "URL"
  };
  static const int kBlacklistLength =
    sizeof(kBlacklist) / sizeof(kBlacklist[0]);

  // skip entries in the blacklist
  for (int i = 0; i < kBlacklistLength; i++) {
    if (key.EqualsASCII(kBlacklist[i]))
      return PL_DHASH_NEXT;
  }

  nsIFileOutputStream* extraStream =
    reinterpret_cast<nsIFileOutputStream*>(userData);
  PRUint32 written;
  extraStream->Write(key.BeginReading(), key.Length(), &written);
  extraStream->Write("=", 1, &written);
  extraStream->Write(entry.BeginReading(), entry.Length(), &written);
  extraStream->Write("\n", 1, &written);
  return PL_DHASH_NEXT;
}

static bool
MoveToPending(nsIFile* dumpFile, nsIFile* extraFile)
{
  nsCOMPtr<nsIProperties> dirSvc
    = do_GetService(NS_DIRECTORY_SERVICE_CONTRACTID);
  if (!dirSvc)
    return false;
  nsCOMPtr<nsILocalFile> pendingDir;
  if (NS_FAILED(dirSvc->Get("UAppData",
                            NS_GET_IID(nsILocalFile),
                            getter_AddRefs(pendingDir))) ||
      NS_FAILED(pendingDir->Append(NS_LITERAL_STRING("Crash Reports"))) ||
      NS_FAILED(pendingDir->Append(NS_LITERAL_STRING("pending"))))
      return false;

  return NS_FAILED(dumpFile->MoveTo(pendingDir, EmptyString())) ||
    NS_FAILED(extraFile->MoveTo(pendingDir, EmptyString()));
}

static void
OnChildProcessDumpRequested(void* aContext,
                            const ClientInfo* aClientInfo,
#if defined(XP_WIN)
                            const std::wstring*
#else
                            const std::string*
#endif
                              aFilePath)
{
  nsCOMPtr<nsILocalFile> lf;
  PRUint32 pid;

#ifdef XP_WIN
  NS_NewLocalFile(nsDependentString(aFilePath->c_str()), PR_FALSE,
                  getter_AddRefs(lf));
  pid = aClientInfo->pid();
#else
  NS_NewNativeLocalFile(nsDependentCString(aFilePath->c_str()), PR_FALSE,
                        getter_AddRefs(lf));
  pid = aClientInfo->pid_;
#endif

  // Get an .extra file with the same base name as the .dmp file
  nsCOMPtr<nsIFile> extraFile;
  nsresult rv = lf->Clone(getter_AddRefs(extraFile));
  if (NS_FAILED(rv))
    return;

  nsAutoString leafName;
  rv = extraFile->GetLeafName(leafName);
  if (NS_FAILED(rv))
    return;

  leafName.Replace(leafName.Length() - 3, 3,
                   NS_LITERAL_STRING("extra"));
  rv = extraFile->SetLeafName(leafName);
  if (NS_FAILED(rv))
    return;

  // Now write out the annotations to it
  nsCOMPtr<nsIFileOutputStream> stream =
    do_CreateInstance("@mozilla.org/network/file-output-stream;1");
  rv = stream->Init(extraFile, -1, 0600, 0);
  if (NS_FAILED(rv))
    return;
  crashReporterAPIData_Hash->EnumerateRead(EnumerateChildAnnotations,
                                           stream.get());
  // Add CrashTime to extra data
  time_t crashTime = time(NULL);
  char crashTimeString[32];
  XP_TTOA(crashTime, crashTimeString, 10);

  PRUint32 written;
  stream->Write(kCrashTimeParameter, kCrashTimeParameterLen, &written);
  stream->Write(crashTimeString, strlen(crashTimeString), &written);
  stream->Write("\n", 1, &written);
  stream->Close();

  bool doReport = true;
  char* e = getenv("MOZ_CRASHREPORTER_NO_REPORT");
  if (e && *e)
    doReport = false;

  if (doReport)
    MoveToPending(lf, extraFile);

  {
    MutexAutoLock lock(*dumpMapLock);
    pidToMinidump->Put(pid, lf);
  }
}

static bool
OOPInitialized()
{
  return crashServer != NULL;
}

static void
OOPInit()
{
  NS_ABORT_IF_FALSE(!OOPInitialized(),
                    "OOP crash reporter initialized more than once!");
  NS_ABORT_IF_FALSE(gExceptionHandler != NULL,
                    "attempt to initialize OOP crash reporter before in-process crashreporter!");

#if defined(XP_WIN)
  childCrashNotifyPipe =
    PR_smprintf("\\\\.\\pipe\\gecko-crash-server-pipe.%i",
                static_cast<int>(::GetCurrentProcessId()));

  const std::wstring dumpPath = gExceptionHandler->dump_path();
  crashServer = new CrashGenerationServer(
    NS_ConvertASCIItoUTF16(childCrashNotifyPipe).get(),
    NULL,                       // default security attributes
    NULL, NULL,                 // we don't care about process connect here
    OnChildProcessDumpRequested, NULL,
    NULL, NULL,                 // we don't care about process exit here
    true,                       // automatically generate dumps
    &dumpPath);

#elif defined(XP_LINUX)
  if (!CrashGenerationServer::CreateReportChannel(&serverSocketFd,
                                                  &clientSocketFd))
    NS_RUNTIMEABORT("can't create crash reporter socketpair()");

  const std::string dumpPath = gExceptionHandler->dump_path();
  crashServer = new CrashGenerationServer(
    serverSocketFd,
    OnChildProcessDumpRequested, NULL,
    NULL, NULL,                 // we don't care about process exit here
    true,                       // automatically generate dumps
    &dumpPath);
#endif

  if (!crashServer->Start())
    NS_RUNTIMEABORT("can't start crash reporter server()");

  pidToMinidump = new ChildMinidumpMap();
  pidToMinidump->Init();

  dumpMapLock = new Mutex("CrashReporter::dumpMapLock");
}

static void
OOPDeinit()
{
  if (!OOPInitialized()) {
    NS_WARNING("OOPDeinit() without successful OOPInit()");
    return;
  }

  delete crashServer;
  crashServer = NULL;

  delete dumpMapLock;
  dumpMapLock = NULL;

  delete pidToMinidump;
  pidToMinidump = NULL;

#if defined(XP_WIN)
  PR_Free(childCrashNotifyPipe);
  childCrashNotifyPipe = NULL;
#endif
}

#if defined(XP_WIN)
// Parent-side API for children
const char*
GetChildNotificationPipe()
{
  if (!GetEnabled())
    return kNullNotifyPipe;

  if (!OOPInitialized())
    OOPInit();

  return childCrashNotifyPipe;
}

// Child-side API
bool
SetRemoteExceptionHandler(const nsACString& crashPipe)
{
  // crash reporting is disabled
  if (crashPipe.Equals(kNullNotifyPipe))
    return true;

  NS_ABORT_IF_FALSE(!gExceptionHandler, "crash client already init'd");

  gExceptionHandler = new google_breakpad::
    ExceptionHandler(L"",
                     NULL,    // no filter callback
                     NULL,    // no minidump callback
                     NULL,    // no callback context
                     google_breakpad::ExceptionHandler::HANDLER_ALL,
                     MiniDumpNormal,
                     NS_ConvertASCIItoUTF16(crashPipe).BeginReading(),
                     NULL);

  // we either do remote or nothing, no fallback to regular crash reporting
  return gExceptionHandler->IsOutOfProcess();
}

//--------------------------------------------------
#elif defined(XP_UNIX)

// Parent-side API for children
bool
CreateNotificationPipeForChild(int* childCrashFd, int* childCrashRemapFd)
{
  if (!GetEnabled()) {
    *childCrashFd = -1;
    *childCrashRemapFd = -1;
    return true;
  }

  if (!OOPInitialized())
    OOPInit();

  *childCrashFd = clientSocketFd;
  *childCrashRemapFd = kMagicChildCrashReportFd;

  return true;
}

// Child-side API
bool
SetRemoteExceptionHandler()
{
  NS_ABORT_IF_FALSE(!gExceptionHandler, "crash client already init'd");

  gExceptionHandler = new google_breakpad::
    ExceptionHandler("",
                     NULL,    // no filter callback
                     NULL,    // no minidump callback
                     NULL,    // no callback context
                     true,    // install signal handlers
                     kMagicChildCrashReportFd);

  // we either do remote or nothing, no fallback to regular crash reporting
  return gExceptionHandler->IsOutOfProcess();
}

#endif  // XP_WIN


bool
GetMinidumpForChild(PRUint32 childPid, nsIFile** dump)
{
  if (!GetEnabled())
    return false;

  MutexAutoLock lock(*dumpMapLock);
  return pidToMinidump->Get(childPid, dump);
}

bool
UnsetRemoteExceptionHandler()
{
  delete gExceptionHandler;
  gExceptionHandler = NULL;
  return true;
}

#endif  // MOZ_IPC

} // namespace CrashReporter
