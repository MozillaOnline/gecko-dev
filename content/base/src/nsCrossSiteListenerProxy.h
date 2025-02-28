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
 * The Original Code is mozilla.org code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2007
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Jonas Sicking <jonas@sicking.cc> (Original Author)
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

#ifndef nsCrossSiteListenerProxy_h__
#define nsCrossSiteListenerProxy_h__

#include "nsIStreamListener.h"
#include "nsIInterfaceRequestor.h"
#include "nsCOMPtr.h"
#include "nsString.h"
#include "nsIURI.h"
#include "nsTArray.h"
#include "nsIInterfaceRequestor.h"
#include "nsIChannelEventSink.h"

class nsIURI;
class nsIParser;
class nsIPrincipal;

extern PRBool
IsValidHTTPToken(const nsCSubstring& aToken);

class nsCrossSiteListenerProxy : public nsIStreamListener,
                                 public nsIInterfaceRequestor,
                                 public nsIChannelEventSink
{
public:
  nsCrossSiteListenerProxy(nsIStreamListener* aOuter,
                           nsIPrincipal* aRequestingPrincipal,
                           nsIChannel* aChannel,
                           PRBool aWithCredentials,
                           nsresult* aResult);
  nsCrossSiteListenerProxy(nsIStreamListener* aOuter,
                           nsIPrincipal* aRequestingPrincipal,
                           nsIChannel* aChannel,
                           PRBool aWithCredentials,
                           const nsCString& aPreflightMethod,
                           const nsTArray<nsCString>& aPreflightHeaders,
                           nsresult* aResult);

  NS_DECL_ISUPPORTS
  NS_DECL_NSIREQUESTOBSERVER
  NS_DECL_NSISTREAMLISTENER
  NS_DECL_NSIINTERFACEREQUESTOR
  NS_DECL_NSICHANNELEVENTSINK

  // Must be called at startup.
  static void Startup();

  void AllowHTTPResult(PRUint32 aResultCode)
  {
    mAllowedHTTPErrors.AppendElement(aResultCode);
  }

private:
  nsresult UpdateChannel(nsIChannel* aChannel);
  nsresult CheckRequestApproved(nsIRequest* aRequest, PRBool aIsRedirect);

  nsCOMPtr<nsIStreamListener> mOuterListener;
  nsCOMPtr<nsIPrincipal> mRequestingPrincipal;
  nsCOMPtr<nsIInterfaceRequestor> mOuterNotificationCallbacks;
  PRBool mWithCredentials;
  PRBool mRequestApproved;
  PRBool mHasBeenCrossSite;
  PRBool mIsPreflight;
  nsCString mPreflightMethod;
  nsTArray<nsCString> mPreflightHeaders;
  nsTArray<PRUint32> mAllowedHTTPErrors;
};

#endif
