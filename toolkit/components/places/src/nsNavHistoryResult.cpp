//* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 * The Original Code is Mozilla History System
 *
 * The Initial Developer of the Original Code is
 * Google Inc.
 * Portions created by the Initial Developer are Copyright (C) 2005
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Brett Wilson <brettw@gmail.com> (original author)
 *   Dietrich Ayala <dietrich@mozilla.com>
 *   Asaf Romano <mano@mozilla.com>
 *   Marco Bonardo <mak77@bonardo.net>
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
#include "nsNavHistory.h"
#include "nsNavBookmarks.h"
#include "nsFaviconService.h"
#include "nsITaggingService.h"
#include "nsAnnotationService.h"

#include "nsDebug.h"
#include "nsNetUtil.h"
#include "nsPrintfCString.h"
#include "nsString.h"
#include "nsUnicharUtils.h"
#include "prtime.h"
#include "prprf.h"

#include "nsIDynamicContainer.h"
#include "mozStorageHelper.h"
#include "nsCycleCollectionParticipant.h"
#include "nsIClassInfo.h"
#include "nsIProgrammingLanguage.h"
#include "nsIXPCScriptable.h"

// What we want is: NS_INTERFACE_MAP_ENTRY(self) for static IID accessors,
// but some of our classes (like nsNavHistoryResult) have an ambiguous base
// class of nsISupports which prevents this from working (the default macro
// converts it to nsISupports, then addrefs it, then returns it). Therefore, we
// expand the macro here and change it so that it works. Yuck.
#define NS_INTERFACE_MAP_STATIC_AMBIGUOUS(_class) \
  if (aIID.Equals(NS_GET_IID(_class))) { \
    NS_ADDREF(this); \
    *aInstancePtr = this; \
    return NS_OK; \
  } else

// emulate string comparison (used for sorting) for PRTime and int
inline PRInt32 ComparePRTime(PRTime a, PRTime b)
{
  if (LL_CMP(a, <, b))
    return -1;
  else if (LL_CMP(a, >, b))
    return 1;
  return 0;
}
inline PRInt32 CompareIntegers(PRUint32 a, PRUint32 b)
{
  return a - b;
}

namespace mozilla {
  namespace places {
    // Class-info and the scriptable helper are implemented in order to
    // allow the JS frontend code to set expando properties on result nodes.
    class ResultNodeClassInfo : public nsIClassInfo
                              , public nsIXPCScriptable
    {
      NS_DECL_ISUPPORTS
      NS_DECL_NSIXPCSCRIPTABLE

      // TODO: Bug 517718.
      NS_IMETHODIMP
      GetInterfaces(PRUint32 *_count, nsIID ***_array)
      {
        *_count = 0;
        *_array = nsnull;

        return NS_OK;
      }

      NS_IMETHODIMP
      GetHelperForLanguage(PRUint32 aLanguage, nsISupports **_helper)
      {
        if (aLanguage == nsIProgrammingLanguage::JAVASCRIPT) {
          *_helper = static_cast<nsIXPCScriptable *>(this);
          NS_ADDREF(*_helper);
        }
        else
          *_helper = nsnull;

        return NS_OK;
      }

      NS_IMETHODIMP
      GetContractID(char **_contractID)
      {
        *_contractID = nsnull;
        return NS_OK;
      }

      NS_IMETHODIMP
      GetClassDescription(char **_desc)
      {
        *_desc = nsnull;
        return NS_OK;
      }

      NS_IMETHODIMP
      GetClassID(nsCID **_id)
      {
        *_id = nsnull;
        return NS_OK;
      }

      NS_IMETHODIMP
      GetImplementationLanguage(PRUint32 *_language)
      {
        *_language = nsIProgrammingLanguage::CPLUSPLUS;
        return NS_OK;
      }

      NS_IMETHODIMP
      GetFlags(PRUint32 *_flags)
      {
        *_flags = 0;
        return NS_OK;
      }

      NS_IMETHODIMP
      GetClassIDNoAlloc(nsCID *_cid)
      {
        return NS_ERROR_NOT_AVAILABLE;
      }
    };

    /**
     * As a static implementation of classinfo, we violate XPCOM rules andjust
     * pretend to use the refcount mechanism.  See classinfo documentation at
     * https://developer.mozilla.org/en/Using_nsIClassInfo
     */
    NS_IMETHODIMP_(nsrefcnt) ResultNodeClassInfo::AddRef()
    {
      return 2;
    }
    NS_IMETHODIMP_(nsrefcnt) ResultNodeClassInfo::Release()
    {
      return 1;
    }

    NS_IMPL_QUERY_INTERFACE2(ResultNodeClassInfo, nsIClassInfo, nsIXPCScriptable)

#define XPC_MAP_CLASSNAME ResultNodeClassInfo
#define XPC_MAP_QUOTED_CLASSNAME "ResultNodeClassInfo"
#define XPC_MAP_FLAGS nsIXPCScriptable::USE_JSSTUB_FOR_ADDPROPERTY | \
                      nsIXPCScriptable::USE_JSSTUB_FOR_DELPROPERTY | \
                      nsIXPCScriptable::USE_JSSTUB_FOR_SETPROPERTY

// xpc_map_end contains implementation for nsIXPCScriptable, that used the
// constant define above
#include "xpc_map_end.h"    

    static ResultNodeClassInfo sResultNodeClassInfo;
  } // namespace places
} // namespace mozilla

using namespace mozilla::places;

// nsNavHistoryResultNode ******************************************************

NS_IMPL_CYCLE_COLLECTION_CLASS(nsNavHistoryResultNode)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsNavHistoryResultNode)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mParent)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END 

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsNavHistoryResultNode)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR_AMBIGUOUS(mParent, nsINavHistoryContainerResultNode);
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsNavHistoryResultNode)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsINavHistoryResultNode)
  if (aIID.Equals(NS_GET_IID(nsIClassInfo)))
    foundInterface = static_cast<nsIClassInfo *>(&mozilla::places::sResultNodeClassInfo);
  else
  NS_INTERFACE_MAP_ENTRY(nsINavHistoryResultNode)
NS_INTERFACE_MAP_END

NS_IMPL_CYCLE_COLLECTING_ADDREF_AMBIGUOUS(nsNavHistoryResultNode, nsINavHistoryResultNode)
NS_IMPL_CYCLE_COLLECTING_RELEASE_AMBIGUOUS(nsNavHistoryResultNode, nsINavHistoryResultNode)

nsNavHistoryResultNode::nsNavHistoryResultNode(
    const nsACString& aURI, const nsACString& aTitle, PRUint32 aAccessCount,
    PRTime aTime, const nsACString& aIconURI) :
  mParent(nsnull),
  mURI(aURI),
  mTitle(aTitle),
  mAccessCount(aAccessCount),
  mTime(aTime),
  mFaviconURI(aIconURI),
  mBookmarkIndex(-1),
  mItemId(-1),
  mFolderId(-1),
  mDateAdded(0),
  mLastModified(0),
  mIndentLevel(-1)
{
  mTags.SetIsVoid(PR_TRUE);
}

NS_IMETHODIMP
nsNavHistoryResultNode::GetIcon(nsACString& aIcon)
{
  if (mFaviconURI.IsEmpty()) {
    aIcon.Truncate();
    return NS_OK;
  }

  nsFaviconService* faviconService = nsFaviconService::GetFaviconService();
  NS_ENSURE_TRUE(faviconService, NS_ERROR_OUT_OF_MEMORY);
  faviconService->GetFaviconSpecForIconString(mFaviconURI, aIcon);
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryResultNode::GetParent(nsINavHistoryContainerResultNode** aParent)
{
  NS_IF_ADDREF(*aParent = mParent);
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryResultNode::GetParentResult(nsINavHistoryResult** aResult)
{
  *aResult = nsnull;
  if (IsContainer() && GetAsContainer()->mResult) {
    NS_ADDREF(*aResult = GetAsContainer()->mResult);
  } else if (mParent && mParent->mResult) {
    NS_ADDREF(*aResult = mParent->mResult);
  } else {
   return NS_ERROR_UNEXPECTED;
  }

  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryResultNode::GetTags(nsAString& aTags) {
  // Only URI-nodes may be associated with tags
  if (!IsURI()) {
    aTags.Truncate();
    return NS_OK;
  }

  // Initially, the tags string is set to a void string (see constructor). We
  // then build it the first time this method called is called (and by that,
  // implicitly unset the void flag). Result observers may re-set the void flag
  // in order to force rebuilding of the tags string.
  if (!mTags.IsVoid()) {
    aTags.Assign(mTags);
    return NS_OK;
  }

  // Fetch the tags
  nsNavHistory *history = nsNavHistory::GetHistoryService();
  NS_ENSURE_TRUE(history, NS_ERROR_OUT_OF_MEMORY);
  mozIStorageStatement *getTagsStatement = history->DBGetTags();
  NS_ENSURE_STATE(getTagsStatement);
  mozStorageStatementScoper scoper(getTagsStatement);
  nsresult rv = getTagsStatement->BindInt64Parameter(0, history->GetTagsFolder());
  NS_ENSURE_SUCCESS(rv, rv);
  rv = getTagsStatement->BindUTF8StringParameter(1, mURI);
  NS_ENSURE_SUCCESS(rv, rv);

  PRBool hasTags = PR_FALSE;
  if (NS_SUCCEEDED(getTagsStatement->ExecuteStep(&hasTags)) && hasTags) {
    rv = getTagsStatement->GetString(0, mTags);
    NS_ENSURE_SUCCESS(rv, rv);
    aTags.Assign(mTags);
  }

  // If this node is a child of a history, we need to make sure
  // bookmarks-liveupdate is turned on for this query
  if (mParent && mParent->IsQuery()) {
    nsNavHistoryQueryResultNode* query = mParent->GetAsQuery();
    if (query->mLiveUpdate != QUERYUPDATE_COMPLEX_WITH_BOOKMARKS) {
      query->mLiveUpdate = QUERYUPDATE_COMPLEX_WITH_BOOKMARKS;
      nsNavHistoryResult* result = query->GetResult();
      NS_ENSURE_TRUE(result, NS_ERROR_FAILURE);
      result->AddAllBookmarksObserver(query);
    }
  }
  return NS_OK;
}

// nsNavHistoryResultNode::OnRemoving
//
//    This will zero out some values in case somebody still holds a reference

void
nsNavHistoryResultNode::OnRemoving()
{
  mParent = nsnull;
}


// nsNavHistoryResultNode::GetResult
//
//    This will find the result for this node. We can ask the nearest container
//    for this value (either ourselves or our parents should be a container,
//    and all containers have result pointers).

nsNavHistoryResult*
nsNavHistoryResultNode::GetResult()
{
  nsNavHistoryResultNode* node = this;
  do {
    if (node->IsContainer()) {
      nsNavHistoryContainerResultNode* container =
        static_cast<nsNavHistoryContainerResultNode*>(node);
      NS_ASSERTION(container->mResult, "Containers must have valid results");
      return container->mResult;
    }
    node = node->mParent;
  } while (node);
  NS_NOTREACHED("No container node found in hierarchy!");
  return nsnull;
}

// nsNavHistoryResultNode::GetGeneratingOptions
//
//    Searches up the tree for the closest node that has an options structure.
//    This will tell us the options that were used to generate this node.
//
//    Be careful, this function walks up the tree, so it can not be used when
//    result nodes are created because they have no parent. Only call this
//    function after the tree has been built.

nsNavHistoryQueryOptions*
nsNavHistoryResultNode::GetGeneratingOptions()
{
  if (! mParent) {
    // When we have no parent, it either means we haven't built the tree yet,
    // in which case calling this function is a bug, or this node is the root
    // of the tree. When we are the root of the tree, our own options are the
    // generating options.
    if (IsContainer())
      return GetAsContainer()->mOptions;
    NS_NOTREACHED("Can't find a generating node for this container, perhaps FillStats has not been called on this tree yet?");
    return nsnull;
  }

  nsNavHistoryContainerResultNode* cur = mParent;
  while (cur) {
    if (cur->IsFolder())
      return cur->GetAsFolder()->mOptions;
    else if (cur->IsQuery())
      return cur->GetAsQuery()->mOptions;
    cur = cur->mParent;
  }
  // we should always find a folder or query node as an ancestor
  NS_NOTREACHED("Can't find a generating node for this container, the tree seemes corrupted.");
  return nsnull;
}


// nsNavHistoryVisitResultNode *************************************************

NS_IMPL_ISUPPORTS_INHERITED1(nsNavHistoryVisitResultNode,
                             nsNavHistoryResultNode,
                             nsINavHistoryVisitResultNode)

nsNavHistoryVisitResultNode::nsNavHistoryVisitResultNode(
    const nsACString& aURI, const nsACString& aTitle, PRUint32 aAccessCount,
    PRTime aTime, const nsACString& aIconURI, PRInt64 aSession) :
  nsNavHistoryResultNode(aURI, aTitle, aAccessCount, aTime, aIconURI),
  mSessionId(aSession)
{
}


// nsNavHistoryFullVisitResultNode *********************************************

NS_IMPL_ISUPPORTS_INHERITED1(nsNavHistoryFullVisitResultNode,
                             nsNavHistoryVisitResultNode,
                             nsINavHistoryFullVisitResultNode)

nsNavHistoryFullVisitResultNode::nsNavHistoryFullVisitResultNode(
    const nsACString& aURI, const nsACString& aTitle, PRUint32 aAccessCount,
    PRTime aTime, const nsACString& aIconURI, PRInt64 aSession,
    PRInt64 aVisitId, PRInt64 aReferringVisitId, PRInt32 aTransitionType) :
  nsNavHistoryVisitResultNode(aURI, aTitle, aAccessCount, aTime, aIconURI,
                              aSession),
  mVisitId(aVisitId),
  mReferringVisitId(aReferringVisitId),
  mTransitionType(aTransitionType)
{
}

// nsNavHistoryContainerResultNode *********************************************

NS_IMPL_CYCLE_COLLECTION_CLASS(nsNavHistoryContainerResultNode)

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN_INHERITED(nsNavHistoryContainerResultNode, nsNavHistoryResultNode)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mResult)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMARRAY(mChildren)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END 

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN_INHERITED(nsNavHistoryContainerResultNode, nsNavHistoryResultNode)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR_AMBIGUOUS(mResult, nsINavHistoryResult)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMARRAY(mChildren)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_ADDREF_INHERITED(nsNavHistoryContainerResultNode, nsNavHistoryResultNode)
NS_IMPL_RELEASE_INHERITED(nsNavHistoryContainerResultNode, nsNavHistoryResultNode)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION_INHERITED(nsNavHistoryContainerResultNode)
  NS_INTERFACE_MAP_STATIC_AMBIGUOUS(nsNavHistoryContainerResultNode)
  NS_INTERFACE_MAP_ENTRY(nsINavHistoryContainerResultNode)
NS_INTERFACE_MAP_END_INHERITING(nsNavHistoryResultNode)

nsNavHistoryContainerResultNode::nsNavHistoryContainerResultNode(
    const nsACString& aURI, const nsACString& aTitle,
    const nsACString& aIconURI, PRUint32 aContainerType, PRBool aReadOnly,
    const nsACString& aDynamicContainerType, nsNavHistoryQueryOptions* aOptions) :
  nsNavHistoryResultNode(aURI, aTitle, 0, 0, aIconURI),
  mResult(nsnull),
  mContainerType(aContainerType),
  mExpanded(PR_FALSE),
  mChildrenReadOnly(aReadOnly),
  mOptions(aOptions),
  mDynamicContainerType(aDynamicContainerType)
{
}

nsNavHistoryContainerResultNode::nsNavHistoryContainerResultNode(
    const nsACString& aURI, const nsACString& aTitle,
    PRTime aTime,
    const nsACString& aIconURI, PRUint32 aContainerType, PRBool aReadOnly,
    const nsACString& aDynamicContainerType, 
    nsNavHistoryQueryOptions* aOptions) :
  nsNavHistoryResultNode(aURI, aTitle, 0, aTime, aIconURI),
  mResult(nsnull),
  mContainerType(aContainerType),
  mExpanded(PR_FALSE),
  mChildrenReadOnly(aReadOnly),
  mOptions(aOptions),
  mDynamicContainerType(aDynamicContainerType)
{
}

nsNavHistoryContainerResultNode::~nsNavHistoryContainerResultNode()
{
  // Explicitly clean up array of children of this container.  We must ensure
  // all references are gone and all of their destructors are called.
  mChildren.Clear();
}

// nsNavHistoryContainerResultNode::OnRemoving
//
//    Containers should notify their children that they are being removed
//    when the container is being removed.

void
nsNavHistoryContainerResultNode::OnRemoving()
{
  nsNavHistoryResultNode::OnRemoving();
  for (PRInt32 i = 0; i < mChildren.Count(); i ++)
    mChildren[i]->OnRemoving();
  mChildren.Clear();
}


// nsNavHistoryContainerResultNode::AreChildrenVisible

PRBool
nsNavHistoryContainerResultNode::AreChildrenVisible()
{
  nsNavHistoryResult* result = GetResult();
  if (!result) {
    NS_NOTREACHED("Invalid result");
    return PR_FALSE;
  }

  // can't see children when we're invisible
  if (!mExpanded)
    return PR_FALSE;

  // Now check if any ancestor is closed.
  nsNavHistoryContainerResultNode* ancestor = mParent;
  while (ancestor) {
    if (!ancestor->mExpanded)
      return PR_FALSE;

    ancestor = ancestor->mParent;
  }

  return PR_TRUE;
}


// nsNavHistoryContainerResultNode::GetContainerOpen

NS_IMETHODIMP
nsNavHistoryContainerResultNode::GetContainerOpen(PRBool *aContainerOpen)
{
  *aContainerOpen = mExpanded;
  return NS_OK;
}


// nsNavHistoryContainerResultNode::SetContainerOpen

NS_IMETHODIMP
nsNavHistoryContainerResultNode::SetContainerOpen(PRBool aContainerOpen)
{
  if (mExpanded && ! aContainerOpen)
    CloseContainer();
  else if (! mExpanded && aContainerOpen)
    OpenContainer();
  return NS_OK;
}


// nsNavHistoryContainerResultNode::OpenContainer
//
//    This handles the generic container case. Other container types should
//    override this to do their own handling.

nsresult
nsNavHistoryContainerResultNode::OpenContainer()
{
  NS_ASSERTION(! mExpanded, "Container must be expanded to close it");
  mExpanded = PR_TRUE;

  if (IsDynamicContainer()) {
    // dynamic container API may want to fill us
    nsresult rv;
    nsCOMPtr<nsIDynamicContainer> svc = do_GetService(mDynamicContainerType.get(), &rv);
    if (NS_SUCCEEDED(rv)) {
      svc->OnContainerNodeOpening(this, GetGeneratingOptions());
    } else {
      NS_WARNING("Unable to get dynamic container for ");
      NS_WARNING(mDynamicContainerType.get());
    }
    PRInt32 oldAccessCount = mAccessCount;
    FillStats();
    ReverseUpdateStats(mAccessCount - oldAccessCount);
  }

  nsNavHistoryResult* result = GetResult();
  NS_ENSURE_TRUE(result, NS_ERROR_FAILURE);
  if (result->GetView())
    result->GetView()->ContainerOpened(this);
  return NS_OK;
}


// nsNavHistoryContainerResultNode::CloseContainer
//
//    Set aUpdateVisible to redraw the screen, this is the normal operation.
//    This is set to false for the recursive calls since the root container
//    that is being closed will handle recomputation of the visible elements
//    for its entire subtree.

nsresult
nsNavHistoryContainerResultNode::CloseContainer(PRBool aUpdateView)
{
  NS_ASSERTION(mExpanded, "Container must be expanded to close it");

  // recursively close all child containers
  for (PRInt32 i = 0; i < mChildren.Count(); i ++) {
    if (mChildren[i]->IsContainer() &&
        mChildren[i]->GetAsContainer()->mExpanded)
      mChildren[i]->GetAsContainer()->CloseContainer(PR_FALSE);
  }

  mExpanded = PR_FALSE;

  nsresult rv;
  if (IsDynamicContainer()) {
    // notify dynamic containers that we are closing
    nsCOMPtr<nsIDynamicContainer> svc =
      do_GetService(mDynamicContainerType.get(), &rv);
    if (NS_SUCCEEDED(rv))
      svc->OnContainerNodeClosed(this);
  }

  nsNavHistoryResult* result = GetResult();
  if (aUpdateView) {
    NS_ENSURE_TRUE(result, NS_ERROR_FAILURE);
    if (result->GetView())
      result->GetView()->ContainerClosed(this);
  }

  // If this is the root container of a result, we can tell the result to stop
  // observing changes, otherwise the result will stay in memory and updates
  // itself till it is cycle collected.
  if (result->mRootNode == this) {
    result->StopObserving();
    // When reopening this node its result will be out of sync.
    // We must clear our children to ensure we will call FillChildren
    // again in such a case.
    if (this->IsQuery())
      this->GetAsQuery()->ClearChildren(PR_TRUE);
    else if (this->IsFolder())
      this->GetAsFolder()->ClearChildren(PR_TRUE);
  }

  return NS_OK;
}


// nsNavHistoryContainerResultNode::FillStats
//
//    This builds up tree statistics from the bottom up. Call with a container
//    and the indent level of that container. To init the full tree, call with
//    the root container. The default indent level is -1, which is appropriate
//    for the root level.
//
//    CALL THIS AFTER FILLING ANY CONTAINER to update the parent and result
//    node pointers, even if you don't care about visit counts and last visit
//    dates.

void
nsNavHistoryContainerResultNode::FillStats()
{
  PRUint32 accessCount = 0;
  PRTime newTime = 0;

  for (PRInt32 i = 0; i < mChildren.Count(); i ++) {
    nsNavHistoryResultNode* node = mChildren[i];
    node->mParent = this;
    node->mIndentLevel = mIndentLevel + 1;
    if (node->IsContainer()) {
      nsNavHistoryContainerResultNode* container = node->GetAsContainer();
      container->mResult = mResult;
      container->FillStats();
    }
    accessCount += node->mAccessCount;
    // this is how container nodes get sorted by date
    // The container gets the most recent time of the child nodes.
    if (node->mTime > newTime)
      newTime = node->mTime;
  }

  if (mExpanded) {
    mAccessCount = accessCount;
    if (!IsQuery() || newTime > mTime)
      mTime = newTime;
  }
}


// nsNavHistoryContainerResultNode::ReverseUpdateStats
//
//    This is used when one container changes to do a minimal update of the
//    tree structure. When something changes, you want to call FillStats if
//    necessary and update this container completely. Then call this function
//    which will walk up the tree and fill in the previous containers.
//
//    Note that you have to tell us by how much our access count changed. Our
//    access count should already be set to the new value; this is used to
//    change the parents without having to re-count all their children.
//
//    This does NOT update the last visit date downward. Therefore, if you are
//    deleting a node that has the most recent last visit date, the parents
//    will not get their last visit dates downshifted accordingly. This is a
//    rather unusual case: we don't often delete things, and we usually don't
//    even show the last visit date for folders. Updating would be slower
//    because we would have to recompute it from scratch.

void
nsNavHistoryContainerResultNode::ReverseUpdateStats(PRInt32 aAccessCountChange)
{
  if (mParent) {
    nsNavHistoryResult* result = GetResult();
    PRBool shouldUpdateView = result && result->GetView() &&
                              mParent->mParent &&
                              mParent->mParent->AreChildrenVisible();

    mParent->mAccessCount += aAccessCountChange;
    PRBool timeChanged = PR_FALSE;
    if (mTime > mParent->mTime) {
      timeChanged = PR_TRUE;
      mParent->mTime = mTime;
    }

    if (shouldUpdateView) {
      result->GetView()->NodeHistoryDetailsChanged(
        static_cast<nsINavHistoryContainerResultNode*>(mParent),
        mParent->mTime,
        mParent->mAccessCount);
    }

    // check sorting, the stats may have caused this node to move if the
    // sorting depended on something we are changing.
    PRUint16 sortMode = mParent->GetSortType();
    PRBool sortingByVisitCount =
      sortMode == nsINavHistoryQueryOptions::SORT_BY_VISITCOUNT_ASCENDING ||
      sortMode == nsINavHistoryQueryOptions::SORT_BY_VISITCOUNT_DESCENDING;
    PRBool sortingByTime =
      sortMode == nsINavHistoryQueryOptions::SORT_BY_DATE_ASCENDING ||
      sortMode == nsINavHistoryQueryOptions::SORT_BY_DATE_DESCENDING;

    if ((sortingByVisitCount && aAccessCountChange != 0) ||
        (sortingByTime && timeChanged)) {
      PRUint32 ourIndex = mParent->FindChild(this);
      EnsureItemPosition(ourIndex);
    }

    mParent->ReverseUpdateStats(aAccessCountChange);
  }
}


// nsNavHistoryContainerResultNode::GetSortType
//
//    This walks up the tree until we find a query result node or the root to
//    get the sorting type.
//
//    See nsNavHistoryQueryResultNode::GetSortType

PRUint16
nsNavHistoryContainerResultNode::GetSortType()
{
  if (mParent)
    return mParent->GetSortType();
  else if (mResult)
    return mResult->mSortingMode;
  NS_NOTREACHED("We should always have a result");
  return nsINavHistoryQueryOptions::SORT_BY_NONE;
}

void
nsNavHistoryContainerResultNode::GetSortingAnnotation(nsACString& aAnnotation)
{
  if (mParent)
    mParent->GetSortingAnnotation(aAnnotation);
  else if (mResult)
    aAnnotation.Assign(mResult->mSortingAnnotation);
  else
    NS_NOTREACHED("We should always have a result");
}

// nsNavHistoryContainerResultNode::GetSortingComparator
//
//    Returns the sorting comparator function for the give sort type.
//    RETURNS NULL if there is no comparator.

nsNavHistoryContainerResultNode::SortComparator
nsNavHistoryContainerResultNode::GetSortingComparator(PRUint16 aSortType)
{
  switch (aSortType)
  {
    case nsINavHistoryQueryOptions::SORT_BY_NONE:
      return &SortComparison_Bookmark;
    case nsINavHistoryQueryOptions::SORT_BY_TITLE_ASCENDING:
      return &SortComparison_TitleLess;
    case nsINavHistoryQueryOptions::SORT_BY_TITLE_DESCENDING:
      return &SortComparison_TitleGreater;
    case nsINavHistoryQueryOptions::SORT_BY_DATE_ASCENDING:
      return &SortComparison_DateLess;
    case nsINavHistoryQueryOptions::SORT_BY_DATE_DESCENDING:
      return &SortComparison_DateGreater;
    case nsINavHistoryQueryOptions::SORT_BY_URI_ASCENDING:
      return &SortComparison_URILess;
    case nsINavHistoryQueryOptions::SORT_BY_URI_DESCENDING:
      return &SortComparison_URIGreater;
    case nsINavHistoryQueryOptions::SORT_BY_VISITCOUNT_ASCENDING:
      return &SortComparison_VisitCountLess;
    case nsINavHistoryQueryOptions::SORT_BY_VISITCOUNT_DESCENDING:
      return &SortComparison_VisitCountGreater;
    case nsINavHistoryQueryOptions::SORT_BY_KEYWORD_ASCENDING:
      return &SortComparison_KeywordLess;
    case nsINavHistoryQueryOptions::SORT_BY_KEYWORD_DESCENDING:
      return &SortComparison_KeywordGreater;
    case nsINavHistoryQueryOptions::SORT_BY_ANNOTATION_ASCENDING:
      return &SortComparison_AnnotationLess;
    case nsINavHistoryQueryOptions::SORT_BY_ANNOTATION_DESCENDING:
      return &SortComparison_AnnotationGreater;
    case nsINavHistoryQueryOptions::SORT_BY_DATEADDED_ASCENDING:
      return &SortComparison_DateAddedLess;
    case nsINavHistoryQueryOptions::SORT_BY_DATEADDED_DESCENDING:
      return &SortComparison_DateAddedGreater;
    case nsINavHistoryQueryOptions::SORT_BY_LASTMODIFIED_ASCENDING:
      return &SortComparison_LastModifiedLess;
    case nsINavHistoryQueryOptions::SORT_BY_LASTMODIFIED_DESCENDING:
      return &SortComparison_LastModifiedGreater;
    case nsINavHistoryQueryOptions::SORT_BY_TAGS_ASCENDING:
      return &SortComparison_TagsLess;
    case nsINavHistoryQueryOptions::SORT_BY_TAGS_DESCENDING:
      return &SortComparison_TagsGreater;
    default:
      NS_NOTREACHED("Bad sorting type");
      return nsnull;
  }
}


// nsNavHistoryContainerResultNode::RecursiveSort
//
//    This is used by Result::SetSortingMode and QueryResultNode::FillChildren to sort
//    the child list.
//
//    This does NOT update any visibility or tree information. The caller will
//    have to completely rebuild the visible list after this.

void
nsNavHistoryContainerResultNode::RecursiveSort(
    const char* aData, SortComparator aComparator)
{
  void* data = const_cast<void*>(static_cast<const void*>(aData));

  mChildren.Sort(aComparator, data);
  for (PRInt32 i = 0; i < mChildren.Count(); i ++) {
    if (mChildren[i]->IsContainer())
      mChildren[i]->GetAsContainer()->RecursiveSort(aData, aComparator);
  }
}


// nsNavHistoryContainerResultNode::FindInsertionPoint
//
//    This returns the index that the given item would fall on if it were to
//    be inserted using the given sorting.

PRUint32
nsNavHistoryContainerResultNode::FindInsertionPoint(
    nsNavHistoryResultNode* aNode, SortComparator aComparator,
    const char* aData, PRBool* aItemExists)
{
  if (aItemExists)
    (*aItemExists) = PR_FALSE;

  if (mChildren.Count() == 0)
    return 0;

  void* data = const_cast<void*>(static_cast<const void*>(aData));

  // The common case is the beginning or the end because this is used to insert
  // new items that are added to history, which is usually sorted by date.
  PRInt32 res;
  res = aComparator(aNode, mChildren[0], data);
  if (res <= 0) {
    if (aItemExists && res == 0)
      (*aItemExists) = PR_TRUE;
    return 0;
  }
  res = aComparator(aNode, mChildren[mChildren.Count() - 1], data);
  if (res >= 0) {
    if (aItemExists && res == 0)
      (*aItemExists) = PR_TRUE;
    return mChildren.Count();
  }

  PRUint32 beginRange = 0; // inclusive
  PRUint32 endRange = mChildren.Count(); // exclusive
  while (1) {
    if (beginRange == endRange)
      return endRange;
    PRUint32 center = beginRange + (endRange - beginRange) / 2;
    PRInt32 res = aComparator(aNode, mChildren[center], data);
    if (res <= 0) {
      endRange = center; // left side
      if (aItemExists && res == 0)
        (*aItemExists) = PR_TRUE;
    }
    else {
      beginRange = center + 1; // right site
    }
  }
}


// nsNavHistoryContainerResultNode::DoesChildNeedResorting
//
//    This checks the child node at the given index to see if its sorting is
//    correct. Returns true if not and it should be resorted. This is called
//    when nodes are updated and we need to see whether we need to move it.

PRBool
nsNavHistoryContainerResultNode::DoesChildNeedResorting(PRUint32 aIndex,
    SortComparator aComparator, const char* aData)
{
  NS_ASSERTION(aIndex >= 0 && aIndex < PRUint32(mChildren.Count()),
               "Input index out of range");
  if (mChildren.Count() == 1)
    return PR_FALSE;

  void* data = const_cast<void*>(static_cast<const void*>(aData));

  if (aIndex > 0) {
    // compare to previous item
    if (aComparator(mChildren[aIndex - 1], mChildren[aIndex], data) > 0)
      return PR_TRUE;
  }
  if (aIndex < PRUint32(mChildren.Count()) - 1) {
    // compare to next item
    if (aComparator(mChildren[aIndex], mChildren[aIndex + 1], data) > 0)
      return PR_TRUE;
  }
  return PR_FALSE;
}


/* static */
PRInt32 nsNavHistoryContainerResultNode::SortComparison_StringLess(
    const nsAString& a, const nsAString& b) {

  nsNavHistory* history = nsNavHistory::GetHistoryService();
  NS_ENSURE_TRUE(history, 0);
  nsICollation* collation = history->GetCollation();
  NS_ENSURE_TRUE(collation, 0);

  PRInt32 res = 0;
  collation->CompareString(nsICollation::kCollationCaseInSensitive, a, b, &res);
  return res;
}

// nsNavHistoryContainerResultNode::SortComparison_Bookmark
//
//    When there are bookmark indices, we should never have ties, so we don't
//    need to worry about tiebreaking. When there are no bookmark indices,
//    everything will be -1 and we don't worry about sorting.

PRInt32 nsNavHistoryContainerResultNode::SortComparison_Bookmark(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b, void* closure)
{
  return a->mBookmarkIndex - b->mBookmarkIndex;
}

// nsNavHistoryContainerResultNode::SortComparison_Title*
//
//    These are a little more complicated because they do a localization
//    conversion. If this is too slow, we can compute the sort keys once in
//    advance, sort that array, and then reorder the real array accordingly.
//    This would save some key generations.
//
//    The collation object must be allocated before sorting on title!

PRInt32 nsNavHistoryContainerResultNode::SortComparison_TitleLess(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b, void* closure)
{
  PRUint32 aType;
  a->GetType(&aType);

  PRInt32 value = SortComparison_StringLess(NS_ConvertUTF8toUTF16(a->mTitle),
                                            NS_ConvertUTF8toUTF16(b->mTitle));
  if (value == 0) {
    // resolve by URI
    if (a->IsURI()) {
      value = a->mURI.Compare(b->mURI.get());
    }
    if (value == 0) {
      // resolve by date
      value = ComparePRTime(a->mTime, b->mTime);
      if (value == 0)
        value = nsNavHistoryContainerResultNode::SortComparison_Bookmark(a, b, closure);
    }
  }
  return value;
}
PRInt32 nsNavHistoryContainerResultNode::SortComparison_TitleGreater(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b, void* closure)
{
  return -SortComparison_TitleLess(a, b, closure);
}

// nsNavHistoryContainerResultNode::SortComparison_Date*
//
//    Equal times will be very unusual, but it is important that there is some
//    deterministic ordering of the results so they don't move around.

PRInt32 nsNavHistoryContainerResultNode::SortComparison_DateLess(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b, void* closure)
{
  PRInt32 value = ComparePRTime(a->mTime, b->mTime);
  if (value == 0) {
    value = SortComparison_StringLess(NS_ConvertUTF8toUTF16(a->mTitle),
                                      NS_ConvertUTF8toUTF16(b->mTitle));
    if (value == 0)
      value = nsNavHistoryContainerResultNode::SortComparison_Bookmark(a, b, closure);
  }
  return value;
}
PRInt32 nsNavHistoryContainerResultNode::SortComparison_DateGreater(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b, void* closure)
{
  return -nsNavHistoryContainerResultNode::SortComparison_DateLess(a, b, closure);
}

// nsNavHistoryContainerResultNode::SortComparison_DateAdded*
//

PRInt32 nsNavHistoryContainerResultNode::SortComparison_DateAddedLess(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b, void* closure)
{
  PRInt32 value = ComparePRTime(a->mDateAdded, b->mDateAdded);
  if (value == 0) {
    value = SortComparison_StringLess(NS_ConvertUTF8toUTF16(a->mTitle),
                                      NS_ConvertUTF8toUTF16(b->mTitle));
    if (value == 0)
      value = nsNavHistoryContainerResultNode::SortComparison_Bookmark(a, b, closure);
  }
  return value;
}
PRInt32 nsNavHistoryContainerResultNode::SortComparison_DateAddedGreater(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b, void* closure)
{
  return -nsNavHistoryContainerResultNode::SortComparison_DateAddedLess(a, b, closure);
}


// nsNavHistoryContainerResultNode::SortComparison_LastModified*
//

PRInt32 nsNavHistoryContainerResultNode::SortComparison_LastModifiedLess(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b, void* closure)
{
  PRInt32 value = ComparePRTime(a->mLastModified, b->mLastModified);
  if (value == 0) {
    value = SortComparison_StringLess(NS_ConvertUTF8toUTF16(a->mTitle),
                                      NS_ConvertUTF8toUTF16(b->mTitle));
    if (value == 0)
      value = nsNavHistoryContainerResultNode::SortComparison_Bookmark(a, b, closure);
  }
  return value;
}
PRInt32 nsNavHistoryContainerResultNode::SortComparison_LastModifiedGreater(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b, void* closure)
{
  return -nsNavHistoryContainerResultNode::SortComparison_LastModifiedLess(a, b, closure);
}

// nsNavHistoryContainerResultNode::SortComparison_URI*
//
//    Certain types of parent nodes are treated specially because URIs are not
//    valid (like days or hosts).

PRInt32 nsNavHistoryContainerResultNode::SortComparison_URILess(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b, void* closure)
{
  PRInt32 value;
  if (a->IsURI() && b->IsURI()) {
    // normal URI or visit
    value = a->mURI.Compare(b->mURI.get());
  } else {
    // for everything else, use title (= host name)
    value = SortComparison_StringLess(NS_ConvertUTF8toUTF16(a->mTitle),
                                      NS_ConvertUTF8toUTF16(b->mTitle));
  }

  if (value == 0) {
    value = ComparePRTime(a->mTime, b->mTime);
    if (value == 0)
      value = nsNavHistoryContainerResultNode::SortComparison_Bookmark(a, b, closure);
  }
  return value;
}
PRInt32 nsNavHistoryContainerResultNode::SortComparison_URIGreater(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b, void* closure)
{
  return -SortComparison_URILess(a, b, closure);
}

// nsNavHistoryContainerResultNode::SortComparison_Keyword*
PRInt32 nsNavHistoryContainerResultNode::SortComparison_KeywordLess(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b, void* closure)
{
  PRInt32 value = 0;
  if (a->mItemId != -1 || b->mItemId != -1) {
    // compare the keywords
    nsAutoString keywordA, keywordB;
    nsNavBookmarks* bookmarks = nsNavBookmarks::GetBookmarksService();
    NS_ENSURE_TRUE(bookmarks, 0);

    nsresult rv;
    if (a->mItemId != -1) {
      rv = bookmarks->GetKeywordForBookmark(a->mItemId, keywordA);
      NS_ENSURE_SUCCESS(rv, 0);
    }
    if (b->mItemId != -1) {
      rv = bookmarks->GetKeywordForBookmark(b->mItemId, keywordB);
      NS_ENSURE_SUCCESS(rv, 0);
    }

    value = SortComparison_StringLess(keywordA, keywordB);
  }

  // fall back to title sorting
  if (value == 0)
    value = SortComparison_TitleLess(a, b, closure);

  return value;
}

PRInt32 nsNavHistoryContainerResultNode::SortComparison_KeywordGreater(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b, void* closure)
{
  return -SortComparison_KeywordLess(a, b, closure);
}

PRInt32 nsNavHistoryContainerResultNode::SortComparison_AnnotationLess(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b, void* closure)
{
  nsCAutoString annoName(static_cast<char*>(closure));
  NS_ENSURE_TRUE(!annoName.IsEmpty(), 0);
  
  nsNavBookmarks* bookmarks = nsNavBookmarks::GetBookmarksService();
  NS_ENSURE_TRUE(bookmarks, 0);

  PRBool a_itemAnno = PR_FALSE;
  PRBool b_itemAnno = PR_FALSE;

  // Not used for item annos
  nsCOMPtr<nsIURI> a_uri, b_uri;
  if (a->mItemId != -1) {
    a_itemAnno = PR_TRUE;
  } else {
    nsCAutoString spec;
    if (NS_SUCCEEDED(a->GetUri(spec)))
      NS_NewURI(getter_AddRefs(a_uri), spec);
    NS_ENSURE_TRUE(a_uri, 0);
  }

  if (b->mItemId != -1) {
    b_itemAnno = PR_TRUE;
  } else {
    nsCAutoString spec;
    if (NS_SUCCEEDED(b->GetUri(spec)))
      NS_NewURI(getter_AddRefs(b_uri), spec);
    NS_ENSURE_TRUE(b_uri, 0);
  }

  nsAnnotationService* annosvc = nsAnnotationService::GetAnnotationService();
  NS_ENSURE_TRUE(annosvc, 0);

  PRBool a_hasAnno, b_hasAnno;
  if (a_itemAnno) {
    NS_ENSURE_SUCCESS(annosvc->ItemHasAnnotation(a->mItemId, annoName,
                                                 &a_hasAnno), 0);
  } else {
    NS_ENSURE_SUCCESS(annosvc->PageHasAnnotation(a_uri, annoName,
                                                 &a_hasAnno), 0);
  }
  if (b_itemAnno) {
    NS_ENSURE_SUCCESS(annosvc->ItemHasAnnotation(b->mItemId, annoName,
                                                 &b_hasAnno), 0);
  } else {
    NS_ENSURE_SUCCESS(annosvc->PageHasAnnotation(b_uri, annoName,
                                                 &b_hasAnno), 0);    
  }

  PRInt32 value = 0;
  if (a_hasAnno || b_hasAnno) {
    PRUint16 annoType;
    if (a_hasAnno) {
      if (a_itemAnno) {
        NS_ENSURE_SUCCESS(annosvc->GetItemAnnotationType(a->mItemId,
                                                         annoName,
                                                         &annoType), 0);
      } else {
        NS_ENSURE_SUCCESS(annosvc->GetPageAnnotationType(a_uri, annoName,
                                                         &annoType), 0);
      }
    }
    if (b_hasAnno) {
      PRUint16 b_type;
      if (b_itemAnno) {
        NS_ENSURE_SUCCESS(annosvc->GetItemAnnotationType(b->mItemId,
                                                         annoName,
                                                         &b_type), 0);
      } else {
        NS_ENSURE_SUCCESS(annosvc->GetPageAnnotationType(b_uri, annoName,
                                                         &b_type), 0);
      }
      // We better make the API not support this state, really
      // XXXmano: this is actually wrong for double<->int and int64<->int32
      if (a_hasAnno && b_type != annoType)
        return 0;
      annoType = b_type;
    }

#define GET_ANNOTATIONS_VALUES(METHOD_ITEM, METHOD_PAGE, A_VAL, B_VAL)        \
        if (a_hasAnno) {                                                      \
          if (a_itemAnno) {                                                   \
            NS_ENSURE_SUCCESS(annosvc->METHOD_ITEM(a->mItemId, annoName,      \
                                                   A_VAL), 0);                \
          } else {                                                            \
            NS_ENSURE_SUCCESS(annosvc->METHOD_PAGE(a_uri, annoName,           \
                                                   A_VAL), 0);                \
          }                                                                   \
        }                                                                     \
        if (b_hasAnno) {                                                      \
          if (b_itemAnno) {                                                   \
            NS_ENSURE_SUCCESS(annosvc->METHOD_ITEM(b->mItemId, annoName,      \
                                                   B_VAL), 0);                \
          } else {                                                            \
            NS_ENSURE_SUCCESS(annosvc->METHOD_PAGE(b_uri, annoName,           \
                                                   B_VAL), 0);                \
          }                                                                   \
        }

    // Surprising as it is, we don't support sorting by a binary annotation
    if (annoType != nsIAnnotationService::TYPE_BINARY) {
      if (annoType == nsIAnnotationService::TYPE_STRING) {
        nsAutoString a_val, b_val;
        GET_ANNOTATIONS_VALUES(GetItemAnnotationString,
                               GetPageAnnotationString, a_val, b_val);
        value = SortComparison_StringLess(a_val, b_val);
      }
      else if (annoType == nsIAnnotationService::TYPE_INT32) {
        PRInt32 a_val = 0, b_val = 0;
        GET_ANNOTATIONS_VALUES(GetItemAnnotationInt32,
                               GetPageAnnotationInt32, &a_val, &b_val);
        value = (a_val < b_val) ? -1 : (a_val > b_val) ? 1 : 0;
      }
      else if (annoType == nsIAnnotationService::TYPE_INT64) {
        PRInt64 a_val = 0, b_val = 0;
        GET_ANNOTATIONS_VALUES(GetItemAnnotationInt64,
                               GetPageAnnotationInt64, &a_val, &b_val);
        value = (a_val < b_val) ? -1 : (a_val > b_val) ? 1 : 0;
      }
      else if (annoType == nsIAnnotationService::TYPE_DOUBLE) {
        double a_val = 0, b_val = 0;
        GET_ANNOTATIONS_VALUES(GetItemAnnotationDouble,
                               GetPageAnnotationDouble, &a_val, &b_val);
        value = (a_val < b_val) ? -1 : (a_val > b_val) ? 1 : 0;
      }
    }
  }

  // Note we also fall back to the title-sorting route one of the items didn't
  // have the annotation set or if both had it set but in a different storage
  // type
  if (value == 0)
    return SortComparison_TitleLess(a, b, nsnull);

  return value;
}
PRInt32 nsNavHistoryContainerResultNode::SortComparison_AnnotationGreater(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b, void* closure)
{
  return -SortComparison_AnnotationLess(a, b, closure);
}

// nsNavHistoryContainerResultNode::SortComparison_VisitCount*
//
//    Fall back on dates for conflict resolution

PRInt32 nsNavHistoryContainerResultNode::SortComparison_VisitCountLess(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b, void* closure)
{
  PRInt32 value = CompareIntegers(a->mAccessCount, b->mAccessCount);
  if (value == 0) {
    value = ComparePRTime(a->mTime, b->mTime);
    if (value == 0)
      value = nsNavHistoryContainerResultNode::SortComparison_Bookmark(a, b, closure);
  }
  return value;
}
PRInt32 nsNavHistoryContainerResultNode::SortComparison_VisitCountGreater(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b, void* closure)
{
  return -nsNavHistoryContainerResultNode::SortComparison_VisitCountLess(a, b, closure);
}


// nsNavHistoryContainerResultNode::SortComparison_Tags*
PRInt32 nsNavHistoryContainerResultNode::SortComparison_TagsLess(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b, void* closure)
{
  PRInt32 value = 0;
  nsAutoString aTags, bTags;

  nsresult rv = a->GetTags(aTags);
  NS_ENSURE_SUCCESS(rv, 0);

  rv = b->GetTags(bTags);
  NS_ENSURE_SUCCESS(rv, 0);

  value = SortComparison_StringLess(aTags, bTags);

  // fall back to title sorting
  if (value == 0)
    value = SortComparison_TitleLess(a, b, closure);

  return value;
}

PRInt32 nsNavHistoryContainerResultNode::SortComparison_TagsGreater(
    nsNavHistoryResultNode* a, nsNavHistoryResultNode* b, void* closure)
{
  return -SortComparison_TagsLess(a, b, closure);
}

// nsNavHistoryContainerResultNode::FindChildURI
//
//    Searches this folder for a node with the given URI. Returns null if not
//    found. Does not addref the node!

nsNavHistoryResultNode*
nsNavHistoryContainerResultNode::FindChildURI(const nsACString& aSpec,
    PRUint32* aNodeIndex)
{
  for (PRInt32 i = 0; i < mChildren.Count(); i ++) {
    if (mChildren[i]->IsURI()) {
      if (aSpec.Equals(mChildren[i]->mURI)) {
        *aNodeIndex = i;
        return mChildren[i];
      }
    }
  }
  return nsnull;
}

//  nsNavHistoryContainerResultNode::FindChildContainerByName
//
//    Searches this container for a subfolder with the given name. This is used
//    to find host and "day" nodes. Returns null if not found. DOES NOT ADDREF.

nsNavHistoryContainerResultNode*
nsNavHistoryContainerResultNode::FindChildContainerByName(
    const nsACString& aTitle, PRUint32* aNodeIndex)
{
  for (PRInt32 i = 0; i < mChildren.Count(); i ++) {
    if (mChildren[i]->IsContainer()) {
      nsNavHistoryContainerResultNode* container =
          mChildren[i]->GetAsContainer();
      if (container->mTitle.Equals(aTitle)) {
        *aNodeIndex = i;
        return container;
      }
    }
  }
  return nsnull;
}


// nsNavHistoryContainerResultNode::InsertChildAt
//
//    This does the work of adding a child to the container. This child can be
//    a container, or a single item that may even be collapsed with the
//    adjacent ones.
//
//    Some inserts are "temporary" meaning that they are happening immediately
//    after a temporary remove. We do this when movings elements when they
//    change to keep them in the proper sorting position. In these cases, we
//    don't need to recompute any statistics.

nsresult
nsNavHistoryContainerResultNode::InsertChildAt(nsNavHistoryResultNode* aNode,
                                               PRInt32 aIndex,
                                               PRBool aIsTemporary)
{
  nsNavHistoryResult* result = GetResult();
  NS_ENSURE_TRUE(result, NS_ERROR_FAILURE);

  aNode->mParent = this;
  aNode->mIndentLevel = mIndentLevel + 1;
  if (! aIsTemporary && aNode->IsContainer()) {
    // need to update all the new item's children
    nsNavHistoryContainerResultNode* container = aNode->GetAsContainer();
    container->mResult = mResult;
    container->FillStats();
  }

  if (! mChildren.InsertObjectAt(aNode, aIndex))
    return NS_ERROR_OUT_OF_MEMORY;

  // update our (the container) stats and refresh our row on the screen
  if (! aIsTemporary) {
    mAccessCount += aNode->mAccessCount;
    if (mTime < aNode->mTime)
      mTime = aNode->mTime;
    if (result->GetView() && (!mParent || mParent->AreChildrenVisible()))
      result->GetView()->NodeHistoryDetailsChanged(
          static_cast<nsINavHistoryContainerResultNode*>(this), mTime,
          mAccessCount);
    ReverseUpdateStats(aNode->mAccessCount);
  }

  // Update tree if we are visible. Note that we could be here and not expanded,
  // like when there is a bookmark folder being updated because its parent is
  // visible.
  if (result->GetView() && AreChildrenVisible())
    result->GetView()->NodeInserted(this, aNode, aIndex);
  return NS_OK;
}


// nsNavHistoryContainerResultNode::InsertSortedChild
//
//    This locates the proper place for insertion according to the current sort
//    and calls InsertChildAt

nsresult
nsNavHistoryContainerResultNode::InsertSortedChild(
    nsNavHistoryResultNode* aNode, 
    PRBool aIsTemporary, PRBool aIgnoreDuplicates)
{

  if (mChildren.Count() == 0)
    return InsertChildAt(aNode, 0, aIsTemporary);

  SortComparator comparator = GetSortingComparator(GetSortType());
  if (comparator) {
    // When inserting a new node, it must have proper statistics because we use
    // them to find the correct insertion point. The insert function will then
    // recompute these statistics and fill in the proper parents and hierarchy
    // level. Doing this twice shouldn't be a large performance penalty because
    // when we are inserting new containers, they typically contain only one
    // item (because we've browsed a new page).
    if (! aIsTemporary && aNode->IsContainer()) {
      // need to update all the new item's children
      nsNavHistoryContainerResultNode* container = aNode->GetAsContainer();
      container->mResult = mResult;
      container->FillStats();
    }

    nsCAutoString sortingAnnotation;
    GetSortingAnnotation(sortingAnnotation);
    PRBool itemExists;
    PRUint32 position = FindInsertionPoint(aNode, comparator, 
                                           sortingAnnotation.get(), 
                                           &itemExists);
    if (aIgnoreDuplicates && itemExists)
      return NS_OK;

    return InsertChildAt(aNode, position, aIsTemporary);
  }
  return InsertChildAt(aNode, mChildren.Count(), aIsTemporary);
}

// nsNavHistoryContainerResultNode::EnsureItemPosition
//
//  This checks if the item at aIndex is located correctly given the sorting
//  move. If it's not, the item is moved, and the result view are notified.
//
//  Returns true if the item position has been changed, false otherwise.

PRBool
nsNavHistoryContainerResultNode::EnsureItemPosition(PRUint32 aIndex) {
  NS_ASSERTION(aIndex >= 0 && aIndex < (PRUint32)mChildren.Count(), "Invalid index");
  if (aIndex < 0 || aIndex >= (PRUint32)mChildren.Count())
    return PR_FALSE;

  SortComparator comparator = GetSortingComparator(GetSortType());
  if (!comparator)
    return PR_FALSE;

  nsCAutoString sortAnno;
  GetSortingAnnotation(sortAnno);
  if (!DoesChildNeedResorting(aIndex, comparator, sortAnno.get()))
    return PR_FALSE;

  nsRefPtr<nsNavHistoryResultNode> node(mChildren[aIndex]);
  mChildren.RemoveObjectAt(aIndex);

  PRUint32 newIndex = FindInsertionPoint(
                          node, comparator,sortAnno.get(), nsnull);
  mChildren.InsertObjectAt(node.get(), newIndex);

  nsNavHistoryResult* result = GetResult();
  NS_ENSURE_TRUE(result, PR_TRUE);

  if (result->GetView() && AreChildrenVisible())
    result->GetView()->NodeMoved(node, this, aIndex, this, newIndex);

  return PR_TRUE;
}

// nsNavHistoryContainerResultNode::MergeResults
//
//    This takes a list of nodes and merges them into the current result set.
//    Any containers that are added must already be sorted.
//
//    This assumes that the items in 'aAddition' are new visits or
//    replacement URIs. We do not update visits.
//
//    PERFORMANCE: In the future, we can do more updates incrementally using
//    this function. When a URI changes in a way we can't easily handle,
//    construct a query with each query object specifying an exact match for
//    the URI in question. Then remove all instances of that URI in the result
//    and call this function.

void
nsNavHistoryContainerResultNode::MergeResults(
    nsCOMArray<nsNavHistoryResultNode>* aAddition)
{
  // Generally we will have very few (one) entries in the addition list, so
  // just iterate through it. If we find we may have a lot, we may want to do
  // some hashing to help with the merge.
  for (PRUint32 i = 0; i < PRUint32(aAddition->Count()); i ++) {
    nsNavHistoryResultNode* curAddition = (*aAddition)[i];
    if (curAddition->IsContainer()) {
      PRUint32 containerIndex;
      nsNavHistoryContainerResultNode* container =
        FindChildContainerByName(curAddition->mTitle, &containerIndex);
      if (container) {
        // recursively merge with the existing container
        container->MergeResults(&curAddition->GetAsContainer()->mChildren);
      } else {
        // need to add the new container to our result.
        InsertSortedChild(curAddition);
      }
    } else {
      if (curAddition->IsVisit()) {
        // insert the new visit
        InsertSortedChild(curAddition);
      } else {
        // add the URI, replacing a current one if any
        PRUint32 oldIndex;
        nsNavHistoryResultNode* oldNode =
          FindChildURI(curAddition->mURI, &oldIndex);
        if (oldNode) {
          // if we don't have a parent (for example, the history
          // sidebar, when sorted by last visited or most visited)
          // we have to manually Remove/Insert instead of Replace
          // see bug #389782 for details
          if (mParent)
            ReplaceChildURIAt(oldIndex, curAddition);
          else {
            RemoveChildAt(oldIndex, PR_TRUE);
            InsertSortedChild(curAddition, PR_TRUE);
          }
        }
        else
          InsertSortedChild(curAddition);
      }
    }
  }
}


// nsNavHistoryContainerResultNode::ReplaceChildURIAt
//
//    This is called to replace a leaf node. It will update tree stats
//    and redraw the view if any. You can not use this to replace a container.
//
//    This assumes that the node is being replaced with a newer version of
//    itself and so its visit count will not go down. Also, this means that the
//    collapsing of duplicates will not change.

nsresult
nsNavHistoryContainerResultNode::ReplaceChildURIAt(PRUint32 aIndex,
    nsNavHistoryResultNode* aNode)
{
  NS_ASSERTION(aIndex < PRUint32(mChildren.Count()),
               "Invalid index for replacement");
  NS_ASSERTION(mChildren[aIndex]->IsURI(),
               "Can not use ReplaceChildAt for a node of another type");
  NS_ASSERTION(mChildren[aIndex]->mURI.Equals(aNode->mURI),
               "We must replace a URI with an updated one of the same");

  aNode->mParent = this;
  aNode->mIndentLevel = mIndentLevel + 1;

  // update tree stats if needed
  PRUint32 accessCountChange = aNode->mAccessCount - mChildren[aIndex]->mAccessCount;
  if (accessCountChange != 0 || mChildren[aIndex]->mTime != aNode->mTime) {
    NS_ASSERTION(aNode->mAccessCount >= mChildren[aIndex]->mAccessCount,
                 "Replacing a node with one back in time or some nonmatching node");

    mAccessCount += accessCountChange;
    if (mTime < aNode->mTime)
      mTime = aNode->mTime;
    ReverseUpdateStats(accessCountChange);
  }

  // Hold a reference so it doesn't go away as soon as we remove it from the
  // array. This needs to be passed to the view.
  nsRefPtr<nsNavHistoryResultNode> oldItem = mChildren[aIndex];

  // actually replace
  if (! mChildren.ReplaceObjectAt(aNode, aIndex))
    return NS_ERROR_FAILURE;

  // update view
  nsNavHistoryResult* result = GetResult();
  NS_ENSURE_TRUE(result, NS_ERROR_FAILURE);
  if (result->GetView() && AreChildrenVisible())
    result->GetView()->NodeReplaced(this, oldItem, aNode, aIndex);

  mChildren[aIndex]->OnRemoving();
  return NS_OK;
}


// nsNavHistoryContainerResultNode::RemoveChildAt
//
//    This does all the work of removing a child from this container, including
//    updating the tree if necessary. Note that we do not need to be open for
//    this to work.
//
//    Some removes are "temporary" meaning that they'll just get inserted
//    again. We do this for resorting. In these cases, we don't need to
//    recompute any statistics, and we shouldn't notify those container that
//    they are being removed.

nsresult
nsNavHistoryContainerResultNode::RemoveChildAt(PRInt32 aIndex,
                                               PRBool aIsTemporary)
{
  NS_ASSERTION(aIndex >= 0 && aIndex < mChildren.Count(), "Invalid index");

  nsNavHistoryResult* result = GetResult();
  NS_ENSURE_TRUE(result, NS_ERROR_FAILURE);

  // hold an owning reference to keep from expiring while we work with it
  nsRefPtr<nsNavHistoryResultNode> oldNode = mChildren[aIndex];

  // stats
  PRUint32 oldAccessCount = 0;
  if (! aIsTemporary) {
    oldAccessCount = mAccessCount;
    mAccessCount -= mChildren[aIndex]->mAccessCount;
    NS_ASSERTION(mAccessCount >= 0, "Invalid access count while updating!");
  }

  // remove from our list and notify the tree
  mChildren.RemoveObjectAt(aIndex);
  if (result->GetView() && AreChildrenVisible())
    result->GetView()->NodeRemoved(this, oldNode, aIndex);

  if (! aIsTemporary) {
    ReverseUpdateStats(mAccessCount - oldAccessCount);
    oldNode->OnRemoving();
  }
  return NS_OK;
}


// nsNavHistoryContainerResultNode::RecursiveFindURIs
//
//    This function searches for matches for the given URI.
//
//    If aOnlyOne is set, it will terminate as soon as it finds a single match.
//    This would be used when there are URI results so there will only ever be
//    one copy of any URI.
//
//    When aOnlyOne is false, it will check all elements. This is for visit
//    style results that may have multiple copies of any given URI.

void
nsNavHistoryContainerResultNode::RecursiveFindURIs(PRBool aOnlyOne,
    nsNavHistoryContainerResultNode* aContainer, const nsCString& aSpec,
    nsCOMArray<nsNavHistoryResultNode>* aMatches)
{
  for (PRInt32 child = 0; child < aContainer->mChildren.Count(); child ++) {
    PRUint32 type;
    aContainer->mChildren[child]->GetType(&type);
    if (nsNavHistoryResultNode::IsTypeURI(type)) {
      // compare URIs
      nsNavHistoryResultNode* uriNode = aContainer->mChildren[child];
      if (uriNode->mURI.Equals(aSpec)) {
        // found
        aMatches->AppendObject(uriNode);
        if (aOnlyOne)
          return;
      }
    }
  }
}


// nsNavHistoryContainerResultNode::UpdateURIs
//
//    If aUpdateSort is true, we will also update the sorting of this item.
//    Normally you want this to be true, but it can be false if the thing you
//    are changing can not affect sorting (like favicons).
//
//    You should NOT change any child lists as part of the callback function.

void
nsNavHistoryContainerResultNode::UpdateURIs(PRBool aRecursive, PRBool aOnlyOne,
    PRBool aUpdateSort, const nsCString& aSpec,
    void (*aCallback)(nsNavHistoryResultNode*,void*, nsNavHistoryResult*), void* aClosure)
{
  nsNavHistoryResult* result = GetResult();
  if (! result) {
    NS_NOTREACHED("Must have a result for this query");
    return;
  }

  // this needs to be owning since sometimes we remove and re-insert nodes
  // in their parents and we don't want them to go away.
  nsCOMArray<nsNavHistoryResultNode> matches;

  if (aRecursive) {
    RecursiveFindURIs(aOnlyOne, this, aSpec, &matches);
  } else if (aOnlyOne) {
    PRUint32 nodeIndex;
    nsNavHistoryResultNode* node = FindChildURI(aSpec, &nodeIndex);
    if (node)
      matches.AppendObject(node);
  } else {
    NS_NOTREACHED("UpdateURIs does not handle nonrecursive updates of multiple items.");
    // this case easy to add if you need it, just find all the matching URIs
    // at this level. However, this isn't currently used. History uses recursive,
    // Bookmarks uses one level and knows that the match is unique.
    return;
  }
  if (matches.Count() == 0)
    return;

  // PERFORMANCE: This updates each container for each child in it that
  // changes. In some cases, many elements have changed inside the same
  // container. It would be better to compose a list of containers, and
  // update each one only once for all the items that have changed in it.
  for (PRInt32 i = 0; i < matches.Count(); i ++)
  {
    nsNavHistoryResultNode* node = matches[i];
    nsNavHistoryContainerResultNode* parent = node->mParent;
    if (! parent) {
      NS_NOTREACHED("All URI nodes being updated must have parents");
      continue;
    }

    PRUint32 oldAccessCount = node->mAccessCount;
    PRTime oldTime = node->mTime;
    aCallback(node, aClosure, result);

    PRBool childrenVisible = result->GetView() != nsnull && parent->AreChildrenVisible();

    if (oldAccessCount != node->mAccessCount || oldTime != node->mTime) {
      // need to update/redraw the parent
      parent->mAccessCount += node->mAccessCount - oldAccessCount;
      if (node->mTime > parent->mTime)
        parent->mTime = node->mTime;
      if (childrenVisible)
        result->GetView()->NodeHistoryDetailsChanged(
            static_cast<nsINavHistoryContainerResultNode*>(parent),
            parent->mTime,
            parent->mAccessCount);
      parent->ReverseUpdateStats(node->mAccessCount - oldAccessCount);
    }

    if (aUpdateSort) {
      PRInt32 childIndex = parent->FindChild(node);
      NS_ASSERTION(childIndex >= 0, "Could not find child we just got a reference to");
      if (childIndex >= 0)
        parent->EnsureItemPosition(childIndex);
    }
  }
}


// nsNavHistoryContainerResultNode::ChangeTitles
//
//    This is used to update the titles in the tree. This is called from both
//    query and bookmark folder containers to update the tree. Bookmark folders
//    should be sure to set recursive to false, since child folders will have
//    their own callbacks registered.

static void setTitleCallback(
    nsNavHistoryResultNode* aNode, void* aClosure,
    nsNavHistoryResult* aResult)
{
  const nsACString* newTitle = reinterpret_cast<nsACString*>(aClosure);
  aNode->mTitle = *newTitle;

  if (aResult && aResult->GetView() &&
      (!aNode->mParent || aNode->mParent->AreChildrenVisible())) {
    aResult->GetView()->NodeTitleChanged(aNode, *newTitle);
  }
}
nsresult
nsNavHistoryContainerResultNode::ChangeTitles(nsIURI* aURI,
                                              const nsACString& aNewTitle,
                                              PRBool aRecursive,
                                              PRBool aOnlyOne)
{
  // uri string
  nsCAutoString uriString;
  nsresult rv = aURI->GetSpec(uriString);
  NS_ENSURE_SUCCESS(rv, rv);

  // The recursive function will update the result's tree nodes, but only if we
  // give it a non-null pointer. So if there isn't a tree, just pass NULL so
  // it doesn't bother trying to call the result.
  nsNavHistoryResult* result = GetResult();
  NS_ENSURE_TRUE(result, NS_ERROR_FAILURE);

  PRUint16 sortType = GetSortType();
  PRBool updateSorting =
    (sortType == nsINavHistoryQueryOptions::SORT_BY_TITLE_ASCENDING ||
     sortType == nsINavHistoryQueryOptions::SORT_BY_TITLE_DESCENDING);

  UpdateURIs(aRecursive, aOnlyOne, updateSorting, uriString,
             setTitleCallback,
             const_cast<void*>(reinterpret_cast<const void*>(&aNewTitle)));
  return NS_OK;
}


// nsNavHistoryContainerResultNode::GetHasChildren
//
//    Complex containers (folders and queries) will override this. Here, we
//    handle the case of simple containers (like host groups) where the children
//    are always stored.

NS_IMETHODIMP
nsNavHistoryContainerResultNode::GetHasChildren(PRBool *aHasChildren)
{
  *aHasChildren = (mChildren.Count() > 0);
  return NS_OK;
}


// nsNavHistoryContainerResultNode::GetChildCount
//
//    This function is only valid when the node is opened.

NS_IMETHODIMP
nsNavHistoryContainerResultNode::GetChildCount(PRUint32* aChildCount)
{
  if (! mExpanded)
    return NS_ERROR_NOT_AVAILABLE;
  *aChildCount = mChildren.Count();
  return NS_OK;
}


// nsNavHistoryContainerResultNode::GetChild

NS_IMETHODIMP
nsNavHistoryContainerResultNode::GetChild(PRUint32 aIndex,
                                          nsINavHistoryResultNode** _retval)
{
  if (! mExpanded)
    return NS_ERROR_NOT_AVAILABLE;
  if (aIndex >= PRUint32(mChildren.Count()))
    return NS_ERROR_INVALID_ARG;
  NS_ADDREF(*_retval = mChildren[aIndex]);
  return NS_OK;
}


NS_IMETHODIMP
nsNavHistoryContainerResultNode::GetChildIndex(nsINavHistoryResultNode* aNode,
                                               PRUint32* _retval)
{
  if (!mExpanded)
    return NS_ERROR_NOT_AVAILABLE;

  *_retval = FindChild(static_cast<nsNavHistoryResultNode*>(aNode));
  if (*_retval == -1)
    return NS_ERROR_INVALID_ARG;

  return NS_OK;
}


NS_IMETHODIMP
nsNavHistoryContainerResultNode::FindNodeByDetails(const nsACString& aURIString,
                                                   PRTime aTime,
                                                   PRInt64 aItemId,
                                                   PRBool aRecursive,
                                                   nsINavHistoryResultNode** _retval) {
  if (!mExpanded)
    return NS_ERROR_NOT_AVAILABLE;

  *_retval = nsnull;
  for (PRInt32 i = 0; i < mChildren.Count(); i++) {
    if (mChildren[i]->mURI.Equals(aURIString) &&
        mChildren[i]->mTime == aTime &&
        mChildren[i]->mItemId == aItemId) {
      *_retval = mChildren[i];
      break;
    }

    if (aRecursive && mChildren[i]->IsContainer()) {
      nsNavHistoryContainerResultNode* asContainer =
        mChildren[i]->GetAsContainer();
      if (asContainer->mExpanded) {
        nsresult rv = asContainer->FindNodeByDetails(aURIString, aTime,
                                                     aItemId,
                                                     aRecursive,
                                                     _retval);
                                                      
        if (NS_SUCCEEDED(rv) && _retval)
          break;
      }
    }
  }
  NS_IF_ADDREF(*_retval);
  return NS_OK;
}

// nsNavHistoryContainerResultNode::GetChildrenReadOnly
//
//    Overridden for folders to query the bookmarks service directly.

NS_IMETHODIMP
nsNavHistoryContainerResultNode::GetChildrenReadOnly(PRBool *aChildrenReadOnly)
{
  *aChildrenReadOnly = mChildrenReadOnly;
  return NS_OK;
}


// nsNavHistoryContainerResultNode::GetDynamicContainerType

NS_IMETHODIMP
nsNavHistoryContainerResultNode::GetDynamicContainerType(
    nsACString& aDynamicContainerType)
{
  aDynamicContainerType = mDynamicContainerType;
  return NS_OK;
}


// nsNavHistoryContainerResultNode::AppendURINode

NS_IMETHODIMP
nsNavHistoryContainerResultNode::AppendURINode(
    const nsACString& aURI, const nsACString& aTitle, PRUint32 aAccessCount,
    PRTime aTime, const nsACString& aIconURI, nsINavHistoryResultNode** _retval)
{
  *_retval = nsnull;
  if (!IsDynamicContainer())
    return NS_ERROR_INVALID_ARG; // we must be a dynamic container

  // If we are child of an ExcludeItems parent or root, we should not append
  // URI Nodes.
  if ((mResult && mResult->mRootNode->mOptions->ExcludeItems()) ||
      (mParent && mParent->mOptions->ExcludeItems()))
    return NS_OK;

  nsRefPtr<nsNavHistoryResultNode> result =
      new nsNavHistoryResultNode(aURI, aTitle, aAccessCount, aTime, aIconURI);
  NS_ENSURE_TRUE(result, NS_ERROR_OUT_OF_MEMORY);

  // append to our list
  nsresult rv = InsertChildAt(result, mChildren.Count());
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ADDREF(*_retval = result);
  return NS_OK;
}


// nsNavHistoryContainerResultNode::AppendVisitNode

#if 0 // UNTESTED, commented out until it can be tested
NS_IMETHODIMP
nsNavHistoryContainerResultNode::AppendVisitNode(
    const nsACString& aURI, const nsACString& aTitle, PRUint32 aAccessCount,
    PRTime aTime, const nsACString& aIconURI, PRInt64 aSession,
    nsINavHistoryVisitResultNode** _retval)
{
  *_retval = nsnull;
  if (!IsDynamicContainer())
    return NS_ERROR_INVALID_ARG; // we must be a dynamic container

  nsRefPtr<nsNavHistoryVisitResultNode> result =
      new nsNavHistoryVisitResultNode(aURI, aTitle, aAccessCount, aTime,
                                      aIconURI, aSession);
  NS_ENSURE_TRUE(result, NS_ERROR_OUT_OF_MEMORY);

  // append to our list
  if (! mChildren.AppendObject(result))
    return NS_ERROR_OUT_OF_MEMORY;
  NS_ADDREF(*_retval = result);
  return NS_OK;
}


// nsNavHistoryContainerResultNode::AppendFullVisitNode

NS_IMETHODIMP
nsNavHistoryContainerResultNode::AppendFullVisitNode(
    const nsACString& aURI, const nsACString& aTitle, PRUint32 aAccessCount,
    PRTime aTime, const nsACString& aIconURI, PRInt64 aSession,
    PRInt64 aVisitId, PRInt64 aReferringVisitId, PRInt32 aTransitionType,
    nsINavHistoryFullVisitResultNode** _retval)
{
  *_retval = nsnull;
  if (!IsDynamicContainer())
    return NS_ERROR_INVALID_ARG; // we must be a dynamic container

  nsRefPtr<nsNavHistoryFullVisitResultNode> result =
      new nsNavHistoryFullVisitResultNode(aURI, aTitle, aAccessCount, aTime,
                                          aIconURI, aSession, aVisitId,
                                          aReferringVisitId, aTransitionType);
  NS_ENSURE_TRUE(result, NS_ERROR_OUT_OF_MEMORY);

  // append to our list
  if (! mChildren.AppendObject(result))
    return NS_ERROR_OUT_OF_MEMORY;
  NS_ADDREF(*_retval = result);
  return NS_OK;
}


// nsNavHistoryContainerResultNode::AppendContainerNode

NS_IMETHODIMP
nsNavHistoryContainerResultNode::AppendContainerNode(
    const nsACString& aTitle, const nsACString& aIconURI,
    PRUint32 aContainerType, const nsACString& aDynamicContainerType,
    nsINavHistoryContainerResultNode** _retval)
{
  *_retval = nsnull;
  if (!IsDynamicContainer())
    return NS_ERROR_INVALID_ARG; // we must be a dynamic container
  if (! IsTypeContainer(aContainerType) || IsTypeFolder(aContainerType) ||
      IsTypeQuery(aContainerType))
    return NS_ERROR_INVALID_ARG; // not proper container type
  if (aContainerType == nsNavHistoryResultNode::RESULT_TYPE_DYNAMIC_CONTAINER &&
      aRemoteContainerType.IsEmpty())
    return NS_ERROR_INVALID_ARG; // dynamic containers must have d.c. type
  if (aContainerType != nsNavHistoryResultNode::RESULT_TYPE_DYNAMIC_CONTAINER &&
      ! aDynamicContainerType.IsEmpty())
    return NS_ERROR_INVALID_ARG; // non-dynamic containers must NOT have d.c. type

  nsRefPtr<nsNavHistoryContainerResultNode> result =
      new nsNavHistoryContainerResultNode(EmptyCString(), aTitle, aIconURI,
                                          aContainerType, PR_TRUE,
                                          aDynamicContainerType);
  NS_ENSURE_TRUE(result, NS_ERROR_OUT_OF_MEMORY);

  // append to our list
  if (! mChildren.AppendObject(result))
    return NS_ERROR_OUT_OF_MEMORY;
  NS_ADDREF(*_retval = result);
  return NS_OK;
}


// nsNavHistoryContainerResultNode::AppendQueryNode

NS_IMETHODIMP
nsNavHistoryContainerResultNode::AppendQueryNode(
    const nsACString& aQueryURI, const nsACString& aTitle,
    const nsACString& aIconURI, nsINavHistoryQueryResultNode** _retval)
{
  *_retval = nsnull;
  if (!IsDynamicContainer())
    return NS_ERROR_INVALID_ARG; // we must be a dynamic container

  nsRefPtr<nsNavHistoryQueryResultNode> result =
      new nsNavHistoryQueryResultNode(aQueryURI, aTitle, aIconURI);
  NS_ENSURE_TRUE(result, NS_ERROR_OUT_OF_MEMORY);

  // append to our list
  if (! mChildren.AppendObject(result))
    return NS_ERROR_OUT_OF_MEMORY;
  NS_ADDREF(*_retval = result);
  return NS_OK;
}
#endif

// nsNavHistoryContainerResultNode::AppendFolderNode

NS_IMETHODIMP
nsNavHistoryContainerResultNode::AppendFolderNode(
    PRInt64 aFolderId, nsINavHistoryContainerResultNode** _retval)
{
  *_retval = nsnull;
  if (!IsDynamicContainer())
    return NS_ERROR_INVALID_ARG; // we must be a dynamic container

  nsNavBookmarks* bookmarks = nsNavBookmarks::GetBookmarksService();
  NS_ENSURE_TRUE(bookmarks, NS_ERROR_OUT_OF_MEMORY);

  // create the node, it will be addrefed for us
  nsRefPtr<nsNavHistoryResultNode> result;
  nsresult rv = bookmarks->ResultNodeForContainer(aFolderId,
                                                  GetGeneratingOptions(),
                                                  getter_AddRefs(result));
  NS_ENSURE_SUCCESS(rv, rv);

  // append to our list
  rv = InsertChildAt(result, mChildren.Count());
  NS_ENSURE_SUCCESS(rv, rv);

  NS_ADDREF(*_retval = result->GetAsContainer());
  return NS_OK;
}


// nsNavHistoryContainerResultNode::ClearContents
//
//    Used by the dynamic container API to clear this container

#if 0 // UNTESTED, commented out until it can be tested
NS_IMETHODIMP
nsNavHistoryContainerResultNode::ClearContents()
{
  if (!IsDynamicContainer())
    return NS_ERROR_INVALID_ARG; // we must be a dynamic container

  // we know if CanRemoteContainersChange() then we are a regular container
  // and not a query or folder, so clearing doesn't need anything else to
  // happen (like unregistering observers). Also, since this should only
  // happen when the container is closed, we don't need to redraw the screen.
  mChildren.Clear();

  PRUint32 oldAccessCount = mAccessCount;
  mAccessCount = 0;
  mTime = 0;
  ReverseUpdateStats(-PRInt32(oldAccessCount));
  return NS_OK;
}
#endif

// nsNavHistoryQueryResultNode *************************************************
//
//    HOW QUERY UPDATING WORKS
//
//    Queries are different than bookmark folders in that we can not always
//    do dynamic updates (easily) and updates are more expensive. Therefore,
//    we do NOT query if we are not open and want to see if we have any children
//    (for drawing a twisty) and always assume we will.
//
//    When the container is opened, we execute the query and register the
//    listeners. Like bookmark folders, we stay registered even when closed,
//    and clear ourselves as soon as a message comes in. This lets us respond
//    quickly if the user closes and reopens the container.
//
//    We try to handle the most common notifications for the most common query
//    types dynamically, that is, figuring out what should happen in response
//    to a message without doing a requery. For complex changes or complex
//    queries, we give up and requery.

NS_IMPL_ISUPPORTS_INHERITED1(nsNavHistoryQueryResultNode,
                             nsNavHistoryContainerResultNode,
                             nsINavHistoryQueryResultNode)

nsNavHistoryQueryResultNode::nsNavHistoryQueryResultNode(
    const nsACString& aTitle, const nsACString& aIconURI,
    const nsACString& aQueryURI) :
  nsNavHistoryContainerResultNode(aQueryURI, aTitle, aIconURI,
                                  nsNavHistoryResultNode::RESULT_TYPE_QUERY,
                                  PR_TRUE, EmptyCString(), nsnull),
  mHasSearchTerms(PR_FALSE),
  mContentsValid(PR_FALSE),
  mBatchInProgress(PR_FALSE)
{
}

nsNavHistoryQueryResultNode::nsNavHistoryQueryResultNode(
    const nsACString& aTitle, const nsACString& aIconURI,
    const nsCOMArray<nsNavHistoryQuery>& aQueries,
    nsNavHistoryQueryOptions* aOptions) :
  nsNavHistoryContainerResultNode(EmptyCString(), aTitle, aIconURI,
                                  nsNavHistoryResultNode::RESULT_TYPE_QUERY,
                                  PR_TRUE, EmptyCString(), aOptions),
  mQueries(aQueries),
  mContentsValid(PR_FALSE),
  mBatchInProgress(PR_FALSE)
{
  NS_ASSERTION(aQueries.Count() > 0, "Must have at least one query");

  nsNavHistory* history = nsNavHistory::GetHistoryService();
  NS_ASSERTION(history, "History service missing");
  mLiveUpdate = history->GetUpdateRequirements(mQueries, mOptions,
                                               &mHasSearchTerms);
}

nsNavHistoryQueryResultNode::nsNavHistoryQueryResultNode(
    const nsACString& aTitle, const nsACString& aIconURI,
    PRTime aTime,
    const nsCOMArray<nsNavHistoryQuery>& aQueries,
    nsNavHistoryQueryOptions* aOptions) :
  nsNavHistoryContainerResultNode(EmptyCString(), aTitle, aTime, aIconURI,
                                  nsNavHistoryResultNode::RESULT_TYPE_QUERY,
                                  PR_TRUE, EmptyCString(), aOptions),
  mQueries(aQueries),
  mContentsValid(PR_FALSE),
  mBatchInProgress(PR_FALSE)
{
  NS_ASSERTION(aQueries.Count() > 0, "Must have at least one query");

  nsNavHistory* history = nsNavHistory::GetHistoryService();
  NS_ASSERTION(history, "History service missing");
  mLiveUpdate = history->GetUpdateRequirements(mQueries, mOptions,
                                               &mHasSearchTerms);
}

nsNavHistoryQueryResultNode::~nsNavHistoryQueryResultNode() {
  // Remove this node from result's observers.  We don't need to be notified
  // anymore.
  if (mResult && mResult->mAllBookmarksObservers.IndexOf(this) !=
                   mResult->mAllBookmarksObservers.NoIndex)
    mResult->RemoveAllBookmarksObserver(this);
  if (mResult && mResult->mHistoryObservers.IndexOf(this) !=
                   mResult->mHistoryObservers.NoIndex)
    mResult->RemoveHistoryObserver(this);
}

// nsNavHistoryQueryResultNode::CanExpand
//
//    Whoever made us may want non-expanding queries. However, we always
//    expand when we are the root node, or else asking for non-expanding
//    queries would be useless. A query node is not expandable if excludeItems=1
//    or expandQueries=0.

PRBool
nsNavHistoryQueryResultNode::CanExpand()
{
  if (IsContainersQuery())
    return PR_TRUE;

  // If we are child of an ExcludeItems parent or root, we should not expand.
  if ((mResult && mResult->mRootNode->mOptions->ExcludeItems()) ||
      (mParent && mParent->mOptions->ExcludeItems()))
    return PR_FALSE;

  nsNavHistoryQueryOptions* options = GetGeneratingOptions();
  if (options) {
    if (options->ExcludeItems())
      return PR_FALSE;
    if (options->ExpandQueries())
      return PR_TRUE;
  }
  if (mResult && mResult->mRootNode == this)
    return PR_TRUE;
  return PR_FALSE;
}

// nsNavHistoryQueryResultNode::IsContainersQuery
//
// Some query with a particular result type can contain other queries,
// they must be always expandable

PRBool
nsNavHistoryQueryResultNode::IsContainersQuery()
{
  PRUint16 resultType = Options()->ResultType();
  return resultType == nsINavHistoryQueryOptions::RESULTS_AS_DATE_QUERY ||
         resultType == nsINavHistoryQueryOptions::RESULTS_AS_DATE_SITE_QUERY ||
         resultType == nsINavHistoryQueryOptions::RESULTS_AS_TAG_QUERY ||
         resultType == nsINavHistoryQueryOptions::RESULTS_AS_SITE_QUERY;
}

// nsNavHistoryQueryResultNode::OnRemoving
//
//    Here we do not want to call ContainerResultNode::OnRemoving since our own
//    ClearChildren will do the same thing and more (unregister the observers).
//    The base ResultNode::OnRemoving will clear some regular node stats, so it
//    is OK.

void
nsNavHistoryQueryResultNode::OnRemoving()
{
  nsNavHistoryResultNode::OnRemoving();
  ClearChildren(PR_TRUE);
}


// nsNavHistoryQueryResultNode::OpenContainer
//
//    Marks the container as open, rebuilding results if they are invalid. We
//    may still have valid results if the container was previously open and
//    nothing happened since closing it.
//
//    We do not handle CloseContainer specially. The default one just marks the
//    container as closed, but doesn't actually mark the results as invalid.
//    The results will be invalidated by the next history or bookmark
//    notification that comes in. This means if you open and close the item
//    without anything happening in between, it will be fast (this actually
//    happens when results are used as menus).

nsresult
nsNavHistoryQueryResultNode::OpenContainer()
{
  NS_ASSERTION(!mExpanded, "Container must be closed to open it");
  mExpanded = PR_TRUE;
  if (!CanExpand())
    return NS_OK;
  if (!mContentsValid) {
    nsresult rv = FillChildren();
    NS_ENSURE_SUCCESS(rv, rv);
  }

  nsNavHistoryResult* result = GetResult();
  NS_ENSURE_TRUE(result, NS_ERROR_FAILURE);
  if (result->GetView())
    result->GetView()->ContainerOpened(
        static_cast<nsNavHistoryContainerResultNode*>(this));
  return NS_OK;
}


// nsNavHistoryQueryResultNode::GetHasChildren
//
//    When we have valid results we can always give an exact answer. When we
//    don't we just assume we'll have results, since actually doing the query
//    might be hard. This is used to draw twisties on the tree, so precise
//    results don't matter.

NS_IMETHODIMP
nsNavHistoryQueryResultNode::GetHasChildren(PRBool* aHasChildren)
{
  if (!CanExpand()) {
    *aHasChildren = PR_FALSE;
    return NS_OK;
  }

  PRUint16 resultType = mOptions->ResultType();
  // For tag containers query we must check if we have any tag
  if (resultType == nsINavHistoryQueryOptions::RESULTS_AS_TAG_QUERY) {
    nsNavHistory* history = nsNavHistory::GetHistoryService();
    NS_ENSURE_TRUE(history, NS_ERROR_OUT_OF_MEMORY);
    mozIStorageConnection *dbConn = history->GetStorageConnection();

    nsNavBookmarks* bookmarks = nsNavBookmarks::GetBookmarksService();
    NS_ENSURE_TRUE(bookmarks, NS_ERROR_OUT_OF_MEMORY);
    PRInt64 tagsFolderId;
    nsresult rv = bookmarks->GetTagsFolder(&tagsFolderId);
    NS_ENSURE_SUCCESS(rv, rv);

    nsCOMPtr<mozIStorageStatement> hasTagsStatement;
    rv = dbConn->CreateStatement(NS_LITERAL_CSTRING(
        "SELECT id FROM moz_bookmarks WHERE parent = ?1 LIMIT 1"),
      getter_AddRefs(hasTagsStatement));
    NS_ENSURE_SUCCESS(rv, rv);
    rv = hasTagsStatement->BindInt64Parameter(0, tagsFolderId);
    NS_ENSURE_SUCCESS(rv, rv);

    rv = hasTagsStatement->ExecuteStep(aHasChildren);
    NS_ENSURE_SUCCESS(rv, rv);

    return NS_OK;
  }

  // For history containers query we must check if we have any history
  if (resultType == nsINavHistoryQueryOptions::RESULTS_AS_DATE_QUERY ||
      resultType == nsINavHistoryQueryOptions::RESULTS_AS_DATE_SITE_QUERY ||
      resultType == nsINavHistoryQueryOptions::RESULTS_AS_SITE_QUERY) {
    nsNavHistory* history = nsNavHistory::GetHistoryService();
    NS_ENSURE_TRUE(history, NS_ERROR_OUT_OF_MEMORY);
    return history->GetHasHistoryEntries(aHasChildren);
  }

  //XXX: For other containers queries we must:
  // 1. If it's open, just check mChildren for containers
  // 2. Else null the view (keep it in a var), open container, check mChildren
  //    for containers, close container, reset the view

  if (mContentsValid) {
    *aHasChildren = (mChildren.Count() > 0);
    return NS_OK;
  }
  *aHasChildren = PR_TRUE;
  return NS_OK;
}


// nsNavHistoryQueryResultNode::GetUri
//
//    This doesn't just return mURI because in the case of queries that may
//    be lazily constructed from the query objects.

NS_IMETHODIMP
nsNavHistoryQueryResultNode::GetUri(nsACString& aURI)
{
  nsresult rv = VerifyQueriesSerialized();
  NS_ENSURE_SUCCESS(rv, rv);
  aURI = mURI;
  return NS_OK;
}

// nsNavHistoryQueryResultNode::GetFolderItemId

NS_IMETHODIMP
nsNavHistoryQueryResultNode::GetFolderItemId(PRInt64* aItemId)
{
  *aItemId = mItemId;
  return NS_OK;
}

// nsNavHistoryQueryResultNode::GetQueries

NS_IMETHODIMP
nsNavHistoryQueryResultNode::GetQueries(PRUint32* queryCount,
                                        nsINavHistoryQuery*** queries)
{
  nsresult rv = VerifyQueriesParsed();
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ASSERTION(mQueries.Count() > 0, "Must have >= 1 query");

  *queries = static_cast<nsINavHistoryQuery**>
                        (nsMemory::Alloc(mQueries.Count() * sizeof(nsINavHistoryQuery*)));
  NS_ENSURE_TRUE(*queries, NS_ERROR_OUT_OF_MEMORY);

  for (PRInt32 i = 0; i < mQueries.Count(); ++i)
    NS_ADDREF((*queries)[i] = mQueries[i]);
  *queryCount = mQueries.Count();
  return NS_OK;
}


// nsNavHistoryQueryResultNode::GetQueryOptions

NS_IMETHODIMP
nsNavHistoryQueryResultNode::GetQueryOptions(
                                      nsINavHistoryQueryOptions** aQueryOptions)
{
  *aQueryOptions = Options();
  NS_ADDREF(*aQueryOptions);
  return NS_OK;
}

// nsNavHistoryQueryResultNode::Options
//
//  Safe options getter, ensures queries are parsed first.

nsNavHistoryQueryOptions*
nsNavHistoryQueryResultNode::Options()
{
  nsresult rv = VerifyQueriesParsed();
  if (NS_FAILED(rv))
    return nsnull;
  NS_ASSERTION(mOptions, "Options invalid, cannot generate from URI");
  return mOptions;
}

// nsNavHistoryQueryResultNode::VerifyQueriesParsed

nsresult
nsNavHistoryQueryResultNode::VerifyQueriesParsed()
{
  if (mQueries.Count() > 0) {
    NS_ASSERTION(mOptions, "If a result has queries, it also needs options");
    return NS_OK;
  }
  NS_ASSERTION(! mURI.IsEmpty(),
               "Query nodes must have either a URI or query/options");

  nsNavHistory* history = nsNavHistory::GetHistoryService();
  NS_ENSURE_TRUE(history, NS_ERROR_OUT_OF_MEMORY);

  nsresult rv = history->QueryStringToQueryArray(mURI, &mQueries,
                                                 getter_AddRefs(mOptions));
  NS_ENSURE_SUCCESS(rv, rv);

  mLiveUpdate = history->GetUpdateRequirements(mQueries, mOptions,
                                               &mHasSearchTerms);
  return NS_OK;
}


// nsNavHistoryQueryResultNode::VerifyQueriesSerialized

nsresult
nsNavHistoryQueryResultNode::VerifyQueriesSerialized()
{
  if (! mURI.IsEmpty()) {
    return NS_OK;
  }
  NS_ASSERTION(mQueries.Count() > 0 && mOptions,
               "Query nodes must have either a URI or query/options");

  nsTArray<nsINavHistoryQuery*> flatQueries;
  flatQueries.SetCapacity(mQueries.Count());
  for (PRInt32 i = 0; i < mQueries.Count(); i ++)
    flatQueries.AppendElement(static_cast<nsINavHistoryQuery*>
                                         (mQueries.ObjectAt(i)));

  nsNavHistory* history = nsNavHistory::GetHistoryService();
  NS_ENSURE_TRUE(history, NS_ERROR_OUT_OF_MEMORY);

  nsresult rv = history->QueriesToQueryString(flatQueries.Elements(),
                                              flatQueries.Length(),
                                              mOptions, mURI);
  NS_ENSURE_SUCCESS(rv, rv);
  NS_ENSURE_TRUE(! mURI.IsEmpty(), NS_ERROR_FAILURE);
  return NS_OK;
}


// nsNavHistoryQueryResultNode::FillChildren

nsresult
nsNavHistoryQueryResultNode::FillChildren()
{
  NS_ASSERTION(! mContentsValid,
               "Don't call FillChildren when contents are valid");
  NS_ASSERTION(mChildren.Count() == 0,
               "We are trying to fill children when there already are some");

  nsNavHistory* history = nsNavHistory::GetHistoryService();
  NS_ENSURE_TRUE(history, NS_ERROR_OUT_OF_MEMORY);

  // get the results from the history service
  nsresult rv = VerifyQueriesParsed();
  NS_ENSURE_SUCCESS(rv, rv);
  rv = history->GetQueryResults(this, mQueries, mOptions, &mChildren);
  NS_ENSURE_SUCCESS(rv, rv);

  // it is important to call FillStats to fill in the parents on all
  // nodes and the result node pointers on the containers
  FillStats();

  PRUint16 sortType = GetSortType();

  if (mResult->mNeedsToApplySortingMode) {
    // We should repopulate container and then apply sortingMode.  To avoid
    // sorting 2 times we simply do that here.
    mResult->SetSortingMode(mResult->mSortingMode);
  }
  else if (mOptions->QueryType() != nsINavHistoryQueryOptions::QUERY_TYPE_HISTORY ||
           sortType != nsINavHistoryQueryOptions::SORT_BY_NONE) {
    // The default SORT_BY_NONE sorts by the bookmark index (position), 
    // which we do not have for history queries.
    // Once we've computed all tree stats, we can sort, because containers will
    // then have proper visit counts and dates.
    SortComparator comparator = GetSortingComparator(GetSortType());
    if (comparator) {
      nsCAutoString sortingAnnotation;
      GetSortingAnnotation(sortingAnnotation);
      // Usually containers queries results comes already sorted from the
      // database, but some locales could have special rules to sort by title.
      // RecursiveSort won't apply these rules to containers in containers
      // queries because when setting sortingMode on the result we want to sort
      // contained items (bug 473157).
      // Base container RecursiveSort will sort both our children and all
      // descendants, and is used in this case because we have to do manual
      // title sorting.
      // Query RecursiveSort will instead only sort descendants if we are a
      // constinaersQuery, e.g. a grouped query that will return other queries.
      // For other type of queries it will act as the base one.
      if (IsContainersQuery() &&
          sortType == mOptions->SortingMode() &&
          (sortType == nsINavHistoryQueryOptions::SORT_BY_TITLE_ASCENDING ||
           sortType == nsINavHistoryQueryOptions::SORT_BY_TITLE_DESCENDING))
        nsNavHistoryContainerResultNode::RecursiveSort(sortingAnnotation.get(), comparator);
      else
        RecursiveSort(sortingAnnotation.get(), comparator);
    }
  }

  // if we are limiting our results remove items from the end of the
  // mChildren array after sorting. This is done for root node only.
  // note, if count < max results, we won't do anything.
  if (!mParent && mOptions->MaxResults()) {
    while ((PRUint32)mChildren.Count() > mOptions->MaxResults())
      mChildren.RemoveObjectAt(mChildren.Count() - 1);
  }

  nsNavHistoryResult* result = GetResult();
  NS_ENSURE_TRUE(result, NS_ERROR_FAILURE);

  if (mOptions->QueryType() == nsINavHistoryQueryOptions::QUERY_TYPE_HISTORY ||
      mOptions->QueryType() == nsINavHistoryQueryOptions::QUERY_TYPE_UNIFIED) {
    // Date containers that contain site containers have no reason to observe
    // history, if the inside site container is expanded it will update,
    // otherwise we are going to refresh the parent query.
    if (!mParent || mParent->mOptions->ResultType() != nsINavHistoryQueryOptions::RESULTS_AS_DATE_SITE_QUERY) {
      // register with the result for history updates
      result->AddHistoryObserver(this);
    }
  }

  if (mOptions->QueryType() == nsINavHistoryQueryOptions::QUERY_TYPE_BOOKMARKS ||
      mOptions->QueryType() == nsINavHistoryQueryOptions::QUERY_TYPE_UNIFIED ||
      mLiveUpdate == QUERYUPDATE_COMPLEX_WITH_BOOKMARKS) {
    // register with the result for bookmark updates
    result->AddAllBookmarksObserver(this);
  }

  mContentsValid = PR_TRUE;
  return NS_OK;
}


// nsNavHistoryQueryResultNode::ClearChildren
//
//    Call with unregister = false when we are going to update the children (for
//    example, when the container is open). This will clear the list and notify
//    all the children that they are going away.
//
//    When the results are becoming invalid and we are not going to refresh
//    them, set unregister = true, which will unregister the listener from the
//    result if any. We use unregister = false when we are refreshing the list
//    immediately so want to stay a notifier.

void
nsNavHistoryQueryResultNode::ClearChildren(PRBool aUnregister)
{
  for (PRInt32 i = 0; i < mChildren.Count(); i ++)
    mChildren[i]->OnRemoving();
  mChildren.Clear();

  if (aUnregister && mContentsValid) {
    nsNavHistoryResult* result = GetResult();
    if (result) {
      result->RemoveHistoryObserver(this);
      result->RemoveAllBookmarksObserver(this);
    }
  }
  mContentsValid = PR_FALSE;
}


// nsNavHistoryQueryResultNode::Refresh
//
//    This is called to update the result when something has changed that we
//    can not incrementally update.

nsresult
nsNavHistoryQueryResultNode::Refresh()
{
  // Ignore refreshes when there is a batch, EndUpdateBatch will do a refresh
  // to get all the changes.
  if (mBatchInProgress)
    return NS_OK;

  // This is not a root node but it does not have a parent - this means that 
  // the node has already been cleared and it is now called, because it was 
  // left in a local copy of the observers array.
  if (mIndentLevel > -1 && !mParent)
    return NS_OK;

  // Do not refresh if we are not expanded or if we are child of a query
  // containing other queries. In this case calling Refresh for each child
  // query could cause a major slowdown. We should not refresh nested
  // queries, since we will already refresh the parent one.
  if (!mExpanded ||
      (mParent && mParent->IsQuery() &&
       mParent->GetAsQuery()->IsContainersQuery())) {
    // Don't update, just invalidate and unhook
    ClearChildren(PR_TRUE);
    return NS_OK; // no updates in tree state
  }

  if (mLiveUpdate == QUERYUPDATE_COMPLEX_WITH_BOOKMARKS)
    ClearChildren(PR_TRUE);
  else
    ClearChildren(PR_FALSE);

  // ignore errors from FillChildren, since we will still want to refresh
  // the tree (there just might not be anything in it on error)
  (void)FillChildren();

  nsNavHistoryResult* result = GetResult();
  NS_ENSURE_TRUE(result, NS_ERROR_FAILURE);
  if (result->GetView())
    return result->GetView()->InvalidateContainer(
        static_cast<nsNavHistoryContainerResultNode*>(this));
  return NS_OK;
}


// nsNavHistoryQueryResultNode::GetSortType
//
//    Here, we override GetSortType to return the current sorting for this
//    query. GetSortType is used when dynamically inserting query results so we
//    can see which comparator we should use to find the proper insertion point
//    (it shouldn't be called from folder containers which maintain their own
//    sorting).
//
//    Normally, the container just forwards it up the chain. This is what
//    we want for host groups, for example. For queries, we often want to
//    use the query's sorting mode.
//
//    However, we only use this query node's sorting when it is not the root.
//    When it is the root, we use the result's sorting mode, which is set
//    according to the column headers in the tree view (if attached). This is
//    because there are two cases:
//
//    - You are looking at a bookmark hierarchy that contains an embedded
//      result. We should always use the query's sort ordering since the result
//      node's headers have nothing to do with us (and are disabled).
//
//    - You are looking at a query in the tree. In this case, we want the
//      result sorting to override ours (it should be initialized to the same
//      sorting mode).
//
//    It might seem like the column headers should set the sorting mode for the
//    query in the result so we don't have to have this special case. But, the
//    UI allows you to save the query in a different place or edit it, and just
//    grabs the parameters and options from the query node. It would be weird
//    to build a query, click a column header, and have the options you built
//    up in the query builder be changed from under you.

PRUint16
nsNavHistoryQueryResultNode::GetSortType()
{
  if (mParent) {
    // use our sorting, we are not the root
    return mOptions->SortingMode();
  }
  if (mResult) {
    return mResult->mSortingMode;
  }

  NS_NOTREACHED("We should always have a result");
  return nsINavHistoryQueryOptions::SORT_BY_NONE;
}

void
nsNavHistoryQueryResultNode::GetSortingAnnotation(nsACString& aAnnotation) {
  if (mParent) {
    // use our sorting, we are not the root
    mOptions->GetSortingAnnotation(aAnnotation);
  }
  else if (mResult) {
    aAnnotation.Assign(mResult->mSortingAnnotation);
  }
  else
    NS_NOTREACHED("We should always have a result");
}

void
nsNavHistoryQueryResultNode::RecursiveSort(
    const char* aData, SortComparator aComparator)
{
  void* data = const_cast<void*>(static_cast<const void*>(aData));

  if (!IsContainersQuery())
    mChildren.Sort(aComparator, data);

  for (PRInt32 i = 0; i < mChildren.Count(); i++) {
    if (mChildren[i]->IsContainer())
      mChildren[i]->GetAsContainer()->RecursiveSort(aData, aComparator);
  }
}


// nsNavHistoryResultNode::OnBeginUpdateBatch

NS_IMETHODIMP
nsNavHistoryQueryResultNode::OnBeginUpdateBatch()
{
  mBatchInProgress = PR_TRUE;
  return NS_OK;
}


// nsNavHistoryResultNode::OnEndUpdateBatch
//
//    Always requery when batches are done. These will typically involve large
//    operations (currently delete) and it is likely more efficient to requery
//    than to incrementally update as each message comes in.

NS_IMETHODIMP
nsNavHistoryQueryResultNode::OnEndUpdateBatch()
{
  NS_ASSERTION(mBatchInProgress, "EndUpdateBatch without a begin");
  // must set to false before calling Refresh or Refresh will ignore us
  mBatchInProgress = PR_FALSE;
  return Refresh();
}


// nsNavHistoryQueryResultNode::OnVisit
//
//    Here we need to update all copies of the URI we have with the new visit
//    count, and potentially add a new entry in our query. This is the most
//    common update operation and it is important that it be as efficient as
//    possible.

NS_IMETHODIMP
nsNavHistoryQueryResultNode::OnVisit(nsIURI* aURI, PRInt64 aVisitId,
                                     PRTime aTime, PRInt64 aSessionId,
                                     PRInt64 aReferringId,
                                     PRUint32 aTransitionType,
                                     PRUint32* aAdded)
{
  // ignore everything during batches
  if (mBatchInProgress)
    return NS_OK;

  nsNavHistory* history = nsNavHistory::GetHistoryService();
  NS_ENSURE_TRUE(history, NS_ERROR_OUT_OF_MEMORY);

  nsresult rv;
  nsRefPtr<nsNavHistoryResultNode> addition;
  switch(mLiveUpdate) {

    case QUERYUPDATE_HOST: {
      // For these simple yet common cases we can check the host ourselves
      // before doing the overhead of creating a new result node.
      NS_ASSERTION(mQueries.Count() == 1, 
          "Host updated queries can have only one object");
      nsCOMPtr<nsNavHistoryQuery> queryHost = 
          do_QueryInterface(mQueries[0], &rv);
      NS_ENSURE_SUCCESS(rv, rv);

      PRBool hasDomain;
      queryHost->GetHasDomain(&hasDomain);
      if (!hasDomain)
        return NS_OK;

      nsCAutoString host;
      if (NS_FAILED(aURI->GetAsciiHost(host)))
        return NS_OK;

      if (!queryHost->Domain().Equals(host))
        return NS_OK;

    } // Let it fall through - we want to check the time too,
      // if the time is not present it will match too.
    case QUERYUPDATE_TIME: {
      // For these simple yet common cases we can check the time ourselves
      // before doing the overhead of creating a new result node.
      NS_ASSERTION(mQueries.Count() == 1, 
          "Time updated queries can have only one object");
      nsCOMPtr<nsNavHistoryQuery> query = 
          do_QueryInterface(mQueries[0], &rv);
      NS_ENSURE_SUCCESS(rv, rv);

      PRBool hasIt;
      query->GetHasBeginTime(&hasIt);
      if (hasIt) {
        PRTime beginTime = history->NormalizeTime(query->BeginTimeReference(),
                                                  query->BeginTime());
        if (aTime < beginTime)
          return NS_OK; // before our time range
      }
      query->GetHasEndTime(&hasIt);
      if (hasIt) {
        PRTime endTime = history->NormalizeTime(query->EndTimeReference(),
                                                query->EndTime());
        if (aTime > endTime)
          return NS_OK; // after our time range
      }
      // now we know that our visit satisfies the time range, create a new node
      rv = history->VisitIdToResultNode(aVisitId, mOptions,
                                        getter_AddRefs(addition));

      // We do not want to add this result to this node
      if (NS_FAILED(rv) || !addition)
          return NS_OK;

      break;
    }
    case QUERYUPDATE_SIMPLE: {
      // The history service can tell us whether the new item should appear
      // in the result. We first have to construct a node for it to check.
      rv = history->VisitIdToResultNode(aVisitId, mOptions,
                                        getter_AddRefs(addition));
      if (NS_FAILED(rv) || !addition ||
          !history->EvaluateQueryForNode(mQueries, mOptions, addition))
        return NS_OK; // don't need to include in our query
      break;
    }
    case QUERYUPDATE_COMPLEX:
    case QUERYUPDATE_COMPLEX_WITH_BOOKMARKS:
      // need to requery in complex cases
      return Refresh();
    default:
      NS_NOTREACHED("Invalid value for mLiveUpdate");
      return Refresh();
  }

  // NOTE: The dynamic updating never deletes any nodes. Sometimes it replaces
  // URI nodes or adds visits, but never deletes old ones.
  //
  // The only time this might happen in the current implementation is if the
  // title changes and it no longer matches a keyword search. This is not
  // important enough to handle given that we don't do any other deleting. It
  // is arguably more useful behavior anyway, since you're searching your
  // history and the page used to match.
  //
  // When more queries are possible (show pages I've visited less than 5 times)
  // this will be important to add.

  nsCOMArray<nsNavHistoryResultNode> mergerNode;

  if (!mergerNode.AppendObject(addition))
    return NS_ERROR_OUT_OF_MEMORY;

  MergeResults(&mergerNode);

  if (aAdded)
    (*aAdded)++;

  return NS_OK;
}


// nsNavHistoryQueryResultNode::OnTitleChanged
//
//    Find every node that matches this URI and rename it. We try to do
//    incremental updates here, even when we are closed, because changing titles
//    is easier than requerying if we are invalid.
//
//    This actually gets called a lot. Typically, we will get an AddURI message
//    when the user visits the page, and then the title will be set asynchronously
//    when the title element of the page is parsed.

NS_IMETHODIMP
nsNavHistoryQueryResultNode::OnTitleChanged(nsIURI* aURI,
                                            const nsAString& aPageTitle)
{
  if (mBatchInProgress)
    return NS_OK; // ignore everything during batches
  if (! mExpanded) {
    // When we are not expanded, we don't update, just invalidate and unhook.
    // It would still be pretty easy to traverse the results and update the
    // titles, but when a title changes, its unlikely that it will be the only
    // thing. Therefore, we just give up.
    ClearChildren(PR_TRUE);
    return NS_OK; // no updates in tree state
  }

  // See if our queries have any keyword matching. In this case, we can't just
  // replace the title on the items, but must redo the query. This could be
  // handled more efficiently, but it is hard because keyword matching might
  // match anything, including the title and other things.
  if (mHasSearchTerms) {
    return Refresh();
  }

  // compute what the new title should be
  NS_ConvertUTF16toUTF8 newTitle(aPageTitle);

  PRBool onlyOneEntry = (mOptions->ResultType() ==
                         nsINavHistoryQueryOptions::RESULTS_AS_URI ||
                         mOptions->ResultType() ==
                         nsINavHistoryQueryOptions::RESULTS_AS_TAG_CONTENTS
                         );
  return ChangeTitles(aURI, newTitle, PR_TRUE, onlyOneEntry);
}


NS_IMETHODIMP
nsNavHistoryQueryResultNode::OnBeforeDeleteURI(nsIURI *aURI)
{
  return NS_OK;
}

// nsNavHistoryQueryResultNode::OnDeleteURI
//
//    Here, we can always live update by just deleting all occurrences of
//    the given URI.

NS_IMETHODIMP
nsNavHistoryQueryResultNode::OnDeleteURI(nsIURI *aURI)
{
  PRBool onlyOneEntry = (mOptions->ResultType() ==
                         nsINavHistoryQueryOptions::RESULTS_AS_URI ||
                         mOptions->ResultType() ==
                         nsINavHistoryQueryOptions::RESULTS_AS_TAG_CONTENTS);
  nsCAutoString spec;
  nsresult rv = aURI->GetSpec(spec);
  NS_ENSURE_SUCCESS(rv, rv);

  nsCOMArray<nsNavHistoryResultNode> matches;
  RecursiveFindURIs(onlyOneEntry, this, spec, &matches);
  if (matches.Count() == 0)
    return NS_OK; // not found

  for (PRInt32 i = 0; i < matches.Count(); i ++) {
    nsNavHistoryResultNode* node = matches[i];
    nsNavHistoryContainerResultNode* parent = node->mParent;
    // URI nodes should always have parents
    NS_ENSURE_TRUE(parent, NS_ERROR_UNEXPECTED);
    
    PRInt32 childIndex = parent->FindChild(node);
    NS_ASSERTION(childIndex >= 0, "Child not found in parent");
    parent->RemoveChildAt(childIndex);

    if (parent->mChildren.Count() == 0 && parent->IsQuery()) {
      // when query subcontainers (like hosts) get empty we should remove them
      // as well. Just append this to our list and it will get evaluated later
      // in the loop.
      matches.AppendObject(parent);
    }
  }
  return NS_OK;
}


// nsNavHistoryQueryResultNode::OnClearHistory

NS_IMETHODIMP
nsNavHistoryQueryResultNode::OnClearHistory()
{
  (void)Refresh();
  return NS_OK;
}


// nsNavHistoryQueryResultNode::OnPageChanged
//

static void setFaviconCallback(
   nsNavHistoryResultNode* aNode, void* aClosure,
   nsNavHistoryResult* aResult)
{
  const nsCString* newFavicon = static_cast<nsCString*>(aClosure);
  aNode->mFaviconURI = *newFavicon;

  if (aResult && aResult->GetView() &&
      (!aNode->mParent || aNode->mParent->AreChildrenVisible())) {
    aResult->GetView()->NodeIconChanged(aNode);
  }
}
NS_IMETHODIMP
nsNavHistoryQueryResultNode::OnPageChanged(nsIURI *aURI, PRUint32 aWhat,
                         const nsAString &aValue)
{
  nsNavHistoryResult* result = GetResult();
  NS_ENSURE_TRUE(result, NS_ERROR_FAILURE);

  nsCAutoString spec;
  nsresult rv = aURI->GetSpec(spec);
  NS_ENSURE_SUCCESS(rv, rv);

  switch (aWhat) {
    case nsINavHistoryObserver::ATTRIBUTE_FAVICON: {
      NS_ConvertUTF16toUTF8 newFavicon(aValue);
      PRBool onlyOneEntry = (mOptions->ResultType() ==
                             nsINavHistoryQueryOptions::RESULTS_AS_URI ||
                             mOptions->ResultType() ==
                             nsINavHistoryQueryOptions::RESULTS_AS_TAG_CONTENTS);
      UpdateURIs(PR_TRUE, onlyOneEntry, PR_FALSE, spec, setFaviconCallback,
                 &newFavicon);
      break;
    }
    default:
      NS_WARNING("Unknown page changed notification");
  }
  return NS_OK;
}


// nsNavHistoryQueryResultNode::OnDeleteVisits
//
//    Do nothing. Perhaps we want to handle this case. If so, add the call to
//    the result to enumerate the history observers.

NS_IMETHODIMP
nsNavHistoryQueryResultNode::OnDeleteVisits(nsIURI* aURI, PRTime aVisitTime)
{
  return NS_OK;
}

// nsNavHistoryQueryResultNode bookmark observers
//
//    These are the bookmark observer functions for query nodes. They listen
//    for bookmark events and refresh the results if we have any dependence on
//    the bookmark system.

NS_IMETHODIMP
nsNavHistoryQueryResultNode::OnItemAdded(PRInt64 aItemId,
                                         PRInt64 aFolder,
                                         PRInt32 aIndex,
                                         PRUint16 aItemType)
{
  if (aItemType == nsINavBookmarksService::TYPE_BOOKMARK &&
      mLiveUpdate == QUERYUPDATE_COMPLEX_WITH_BOOKMARKS)
    return Refresh();
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryQueryResultNode::OnBeforeItemRemoved(PRInt64 aItemId,
                                                 PRUint16 aItemType)
{
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryQueryResultNode::OnItemRemoved(PRInt64 aItemId, PRInt64 aFolder,
                                           PRInt32 aIndex, PRUint16 aItemType)
{
  if (mLiveUpdate == QUERYUPDATE_COMPLEX_WITH_BOOKMARKS)
    return Refresh();
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryQueryResultNode::OnItemChanged(PRInt64 aItemId,
                                           const nsACString& aProperty,
                                           PRBool aIsAnnotationProperty,
                                           const nsACString& aNewValue,
                                           PRTime aLastModified,
                                           PRUint16 aItemType)
{
  // History observers should not get OnItemChanged
  // but should get the corresponding history notifications instead.
  // For bookmark queries, "all bookmark" observers should get OnItemChanged.
  // For example, when a title of a bookmark changes, we want that to refresh.

  if (mLiveUpdate == QUERYUPDATE_COMPLEX_WITH_BOOKMARKS) {
    switch (aItemType) {
      case nsINavBookmarksService::TYPE_SEPARATOR:
        // No separators in queries.
        return NS_OK;
      case nsINavBookmarksService::TYPE_FOLDER:
        // Queries never result as "folders", but the tags-query results as
        // special "tag" containers, which should follow their corresponding
        // folders titles.
        if (mOptions->ResultType() != nsINavHistoryQueryOptions::RESULTS_AS_TAG_QUERY)
          return NS_OK;
      default:
        (void)Refresh();
    }
  }
  else {
    NS_WARNING("history observers should not get OnItemChanged, but should get the corresponding history notifications instead");
  }

  return nsNavHistoryResultNode::OnItemChanged(aItemId, aProperty,
                                               aIsAnnotationProperty,
                                               aNewValue,
                                               aLastModified,
                                               aItemType);
}

NS_IMETHODIMP
nsNavHistoryQueryResultNode::OnItemVisited(PRInt64 aItemId,
                                           PRInt64 aVisitId, PRTime aTime)
{
  // for bookmark queries, "all bookmark" observer should get OnItemVisited
  // but it is ignored.
  if (mLiveUpdate != QUERYUPDATE_COMPLEX_WITH_BOOKMARKS)
    NS_WARNING("history observers should not get OnItemVisited, but should get OnVisit instead");
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryQueryResultNode::OnItemMoved(PRInt64 aFolder,
                                         PRInt64 aOldParent, PRInt32 aOldIndex,
                                         PRInt64 aNewParent, PRInt32 aNewIndex,
                                         PRUint16 aItemType)
{
  // 1. The query cannot be affected by the item's position
  // 2. For the time being, we cannot optimize this not to update
  //    queries which are not restricted to some folders, due to way
  //    sub-queries are updated (see Refresh)
  if (mLiveUpdate == QUERYUPDATE_COMPLEX_WITH_BOOKMARKS &&
      aItemType != nsINavBookmarksService::TYPE_SEPARATOR &&
      aOldParent != aNewParent) {
    return Refresh();
  }
  return NS_OK;
}

// nsNavHistoryFolderResultNode ************************************************
//
//    HOW DYNAMIC FOLDER UPDATING WORKS
//
//    When you create a result, it will automatically keep itself in sync with
//    stuff that happens in the system. For folder nodes, this means changes
//    to bookmarks.
//
//    A folder will fill its children "when necessary." This means it is being
//    opened or whether we need to see if it is empty for twisty drawing. It
//    will then register its ID with the main result object that owns it. This
//    result object will listen for all bookmark notifications and pass those
//    notifications to folder nodes that have registered for that specific
//    folder ID.
//
//    When a bookmark folder is closed, it will not clear its children. Instead,
//    it will keep them and also stay registered as a listener. This means that
//    you can more quickly re-open the same folder without doing any work. This
//    happens a lot for menus, and bookmarks don't change very often.
//
//    When a message comes in and the folder is open, we will do the correct
//    operations to keep ourselves in sync with the bookmark service. If the
//    folder is closed, we just clear our list to mark it as invalid and
//    unregister as a listener. This means we do not have to keep maintaining
//    an up-to-date list for the entire bookmark menu structure in every place
//    it is used.
//
//    There is an additional wrinkle. If the result is attached to a treeview,
//    and we invalidate but the folder itself is visible (its parent is open),
//    we will immediately get asked if we are full. The folder will then query
//    its children. Thus, the whole benefit of incremental updating is lost.
//    Therefore, if we are in a treeview AND our parent is visible, we will
//    keep incremental updating.

NS_IMPL_ISUPPORTS_INHERITED1(nsNavHistoryFolderResultNode,
                             nsNavHistoryContainerResultNode,
                             nsINavHistoryQueryResultNode)

nsNavHistoryFolderResultNode::nsNavHistoryFolderResultNode(
    const nsACString& aTitle, nsNavHistoryQueryOptions* aOptions,
    PRInt64 aFolderId, const nsACString& aDynamicContainerType) :
  nsNavHistoryContainerResultNode(EmptyCString(), aTitle, EmptyCString(),
                                  nsNavHistoryResultNode::RESULT_TYPE_FOLDER,
                                  PR_FALSE, aDynamicContainerType, aOptions),
  mContentsValid(PR_FALSE),
  mQueryItemId(-1),
  mIsRegisteredFolderObserver(PR_FALSE)
{
  mItemId = aFolderId;
}

nsNavHistoryFolderResultNode::~nsNavHistoryFolderResultNode()
{
  if (mIsRegisteredFolderObserver && mResult)
    mResult->RemoveBookmarkFolderObserver(this, mItemId);
}

// nsNavHistoryFolderResultNode::OnRemoving
//
//    Here we do not want to call ContainerResultNode::OnRemoving since our own
//    ClearChildren will do the same thing and more (unregister the observers).
//    The base ResultNode::OnRemoving will clear some regular node stats, so it
//    is OK.

void
nsNavHistoryFolderResultNode::OnRemoving()
{
  nsNavHistoryResultNode::OnRemoving();
  ClearChildren(PR_TRUE);
}


// nsNavHistoryFolderResultNode::OpenContainer
//
//    See nsNavHistoryQueryResultNode::OpenContainer

nsresult
nsNavHistoryFolderResultNode::OpenContainer()
{
  NS_ASSERTION(! mExpanded, "Container must be expanded to close it");
  nsresult rv;

  if (! mContentsValid) {
    rv = FillChildren();
    NS_ENSURE_SUCCESS(rv, rv);
    if (IsDynamicContainer()) {
      // dynamic container API may want to change the bookmarks for this folder.
      nsCOMPtr<nsIDynamicContainer> svc = do_GetService(mDynamicContainerType.get(), &rv);
      if (NS_SUCCEEDED(rv)) {
        svc->OnContainerNodeOpening(
            static_cast<nsNavHistoryContainerResultNode*>(this), mOptions);
      } else {
        NS_WARNING("Unable to get dynamic container for ");
        NS_WARNING(mDynamicContainerType.get());
      }
    }
  }
  mExpanded = PR_TRUE;

  nsNavHistoryResult* result = GetResult();
  NS_ENSURE_TRUE(result, NS_ERROR_FAILURE);
  if (result->GetView())
    result->GetView()->ContainerOpened(
        static_cast<nsNavHistoryContainerResultNode*>(this));
  return NS_OK;
}


// nsNavHistoryFolderResultNode::GetHasChildren
//
//    See nsNavHistoryQueryResultNode::HasChildren.
//
//    The semantics here are a little different. Querying the contents of a
//    bookmark folder is relatively fast and it is common to have empty folders.
//    Therefore, we always want to return the correct result so that twisties
//    are drawn properly.

NS_IMETHODIMP
nsNavHistoryFolderResultNode::GetHasChildren(PRBool* aHasChildren)
{
  if (! mContentsValid) {
    nsresult rv = FillChildren();
    NS_ENSURE_SUCCESS(rv, rv);
  }
  *aHasChildren = (mChildren.Count() > 0);
  return NS_OK;
}

// nsNavHistoryFolderResultNode::GetItemId
//
// Returns the id of the item from which the folder node was generated, it
// could be either a concrete folder-itemId or the id used in
// a simple-folder-query-bookmark (place:folder=X)

NS_IMETHODIMP
nsNavHistoryFolderResultNode::GetItemId(PRInt64* aItemId)
{
  *aItemId = mQueryItemId == -1 ? mItemId : mQueryItemId;
  return NS_OK;
}

// nsNavHistoryFolderResultNode::GetChildrenReadOnly
//
//    Here, we override the getter and ignore the value stored in our object.
//    The bookmarks service can tell us whether this folder should be read-only
//    or not.
//
//    It would be nice to put this code in the folder constructor, but the
//    database was complaining. I believe it is because most folders are
//    created while enumerating the bookmarks table and having a statement
//    open, and doing another statement might make it unhappy in some cases.

NS_IMETHODIMP
nsNavHistoryFolderResultNode::GetChildrenReadOnly(PRBool *aChildrenReadOnly)
{
  nsNavBookmarks* bookmarks = nsNavBookmarks::GetBookmarksService();
  NS_ENSURE_TRUE(bookmarks, NS_ERROR_UNEXPECTED);
  return bookmarks->GetFolderReadonly(mItemId, aChildrenReadOnly);
}


// nsNavHistoryFolderResultNode::GetFolderItemId

NS_IMETHODIMP
nsNavHistoryFolderResultNode::GetFolderItemId(PRInt64* aItemId)
{
  *aItemId = mItemId;
  return NS_OK;
}

// nsNavHistoryFolderResultNode::GetUri
//
//    This lazily computes the URI for this specific folder query with
//    the current options.

NS_IMETHODIMP
nsNavHistoryFolderResultNode::GetUri(nsACString& aURI)
{
  if (! mURI.IsEmpty()) {
    aURI = mURI;
    return NS_OK;
  }

  PRUint32 queryCount;
  nsINavHistoryQuery** queries;
  nsresult rv = GetQueries(&queryCount, &queries);
  NS_ENSURE_SUCCESS(rv, rv);

  nsNavHistory* history = nsNavHistory::GetHistoryService();
  NS_ENSURE_TRUE(history, NS_ERROR_OUT_OF_MEMORY);

  rv = history->QueriesToQueryString(queries, queryCount, mOptions, aURI);
  for (PRUint32 queryIndex = 0; queryIndex < queryCount;  queryIndex ++) {
    NS_RELEASE(queries[queryIndex]);
  }
  nsMemory::Free(queries);
  return rv;
}


// nsNavHistoryFolderResultNode::GetQueries
//
//    This just returns the queries that give you this bookmarks folder

NS_IMETHODIMP
nsNavHistoryFolderResultNode::GetQueries(PRUint32* queryCount,
                                         nsINavHistoryQuery*** queries)
{
  // get the query object
  nsCOMPtr<nsINavHistoryQuery> query;
  nsNavHistory* history = nsNavHistory::GetHistoryService();
  NS_ENSURE_TRUE(history, NS_ERROR_OUT_OF_MEMORY);
  nsresult rv = history->GetNewQuery(getter_AddRefs(query));
  NS_ENSURE_SUCCESS(rv, rv);

  // query just has the folder ID set and nothing else
  rv = query->SetFolders(&mItemId, 1);
  NS_ENSURE_SUCCESS(rv, rv);

  // make array of our 1 query
  *queries = static_cast<nsINavHistoryQuery**>
                        (nsMemory::Alloc(sizeof(nsINavHistoryQuery*)));
  if (! *queries)
    return NS_ERROR_OUT_OF_MEMORY;
  NS_ADDREF((*queries)[0] = query);
  *queryCount = 1;
  return NS_OK;
}


// nsNavHistoryFolderResultNode::GetQueryOptions
//
//    Options for the query that gives you this bookmarks folder. This is just
//    the options for the folder with the current folder ID set.

NS_IMETHODIMP
nsNavHistoryFolderResultNode::GetQueryOptions(
                                      nsINavHistoryQueryOptions** aQueryOptions)
{
  NS_ASSERTION(mOptions, "Options invalid");

  *aQueryOptions = mOptions;
  NS_ADDREF(*aQueryOptions);
  return NS_OK;
}


// nsNavHistoryFolderResultNode::FillChildren
//
//    Call to fill the actual children of this folder.

nsresult
nsNavHistoryFolderResultNode::FillChildren()
{
  NS_ASSERTION(! mContentsValid,
               "Don't call FillChildren when contents are valid");
  NS_ASSERTION(mChildren.Count() == 0,
               "We are trying to fill children when there already are some");

  nsNavBookmarks* bookmarks = nsNavBookmarks::GetBookmarksService();
  NS_ENSURE_TRUE(bookmarks, NS_ERROR_OUT_OF_MEMORY);

  // actually get the folder children from the bookmark service
  nsresult rv = bookmarks->QueryFolderChildren(mItemId, mOptions, &mChildren);
  NS_ENSURE_SUCCESS(rv, rv);

  // PERFORMANCE: it may be better to also fill any child folders at this point
  // so that we can draw tree twisties without doing a separate query later. If
  // we don't end up drawing twisties a lot, it doesn't matter. If we do this,
  // we should wrap everything in a transaction here on the bookmark service's
  // connection.

  // it is important to call FillStats to fill in the parents on all
  // nodes and the result node pointers on the containers
  FillStats();

  if (mResult->mNeedsToApplySortingMode) {
    // We should repopulate container and then apply sortingMode.  To avoid
    // sorting 2 times we simply do that here.
    mResult->SetSortingMode(mResult->mSortingMode);
  }
  else {
    // once we've computed all tree stats, we can sort, because containers will
    // then have proper visit counts and dates
    SortComparator comparator = GetSortingComparator(GetSortType());
    if (comparator) {
      nsCAutoString sortingAnnotation;
      GetSortingAnnotation(sortingAnnotation);
      RecursiveSort(sortingAnnotation.get(), comparator);
    }
  }

  // if we are limiting our results remove items from the end of the
  // mChildren array after sorting. This is done for root node only.
  // note, if count < max results, we won't do anything.
  if (!mParent && mOptions->MaxResults()) {
    while ((PRUint32)mChildren.Count() > mOptions->MaxResults())
      mChildren.RemoveObjectAt(mChildren.Count() - 1);
  }

  // register with the result for updates
  nsNavHistoryResult* result = GetResult();
  NS_ENSURE_TRUE(result, NS_ERROR_FAILURE);
  result->AddBookmarkFolderObserver(this, mItemId);
  mIsRegisteredFolderObserver = PR_TRUE;

  mContentsValid = PR_TRUE;
  return NS_OK;
}


// nsNavHistoryFolderResultNode::ClearChildren
//
//    See nsNavHistoryQueryResultNode::FillChildren

void
nsNavHistoryFolderResultNode::ClearChildren(PRBool unregister)
{
  for (PRInt32 i = 0; i < mChildren.Count(); i ++)
    mChildren[i]->OnRemoving();
  mChildren.Clear();

  if (unregister && mContentsValid) {
    if (mResult) {
      mResult->RemoveBookmarkFolderObserver(this, mItemId);
      mIsRegisteredFolderObserver = PR_FALSE;
    }
  }
  mContentsValid = PR_FALSE;
}


// nsNavHistoryFolderResultNode::Refresh
//
//    This is called to update the result when something has changed that we
//    can not incrementally update.

nsresult
nsNavHistoryFolderResultNode::Refresh()
{
  ClearChildren(PR_TRUE);

  if (! mExpanded) {
    // when we are not expanded, we don't update, just invalidate and unhook
    return NS_OK; // no updates in tree state
  }

  // ignore errors from FillChildren, since we will still want to refresh
  // the tree (there just might not be anything in it on error). ClearChildren
  // has unregistered us as an observer since FillChildren will try to
  // re-register us.
  (void)FillChildren();

  nsNavHistoryResult* result = GetResult();
  NS_ENSURE_TRUE(result, NS_ERROR_FAILURE);
  if (result->GetView())
    return result->GetView()->InvalidateContainer(
        static_cast<nsNavHistoryContainerResultNode*>(this));
  return NS_OK;
}


// nsNavHistoryFolderResultNode::StartIncrementalUpdate
//
//    This implements the logic described above the constructor. This sees if
//    we should do an incremental update and returns true if so. If not, it
//    invalidates our children, unregisters us an observer, and returns false.

PRBool
nsNavHistoryFolderResultNode::StartIncrementalUpdate()
{
  // if any items are excluded, we can not do incremental updates since the
  // indices from the bookmark service will not be valid
  nsCAutoString parentAnnotationToExclude;
  nsresult rv = mOptions->GetExcludeItemIfParentHasAnnotation(parentAnnotationToExclude);
  NS_ENSURE_SUCCESS(rv, PR_FALSE);

  if (! mOptions->ExcludeItems() && 
      ! mOptions->ExcludeQueries() && 
      ! mOptions->ExcludeReadOnlyFolders() && 
      parentAnnotationToExclude.IsEmpty()) {

    // easy case: we are visible, always do incremental update
    if (mExpanded || AreChildrenVisible())
      return PR_TRUE;

    nsNavHistoryResult* result = GetResult();
    NS_ENSURE_TRUE(result, PR_FALSE);

    // when a tree is attached also do incremental updates if our parent is
    // visible so that twisties are drawn correctly.
    if (mParent && result->GetView())
      return PR_TRUE;
  }

  // otherwise, we don't do incremental updates, invalidate and unregister
  (void)Refresh();
  return PR_FALSE;
}


// nsNavHistoryFolderResultNode::ReindexRange
//
//    This function adds aDelta to all bookmark indices between the two
//    endpoints, inclusive. It is used when items are added or removed from
//    the bookmark folder.

void
nsNavHistoryFolderResultNode::ReindexRange(PRInt32 aStartIndex,
                                           PRInt32 aEndIndex,
                                           PRInt32 aDelta)
{
  for (PRInt32 i = 0; i < mChildren.Count(); i ++) {
    nsNavHistoryResultNode* node = mChildren[i];
    if (node->mBookmarkIndex >= aStartIndex &&
        node->mBookmarkIndex <= aEndIndex)
      node->mBookmarkIndex += aDelta;
  }
}


// nsNavHistoryFolderResultNode::FindChildById
//
//    Searches this folder for a node with the given id. Returns null if not
//    found. Does not addref the node!

nsNavHistoryResultNode*
nsNavHistoryFolderResultNode::FindChildById(PRInt64 aItemId,
    PRUint32* aNodeIndex)
{
  for (PRInt32 i = 0; i < mChildren.Count(); i ++) {
    if (mChildren[i]->mItemId == aItemId ||
        (mChildren[i]->IsFolder() &&
         mChildren[i]->GetAsFolder()->mQueryItemId == aItemId)) {
      *aNodeIndex = i;
      return mChildren[i];
    }
  }
  return nsnull;
}


// nsNavHistoryFolderResultNode::OnBeginUpdateBatch (nsINavBookmarkObserver)

NS_IMETHODIMP
nsNavHistoryFolderResultNode::OnBeginUpdateBatch()
{
  return NS_OK;
}


// nsNavHistoryFolderResultNode::OnEndUpdateBatch (nsINavBookmarkObserver)

NS_IMETHODIMP
nsNavHistoryFolderResultNode::OnEndUpdateBatch()
{
  return NS_OK;
}

// nsNavHistoryFolderResultNode::OnItemAdded (nsINavBookmarkObserver)

NS_IMETHODIMP
nsNavHistoryFolderResultNode::OnItemAdded(PRInt64 aItemId,
                                          PRInt64 aParentFolder,
                                          PRInt32 aIndex,
                                          PRUint16 aItemType)
{
  NS_ASSERTION(aParentFolder == mItemId, "Got wrong bookmark update");

  PRBool excludeItems = (mResult && mResult->mRootNode->mOptions->ExcludeItems()) ||
                        (mParent && mParent->mOptions->ExcludeItems()) ||
                        mOptions->ExcludeItems();

  // here, try to do something reasonable if the bookmark service gives us
  // a bogus index.
  if (aIndex < 0) {
    NS_NOTREACHED("Invalid index for item adding: <0");
    aIndex = 0;
  }
  else if (aIndex > mChildren.Count()) {
    if (!excludeItems) {
      // Something wrong happened while updating indexes.
      NS_NOTREACHED("Invalid index for item adding: greater than count");
    }
    aIndex = mChildren.Count();
  }

  nsNavBookmarks* bookmarks = nsNavBookmarks::GetBookmarksService();
  NS_ENSURE_TRUE(bookmarks, NS_ERROR_OUT_OF_MEMORY);

  nsresult rv;

  // check for query URIs, which are bookmarks, but treated as containers
  // in results and views.
  PRBool isQuery = PR_FALSE;
  if (aItemType == nsINavBookmarksService::TYPE_BOOKMARK) {
    nsCOMPtr<nsIURI> itemURI;
    rv = bookmarks->GetBookmarkURI(aItemId, getter_AddRefs(itemURI));
    NS_ENSURE_SUCCESS(rv, rv);
    nsCAutoString itemURISpec;
    rv = itemURI->GetSpec(itemURISpec);
    NS_ENSURE_SUCCESS(rv, rv);
    isQuery = IsQueryURI(itemURISpec);
  }

  if (aItemType != nsINavBookmarksService::TYPE_FOLDER &&
      !isQuery && excludeItems) {
    // don't update items when we aren't displaying them, but we still need
    // to adjust bookmark indices to account for the insertion
    ReindexRange(aIndex, PR_INT32_MAX, 1);
    return NS_OK; 
  }

  if (!StartIncrementalUpdate())
    return NS_OK; // folder was completely refreshed for us

  // adjust indices to account for insertion
  ReindexRange(aIndex, PR_INT32_MAX, 1);

  nsRefPtr<nsNavHistoryResultNode> node;
  if (aItemType == nsINavBookmarksService::TYPE_BOOKMARK) {
    nsNavHistory* history = nsNavHistory::GetHistoryService();
    NS_ENSURE_TRUE(history, NS_ERROR_OUT_OF_MEMORY);
    rv = history->BookmarkIdToResultNode(aItemId, mOptions, getter_AddRefs(node));
    NS_ENSURE_SUCCESS(rv, rv);
    // Correctly set batch status for new query nodes.
    if (mResult && node->IsQuery())
      node->GetAsQuery()->mBatchInProgress = mResult->mBatchInProgress;
  }
  else if (aItemType == nsINavBookmarksService::TYPE_FOLDER ||
           aItemType == nsINavBookmarksService::TYPE_DYNAMIC_CONTAINER) {
    rv = bookmarks->ResultNodeForContainer(aItemId, mOptions, getter_AddRefs(node));
    NS_ENSURE_SUCCESS(rv, rv);
  }
  else if (aItemType == nsINavBookmarksService::TYPE_SEPARATOR) {
    node = new nsNavHistorySeparatorResultNode();
    NS_ENSURE_TRUE(node, NS_ERROR_OUT_OF_MEMORY);
    node->mItemId = aItemId;
  }
  node->mBookmarkIndex = aIndex;

  if (aItemType == nsINavBookmarksService::TYPE_SEPARATOR ||
      GetSortType() == nsINavHistoryQueryOptions::SORT_BY_NONE) {
    // insert at natural bookmarks position
    return InsertChildAt(node, aIndex);
  }
  // insert at sorted position
  return InsertSortedChild(node, PR_FALSE);
}


// nsNavHistoryFolderResultNode::OnBeforeItemRemoved (nsINavBookmarkObserver)

NS_IMETHODIMP
nsNavHistoryFolderResultNode::OnBeforeItemRemoved(PRInt64 aItemId,
                                                  PRUint16 aItemType)
{
  return NS_OK;
}


// nsNavHistoryFolderResultNode::OnItemRemoved (nsINavBookmarkObserver)

NS_IMETHODIMP
nsNavHistoryFolderResultNode::OnItemRemoved(PRInt64 aItemId,
                                            PRInt64 aParentFolder,
                                            PRInt32 aIndex,
                                            PRUint16 aItemType)
{
  // We only care about notifications when a child changes. When the deleted
  // item is us, our parent should also be registered and will remove us from
  // its list.
  if (mItemId == aItemId)
    return NS_OK;

  NS_ASSERTION(aParentFolder == mItemId, "Got wrong bookmark update");

  PRBool excludeItems = (mResult && mResult->mRootNode->mOptions->ExcludeItems()) ||
                        (mParent && mParent->mOptions->ExcludeItems()) ||
                        mOptions->ExcludeItems();

  // don't trust the index from the bookmark service, find it ourselves. The
  // sorting could be different, or the bookmark services indices and ours might
  // be out of sync somehow.
  PRUint32 index;
  nsNavHistoryResultNode* node = FindChildById(aItemId, &index);
  if (!node) {
    if (excludeItems)
      return NS_OK;

    NS_NOTREACHED("Removing item we don't have");
    return NS_ERROR_FAILURE;
  }

  if ((node->IsURI() || node->IsSeparator()) && excludeItems) {
    // don't update items when we aren't displaying them, but we do need to
    // adjust everybody's bookmark indices to account for the removal
    ReindexRange(aIndex, PR_INT32_MAX, -1);
    return NS_OK;
  }

  if (!StartIncrementalUpdate())
    return NS_OK; // we are completely refreshed

  // shift all following indices down
  ReindexRange(aIndex + 1, PR_INT32_MAX, -1);

  return RemoveChildAt(index);
}


// nsNavHistoryResultNode::OnItemChanged

NS_IMETHODIMP
nsNavHistoryResultNode::OnItemChanged(PRInt64 aItemId,
                                      const nsACString& aProperty,
                                      PRBool aIsAnnotationProperty,
                                      const nsACString& aNewValue,
                                      PRTime aLastModified,
                                      PRUint16 aItemType)
{
  if (aItemId != mItemId)
    return NS_OK;

  mLastModified = aLastModified;

  nsNavHistoryResult* result = GetResult();
  NS_ENSURE_TRUE(result, NS_ERROR_FAILURE);

  PRBool shouldUpdateView =
    result->GetView() && (!mParent || mParent->AreChildrenVisible());

  if (aIsAnnotationProperty) {
    if (shouldUpdateView)
      result->GetView()->NodeAnnotationChanged(this, aProperty);
  }
  else if (aProperty.EqualsLiteral("title")) {
    // XXX: what should we do if the new title is void?
    mTitle = aNewValue;
    if (shouldUpdateView)
      result->GetView()->NodeTitleChanged(this, mTitle);
  }
  else if (aProperty.EqualsLiteral("uri")) {
    // clear the tags string as well
    mTags.SetIsVoid(PR_TRUE);
    mURI = aNewValue;
    if (shouldUpdateView)
      result->GetView()->NodeURIChanged(this, mURI);
  }
  else if (aProperty.EqualsLiteral("favicon")) {
    mFaviconURI = aNewValue;
    if (shouldUpdateView)
      result->GetView()->NodeIconChanged(this);
  }
  else if (aProperty.EqualsLiteral("cleartime")) {
    mTime = 0;
    if (shouldUpdateView)
      result->GetView()->NodeHistoryDetailsChanged(this, 0, mAccessCount);
  }
  else if (aProperty.EqualsLiteral("tags")) {
    mTags.SetIsVoid(PR_TRUE);
    if (shouldUpdateView)
      result->GetView()->NodeTagsChanged(this);
  }
  else if (aProperty.EqualsLiteral("dateAdded")) {
    // aNewValue has the date as a string, but we can use aLastModified,
    // because it's set to the same value when dateAdded is changed.
    mDateAdded = aLastModified;
    if (shouldUpdateView)
      result->GetView()->NodeDateAddedChanged(this, mDateAdded);
  }
  else if (aProperty.EqualsLiteral("lastModified")) {
    if (shouldUpdateView)
      result->GetView()->NodeLastModifiedChanged(this, aLastModified);
  }
  else if (aProperty.EqualsLiteral("keyword")) {
    if (shouldUpdateView)
      result->GetView()->NodeKeywordChanged(this, aNewValue);
  }
  else {
    NS_NOTREACHED("Unknown bookmark property changing.");
  }

  if (!mParent)
    return NS_OK;

  // DO NOT OPTIMIZE THIS TO CHECK aProperty
  // the sorting methods fall back to each other so we need to re-sort the
  // result even if it's not set to sort by the given property
  PRInt32 ourIndex = mParent->FindChild(this);
  mParent->EnsureItemPosition(ourIndex);

  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryFolderResultNode::OnItemChanged(PRInt64 aItemId,
                                            const nsACString& aProperty,
                                            PRBool aIsAnnotationProperty,
                                            const nsACString& aNewValue,
                                            PRTime aLastModified,
                                            PRUint16 aItemType) {
  // The query-item's title is used for simple-query nodes
  if (mQueryItemId != -1) {
    PRBool isTitleChange = aProperty.EqualsLiteral("title");
    if ((mQueryItemId == aItemId && !isTitleChange) ||
        (mQueryItemId != aItemId && isTitleChange)) {
      return NS_OK;
    }
  }

  return nsNavHistoryResultNode::OnItemChanged(aItemId, aProperty,
                                               aIsAnnotationProperty,
                                               aNewValue,
                                               aLastModified,
                                               aItemType);
}

// nsNavHistoryFolderResultNode::OnItemVisited (nsINavBookmarkObserver)
//
//    Update visit count and last visit time and refresh.

NS_IMETHODIMP
nsNavHistoryFolderResultNode::OnItemVisited(PRInt64 aItemId,
                                            PRInt64 aVisitId, PRTime aTime)
{
  PRBool excludeItems = (mResult && mResult->mRootNode->mOptions->ExcludeItems()) ||
                        (mParent && mParent->mOptions->ExcludeItems()) ||
                        mOptions->ExcludeItems();
  if (excludeItems)
    return NS_OK; // don't update items when we aren't displaying them
  if (!StartIncrementalUpdate())
    return NS_OK;

  PRUint32 nodeIndex;
  nsNavHistoryResultNode* node = FindChildById(aItemId, &nodeIndex);
  if (!node)
    return NS_ERROR_FAILURE;

  nsNavHistoryResult* result = GetResult();
  NS_ENSURE_TRUE(result, NS_ERROR_FAILURE);

  // update node
  node->mTime = aTime;
  node->mAccessCount ++;

  // update us
  PRInt32 oldAccessCount = mAccessCount;
  mAccessCount ++;
  if (aTime > mTime)
    mTime = aTime;
  ReverseUpdateStats(mAccessCount - oldAccessCount);

  if (result->GetView() && AreChildrenVisible()) {
    // Sorting has not changed, just redraw the row if it's visible.
    result->GetView()->NodeHistoryDetailsChanged(node, mTime, mAccessCount);
  }

  // update sorting if necessary
  PRUint32 sortType = GetSortType();
  if (sortType == nsINavHistoryQueryOptions::SORT_BY_VISITCOUNT_ASCENDING ||
      sortType == nsINavHistoryQueryOptions::SORT_BY_VISITCOUNT_DESCENDING ||
      sortType == nsINavHistoryQueryOptions::SORT_BY_DATE_ASCENDING ||
      sortType == nsINavHistoryQueryOptions::SORT_BY_DATE_DESCENDING) {
    PRInt32 childIndex = FindChild(node);
    NS_ASSERTION(childIndex >= 0, "Could not find child we just got a reference to");
    if (childIndex >= 0) {
      EnsureItemPosition(childIndex);
    }
  }

  return NS_OK;
}

// nsNavHistoryFolderResultNode::OnItemMoved (nsINavBookmarkObserver)

NS_IMETHODIMP
nsNavHistoryFolderResultNode::OnItemMoved(PRInt64 aItemId, PRInt64 aOldParent,
                                          PRInt32 aOldIndex, PRInt64 aNewParent,
                                          PRInt32 aNewIndex, PRUint16 aItemType)
{
  NS_ASSERTION(aOldParent == mItemId || aNewParent == mItemId,
               "Got a bookmark message that doesn't belong to us");
  if (! StartIncrementalUpdate())
    return NS_OK; // entire container was refreshed for us

  if (aOldParent == aNewParent) {
    // getting moved within the same folder, we don't want to do a remove and
    // an add because that will lose your tree state.

    // adjust bookmark indices
    ReindexRange(aOldIndex + 1, PR_INT32_MAX, -1);
    ReindexRange(aNewIndex, PR_INT32_MAX, 1);

    PRUint32 index;
    nsNavHistoryResultNode* node = FindChildById(aItemId, &index);
    if (!node) {
      NS_NOTREACHED("Can't find folder that is moving!");
      return NS_ERROR_FAILURE;
    }
    NS_ASSERTION(index >= 0 && index < PRUint32(mChildren.Count()),
                 "Invalid index!");
    node->mBookmarkIndex = aNewIndex;

    // adjust position
    EnsureItemPosition(index);
    return NS_OK;
  } else {
    // moving between two different folders, just do a remove and an add
    if (aOldParent == mItemId)
      OnItemRemoved(aItemId, aOldParent, aOldIndex, aItemType);
    if (aNewParent == mItemId)
      OnItemAdded(aItemId, aNewParent, aNewIndex, aItemType);
  }
  return NS_OK;
}


// nsNavHistorySeparatorResultNode
//
// Separator nodes do not hold any data

nsNavHistorySeparatorResultNode::nsNavHistorySeparatorResultNode()
  : nsNavHistoryResultNode(EmptyCString(), EmptyCString(),
                           0, 0, EmptyCString())
{
}


// nsNavHistoryResult **********************************************************
NS_IMPL_CYCLE_COLLECTION_CLASS(nsNavHistoryResult)

static PLDHashOperator
RemoveBookmarkFolderObserversCallback(nsTrimInt64HashKey::KeyType aKey,
                                      nsNavHistoryResult::FolderObserverList*& aData,
                                      void* userArg)
{
  delete aData;
  return PL_DHASH_REMOVE;
}

NS_IMPL_CYCLE_COLLECTION_UNLINK_BEGIN(nsNavHistoryResult)
  tmp->StopObserving();
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mRootNode)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSCOMPTR(mView)
  tmp->mBookmarkFolderObservers.Enumerate(&RemoveBookmarkFolderObserversCallback, nsnull);
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSTARRAY(mAllBookmarksObservers)
  NS_IMPL_CYCLE_COLLECTION_UNLINK_NSTARRAY(mHistoryObservers)
NS_IMPL_CYCLE_COLLECTION_UNLINK_END

static PLDHashOperator
TraverseBookmarkFolderObservers(nsTrimInt64HashKey::KeyType aKey,
                                nsNavHistoryResult::FolderObserverList* &aData,
                                void *aClosure)
{
  nsCycleCollectionTraversalCallback* cb =
    static_cast<nsCycleCollectionTraversalCallback*>(aClosure);
  for (PRUint32 i = 0; i < aData->Length(); ++i) {
    NS_CYCLE_COLLECTION_NOTE_EDGE_NAME(*cb,
                                       "mBookmarkFolderObservers value[i]");
    nsNavHistoryResultNode* node = aData->ElementAt(i);
    cb->NoteXPCOMChild(node);
  }
  return PL_DHASH_NEXT;
}

NS_IMPL_CYCLE_COLLECTION_TRAVERSE_BEGIN(nsNavHistoryResult)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR_AMBIGUOUS(mRootNode, nsINavHistoryContainerResultNode)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSCOMPTR(mView)
  tmp->mBookmarkFolderObservers.Enumerate(&TraverseBookmarkFolderObservers, &cb);
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSTARRAY_MEMBER(mAllBookmarksObservers, nsNavHistoryQueryResultNode)
  NS_IMPL_CYCLE_COLLECTION_TRAVERSE_NSTARRAY_MEMBER(mHistoryObservers, nsNavHistoryQueryResultNode)
NS_IMPL_CYCLE_COLLECTION_TRAVERSE_END

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsNavHistoryResult)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsNavHistoryResult)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsNavHistoryResult)
  NS_INTERFACE_MAP_ENTRY_AMBIGUOUS(nsISupports, nsINavHistoryResult)
  NS_INTERFACE_MAP_STATIC_AMBIGUOUS(nsNavHistoryResult)
  NS_INTERFACE_MAP_ENTRY(nsINavHistoryResult)
  NS_INTERFACE_MAP_ENTRY(nsINavBookmarkObserver)
  NS_INTERFACE_MAP_ENTRY(nsINavHistoryObserver)
  NS_INTERFACE_MAP_ENTRY(nsISupportsWeakReference)
NS_INTERFACE_MAP_END

nsNavHistoryResult::nsNavHistoryResult(nsNavHistoryContainerResultNode* aRoot)
: mRootNode(aRoot)
, mNeedsToApplySortingMode(PR_FALSE)
, mIsHistoryObserver(PR_FALSE)
, mIsBookmarkFolderObserver(PR_FALSE)
, mIsAllBookmarksObserver(PR_FALSE)
, mBatchInProgress(PR_FALSE)
{
  mRootNode->mResult = this;
}

nsNavHistoryResult::~nsNavHistoryResult()
{
  // delete all bookmark folder observer arrays which are allocated on the heap
  mBookmarkFolderObservers.Enumerate(&RemoveBookmarkFolderObserversCallback, nsnull);
}

void
nsNavHistoryResult::StopObserving()
{
  if (mIsBookmarkFolderObserver || mIsAllBookmarksObserver) {
    nsNavBookmarks* bookmarks = nsNavBookmarks::GetBookmarksService();
    if (bookmarks) {
      bookmarks->RemoveObserver(this);
      mIsBookmarkFolderObserver = PR_FALSE;
      mIsAllBookmarksObserver = PR_FALSE;
    }
  }
  if (mIsHistoryObserver) {
    nsNavHistory* history = nsNavHistory::GetHistoryService();
    if (history) {
      history->RemoveObserver(this);
      mIsHistoryObserver = PR_FALSE;
    }
  }
}

// nsNavHistoryResult::Init
//
//    Call AddRef before this, since we may do things like register ourselves.

nsresult
nsNavHistoryResult::Init(nsINavHistoryQuery** aQueries,
                         PRUint32 aQueryCount,
                         nsNavHistoryQueryOptions *aOptions)
{
  nsresult rv;
  NS_ASSERTION(aOptions, "Must have valid options");
  NS_ASSERTION(aQueries && aQueryCount > 0, "Must have >1 query in result");

  // Fill saved source queries with copies of the original (the caller might
  // change their original objects, and we always want to reflect the source
  // parameters).
  for (PRUint32 i = 0; i < aQueryCount; i ++) {
    nsCOMPtr<nsINavHistoryQuery> queryClone;
    rv = aQueries[i]->Clone(getter_AddRefs(queryClone));
    NS_ENSURE_SUCCESS(rv, rv);
    if (!mQueries.AppendObject(queryClone))
      return NS_ERROR_OUT_OF_MEMORY;
  }
  rv = aOptions->Clone(getter_AddRefs(mOptions));
  NS_ENSURE_SUCCESS(rv, rv);
  mSortingMode = aOptions->SortingMode();
  rv = aOptions->GetSortingAnnotation(mSortingAnnotation);
  NS_ENSURE_SUCCESS(rv, rv);

  if (! mBookmarkFolderObservers.Init(128))
    return NS_ERROR_OUT_OF_MEMORY;

  NS_ASSERTION(mRootNode->mIndentLevel == -1,
               "Root node's indent level initialized wrong");
  mRootNode->FillStats();

  return NS_OK;
}


// nsNavHistoryResult::NewHistoryResult
//
//    Constructs a new history result object.

nsresult // static
nsNavHistoryResult::NewHistoryResult(nsINavHistoryQuery** aQueries,
                                     PRUint32 aQueryCount,
                                     nsNavHistoryQueryOptions* aOptions,
                                     nsNavHistoryContainerResultNode* aRoot,
                                     nsNavHistoryResult** result)
{
  *result = new nsNavHistoryResult(aRoot);
  if (! *result)
    return NS_ERROR_OUT_OF_MEMORY;
  NS_ADDREF(*result); // must happen before Init
  nsresult rv = (*result)->Init(aQueries, aQueryCount, aOptions);
  if (NS_FAILED(rv)) {
    NS_RELEASE(*result);
    *result = nsnull;
    return rv;
  }

  // Correctly set mBatchInProgress for the result based on the root node value.
  if (aRoot->IsQuery())
    (*result)->mBatchInProgress = aRoot->GetAsQuery()->mBatchInProgress;

  return NS_OK;
}



// nsNavHistoryResult::AddHistoryObserver

void
nsNavHistoryResult::AddHistoryObserver(nsNavHistoryQueryResultNode* aNode)
{
  if (! mIsHistoryObserver) {
      nsNavHistory* history = nsNavHistory::GetHistoryService();
      NS_ASSERTION(history, "Can't create history service");
      history->AddObserver(this, PR_TRUE);
      mIsHistoryObserver = PR_TRUE;
  }
  if (mHistoryObservers.IndexOf(aNode) != mHistoryObservers.NoIndex) {
    NS_WARNING("Attempting to register as a history observer twice!");
    return;
  }
  mHistoryObservers.AppendElement(aNode);
}

// nsNavHistoryResult::AddAllBookmarksObserver

void
nsNavHistoryResult::AddAllBookmarksObserver(nsNavHistoryQueryResultNode* aNode)
{
  if (!mIsAllBookmarksObserver && !mIsBookmarkFolderObserver) {
    nsNavBookmarks* bookmarks = nsNavBookmarks::GetBookmarksService();
    if (! bookmarks) {
      NS_NOTREACHED("Can't create bookmark service");
      return;
    }
    bookmarks->AddObserver(this, PR_TRUE);
    mIsAllBookmarksObserver = PR_TRUE;
  }
  if (mAllBookmarksObservers.IndexOf(aNode) != mAllBookmarksObservers.NoIndex) {
    NS_WARNING("Attempting to register an all bookmarks observer twice!");
    return;
  }
  mAllBookmarksObservers.AppendElement(aNode);
}


// nsNavHistoryResult::AddBookmarkFolderObserver
//
//    Here, we also attach as a bookmark service observer if necessary

void
nsNavHistoryResult::AddBookmarkFolderObserver(nsNavHistoryFolderResultNode* aNode,
                                              PRInt64 aFolder)
{
  if (!mIsBookmarkFolderObserver && !mIsAllBookmarksObserver) {
    nsNavBookmarks* bookmarks = nsNavBookmarks::GetBookmarksService();
    if (!bookmarks) {
      NS_NOTREACHED("Can't create bookmark service");
      return;
    }
    bookmarks->AddObserver(this, PR_TRUE);
    mIsBookmarkFolderObserver = PR_TRUE;
  }

  FolderObserverList* list = BookmarkFolderObserversForId(aFolder, PR_TRUE);
  if (list->IndexOf(aNode) != list->NoIndex) {
    NS_NOTREACHED("Attempting to register as a folder observer twice!");
    return;
  }
  list->AppendElement(aNode);
}


// nsNavHistoryResult::RemoveHistoryObserver

void
nsNavHistoryResult::RemoveHistoryObserver(nsNavHistoryQueryResultNode* aNode)
{
  mHistoryObservers.RemoveElement(aNode);
}

// nsNavHistoryResult::RemoveAllBookmarksObserver

void
nsNavHistoryResult::RemoveAllBookmarksObserver(nsNavHistoryQueryResultNode* aNode)
{
  mAllBookmarksObservers.RemoveElement(aNode);
}


// nsNavHistoryResult::RemoveBookmarkFolderObserver

void
nsNavHistoryResult::RemoveBookmarkFolderObserver(nsNavHistoryFolderResultNode* aNode,
                                                 PRInt64 aFolder)
{
  FolderObserverList* list = BookmarkFolderObserversForId(aFolder, PR_FALSE);
  if (! list)
    return; // we don't even have an entry for that folder
  list->RemoveElement(aNode);
}


// nsNavHistoryResult::BookmarkFolderObserversForId

nsNavHistoryResult::FolderObserverList*
nsNavHistoryResult::BookmarkFolderObserversForId(PRInt64 aFolderId, PRBool aCreate)
{
  FolderObserverList* list;
  if (mBookmarkFolderObservers.Get(aFolderId, &list))
    return list;
  if (! aCreate)
    return nsnull;

  // need to create a new list
  list = new FolderObserverList;
  mBookmarkFolderObservers.Put(aFolderId, list);
  return list;
}

// nsNavHistoryResult::GetSortingMode (nsINavHistoryResult)

NS_IMETHODIMP
nsNavHistoryResult::GetSortingMode(PRUint16* aSortingMode)
{
  *aSortingMode = mSortingMode;
  return NS_OK;
}

// nsNavHistoryResult::SetSortingMode (nsINavHistoryResult)

NS_IMETHODIMP
nsNavHistoryResult::SetSortingMode(PRUint16 aSortingMode)
{
  if (aSortingMode > nsINavHistoryQueryOptions::SORT_BY_ANNOTATION_DESCENDING)
    return NS_ERROR_INVALID_ARG;
  if (! mRootNode)
    return NS_ERROR_FAILURE;

  // keep everything in sync
  NS_ASSERTION(mOptions, "Options should always be present for a root query");

  mSortingMode = aSortingMode;

  if (!mRootNode->mExpanded) {
    // Need to do this later when node will be expanded.
    mNeedsToApplySortingMode = PR_TRUE;
    return NS_OK;
  }

  // actually do sorting
  nsNavHistoryContainerResultNode::SortComparator comparator =
      nsNavHistoryContainerResultNode::GetSortingComparator(aSortingMode);
  if (comparator) {
    nsNavHistory* history = nsNavHistory::GetHistoryService();
    NS_ENSURE_TRUE(history, NS_ERROR_OUT_OF_MEMORY);
    mRootNode->RecursiveSort(mSortingAnnotation.get(), comparator);
  }

  if (mView) {
    mView->SortingChanged(aSortingMode);
    mView->InvalidateContainer(mRootNode);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryResult::GetSortingAnnotation(nsACString& _result) {
  _result.Assign(mSortingAnnotation);
  return NS_OK;
}

NS_IMETHODIMP
nsNavHistoryResult::SetSortingAnnotation(const nsACString& aSortingAnnotation) {
  mSortingAnnotation.Assign(aSortingAnnotation);
  return NS_OK;
}

// nsNavHistoryResult::Viewer (nsINavHistoryResult)

NS_IMETHODIMP
nsNavHistoryResult::GetViewer(nsINavHistoryResultViewer** aViewer)
{
  *aViewer = mView;
  NS_IF_ADDREF(*aViewer);
  return NS_OK;
}
NS_IMETHODIMP
nsNavHistoryResult::SetViewer(nsINavHistoryResultViewer* aViewer)
{
  mView = aViewer;
  if (aViewer)
    aViewer->SetResult(this);
  return NS_OK;
}


// nsNavHistoryResult::GetRoot (nsINavHistoryResult)
//
NS_IMETHODIMP
nsNavHistoryResult::GetRoot(nsINavHistoryContainerResultNode** aRoot)
{
  if (! mRootNode) {
    NS_NOTREACHED("Root is null");
    *aRoot = nsnull;
    return NS_ERROR_FAILURE;
  }
  return mRootNode->QueryInterface(NS_GET_IID(nsINavHistoryContainerResultNode),
                                   reinterpret_cast<void**>(aRoot));
}


// nsINavBookmarkObserver implementation

// Here, it is important that we create a COPY of the observer array. Some
// observers will requery themselves, which may cause the observer array to
// be modified or added to.
#define ENUMERATE_BOOKMARK_FOLDER_OBSERVERS(_folderId, _functionCall) \
  PR_BEGIN_MACRO \
    FolderObserverList* _fol = BookmarkFolderObserversForId(_folderId, PR_FALSE); \
    if (_fol) { \
      FolderObserverList _listCopy(*_fol); \
      for (PRUint32 _fol_i = 0; _fol_i < _listCopy.Length(); _fol_i ++) { \
        if (_listCopy[_fol_i]) \
          _listCopy[_fol_i]->_functionCall; \
      } \
    } \
  PR_END_MACRO
#define ENUMERATE_QUERY_OBSERVERS(_functionCall, _observersList, _conditionCall) \
  PR_BEGIN_MACRO \
    QueryObserverList _listCopy(_observersList); \
    for (PRUint32 _obs_i = 0; _obs_i < _listCopy.Length(); _obs_i ++) { \
      if (_listCopy[_obs_i] && _listCopy[_obs_i]->_conditionCall) \
        _listCopy[_obs_i]->_functionCall; \
    } \
  PR_END_MACRO
#define ENUMERATE_ALL_BOOKMARKS_OBSERVERS(_functionCall) \
  ENUMERATE_QUERY_OBSERVERS(_functionCall, mAllBookmarksObservers, IsQuery())
#define ENUMERATE_HISTORY_OBSERVERS(_functionCall) \
  ENUMERATE_QUERY_OBSERVERS(_functionCall, mHistoryObservers, IsQuery())

// nsNavHistoryResult::OnBeginUpdateBatch (nsINavBookmark/HistoryObserver)

NS_IMETHODIMP
nsNavHistoryResult::OnBeginUpdateBatch()
{
  mBatchInProgress = PR_TRUE;
  ENUMERATE_HISTORY_OBSERVERS(OnBeginUpdateBatch());
  ENUMERATE_ALL_BOOKMARKS_OBSERVERS(OnBeginUpdateBatch());
  return NS_OK;
}


// nsNavHistoryResult::OnEndUpdateBatch (nsINavBookmark/HistoryObserver)

NS_IMETHODIMP
nsNavHistoryResult::OnEndUpdateBatch()
{
  if (mBatchInProgress) {
    mBatchInProgress = PR_FALSE;
    ENUMERATE_HISTORY_OBSERVERS(OnEndUpdateBatch());
    ENUMERATE_ALL_BOOKMARKS_OBSERVERS(OnEndUpdateBatch());
  }
  else
    NS_WARNING("EndUpdateBatch without a begin");
  return NS_OK;
}


// nsNavHistoryResult::OnItemAdded (nsINavBookmarkObserver)

NS_IMETHODIMP
nsNavHistoryResult::OnItemAdded(PRInt64 aItemId,
                                PRInt64 aParentId,
                                PRInt32 aIndex,
                                PRUint16 aItemType)
{
  ENUMERATE_BOOKMARK_FOLDER_OBSERVERS(aParentId,
      OnItemAdded(aItemId, aParentId, aIndex, aItemType));
  ENUMERATE_HISTORY_OBSERVERS(OnItemAdded(aItemId, aParentId, aIndex, aItemType));
  ENUMERATE_ALL_BOOKMARKS_OBSERVERS(OnItemAdded(aItemId, aParentId, aIndex,
                                                aItemType));
  return NS_OK;
}


// nsNavHistoryResult::OnBeforeItemRemoved (nsINavBookmarkObserver)

NS_IMETHODIMP
nsNavHistoryResult::OnBeforeItemRemoved(PRInt64 aItemId, PRUint16 aItemType)
{
  // Nobody actually does anything with this method, so we do not need to notify
  return NS_OK;
}


// nsNavHistoryResult::OnItemRemoved (nsINavBookmarkObserver)

NS_IMETHODIMP
nsNavHistoryResult::OnItemRemoved(PRInt64 aItemId,
                                  PRInt64 aParentId, PRInt32 aIndex,
                                  PRUint16 aItemType)
{
  ENUMERATE_BOOKMARK_FOLDER_OBSERVERS(aParentId,
      OnItemRemoved(aItemId, aParentId, aIndex, aItemType));
  ENUMERATE_ALL_BOOKMARKS_OBSERVERS(
      OnItemRemoved(aItemId, aParentId, aIndex, aItemType));
  ENUMERATE_HISTORY_OBSERVERS(
      OnItemRemoved(aItemId, aParentId, aIndex, aItemType));
  return NS_OK;
}


// nsNavHistoryResult::OnItemChanged (nsINavBookmarkObserver)

NS_IMETHODIMP
nsNavHistoryResult::OnItemChanged(PRInt64 aItemId,
                                  const nsACString &aProperty,
                                  PRBool aIsAnnotationProperty,
                                  const nsACString &aNewValue,
                                  PRTime aLastModified,
                                  PRUint16 aItemType)
{
  ENUMERATE_ALL_BOOKMARKS_OBSERVERS(
    OnItemChanged(aItemId, aProperty, aIsAnnotationProperty, aNewValue,
                  aLastModified, aItemType));

  // Note: folder-nodes set their own bookmark observer only once they're
  // opened, meaning we cannot optimize this code path for changes done to
  // folder-nodes.

  nsNavBookmarks* bookmarkService = nsNavBookmarks::GetBookmarksService();
  NS_ENSURE_TRUE(bookmarkService, NS_ERROR_OUT_OF_MEMORY);

  // Find the changed items under the folders list
  PRInt64 folderId;
  nsresult rv = bookmarkService->GetFolderIdForItem(aItemId, &folderId);
  NS_ENSURE_SUCCESS(rv, rv);

  FolderObserverList* list = BookmarkFolderObserversForId(folderId, PR_FALSE);
  if (!list)
    return NS_OK;

  for (PRUint32 i = 0; i < list->Length(); i++) {
    nsRefPtr<nsNavHistoryFolderResultNode> folder = list->ElementAt(i);
    if (folder) {
      PRUint32 nodeIndex;
      nsRefPtr<nsNavHistoryResultNode> node =
        folder->FindChildById(aItemId, &nodeIndex);
      // if ExcludeItems is true we don't update non visible items
      PRBool excludeItems = (mRootNode->mOptions->ExcludeItems()) ||
                             folder->mOptions->ExcludeItems();
      if (node &&
          (!excludeItems || !(node->IsURI() || node->IsSeparator())) &&
          folder->StartIncrementalUpdate()) {
        node->OnItemChanged(aItemId, aProperty, aIsAnnotationProperty,
                            aNewValue, aLastModified, aItemType);
      }
    }
  }

  // Note: we do NOT call history observers in this case. This notification is
  // the same as other history notification, except that here we know the item
  // is a bookmark. History observers will handle the history notification
  // instead.
  return NS_OK;
}

// nsNavHistoryResult::OnItemVisited (nsINavBookmarkObserver)

NS_IMETHODIMP
nsNavHistoryResult::OnItemVisited(PRInt64 aItemId, PRInt64 aVisitId,
                                  PRTime aVisitTime)
{
  nsresult rv;
  nsNavBookmarks* bookmarkService = nsNavBookmarks::GetBookmarksService();
  NS_ENSURE_TRUE(bookmarkService, NS_ERROR_OUT_OF_MEMORY);

  // find the folder to notify about this item
  PRInt64 folderId;
  rv = bookmarkService->GetFolderIdForItem(aItemId, &folderId);
  NS_ENSURE_SUCCESS(rv, rv);
  ENUMERATE_BOOKMARK_FOLDER_OBSERVERS(folderId,
      OnItemVisited(aItemId, aVisitId, aVisitTime));
  ENUMERATE_ALL_BOOKMARKS_OBSERVERS(
      OnItemVisited(aItemId, aVisitId, aVisitTime));
  // Note: we do NOT call history observers in this case. This notification is
  // the same as OnVisit, except that here we know the item is a bookmark.
  // History observers will handle the history notification instead.
  return NS_OK;
}


// nsNavHistoryResult::OnItemMoved (nsINavBookmarkObserver)
//
//    Need to notify both the source and the destination folders (if they
//    are different).

NS_IMETHODIMP
nsNavHistoryResult::OnItemMoved(PRInt64 aItemId,
                                PRInt64 aOldParent, PRInt32 aOldIndex,
                                PRInt64 aNewParent, PRInt32 aNewIndex,
                                PRUint16 aItemType)
{
  { // scope for loop index for VC6's broken for loop scoping
    ENUMERATE_BOOKMARK_FOLDER_OBSERVERS(aOldParent,
        OnItemMoved(aItemId, aOldParent, aOldIndex, aNewParent, aNewIndex,
                    aItemType));
  }
  if (aNewParent != aOldParent) {
    ENUMERATE_BOOKMARK_FOLDER_OBSERVERS(aNewParent,
        OnItemMoved(aItemId, aOldParent, aOldIndex, aNewParent, aNewIndex,
                    aItemType));
  }
  ENUMERATE_ALL_BOOKMARKS_OBSERVERS(OnItemMoved(aItemId, aOldParent, aOldIndex,
                                                aNewParent, aNewIndex,
                                                aItemType));
  ENUMERATE_HISTORY_OBSERVERS(OnItemMoved(aItemId, aOldParent, aOldIndex,
                                          aNewParent, aNewIndex, aItemType));
  return NS_OK;
}

// nsNavHistoryResult::OnVisit (nsINavHistoryObserver)

NS_IMETHODIMP
nsNavHistoryResult::OnVisit(nsIURI* aURI, PRInt64 aVisitId, PRTime aTime,
                            PRInt64 aSessionId, PRInt64 aReferringId,
                            PRUint32 aTransitionType, PRUint32* aAdded)
{
  PRUint32 added = 0;

  ENUMERATE_HISTORY_OBSERVERS(OnVisit(aURI, aVisitId, aTime, aSessionId,
                                      aReferringId, aTransitionType, &added));

  if (!mRootNode->mExpanded)
    return NS_OK;

  // If this visit is accepted by an overlapped container, and not all
  // overlapped containers are visible, we should still call Refresh if the
  // visit falls into any of them.
  PRBool todayIsMissing = PR_FALSE;
  PRUint32 resultType = mRootNode->mOptions->ResultType();
  if (resultType == nsINavHistoryQueryOptions::RESULTS_AS_DATE_QUERY ||
      resultType == nsINavHistoryQueryOptions::RESULTS_AS_DATE_SITE_QUERY) {
    PRUint32 childCount;
    nsresult rv = mRootNode->GetChildCount(&childCount);
    NS_ENSURE_SUCCESS(rv, rv);
    if (childCount) {
      nsCOMPtr<nsINavHistoryResultNode> firstChild;
      rv = mRootNode->GetChild(0, getter_AddRefs(firstChild));
      NS_ENSURE_SUCCESS(rv, rv);
      nsCAutoString title;
      rv = firstChild->GetTitle(title);
      NS_ENSURE_SUCCESS(rv, rv);
      nsNavHistory* history = nsNavHistory::GetHistoryService();
      NS_ENSURE_TRUE(history, 0);
      nsCAutoString todayLabel;
      history->GetStringFromName(
        NS_LITERAL_STRING("finduri-AgeInDays-is-0").get(), todayLabel);
      todayIsMissing = !todayLabel.Equals(title);
    }
  }

  if (!added || todayIsMissing) {
    // None of registered query observers has accepted our URI.  This means,
    // that a matching query either was not expanded or it does not exist.
    PRUint32 resultType = mRootNode->mOptions->ResultType();
    if (resultType == nsINavHistoryQueryOptions::RESULTS_AS_DATE_QUERY ||
        resultType == nsINavHistoryQueryOptions::RESULTS_AS_DATE_SITE_QUERY ||
        resultType == nsINavHistoryQueryOptions::RESULTS_AS_SITE_QUERY)
      (void)mRootNode->GetAsQuery()->Refresh();
    else {
      // We are result of a folder node, then we should run through history
      // observers that are containers queries and refresh them.
      // We use a copy of the observers array since requerying could potentially
      // cause changes to the array.
      ENUMERATE_QUERY_OBSERVERS(Refresh(), mHistoryObservers, IsContainersQuery());
    }
  }

  return NS_OK;
}


// nsNavHistoryResult::OnTitleChanged (nsINavHistoryObserver)

NS_IMETHODIMP
nsNavHistoryResult::OnTitleChanged(nsIURI* aURI, const nsAString& aPageTitle)
{
  ENUMERATE_HISTORY_OBSERVERS(OnTitleChanged(aURI, aPageTitle));
  return NS_OK;
}


// nsNavHistoryResult::OnBeforeDeleteURI (nsINavHistoryObserver)
NS_IMETHODIMP
nsNavHistoryResult::OnBeforeDeleteURI(nsIURI *aURI)
{
  return NS_OK;
}


// nsNavHistoryResult::OnDeleteURI (nsINavHistoryObserver)

NS_IMETHODIMP
nsNavHistoryResult::OnDeleteURI(nsIURI *aURI)
{
  ENUMERATE_HISTORY_OBSERVERS(OnDeleteURI(aURI));
  return NS_OK;
}


// nsNavHistoryResult::OnClearHistory (nsINavHistoryObserver)

NS_IMETHODIMP
nsNavHistoryResult::OnClearHistory()
{
  ENUMERATE_HISTORY_OBSERVERS(OnClearHistory());
  return NS_OK;
}


// nsNavHistoryResult::OnPageChanged (nsINavHistoryObserver)

NS_IMETHODIMP
nsNavHistoryResult::OnPageChanged(nsIURI *aURI,
                                  PRUint32 aWhat, const nsAString &aValue)
{
  ENUMERATE_HISTORY_OBSERVERS(OnPageChanged(aURI, aWhat, aValue));
  return NS_OK;
}


// nsNavHistoryResult::OnDeleteVisits (nsINavHistoryObserver)
//
//    Don't do anything when visits expire.

NS_IMETHODIMP
nsNavHistoryResult::OnDeleteVisits(nsIURI* aURI, PRTime aVisitTime)
{
  return NS_OK;
}
