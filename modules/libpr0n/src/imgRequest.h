/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
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
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2001
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Stuart Parmenter <pavlov@netscape.com>
 *   Bobby Holley <bobbyholley@gmail.com>
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

#ifndef imgRequest_h__
#define imgRequest_h__

#include "imgIContainer.h"
#include "imgIDecoder.h"
#include "imgIDecoderObserver.h"

#include "nsIChannelEventSink.h"
#include "nsIContentSniffer.h"
#include "nsIInterfaceRequestor.h"
#include "nsIRequest.h"
#include "nsIProperties.h"
#include "nsIStreamListener.h"
#include "nsIURI.h"
#include "nsIPrincipal.h"

#include "nsCategoryCache.h"
#include "nsCOMPtr.h"
#include "nsString.h"
#include "nsTObserverArray.h"
#include "nsWeakReference.h"
#include "ImageErrors.h"
#include "imgIRequest.h"

class imgCacheValidator;

class imgRequestProxy;
class imgCacheEntry;

enum {
  stateRequestStarted    = PR_BIT(0),
  stateHasSize           = PR_BIT(1),
  stateDecodeStarted     = PR_BIT(2),
  stateDecodeStopped     = PR_BIT(3),
  stateRequestStopped    = PR_BIT(4)
};

class imgRequest : public imgIDecoderObserver,
                   public nsIStreamListener,
                   public nsSupportsWeakReference,
                   public nsIChannelEventSink,
                   public nsIInterfaceRequestor
{
public:
  imgRequest();
  virtual ~imgRequest();

  NS_DECL_ISUPPORTS

  nsresult Init(nsIURI *aURI,
                nsIURI *aKeyURI,
                nsIRequest *aRequest,
                nsIChannel *aChannel,
                imgCacheEntry *aCacheEntry,
                void *aCacheId,
                void *aLoadId);

  // Callers must call NotifyProxyListener later.
  nsresult AddProxy(imgRequestProxy *proxy);

  // aNotify==PR_FALSE still sends OnStopRequest.
  nsresult RemoveProxy(imgRequestProxy *proxy, nsresult aStatus, PRBool aNotify);
  nsresult NotifyProxyListener(imgRequestProxy *proxy);

  void SniffMimeType(const char *buf, PRUint32 len);

  // a request is "reusable" if it has already been loaded, or it is
  // currently being loaded on the same event queue as the new request
  // being made...
  PRBool IsReusable(void *aCacheId) { return !mLoading || (aCacheId == mCacheId); }

  // Cancel, but also ensure that all work done in Init() is undone. Call this
  // only when the channel has failed to open, and so calling Cancel() on it
  // won't be sufficient.
  void CancelAndAbort(nsresult aStatus);

  nsresult GetImage(imgIContainer **aImage);

  // Methods that get forwarded to the imgContainer, or deferred until it's
  // instantiated.
  nsresult LockImage();
  nsresult UnlockImage();
  nsresult RequestDecode();
  static nsresult GetResultFromImageStatus(PRUint32 aStatus)
  {
    if (aStatus & imgIRequest::STATUS_ERROR)
      return NS_IMAGELIB_ERROR_FAILURE;
    if (aStatus & imgIRequest::STATUS_LOAD_COMPLETE)
      return NS_IMAGELIB_SUCCESS_LOAD_FINISHED;
    return NS_OK;
  }
private:
  friend class imgCacheEntry;
  friend class imgRequestProxy;
  friend class imgLoader;
  friend class imgCacheValidator;
  friend class imgCacheExpirationTracker;

  inline void SetLoadId(void *aLoadId) {
    mLoadId = aLoadId;
    mLoadTime = PR_Now();
  }
  inline PRUint32 GetImageStatus() const { return mImageStatus; }
  inline PRUint32 GetState() const { return mState; }
  void Cancel(nsresult aStatus);
  nsresult GetURI(nsIURI **aURI);
  nsresult GetKeyURI(nsIURI **aURI);
  nsresult GetPrincipal(nsIPrincipal **aPrincipal);
  nsresult GetSecurityInfo(nsISupports **aSecurityInfo);
  void RemoveFromCache();
  inline const char *GetMimeType() const {
    return mContentType.get();
  }
  inline nsIProperties *Properties() {
    return mProperties;
  }

  // Reset the cache entry after we've dropped our reference to it. Used by the
  // imgLoader when our cache entry is re-requested after we've dropped our
  // reference to it.
  void SetCacheEntry(imgCacheEntry *entry);

  // Returns whether we've got a reference to the cache entry.
  PRBool HasCacheEntry() const;

  // Return true if at least one of our proxies, excluding
  // aProxyToIgnore, has an observer.  aProxyToIgnore may be null.
  PRBool HaveProxyWithObserver(imgRequestProxy* aProxyToIgnore) const;

  // Return the priority of the underlying network request, or return
  // PRIORITY_NORMAL if it doesn't support nsISupportsPriority.
  PRInt32 Priority() const;

  // Adjust the priority of the underlying network request by the given delta
  // on behalf of the given proxy.
  void AdjustPriority(imgRequestProxy *aProxy, PRInt32 aDelta);

  // Return whether we've seen some data at this point
  PRBool HasTransferredData() const { return mGotData; }

  // Set whether this request is stored in the cache. If it isn't, regardless
  // of whether this request has a non-null mCacheEntry, this imgRequest won't
  // try to update or modify the image cache.
  void SetIsInCache(PRBool cacheable);

  // Update the cache entry size based on the image container
  void UpdateCacheEntrySize();

public:
  NS_DECL_IMGIDECODEROBSERVER
  NS_DECL_IMGICONTAINEROBSERVER
  NS_DECL_NSISTREAMLISTENER
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSICHANNELEVENTSINK
  NS_DECL_NSIINTERFACEREQUESTOR

private:
  nsCOMPtr<nsIRequest> mRequest;
  // The original URI we were loaded with.
  nsCOMPtr<nsIURI> mURI;
  // The URI we are keyed on in the cache.
  nsCOMPtr<nsIURI> mKeyURI;
  nsCOMPtr<nsIPrincipal> mPrincipal;
  nsCOMPtr<imgIContainer> mImage;
  nsCOMPtr<nsIProperties> mProperties;
  nsCOMPtr<nsISupports> mSecurityInfo;
  nsCOMPtr<nsIChannel> mChannel;
  nsCOMPtr<nsIInterfaceRequestor> mPrevChannelSink;

  nsTObserverArray<imgRequestProxy*> mObservers;

  PRUint32 mImageStatus;
  PRUint32 mState;
  nsCString mContentType;

  nsRefPtr<imgCacheEntry> mCacheEntry; /* we hold on to this to this so long as we have observers */

  void *mCacheId;

  void *mLoadId;
  PRTime mLoadTime;

  imgCacheValidator *mValidator;
  nsCategoryCache<nsIContentSniffer> mImageSniffers;

  // Sometimes consumers want to do things before the image is ready. Let them,
  // and apply the action when the image becomes available.
  PRUint32 mDeferredLocks;
  PRPackedBool mDecodeRequested : 1;

  PRPackedBool mIsMultiPartChannel : 1;
  PRPackedBool mLoading : 1;
  PRPackedBool mHadLastPart : 1;
  PRPackedBool mGotData : 1;
  PRPackedBool mIsInCache : 1;
};

#endif
