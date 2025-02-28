/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 * The Original Code is Places.
 *
 * The Initial Developer of the Original Code is
 * Google Inc.
 * Portions created by the Initial Developer are Copyright (C) 2005
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Brett Wilson <brettw@gmail.com> (original author)
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

#include "nsCOMPtr.h"
#include "nsDataHashtable.h"
#include "nsIFaviconService.h"
#include "nsServiceManagerUtils.h"
#include "nsString.h"

#include "nsToolkitCompsCID.h"

#include "mozilla/storage.h"

// Favicons bigger than this size should not be saved to the db to avoid
// bloating it with large image blobs.
// This still allows us to accept a favicon even if we cannot optimize it.
#define MAX_FAVICON_SIZE 10240

// forward class definitions
class mozIStorageStatementCallback;

// forward definition for friend class
class FaviconLoadListener;

class nsFaviconService : public nsIFaviconService
{
public:
  nsFaviconService();

  /**
   * Obtains the service's object.
   */
  static nsFaviconService* GetSingleton();

  /**
   * Initializes the service's object.  This should only be called once.
   */
  nsresult Init();

  // called by nsNavHistory::Init
  static nsresult InitTables(mozIStorageConnection* aDBConn);

  /**
   * Returns a cached pointer to the favicon service for consumers in the
   * places directory.
   */
  static nsFaviconService* GetFaviconService()
  {
    if (!gFaviconService) {
      nsCOMPtr<nsIFaviconService> serv =
        do_GetService(NS_FAVICONSERVICE_CONTRACTID);
      NS_ENSURE_TRUE(serv, nsnull);
      NS_ASSERTION(gFaviconService, "Should have static instance pointer now");
    }
    return gFaviconService;
  }

  // internal version called by history when done lazily
  nsresult DoSetAndLoadFaviconForPage(nsIURI* aPage, nsIURI* aFavicon,
                                      PRBool aForceReload);

  // addition to API for strings to prevent excessive parsing of URIs
  nsresult GetFaviconLinkForIconString(const nsCString& aIcon, nsIURI** aOutput);
  void GetFaviconSpecForIconString(const nsCString& aIcon, nsACString& aOutput);

  nsresult OptimizeFaviconImage(const PRUint8* aData, PRUint32 aDataLen,
                                const nsACString& aMimeType,
                                nsACString& aNewData, nsACString& aNewMimeType);

  /**
   * Obtains the favicon data asynchronously.
   *
   * @param aFaviconURI
   *        The URI representing the favicon we are looking for.
   * @param aCallback
   *        The callback where results or errors will be dispatch to.  In the
   *        returned result, the favicon binary data will be at index 0, and the
   *        mime type will be at index 1.
   */
  nsresult GetFaviconDataAsync(nsIURI* aFaviconURI,
                               mozIStorageStatementCallback* aCallback);

  /**
   * Checks to see if a favicon's URI has changed, and notifies callers if it
   * has.
   *
   * @param aPageURI
   *        The URI of the page aFaviconURI is for.
   * @param aFaviconURI
   *        The URI for the favicon we want to test for on aPageURI.
   */
  void checkAndNotify(nsIURI* aPageURI, nsIURI* aFaviconURI);

  /**
   * Finalize all internal statements.
   */
  nsresult FinalizeStatements();

  NS_DECL_ISUPPORTS
  NS_DECL_NSIFAVICONSERVICE

private:
  ~nsFaviconService();

  nsCOMPtr<mozIStorageConnection> mDBConn; // from history service

  /**
   * Always use this getter and never use directly the statement nsCOMPtr.
   */
  mozIStorageStatement* GetStatement(const nsCOMPtr<mozIStorageStatement>& aStmt);
  nsCOMPtr<mozIStorageStatement> mDBGetURL; // returns URL, data len given page
  nsCOMPtr<mozIStorageStatement> mDBGetData; // returns actual data given URL
  nsCOMPtr<mozIStorageStatement> mDBGetIconInfo;
  nsCOMPtr<mozIStorageStatement> mDBInsertIcon;
  nsCOMPtr<mozIStorageStatement> mDBUpdateIcon;
  nsCOMPtr<mozIStorageStatement> mDBSetPageFavicon;
  nsCOMPtr<mozIStorageStatement> mDBRemoveOnDiskReferences;
  nsCOMPtr<mozIStorageStatement> mDBRemoveTempReferences;
  nsCOMPtr<mozIStorageStatement> mDBRemoveAllFavicons;

  static nsFaviconService* gFaviconService;

  /**
   * A cached URI for the default icon. We return this a lot, and don't want to
   * re-parse and normalize our unchanging string many times.  Important: do
   * not return this directly; use Clone() since callers may change the object
   * they get back. May be null, in which case it needs initialization.
   */
  nsCOMPtr<nsIURI> mDefaultIcon;

  // Set to true during favicons expiration, addition of new favicons won't be
  // allowed till expiration has finished since those should then be expired.
  bool mFaviconsExpirationRunning;

  // The target dimension, in pixels, for favicons we optimize.
  // If we find images that are as large or larger than an uncompressed RGBA
  // image of this size (mOptimizedIconDimension*mOptimizedIconDimension*4),
  // we will try to optimize it.
  PRInt32 mOptimizedIconDimension;

  PRUint32 mFailedFaviconSerial;
  nsDataHashtable<nsCStringHashKey, PRUint32> mFailedFavicons;

  nsresult SetFaviconUrlForPageInternal(nsIURI* aURI, nsIURI* aFavicon,
                                        PRBool* aHasData);

  nsresult UpdateBookmarkRedirectFavicon(nsIURI* aPage, nsIURI* aFavicon);
  void SendFaviconNotifications(nsIURI* aPage, nsIURI* aFaviconURI);

  friend class FaviconLoadListener;

  bool mShuttingDown;
};

#define FAVICON_DEFAULT_URL "chrome://mozapps/skin/places/defaultFavicon.png"
#define FAVICON_ERRORPAGE_URL "chrome://global/skin/icons/warning-16.png"
#define FAVICON_ANNOTATION_NAME "favicon"
