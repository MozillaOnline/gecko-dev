/* -*- Mode: C++; tab-width: 2; indent-tabs-mode:nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sts=2 sw=2 et cin: */
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
 * Portions created by the Initial Developer are Copyright (C) 1999
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
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

#include "nsURILoader.h"
#include "nsAutoPtr.h"
#include "nsIURIContentListener.h"
#include "nsIContentHandler.h"
#include "nsILoadGroup.h"
#include "nsIDocumentLoader.h"
#include "nsIWebProgress.h"
#include "nsIWebProgressListener.h"
#include "nsIIOService.h"
#include "nsIServiceManager.h"
#include "nsIStreamListener.h"
#include "nsIURI.h"
#include "nsIChannel.h"
#include "nsIInterfaceRequestor.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsIProgressEventSink.h"
#include "nsIInputStream.h"
#include "nsIStreamConverterService.h"
#include "nsWeakReference.h"
#include "nsIHttpChannel.h"
#include "nsIMultiPartChannel.h"
#include "netCore.h"
#include "nsCRT.h"
#include "nsIDocShell.h"
#include "nsIDocShellTreeItem.h"
#include "nsIDocShellTreeOwner.h"

#include "nsXPIDLString.h"
#include "nsString.h"
#include "nsNetUtil.h"
#include "nsIDOMWindowInternal.h"
#include "nsReadableUtils.h"
#include "nsDOMError.h"

#include "nsICategoryManager.h"
#include "nsCExternalHandlerService.h" // contains contractids for the helper app service

#include "nsIMIMEHeaderParam.h"
#include "nsNetCID.h"

#include "nsMimeTypes.h"

#include "nsDocLoader.h"

#ifdef PR_LOGGING
PRLogModuleInfo* nsURILoader::mLog = nsnull;
#endif

#define LOG(args) PR_LOG(nsURILoader::mLog, PR_LOG_DEBUG, args)
#define LOG_ERROR(args) PR_LOG(nsURILoader::mLog, PR_LOG_ERROR, args)
#define LOG_ENABLED() PR_LOG_TEST(nsURILoader::mLog, PR_LOG_DEBUG)

/**
 * The nsDocumentOpenInfo contains the state required when a single
 * document is being opened in order to discover the content type...
 * Each instance remains alive until its target URL has been loaded
 * (or aborted).
 */
class nsDocumentOpenInfo : public nsIStreamListener
{
public:
  // Needed for nsCOMPtr to work right... Don't call this!
  nsDocumentOpenInfo();

  // Real constructor
  // aFlags is a combination of the flags on nsIURILoader
  nsDocumentOpenInfo(nsIInterfaceRequestor* aWindowContext,
                     PRUint32 aFlags,
                     nsURILoader* aURILoader);

  NS_DECL_ISUPPORTS

  /**
   * Prepares this object for receiving data. The stream
   * listener methods of this class must not be called before calling this
   * method.
   */
  nsresult Prepare();

  // Call this (from OnStartRequest) to attempt to find an nsIStreamListener to
  // take the data off our hands.
  nsresult DispatchContent(nsIRequest *request, nsISupports * aCtxt);

  // Call this if we need to insert a stream converter from aSrcContentType to
  // aOutContentType into the StreamListener chain.  DO NOT call it if the two
  // types are the same, since no conversion is needed in that case.
  nsresult ConvertData(nsIRequest *request,
                       nsIURIContentListener *aListener,
                       const nsACString & aSrcContentType,
                       const nsACString & aOutContentType);

  /**
   * Function to attempt to use aListener to handle the load.  If
   * PR_TRUE is returned, nothing else needs to be done; if PR_FALSE
   * is returned, then a different way of handling the load should be
   * tried.
   */
  PRBool TryContentListener(nsIURIContentListener* aListener,
                            nsIChannel* aChannel);

  // nsIRequestObserver methods:
  NS_DECL_NSIREQUESTOBSERVER

  // nsIStreamListener methods:
  NS_DECL_NSISTREAMLISTENER

protected:
  ~nsDocumentOpenInfo();

protected:
  /**
   * The first content listener to try dispatching data to.  Typically
   * the listener associated with the entity that originated the load.
   */
  nsCOMPtr<nsIURIContentListener> m_contentListener;

  /**
   * The stream listener to forward nsIStreamListener notifications
   * to.  This is set once the load is dispatched.
   */
  nsCOMPtr<nsIStreamListener> m_targetStreamListener;

  /**
   * A pointer to the entity that originated the load. We depend on getting
   * things like nsIURIContentListeners, nsIDOMWindows, etc off of it.
   */
  nsCOMPtr<nsIInterfaceRequestor> m_originalContext;

  /**
   * IS_CONTENT_PREFERRED is used for the boolean to pass to CanHandleContent
   * (also determines whether we use CanHandleContent or IsPreferred).
   * DONT_RETARGET means that we will only try m_originalContext, no other
   * listeners.
   */
  PRUint32 mFlags;

  /**
   * The type of the data we will be trying to dispatch.
   */
  nsCString mContentType;

  /**
   * Reference to the URILoader service so we can access its list of
   * nsIURIContentListeners.
   */
  nsRefPtr<nsURILoader> mURILoader;
};

NS_IMPL_THREADSAFE_ADDREF(nsDocumentOpenInfo)
NS_IMPL_THREADSAFE_RELEASE(nsDocumentOpenInfo)

NS_INTERFACE_MAP_BEGIN(nsDocumentOpenInfo)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIRequestObserver)
  NS_INTERFACE_MAP_ENTRY(nsIRequestObserver)
  NS_INTERFACE_MAP_ENTRY(nsIStreamListener)
NS_INTERFACE_MAP_END_THREADSAFE

nsDocumentOpenInfo::nsDocumentOpenInfo()
{
  NS_NOTREACHED("This should never be called\n");
}

nsDocumentOpenInfo::nsDocumentOpenInfo(nsIInterfaceRequestor* aWindowContext,
                                       PRUint32 aFlags,
                                       nsURILoader* aURILoader)
  : m_originalContext(aWindowContext),
    mFlags(aFlags),
    mURILoader(aURILoader)
{
}

nsDocumentOpenInfo::~nsDocumentOpenInfo()
{
}

nsresult nsDocumentOpenInfo::Prepare()
{
  LOG(("[0x%p] nsDocumentOpenInfo::Prepare", this));

  nsresult rv;

  // ask our window context if it has a uri content listener...
  m_contentListener = do_GetInterface(m_originalContext, &rv);
  return rv;
}

NS_IMETHODIMP nsDocumentOpenInfo::OnStartRequest(nsIRequest *request, nsISupports * aCtxt)
{
  LOG(("[0x%p] nsDocumentOpenInfo::OnStartRequest", this));
  
  nsresult rv = NS_OK;

  //
  // Deal with "special" HTTP responses:
  // 
  // - In the case of a 204 (No Content) or 205 (Reset Content) response, do
  //   not try to find a content handler.  Return NS_BINDING_ABORTED to cancel
  //   the request.  This has the effect of ensuring that the DocLoader does
  //   not try to interpret this as a real request.
  // 
  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(request, &rv));

  if (NS_SUCCEEDED(rv)) {
    PRUint32 responseCode = 0;

    rv = httpChannel->GetResponseStatus(&responseCode);

    if (NS_FAILED(rv)) {
      LOG_ERROR(("  Failed to get HTTP response status"));
      
      // behave as in the canceled case
      return NS_OK;
    }

    LOG(("  HTTP response status: %d", responseCode));

    if (204 == responseCode || 205 == responseCode) {
      return NS_BINDING_ABORTED;
    }
  }

  //
  // Make sure that the transaction has succeeded, so far...
  //
  nsresult status;

  rv = request->GetStatus(&status);
  
  NS_ASSERTION(NS_SUCCEEDED(rv), "Unable to get request status!");
  if (NS_FAILED(rv)) return rv;

  if (NS_FAILED(status)) {
    LOG_ERROR(("  Request failed, status: 0x%08X", rv));
  
    //
    // The transaction has already reported an error - so it will be torn
    // down. Therefore, it is not necessary to return an error code...
    //
    return NS_OK;
  }

  rv = DispatchContent(request, aCtxt);

  LOG(("  After dispatch, m_targetStreamListener: 0x%p, rv: 0x%08X", m_targetStreamListener.get(), rv));

  NS_ASSERTION(NS_SUCCEEDED(rv) || !m_targetStreamListener,
               "Must not have an m_targetStreamListener with a failure return!");

  NS_ENSURE_SUCCESS(rv, rv);
  
  if (m_targetStreamListener)
    rv = m_targetStreamListener->OnStartRequest(request, aCtxt);

  LOG(("  OnStartRequest returning: 0x%08X", rv));
  
  return rv;
}

NS_IMETHODIMP nsDocumentOpenInfo::OnDataAvailable(nsIRequest *request, nsISupports * aCtxt,
                                                  nsIInputStream * inStr, PRUint32 sourceOffset, PRUint32 count)
{
  // if we have retarged to the end stream listener, then forward the call....
  // otherwise, don't do anything

  nsresult rv = NS_OK;
  
  if (m_targetStreamListener)
    rv = m_targetStreamListener->OnDataAvailable(request, aCtxt, inStr, sourceOffset, count);
  return rv;
}

NS_IMETHODIMP nsDocumentOpenInfo::OnStopRequest(nsIRequest *request, nsISupports *aCtxt, 
                                                nsresult aStatus)
{
  LOG(("[0x%p] nsDocumentOpenInfo::OnStopRequest", this));
  
  if ( m_targetStreamListener)
  {
    nsCOMPtr<nsIStreamListener> listener(m_targetStreamListener);

    // If this is a multipart stream, we could get another
    // OnStartRequest after this... reset state.
    m_targetStreamListener = 0;
    mContentType.Truncate();
    listener->OnStopRequest(request, aCtxt, aStatus);
  }

  // Remember...
  // In the case of multiplexed streams (such as multipart/x-mixed-replace)
  // these stream listener methods could be called again :-)
  //
  return NS_OK;
}

nsresult nsDocumentOpenInfo::DispatchContent(nsIRequest *request, nsISupports * aCtxt)
{
  LOG(("[0x%p] nsDocumentOpenInfo::DispatchContent for type '%s'", this, mContentType.get()));

  NS_PRECONDITION(!m_targetStreamListener,
                  "Why do we already have a target stream listener?");
  
  nsresult rv;
  nsCOMPtr<nsIChannel> aChannel = do_QueryInterface(request);
  if (!aChannel) {
    LOG_ERROR(("  Request is not a channel.  Bailing."));
    return NS_ERROR_FAILURE;
  }

  NS_NAMED_LITERAL_CSTRING(anyType, "*/*");
  if (mContentType.IsEmpty() || mContentType == anyType) {
    rv = aChannel->GetContentType(mContentType);
    if (NS_FAILED(rv)) return rv;
    LOG(("  Got type from channel: '%s'", mContentType.get()));
  }

  PRBool isGuessFromExt =
    mContentType.LowerCaseEqualsASCII(APPLICATION_GUESS_FROM_EXT);
  if (isGuessFromExt) {
    // Reset to application/octet-stream for now; no one other than the
    // external helper app service should see APPLICATION_GUESS_FROM_EXT.
    mContentType = APPLICATION_OCTET_STREAM;
    aChannel->SetContentType(NS_LITERAL_CSTRING(APPLICATION_OCTET_STREAM));
  }

  // Check whether the data should be forced to be handled externally.  This
  // could happen because the Content-Disposition header is set so, or, in the
  // future, because the user has specified external handling for the MIME
  // type.
  PRBool forceExternalHandling = PR_FALSE;
  nsCAutoString disposition;
  nsCOMPtr<nsIHttpChannel> httpChannel(do_QueryInterface(request));
  nsCOMPtr<nsIURI> uri;
  if (httpChannel)
  {
    rv = httpChannel->GetResponseHeader(NS_LITERAL_CSTRING("content-disposition"),
                                        disposition);
    httpChannel->GetURI(getter_AddRefs(uri));
  }
  else
  {
    nsCOMPtr<nsIMultiPartChannel> multipartChannel(do_QueryInterface(request));
    if (multipartChannel)
    {
      rv = multipartChannel->GetContentDisposition(disposition);
    } else {
      // Soon-to-be common way to get Disposition: right now only JARChannel
      rv = NS_GetContentDisposition(request, disposition);
    }
  }

  LOG(("  Disposition header: '%s'", disposition.get()));

  if (NS_SUCCEEDED(rv) && !disposition.IsEmpty())
  {
    nsCOMPtr<nsIMIMEHeaderParam> mimehdrpar = do_GetService(NS_MIMEHEADERPARAM_CONTRACTID, &rv);
    if (NS_SUCCEEDED(rv))
    {
      nsCAutoString fallbackCharset;
      if (uri)
        uri->GetOriginCharset(fallbackCharset);
      nsAutoString dispToken;
      // Get the disposition type
      rv = mimehdrpar->GetParameter(disposition, "", fallbackCharset,
                                    PR_TRUE, nsnull, dispToken);
      // RFC 2183, section 2.8 says that an unknown disposition
      // value should be treated as "attachment"
      // XXXbz this code is duplicated in GetFilenameAndExtensionFromChannel in
      // nsExternalHelperAppService.  Factor it out!
      if (NS_FAILED(rv) || 
          (!dispToken.IsEmpty() &&
           !dispToken.LowerCaseEqualsLiteral("inline") &&
           // Broken sites just send
           // Content-Disposition: filename="file"
           // without a disposition token... screen those out.
           !dispToken.EqualsIgnoreCase("filename", 8) &&
           // Also in use is Content-Disposition: name="file"
           !dispToken.EqualsIgnoreCase("name", 4)))
        // We have a content-disposition of "attachment" or unknown
        forceExternalHandling = PR_TRUE;
    }
  }

  LOG(("  forceExternalHandling: %s", forceExternalHandling ? "yes" : "no"));
    
  // We're going to try to find a contentListener that can handle our data
  nsCOMPtr<nsIURIContentListener> contentListener;
  // The type or data the contentListener wants.
  nsXPIDLCString desiredContentType;

  if (!forceExternalHandling)
  {
    //
    // First step: See whether m_contentListener wants to handle this
    // content type.
    //
    if (m_contentListener && TryContentListener(m_contentListener, aChannel)) {
      LOG(("  Success!  Our default listener likes this type"));
      // All done here
      return NS_OK;
    }

    // If we aren't allowed to try other listeners, we're done here.
    if (!(mFlags & nsIURILoader::DONT_RETARGET)) {

      //
      // Second step: See whether some other registered listener wants
      // to handle this content type.
      //
      PRInt32 count = mURILoader->m_listeners.Count();
      nsCOMPtr<nsIURIContentListener> listener;
      for (PRInt32 i = 0; i < count; i++) {
        listener = do_QueryReferent(mURILoader->m_listeners[i]);
        if (listener) {
          if (TryContentListener(listener, aChannel)) {
            LOG(("  Found listener registered on the URILoader"));
            return NS_OK;
          }
        } else {
          // remove from the listener list, reset i and update count
          mURILoader->m_listeners.RemoveObjectAt(i--);
          --count;
        }
      }

      //
      // Third step: Try to find a content listener that has not yet had
      // the chance to register, as it is contained in a not-yet-loaded
      // module, but which has registered a contract ID.
      //
      nsCOMPtr<nsICategoryManager> catman =
        do_GetService(NS_CATEGORYMANAGER_CONTRACTID);
      if (catman) {
        nsXPIDLCString contractidString;
        rv = catman->GetCategoryEntry(NS_CONTENT_LISTENER_CATEGORYMANAGER_ENTRY,
                                      mContentType.get(),
                                      getter_Copies(contractidString));
        if (NS_SUCCEEDED(rv) && !contractidString.IsEmpty()) {
          LOG(("  Listener contractid for '%s' is '%s'",
               mContentType.get(), contractidString.get()));

          listener = do_CreateInstance(contractidString);
          LOG(("  Listener from category manager: 0x%p", listener.get()));
          
          if (listener && TryContentListener(listener, aChannel)) {
            LOG(("  Listener from category manager likes this type"));
            return NS_OK;
          }
        }
      }

      //
      // Fourth step: try to find an nsIContentHandler for our type.
      //
      nsCAutoString handlerContractID (NS_CONTENT_HANDLER_CONTRACTID_PREFIX);
      handlerContractID += mContentType;

      nsCOMPtr<nsIContentHandler> contentHandler =
        do_CreateInstance(handlerContractID.get());
      if (contentHandler) {
        LOG(("  Content handler found"));
        rv = contentHandler->HandleContent(mContentType.get(),
                                           m_originalContext, request);
        // XXXbz returning an error code to represent handling the
        // content is just bizarre!
        if (rv != NS_ERROR_WONT_HANDLE_CONTENT) {
          if (NS_FAILED(rv)) {
            // The content handler has unexpectedly failed.  Cancel the request
            // just in case the handler didn't...
            LOG(("  Content handler failed.  Aborting load"));
            request->Cancel(rv);
          }
#ifdef PR_LOGGING
          else {
            LOG(("  Content handler taking over load"));
          }
#endif

          return rv;
        }
      }
    }
  } else if (mFlags & nsIURILoader::DONT_RETARGET) {
    // External handling was forced, but we must not retarget
    // -> abort
    LOG(("  External handling forced, but not allowed to retarget -> aborting"));
    return NS_ERROR_WONT_HANDLE_CONTENT;
  }

  NS_ASSERTION(!m_targetStreamListener,
               "If we found a listener, why are we not using it?");
  
  //
  // Fifth step:  If no listener prefers this type, see if any stream
  //              converters exist to transform this content type into
  //              some other.
  //

  // We always want to do this, since even content being forced to
  // be handled externally may need decoding (eg via the unknown
  // content decoder).
  // Don't do this if the server sent us a MIME type of "*/*" because they saw
  // it in our Accept header and got confused.
  // XXXbz have to be careful here; may end up in some sort of bizarre infinite
  // decoding loop.
  if (mContentType != anyType) {
    rv = ConvertData(request, m_contentListener, mContentType, anyType);
    if (NS_FAILED(rv)) {
      m_targetStreamListener = nsnull;
    } else if (m_targetStreamListener) {
      // We found a converter for this MIME type.  We'll just pump data into it
      // and let the downstream nsDocumentOpenInfo handle things.
      LOG(("  Converter taking over now"));
      return NS_OK;
    }
  }

  if (mFlags & nsIURILoader::DONT_RETARGET) {
    LOG(("  Listener not interested and no stream converter exists, and retargeting disallowed -> aborting"));
    return NS_ERROR_WONT_HANDLE_CONTENT;
  }

  // Before dispatching to the external helper app service, check for an HTTP
  // error page.  If we got one, we don't want to handle it with a helper app,
  // really.
  if (httpChannel) {
    PRBool requestSucceeded;
    httpChannel->GetRequestSucceeded(&requestSucceeded);
    if (!requestSucceeded) {
      // returning error from OnStartRequest will cancel the channel
      return NS_ERROR_FILE_NOT_FOUND;
    }
  }
  
  // Sixth step:
  //
  // All attempts to dispatch this content have failed.  Just pass it off to
  // the helper app service.
  //
  nsCOMPtr<nsIExternalHelperAppService> helperAppService =
    do_GetService(NS_EXTERNALHELPERAPPSERVICE_CONTRACTID, &rv);
  if (helperAppService) {
    LOG(("  Passing load off to helper app service"));

    // Set these flags to indicate that the channel has been targeted and that
    // we are not using the original consumer.
    nsLoadFlags loadFlags = 0;
    request->GetLoadFlags(&loadFlags);
    request->SetLoadFlags(loadFlags | nsIChannel::LOAD_RETARGETED_DOCUMENT_URI
                                    | nsIChannel::LOAD_TARGETED);

    if (isGuessFromExt) {
      mContentType = APPLICATION_GUESS_FROM_EXT;
      aChannel->SetContentType(NS_LITERAL_CSTRING(APPLICATION_GUESS_FROM_EXT));
    }

    rv = helperAppService->DoContent(mContentType,
                                     request,
                                     m_originalContext,
                                     PR_FALSE,
                                     getter_AddRefs(m_targetStreamListener));
    if (NS_FAILED(rv)) {
      request->SetLoadFlags(loadFlags);
      m_targetStreamListener = nsnull;
    }
  }
      
  NS_ASSERTION(m_targetStreamListener || NS_FAILED(rv),
               "There is no way we should be successful at this point without a m_targetStreamListener");
  return rv;
}

nsresult
nsDocumentOpenInfo::ConvertData(nsIRequest *request,
                                nsIURIContentListener* aListener,
                                const nsACString& aSrcContentType,
                                const nsACString& aOutContentType)
{
  LOG(("[0x%p] nsDocumentOpenInfo::ConvertData from '%s' to '%s'", this,
       PromiseFlatCString(aSrcContentType).get(),
       PromiseFlatCString(aOutContentType).get()));

  NS_PRECONDITION(aSrcContentType != aOutContentType,
                  "ConvertData called when the two types are the same!");
  nsresult rv = NS_OK;

  nsCOMPtr<nsIStreamConverterService> StreamConvService = 
    do_GetService(NS_STREAMCONVERTERSERVICE_CONTRACTID, &rv);
  if (NS_FAILED(rv)) return rv;

  LOG(("  Got converter service"));
  
  // When applying stream decoders, it is necessary to "insert" an 
  // intermediate nsDocumentOpenInfo instance to handle the targeting of
  // the "final" stream or streams.
  //
  // For certain content types (ie. multi-part/x-mixed-replace) the input
  // stream is split up into multiple destination streams.  This
  // intermediate instance is used to target these "decoded" streams...
  //
  nsCOMPtr<nsDocumentOpenInfo> nextLink =
    new nsDocumentOpenInfo(m_originalContext, mFlags, mURILoader);
  if (!nextLink) return NS_ERROR_OUT_OF_MEMORY;

  LOG(("  Downstream DocumentOpenInfo would be: 0x%p", nextLink.get()));
  
  // Make sure nextLink starts with the contentListener that said it wanted the
  // results of this decode.
  nextLink->m_contentListener = aListener;
  // Also make sure it has to look for a stream listener to pump data into.
  nextLink->m_targetStreamListener = nsnull;

  // Make sure that nextLink treats the data as aOutContentType when
  // dispatching; that way even if the stream converters don't change the type
  // on the channel we will still do the right thing.  If aOutContentType is
  // */*, that's OK -- that will just indicate to nextLink that it should get
  // the type off the channel.
  nextLink->mContentType = aOutContentType;

  // The following call sets m_targetStreamListener to the input end of the
  // stream converter and sets the output end of the stream converter to
  // nextLink.  As we pump data into m_targetStreamListener the stream
  // converter will convert it and pass the converted data to nextLink.
  return StreamConvService->AsyncConvertData(PromiseFlatCString(aSrcContentType).get(), 
                                             PromiseFlatCString(aOutContentType).get(), 
                                             nextLink, 
                                             request,
                                             getter_AddRefs(m_targetStreamListener));
}

PRBool
nsDocumentOpenInfo::TryContentListener(nsIURIContentListener* aListener,
                                       nsIChannel* aChannel)
{
  LOG(("[0x%p] nsDocumentOpenInfo::TryContentListener; mFlags = 0x%x",
       this, mFlags));

  NS_PRECONDITION(aListener, "Must have a non-null listener");
  NS_PRECONDITION(aChannel, "Must have a channel");
  
  PRBool listenerWantsContent = PR_FALSE;
  nsXPIDLCString typeToUse;
  
  if (mFlags & nsIURILoader::IS_CONTENT_PREFERRED) {
    aListener->IsPreferred(mContentType.get(),
                           getter_Copies(typeToUse),
                           &listenerWantsContent);
  } else {
    aListener->CanHandleContent(mContentType.get(), PR_FALSE,
                                getter_Copies(typeToUse),
                                &listenerWantsContent);
  }
  if (!listenerWantsContent) {
    LOG(("  Listener is not interested"));
    return PR_FALSE;
  }

  if (!typeToUse.IsEmpty() && typeToUse != mContentType) {
    // Need to do a conversion here.

    nsresult rv = ConvertData(aChannel, aListener, mContentType, typeToUse);

    if (NS_FAILED(rv)) {
      // No conversion path -- we don't want this listener, if we got one
      m_targetStreamListener = nsnull;
    }

    LOG(("  Found conversion: %s", m_targetStreamListener ? "yes" : "no"));
    
    // m_targetStreamListener is now the input end of the converter, and we can
    // just pump the data in there, if it exists.  If it does not, we need to
    // try other nsIURIContentListeners.
    return m_targetStreamListener != nsnull;
  }

  // At this point, aListener wants data of type mContentType.  Let 'em have
  // it.  But first, if we are retargeting, set an appropriate flag on the
  // channel
  nsLoadFlags loadFlags = 0;
  aChannel->GetLoadFlags(&loadFlags);

  // Set this flag to indicate that the channel has been targeted at a final
  // consumer.  This load flag is tested in nsDocLoader::OnProgress.
  nsLoadFlags newLoadFlags = nsIChannel::LOAD_TARGETED;

  nsCOMPtr<nsIURIContentListener> originalListener =
    do_GetInterface(m_originalContext);
  if (originalListener != aListener) {
    newLoadFlags |= nsIChannel::LOAD_RETARGETED_DOCUMENT_URI;
  }
  aChannel->SetLoadFlags(loadFlags | newLoadFlags);
  
  PRBool abort = PR_FALSE;
  PRBool isPreferred = (mFlags & nsIURILoader::IS_CONTENT_PREFERRED) != 0;
  nsresult rv = aListener->DoContent(mContentType.get(),
                                     isPreferred,
                                     aChannel,
                                     getter_AddRefs(m_targetStreamListener),
                                     &abort);
    
  if (NS_FAILED(rv)) {
    LOG_ERROR(("  DoContent failed"));
    
    // Unset the RETARGETED_DOCUMENT_URI flag if we set it...
    aChannel->SetLoadFlags(loadFlags);
    m_targetStreamListener = nsnull;
    return PR_FALSE;
  }

  if (abort) {
    // Nothing else to do here -- aListener is handling it all.  Make
    // sure m_targetStreamListener is null so we don't do anything
    // after this point.
    LOG(("  Listener has aborted the load"));
    m_targetStreamListener = nsnull;
  }

  NS_ASSERTION(abort || m_targetStreamListener, "DoContent returned no listener?");

  // aListener is handling the load from this point on.  
  return PR_TRUE;
}


///////////////////////////////////////////////////////////////////////////////////////////////
// Implementation of nsURILoader
///////////////////////////////////////////////////////////////////////////////////////////////

nsURILoader::nsURILoader()
{
#ifdef PR_LOGGING
  if (!mLog) {
    mLog = PR_NewLogModule("URILoader");
  }
#endif
}

nsURILoader::~nsURILoader()
{
}

NS_IMPL_ADDREF(nsURILoader)
NS_IMPL_RELEASE(nsURILoader)

NS_INTERFACE_MAP_BEGIN(nsURILoader)
   NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIURILoader)
   NS_INTERFACE_MAP_ENTRY(nsIURILoader)
NS_INTERFACE_MAP_END

NS_IMETHODIMP nsURILoader::RegisterContentListener(nsIURIContentListener * aContentListener)
{
  nsresult rv = NS_OK;

  nsWeakPtr weakListener = do_GetWeakReference(aContentListener);
  NS_ASSERTION(weakListener, "your URIContentListener must support weak refs!\n");
  
  if (weakListener)
    m_listeners.AppendObject(weakListener);

  return rv;
} 

NS_IMETHODIMP nsURILoader::UnRegisterContentListener(nsIURIContentListener * aContentListener)
{
  nsWeakPtr weakListener = do_GetWeakReference(aContentListener);
  if (weakListener)
    m_listeners.RemoveObject(weakListener);

  return NS_OK;
  
}

NS_IMETHODIMP nsURILoader::OpenURI(nsIChannel *channel, 
                                   PRBool aIsContentPreferred,
                                   nsIInterfaceRequestor *aWindowContext)
{
  NS_ENSURE_ARG_POINTER(channel);

#ifdef PR_LOGGING
  if (LOG_ENABLED()) {
    nsCOMPtr<nsIURI> uri;
    channel->GetURI(getter_AddRefs(uri));
    nsCAutoString spec;
    uri->GetAsciiSpec(spec);
    LOG(("nsURILoader::OpenURI for %s", spec.get()));
  }
#endif

  nsCOMPtr<nsIStreamListener> loader;
  nsresult rv = OpenChannel(channel,
                            aIsContentPreferred ? IS_CONTENT_PREFERRED : 0,
                            aWindowContext,
                            PR_FALSE,
                            getter_AddRefs(loader));

  if (NS_SUCCEEDED(rv)) {
    // this method is not complete!!! Eventually, we should first go
    // to the content listener and ask them for a protocol handler...
    // if they don't give us one, we need to go to the registry and get
    // the preferred protocol handler. 

    // But for now, I'm going to let necko do the work for us....
    rv = channel->AsyncOpen(loader, nsnull);

    // no content from this load - that's OK.
    if (rv == NS_ERROR_NO_CONTENT) {
      LOG(("  rv is NS_ERROR_NO_CONTENT -- doing nothing"));
      rv = NS_OK;
    }
  } else if (rv == NS_ERROR_WONT_HANDLE_CONTENT) {
    // Not really an error, from this method's point of view
    rv = NS_OK;
  }
  return rv;
}

nsresult nsURILoader::OpenChannel(nsIChannel* channel,
                                  PRUint32 aFlags,
                                  nsIInterfaceRequestor* aWindowContext,
                                  PRBool aChannelIsOpen,
                                  nsIStreamListener** aListener)
{
  NS_ASSERTION(channel, "Trying to open a null channel!");
  NS_ASSERTION(aWindowContext, "Window context must not be null");

#ifdef PR_LOGGING
  if (LOG_ENABLED()) {
    nsCOMPtr<nsIURI> uri;
    channel->GetURI(getter_AddRefs(uri));
    nsCAutoString spec;
    uri->GetAsciiSpec(spec);
    LOG(("nsURILoader::OpenChannel for %s", spec.get()));
  }
#endif

  // Let the window context's uriListener know that the open is starting.  This
  // gives that window a chance to abort the load process.
  nsCOMPtr<nsIURIContentListener> winContextListener(do_GetInterface(aWindowContext));
  if (winContextListener) {
    nsCOMPtr<nsIURI> uri;
    channel->GetURI(getter_AddRefs(uri));
    if (uri) {
      PRBool doAbort = PR_FALSE;
      winContextListener->OnStartURIOpen(uri, &doAbort);

      if (doAbort) {
        LOG(("  OnStartURIOpen aborted load"));
        return NS_ERROR_WONT_HANDLE_CONTENT;
      }
    }
  }

  // we need to create a DocumentOpenInfo object which will go ahead and open
  // the url and discover the content type....
  nsCOMPtr<nsDocumentOpenInfo> loader =
    new nsDocumentOpenInfo(aWindowContext, aFlags, this);

  if (!loader) return NS_ERROR_OUT_OF_MEMORY;

  // Set the correct loadgroup on the channel
  nsCOMPtr<nsILoadGroup> loadGroup(do_GetInterface(aWindowContext));

  if (!loadGroup) {
    // XXXbz This context is violating what we'd like to be the new uriloader
    // api.... Set up a nsDocLoader to handle the loadgroup for this context.
    // This really needs to go away!
    nsCOMPtr<nsIURIContentListener> listener(do_GetInterface(aWindowContext));
    if (listener) {
      nsCOMPtr<nsISupports> cookie;
      listener->GetLoadCookie(getter_AddRefs(cookie));
      if (!cookie) {
        nsRefPtr<nsDocLoader> newDocLoader = new nsDocLoader();
        if (!newDocLoader)
          return NS_ERROR_OUT_OF_MEMORY;
        nsresult rv = newDocLoader->Init();
        if (NS_FAILED(rv))
          return rv;
        rv = nsDocLoader::AddDocLoaderAsChildOfRoot(newDocLoader);
        if (NS_FAILED(rv))
          return rv;
        cookie = nsDocLoader::GetAsSupports(newDocLoader);
        listener->SetLoadCookie(cookie);
      }
      loadGroup = do_GetInterface(cookie);
    }
  }

  // If the channel is pending, then we need to remove it from its current
  // loadgroup
  nsCOMPtr<nsILoadGroup> oldGroup;
  channel->GetLoadGroup(getter_AddRefs(oldGroup));
  if (aChannelIsOpen && !SameCOMIdentity(oldGroup, loadGroup)) {
    // It is important to add the channel to the new group before
    // removing it from the old one, so that the load isn't considered
    // done as soon as the request is removed.
    loadGroup->AddRequest(channel, nsnull);

   if (oldGroup) {
      oldGroup->RemoveRequest(channel, nsnull, NS_BINDING_RETARGETED);
    }
  }

  channel->SetLoadGroup(loadGroup);

  // prepare the loader for receiving data
  nsresult rv = loader->Prepare();
  if (NS_SUCCEEDED(rv))
    NS_ADDREF(*aListener = loader);
  return rv;
}

NS_IMETHODIMP nsURILoader::OpenChannel(nsIChannel* channel,
                                       PRUint32 aFlags,
                                       nsIInterfaceRequestor* aWindowContext,
                                       nsIStreamListener** aListener)
{
  PRBool pending;
  if (NS_FAILED(channel->IsPending(&pending))) {
    pending = PR_FALSE;
  }

  return OpenChannel(channel, aFlags, aWindowContext, pending, aListener);
}

NS_IMETHODIMP nsURILoader::Stop(nsISupports* aLoadCookie)
{
  nsresult rv;
  nsCOMPtr<nsIDocumentLoader> docLoader;

  NS_ENSURE_ARG_POINTER(aLoadCookie);

  docLoader = do_GetInterface(aLoadCookie, &rv);
  if (docLoader) {
    rv = docLoader->Stop();
  }
  return rv;
}

