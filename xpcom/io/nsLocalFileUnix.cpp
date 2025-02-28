/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */ 
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
 * The Original Code is Mozilla Communicator client code, released
 * March 31, 1998.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998-1999
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Mike Shaver            <shaver@mozilla.org>
 *   Christopher Blizzard   <blizzard@mozilla.org>
 *   Jason Eager            <jce2@po.cwru.edu>
 *   Stuart Parmenter       <pavlov@netscape.com>
 *   Brendan Eich           <brendan@mozilla.org>
 *   Pete Collins           <petejc@mozdev.org>
 *   Paul Ashford           <arougthopher@lizardland.net>
 *   Fredrik Holmqvist      <thesuckiestemail@yahoo.se>
 *   Josh Aas               <josh@mozilla.com>
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

/**
 * Implementation of nsIFile for ``Unixy'' systems.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <utime.h>
#include <dirent.h>
#include <ctype.h>
#include <locale.h>
#ifdef XP_BEOS
    #include <Path.h>
    #include <Entry.h>
    #include <Roster.h>
#endif
#if defined(VMS)
    #include <fabdef.h>
#endif

#if defined(HAVE_SYS_QUOTA_H)
#include <sys/sysmacros.h>
#include <sys/quota.h>
#endif

#include "nsDirectoryServiceDefs.h"
#include "nsCRT.h"
#include "nsCOMPtr.h"
#include "nsMemory.h"
#include "nsIFile.h"
#include "nsString.h"
#include "nsReadableUtils.h"
#include "nsLocalFile.h"
#include "nsIComponentManager.h"
#include "nsXPIDLString.h"
#include "prproces.h"
#include "nsIDirectoryEnumerator.h"
#include "nsISimpleEnumerator.h"
#include "nsITimelineService.h"

#ifdef MOZ_WIDGET_GTK2
#include "nsIGIOService.h"
#include "nsIGnomeVFSService.h"
#endif

#if (MOZ_PLATFORM_MAEMO == 5)
#include <glib.h>
#include <hildon-uri.h>
#include <hildon-mime.h>
#include <libosso.h>
#endif

#include "nsNativeCharsetUtils.h"
#include "nsTraceRefcntImpl.h"

#define ENSURE_STAT_CACHE()                     \
    PR_BEGIN_MACRO                              \
        if (!FillStatCache())                   \
             return NSRESULT_FOR_ERRNO();       \
    PR_END_MACRO

#define CHECK_mPath()                           \
    PR_BEGIN_MACRO                              \
        if (mPath.IsEmpty())                    \
            return NS_ERROR_NOT_INITIALIZED;    \
    PR_END_MACRO

/* directory enumerator */
class NS_COM
nsDirEnumeratorUnix : public nsISimpleEnumerator,
                      public nsIDirectoryEnumerator
{
    public:
    nsDirEnumeratorUnix();

    // nsISupports interface
    NS_DECL_ISUPPORTS

    // nsISimpleEnumerator interface
    NS_DECL_NSISIMPLEENUMERATOR

    // nsIDirectoryEnumerator interface
    NS_DECL_NSIDIRECTORYENUMERATOR

    NS_IMETHOD Init(nsLocalFile *parent, PRBool ignored);

    private:
    ~nsDirEnumeratorUnix();

    protected:
    NS_IMETHOD GetNextEntry();

    DIR           *mDir;
    struct dirent *mEntry;
    nsCString      mParentPath;
};

nsDirEnumeratorUnix::nsDirEnumeratorUnix() :
                         mDir(nsnull), 
                         mEntry(nsnull)
{
}

nsDirEnumeratorUnix::~nsDirEnumeratorUnix()
{
    Close();
}

NS_IMPL_ISUPPORTS2(nsDirEnumeratorUnix, nsISimpleEnumerator, nsIDirectoryEnumerator)

NS_IMETHODIMP
nsDirEnumeratorUnix::Init(nsLocalFile *parent, PRBool resolveSymlinks /*ignored*/)
{
    nsCAutoString dirPath;
    if (NS_FAILED(parent->GetNativePath(dirPath)) ||
        dirPath.IsEmpty()) {
        return NS_ERROR_FILE_INVALID_PATH;
    }

    if (NS_FAILED(parent->GetNativePath(mParentPath)))
        return NS_ERROR_FAILURE;

    mDir = opendir(dirPath.get());
    if (!mDir)
        return NSRESULT_FOR_ERRNO();
    return GetNextEntry();
}

NS_IMETHODIMP
nsDirEnumeratorUnix::HasMoreElements(PRBool *result)
{
    *result = mDir && mEntry;
    if (!*result)
        Close();
    return NS_OK;
}

NS_IMETHODIMP
nsDirEnumeratorUnix::GetNext(nsISupports **_retval)
{
    nsCOMPtr<nsIFile> file;
    nsresult rv = GetNextFile(getter_AddRefs(file));
    if (NS_FAILED(rv))
        return rv;
    NS_IF_ADDREF(*_retval = file);
    return NS_OK;
}

NS_IMETHODIMP
nsDirEnumeratorUnix::GetNextEntry()
{
    do {
        errno = 0;
        mEntry = readdir(mDir);

        // end of dir or error
        if (!mEntry)
            return NSRESULT_FOR_ERRNO();

        // keep going past "." and ".."
    } while (mEntry->d_name[0] == '.'     &&
            (mEntry->d_name[1] == '\0'    ||   // .\0
            (mEntry->d_name[1] == '.'     &&
            mEntry->d_name[2] == '\0')));      // ..\0
    return NS_OK;
}

NS_IMETHODIMP
nsDirEnumeratorUnix::GetNextFile(nsIFile **_retval)
{
    nsresult rv;
    if (!mDir || !mEntry) {
        *_retval = nsnull;
        return NS_OK;
    }

    nsCOMPtr<nsILocalFile> file = new nsLocalFile();
    if (!file)
        return NS_ERROR_OUT_OF_MEMORY;

    if (NS_FAILED(rv = file->InitWithNativePath(mParentPath)) ||
        NS_FAILED(rv = file->AppendNative(nsDependentCString(mEntry->d_name))))
        return rv;

    *_retval = file;
    NS_ADDREF(*_retval);
    return GetNextEntry();
}

NS_IMETHODIMP 
nsDirEnumeratorUnix::Close()
{
    if (mDir) {
        closedir(mDir);
        mDir = nsnull;
    }
    return NS_OK;
}

nsLocalFile::nsLocalFile()
{
}

nsLocalFile::nsLocalFile(const nsLocalFile& other)
  : mPath(other.mPath)
{
}

NS_IMPL_THREADSAFE_ISUPPORTS3(nsLocalFile,
                              nsIFile,
                              nsILocalFile,
                              nsIHashable)

nsresult
nsLocalFile::nsLocalFileConstructor(nsISupports *outer, 
                                    const nsIID &aIID,
                                    void **aInstancePtr)
{
    NS_ENSURE_ARG_POINTER(aInstancePtr);
    NS_ENSURE_NO_AGGREGATION(outer);

    *aInstancePtr = nsnull;

    nsCOMPtr<nsIFile> inst = new nsLocalFile();
    if (!inst)
        return NS_ERROR_OUT_OF_MEMORY;
    return inst->QueryInterface(aIID, aInstancePtr);
}

PRBool 
nsLocalFile::FillStatCache() {
    if (STAT(mPath.get(), &mCachedStat) == -1) {
        // try lstat it may be a symlink
        if (LSTAT(mPath.get(), &mCachedStat) == -1) {
            return PR_FALSE;
        }
    }
    return PR_TRUE;
}

NS_IMETHODIMP
nsLocalFile::Clone(nsIFile **file)
{
    // Just copy-construct ourselves
    *file = new nsLocalFile(*this);
    if (!*file)
      return NS_ERROR_OUT_OF_MEMORY;

    NS_ADDREF(*file);
    
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::InitWithNativePath(const nsACString &filePath)
{
    if (Substring(filePath, 0, 2).EqualsLiteral("~/")) {
        nsCOMPtr<nsIFile> homeDir;
        nsCAutoString homePath;
        if (NS_FAILED(NS_GetSpecialDirectory(NS_OS_HOME_DIR,
                                             getter_AddRefs(homeDir))) ||
            NS_FAILED(homeDir->GetNativePath(homePath))) {
            return NS_ERROR_FAILURE;
        }
        
        mPath = homePath + Substring(filePath, 1, filePath.Length() - 1);
    } else {
        if (filePath.IsEmpty() || filePath.First() != '/')
            return NS_ERROR_FILE_UNRECOGNIZED_PATH;
        mPath = filePath;
    }

    // trim off trailing slashes
    ssize_t len = mPath.Length();
    while ((len > 1) && (mPath[len - 1] == '/'))
        --len;
    mPath.SetLength(len);

    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::CreateAllAncestors(PRUint32 permissions)
{
    // <jband> I promise to play nice
    char *buffer = mPath.BeginWriting(),
         *slashp = buffer;

#ifdef DEBUG_NSIFILE
    fprintf(stderr, "nsIFile: before: %s\n", buffer);
#endif

    while ((slashp = strchr(slashp + 1, '/'))) {
        /*
         * Sequences of '/' are equivalent to a single '/'.
         */
        if (slashp[1] == '/')
            continue;

        /*
         * If the path has a trailing slash, don't make the last component,
         * because we'll get EEXIST in Create when we try to build the final
         * component again, and it's easier to condition the logic here than
         * there.
         */
        if (slashp[1] == '\0')
            break;

        /* Temporarily NUL-terminate here */
        *slashp = '\0';
#ifdef DEBUG_NSIFILE
        fprintf(stderr, "nsIFile: mkdir(\"%s\")\n", buffer);
#endif
        int mkdir_result = mkdir(buffer, permissions);
        int mkdir_errno  = errno;
        if (mkdir_result == -1) {
            /*
             * Always set |errno| to EEXIST if the dir already exists
             * (we have to do this here since the errno value is not consistent
             * in all cases - various reasons like different platform,
             * automounter-controlled dir, etc. can affect it (see bug 125489
             * for details)).
             */
            if (access(buffer, F_OK) == 0) {
                mkdir_errno = EEXIST;
            }
        }

        /* Put the / back before we (maybe) return */
        *slashp = '/';

        /*
         * We could get EEXIST for an existing file -- not directory --
         * with the name of one of our ancestors, but that's OK: we'll get
         * ENOTDIR when we try to make the next component in the path,
         * either here on back in Create, and error out appropriately.
         */
        if (mkdir_result == -1 && mkdir_errno != EEXIST)
            return nsresultForErrno(mkdir_errno);
    }

#ifdef DEBUG_NSIFILE
    fprintf(stderr, "nsIFile: after: %s\n", buffer);
#endif

    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::OpenNSPRFileDesc(PRInt32 flags, PRInt32 mode, PRFileDesc **_retval)
{
    *_retval = PR_Open(mPath.get(), flags, mode);
    if (! *_retval)
        return NS_ErrorAccordingToNSPR();

    if (flags & DELETE_ON_CLOSE) {
        PR_Delete(mPath.get());
    }

    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::OpenANSIFileDesc(const char *mode, FILE **_retval)
{
    *_retval = fopen(mPath.get(), mode);
    if (! *_retval)
        return NS_ERROR_FAILURE;

    return NS_OK;
}

static int
do_create(const char *path, PRIntn flags, mode_t mode, PRFileDesc **_retval)
{
    *_retval = PR_Open(path, flags, mode);
    return *_retval ? 0 : -1;
}

static int
do_mkdir(const char *path, PRIntn flags, mode_t mode, PRFileDesc **_retval)
{
    *_retval = nsnull;
    return mkdir(path, mode);
}

nsresult
nsLocalFile::CreateAndKeepOpen(PRUint32 type, PRIntn flags,
                               PRUint32 permissions, PRFileDesc **_retval)
{
    if (type != NORMAL_FILE_TYPE && type != DIRECTORY_TYPE)
        return NS_ERROR_FILE_UNKNOWN_TYPE;

    int result;
    int (*createFunc)(const char *, PRIntn, mode_t, PRFileDesc **) =
        (type == NORMAL_FILE_TYPE) ? do_create : do_mkdir;

    result = createFunc(mPath.get(), flags, permissions, _retval);
    if (result == -1 && errno == ENOENT) {
        /*
         * If we failed because of missing ancestor components, try to create
         * them and then retry the original creation.
         *
         * Ancestor directories get the same permissions as the file we're
         * creating, with the X bit set for each of (user,group,other) with
         * an R bit in the original permissions.    If you want to do anything
         * fancy like setgid or sticky bits, do it by hand.
         */
        int dirperm = permissions;
        if (permissions & S_IRUSR)
            dirperm |= S_IXUSR;
        if (permissions & S_IRGRP)
            dirperm |= S_IXGRP;
        if (permissions & S_IROTH)
            dirperm |= S_IXOTH;

#ifdef DEBUG_NSIFILE
        fprintf(stderr, "nsIFile: perm = %o, dirperm = %o\n", permissions,
                dirperm);
#endif

        if (NS_FAILED(CreateAllAncestors(dirperm)))
            return NS_ERROR_FAILURE;

#ifdef DEBUG_NSIFILE
        fprintf(stderr, "nsIFile: Create(\"%s\") again\n", mPath.get());
#endif
        result = createFunc(mPath.get(), flags, permissions, _retval);
    }
    return NSRESULT_FOR_RETURN(result);
}

NS_IMETHODIMP
nsLocalFile::Create(PRUint32 type, PRUint32 permissions)
{
    PRFileDesc *junk = nsnull;
    nsresult rv = CreateAndKeepOpen(type,
                                    PR_WRONLY | PR_CREATE_FILE | PR_TRUNCATE |
                                    PR_EXCL,
                                    permissions,
                                    &junk);
    if (junk)
        PR_Close(junk);
    return rv;
}

NS_IMETHODIMP
nsLocalFile::AppendNative(const nsACString &fragment)
{
    if (fragment.IsEmpty())
        return NS_OK;

    // only one component of path can be appended
    nsACString::const_iterator begin, end;
    if (FindCharInReadable('/', fragment.BeginReading(begin),
                                fragment.EndReading(end)))
        return NS_ERROR_FILE_UNRECOGNIZED_PATH;

    return AppendRelativeNativePath(fragment);
}

NS_IMETHODIMP
nsLocalFile::AppendRelativeNativePath(const nsACString &fragment)
{
    if (fragment.IsEmpty())
        return NS_OK;

    // No leading '/' 
    if (fragment.First() == '/')
        return NS_ERROR_FILE_UNRECOGNIZED_PATH;

    if (mPath.EqualsLiteral("/"))
        mPath.Append(fragment);
    else
        mPath.Append(NS_LITERAL_CSTRING("/") + fragment);

    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::Normalize()
{
    char    resolved_path[PATH_MAX] = "";
    char *resolved_path_ptr = nsnull;

#ifdef XP_BEOS
    BEntry be_e(mPath.get(), true);
    BPath be_p;
    status_t err;
    if ((err = be_e.GetPath(&be_p)) == B_OK) {
        resolved_path_ptr = (char *)be_p.Path();
        PL_strncpyz(resolved_path, resolved_path_ptr, PATH_MAX - 1);
    }
#else
    resolved_path_ptr = realpath(mPath.get(), resolved_path);
#endif
    // if there is an error, the return is null.
    if (!resolved_path_ptr)
        return NSRESULT_FOR_ERRNO();

    mPath = resolved_path;
    return NS_OK;
}

void
nsLocalFile::LocateNativeLeafName(nsACString::const_iterator &begin, 
                                  nsACString::const_iterator &end)
{
    // XXX perhaps we should cache this??

    mPath.BeginReading(begin);
    mPath.EndReading(end);
    
    nsACString::const_iterator it = end;
    nsACString::const_iterator stop = begin;
    --stop;
    while (--it != stop) {
        if (*it == '/') {
            begin = ++it;
            return;
        }
    }
    // else, the entire path is the leaf name (which means this
    // isn't an absolute path... unexpected??)
}

NS_IMETHODIMP
nsLocalFile::GetNativeLeafName(nsACString &aLeafName)
{
    nsACString::const_iterator begin, end;
    LocateNativeLeafName(begin, end);
    aLeafName = Substring(begin, end);
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::SetNativeLeafName(const nsACString &aLeafName)
{
    nsACString::const_iterator begin, end;
    LocateNativeLeafName(begin, end);
    mPath.Replace(begin.get() - mPath.get(), Distance(begin, end), aLeafName);
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::GetNativePath(nsACString &_retval)
{
    _retval = mPath;
    return NS_OK;
}

nsresult
nsLocalFile::GetNativeTargetPathName(nsIFile *newParent, 
                                     const nsACString &newName,
                                     nsACString &_retval)
{
    nsresult rv;
    nsCOMPtr<nsIFile> oldParent;

    if (!newParent) {
        if (NS_FAILED(rv = GetParent(getter_AddRefs(oldParent))))
            return rv;
        newParent = oldParent.get();
    } else {
        // check to see if our target directory exists
        PRBool targetExists;
        if (NS_FAILED(rv = newParent->Exists(&targetExists)))
            return rv;

        if (!targetExists) {
            // XXX create the new directory with some permissions
            rv = newParent->Create(DIRECTORY_TYPE, 0755);
            if (NS_FAILED(rv))
                return rv;
        } else {
            // make sure that the target is actually a directory
            PRBool targetIsDirectory;
            if (NS_FAILED(rv = newParent->IsDirectory(&targetIsDirectory)))
                return rv;
            if (!targetIsDirectory)
                return NS_ERROR_FILE_DESTINATION_NOT_DIR;
        }
    }

    nsACString::const_iterator nameBegin, nameEnd;
    if (!newName.IsEmpty()) {
        newName.BeginReading(nameBegin);
        newName.EndReading(nameEnd);
    }
    else
        LocateNativeLeafName(nameBegin, nameEnd);

    nsCAutoString dirName;
    if (NS_FAILED(rv = newParent->GetNativePath(dirName)))
        return rv;

    _retval = dirName
            + NS_LITERAL_CSTRING("/")
            + Substring(nameBegin, nameEnd);
    return NS_OK;
}

nsresult
nsLocalFile::CopyDirectoryTo(nsIFile *newParent)
{
    nsresult rv;
    /*
     * dirCheck is used for various boolean test results such as from Equals,
     * Exists, isDir, etc.
     */
    PRBool dirCheck, isSymlink;
    PRUint32 oldPerms;

    if (NS_FAILED(rv = IsDirectory(&dirCheck)))
        return rv;
    if (!dirCheck)
        return CopyToNative(newParent, EmptyCString());
    
    if (NS_FAILED(rv = Equals(newParent, &dirCheck)))
        return rv;
    if (dirCheck) { 
        // can't copy dir to itself
        return NS_ERROR_INVALID_ARG;
    }
    
    if (NS_FAILED(rv = newParent->Exists(&dirCheck))) 
        return rv;
    // get the dirs old permissions
    if (NS_FAILED(rv = GetPermissions(&oldPerms)))
        return rv;
    if (!dirCheck) {
        if (NS_FAILED(rv = newParent->Create(DIRECTORY_TYPE, oldPerms)))
            return rv;
    } else {    // dir exists lets try to use leaf
        nsCAutoString leafName;
        if (NS_FAILED(rv = GetNativeLeafName(leafName)))
            return rv;
        if (NS_FAILED(rv = newParent->AppendNative(leafName)))
            return rv;
        if (NS_FAILED(rv = newParent->Exists(&dirCheck)))
            return rv;
        if (dirCheck) 
            return NS_ERROR_FILE_ALREADY_EXISTS; // dest exists
        if (NS_FAILED(rv = newParent->Create(DIRECTORY_TYPE, oldPerms)))
            return rv;
    }

    nsCOMPtr<nsISimpleEnumerator> dirIterator;
    if (NS_FAILED(rv = GetDirectoryEntries(getter_AddRefs(dirIterator))))
        return rv;

    PRBool hasMore = PR_FALSE;
    while (dirIterator->HasMoreElements(&hasMore), hasMore) {
        nsCOMPtr<nsIFile> entry;
        rv = dirIterator->GetNext((nsISupports**)getter_AddRefs(entry));
        if (NS_FAILED(rv)) 
            continue;
        if (NS_FAILED(rv = entry->IsSymlink(&isSymlink)))
            return rv;
        if (NS_FAILED(rv = entry->IsDirectory(&dirCheck)))
            return rv;
        if (dirCheck && !isSymlink) {
            nsCOMPtr<nsIFile> destClone;
            rv = newParent->Clone(getter_AddRefs(destClone));
            if (NS_SUCCEEDED(rv)) {
                nsCOMPtr<nsILocalFile> newDir(do_QueryInterface(destClone));
                if (NS_FAILED(rv = entry->CopyToNative(newDir, EmptyCString()))) {
#ifdef DEBUG
                    nsresult rv2;
                    nsCAutoString pathName;
                    if (NS_FAILED(rv2 = entry->GetNativePath(pathName)))
                        return rv2;
                    printf("Operation not supported: %s\n", pathName.get());
#endif
                    if (rv == NS_ERROR_OUT_OF_MEMORY) 
                        return rv;
                    continue;
                }
            }
        } else {
            if (NS_FAILED(rv = entry->CopyToNative(newParent, EmptyCString()))) {
#ifdef DEBUG
                nsresult rv2;
                nsCAutoString pathName;
                if (NS_FAILED(rv2 = entry->GetNativePath(pathName)))
                    return rv2;
                printf("Operation not supported: %s\n", pathName.get());
#endif
                if (rv == NS_ERROR_OUT_OF_MEMORY) 
                    return rv;
                continue;
            }
        }
    }
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::CopyToNative(nsIFile *newParent, const nsACString &newName)
{
    nsresult rv;
    // check to make sure that this has been initialized properly
    CHECK_mPath();

    // we copy the parent here so 'newParent' remains immutable
    nsCOMPtr <nsIFile> workParent;
    if (newParent) {
        if (NS_FAILED(rv = newParent->Clone(getter_AddRefs(workParent))))
            return rv;
    } else {
        if (NS_FAILED(rv = GetParent(getter_AddRefs(workParent))))
            return rv;
    }
    
    // check to see if we are a directory or if we are a file
    PRBool isDirectory;
    if (NS_FAILED(rv = IsDirectory(&isDirectory)))
        return rv;

    nsCAutoString newPathName;
    if (isDirectory) {
        if (!newName.IsEmpty()) {
            if (NS_FAILED(rv = workParent->AppendNative(newName)))
                return rv;
        } else {
            if (NS_FAILED(rv = GetNativeLeafName(newPathName)))
                return rv;
            if (NS_FAILED(rv = workParent->AppendNative(newPathName)))
                return rv;
        }
        if (NS_FAILED(rv = CopyDirectoryTo(workParent)))
            return rv;
    } else {
        rv = GetNativeTargetPathName(workParent, newName, newPathName);
        if (NS_FAILED(rv)) 
            return rv;

#ifdef DEBUG_blizzard
        printf("nsLocalFile::CopyTo() %s -> %s\n", mPath.get(), newPathName.get());
#endif

        // actually create the file.
        nsLocalFile *newFile = new nsLocalFile();
        if (!newFile)
            return NS_ERROR_OUT_OF_MEMORY;

        nsCOMPtr<nsILocalFile> fileRef(newFile); // release on exit

        rv = newFile->InitWithNativePath(newPathName);
        if (NS_FAILED(rv))
            return rv;

        // get the old permissions
        PRUint32 myPerms;
        GetPermissions(&myPerms);

        // Create the new file with the old file's permissions, even if write
        // permission is missing.  We can't create with write permission and
        // then change back to myPerm on all filesystems (FAT on Linux, e.g.).
        // But we can write to a read-only file on all Unix filesystems if we
        // open it successfully for writing.

        PRFileDesc *newFD;
        rv = newFile->CreateAndKeepOpen(NORMAL_FILE_TYPE,
                                        PR_WRONLY|PR_CREATE_FILE|PR_TRUNCATE,
                                        myPerms,
                                        &newFD);
        if (NS_FAILED(rv))
            return rv;

        // open the old file, too
        PRBool specialFile;
        if (NS_FAILED(rv = IsSpecial(&specialFile))) {
            PR_Close(newFD);
            return rv;
        }
        if (specialFile) {
#ifdef DEBUG
            printf("Operation not supported: %s\n", mPath.get());
#endif
            // make sure to clean up properly
            PR_Close(newFD);
            return NS_OK;
        }
               
        PRFileDesc *oldFD;
        rv = OpenNSPRFileDesc(PR_RDONLY, myPerms, &oldFD);
        if (NS_FAILED(rv)) {
            // make sure to clean up properly
            PR_Close(newFD);
            return rv;
        }

#ifdef DEBUG_blizzard
        PRInt32 totalRead = 0;
        PRInt32 totalWritten = 0;
#endif
        char buf[BUFSIZ];
        PRInt32 bytesRead;
        
        while ((bytesRead = PR_Read(oldFD, buf, BUFSIZ)) > 0) {
#ifdef DEBUG_blizzard
            totalRead += bytesRead;
#endif

            // PR_Write promises never to do a short write
            PRInt32 bytesWritten = PR_Write(newFD, buf, bytesRead);
            if (bytesWritten < 0) {
                bytesRead = -1;
                break;
            }
            NS_ASSERTION(bytesWritten == bytesRead, "short PR_Write?");

#ifdef DEBUG_blizzard
            totalWritten += bytesWritten;
#endif
        }

#ifdef DEBUG_blizzard
        printf("read %d bytes, wrote %d bytes\n",
                 totalRead, totalWritten);
#endif

        // close the files
        PR_Close(newFD);
        PR_Close(oldFD);

        // check for read (or write) error after cleaning up
        if (bytesRead < 0) 
            return NS_ERROR_OUT_OF_MEMORY;
    }
    return rv;
}

NS_IMETHODIMP
nsLocalFile::CopyToFollowingLinksNative(nsIFile *newParent, const nsACString &newName)
{
    return CopyToNative(newParent, newName);
}

NS_IMETHODIMP
nsLocalFile::MoveToNative(nsIFile *newParent, const nsACString &newName)
{
    nsresult rv;

    // check to make sure that this has been initialized properly
    CHECK_mPath();

    // check to make sure that we have a new parent
    nsCAutoString newPathName;
    rv = GetNativeTargetPathName(newParent, newName, newPathName);
    if (NS_FAILED(rv))
        return rv;

    // try for atomic rename, falling back to copy/delete
    if (rename(mPath.get(), newPathName.get()) < 0) {
#ifdef VMS
        if (errno == EXDEV || errno == ENXIO) {
#else
        if (errno == EXDEV) {
#endif
            rv = CopyToNative(newParent, newName);
            if (NS_SUCCEEDED(rv))
                rv = Remove(PR_TRUE);
        } else {
            rv = NSRESULT_FOR_ERRNO();
        }
    }

    if (NS_SUCCEEDED(rv)) {
        // Adjust this
        mPath = newPathName;
    }
    return rv;
}

NS_IMETHODIMP
nsLocalFile::Remove(PRBool recursive)
{
    CHECK_mPath();
    ENSURE_STAT_CACHE();

    PRBool isSymLink;

    nsresult rv = IsSymlink(&isSymLink);
    if (NS_FAILED(rv))
        return rv;

    if (isSymLink || !S_ISDIR(mCachedStat.st_mode))
        return NSRESULT_FOR_RETURN(unlink(mPath.get()));

    if (recursive) {
        nsDirEnumeratorUnix *dir = new nsDirEnumeratorUnix();
        if (!dir)
            return NS_ERROR_OUT_OF_MEMORY;

        nsCOMPtr<nsISimpleEnumerator> dirRef(dir); // release on exit

        rv = dir->Init(this, PR_FALSE);
        if (NS_FAILED(rv))
            return rv;

        PRBool more;
        while (dir->HasMoreElements(&more), more) {
            nsCOMPtr<nsISupports> item;
            rv = dir->GetNext(getter_AddRefs(item));
            if (NS_FAILED(rv))
                return NS_ERROR_FAILURE;

            nsCOMPtr<nsIFile> file = do_QueryInterface(item, &rv);
            if (NS_FAILED(rv))
                return NS_ERROR_FAILURE;
            if (NS_FAILED(rv = file->Remove(recursive)))
                return rv;
        }
    }

    return NSRESULT_FOR_RETURN(rmdir(mPath.get()));
}

NS_IMETHODIMP
nsLocalFile::GetLastModifiedTime(PRInt64 *aLastModTime)
{
    CHECK_mPath();
    NS_ENSURE_ARG(aLastModTime);

    PRFileInfo64 info;
    if (PR_GetFileInfo64(mPath.get(), &info) != PR_SUCCESS)
        return NSRESULT_FOR_ERRNO();
    PRInt64 modTime = PRInt64(info.modifyTime);
    if (modTime == 0)
        *aLastModTime = 0;
    else
        *aLastModTime = modTime / PRInt64(PR_USEC_PER_MSEC);

    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::SetLastModifiedTime(PRInt64 aLastModTime)
{
    CHECK_mPath();

    int result;
    if (aLastModTime != 0) {
        ENSURE_STAT_CACHE();
        struct utimbuf ut;
        ut.actime = mCachedStat.st_atime;

        // convert milliseconds to seconds since the unix epoch
        ut.modtime = (time_t)(PRFloat64(aLastModTime) / PR_MSEC_PER_SEC);
        result = utime(mPath.get(), &ut);
    } else {
        result = utime(mPath.get(), nsnull);
    }
    return NSRESULT_FOR_RETURN(result);
}

NS_IMETHODIMP
nsLocalFile::GetLastModifiedTimeOfLink(PRInt64 *aLastModTimeOfLink)
{
    CHECK_mPath();
    NS_ENSURE_ARG(aLastModTimeOfLink);

    struct STAT sbuf;
    if (LSTAT(mPath.get(), &sbuf) == -1)
        return NSRESULT_FOR_ERRNO();
    *aLastModTimeOfLink = PRInt64(sbuf.st_mtime) * PRInt64(PR_MSEC_PER_SEC);

    return NS_OK;
}

/*
 * utime(2) may or may not dereference symlinks, joy.
 */
NS_IMETHODIMP
nsLocalFile::SetLastModifiedTimeOfLink(PRInt64 aLastModTimeOfLink)
{
    return SetLastModifiedTime(aLastModTimeOfLink);
}

/*
 * Only send back permissions bits: maybe we want to send back the whole
 * mode_t to permit checks against other file types?
 */

#define NORMALIZE_PERMS(mode)    ((mode)& (S_IRWXU | S_IRWXG | S_IRWXO))

NS_IMETHODIMP
nsLocalFile::GetPermissions(PRUint32 *aPermissions)
{
    NS_ENSURE_ARG(aPermissions);
    ENSURE_STAT_CACHE();
    *aPermissions = NORMALIZE_PERMS(mCachedStat.st_mode);
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::GetPermissionsOfLink(PRUint32 *aPermissionsOfLink)
{
    CHECK_mPath();
    NS_ENSURE_ARG(aPermissionsOfLink);

    struct STAT sbuf;
    if (LSTAT(mPath.get(), &sbuf) == -1)
        return NSRESULT_FOR_ERRNO();
    *aPermissionsOfLink = NORMALIZE_PERMS(sbuf.st_mode);
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::SetPermissions(PRUint32 aPermissions)
{
    CHECK_mPath();

    /*
     * Race condition here: we should use fchmod instead, there's no way to 
     * guarantee the name still refers to the same file.
     */
    if (chmod(mPath.get(), aPermissions) < 0)
        return NSRESULT_FOR_ERRNO();
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::SetPermissionsOfLink(PRUint32 aPermissions)
{
    // There isn't a consistent mechanism for doing this on UNIX platforms. We
    // might want to carefully implement this in the future though.
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsLocalFile::GetFileSize(PRInt64 *aFileSize)
{
    NS_ENSURE_ARG_POINTER(aFileSize);
    *aFileSize = 0;
    ENSURE_STAT_CACHE();

#if defined(VMS)
    /* Only two record formats can report correct file content size */
    if ((mCachedStat.st_fab_rfm != FAB$C_STMLF) &&
        (mCachedStat.st_fab_rfm != FAB$C_STMCR)) {
        return NS_ERROR_FAILURE;
    }
#endif

    if (!S_ISDIR(mCachedStat.st_mode)) {
        *aFileSize = (PRInt64)mCachedStat.st_size;
    }
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::SetFileSize(PRInt64 aFileSize)
{
    CHECK_mPath();

#ifdef HAVE_TRUNCATE64
    if (truncate64(mPath.get(), (off64_t)aFileSize) == -1)
        return NSRESULT_FOR_ERRNO();
#else
    off_t size = (off_t)aFileSize;
    if (truncate(mPath.get(), size) == -1)
        return NSRESULT_FOR_ERRNO();
#endif
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::GetFileSizeOfLink(PRInt64 *aFileSize)
{
    CHECK_mPath();
    NS_ENSURE_ARG(aFileSize);

    struct STAT sbuf;
    if (LSTAT(mPath.get(), &sbuf) == -1)
        return NSRESULT_FOR_ERRNO();

    *aFileSize = (PRInt64)sbuf.st_size;
    return NS_OK;
}

/*
 * Searches /proc/self/mountinfo for given device (Major:Minor), 
 * returns exported name from /dev
 *
 * Fails when /proc/self/mountinfo or diven device don't exist.
 */
static PRBool
GetDeviceName(int deviceMajor, int deviceMinor, nsACString &deviceName)
{
    PRBool ret = false;
    
    const int kMountInfoLineLength = 200;
    const int kMountInfoDevPosition = 6;

    char mountinfo_line[kMountInfoLineLength];
    char device_num[kMountInfoLineLength];
    
    snprintf(device_num,kMountInfoLineLength,"%d:%d", deviceMajor, deviceMinor);
    
    FILE *f = fopen("/proc/self/mountinfo","rt");
    if(!f)
        return ret;

    // Expects /proc/self/mountinfo in format:
    // 'ID ID major:minor root mountpoint flags - type devicename flags'
    while(fgets(mountinfo_line,kMountInfoLineLength,f)) {
        char *p_dev = strstr(mountinfo_line,device_num);
    
        int i;
        for(i = 0; i < kMountInfoDevPosition && p_dev != NULL; i++) {
            p_dev = strchr(p_dev,' ');
            if(p_dev)
              p_dev++;
        }
    
        if(p_dev) {
            char *p_dev_end = strchr(p_dev,' ');
            if(p_dev_end) {
                *p_dev_end = '\0';
                deviceName.Assign(p_dev);
                ret = true;
                break;
            }
        }
    }
    
    fclose(f);
    return ret; 
}

NS_IMETHODIMP
nsLocalFile::GetDiskSpaceAvailable(PRInt64 *aDiskSpaceAvailable)
{
    NS_ENSURE_ARG_POINTER(aDiskSpaceAvailable);

    // These systems have the operations necessary to check disk space.

#ifdef STATFS

    // check to make sure that mPath is properly initialized
    CHECK_mPath();

    struct STATFS fs_buf;

    /* 
     * Members of the STATFS struct that you should know about:
     * f_bsize = block size on disk.
     * f_bavail = number of free blocks available to a non-superuser.
     * f_bfree = number of total free blocks in file system.
     */

    if (STATFS(mPath.get(), &fs_buf) < 0) {
        // The call to STATFS failed.
#ifdef DEBUG
        printf("ERROR: GetDiskSpaceAvailable: STATFS call FAILED. \n");
#endif
        return NS_ERROR_FAILURE;
    }
#ifdef DEBUG_DISK_SPACE
    printf("DiskSpaceAvailable: %d bytes\n",
         fs_buf.f_bsize * (fs_buf.f_bavail - 1));
#endif

    /* 
     * The number of bytes free == The number of free blocks available to
     * a non-superuser, minus one as a fudge factor, multiplied by the size
     * of the aforementioned blocks.
     */
    *aDiskSpaceAvailable = (PRInt64)fs_buf.f_bsize * (fs_buf.f_bavail - 1);

#if defined(HAVE_SYS_STAT_H) || defined(HAVE_SYS_QUOTA_H)

    if(!FillStatCache()) {
        // Return available size from statfs
        return NS_OK;
    }

    nsCString deviceName;
    if(!GetDeviceName(major(mCachedStat.st_dev), minor(mCachedStat.st_dev), deviceName)) {
        return NS_OK;
    }

    struct dqblk dq;
    if(!quotactl(QCMD(Q_GETQUOTA, USRQUOTA), deviceName.get(), getuid(), (caddr_t)&dq)) {
        PRInt64 QuotaSpaceAvailable = PRInt64(fs_buf.f_bsize * dq.dqb_bhardlimit);
        if(QuotaSpaceAvailable < *aDiskSpaceAvailable) {
            *aDiskSpaceAvailable = QuotaSpaceAvailable;
        }
    }
#endif

    return NS_OK;

#else
    /*
     * This platform doesn't have statfs or statvfs.  I'm sure that there's
     * a way to check for free disk space on platforms that don't have statfs
     * (I'm SURE they have df, for example).
     *
     * Until we figure out how to do that, lets be honest and say that this
     * command isn't implemented properly for these platforms yet.
     */
#ifdef DEBUG
    printf("ERROR: GetDiskSpaceAvailable: Not implemented for plaforms without statfs.\n");
#endif
    return NS_ERROR_NOT_IMPLEMENTED;

#endif /* STATFS */

}

NS_IMETHODIMP
nsLocalFile::GetParent(nsIFile **aParent)
{
    CHECK_mPath();
    NS_ENSURE_ARG_POINTER(aParent);
    *aParent       = nsnull;

    // if '/' we are at the top of the volume, return null
    if (mPath.Equals("/"))
        return  NS_OK;
 
    // <brendan, after jband> I promise to play nice
    char *buffer   = mPath.BeginWriting(),
         *slashp   = buffer;

    // find the last significant slash in buffer
    slashp = strrchr(buffer, '/');
    NS_ASSERTION(slashp, "non-canonical mPath?");
    if (!slashp)
        return NS_ERROR_FILE_INVALID_PATH;

    // for the case where we are at '/'
    if (slashp == buffer)
        slashp++;

    // temporarily terminate buffer at the last significant slash
    char c = *slashp;
    *slashp = '\0';

    nsCOMPtr<nsILocalFile> localFile;
    nsresult rv = NS_NewNativeLocalFile(nsDependentCString(buffer), PR_TRUE,
                                        getter_AddRefs(localFile));

    // make buffer whole again
    *slashp = c;

    if (NS_SUCCEEDED(rv) && localFile)
        rv = CallQueryInterface(localFile, aParent);
    return rv;
}

/*
 * The results of Exists, isWritable and isReadable are not cached.
 */


#if defined(XP_BEOS) || defined(SOLARIS)
// access() is buggy in BeOS POSIX implementation, at least for BFS, using stat() instead
// see bug 169506, https://bugzilla.mozilla.org/show_bug.cgi?id=169506
// access() problem also exists in Solaris POSIX implementation
// see bug 351595, https://bugzilla.mozilla.org/show_bug.cgi?id=351595
NS_IMETHODIMP
nsLocalFile::Exists(PRBool *_retval)
{
    CHECK_mPath();
    NS_ENSURE_ARG_POINTER(_retval);
    struct STAT buf;

    *_retval = (STAT(mPath.get(), &buf) == 0);
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::IsWritable(PRBool *_retval)
{
    CHECK_mPath();
    NS_ENSURE_ARG_POINTER(_retval);
    struct STAT buf;

    *_retval = (STAT(mPath.get(), &buf) == 0);
    if (*_retval || errno == EACCES) {
        *_retval = *_retval && (buf.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH ));
        return NS_OK;
    }
    return NSRESULT_FOR_ERRNO();
}

NS_IMETHODIMP
nsLocalFile::IsReadable(PRBool *_retval)
{
    CHECK_mPath();
    NS_ENSURE_ARG_POINTER(_retval);
    struct STAT buf;

    *_retval = (STAT(mPath.get(), &buf) == 0);
    if (*_retval || errno == EACCES) {
        *_retval = *_retval && (buf.st_mode & (S_IRUSR | S_IRGRP | S_IROTH ));
        return NS_OK;
    }
    return NSRESULT_FOR_ERRNO();
}

NS_IMETHODIMP
nsLocalFile::IsExecutable(PRBool *_retval)
{
    CHECK_mPath();
    NS_ENSURE_ARG_POINTER(_retval);
    struct STAT buf;

    *_retval = (STAT(mPath.get(), &buf) == 0);
    if (*_retval || errno == EACCES) {
        *_retval = *_retval && (buf.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH ));
        return NS_OK;
    }
    return NSRESULT_FOR_ERRNO();
}
#else

NS_IMETHODIMP
nsLocalFile::Exists(PRBool *_retval)
{
    CHECK_mPath();
    NS_ENSURE_ARG_POINTER(_retval);

    *_retval = (access(mPath.get(), F_OK) == 0);
    return NS_OK;
}


NS_IMETHODIMP
nsLocalFile::IsWritable(PRBool *_retval)
{
    CHECK_mPath();
    NS_ENSURE_ARG_POINTER(_retval);

    *_retval = (access(mPath.get(), W_OK) == 0);
    if (*_retval || errno == EACCES)
        return NS_OK;
    return NSRESULT_FOR_ERRNO();
}

NS_IMETHODIMP
nsLocalFile::IsReadable(PRBool *_retval)
{
    CHECK_mPath();
    NS_ENSURE_ARG_POINTER(_retval);

    *_retval = (access(mPath.get(), R_OK) == 0);
    if (*_retval || errno == EACCES)
        return NS_OK;
    return NSRESULT_FOR_ERRNO();
}

NS_IMETHODIMP
nsLocalFile::IsExecutable(PRBool *_retval)
{
    CHECK_mPath();
    NS_ENSURE_ARG_POINTER(_retval);

    *_retval = (access(mPath.get(), X_OK) == 0);
    if (*_retval || errno == EACCES)
        return NS_OK;
    return NSRESULT_FOR_ERRNO();
}
#endif
NS_IMETHODIMP
nsLocalFile::IsDirectory(PRBool *_retval)
{
    NS_ENSURE_ARG_POINTER(_retval);
    *_retval = PR_FALSE;
    ENSURE_STAT_CACHE();
    *_retval = S_ISDIR(mCachedStat.st_mode);
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::IsFile(PRBool *_retval)
{
    NS_ENSURE_ARG_POINTER(_retval);
    *_retval = PR_FALSE;
    ENSURE_STAT_CACHE();
    *_retval = S_ISREG(mCachedStat.st_mode);
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::IsHidden(PRBool *_retval)
{
    NS_ENSURE_ARG_POINTER(_retval);
    nsACString::const_iterator begin, end;
    LocateNativeLeafName(begin, end);
    *_retval = (*begin == '.');
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::IsSymlink(PRBool *_retval)
{
    NS_ENSURE_ARG_POINTER(_retval);
    CHECK_mPath();

    struct STAT symStat;
    if (LSTAT(mPath.get(), &symStat) == -1)
        return NSRESULT_FOR_ERRNO();
    *_retval=S_ISLNK(symStat.st_mode);
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::IsSpecial(PRBool *_retval)
{
    NS_ENSURE_ARG_POINTER(_retval);
    ENSURE_STAT_CACHE();
    *_retval = S_ISCHR(mCachedStat.st_mode)      ||
                 S_ISBLK(mCachedStat.st_mode)    ||
#ifdef S_ISSOCK
                 S_ISSOCK(mCachedStat.st_mode)   ||
#endif
                 S_ISFIFO(mCachedStat.st_mode);

    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::Equals(nsIFile *inFile, PRBool *_retval)
{
    NS_ENSURE_ARG(inFile);
    NS_ENSURE_ARG_POINTER(_retval);
    *_retval = PR_FALSE;

    nsCAutoString inPath;
    nsresult rv = inFile->GetNativePath(inPath);
    if (NS_FAILED(rv))
        return rv;

    // We don't need to worry about "/foo/" vs. "/foo" here
    // because trailing slashes are stripped on init.
    *_retval = !strcmp(inPath.get(), mPath.get());
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::Contains(nsIFile *inFile, PRBool recur, PRBool *_retval)
{
    CHECK_mPath();
    NS_ENSURE_ARG(inFile);
    NS_ENSURE_ARG_POINTER(_retval);

    nsCAutoString inPath;
    nsresult rv;

    if (NS_FAILED(rv = inFile->GetNativePath(inPath)))
        return rv;

    *_retval = PR_FALSE;

    ssize_t len = mPath.Length();
    if (strncmp(mPath.get(), inPath.get(), len) == 0) {
        // Now make sure that the |inFile|'s path has a separator at len,
        // which implies that it has more components after len.
        if (inPath[len] == '/')
            *_retval = PR_TRUE;
    }

    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::GetNativeTarget(nsACString &_retval)
{
    CHECK_mPath();
    _retval.Truncate();

    struct STAT symStat;
    if (LSTAT(mPath.get(), &symStat) == -1)
        return NSRESULT_FOR_ERRNO();

    if (!S_ISLNK(symStat.st_mode))
        return NS_ERROR_FILE_INVALID_PATH;

    PRInt32 size = (PRInt32)symStat.st_size;
    char *target = (char *)nsMemory::Alloc(size + 1);
    if (!target)
        return NS_ERROR_OUT_OF_MEMORY;

    if (readlink(mPath.get(), target, (size_t)size) < 0) {
        nsMemory::Free(target);
        return NSRESULT_FOR_ERRNO();
    }
    target[size] = '\0';

    nsresult rv = NS_OK;
    nsCOMPtr<nsIFile> self(this);
    PRInt32 maxLinks = 40;
    while (PR_TRUE) {
        if (maxLinks-- == 0) {
            rv = NS_ERROR_FILE_UNRESOLVABLE_SYMLINK;
            break;
        }

        if (target[0] != '/') {
            nsCOMPtr<nsIFile> parent;
            if (NS_FAILED(rv = self->GetParent(getter_AddRefs(parent))))
                break;
            nsCOMPtr<nsILocalFile> localFile(do_QueryInterface(parent, &rv));
            if (NS_FAILED(rv))
                break;
            if (NS_FAILED(rv = localFile->AppendRelativeNativePath(nsDependentCString(target))))
                break;
            if (NS_FAILED(rv = localFile->GetNativePath(_retval)))
                break;
            self = parent;
        } else {
            _retval = target;
        }

        const nsPromiseFlatCString &flatRetval = PromiseFlatCString(_retval);

        // Any failure in testing the current target we'll just interpret
        // as having reached our destiny.
        if (LSTAT(flatRetval.get(), &symStat) == -1)
            break;

        // And of course we're done if it isn't a symlink.
        if (!S_ISLNK(symStat.st_mode))
            break;

        PRInt32 newSize = (PRInt32)symStat.st_size;
        if (newSize > size) {
            char *newTarget = (char *)nsMemory::Realloc(target, newSize + 1);
            if (!newTarget) {
                rv = NS_ERROR_OUT_OF_MEMORY;
                break;
            }
            target = newTarget;
            size = newSize;
        }

        PRInt32 linkLen = readlink(flatRetval.get(), target, size);
        if (linkLen == -1) {
            rv = NSRESULT_FOR_ERRNO();
            break;
        }
        target[linkLen] = '\0';
    }

    nsMemory::Free(target);

    if (NS_FAILED(rv))
        _retval.Truncate();
    return rv;
}

/* attribute PRBool followLinks; */
NS_IMETHODIMP
nsLocalFile::GetFollowLinks(PRBool *aFollowLinks)
{
    *aFollowLinks = PR_TRUE;
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::SetFollowLinks(PRBool aFollowLinks)
{
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::GetDirectoryEntries(nsISimpleEnumerator **entries)
{
    nsDirEnumeratorUnix *dir = new nsDirEnumeratorUnix();
    if (!dir)
        return NS_ERROR_OUT_OF_MEMORY;

    NS_ADDREF(dir);
    nsresult rv = dir->Init(this, PR_FALSE);
    if (NS_FAILED(rv)) {
        *entries = nsnull;
        NS_RELEASE(dir);
    } else {
        *entries = dir; // transfer reference
    }

    return rv;
}

NS_IMETHODIMP
nsLocalFile::Load(PRLibrary **_retval)
{
    CHECK_mPath();
    NS_ENSURE_ARG_POINTER(_retval);

    NS_TIMELINE_START_TIMER("PR_LoadLibrary");

#ifdef NS_BUILD_REFCNT_LOGGING
    nsTraceRefcntImpl::SetActivityIsLegal(PR_FALSE);
#endif

    *_retval = PR_LoadLibrary(mPath.get());

#ifdef NS_BUILD_REFCNT_LOGGING
    nsTraceRefcntImpl::SetActivityIsLegal(PR_TRUE);
#endif

    NS_TIMELINE_STOP_TIMER("PR_LoadLibrary");
    NS_TIMELINE_MARK_TIMER1("PR_LoadLibrary", mPath.get());

    if (!*_retval)
        return NS_ERROR_FAILURE;
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::GetPersistentDescriptor(nsACString &aPersistentDescriptor)
{
    return GetNativePath(aPersistentDescriptor);
}

NS_IMETHODIMP
nsLocalFile::SetPersistentDescriptor(const nsACString &aPersistentDescriptor)
{
    return InitWithNativePath(aPersistentDescriptor);
}

#ifdef XP_BEOS
NS_IMETHODIMP
nsLocalFile::Reveal()
{
    BPath bPath(mPath.get());
    PRBool isDirectory;
    if (NS_FAILED(IsDirectory(&isDirectory)))
        return NS_ERROR_FAILURE;
  
    if(!isDirectory)
        bPath.GetParent(&bPath);
    entry_ref ref;
    get_ref_for_path(bPath.Path(),&ref);
    BMessage message(B_REFS_RECEIVED);
    message.AddRef("refs",&ref);
    BMessenger messenger("application/x-vnd.Be-TRAK");
    messenger.SendMessage(&message);
    return NS_OK;
}

NS_IMETHODIMP
nsLocalFile::Launch()
{
    entry_ref ref;
    get_ref_for_path (mPath.get(), &ref);
    be_roster->Launch (&ref);

    return NS_OK;
}
#else
NS_IMETHODIMP
nsLocalFile::Reveal()
{
#ifdef MOZ_WIDGET_GTK2
    nsCOMPtr<nsIGIOService> giovfs = do_GetService(NS_GIOSERVICE_CONTRACTID);
    nsCOMPtr<nsIGnomeVFSService> gnomevfs = do_GetService(NS_GNOMEVFSSERVICE_CONTRACTID);
    if (!giovfs && !gnomevfs)
        return NS_ERROR_FAILURE;

    PRBool isDirectory;
    if (NS_FAILED(IsDirectory(&isDirectory)))
        return NS_ERROR_FAILURE;

    if (isDirectory) {
        if (giovfs)
            return giovfs->ShowURIForInput(mPath);
        else 
            /* Fallback to GnomeVFS */
            return gnomevfs->ShowURIForInput(mPath);
    } else {
        nsCOMPtr<nsIFile> parentDir;
        nsCAutoString dirPath;
        if (NS_FAILED(GetParent(getter_AddRefs(parentDir))))
            return NS_ERROR_FAILURE;
        if (NS_FAILED(parentDir->GetNativePath(dirPath)))
            return NS_ERROR_FAILURE;

        if (giovfs)
            return giovfs->ShowURIForInput(dirPath);
        else 
            return gnomevfs->ShowURIForInput(dirPath);        
    }
#else
    return NS_ERROR_FAILURE;
#endif
}

NS_IMETHODIMP
nsLocalFile::Launch()
{
#ifdef MOZ_WIDGET_GTK2
#if (MOZ_PLATFORM_MAEMO==5)
    const PRInt32 kHILDON_SUCCESS = 1;
    DBusError err;
    dbus_error_init(&err);

    DBusConnection *connection = dbus_bus_get(DBUS_BUS_SESSION, &err);
    if (dbus_error_is_set(&err)) {
      dbus_error_free(&err);
      return NS_ERROR_FAILURE;
    }

    if (nsnull == connection)
      return NS_ERROR_FAILURE;

    if (hildon_mime_open_file(connection, mPath.get()) != kHILDON_SUCCESS)
      return NS_ERROR_FAILURE;
    return NS_OK;
#else
    nsCOMPtr<nsIGIOService> giovfs = do_GetService(NS_GIOSERVICE_CONTRACTID);
    nsCOMPtr<nsIGnomeVFSService> gnomevfs = do_GetService(NS_GNOMEVFSSERVICE_CONTRACTID);
    if (giovfs) {
      return giovfs->ShowURIForInput(mPath);
    } else if (gnomevfs) {
      /* GnomeVFS fallback */
      return gnomevfs->ShowURIForInput(mPath);
    }
    
    return NS_ERROR_FAILURE;
#endif
#else
    return NS_ERROR_FAILURE;
#endif
}
#endif

nsresult
NS_NewNativeLocalFile(const nsACString &path, PRBool followSymlinks, nsILocalFile **result)
{
    nsLocalFile *file = new nsLocalFile();
    if (!file)
        return NS_ERROR_OUT_OF_MEMORY;
    NS_ADDREF(file);

    if (!path.IsEmpty()) {
        nsresult rv = file->InitWithNativePath(path);
        if (NS_FAILED(rv)) {
            NS_RELEASE(file);
            return rv;
        }
    }
    *result = file;
    return NS_OK;
}

//-----------------------------------------------------------------------------
// unicode support
//-----------------------------------------------------------------------------

#define SET_UCS(func, ucsArg) \
    { \
        nsCAutoString buf; \
        nsresult rv = NS_CopyUnicodeToNative(ucsArg, buf); \
        if (NS_FAILED(rv)) \
            return rv; \
        return (func)(buf); \
    }

#define GET_UCS(func, ucsArg) \
    { \
        nsCAutoString buf; \
        nsresult rv = (func)(buf); \
        if (NS_FAILED(rv)) return rv; \
        return NS_CopyNativeToUnicode(buf, ucsArg); \
    }

#define SET_UCS_2ARGS_2(func, opaqueArg, ucsArg) \
    { \
        nsCAutoString buf; \
        nsresult rv = NS_CopyUnicodeToNative(ucsArg, buf); \
        if (NS_FAILED(rv)) \
            return rv; \
        return (func)(opaqueArg, buf); \
    }

// Unicode interface Wrapper
nsresult  
nsLocalFile::InitWithPath(const nsAString &filePath)
{
    SET_UCS(InitWithNativePath, filePath);
}
nsresult  
nsLocalFile::Append(const nsAString &node)
{
    SET_UCS(AppendNative, node);
}
nsresult  
nsLocalFile::AppendRelativePath(const nsAString &node)
{
    SET_UCS(AppendRelativeNativePath, node);
}
nsresult  
nsLocalFile::GetLeafName(nsAString &aLeafName)
{
    GET_UCS(GetNativeLeafName, aLeafName);
}
nsresult  
nsLocalFile::SetLeafName(const nsAString &aLeafName)
{
    SET_UCS(SetNativeLeafName, aLeafName);
}
nsresult  
nsLocalFile::GetPath(nsAString &_retval)
{
    return NS_CopyNativeToUnicode(mPath, _retval);
}
nsresult  
nsLocalFile::CopyTo(nsIFile *newParentDir, const nsAString &newName)
{
    SET_UCS_2ARGS_2(CopyToNative , newParentDir, newName);
}
nsresult  
nsLocalFile::CopyToFollowingLinks(nsIFile *newParentDir, const nsAString &newName)
{
    SET_UCS_2ARGS_2(CopyToFollowingLinksNative , newParentDir, newName);
}
nsresult  
nsLocalFile::MoveTo(nsIFile *newParentDir, const nsAString &newName)
{
    SET_UCS_2ARGS_2(MoveToNative, newParentDir, newName);
}
nsresult
nsLocalFile::GetTarget(nsAString &_retval)
{   
    GET_UCS(GetNativeTarget, _retval);
}

// nsIHashable

NS_IMETHODIMP
nsLocalFile::Equals(nsIHashable* aOther, PRBool *aResult)
{
    nsCOMPtr<nsIFile> otherFile(do_QueryInterface(aOther));
    if (!otherFile) {
        *aResult = PR_FALSE;
        return NS_OK;
    }

    return Equals(otherFile, aResult);
}

NS_IMETHODIMP
nsLocalFile::GetHashCode(PRUint32 *aResult)
{
    *aResult = nsCRT::HashCode(mPath.get());
    return NS_OK;
}

nsresult 
NS_NewLocalFile(const nsAString &path, PRBool followLinks, nsILocalFile* *result)
{
    nsCAutoString buf;
    nsresult rv = NS_CopyUnicodeToNative(path, buf);
    if (NS_FAILED(rv))
        return rv;
    return NS_NewNativeLocalFile(buf, followLinks, result);
}

//-----------------------------------------------------------------------------
// global init/shutdown
//-----------------------------------------------------------------------------

void
nsLocalFile::GlobalInit()
{
}

void
nsLocalFile::GlobalShutdown()
{
}
