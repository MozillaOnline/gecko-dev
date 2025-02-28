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
 * The Original Code is Mozilla Communicator client code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Robert Churchill <rjc@netscape.com>
 *   David Hyatt <hyatt@netscape.com>
 *   Chris Waterson <waterson@netscape.com>
 *   Pierre Phaneuf <pp@ludusdesign.com>
 *   Joe Hewitt <hewitt@netscape.com>
 *   Neil Deakin <enndeakin@sympatico.ca>
 *   Laurent Jouanneau <laurent.jouanneau@disruptive-innovations.com>
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

/*

  Builds content from a datasource using the XUL <template> tag.

  TO DO

  . Fix ContentTagTest's location in the network construction

  To turn on logging for this module, set:

    NSPR_LOG_MODULES nsXULTemplateBuilder:5

 */

#include "nsCOMPtr.h"
#include "nsCRT.h"
#include "nsFixedSizeAllocator.h"
#include "nsIContent.h"
#include "nsIDOMElement.h"
#include "nsIDOMNode.h"
#include "nsIDOMDocument.h"
#include "nsIDOMXMLDocument.h"
#include "nsIPrivateDOMImplementation.h"
#include "nsIDOMXULElement.h"
#include "nsIDocument.h"
#include "nsBindingManager.h"
#include "nsIDOMNodeList.h"
#include "nsINameSpaceManager.h"
#include "nsIObserverService.h"
#include "nsIRDFCompositeDataSource.h"
#include "nsIRDFInferDataSource.h"
#include "nsIRDFContainerUtils.h"
#include "nsIXULDocument.h"
#include "nsIXULTemplateBuilder.h"
#include "nsIXULBuilderListener.h"
#include "nsIRDFRemoteDataSource.h"
#include "nsIRDFService.h"
#include "nsIScriptGlobalObject.h"
#include "nsIServiceManager.h"
#include "nsISimpleEnumerator.h"
#include "nsISupportsArray.h"
#include "nsIMutableArray.h"
#include "nsIURL.h"
#include "nsIXPConnect.h"
#include "nsContentCID.h"
#include "nsRDFCID.h"
#include "nsXULContentUtils.h"
#include "nsString.h"
#include "nsTArray.h"
#include "nsXPIDLString.h"
#include "nsWhitespaceTokenizer.h"
#include "nsGkAtoms.h"
#include "nsXULElement.h"
#include "jsapi.h"
#include "prlog.h"
#include "rdf.h"
#include "pldhash.h"
#include "plhash.h"
#include "nsIDOMClassInfo.h"
#include "nsPIDOMWindow.h"
#include "nsIConsoleService.h" 
#include "nsNetUtil.h"
#include "nsXULTemplateBuilder.h"
#include "nsXULTemplateQueryProcessorRDF.h"
#include "nsXULTemplateQueryProcessorXML.h"
#include "nsXULTemplateQueryProcessorStorage.h"

//----------------------------------------------------------------------

static NS_DEFINE_CID(kRDFContainerUtilsCID,      NS_RDFCONTAINERUTILS_CID);
static NS_DEFINE_CID(kRDFServiceCID,             NS_RDFSERVICE_CID);

//----------------------------------------------------------------------
//
// nsXULTemplateBuilder
//

nsrefcnt                  nsXULTemplateBuilder::gRefCnt = 0;
nsIRDFService*            nsXULTemplateBuilder::gRDFService;
nsIRDFContainerUtils*     nsXULTemplateBuilder::gRDFContainerUtils;
nsIScriptSecurityManager* nsXULTemplateBuilder::gScriptSecurityManager;
nsIPrincipal*             nsXULTemplateBuilder::gSystemPrincipal;
nsIObserverService*       nsXULTemplateBuilder::gObserverService;

#ifdef PR_LOGGING
PRLogModuleInfo* gXULTemplateLog;
#endif

#define NS_QUERY_PROCESSOR_CONTRACTID_PREFIX "@mozilla.org/xul/xul-query-processor;1?name="

//----------------------------------------------------------------------
//
// nsXULTemplateBuilder methods
//

nsXULTemplateBuilder::nsXULTemplateBuilder(void)
    : mQueriesCompiled(PR_FALSE),
      mFlags(0),
      mTop(nsnull),
      mObservedDocument(nsnull)
{
}

static PLDHashOperator
DestroyMatchList(nsISupports* aKey, nsTemplateMatch* aMatch, void* aContext)
{
    nsFixedSizeAllocator* pool = static_cast<nsFixedSizeAllocator *>(aContext);

    // delete all the matches in the list
    while (aMatch) {
        nsTemplateMatch* next = aMatch->mNext;
        nsTemplateMatch::Destroy(*pool, aMatch, PR_TRUE);
        aMatch = next;
    }

    return PL_DHASH_NEXT;
}

nsXULTemplateBuilder::~nsXULTemplateBuilder(void)
{
    if (--gRefCnt == 0) {
        NS_IF_RELEASE(gRDFService);
        NS_IF_RELEASE(gRDFContainerUtils);
        NS_IF_RELEASE(gSystemPrincipal);
        NS_IF_RELEASE(gScriptSecurityManager);
        NS_IF_RELEASE(gObserverService);
    }

    Uninit(PR_TRUE);
}


nsresult
nsXULTemplateBuilder::InitGlobals()
{
    nsresult rv;

    if (gRefCnt++ == 0) {
        // Initialize the global shared reference to the service
        // manager and get some shared resource objects.
        rv = CallGetService(kRDFServiceCID, &gRDFService);
        if (NS_FAILED(rv))
            return rv;

        rv = CallGetService(kRDFContainerUtilsCID, &gRDFContainerUtils);
        if (NS_FAILED(rv))
            return rv;

        rv = CallGetService(NS_SCRIPTSECURITYMANAGER_CONTRACTID,
                            &gScriptSecurityManager);
        if (NS_FAILED(rv))
            return rv;

        rv = gScriptSecurityManager->GetSystemPrincipal(&gSystemPrincipal);
        if (NS_FAILED(rv))
            return rv;

        rv = CallGetService(NS_OBSERVERSERVICE_CONTRACTID, &gObserverService);
        if (NS_FAILED(rv))
            return rv;
    }

#ifdef PR_LOGGING
    if (! gXULTemplateLog)
        gXULTemplateLog = PR_NewLogModule("nsXULTemplateBuilder");
#endif

    if (!mMatchMap.IsInitialized() && !mMatchMap.Init())
        return NS_ERROR_OUT_OF_MEMORY;

    const size_t bucketsizes[] = { sizeof(nsTemplateMatch) };
    return mPool.Init("nsXULTemplateBuilder", bucketsizes, 1, 256);
}


void
nsXULTemplateBuilder::Uninit(PRBool aIsFinal)
{
    if (mObservedDocument && aIsFinal) {
        gObserverService->RemoveObserver(this, DOM_WINDOW_DESTROYED_TOPIC);
        mObservedDocument->RemoveObserver(this);
        mObservedDocument = nsnull;
    }

    if (mQueryProcessor)
        mQueryProcessor->Done();

    for (PRInt32 q = mQuerySets.Length() - 1; q >= 0; q--) {
        nsTemplateQuerySet* qs = mQuerySets[q];
        delete qs;
    }

    mQuerySets.Clear();

    mMatchMap.EnumerateRead(DestroyMatchList, &mPool);
    mMatchMap.Clear();

    mRootResult = nsnull;
    mRefVariable = nsnull;
    mMemberVariable = nsnull;

    mQueriesCompiled = PR_FALSE;
}

static PLDHashOperator
TraverseMatchList(nsISupports* aKey, nsTemplateMatch* aMatch, void* aContext)
{
    nsCycleCollectionTraversalCallback *cb =
        static_cast<nsCycleCollectionTraversalCallback*>(aContext);

    cb->NoteXPCOMChild(aKey);
    nsTemplateMatch* match = aMatch;
    while (match) {
        cb->NoteXPCOMChild(match->GetContainer());
        cb->NoteXPCOMChild(match->mResult);
        match = match->mNext;
    }

    return PL_DHASH_NEXT;
}

NS_IMPL_CYCLE_COLLECTION_CLASS(nsXULTemplateBuilder)
NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsXULTemplateBuilder)
    NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mDataSource)
    NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mDB)
    NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mCompDB)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsXULTemplateBuilder)
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mDataSource)
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mDB)
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mCompDB)
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mRoot)
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mRootResult)
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMARRAY(mListeners)
    NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mQueryProcessor)
    if (tmp->mMatchMap.IsInitialized())
        tmp->mMatchMap.EnumerateRead(TraverseMatchList, &cb);
    {
      PRUint32 i, count = tmp->mQuerySets.Length();
      for (i = 0; i < count; ++i) {
        nsTemplateQuerySet *set = tmp->mQuerySets[i];
        cb.NoteXPCOMChild(set->mQueryNode);
        cb.NoteXPCOMChild(set->mCompiledQuery);
        PRUint16 j, rulesCount = set->RuleCount();
        for (j = 0; j < rulesCount; ++j) {
          set->GetRuleAt(j)->Traverse(cb);
        }
      }
    }
    tmp->Traverse(cb);
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTING_ADDREF_AMBIGUOUS(nsXULTemplateBuilder,
                                          nsIXULTemplateBuilder)
NS_IMPL_CYCLE_COLLECTING_RELEASE_AMBIGUOUS(nsXULTemplateBuilder,
                                           nsIXULTemplateBuilder)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsXULTemplateBuilder)
  NS_INTERFACE_MAP_ENTRY(nsIXULTemplateBuilder)
  NS_INTERFACE_MAP_ENTRY(nsIDocumentObserver)
  NS_INTERFACE_MAP_ENTRY(nsIMutationObserver)
  NS_INTERFACE_MAP_ENTRY(nsIObserver)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsIXULTemplateBuilder)
  NS_DOM_INTERFACE_MAP_ENTRY_CLASSINFO(XULTemplateBuilder)
NS_INTERFACE_MAP_END

//----------------------------------------------------------------------
//
// nsIXULTemplateBuilder methods
//

NS_IMETHODIMP
nsXULTemplateBuilder::GetRoot(nsIDOMElement** aResult)
{
    if (mRoot) {
        return CallQueryInterface(mRoot, aResult);
    }
    *aResult = nsnull;
    return NS_OK;
}

NS_IMETHODIMP
nsXULTemplateBuilder::GetDatasource(nsISupports** aResult)
{
    if (mCompDB)
        NS_ADDREF(*aResult = mCompDB);
    else
        NS_IF_ADDREF(*aResult = mDataSource);
    return NS_OK;
}

NS_IMETHODIMP
nsXULTemplateBuilder::SetDatasource(nsISupports* aResult)
{
    mDataSource = aResult;
    mCompDB = do_QueryInterface(mDataSource);

    return Rebuild();
}

NS_IMETHODIMP
nsXULTemplateBuilder::GetDatabase(nsIRDFCompositeDataSource** aResult)
{
    NS_IF_ADDREF(*aResult = mCompDB);
    return NS_OK;
}

NS_IMETHODIMP
nsXULTemplateBuilder::GetQueryProcessor(nsIXULTemplateQueryProcessor** aResult)
{
    NS_IF_ADDREF(*aResult = mQueryProcessor.get());
    return NS_OK;
}

NS_IMETHODIMP
nsXULTemplateBuilder::AddRuleFilter(nsIDOMNode* aRule, nsIXULTemplateRuleFilter* aFilter)
{
    if (!aRule || !aFilter)
        return NS_ERROR_NULL_POINTER;

    // a custom rule filter may be added, one for each rule. If a new one is
    // added, it replaces the old one. Look for the right rule and set its
    // filter

    PRInt32 count = mQuerySets.Length();
    for (PRInt32 q = 0; q < count; q++) {
        nsTemplateQuerySet* queryset = mQuerySets[q];

        PRInt16 rulecount = queryset->RuleCount();
        for (PRInt16 r = 0; r < rulecount; r++) {
            nsTemplateRule* rule = queryset->GetRuleAt(r);

            nsCOMPtr<nsIDOMNode> rulenode;
            rule->GetRuleNode(getter_AddRefs(rulenode));
            if (aRule == rulenode) {
                rule->SetRuleFilter(aFilter);
                return NS_OK;
            }
        }
    }

    return NS_OK;
}

NS_IMETHODIMP
nsXULTemplateBuilder::Rebuild()
{
    PRInt32 i;

    for (i = mListeners.Count() - 1; i >= 0; --i) {
        mListeners[i]->WillRebuild(this);
    }

    nsresult rv = RebuildAll();

    for (i = mListeners.Count() - 1; i >= 0; --i) {
        mListeners[i]->DidRebuild(this);
    }

    return rv;
}

NS_IMETHODIMP
nsXULTemplateBuilder::Refresh()
{
    nsresult rv;

    if (!mCompDB)
        return NS_ERROR_FAILURE;

    nsCOMPtr<nsISimpleEnumerator> dslist;
    rv = mCompDB->GetDataSources(getter_AddRefs(dslist));
    NS_ENSURE_SUCCESS(rv, rv);

    PRBool hasMore;
    nsCOMPtr<nsISupports> next;
    nsCOMPtr<nsIRDFRemoteDataSource> rds;

    while(NS_SUCCEEDED(dslist->HasMoreElements(&hasMore)) && hasMore) {
        dslist->GetNext(getter_AddRefs(next));
        if (next && (rds = do_QueryInterface(next))) {
            rds->Refresh(PR_FALSE);
        }
    }

    // XXXbsmedberg: it would be kinda nice to install an async nsIRDFXMLSink
    // observer and call rebuild() once the load is complete. See bug 254600.

    return NS_OK;
}

NS_IMETHODIMP
nsXULTemplateBuilder::Init(nsIContent* aElement)
{
    NS_ENSURE_TRUE(aElement, NS_ERROR_NULL_POINTER);
    mRoot = aElement;

    nsCOMPtr<nsIDocument> doc = mRoot->GetDocument();
    NS_ASSERTION(doc, "element has no document");
    if (! doc)
        return NS_ERROR_UNEXPECTED;

    PRBool shouldDelay;
    nsresult rv = LoadDataSources(doc, &shouldDelay);

    if (NS_SUCCEEDED(rv)) {
        // Add ourselves as a document observer
        doc->AddObserver(this);

        mObservedDocument = doc;
        gObserverService->AddObserver(this, DOM_WINDOW_DESTROYED_TOPIC,
                                      PR_FALSE);
    }

    return rv;
}

NS_IMETHODIMP
nsXULTemplateBuilder::CreateContents(nsIContent* aElement, PRBool aForceCreation)
{
    return NS_OK;
}

NS_IMETHODIMP
nsXULTemplateBuilder::HasGeneratedContent(nsIRDFResource* aResource,
                                          nsIAtom* aTag,
                                          PRBool* aGenerated)
{
    *aGenerated = PR_FALSE;
    return NS_OK;
}

NS_IMETHODIMP
nsXULTemplateBuilder::AddResult(nsIXULTemplateResult* aResult,
                                nsIDOMNode* aQueryNode)
{
    NS_ENSURE_ARG_POINTER(aResult);
    NS_ENSURE_ARG_POINTER(aQueryNode);

    return UpdateResult(nsnull, aResult, aQueryNode);
}

NS_IMETHODIMP
nsXULTemplateBuilder::RemoveResult(nsIXULTemplateResult* aResult)
{
    NS_ENSURE_ARG_POINTER(aResult);

    return UpdateResult(aResult, nsnull, nsnull);
}

NS_IMETHODIMP
nsXULTemplateBuilder::ReplaceResult(nsIXULTemplateResult* aOldResult,
                                    nsIXULTemplateResult* aNewResult,
                                    nsIDOMNode* aQueryNode)
{
    NS_ENSURE_ARG_POINTER(aOldResult);
    NS_ENSURE_ARG_POINTER(aNewResult);
    NS_ENSURE_ARG_POINTER(aQueryNode);

    // just remove the old result and then add a new result separately

    nsresult rv = UpdateResult(aOldResult, nsnull, nsnull);
    if (NS_FAILED(rv))
        return rv;

    return UpdateResult(nsnull, aNewResult, aQueryNode);
}

nsresult
nsXULTemplateBuilder::UpdateResult(nsIXULTemplateResult* aOldResult,
                                   nsIXULTemplateResult* aNewResult,
                                   nsIDOMNode* aQueryNode)
{
    PR_LOG(gXULTemplateLog, PR_LOG_ALWAYS,
           ("nsXULTemplateBuilder::UpdateResult %p %p %p",
           aOldResult, aNewResult, aQueryNode));

    // get the containers where content may be inserted. If
    // GetInsertionLocations returns false, no container has generated
    // any content yet so new content should not be generated either. This
    // will be false if the result applies to content that is in a closed menu
    // or treeitem for example.

    nsAutoPtr<nsCOMArray<nsIContent> > insertionPoints;
    PRBool mayReplace = GetInsertionLocations(aOldResult ? aOldResult : aNewResult,
                                              getter_Transfers(insertionPoints));
    if (! mayReplace)
        return NS_OK;

    nsresult rv = NS_OK;

    nsCOMPtr<nsIRDFResource> oldId, newId;
    nsTemplateQuerySet* queryset = nsnull;

    if (aOldResult) {
        rv = GetResultResource(aOldResult, getter_AddRefs(oldId));
        if (NS_FAILED(rv))
            return rv;

        // Ignore re-entrant builds for content that is currently in our
        // activation stack.
        if (IsActivated(oldId))
            return NS_OK;
    }

    if (aNewResult) {
        rv = GetResultResource(aNewResult, getter_AddRefs(newId));
        if (NS_FAILED(rv))
            return rv;

        // skip results that don't have ids
        if (! newId)
            return NS_OK;

        // Ignore re-entrant builds for content that is currently in our
        // activation stack.
        if (IsActivated(newId))
            return NS_OK;

        // look for the queryset associated with the supplied query node
        nsCOMPtr<nsIContent> querycontent = do_QueryInterface(aQueryNode);

        PRInt32 count = mQuerySets.Length();
        for (PRInt32 q = 0; q < count; q++) {
            nsTemplateQuerySet* qs = mQuerySets[q];
            if (qs->mQueryNode == querycontent) {
                queryset = qs;
                break;
            }
        }

        if (! queryset)
            return NS_OK;
    }

    if (insertionPoints) {
        // iterate over each insertion point and add or remove the result from
        // that container
        PRUint32 count = insertionPoints->Count();
        for (PRUint32 t = 0; t < count; t++) {
            nsCOMPtr<nsIContent> insertionPoint = insertionPoints->SafeObjectAt(t);
            if (insertionPoint) {
                rv = UpdateResultInContainer(aOldResult, aNewResult, queryset,
                                             oldId, newId, insertionPoint);
                if (NS_FAILED(rv))
                    return rv;
            }
        }
    }
    else {
        // The tree builder doesn't use insertion points, so no insertion
        // points will be set. In this case, just update the one result.
        rv = UpdateResultInContainer(aOldResult, aNewResult, queryset,
                                     oldId, newId, nsnull);
    }

    return NS_OK;
}

nsresult
nsXULTemplateBuilder::UpdateResultInContainer(nsIXULTemplateResult* aOldResult,
                                              nsIXULTemplateResult* aNewResult,
                                              nsTemplateQuerySet* aQuerySet,
                                              nsIRDFResource* aOldId,
                                              nsIRDFResource* aNewId,
                                              nsIContent* aInsertionPoint)
{
    // This method takes a result that no longer applies (aOldResult) and
    // replaces it with a new result (aNewResult). Either may be null
    // indicating to just remove a result or add a new one without replacing.
    //
    // Matches are stored in the hashtable mMatchMap, keyed by result id. If
    // there is more than one query, or the same id is found in different
    // containers, the values in the hashtable will be a linked list of all
    // the matches for that id. The matches are sorted according to the
    // queries they are associated with. Matches for earlier queries in the
    // template take priority over matches from later queries. The priority
    // for a match is determined from the match's QuerySetPriority method.
    // The first query has a priority 0, and higher numbers are for later
    // queries with successively higher priorities. Thus, a match takes
    // precedence if it has a lower priority than another. If there is only
    // one query or container, then the match doesn't have any linked items.
    //
    // Matches are nsTemplateMatch objects. They are wrappers around
    // nsIXULTemplateResult result objects and are created with
    // nsTemplateMatch::Create below. The aQuerySet argument specifies which
    // query the match is associated with.
    //
    // When a result id exists in multiple containers, the match's mContainer
    // field is set to the container it corresponds to. The aInsertionPoint
    // argument specifies which container is being updated. Even though they
    // are stored in the same linked list as other matches of the same id, the
    // matches for different containers are treated separately. They are only
    // stored in the same hashtable to avoid a more complex data structure, as
    // the use of the same id in multiple containers isn't a common occurance.
    //
    // Only one match with a given id per container is active at a time. When
    // a match is active, content is generated for it. When a match is
    // inactive, content is not generated for it. A match becomes active if
    // another match with the same id and container with a lower priority
    // isn't already active, and the match has a rule or conditions clause
    // which evaluates to true. The former is checked by comparing the value
    // of the QuerySetPriority method of the match with earlier matches. The
    // latter is checked with the DetermineMatchedRule method.
    //
    // Naturally, if a match with a lower priority is active, it overrides
    // the new match, so the new match is hooked up into the match linked
    // list as inactive, and no content is generated for it. If a match with a
    // higher priority is active, and the new match's conditions evaluate
    // to true, then this existing match with the higher priority needs to have
    // its generated content removed and replaced with the new match's
    // generated content.
    //
    // Similar situations apply when removing an existing match. If the match
    // is active, the existing generated content will need to be removed, and
    // a match of higher priority that is revealed may become active and need
    // to have content generated.
    //
    // Content removal and generation is done by the ReplaceMatch method which
    // is overridden for the content builder and tree builder to update the
    // generated output for each type.
    //
    // The code below handles all of the various cases and ensures that the
    // match lists are maintained properly.

    nsresult rv = NS_OK;
    PRInt16 ruleindex;
    nsTemplateRule* matchedrule = nsnull;

    // Indicates that the old match was active and must have its content
    // removed
    PRBool oldMatchWasActive = PR_FALSE;

    // acceptedmatch will be set to a new match that has to have new content
    // generated for it. If a new match doesn't need to have content
    // generated, (because for example, a match with a lower priority
    // already applies), then acceptedmatch will be null, but the match will
    // be still hooked up into the chain, since it may become active later
    // as other results are updated.
    nsTemplateMatch* acceptedmatch = nsnull;

    // When aOldResult is specified, removematch will be set to the
    // corresponding match. This match needs to be deleted as it no longer
    // applies. However, removedmatch will be null when aOldResult is null, or
    // when no match was found corresponding to aOldResult.
    nsTemplateMatch* removedmatch = nsnull;

    // These will be set when aNewResult is specified indicating to add a
    // result, but will end up replacing an existing match. The former
    // indicates a match being replaced that was active and had content
    // generated for it, while the latter indicates a match that wasn't active
    // and just needs to be deleted. Both may point to different matches. For
    // example, if the new match becomes active, replacing an inactive match,
    // the inactive match will need to be deleted. However, if another match
    // with a higher priority is active, the new match will override it, so
    // content will need to be generated for the new match and removed for
    // this existing active match.
    nsTemplateMatch* replacedmatch = nsnull, * replacedmatchtodelete = nsnull;

    if (aOldResult) {
        nsTemplateMatch* firstmatch;
        if (mMatchMap.Get(aOldId, &firstmatch)) {
            nsTemplateMatch* oldmatch = firstmatch;
            nsTemplateMatch* prevmatch = nsnull;

            // look for the right match if there was more than one
            while (oldmatch && (oldmatch->mResult != aOldResult)) {
                prevmatch = oldmatch;
                oldmatch = oldmatch->mNext;
            }

            if (oldmatch) {
                nsTemplateMatch* findmatch = oldmatch->mNext;

                // Keep a reference so that linked list can be hooked up at
                // the end in case an error occurs.
                nsTemplateMatch* nextmatch = findmatch;

                if (oldmatch->IsActive()) {
                    // Indicate that the old match was active so its content
                    // will be removed later.
                    oldMatchWasActive = PR_TRUE;

                    // The match being removed is the active match, so scan
                    // through the later matches to determine if one should
                    // now become the active match.
                    while (findmatch) {
                        // only other matches with the same container should
                        // now match, leave other containers alone
                        if (findmatch->GetContainer() == aInsertionPoint) {
                            nsTemplateQuerySet* qs =
                                mQuerySets[findmatch->QuerySetPriority()];
                        
                            DetermineMatchedRule(aInsertionPoint, findmatch->mResult,
                                                 qs, &matchedrule, &ruleindex);

                            if (matchedrule) {
                                rv = findmatch->RuleMatched(qs,
                                                            matchedrule, ruleindex,
                                                            findmatch->mResult);
                                if (NS_FAILED(rv))
                                    return rv;

                                acceptedmatch = findmatch;
                                break;
                            }
                        }

                        findmatch = findmatch->mNext;
                    }
                }

                if (oldmatch == firstmatch) {
                    // the match to remove is at the beginning
                    if (oldmatch->mNext) {
                        if (!mMatchMap.Put(aOldId, oldmatch->mNext))
                            return NS_ERROR_OUT_OF_MEMORY;
                    }
                    else {
                        mMatchMap.Remove(aOldId);
                    }
                }

                if (prevmatch)
                    prevmatch->mNext = nextmatch;

                removedmatch = oldmatch;
                if (mFlags & eLoggingEnabled)
                    OutputMatchToLog(aOldId, removedmatch, PR_FALSE);
            }
        }
    }

    nsTemplateMatch *newmatch = nsnull;
    if (aNewResult) {
        // only allow a result to be inserted into containers with a matching tag
        nsIAtom* tag = aQuerySet->GetTag();
        if (aInsertionPoint && tag && tag != aInsertionPoint->Tag())
            return NS_OK;

        PRInt32 findpriority = aQuerySet->Priority();

        newmatch = nsTemplateMatch::Create(mPool, findpriority,
                                           aNewResult, aInsertionPoint);
        if (!newmatch)
            return NS_ERROR_OUT_OF_MEMORY;

        nsTemplateMatch* firstmatch;
        if (mMatchMap.Get(aNewId, &firstmatch)) {
            PRBool hasEarlierActiveMatch = PR_FALSE;

            // Scan through the existing matches to find where the new one
            // should be inserted. oldmatch will be set to the old match for
            // the same query and prevmatch will be set to the match before it.
            nsTemplateMatch* prevmatch = nsnull;
            nsTemplateMatch* oldmatch = firstmatch;
            while (oldmatch) {
                // Break out once we've reached a query in the list with a
                // lower priority. The new match will be inserted at this
                // location so that the match list is sorted by priority.
                PRInt32 priority = oldmatch->QuerySetPriority();
                if (priority > findpriority) {
                    oldmatch = nsnull;
                    break;
                }

                // look for matches that belong in the same container
                if (oldmatch->GetContainer() == aInsertionPoint) {
                    if (priority == findpriority)
                        break;

                    // If a match with a lower priority is active, the new
                    // match can't replace it.
                    if (oldmatch->IsActive())
                        hasEarlierActiveMatch = PR_TRUE;
                }

                prevmatch = oldmatch;
                oldmatch = oldmatch->mNext;
            }

            // At this point, oldmatch will either be null, or set to a match
            // with the same container and priority. If set, oldmatch will
            // need to be replaced by newmatch.

            if (oldmatch)
                newmatch->mNext = oldmatch->mNext;
            else if (prevmatch)
                newmatch->mNext = prevmatch->mNext;
            else
                newmatch->mNext = firstmatch;

            // hasEarlierActiveMatch will be set to true if a match with a
            // lower priority was found. The new match won't replace it in
            // this case. If hasEarlierActiveMatch is false, then the new match
            // may be become active if it matches one of the rules, and will
            // generate output. It's also possible however, that a match with
            // the same priority already exists, which means that the new match
            // will replace the old one. In this case, oldmatch will be set to
            // the old match. The content for the old match must be removed and
            // content for the new match generated in its place.
            if (! hasEarlierActiveMatch) {
                // If the old match was the active match, set replacedmatch to
                // indicate that it needs its content removed.
                if (oldmatch) {
                    if (oldmatch->IsActive())
                        replacedmatch = oldmatch;
                    replacedmatchtodelete = oldmatch;
                }

                // check if the new result matches the rules
                rv = DetermineMatchedRule(aInsertionPoint, newmatch->mResult,
                                          aQuerySet, &matchedrule, &ruleindex);
                if (NS_FAILED(rv)) {
                    nsTemplateMatch::Destroy(mPool, newmatch, PR_FALSE);
                    return rv;
                }

                if (matchedrule) {
                    rv = newmatch->RuleMatched(aQuerySet,
                                               matchedrule, ruleindex,
                                               newmatch->mResult);
                    if (NS_FAILED(rv)) {
                        nsTemplateMatch::Destroy(mPool, newmatch, PR_FALSE);
                        return rv;
                    }

                    // acceptedmatch may have been set in the block handling
                    // aOldResult earlier. If so, we would only get here when
                    // that match has a higher priority than this new match.
                    // As only one match can have content generated for it, it
                    // is OK to set acceptedmatch here to the new match,
                    // ignoring the other one.
                    acceptedmatch = newmatch;

                    // Clear the matched state of the later results for the
                    // same container.
                    nsTemplateMatch* clearmatch = newmatch->mNext;
                    while (clearmatch) {
                        if (clearmatch->GetContainer() == aInsertionPoint &&
                            clearmatch->IsActive()) {
                            clearmatch->SetInactive();
                            // Replacedmatch should be null here. If not, it
                            // means that two matches were active which isn't
                            // a valid state
                            NS_ASSERTION(!replacedmatch,
                                         "replaced match already set");
                            replacedmatch = clearmatch;
                            break;
                        }
                        clearmatch = clearmatch->mNext;
                    }
                }
                else if (oldmatch && oldmatch->IsActive()) {
                    // The result didn't match the rules, so look for a later
                    // one. However, only do this if the old match was the
                    // active match.
                    newmatch = newmatch->mNext;
                    while (newmatch) {
                        if (newmatch->GetContainer() == aInsertionPoint) {
                            rv = DetermineMatchedRule(aInsertionPoint, newmatch->mResult,
                                                      aQuerySet, &matchedrule, &ruleindex);
                            if (NS_FAILED(rv)) {
                                nsTemplateMatch::Destroy(mPool, newmatch,
                                                         PR_FALSE);
                                return rv;
                            }

                            if (matchedrule) {
                                rv = newmatch->RuleMatched(aQuerySet,
                                                           matchedrule, ruleindex,
                                                           newmatch->mResult);
                                if (NS_FAILED(rv)) {
                                    nsTemplateMatch::Destroy(mPool, newmatch,
                                                             PR_FALSE);
                                    return rv;
                                }

                                acceptedmatch = newmatch;
                                break;
                            }
                        }

                        newmatch = newmatch->mNext;
                    }
                }

                // put the match in the map if there isn't a previous match
                if (! prevmatch) {
                    if (!mMatchMap.Put(aNewId, newmatch)) {
                        // The match may have already matched a rule above, so
                        // HasBeenRemoved should be called to indicate that it
                        // is being removed again.
                        nsTemplateMatch::Destroy(mPool, newmatch, PR_TRUE);
                        return rv;
                    }
                }
            }

            // hook up the match last in case an error occurs
            if (prevmatch)
                prevmatch->mNext = newmatch;
        }
        else {
            // The id is not used in the hashtable yet so create a new match
            // and add it to the hashtable.
            rv = DetermineMatchedRule(aInsertionPoint, aNewResult,
                                      aQuerySet, &matchedrule, &ruleindex);
            if (NS_FAILED(rv)) {
                nsTemplateMatch::Destroy(mPool, newmatch, PR_FALSE);
                return rv;
            }

            if (matchedrule) {
                rv = newmatch->RuleMatched(aQuerySet, matchedrule,
                                           ruleindex, aNewResult);
                if (NS_FAILED(rv)) {
                    nsTemplateMatch::Destroy(mPool, newmatch, PR_FALSE);
                    return rv;
                }

                acceptedmatch = newmatch;
            }

            if (!mMatchMap.Put(aNewId, newmatch)) {
                nsTemplateMatch::Destroy(mPool, newmatch, PR_TRUE);
                return NS_ERROR_OUT_OF_MEMORY;
            }
        }
    }

    // The ReplaceMatch method is builder specific and removes the generated
    // content for a match.

    // Remove the content for a match that was active and needs to be replaced.
    if (replacedmatch) {
        rv = ReplaceMatch(replacedmatch->mResult, nsnull, nsnull,
                          aInsertionPoint);

        if (mFlags & eLoggingEnabled)
            OutputMatchToLog(aNewId, replacedmatch, PR_FALSE);
    }
 
    // remove a match that needs to be deleted.
    if (replacedmatchtodelete)
        nsTemplateMatch::Destroy(mPool, replacedmatchtodelete, PR_TRUE);

    // If the old match was active, the content for it needs to be removed.
    // If the old match was not active, it shouldn't have had any content,
    // so just pass null to ReplaceMatch. If acceptedmatch was set, then
    // content needs to be generated for a new match.
    if (oldMatchWasActive || acceptedmatch)
        rv = ReplaceMatch(oldMatchWasActive ? aOldResult : nsnull,
                          acceptedmatch, matchedrule, aInsertionPoint);

    // delete the old match that was replaced
    if (removedmatch)
        nsTemplateMatch::Destroy(mPool, removedmatch, PR_TRUE);

    if (mFlags & eLoggingEnabled && newmatch)
        OutputMatchToLog(aNewId, newmatch, PR_TRUE);

    return rv;
}

NS_IMETHODIMP
nsXULTemplateBuilder::ResultBindingChanged(nsIXULTemplateResult* aResult)
{
    // A binding update is used when only the values of the bindings have
    // changed, so the same rule still applies. Just synchronize the content.
    // The new result will have the new values.
    NS_ENSURE_ARG_POINTER(aResult);

    return SynchronizeResult(aResult);
}

NS_IMETHODIMP
nsXULTemplateBuilder::GetRootResult(nsIXULTemplateResult** aResult)
{
  *aResult = mRootResult;
  NS_IF_ADDREF(*aResult);
  return NS_OK;
}

NS_IMETHODIMP
nsXULTemplateBuilder::GetResultForId(const nsAString& aId,
                                     nsIXULTemplateResult** aResult)
{
    if (aId.IsEmpty())
        return NS_ERROR_INVALID_ARG;

    nsCOMPtr<nsIRDFResource> resource;
    gRDFService->GetUnicodeResource(aId, getter_AddRefs(resource));

    *aResult = nsnull;

    nsTemplateMatch* match;
    if (mMatchMap.Get(resource, &match)) {
        // find the active match
        while (match) {
            if (match->IsActive()) {
                *aResult = match->mResult;
                NS_IF_ADDREF(*aResult);
                break;
            }
            match = match->mNext;
        }
    }

    return NS_OK;
}

NS_IMETHODIMP
nsXULTemplateBuilder::GetResultForContent(nsIDOMElement* aContent,
                                          nsIXULTemplateResult** aResult)
{
    *aResult = nsnull;
    return NS_OK;
}

NS_IMETHODIMP
nsXULTemplateBuilder::AddListener(nsIXULBuilderListener* aListener)
{
    NS_ENSURE_ARG(aListener);

    if (!mListeners.AppendObject(aListener))
        return NS_ERROR_OUT_OF_MEMORY;

    return NS_OK;
}

NS_IMETHODIMP
nsXULTemplateBuilder::RemoveListener(nsIXULBuilderListener* aListener)
{
    NS_ENSURE_ARG(aListener);

    mListeners.RemoveObject(aListener);

    return NS_OK;
}

NS_IMETHODIMP
nsXULTemplateBuilder::Observe(nsISupports* aSubject,
                              const char* aTopic,
                              const PRUnichar* aData)
{
    // Uuuuber hack to clean up circular references that the cycle collector
    // doesn't know about. See bug 394514.
    if (!strcmp(aTopic, DOM_WINDOW_DESTROYED_TOPIC)) {
        nsCOMPtr<nsPIDOMWindow> window = do_QueryInterface(aSubject);
        if (window) {
            nsCOMPtr<nsIDocument> doc =
                do_QueryInterface(window->GetExtantDocument());
            if (doc && doc == mObservedDocument)
                NodeWillBeDestroyed(doc);
        }
    }
    return NS_OK;
}
//----------------------------------------------------------------------
//
// nsIDocumentOberver interface
//

void
nsXULTemplateBuilder::AttributeChanged(nsIDocument* aDocument,
                                       nsIContent*  aContent,
                                       PRInt32      aNameSpaceID,
                                       nsIAtom*     aAttribute,
                                       PRInt32      aModType)
{
    if (aContent == mRoot && aNameSpaceID == kNameSpaceID_None) {
        // Check for a change to the 'ref' attribute on an atom, in which
        // case we may need to nuke and rebuild the entire content model
        // beneath the element.
        if (aAttribute == nsGkAtoms::ref)
            nsContentUtils::AddScriptRunner(
                NS_NEW_RUNNABLE_METHOD(nsXULTemplateBuilder, this,
                                       RunnableRebuild));

        // Check for a change to the 'datasources' attribute. If so, setup
        // mDB by parsing the new value and rebuild.
        else if (aAttribute == nsGkAtoms::datasources) {
            Uninit(PR_FALSE);  // Reset results
            
            PRBool shouldDelay;
            LoadDataSources(aDocument, &shouldDelay);
            if (!shouldDelay)
                nsContentUtils::AddScriptRunner(
                    NS_NEW_RUNNABLE_METHOD(nsXULTemplateBuilder, this,
                                           RunnableRebuild));
        }
    }
}

void
nsXULTemplateBuilder::ContentRemoved(nsIDocument* aDocument,
                                     nsIContent* aContainer,
                                     nsIContent* aChild,
                                     PRInt32 aIndexInContainer)
{
    if (mRoot && nsContentUtils::ContentIsDescendantOf(mRoot, aChild)) {
        nsRefPtr<nsXULTemplateBuilder> kungFuDeathGrip(this);

        if (mQueryProcessor)
            mQueryProcessor->Done();

        // use false since content is going away anyway
        Uninit(PR_FALSE);

        aDocument->RemoveObserver(this);

        nsCOMPtr<nsIXULDocument> xuldoc = do_QueryInterface(aDocument);
        if (xuldoc)
            xuldoc->SetTemplateBuilderFor(mRoot, nsnull);

        // clear the template state when removing content so that template
        // content will be regenerated again if the content is reinserted
        nsXULElement *xulcontent = nsXULElement::FromContent(mRoot);
        if (xulcontent)
            xulcontent->ClearTemplateGenerated();

        mDB = nsnull;
        mCompDB = nsnull;
        mRoot = nsnull;
        mDataSource = nsnull;
    }
}

void
nsXULTemplateBuilder::NodeWillBeDestroyed(const nsINode* aNode)
{
    // The call to RemoveObserver could release the last reference to
    // |this|, so hold another reference.
    nsRefPtr<nsXULTemplateBuilder> kungFuDeathGrip(this);

    // Break circular references
    if (mQueryProcessor)
        mQueryProcessor->Done();

    mDataSource = nsnull;
    mDB = nsnull;
    mCompDB = nsnull;
    mRoot = nsnull;

    Uninit(PR_TRUE);
}




//----------------------------------------------------------------------
//
// Implementation methods
//

nsresult
nsXULTemplateBuilder::LoadDataSources(nsIDocument* aDocument,
                                      PRBool* aShouldDelayBuilding)
{
    NS_PRECONDITION(mRoot != nsnull, "not initialized");

    nsresult rv;
    PRBool isRDFQuery = PR_FALSE;
  
    // we'll set these again later, after we create a new composite ds
    mDB = nsnull;
    mCompDB = nsnull;
    mDataSource = nsnull;

    *aShouldDelayBuilding = PR_FALSE;

    nsAutoString datasources;
    mRoot->GetAttr(kNameSpaceID_None, nsGkAtoms::datasources, datasources);

    nsAutoString querytype;
    mRoot->GetAttr(kNameSpaceID_None, nsGkAtoms::querytype, querytype);

    // create the query processor. The querytype attribute on the root element
    // may be used to create one of a specific type.
  
    // XXX should non-chrome be restricted to specific names?
    if (querytype.IsEmpty())
        querytype.AssignLiteral("rdf");

    if (querytype.EqualsLiteral("rdf")) {
        isRDFQuery = PR_TRUE;
        mQueryProcessor = new nsXULTemplateQueryProcessorRDF();
        NS_ENSURE_TRUE(mQueryProcessor, NS_ERROR_OUT_OF_MEMORY);
    }
    else if (querytype.EqualsLiteral("xml")) {
        mQueryProcessor = new nsXULTemplateQueryProcessorXML();
        NS_ENSURE_TRUE(mQueryProcessor, NS_ERROR_OUT_OF_MEMORY);
    }
    else if (querytype.EqualsLiteral("storage")) {
        mQueryProcessor = new nsXULTemplateQueryProcessorStorage();
        NS_ENSURE_TRUE(mQueryProcessor, NS_ERROR_OUT_OF_MEMORY);
    }
    else {
        nsCAutoString cid(NS_QUERY_PROCESSOR_CONTRACTID_PREFIX);
        AppendUTF16toUTF8(querytype, cid);
        mQueryProcessor = do_CreateInstance(cid.get(), &rv);

        if (!mQueryProcessor) {
            nsXULContentUtils::LogTemplateError(ERROR_TEMPLATE_INVALID_QUERYPROCESSOR);
            return rv;
        }
    }

    rv = LoadDataSourceUrls(aDocument, datasources,
                            isRDFQuery, aShouldDelayBuilding);
    NS_ENSURE_SUCCESS(rv, rv);

    // Now set the database on the element, so that script writers can
    // access it.
    nsCOMPtr<nsIXULDocument> xuldoc = do_QueryInterface(aDocument);
    if (xuldoc)
        xuldoc->SetTemplateBuilderFor(mRoot, this);

    if (!mRoot->IsXUL()) {
        // Hmm. This must be an HTML element. Try to set it as a
        // JS property "by hand".
        InitHTMLTemplateRoot();
    }
  
    return NS_OK;
}
  
nsresult
nsXULTemplateBuilder::LoadDataSourceUrls(nsIDocument* aDocument,
                                         const nsAString& aDataSources,
                                         PRBool aIsRDFQuery,
                                         PRBool* aShouldDelayBuilding)
{
    // Grab the doc's principal...
    nsIPrincipal *docPrincipal = aDocument->NodePrincipal();

    NS_ASSERTION(docPrincipal == mRoot->NodePrincipal(),
                 "Principal mismatch?  Which one to use?");

    PRBool isTrusted = PR_FALSE;
    nsresult rv = IsSystemPrincipal(docPrincipal, &isTrusted);
    NS_ENSURE_SUCCESS(rv, rv);

    // Parse datasources: they are assumed to be a whitespace
    // separated list of URIs; e.g.,
    //
    //     rdf:bookmarks rdf:history http://foo.bar.com/blah.cgi?baz=9
    //
    nsIURI *docurl = aDocument->GetDocumentURI();

    nsCOMPtr<nsIMutableArray> uriList = do_CreateInstance(NS_ARRAY_CONTRACTID);
    if (!uriList)
        return NS_ERROR_FAILURE;

    nsAutoString datasources(aDataSources);
    PRUint32 first = 0;
    while (1) {
        while (first < datasources.Length() && nsCRT::IsAsciiSpace(datasources.CharAt(first)))
            ++first;

        if (first >= datasources.Length())
            break;

        PRUint32 last = first;
        while (last < datasources.Length() && !nsCRT::IsAsciiSpace(datasources.CharAt(last)))
            ++last;

        nsAutoString uriStr;
        datasources.Mid(uriStr, first, last - first);
        first = last + 1;

        // A special 'dummy' datasource
        if (uriStr.EqualsLiteral("rdf:null"))
            continue;

        if (uriStr.CharAt(0) == '#') {
            // ok, the datasource is certainly a node of the current document
            nsCOMPtr<nsIDOMDocument> domdoc = do_QueryInterface(aDocument);
            nsCOMPtr<nsIDOMElement> dsnode;

            domdoc->GetElementById(Substring(uriStr, 1),
                                   getter_AddRefs(dsnode));

            if (dsnode)
                uriList->AppendElement(dsnode, PR_FALSE);
            continue;
        }

        // N.B. that `failure' (e.g., because it's an unknown
        // protocol) leaves uriStr unaltered.
        NS_MakeAbsoluteURI(uriStr, uriStr, docurl);

        nsCOMPtr<nsIURI> uri;
        rv = NS_NewURI(getter_AddRefs(uri), uriStr);
        if (NS_FAILED(rv) || !uri)
            continue; // Necko will barf if our URI is weird

        // don't add the uri to the list if the document is not allowed to
        // load it
        if (!isTrusted && NS_FAILED(docPrincipal->CheckMayLoad(uri, PR_TRUE)))
          continue;

        uriList->AppendElement(uri, PR_FALSE);
    }

    nsCOMPtr<nsIDOMNode> rootNode = do_QueryInterface(mRoot);
    rv = mQueryProcessor->GetDatasource(uriList,
                                        rootNode,
                                        isTrusted,
                                        this,
                                        aShouldDelayBuilding,
                                        getter_AddRefs(mDataSource));
    NS_ENSURE_SUCCESS(rv, rv);

    if (aIsRDFQuery && mDataSource) {  
        // check if we were given an inference engine type
        nsCOMPtr<nsIRDFInferDataSource> inferDB = do_QueryInterface(mDataSource);
        if (inferDB) {
            nsCOMPtr<nsIRDFDataSource> ds;
            inferDB->GetBaseDataSource(getter_AddRefs(ds));
            if (ds)
                mCompDB = do_QueryInterface(ds);
        }

        if (!mCompDB)
            mCompDB = do_QueryInterface(mDataSource);

        mDB = do_QueryInterface(mDataSource);
    }

    if (!mDB && isTrusted) {
        gRDFService->GetDataSource("rdf:local-store", getter_AddRefs(mDB));
    }

    return NS_OK;
}

nsresult
nsXULTemplateBuilder::InitHTMLTemplateRoot()
{
    // Use XPConnect and the JS APIs to whack mDB and this as the
    // 'database' and 'builder' properties onto aElement.
    nsresult rv;

    nsCOMPtr<nsIDocument> doc = mRoot->GetDocument();
    NS_ASSERTION(doc, "no document");
    if (! doc)
        return NS_ERROR_UNEXPECTED;

    nsIScriptGlobalObject *global = doc->GetScriptGlobalObject();
    if (! global)
        return NS_ERROR_UNEXPECTED;

    JSObject *scope = global->GetGlobalJSObject();

    nsIScriptContext *context = global->GetContext();
    if (! context)
        return NS_ERROR_UNEXPECTED;

    JSContext* jscontext = reinterpret_cast<JSContext*>(context->GetNativeContext());
    NS_ASSERTION(context != nsnull, "no jscontext");
    if (! jscontext)
        return NS_ERROR_UNEXPECTED;

    JSAutoRequest ar(jscontext);

    jsval v;
    nsCOMPtr<nsIXPConnectJSObjectHolder> wrapper;
    rv = nsContentUtils::WrapNative(jscontext, scope, mRoot,
                                    &NS_GET_IID(nsIDOMElement), &v,
                                    getter_AddRefs(wrapper));
    NS_ENSURE_SUCCESS(rv, rv);

    JSObject* jselement = JSVAL_TO_OBJECT(v);

    if (mDB) {
        // database
        jsval jsdatabase;
        rv = nsContentUtils::WrapNative(jscontext, scope, mDB,
                                        &NS_GET_IID(nsIRDFCompositeDataSource),
                                        &jsdatabase, getter_AddRefs(wrapper));
        NS_ENSURE_SUCCESS(rv, rv);

        PRBool ok;
        ok = JS_SetProperty(jscontext, jselement, "database", &jsdatabase);
        NS_ASSERTION(ok, "unable to set database property");
        if (! ok)
            return NS_ERROR_FAILURE;
    }

    {
        // builder
        jsval jsbuilder;
        nsCOMPtr<nsIXPConnectJSObjectHolder> wrapper;
        rv = nsContentUtils::WrapNative(jscontext, jselement,
                                        static_cast<nsIXULTemplateBuilder*>(this),
                                        &NS_GET_IID(nsIXULTemplateBuilder),
                                        &jsbuilder, getter_AddRefs(wrapper));
        NS_ENSURE_SUCCESS(rv, rv);

        PRBool ok;
        ok = JS_SetProperty(jscontext, jselement, "builder", &jsbuilder);
        if (! ok)
            return NS_ERROR_FAILURE;
    }

    return NS_OK;
}

nsresult
nsXULTemplateBuilder::DetermineMatchedRule(nsIContent *aContainer,
                                           nsIXULTemplateResult* aResult,
                                           nsTemplateQuerySet* aQuerySet,
                                           nsTemplateRule** aMatchedRule,
                                           PRInt16 *aRuleIndex)
{
    // iterate through the rules and look for one that the result matches
    PRInt16 count = aQuerySet->RuleCount();
    for (PRInt16 r = 0; r < count; r++) {
        nsTemplateRule* rule = aQuerySet->GetRuleAt(r);
        // If a tag was specified, it must match the tag of the container
        // where content is being inserted.
        nsIAtom* tag = rule->GetTag();
        if ((!aContainer || !tag || tag == aContainer->Tag()) &&
            rule->CheckMatch(aResult)) {
            *aMatchedRule = rule;
            *aRuleIndex = r;
            return NS_OK;
        }
    }

    *aRuleIndex = -1;
    *aMatchedRule = nsnull;
    return NS_OK;
}

void
nsXULTemplateBuilder::ParseAttribute(const nsAString& aAttributeValue,
                                     void (*aVariableCallback)(nsXULTemplateBuilder*, const nsAString&, void*),
                                     void (*aTextCallback)(nsXULTemplateBuilder*, const nsAString&, void*),
                                     void* aClosure)
{
    nsAString::const_iterator done_parsing;
    aAttributeValue.EndReading(done_parsing);

    nsAString::const_iterator iter;
    aAttributeValue.BeginReading(iter);

    nsAString::const_iterator mark(iter), backup(iter);

    for (; iter != done_parsing; backup = ++iter) {
        // A variable is either prefixed with '?' (in the extended
        // syntax) or "rdf:" (in the simple syntax).
        PRBool isvar;
        if (*iter == PRUnichar('?') && (++iter != done_parsing)) {
            isvar = PR_TRUE;
        }
        else if ((*iter == PRUnichar('r') && (++iter != done_parsing)) &&
                 (*iter == PRUnichar('d') && (++iter != done_parsing)) &&
                 (*iter == PRUnichar('f') && (++iter != done_parsing)) &&
                 (*iter == PRUnichar(':') && (++iter != done_parsing))) {
            isvar = PR_TRUE;
        }
        else {
            isvar = PR_FALSE;
        }

        if (! isvar) {
            // It's not a variable, or we ran off the end of the
            // string after the initial variable prefix. Since we may
            // have slurped down some characters before realizing that
            // fact, back up to the point where we started.
            iter = backup;
            continue;
        }
        else if (backup != mark && aTextCallback) {
            // Okay, we've found a variable, and there's some vanilla
            // text that's been buffered up. Flush it.
            (*aTextCallback)(this, Substring(mark, backup), aClosure);
        }

        if (*iter == PRUnichar('?')) {
            // Well, it was not really a variable, but "??". We use one
            // question mark (the second one, actually) literally.
            mark = iter;
            continue;
        }

        // Construct a substring that is the symbol we need to look up
        // in the rule's symbol table. The symbol is terminated by a
        // space character, a caret, or the end of the string,
        // whichever comes first.
        nsAString::const_iterator first(backup);

        PRUnichar c = 0;
        while (iter != done_parsing) {
            c = *iter;
            if ((c == PRUnichar(' ')) || (c == PRUnichar('^')))
                break;

            ++iter;
        }

        nsAString::const_iterator last(iter);

        // Back up so we don't consume the terminating character
        // *unless* the terminating character was a caret: the caret
        // means "concatenate with no space in between".
        if (c != PRUnichar('^'))
            --iter;

        (*aVariableCallback)(this, Substring(first, last), aClosure);
        mark = iter;
        ++mark;
    }

    if (backup != mark && aTextCallback) {
        // If there's any text left over, then fire the text callback
        (*aTextCallback)(this, Substring(mark, backup), aClosure);
    }
}


struct NS_STACK_CLASS SubstituteTextClosure {
    SubstituteTextClosure(nsIXULTemplateResult* aResult, nsAString& aString)
        : result(aResult), str(aString) {}

    // some datasources are lazily initialized or modified while values are
    // being retrieved, causing results to be removed. Due to this, hold a
    // strong reference to the result.
    nsCOMPtr<nsIXULTemplateResult> result;
    nsAString& str;
};

nsresult
nsXULTemplateBuilder::SubstituteText(nsIXULTemplateResult* aResult,
                                     const nsAString& aAttributeValue,
                                     nsAString& aString)
{
    // See if it's the special value "..."
    if (aAttributeValue.EqualsLiteral("...")) {
        aResult->GetId(aString);
        return NS_OK;
    }

    // Reasonable guess at how big it should be
    aString.SetCapacity(aAttributeValue.Length());

    SubstituteTextClosure closure(aResult, aString);
    ParseAttribute(aAttributeValue,
                   SubstituteTextReplaceVariable,
                   SubstituteTextAppendText,
                   &closure);

    return NS_OK;
}


void
nsXULTemplateBuilder::SubstituteTextAppendText(nsXULTemplateBuilder* aThis,
                                               const nsAString& aText,
                                               void* aClosure)
{
    // Append aString to the closure's result
    SubstituteTextClosure* c = static_cast<SubstituteTextClosure*>(aClosure);
    c->str.Append(aText);
}

void
nsXULTemplateBuilder::SubstituteTextReplaceVariable(nsXULTemplateBuilder* aThis,
                                                    const nsAString& aVariable,
                                                    void* aClosure)
{
    // Substitute the value for the variable and append to the
    // closure's result.
    SubstituteTextClosure* c = static_cast<SubstituteTextClosure*>(aClosure);

    nsAutoString replacementText;

    // The symbol "rdf:*" is special, and means "this guy's URI"
    if (aVariable.EqualsLiteral("rdf:*")){
        c->result->GetId(replacementText);
    }
    else {
        // Got a variable; get the value it's assigned to
        nsCOMPtr<nsIAtom> var = do_GetAtom(aVariable);
        c->result->GetBindingFor(var, replacementText);
    }

    c->str += replacementText;
}

PRBool
nsXULTemplateBuilder::IsTemplateElement(nsIContent* aContent)
{
    return aContent->NodeInfo()->Equals(nsGkAtoms::_template,
                                        kNameSpaceID_XUL);
}

nsresult
nsXULTemplateBuilder::GetTemplateRoot(nsIContent** aResult)
{
    NS_PRECONDITION(mRoot != nsnull, "not initialized");
    if (! mRoot)
        return NS_ERROR_NOT_INITIALIZED;

    // First, check and see if the root has a template attribute. This
    // allows a template to be specified "out of line"; e.g.,
    //
    //   <window>
    //     <foo template="MyTemplate">...</foo>
    //     <template id="MyTemplate">...</template>
    //   </window>
    //
    nsAutoString templateID;
    mRoot->GetAttr(kNameSpaceID_None, nsGkAtoms::_template, templateID);

    if (! templateID.IsEmpty()) {
        nsCOMPtr<nsIDOMDocument> domDoc = do_QueryInterface(mRoot->GetDocument());
        if (! domDoc)
            return NS_OK;

        nsCOMPtr<nsIDOMElement> domElement;
        domDoc->GetElementById(templateID, getter_AddRefs(domElement));

        if (domElement) {
            nsCOMPtr<nsIContent> content = do_QueryInterface(domElement);
            NS_ENSURE_STATE(content &&
                            !nsContentUtils::ContentIsDescendantOf(mRoot,
                                                                   content));
            content.forget(aResult);
            return NS_OK;
        }
    }

#if 1 // XXX hack to workaround bug with XBL insertion/removal?
    {
        // If root node has no template attribute, then look for a child
        // node which is a template tag
        PRUint32 count = mRoot->GetChildCount();

        for (PRUint32 i = 0; i < count; ++i) {
            nsIContent *child = mRoot->GetChildAt(i);

            if (IsTemplateElement(child)) {
                NS_ADDREF(*aResult = child);
                return NS_OK;
            }
        }
    }
#endif

    // If we couldn't find a real child, look through the anonymous
    // kids, too.
    nsCOMPtr<nsIDocument> doc = mRoot->GetDocument();
    if (! doc)
        return NS_OK;

    nsCOMPtr<nsIDOMNodeList> kids;
    doc->BindingManager()->GetXBLChildNodesFor(mRoot, getter_AddRefs(kids));

    if (kids) {
        PRUint32 length;
        kids->GetLength(&length);

        for (PRUint32 i = 0; i < length; ++i) {
            nsCOMPtr<nsIDOMNode> node;
            kids->Item(i, getter_AddRefs(node));
            if (! node)
                continue;

            nsCOMPtr<nsIContent> child = do_QueryInterface(node);

            if (IsTemplateElement(child)) {
                NS_ADDREF(*aResult = child.get());
                return NS_OK;
            }
        }
    }

    *aResult = nsnull;
    return NS_OK;
}

nsresult
nsXULTemplateBuilder::CompileQueries()
{
    nsCOMPtr<nsIContent> tmpl;
    GetTemplateRoot(getter_AddRefs(tmpl));
    if (! tmpl)
        return NS_OK;

    if (! mRoot)
        return NS_ERROR_NOT_INITIALIZED;

    // Determine if there are any special settings we need to observe
    mFlags = 0;

    nsAutoString flags;
    mRoot->GetAttr(kNameSpaceID_None, nsGkAtoms::flags, flags);

    // if the dont-test-empty flag is set, containers should not be checked to
    // see if they are empty. If dont-recurse is set, then don't process the
    // template recursively and only show one level of results. The logging
    // flag logs errors and results to the console, which is useful when
    // debugging templates.
    nsWhitespaceTokenizer tokenizer(flags);
    while (tokenizer.hasMoreTokens()) {
      const nsDependentSubstring& token(tokenizer.nextToken());
      if (token.EqualsLiteral("dont-test-empty"))
        mFlags |= eDontTestEmpty;
      else if (token.EqualsLiteral("dont-recurse"))
        mFlags |= eDontRecurse;
      else if (token.EqualsLiteral("logging"))
        mFlags |= eLoggingEnabled;
    }

#ifdef PR_LOGGING
    // always enable logging if the debug setting is used
    if (PR_LOG_TEST(gXULTemplateLog, PR_LOG_DEBUG))
        mFlags |= eLoggingEnabled;
#endif

    nsCOMPtr<nsIDOMNode> rootnode = do_QueryInterface(mRoot);
    nsresult rv =
        mQueryProcessor->InitializeForBuilding(mDataSource, this, rootnode);
    if (NS_FAILED(rv))
        return rv;

    // Set the "container" and "member" variables, if the user has specified
    // them. The container variable may be specified with the container
    // attribute on the <template> and the member variable may be specified
    // using the member attribute or the value of the uri attribute inside the
    // first action body in the template. If not specified, the container
    // variable defaults to '?uri' and the member variable defaults to '?' or
    // 'rdf:*' for simple queries.

    // For RDF queries, the container variable may also be set via the
    // <content> tag.

    nsAutoString containervar;
    tmpl->GetAttr(kNameSpaceID_None, nsGkAtoms::container, containervar);

    if (containervar.IsEmpty())
        mRefVariable = do_GetAtom("?uri");
    else
        mRefVariable = do_GetAtom(containervar);

    nsAutoString membervar;
    tmpl->GetAttr(kNameSpaceID_None, nsGkAtoms::member, membervar);

    if (membervar.IsEmpty())
        mMemberVariable = nsnull;
    else
        mMemberVariable = do_GetAtom(membervar);

    nsTemplateQuerySet* queryset = new nsTemplateQuerySet(0);
    if (!queryset)
        return NS_ERROR_OUT_OF_MEMORY;

    if (!mQuerySets.AppendElement(queryset)) {
        delete queryset;
        return NS_ERROR_OUT_OF_MEMORY;
    }

    PRBool canUseTemplate = PR_FALSE;
    PRInt32 priority = 0;
    rv = CompileTemplate(tmpl, queryset, PR_FALSE, &priority, &canUseTemplate);

    if (NS_FAILED(rv) || !canUseTemplate) {
        for (PRInt32 q = mQuerySets.Length() - 1; q >= 0; q--) {
            nsTemplateQuerySet* qs = mQuerySets[q];
            delete qs;
        }
        mQuerySets.Clear();
    }

    mQueriesCompiled = PR_TRUE;

    return NS_OK;
}

nsresult
nsXULTemplateBuilder::CompileTemplate(nsIContent* aTemplate,
                                      nsTemplateQuerySet* aQuerySet,
                                      PRBool aIsQuerySet,
                                      PRInt32* aPriority,
                                      PRBool* aCanUseTemplate)
{
    NS_ASSERTION(aQuerySet, "No queryset supplied");

    nsresult rv = NS_OK;

    PRBool isQuerySetMode = PR_FALSE;
    PRBool hasQuerySet = PR_FALSE, hasRule = PR_FALSE, hasQuery = PR_FALSE;

    PRUint32 count = aTemplate->GetChildCount();

    for (PRUint32 i = 0; i < count; i++) {
        nsIContent *rulenode = aTemplate->GetChildAt(i);
        nsINodeInfo *ni = rulenode->NodeInfo();

        // don't allow more queries than can be supported
        if (*aPriority == PR_INT16_MAX)
            return NS_ERROR_FAILURE;

        // XXXndeakin queryset isn't a good name for this tag since it only
        //            ever contains one query
        if (!aIsQuerySet && ni->Equals(nsGkAtoms::queryset, kNameSpaceID_XUL)) {
            if (hasRule || hasQuery) {
              nsXULContentUtils::LogTemplateError(ERROR_TEMPLATE_INVALID_QUERYSET);
              continue;
            }

            isQuerySetMode = PR_TRUE;

            // only create a queryset for those after the first since the
            // first one is always created by CompileQueries
            if (hasQuerySet) {
                aQuerySet = new nsTemplateQuerySet(++*aPriority);
                if (!aQuerySet)
                    return NS_ERROR_OUT_OF_MEMORY;

                // once the queryset is appended to the mQuerySets list, it
                // will be removed by CompileQueries if an error occurs
                if (!mQuerySets.AppendElement(aQuerySet)) {
                    delete aQuerySet;
                    return NS_ERROR_OUT_OF_MEMORY;
                }
            }

            hasQuerySet = PR_TRUE;

            rv = CompileTemplate(rulenode, aQuerySet, PR_TRUE, aPriority, aCanUseTemplate);
            if (NS_FAILED(rv))
                return rv;
        }

        // once a queryset is used, everything must be a queryset
        if (isQuerySetMode)
            continue;

        if (ni->Equals(nsGkAtoms::rule, kNameSpaceID_XUL)) {
            nsCOMPtr<nsIContent> action;
            nsXULContentUtils::FindChildByTag(rulenode,
                                              kNameSpaceID_XUL,
                                              nsGkAtoms::action,
                                              getter_AddRefs(action));

            if (action){
                nsCOMPtr<nsIAtom> memberVariable = mMemberVariable;
                if (!memberVariable) {
                    memberVariable = DetermineMemberVariable(action);
                    if (!memberVariable) {
                        nsXULContentUtils::LogTemplateError(ERROR_TEMPLATE_NO_MEMBERVAR);
                        continue;
                    }
                }

                if (hasQuery) {
                    nsCOMPtr<nsIAtom> tag;
                    DetermineRDFQueryRef(aQuerySet->mQueryNode,
                                         getter_AddRefs(tag));
                    if (tag)
                        aQuerySet->SetTag(tag);

                    if (! aQuerySet->mCompiledQuery) {
                        nsCOMPtr<nsIDOMNode> query(do_QueryInterface(aQuerySet->mQueryNode));

                        rv = mQueryProcessor->CompileQuery(this, query,
                                                           mRefVariable, memberVariable,
                                                           getter_AddRefs(aQuerySet->mCompiledQuery));
                        if (NS_FAILED(rv))
                            return rv;
                    }

                    if (aQuerySet->mCompiledQuery) {
                        rv = CompileExtendedQuery(rulenode, action, memberVariable,
                                                  aQuerySet);
                        if (NS_FAILED(rv))
                            return rv;

                        *aCanUseTemplate = PR_TRUE;
                    }
                }
                else {
                    // backwards-compatible RDF template syntax where there is
                    // an <action> node but no <query> node. In this case,
                    // use the conditions as if it was the query.

                    nsCOMPtr<nsIContent> conditions;
                    nsXULContentUtils::FindChildByTag(rulenode,
                                                      kNameSpaceID_XUL,
                                                      nsGkAtoms::conditions,
                                                      getter_AddRefs(conditions));

                    if (conditions) {
                        // create a new queryset if one hasn't been created already
                        if (hasQuerySet) {
                            aQuerySet = new nsTemplateQuerySet(++*aPriority);
                            if (! aQuerySet)
                                return NS_ERROR_OUT_OF_MEMORY;

                            if (!mQuerySets.AppendElement(aQuerySet)) {
                                delete aQuerySet;
                                return NS_ERROR_OUT_OF_MEMORY;
                            }
                        }

                        nsCOMPtr<nsIAtom> tag;
                        DetermineRDFQueryRef(conditions, getter_AddRefs(tag));
                        if (tag)
                            aQuerySet->SetTag(tag);

                        hasQuerySet = PR_TRUE;

                        nsCOMPtr<nsIDOMNode> conditionsnode(do_QueryInterface(conditions));

                        aQuerySet->mQueryNode = conditions;
                        rv = mQueryProcessor->CompileQuery(this, conditionsnode,
                                                           mRefVariable,
                                                           memberVariable,
                                                           getter_AddRefs(aQuerySet->mCompiledQuery));
                        if (NS_FAILED(rv))
                            return rv;

                        if (aQuerySet->mCompiledQuery) {
                            rv = CompileExtendedQuery(rulenode, action, memberVariable,
                                                      aQuerySet);
                            if (NS_FAILED(rv))
                                return rv;

                            *aCanUseTemplate = PR_TRUE;
                        }
                    }
                }
            }
            else {
                if (hasQuery)
                    continue;

                // a new queryset must always be created in this case
                if (hasQuerySet) {
                    aQuerySet = new nsTemplateQuerySet(++*aPriority);
                    if (! aQuerySet)
                        return NS_ERROR_OUT_OF_MEMORY;

                    if (!mQuerySets.AppendElement(aQuerySet)) {
                        delete aQuerySet;
                        return NS_ERROR_OUT_OF_MEMORY;
                    }
                }

                hasQuerySet = PR_TRUE;

                rv = CompileSimpleQuery(rulenode, aQuerySet, aCanUseTemplate);
                if (NS_FAILED(rv))
                    return rv;
            }

            hasRule = PR_TRUE;
        }
        else if (ni->Equals(nsGkAtoms::query, kNameSpaceID_XUL)) {
            if (hasQuery)
              continue;

            aQuerySet->mQueryNode = rulenode;
            hasQuery = PR_TRUE;
        }
        else if (ni->Equals(nsGkAtoms::action, kNameSpaceID_XUL)) {
            // the query must appear before the action
            if (! hasQuery)
                continue;

            nsCOMPtr<nsIAtom> tag;
            DetermineRDFQueryRef(aQuerySet->mQueryNode, getter_AddRefs(tag));
            if (tag)
                aQuerySet->SetTag(tag);

            nsCOMPtr<nsIAtom> memberVariable = mMemberVariable;
            if (!memberVariable) {
                memberVariable = DetermineMemberVariable(rulenode);
                if (!memberVariable) {
                    nsXULContentUtils::LogTemplateError(ERROR_TEMPLATE_NO_MEMBERVAR);
                    continue;
                }
            }

            nsCOMPtr<nsIDOMNode> query(do_QueryInterface(aQuerySet->mQueryNode));

            rv = mQueryProcessor->CompileQuery(this, query,
                                               mRefVariable, memberVariable,
                                               getter_AddRefs(aQuerySet->mCompiledQuery));

            if (aQuerySet->mCompiledQuery) {
                nsTemplateRule* rule = aQuerySet->NewRule(aTemplate, rulenode, aQuerySet);
                if (! rule)
                    return NS_ERROR_OUT_OF_MEMORY;

                rule->SetVars(mRefVariable, memberVariable);

                *aCanUseTemplate = PR_TRUE;

                return NS_OK;
            }
        }
    }

    if (! hasRule && ! hasQuery && ! hasQuerySet) {
        // if no rules are specified in the template, then the contents of the
        // <template> tag are the one-and-only template.
        rv = CompileSimpleQuery(aTemplate, aQuerySet, aCanUseTemplate);
     }

    return rv;
}

nsresult
nsXULTemplateBuilder::CompileExtendedQuery(nsIContent* aRuleElement,
                                           nsIContent* aActionElement,
                                           nsIAtom* aMemberVariable,
                                           nsTemplateQuerySet* aQuerySet)
{
    // Compile an "extended" <template> rule. An extended rule may have
    // a <conditions> child, an <action> child, and a <bindings> child.
    nsresult rv;

    nsTemplateRule* rule = aQuerySet->NewRule(aRuleElement, aActionElement, aQuerySet);
    if (! rule)
         return NS_ERROR_OUT_OF_MEMORY;

    nsCOMPtr<nsIContent> conditions;
    nsXULContentUtils::FindChildByTag(aRuleElement,
                                      kNameSpaceID_XUL,
                                      nsGkAtoms::conditions,
                                      getter_AddRefs(conditions));

    // allow the conditions to be placed directly inside the rule
    if (!conditions)
        conditions = aRuleElement;
  
    rv = CompileConditions(rule, conditions);
    // If the rule compilation failed, then we have to bail.
    if (NS_FAILED(rv)) {
        aQuerySet->RemoveRule(rule);
        return rv;
    }

    rule->SetVars(mRefVariable, aMemberVariable);

    // If we've got bindings, add 'em.
    nsCOMPtr<nsIContent> bindings;
    nsXULContentUtils::FindChildByTag(aRuleElement,
                                      kNameSpaceID_XUL,
                                      nsGkAtoms::bindings,
                                      getter_AddRefs(bindings));

    // allow bindings to be placed directly inside rule
    if (!bindings)
        bindings = aRuleElement;

    rv = CompileBindings(rule, bindings);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
}

already_AddRefed<nsIAtom>
nsXULTemplateBuilder::DetermineMemberVariable(nsIContent* aElement)
{
    // recursively iterate over the children looking for an element
    // with uri="?..."
    for (nsINode::ChildIterator iter(aElement); !iter.IsDone(); iter.Next()) {
        nsAutoString uri;
        nsIContent *child = iter;
        child->GetAttr(kNameSpaceID_None, nsGkAtoms::uri, uri);
        if (!uri.IsEmpty() && uri[0] == PRUnichar('?')) {
            return NS_NewAtom(uri);
        }

        nsCOMPtr<nsIAtom> result = DetermineMemberVariable(child);
        if (result) {
            return result.forget();
        }
    }

    return nsnull;
}

void
nsXULTemplateBuilder::DetermineRDFQueryRef(nsIContent* aQueryElement, nsIAtom** aTag)
{
    // check for a tag
    nsCOMPtr<nsIContent> content;
    nsXULContentUtils::FindChildByTag(aQueryElement,
                                      kNameSpaceID_XUL,
                                      nsGkAtoms::content,
                                      getter_AddRefs(content));

    if (! content) {
        // look for older treeitem syntax as well
        nsXULContentUtils::FindChildByTag(aQueryElement,
                                          kNameSpaceID_XUL,
                                          nsGkAtoms::treeitem,
                                          getter_AddRefs(content));
    }

    if (content) {
        nsAutoString uri;
        content->GetAttr(kNameSpaceID_None, nsGkAtoms::uri, uri);

        if (!uri.IsEmpty())
            mRefVariable = do_GetAtom(uri);

        nsAutoString tag;
        content->GetAttr(kNameSpaceID_None, nsGkAtoms::tag, tag);

        if (!tag.IsEmpty())
            *aTag = NS_NewAtom(tag);
    }
}

nsresult
nsXULTemplateBuilder::CompileSimpleQuery(nsIContent* aRuleElement,
                                         nsTemplateQuerySet* aQuerySet,
                                         PRBool* aCanUseTemplate)
{
    // compile a simple query, which is a query with no <query> or
    // <conditions>. This means that a default query is used.
    nsCOMPtr<nsIDOMNode> query(do_QueryInterface(aRuleElement));

    nsCOMPtr<nsIAtom> memberVariable;
    if (mMemberVariable)
        memberVariable = mMemberVariable;
    else
        memberVariable = do_GetAtom("rdf:*");

    // since there is no <query> node for a simple query, the query node will
    // be either the <rule> node if multiple rules are used, or the <template> node.
    aQuerySet->mQueryNode = aRuleElement;
    nsresult rv = mQueryProcessor->CompileQuery(this, query,
                                                mRefVariable, memberVariable,
                                                getter_AddRefs(aQuerySet->mCompiledQuery));
    if (NS_FAILED(rv))
        return rv;

    if (! aQuerySet->mCompiledQuery) {
        *aCanUseTemplate = PR_FALSE;
        return NS_OK;
    }

    nsTemplateRule* rule = aQuerySet->NewRule(aRuleElement, aRuleElement, aQuerySet);
    if (! rule)
        return NS_ERROR_OUT_OF_MEMORY;

    rule->SetVars(mRefVariable, memberVariable);

    nsAutoString tag;
    aRuleElement->GetAttr(kNameSpaceID_None, nsGkAtoms::parent, tag);

    if (!tag.IsEmpty()) {
        nsCOMPtr<nsIAtom> tagatom = do_GetAtom(tag);
        aQuerySet->SetTag(tagatom);
    }

    *aCanUseTemplate = PR_TRUE;

    return AddSimpleRuleBindings(rule, aRuleElement);
}

nsresult
nsXULTemplateBuilder::CompileConditions(nsTemplateRule* aRule,
                                        nsIContent* aCondition)
{
    nsAutoString tag;
    aCondition->GetAttr(kNameSpaceID_None, nsGkAtoms::parent, tag);

    if (!tag.IsEmpty()) {
        nsCOMPtr<nsIAtom> tagatom = do_GetAtom(tag);
        aRule->SetTag(tagatom);
    }

    PRUint32 count = aCondition->GetChildCount();

    nsTemplateCondition* currentCondition = nsnull;

    for (PRUint32 i = 0; i < count; i++) {
        nsIContent *node = aCondition->GetChildAt(i);

        if (node->NodeInfo()->Equals(nsGkAtoms::where, kNameSpaceID_XUL)) {
            nsresult rv = CompileWhereCondition(aRule, node, &currentCondition);
            if (NS_FAILED(rv))
                return rv;
        }
    }

    return NS_OK;
}

nsresult
nsXULTemplateBuilder::CompileWhereCondition(nsTemplateRule* aRule,
                                            nsIContent* aCondition,
                                            nsTemplateCondition** aCurrentCondition)
{
    // Compile a <where> condition, which must be of the form:
    //
    //   <where subject="?var1|string" rel="relation" value="?var2|string" />
    //
    //    The value of rel may be:
    //      equal - subject must be equal to object
    //      notequal - subject must not be equal to object
    //      less - subject must be less than object
    //      greater - subject must be greater than object
    //      startswith - subject must start with object
    //      endswith - subject must end with object
    //      contains - subject must contain object
    //    Comparisons are done as strings unless the subject is an integer.

    // subject
    nsAutoString subject;
    aCondition->GetAttr(kNameSpaceID_None, nsGkAtoms::subject, subject);
    if (subject.IsEmpty()) {
        nsXULContentUtils::LogTemplateError(ERROR_TEMPLATE_WHERE_NO_SUBJECT);
        return NS_OK;
    }

    nsCOMPtr<nsIAtom> svar;
    if (subject[0] == PRUnichar('?'))
        svar = do_GetAtom(subject);

    nsAutoString relstring;
    aCondition->GetAttr(kNameSpaceID_None, nsGkAtoms::rel, relstring);
    if (relstring.IsEmpty()) {
        nsXULContentUtils::LogTemplateError(ERROR_TEMPLATE_WHERE_NO_RELATION);
        return NS_OK;
    }

    // object
    nsAutoString value;
    aCondition->GetAttr(kNameSpaceID_None, nsGkAtoms::value, value);
    if (value.IsEmpty()) {
        nsXULContentUtils::LogTemplateError(ERROR_TEMPLATE_WHERE_NO_VALUE);
        return NS_OK;
    }

    // multiple
    PRBool shouldMultiple =
      aCondition->AttrValueIs(kNameSpaceID_None, nsGkAtoms::multiple,
                              nsGkAtoms::_true, eCaseMatters);

    nsCOMPtr<nsIAtom> vvar;
    if (!shouldMultiple && (value[0] == PRUnichar('?'))) {
        vvar = do_GetAtom(value);
    }

    // ignorecase
    PRBool shouldIgnoreCase =
      aCondition->AttrValueIs(kNameSpaceID_None, nsGkAtoms::ignorecase,
                              nsGkAtoms::_true, eCaseMatters);

    // negate
    PRBool shouldNegate =
      aCondition->AttrValueIs(kNameSpaceID_None, nsGkAtoms::negate,
                              nsGkAtoms::_true, eCaseMatters);

    nsTemplateCondition* condition;

    if (svar && vvar) {
        condition = new nsTemplateCondition(svar, relstring, vvar,
                                            shouldIgnoreCase, shouldNegate);
    }
    else if (svar && !value.IsEmpty()) {
        condition = new nsTemplateCondition(svar, relstring, value,
                                            shouldIgnoreCase, shouldNegate, shouldMultiple);
    }
    else if (vvar) {
        condition = new nsTemplateCondition(subject, relstring, vvar,
                                            shouldIgnoreCase, shouldNegate);
    }
    else {
        nsXULContentUtils::LogTemplateError(ERROR_TEMPLATE_WHERE_NO_VAR);
        return NS_OK;
    }

    if (! condition)
        return NS_ERROR_OUT_OF_MEMORY;

    if (*aCurrentCondition) {
        (*aCurrentCondition)->SetNext(condition);
    }
    else {
        aRule->SetCondition(condition);
    }

    *aCurrentCondition = condition;

    return NS_OK;
}

nsresult
nsXULTemplateBuilder::CompileBindings(nsTemplateRule* aRule, nsIContent* aBindings)
{
    // Add an extended rule's bindings.
    nsresult rv;

    PRUint32 count = aBindings->GetChildCount();

    for (PRUint32 i = 0; i < count; ++i) {
        nsIContent *binding = aBindings->GetChildAt(i);

        if (binding->NodeInfo()->Equals(nsGkAtoms::binding,
                                        kNameSpaceID_XUL)) {
            rv = CompileBinding(aRule, binding);
            if (NS_FAILED(rv))
                return rv;
        }
    }

    aRule->AddBindingsToQueryProcessor(mQueryProcessor);

    return NS_OK;
}


nsresult
nsXULTemplateBuilder::CompileBinding(nsTemplateRule* aRule,
                                     nsIContent* aBinding)
{
    // Compile a <binding> "condition", which must be of the form:
    //
    //   <binding subject="?var1"
    //            predicate="resource"
    //            object="?var2" />
    //
    // XXXwaterson Some day it would be cool to allow the 'predicate'
    // to be bound to a variable.

    // subject
    nsAutoString subject;
    aBinding->GetAttr(kNameSpaceID_None, nsGkAtoms::subject, subject);
    if (subject.IsEmpty()) {
        nsXULContentUtils::LogTemplateError(ERROR_TEMPLATE_BINDING_BAD_SUBJECT);
        return NS_OK;
    }

    nsCOMPtr<nsIAtom> svar;
    if (subject[0] == PRUnichar('?')) {
        svar = do_GetAtom(subject);
    }
    else {
        nsXULContentUtils::LogTemplateError(ERROR_TEMPLATE_BINDING_BAD_SUBJECT);
        return NS_OK;
    }

    // predicate
    nsAutoString predicate;
    aBinding->GetAttr(kNameSpaceID_None, nsGkAtoms::predicate, predicate);
    if (predicate.IsEmpty()) {
        nsXULContentUtils::LogTemplateError(ERROR_TEMPLATE_BINDING_BAD_PREDICATE);
        return NS_OK;
    }

    // object
    nsAutoString object;
    aBinding->GetAttr(kNameSpaceID_None, nsGkAtoms::object, object);

    if (object.IsEmpty()) {
        nsXULContentUtils::LogTemplateError(ERROR_TEMPLATE_BINDING_BAD_OBJECT);
        return NS_OK;
    }

    nsCOMPtr<nsIAtom> ovar;
    if (object[0] == PRUnichar('?')) {
        ovar = do_GetAtom(object);
    }
    else {
        nsXULContentUtils::LogTemplateError(ERROR_TEMPLATE_BINDING_BAD_OBJECT);
        return NS_OK;
    }

    return aRule->AddBinding(svar, predicate, ovar);
}

nsresult
nsXULTemplateBuilder::AddSimpleRuleBindings(nsTemplateRule* aRule,
                                            nsIContent* aElement)
{
    // Crawl the content tree of a "simple" rule, adding a variable
    // assignment for any attribute whose value is "rdf:".

    nsAutoTArray<nsIContent*, 8> elements;

    if (elements.AppendElement(aElement) == nsnull)
        return NS_ERROR_OUT_OF_MEMORY;

    while (elements.Length()) {
        // Pop the next element off the stack
        PRUint32 i = elements.Length() - 1;
        nsIContent* element = elements[i];
        elements.RemoveElementAt(i);

        // Iterate through its attributes, looking for substitutions
        // that we need to add as bindings.
        PRUint32 count = element->GetAttrCount();

        for (i = 0; i < count; ++i) {
            const nsAttrName* name = element->GetAttrNameAt(i);

            if (!name->Equals(nsGkAtoms::id, kNameSpaceID_None) &&
                !name->Equals(nsGkAtoms::uri, kNameSpaceID_None)) {
                nsAutoString value;
                element->GetAttr(name->NamespaceID(), name->LocalName(), value);

                // Scan the attribute for variables, adding a binding for
                // each one.
                ParseAttribute(value, AddBindingsFor, nsnull, aRule);
            }
        }

        // Push kids onto the stack, and search them next.
        count = element->GetChildCount();

        while (count-- > 0) {
            if (elements.AppendElement(element->GetChildAt(count)) == nsnull)
                return NS_ERROR_OUT_OF_MEMORY;
        }
    }

    aRule->AddBindingsToQueryProcessor(mQueryProcessor);

    return NS_OK;
}

void
nsXULTemplateBuilder::AddBindingsFor(nsXULTemplateBuilder* aThis,
                                     const nsAString& aVariable,
                                     void* aClosure)
{
    // We should *only* be recieving "rdf:"-style variables. Make
    // sure...
    if (!StringBeginsWith(aVariable, NS_LITERAL_STRING("rdf:")))
        return;

    nsTemplateRule* rule = static_cast<nsTemplateRule*>(aClosure);

    nsCOMPtr<nsIAtom> var = do_GetAtom(aVariable);

    // Strip it down to the raw RDF property by clobbering the "rdf:"
    // prefix
    nsAutoString property;
    property.Assign(Substring(aVariable, PRUint32(4), aVariable.Length() - 4));

    if (! rule->HasBinding(rule->GetMemberVariable(), property, var))
        // In the simple syntax, the binding is always from the
        // member variable, through the property, to the target.
        rule->AddBinding(rule->GetMemberVariable(), property, var);
}


nsresult
nsXULTemplateBuilder::IsSystemPrincipal(nsIPrincipal *principal, PRBool *result)
{
  if (!gSystemPrincipal)
    return NS_ERROR_UNEXPECTED;

  *result = (principal == gSystemPrincipal);
  return NS_OK;
}

PRBool
nsXULTemplateBuilder::IsActivated(nsIRDFResource *aResource)
{
    for (ActivationEntry *entry = mTop;
         entry != nsnull;
         entry = entry->mPrevious) {
        if (entry->mResource == aResource)
            return PR_TRUE;
    }
    return PR_FALSE;
}

nsresult
nsXULTemplateBuilder::GetResultResource(nsIXULTemplateResult* aResult,
                                        nsIRDFResource** aResource)
{
    // get the resource for a result by checking its resource property. If it
    // is not set, check the id. This allows non-chrome implementations to
    // avoid having to use RDF.
    nsresult rv = aResult->GetResource(aResource);
    if (NS_FAILED(rv))
        return rv;

    if (! *aResource) {
        nsAutoString id;
        rv = aResult->GetId(id);
        if (NS_FAILED(rv))
            return rv;

        return gRDFService->GetUnicodeResource(id, aResource);
    }

    return rv;
}


void
nsXULTemplateBuilder::OutputMatchToLog(nsIRDFResource* aId,
                                       nsTemplateMatch* aMatch,
                                       PRBool aIsNew)
{
    PRInt32 priority = aMatch->QuerySetPriority() + 1;
    PRInt32 activePriority = -1;

    nsAutoString msg;

    nsAutoString templateid;
    mRoot->GetAttr(kNameSpaceID_None, nsGkAtoms::id, templateid);
    msg.AppendLiteral("In template");
    if (!templateid.IsEmpty()) {
        msg.AppendLiteral(" with id ");
        msg.Append(templateid);
    }

    nsAutoString refstring;
    aMatch->mResult->GetBindingFor(mRefVariable, refstring);
    if (!refstring.IsEmpty()) {
        msg.AppendLiteral(" using ref ");
        msg.Append(refstring);
    }

    msg.AppendLiteral("\n    ");

    nsTemplateMatch* match = nsnull;
    if (mMatchMap.Get(aId, &match)){
        while (match) {
            if (match == aMatch)
                break;
            if (match->IsActive() &&
                match->GetContainer() == aMatch->GetContainer()) {
                activePriority = match->QuerySetPriority() + 1;
                break;
            }
            match = match->mNext;
        }
    }

    if (aMatch->IsActive()) {
        if (aIsNew) {
            msg.AppendLiteral("New active result for query ");
            msg.AppendInt(priority);
            msg.AppendLiteral(" matching rule ");
            msg.AppendInt(aMatch->RuleIndex() + 1);
        }
        else {
            msg.AppendLiteral("Removed active result for query ");
            msg.AppendInt(priority);
            if (activePriority > 0) {
                msg.AppendLiteral(" (new active query is ");
                msg.AppendInt(activePriority);
                msg.Append(')');
            }
            else {
                msg.AppendLiteral(" (no new active query)");
            }
        }
    }
    else {
        if (aIsNew) {
            msg.AppendLiteral("New inactive result for query ");
            msg.AppendInt(priority);
            if (activePriority > 0) {
                msg.AppendLiteral(" (overridden by query ");
                msg.AppendInt(activePriority);
                msg.Append(')');
            }
            else {
                msg.AppendLiteral(" (didn't match a rule)");
            }
        }
        else {
            msg.AppendLiteral("Removed inactive result for query ");
            msg.AppendInt(priority);
            if (activePriority > 0) {
                msg.AppendLiteral(" (active query is ");
                msg.AppendInt(activePriority);
                msg.Append(')');
            }
            else {
                msg.AppendLiteral(" (no active query)");
            }
        }
    }

    nsAutoString idstring;
    nsXULContentUtils::GetTextForNode(aId, idstring);
    msg.AppendLiteral(": ");
    msg.Append(idstring);

    nsCOMPtr<nsIConsoleService> cs = do_GetService(NS_CONSOLESERVICE_CONTRACTID);
    if (cs)
      cs->LogStringMessage(msg.get());
}
