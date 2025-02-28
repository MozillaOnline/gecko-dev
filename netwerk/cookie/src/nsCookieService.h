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
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2003
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Daniel Witte (dwitte@stanford.edu)
 *   Michiel van Leeuwen (mvl@exedo.nl)
 *   Michael Ventnor <m.ventnor@gmail.com>
 *   Ehsan Akhgari <ehsan.akhgari@gmail.com>
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

#ifndef nsCookieService_h__
#define nsCookieService_h__

#include "nsICookieService.h"
#include "nsICookieManager.h"
#include "nsICookieManager2.h"
#include "nsIObserver.h"
#include "nsWeakReference.h"

#include "nsCookie.h"
#include "nsString.h"
#include "nsAutoPtr.h"
#include "nsHashKeys.h"
#include "nsTHashtable.h"
#include "mozIStorageStatement.h"
#include "mozIStorageConnection.h"

struct nsCookieAttributes;
struct nsListIter;
struct nsEnumerationData;

class nsICookiePermission;
class nsIEffectiveTLDService;
class nsIIDNService;
class nsIPrefBranch;
class nsIObserverService;
class nsIURI;
class nsIChannel;

// hash entry class
class nsCookieEntry : public PLDHashEntryHdr
{
  public:
    // Hash methods
    typedef const nsCString& KeyType;
    typedef const nsCString* KeyTypePointer;
    typedef nsTArray< nsRefPtr<nsCookie> > ArrayType;
    typedef ArrayType::index_type IndexType;

    explicit
    nsCookieEntry(KeyTypePointer aBaseDomain)
     : mBaseDomain(*aBaseDomain)
    {
    }

    nsCookieEntry(const nsCookieEntry& toCopy)
    {
      // if we end up here, things will break. nsTHashtable shouldn't
      // allow this, since we set ALLOW_MEMMOVE to true.
      NS_NOTREACHED("nsCookieEntry copy constructor is forbidden!");
    }

    ~nsCookieEntry()
    {
    }

    KeyType GetKey() const
    {
      return mBaseDomain;
    }

    PRBool KeyEquals(KeyTypePointer aKey) const
    {
      return mBaseDomain == *aKey;
    }

    static KeyTypePointer KeyToPointer(KeyType aKey)
    {
      return &aKey;
    }

    static PLDHashNumber HashKey(KeyTypePointer aKey)
    {
      return HashString(*aKey);
    }

    enum { ALLOW_MEMMOVE = PR_TRUE };

    inline ArrayType& GetCookies() { return mCookies; }

  private:
    nsCString mBaseDomain;
    ArrayType mCookies;
};

// encapsulates in-memory and on-disk DB states, so we can
// conveniently switch state when entering or exiting private browsing.
struct DBState
{
  DBState() : cookieCount(0), cookieOldestTime(LL_MAXINT) { }

  nsTHashtable<nsCookieEntry>     hostTable;
  PRUint32                        cookieCount;
  PRInt64                         cookieOldestTime;
  nsCOMPtr<mozIStorageConnection> dbConn;
  nsCOMPtr<mozIStorageStatement>  stmtInsert;
  nsCOMPtr<mozIStorageStatement>  stmtDelete;
  nsCOMPtr<mozIStorageStatement>  stmtUpdate;
};

/******************************************************************************
 * nsCookieService:
 * class declaration
 ******************************************************************************/

class nsCookieService : public nsICookieService
                      , public nsICookieManager2
                      , public nsIObserver
                      , public nsSupportsWeakReference
{
  public:
    // nsISupports
    NS_DECL_ISUPPORTS
    NS_DECL_NSIOBSERVER
    NS_DECL_NSICOOKIESERVICE
    NS_DECL_NSICOOKIEMANAGER
    NS_DECL_NSICOOKIEMANAGER2

    nsCookieService();
    virtual ~nsCookieService();
    static nsCookieService*       GetSingleton();
    nsresult                      Init();

  protected:
    void                          PrefChanged(nsIPrefBranch *aPrefBranch);
    nsresult                      InitDB();
    nsresult                      TryInitDB(PRBool aDeleteExistingDB);
    nsresult                      CreateTable();
    void                          CloseDB();
    nsresult                      Read();
    nsresult                      NormalizeHost(nsCString &aHost);
    nsresult                      GetBaseDomain(nsIURI *aHostURI, nsCString &aBaseDomain, PRBool &aRequireHostMatch);
    nsresult                      GetBaseDomainFromHost(const nsACString &aHost, nsCString &aBaseDomain);
    void                          GetCookieInternal(nsIURI *aHostURI, nsIChannel *aChannel, PRBool aHttpBound, char **aCookie);
    nsresult                      SetCookieStringInternal(nsIURI *aHostURI, nsIPrompt *aPrompt, const char *aCookieHeader, const char *aServerTime, nsIChannel *aChannel, PRBool aFromHttp);
    PRBool                        SetCookieInternal(nsIURI *aHostURI, nsIChannel *aChannel, const nsCString& aBaseDomain, PRBool aRequireHostMatch, nsDependentCString &aCookieHeader, PRInt64 aServerTime, PRBool aFromHttp);
    void                          AddInternal(const nsCString& aBaseDomain, nsCookie *aCookie, PRInt64 aCurrentTimeInUsec, nsIURI *aHostURI, const char *aCookieHeader, PRBool aFromHttp);
    void                          RemoveCookieFromList(const nsListIter &aIter);
    PRBool                        AddCookieToList(const nsCString& aBaseDomain, nsCookie *aCookie, PRBool aWriteToDB = PR_TRUE);
    void                          UpdateCookieInList(nsCookie *aCookie, PRInt64 aLastAccessed);
    static PRBool                 GetTokenValue(nsASingleFragmentCString::const_char_iterator &aIter, nsASingleFragmentCString::const_char_iterator &aEndIter, nsDependentCSubstring &aTokenString, nsDependentCSubstring &aTokenValue, PRBool &aEqualsFound);
    static PRBool                 ParseAttributes(nsDependentCString &aCookieHeader, nsCookieAttributes &aCookie);
    PRBool                        IsForeign(const nsCString &aBaseDomain, PRBool aRequireHostMatch, nsIURI *aFirstURI);
    PRUint32                      CheckPrefs(nsIURI *aHostURI, nsIChannel *aChannel, const nsCString &aBaseDomain, PRBool aRequireHostMatch, const char *aCookieHeader);
    PRBool                        CheckDomain(nsCookieAttributes &aCookie, nsIURI *aHostURI, const nsCString &aBaseDomain, PRBool aRequireHostMatch);
    static PRBool                 CheckPath(nsCookieAttributes &aCookie, nsIURI *aHostURI);
    static PRBool                 GetExpiry(nsCookieAttributes &aCookie, PRInt64 aServerTime, PRInt64 aCurrentTime);
    void                          RemoveAllFromMemory();
    void                          PurgeCookies(PRInt64 aCurrentTimeInUsec);
    PRBool                        FindCookie(const nsCString& aBaseDomain, const nsAFlatCString &aHost, const nsAFlatCString &aName, const nsAFlatCString &aPath, nsListIter &aIter, PRInt64 aCurrentTime);
    PRUint32                      CountCookiesFromHostInternal(const nsCString &aBaseDomain, nsEnumerationData &aData);
    void                          NotifyRejected(nsIURI *aHostURI);
    void                          NotifyChanged(nsISupports *aSubject, const PRUnichar *aData);

  protected:
    // cached members.
    nsCOMPtr<nsIObserverService>     mObserverService;
    nsCOMPtr<nsICookiePermission>    mPermissionService;
    nsCOMPtr<nsIEffectiveTLDService> mTLDService;
    nsCOMPtr<nsIIDNService>          mIDNService;

    // we have two separate DB states: one for normal browsing and one for
    // private browsing, switching between them as appropriate. this state
    // encapsulates both the in-memory table and the on-disk DB.
    // note that the private states' dbConn should always be null - we never
    // want to be dealing with the on-disk DB when in private browsing.
    DBState                      *mDBState;
    DBState                       mDefaultDBState;
    DBState                       mPrivateDBState;

    // cached prefs
    PRUint8                       mCookiesPermissions;   // BEHAVIOR_{ACCEPT, REJECTFOREIGN, REJECT}
    PRUint16                      mMaxNumberOfCookies;
    PRUint16                      mMaxCookiesPerHost;
    PRInt64                       mCookiePurgeAge;

    // private static member, used to cache a ptr to nsCookieService,
    // so we can make nsCookieService a singleton xpcom object.
    static nsCookieService        *gCookieService;

    // this callback needs access to member functions
    friend PLDHashOperator purgeCookiesCallback(nsCookieEntry *aEntry, void *aArg);
};

#endif // nsCookieService_h__
