/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim:cindent:ts=2:et:sw=2:
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
 * Portions created by the Initial Developer are Copyright (C) 1998
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
 * ***** END LICENSE BLOCK *****
 *
 * This Original Code has been modified by IBM Corporation. Modifications made by IBM 
 * described herein are Copyright (c) International Business Machines Corporation, 2000.
 * Modifications to Mozilla code or documentation identified per MPL Section 3.3
 *
 * Date             Modified by     Description of modification
 * 04/20/2000       IBM Corp.      OS/2 VisualAge build.
 */

/* storage of the frame tree and information about it */

#include "nscore.h"
#include "nsPresContext.h"
#include "nsIPresShell.h"
#include "nsStyleSet.h"
#include "nsCSSFrameConstructor.h"
#include "nsStyleContext.h"
#include "nsStyleChangeList.h"
#include "nsIServiceManager.h"
#include "nsCOMPtr.h"
#include "prthread.h"
#include "plhash.h"
#include "nsPlaceholderFrame.h"
#include "nsContainerFrame.h"
#include "nsBlockFrame.h"
#include "nsGkAtoms.h"
#include "nsCSSAnonBoxes.h"
#include "nsCSSPseudoElements.h"
#ifdef NS_DEBUG
#include "nsIStyleRule.h"
#endif
#include "nsILayoutHistoryState.h"
#include "nsPresState.h"
#include "nsIContent.h"
#include "nsINameSpaceManager.h"
#include "nsIDocument.h"
#include "nsIScrollableFrame.h"

#include "nsIHTMLDocument.h"
#include "nsIDOMHTMLDocument.h"
#include "nsIDOMNodeList.h"
#include "nsIDOMHTMLCollection.h"
#include "nsIFormControl.h"
#include "nsIDOMElement.h"
#include "nsIDOMHTMLFormElement.h"
#include "nsIForm.h"
#include "nsContentUtils.h"
#include "nsReadableUtils.h"
#include "nsUnicharUtils.h"
#include "nsPrintfCString.h"
#include "nsLayoutErrors.h"
#include "nsLayoutUtils.h"
#include "nsAutoPtr.h"
#include "imgIRequest.h"
#include "nsTransitionManager.h"

#include "nsFrameManager.h"
#ifdef ACCESSIBILITY
#include "nsIAccessibilityService.h"
#include "nsIAccessibleEvent.h"
#endif

  #ifdef DEBUG
    //#define NOISY_DEBUG
    //#define DEBUG_UNDISPLAYED_MAP
  #else
    #undef NOISY_DEBUG
    #undef DEBUG_UNDISPLAYED_MAP
  #endif

  #ifdef NOISY_DEBUG
    #define NOISY_TRACE(_msg) \
      printf("%s",_msg);
    #define NOISY_TRACE_FRAME(_msg,_frame) \
      printf("%s ",_msg); nsFrame::ListTag(stdout,_frame); printf("\n");
  #else
    #define NOISY_TRACE(_msg);
    #define NOISY_TRACE_FRAME(_msg,_frame);
  #endif

// IID's

//----------------------------------------------------------------------

struct PlaceholderMapEntry : public PLDHashEntryHdr {
  // key (the out of flow frame) can be obtained through placeholder frame
  nsPlaceholderFrame *placeholderFrame;
};

static PRBool
PlaceholderMapMatchEntry(PLDHashTable *table, const PLDHashEntryHdr *hdr,
                         const void *key)
{
  const PlaceholderMapEntry *entry =
    static_cast<const PlaceholderMapEntry*>(hdr);
  NS_ASSERTION(entry->placeholderFrame->GetOutOfFlowFrame() !=
               (void*)0xdddddddd,
               "Dead placeholder in placeholder map");
  return entry->placeholderFrame->GetOutOfFlowFrame() == key;
}

static PLDHashTableOps PlaceholderMapOps = {
  PL_DHashAllocTable,
  PL_DHashFreeTable,
  PL_DHashVoidPtrKeyStub,
  PlaceholderMapMatchEntry,
  PL_DHashMoveEntryStub,
  PL_DHashClearEntryStub,
  PL_DHashFinalizeStub,
  NULL
};

//----------------------------------------------------------------------

// XXXldb This seems too complicated for what I think it's doing, and it
// should also be using pldhash rather than plhash to use less memory.

class UndisplayedNode {
public:
  UndisplayedNode(nsIContent* aContent, nsStyleContext* aStyle)
    : mContent(aContent),
      mStyle(aStyle),
      mNext(nsnull)
  {
    MOZ_COUNT_CTOR(UndisplayedNode);
  }

  NS_HIDDEN ~UndisplayedNode()
  {
    MOZ_COUNT_DTOR(UndisplayedNode);

    // Delete mNext iteratively to avoid blowing up the stack (bug 460461).
    UndisplayedNode *cur = mNext;
    while (cur) {
      UndisplayedNode *next = cur->mNext;
      cur->mNext = nsnull;
      delete cur;
      cur = next;
    }
  }

  nsCOMPtr<nsIContent>      mContent;
  nsRefPtr<nsStyleContext>  mStyle;
  UndisplayedNode*          mNext;
};

class nsFrameManagerBase::UndisplayedMap {
public:
  UndisplayedMap(PRUint32 aNumBuckets = 16) NS_HIDDEN;
  ~UndisplayedMap(void) NS_HIDDEN;

  NS_HIDDEN_(UndisplayedNode*) GetFirstNode(nsIContent* aParentContent);

  NS_HIDDEN_(nsresult) AddNodeFor(nsIContent* aParentContent,
                                  nsIContent* aChild, nsStyleContext* aStyle);

  NS_HIDDEN_(void) RemoveNodeFor(nsIContent* aParentContent,
                                 UndisplayedNode* aNode);

  NS_HIDDEN_(void) RemoveNodesFor(nsIContent* aParentContent);

  // Removes all entries from the hash table
  NS_HIDDEN_(void)  Clear(void);

protected:
  NS_HIDDEN_(PLHashEntry**) GetEntryFor(nsIContent* aParentContent);
  NS_HIDDEN_(void)          AppendNodeFor(UndisplayedNode* aNode,
                                          nsIContent* aParentContent);

  PLHashTable*  mTable;
  PLHashEntry** mLastLookup;
};

//----------------------------------------------------------------------

nsFrameManager::nsFrameManager()
{
}

nsFrameManager::~nsFrameManager()
{
  NS_ASSERTION(!mPresShell, "nsFrameManager::Destroy never called");
}

nsresult
nsFrameManager::Init(nsIPresShell* aPresShell,
                     nsStyleSet*  aStyleSet)
{
  if (!aPresShell) {
    NS_ERROR("null pres shell");
    return NS_ERROR_FAILURE;
  }

  if (!aStyleSet) {
    NS_ERROR("null style set");
    return NS_ERROR_FAILURE;
  }

  mPresShell = aPresShell;
  mStyleSet = aStyleSet;
  return NS_OK;
}

void
nsFrameManager::Destroy()
{
  NS_ASSERTION(mPresShell, "Frame manager already shut down.");

  // Destroy the frame hierarchy.
  mPresShell->SetIgnoreFrameDestruction(PR_TRUE);

  // Unregister all placeholders before tearing down the frame tree
  nsFrameManager::ClearPlaceholderFrameMap();

  if (mRootFrame) {
    mRootFrame->Destroy();
    mRootFrame = nsnull;
  }
  
  delete mUndisplayedMap;
  mUndisplayedMap = nsnull;

  mPresShell = nsnull;
}

nsIFrame*
nsFrameManager::GetCanvasFrame()
{
  if (mRootFrame) {
    // walk the children of the root frame looking for a frame with type==canvas
    // start at the root
    nsIFrame* childFrame = mRootFrame;
    while (childFrame) {
      // get each sibling of the child and check them, startig at the child
      nsIFrame *siblingFrame = childFrame;
      while (siblingFrame) {
        if (siblingFrame->GetType() == nsGkAtoms::canvasFrame) {
          // this is it
          return siblingFrame;
        } else {
          siblingFrame = siblingFrame->GetNextSibling();
        }
      }
      // move on to the child's child
      childFrame = childFrame->GetFirstChild(nsnull);
    }
  }
  return nsnull;
}

//----------------------------------------------------------------------

// Placeholder frame functions
nsPlaceholderFrame*
nsFrameManager::GetPlaceholderFrameFor(nsIFrame*  aFrame)
{
  NS_PRECONDITION(aFrame, "null param unexpected");

  if (mPlaceholderMap.ops) {
    PlaceholderMapEntry *entry = static_cast<PlaceholderMapEntry*>
                                            (PL_DHashTableOperate(const_cast<PLDHashTable*>(&mPlaceholderMap),
                                aFrame, PL_DHASH_LOOKUP));
    if (PL_DHASH_ENTRY_IS_BUSY(entry)) {
      return entry->placeholderFrame;
    }
  }

  return nsnull;
}

nsresult
nsFrameManager::RegisterPlaceholderFrame(nsPlaceholderFrame* aPlaceholderFrame)
{
  NS_PRECONDITION(aPlaceholderFrame, "null param unexpected");
  NS_PRECONDITION(nsGkAtoms::placeholderFrame == aPlaceholderFrame->GetType(),
                  "unexpected frame type");
  if (!mPlaceholderMap.ops) {
    if (!PL_DHashTableInit(&mPlaceholderMap, &PlaceholderMapOps, nsnull,
                           sizeof(PlaceholderMapEntry), 16)) {
      mPlaceholderMap.ops = nsnull;
      return NS_ERROR_OUT_OF_MEMORY;
    }
  }
  PlaceholderMapEntry *entry = static_cast<PlaceholderMapEntry*>(PL_DHashTableOperate(&mPlaceholderMap,
                              aPlaceholderFrame->GetOutOfFlowFrame(),
                              PL_DHASH_ADD));
  if (!entry)
    return NS_ERROR_OUT_OF_MEMORY;

  NS_ASSERTION(!entry->placeholderFrame, "Registering a placeholder for a frame that already has a placeholder!");
  entry->placeholderFrame = aPlaceholderFrame;

  return NS_OK;
}

void
nsFrameManager::UnregisterPlaceholderFrame(nsPlaceholderFrame* aPlaceholderFrame)
{
  NS_PRECONDITION(aPlaceholderFrame, "null param unexpected");
  NS_PRECONDITION(nsGkAtoms::placeholderFrame == aPlaceholderFrame->GetType(),
                  "unexpected frame type");

  if (mPlaceholderMap.ops) {
    PL_DHashTableOperate(&mPlaceholderMap,
                         aPlaceholderFrame->GetOutOfFlowFrame(),
                         PL_DHASH_REMOVE);
  }
}

static PLDHashOperator
UnregisterPlaceholders(PLDHashTable* table, PLDHashEntryHdr* hdr,
                       PRUint32 number, void* arg)
{
  PlaceholderMapEntry* entry = static_cast<PlaceholderMapEntry*>(hdr);
  entry->placeholderFrame->SetOutOfFlowFrame(nsnull);
  return PL_DHASH_NEXT;
}

void
nsFrameManager::ClearPlaceholderFrameMap()
{
  if (mPlaceholderMap.ops) {
    PL_DHashTableEnumerate(&mPlaceholderMap, UnregisterPlaceholders, nsnull);
    PL_DHashTableFinish(&mPlaceholderMap);
    mPlaceholderMap.ops = nsnull;
  }
}

//----------------------------------------------------------------------

nsStyleContext*
nsFrameManager::GetUndisplayedContent(nsIContent* aContent)
{
  if (!aContent || !mUndisplayedMap)
    return nsnull;

  nsIContent* parent = aContent->GetParent();
  for (UndisplayedNode* node = mUndisplayedMap->GetFirstNode(parent);
         node; node = node->mNext) {
    if (node->mContent == aContent)
      return node->mStyle;
  }

  return nsnull;
}
  
void
nsFrameManager::SetUndisplayedContent(nsIContent* aContent, 
                                      nsStyleContext* aStyleContext)
{
  NS_PRECONDITION(!aStyleContext->GetPseudo(),
                  "Should only have actual elements here");

#ifdef DEBUG_UNDISPLAYED_MAP
  static int i = 0;
  printf("SetUndisplayedContent(%d): p=%p \n", i++, (void *)aContent);
#endif

  NS_ASSERTION(!GetUndisplayedContent(aContent),
               "Already have an undisplayed context entry for aContent");

  if (! mUndisplayedMap) {
    mUndisplayedMap = new UndisplayedMap;
  }
  if (mUndisplayedMap) {
    nsIContent* parent = aContent->GetParent();
    NS_ASSERTION(parent || (mPresShell && mPresShell->GetDocument() &&
                 mPresShell->GetDocument()->GetRootContent() == aContent),
                 "undisplayed content must have a parent, unless it's the root content");
    mUndisplayedMap->AddNodeFor(parent, aContent, aStyleContext);
  }
}

void
nsFrameManager::ChangeUndisplayedContent(nsIContent* aContent, 
                                         nsStyleContext* aStyleContext)
{
  NS_ASSERTION(mUndisplayedMap, "no existing undisplayed content");
  
#ifdef DEBUG_UNDISPLAYED_MAP
   static int i = 0;
   printf("ChangeUndisplayedContent(%d): p=%p \n", i++, (void *)aContent);
#endif

  for (UndisplayedNode* node = mUndisplayedMap->GetFirstNode(aContent->GetParent());
         node; node = node->mNext) {
    if (node->mContent == aContent) {
      node->mStyle = aStyleContext;
      return;
    }
  }

  NS_NOTREACHED("no existing undisplayed content");
}

void
nsFrameManager::ClearUndisplayedContentIn(nsIContent* aContent,
                                          nsIContent* aParentContent)
{
#ifdef DEBUG_UNDISPLAYED_MAP
  static int i = 0;
  printf("ClearUndisplayedContent(%d): content=%p parent=%p --> ", i++, (void *)aContent, (void*)aParentContent);
#endif
  
  if (mUndisplayedMap) {
    UndisplayedNode* node = mUndisplayedMap->GetFirstNode(aParentContent);
    while (node) {
      if (node->mContent == aContent) {
        mUndisplayedMap->RemoveNodeFor(aParentContent, node);

#ifdef DEBUG_UNDISPLAYED_MAP
        printf( "REMOVED!\n");
#endif
#ifdef DEBUG
        // make sure that there are no more entries for the same content
        nsStyleContext *context = GetUndisplayedContent(aContent);
        NS_ASSERTION(context == nsnull, "Found more undisplayed content data after removal");
#endif
        return;
      }
      node = node->mNext;
    }
  }
}

void
nsFrameManager::ClearAllUndisplayedContentIn(nsIContent* aParentContent)
{
#ifdef DEBUG_UNDISPLAYED_MAP
  static int i = 0;
  printf("ClearAllUndisplayedContentIn(%d): parent=%p \n", i++, (void*)aParentContent);
#endif

  if (mUndisplayedMap) {
    mUndisplayedMap->RemoveNodesFor(aParentContent);
  }

  // Need to look at aParentContent's content list due to XBL insertions.
  // Nodes in aParentContent's content list do not have aParentContent as a
  // parent, but are treated as children of aParentContent. We get access to
  // the content list via GetXBLChildNodesFor and just ignore any nodes we
  // don't care about.
  nsINodeList* list =
    aParentContent->GetOwnerDoc()->BindingManager()->GetXBLChildNodesFor(aParentContent);
  if (list) {
    PRUint32 length;
    list->GetLength(&length);
    for (PRUint32 i = 0; i < length; ++i) {
      nsIContent* child = list->GetNodeAt(i);
      if (child->GetParent() != aParentContent) {
        ClearUndisplayedContentIn(child, child->GetParent());
      }
    }
  }
}

void
nsFrameManager::ClearUndisplayedContentMap()
{
#ifdef DEBUG_UNDISPLAYED_MAP
  static int i = 0;
  printf("ClearUndisplayedContentMap(%d)\n", i++);
#endif

  if (mUndisplayedMap) {
    mUndisplayedMap->Clear();
  }
}

//----------------------------------------------------------------------

nsresult
nsFrameManager::InsertFrames(nsIFrame*       aParentFrame,
                             nsIAtom*        aListName,
                             nsIFrame*       aPrevFrame,
                             nsFrameList&    aFrameList)
{
  NS_PRECONDITION(!aPrevFrame || (!aPrevFrame->GetNextContinuation()
                  || IS_TRUE_OVERFLOW_CONTAINER(aPrevFrame->GetNextContinuation()))
                  && !IS_TRUE_OVERFLOW_CONTAINER(aPrevFrame),
                  "aPrevFrame must be the last continuation in its chain!");

  return aParentFrame->InsertFrames(aListName, aPrevFrame, aFrameList);
}

nsresult
nsFrameManager::RemoveFrame(nsIAtom*        aListName,
                            nsIFrame*       aOldFrame)
{
  PRBool wasDestroyingFrames = mIsDestroyingFrames;
  mIsDestroyingFrames = PR_TRUE;

  // In case the reflow doesn't invalidate anything since it just leaves
  // a gap where the old frame was, we invalidate it here.  (This is
  // reasonably likely to happen when removing a last child in a way
  // that doesn't change the size of the parent.)
  // This has to sure to invalidate the entire overflow rect; this
  // is important in the presence of absolute positioning
  aOldFrame->Invalidate(aOldFrame->GetOverflowRect());

  NS_ASSERTION(!aOldFrame->GetPrevContinuation() ||
               // exception for nsCSSFrameConstructor::RemoveFloatingFirstLetterFrames
               aOldFrame->GetType() == nsGkAtoms::textFrame,
               "Must remove first continuation.");
  NS_ASSERTION(!(aOldFrame->GetStateBits() & NS_FRAME_OUT_OF_FLOW &&
                 GetPlaceholderFrameFor(aOldFrame)),
               "Must call RemoveFrame on placeholder for out-of-flows.");
  nsresult rv = aOldFrame->GetParent()->RemoveFrame(aListName, aOldFrame);

  mIsDestroyingFrames = wasDestroyingFrames;

  return rv;
}

//----------------------------------------------------------------------

void
nsFrameManager::NotifyDestroyingFrame(nsIFrame* aFrame)
{
  nsIContent* content = aFrame->GetContent();
  if (content && content->GetPrimaryFrame() == aFrame) {
    ClearAllUndisplayedContentIn(content);
  }
}

#ifdef NS_DEBUG
static void
DumpContext(nsIFrame* aFrame, nsStyleContext* aContext)
{
  if (aFrame) {
    fputs("frame: ", stdout);
    nsAutoString  name;
    aFrame->GetFrameName(name);
    fputs(NS_LossyConvertUTF16toASCII(name).get(), stdout);
    fprintf(stdout, " (%p)", static_cast<void*>(aFrame));
  }
  if (aContext) {
    fprintf(stdout, " style: %p ", static_cast<void*>(aContext));

    nsIAtom* pseudoTag = aContext->GetPseudo();
    if (pseudoTag) {
      nsAutoString  buffer;
      pseudoTag->ToString(buffer);
      fputs(NS_LossyConvertUTF16toASCII(buffer).get(), stdout);
      fputs(" ", stdout);
    }
    fputs("{}\n", stdout);
  }
}

static void
VerifySameTree(nsStyleContext* aContext1, nsStyleContext* aContext2)
{
  nsStyleContext* top1 = aContext1;
  nsStyleContext* top2 = aContext2;
  nsStyleContext* parent;
  for (;;) {
    parent = top1->GetParent();
    if (!parent)
      break;
    top1 = parent;
  }
  for (;;) {
    parent = top2->GetParent();
    if (!parent)
      break;
    top2 = parent;
  }
  NS_ASSERTION(top1 == top2,
               "Style contexts are not in the same style context tree");
}

static void
VerifyContextParent(nsPresContext* aPresContext, nsIFrame* aFrame, 
                    nsStyleContext* aContext, nsStyleContext* aParentContext)
{
  // get the contexts not provided
  if (!aContext) {
    aContext = aFrame->GetStyleContext();
  }

  if (!aParentContext) {
    // Get the correct parent context from the frame
    //  - if the frame is a placeholder, we get the out of flow frame's context 
    //    as the parent context instead of asking the frame

    // get the parent context from the frame (indirectly)
    nsIFrame* providerFrame = nsnull;
    PRBool providerIsChild;
    aFrame->GetParentStyleContextFrame(aPresContext,
                                       &providerFrame, &providerIsChild);
    if (providerFrame)
      aParentContext = providerFrame->GetStyleContext();
    // aParentContext could still be null
  }

  NS_ASSERTION(aContext, "Failure to get required contexts");
  nsStyleContext* actualParentContext = aContext->GetParent();

  if (aParentContext) {
    if (aParentContext != actualParentContext) {
      DumpContext(aFrame, aContext);
      if (aContext == aParentContext) {
        NS_ERROR("Using parent's style context");
      }
      else {
        NS_ERROR("Wrong parent style context");
        fputs("Wrong parent style context: ", stdout);
        DumpContext(nsnull, actualParentContext);
        fputs("should be using: ", stdout);
        DumpContext(nsnull, aParentContext);
        VerifySameTree(actualParentContext, aParentContext);
        fputs("\n", stdout);
      }
    }
  }
  else {
    if (actualParentContext) {
      NS_ERROR("Have parent context and shouldn't");
      DumpContext(aFrame, aContext);
      fputs("Has parent context: ", stdout);
      DumpContext(nsnull, actualParentContext);
      fputs("Should be null\n\n", stdout);
    }
  }
}

static void
VerifyStyleTree(nsPresContext* aPresContext, nsIFrame* aFrame,
                nsStyleContext* aParentContext)
{
  nsStyleContext*  context = aFrame->GetStyleContext();
  VerifyContextParent(aPresContext, aFrame, context, nsnull);

  PRInt32 listIndex = 0;
  nsIAtom* childList = nsnull;
  nsIFrame* child;

  do {
    child = aFrame->GetFirstChild(childList);
    while (child) {
      if (!(child->GetStateBits() & NS_FRAME_OUT_OF_FLOW)
          || (child->GetStateBits() & NS_FRAME_IS_OVERFLOW_CONTAINER)) {
        // only do frames that don't have placeholders
        if (nsGkAtoms::placeholderFrame == child->GetType()) { 
          // placeholder: first recurse and verify the out of flow frame,
          // then verify the placeholder's context
          nsIFrame* outOfFlowFrame =
            nsPlaceholderFrame::GetRealFrameForPlaceholder(child);

          // recurse to out of flow frame, letting the parent context get resolved
          VerifyStyleTree(aPresContext, outOfFlowFrame, nsnull);

          // verify placeholder using the parent frame's context as
          // parent context
          VerifyContextParent(aPresContext, child, nsnull, nsnull);
        }
        else { // regular frame
          VerifyStyleTree(aPresContext, child, nsnull);
        }
      }
      child = child->GetNextSibling();
    }

    childList = aFrame->GetAdditionalChildListName(listIndex++);
  } while (childList);
  
  // do additional contexts 
  PRInt32 contextIndex = -1;
  while (1) {
    nsStyleContext* extraContext = aFrame->GetAdditionalStyleContext(++contextIndex);
    if (extraContext) {
      VerifyContextParent(aPresContext, aFrame, extraContext, context);
    }
    else {
      break;
    }
  }
}

void
nsFrameManager::DebugVerifyStyleTree(nsIFrame* aFrame)
{
  if (aFrame) {
    nsStyleContext* context = aFrame->GetStyleContext();
    nsStyleContext* parentContext = context->GetParent();
    VerifyStyleTree(GetPresContext(), aFrame, parentContext);
  }
}

#endif // DEBUG

// aContent must be the content for the frame in question, which may be
// :before/:after content
static void
TryStartingTransition(nsPresContext *aPresContext, nsIContent *aContent,
                      nsStyleContext *aOldStyleContext,
                      nsRefPtr<nsStyleContext> *aNewStyleContext /* inout */)
{
  // Notify the transition manager, and if it starts a transition,
  // it will give us back a transition-covering style rule which
  // we'll use to get *another* style context.  We want to ignore
  // any already-running transitions, but cover up any that we're
  // currently starting with their start value so we don't start
  // them again for descendants that inherit that value.
  nsCOMPtr<nsIStyleRule> coverRule = 
    aPresContext->TransitionManager()->StyleContextChanged(
      aContent, aOldStyleContext, *aNewStyleContext);
  if (coverRule) {
    nsCOMArray<nsIStyleRule> rules;
    rules.AppendObject(coverRule);
    *aNewStyleContext = aPresContext->StyleSet()->ResolveStyleForRules(
                     (*aNewStyleContext)->GetParent(),
                     (*aNewStyleContext)->GetPseudo(),
                     (*aNewStyleContext)->GetPseudoType(),
                     (*aNewStyleContext)->GetRuleNode(),
                     rules);
  }
}

nsresult
nsFrameManager::ReParentStyleContext(nsIFrame* aFrame)
{
  if (nsGkAtoms::placeholderFrame == aFrame->GetType()) {
    // Also reparent the out-of-flow
    nsIFrame* outOfFlow =
      nsPlaceholderFrame::GetRealFrameForPlaceholder(aFrame);
    NS_ASSERTION(outOfFlow, "no out-of-flow frame");

    ReParentStyleContext(outOfFlow);
  }

  // DO NOT verify the style tree before reparenting.  The frame
  // tree has already been changed, so this check would just fail.
  nsStyleContext* oldContext = aFrame->GetStyleContext();
  // XXXbz can oldContext really ever be null?
  if (oldContext) {
    nsPresContext *presContext = GetPresContext();
    nsRefPtr<nsStyleContext> newContext;
    nsIFrame* providerFrame = nsnull;
    PRBool providerIsChild = PR_FALSE;
    nsIFrame* providerChild = nsnull;
    aFrame->GetParentStyleContextFrame(presContext, &providerFrame,
                                       &providerIsChild);
    nsStyleContext* newParentContext = nsnull;
    if (providerIsChild) {
      ReParentStyleContext(providerFrame);
      newParentContext = providerFrame->GetStyleContext();
      providerChild = providerFrame;
    } else if (providerFrame) {
      newParentContext = providerFrame->GetStyleContext();
    } else {
      NS_NOTREACHED("Reparenting something that has no usable parent? "
                    "Shouldn't happen!");
    }
    // XXX need to do something here to produce the correct style context for
    // an IB split whose first inline part is inside a first-line frame.
    // Currently the first IB anonymous block's style context takes the first
    // part's style context as parent, which is wrong since first-line style
    // should not apply to the anonymous block.

#ifdef DEBUG
    {
      // Check that our assumption that continuations of the same
      // pseudo-type and with the same style context parent have the
      // same style context is valid before the reresolution.  (We need
      // to check the pseudo-type and style context parent because of
      // :first-letter and :first-line, where we create styled and
      // unstyled letter/line frames distinguished by pseudo-type, and
      // then need to distinguish their descendants based on having
      // different parents.)
      nsIFrame *nextContinuation = aFrame->GetNextContinuation();
      if (nextContinuation) {
        nsStyleContext *nextContinuationContext =
          nextContinuation->GetStyleContext();
        NS_ASSERTION(oldContext == nextContinuationContext ||
                     oldContext->GetPseudo() !=
                       nextContinuationContext->GetPseudo() ||
                     oldContext->GetParent() !=
                       nextContinuationContext->GetParent(),
                     "continuations should have the same style context");
      }
    }
#endif

    nsIFrame *prevContinuation = aFrame->GetPrevContinuation();
    nsStyleContext *prevContinuationContext;
    PRBool copyFromContinuation =
      prevContinuation &&
      (prevContinuationContext = prevContinuation->GetStyleContext())
        ->GetPseudo() == oldContext->GetPseudo() &&
       prevContinuationContext->GetParent() == newParentContext;
    if (copyFromContinuation) {
      // Just use the style context from the frame's previous
      // continuation (see assertion about aFrame->GetNextContinuation()
      // above, which we would have previously hit for aFrame's previous
      // continuation).
      newContext = prevContinuationContext;
    } else {
      newContext = mStyleSet->ReParentStyleContext(presContext, oldContext,
                                                   newParentContext);
    }

    if (newContext) {
      if (newContext != oldContext) {
        // We probably don't want to initiate transitions from
        // ReParentStyleContext, since we call it during frame
        // construction rather than in response to dynamic changes.
        // Also see the comment at the start of
        // nsTransitionManager::ConsiderStartingTransition.
#if 0
        if (!copyFromContinuation) {
          TryStartingTransition(presContext, aFrame->GetContent(),
                                oldContext, &newContext);
        }
#endif

        // Make sure to call CalcStyleDifference so that the new context ends
        // up resolving all the structs the old context resolved.
        nsChangeHint styleChange = oldContext->CalcStyleDifference(newContext);
        // The style change is always 0 because we have the same rulenode and
        // CalcStyleDifference optimizes us away.  That's OK, though:
        // reparenting should never trigger a frame reconstruct, and whenever
        // it's happening we already plan to reflow and repaint the frames.
        NS_ASSERTION(!(styleChange & nsChangeHint_ReconstructFrame),
                     "Our frame tree is likely to be bogus!");
        
        PRInt32 listIndex = 0;
        nsIAtom* childList = nsnull;
        nsIFrame* child;
          
        aFrame->SetStyleContext(newContext);

        do {
          child = aFrame->GetFirstChild(childList);
          while (child) {
            // only do frames that don't have placeholders
            if ((!(child->GetStateBits() & NS_FRAME_OUT_OF_FLOW) ||
                 (child->GetStateBits() & NS_FRAME_IS_OVERFLOW_CONTAINER)) &&
                child != providerChild) {
#ifdef DEBUG
              if (nsGkAtoms::placeholderFrame == child->GetType()) {
                nsIFrame* outOfFlowFrame =
                  nsPlaceholderFrame::GetRealFrameForPlaceholder(child);
                NS_ASSERTION(outOfFlowFrame, "no out-of-flow frame");

                NS_ASSERTION(outOfFlowFrame != providerChild,
                             "Out of flow provider?");
              }
#endif

              ReParentStyleContext(child);
            }

            child = child->GetNextSibling();
          }

          childList = aFrame->GetAdditionalChildListName(listIndex++);
        } while (childList);

        // If this frame is part of an IB split, then the style context of
        // the next part of the split might be a child of our style context.
        // Reparent its style context just in case one of our ancestors
        // (split or not) hasn't done so already). It's not a problem to
        // reparent the same frame twice because the "if (newContext !=
        // oldContext)" check will prevent us from redoing work.
        if ((aFrame->GetStateBits() & NS_FRAME_IS_SPECIAL) &&
            !aFrame->GetPrevContinuation()) {
          nsIFrame* sib = static_cast<nsIFrame*>(aFrame->GetProperty(nsGkAtoms::IBSplitSpecialSibling));
          if (sib) {
            ReParentStyleContext(sib);
          }
        }

        // do additional contexts 
        PRInt32 contextIndex = -1;
        while (1) {
          nsStyleContext* oldExtraContext =
            aFrame->GetAdditionalStyleContext(++contextIndex);
          if (oldExtraContext) {
            nsRefPtr<nsStyleContext> newExtraContext;
            newExtraContext = mStyleSet->ReParentStyleContext(presContext,
                                                              oldExtraContext,
                                                              newContext);
            if (newExtraContext) {
              if (newExtraContext != oldExtraContext) {
                // Make sure to call CalcStyleDifference so that the new
                // context ends up resolving all the structs the old context
                // resolved.
                styleChange =
                  oldExtraContext->CalcStyleDifference(newExtraContext);
                // The style change is always 0 because we have the same
                // rulenode and CalcStyleDifference optimizes us away.  That's
                // OK, though: reparenting should never trigger a frame
                // reconstruct, and whenever it's happening we already plan to
                // reflow and repaint the frames.
                NS_ASSERTION(!(styleChange & nsChangeHint_ReconstructFrame),
                             "Our frame tree is likely to be bogus!");
              }
              
              aFrame->SetAdditionalStyleContext(contextIndex, newExtraContext);
            }
          }
          else {
            break;
          }
        }
#ifdef DEBUG
        VerifyStyleTree(GetPresContext(), aFrame, newParentContext);
#endif
      }
    }
  }
  return NS_OK;
}

static nsChangeHint
CaptureChange(nsStyleContext* aOldContext, nsStyleContext* aNewContext,
              nsIFrame* aFrame, nsIContent* aContent,
              nsStyleChangeList* aChangeList, nsChangeHint aMinChange,
              nsChangeHint aChangeToAssume)
{
  nsChangeHint ourChange = aOldContext->CalcStyleDifference(aNewContext);
  NS_ASSERTION(!(ourChange & nsChangeHint_ReflowFrame) ||
               (ourChange & nsChangeHint_NeedReflow),
               "Reflow hint bits set without actually asking for a reflow");

  NS_UpdateHint(ourChange, aChangeToAssume);
  if (NS_UpdateHint(aMinChange, ourChange)) {
    aChangeList->AppendChange(aFrame, aContent, ourChange);
  }
  return aMinChange;
}

/**
 * Recompute style for aFrame and accumulate changes into aChangeList
 * given that aMinChange is already accumulated for an ancestor.
 * aParentContent is the content node used to resolve the parent style
 * context.  This means that, for pseudo-elements, it is the content
 * that should be used for selector matching (rather than the fake
 * content node attached to the frame).
 */
nsChangeHint
nsFrameManager::ReResolveStyleContext(nsPresContext     *aPresContext,
                                      nsIFrame          *aFrame,
                                      nsIContent        *aParentContent,
                                      nsStyleChangeList *aChangeList, 
                                      nsChangeHint       aMinChange,
                                      PRBool             aFireAccessibilityEvents)
{
  if (!NS_IsHintSubset(nsChangeHint_NeedDirtyReflow, aMinChange)) {
    // If aMinChange doesn't include nsChangeHint_NeedDirtyReflow, clear out
    // all the reflow change bits from it, so that we'll make sure to append a
    // change to the list for ourselves if we need a reflow.  We need this
    // because the parent may or may not actually end up reflowing us
    // otherwise.
    aMinChange = NS_SubtractHint(aMinChange, nsChangeHint_ReflowFrame);
  } else if (!NS_IsHintSubset(nsChangeHint_ClearDescendantIntrinsics,
                              aMinChange)) {
    // If aMinChange doesn't include nsChangeHint_ClearDescendantIntrinsics,
    // clear out the nsChangeHint_ClearAncestorIntrinsics flag, since it's
    // possible that we had some random ancestor that cleared ancestor
    // intrinsic widths, but we still need to clear intrinsic widths on frames
    // that are our ancestors but its descendants.
    aMinChange =
      NS_SubtractHint(aMinChange, nsChangeHint_ClearAncestorIntrinsics);
  }
  
  // It would be nice if we could make stronger assertions here; they
  // would let us simplify the ?: expressions below setting |content|
  // and |pseudoContent| in sensible ways as well as making what
  // |localContent|, |content|, and |pseudoContent| mean make more
  // sense.  However, we can't, because of frame trees like the one in
  // https://bugzilla.mozilla.org/show_bug.cgi?id=472353#c14 .  Once we
  // fix bug 242277 we should be able to make this make more sense.
  NS_ASSERTION(aFrame->GetContent() || !aParentContent ||
               !aParentContent->GetParent(),
               "frame must have content (unless at the top of the tree)");
  // XXXldb get new context from prev-in-flow if possible, to avoid
  // duplication.  (Or should we just let |GetContext| handle that?)
  // Getting the hint would be nice too, but that's harder.

  // XXXbryner we may be able to avoid some of the refcounting goop here.
  // We do need a reference to oldContext for the lifetime of this function, and it's possible
  // that the frame has the last reference to it, so AddRef it here.

  nsChangeHint assumeDifferenceHint = NS_STYLE_HINT_NONE;
  // XXXbz oldContext should just be an nsRefPtr
  nsStyleContext* oldContext = aFrame->GetStyleContext();
  nsStyleSet* styleSet = aPresContext->StyleSet();
#ifdef ACCESSIBILITY
  PRBool isVisible = aFrame->GetStyleVisibility()->IsVisible();
#endif

  // XXXbz the nsIFrame constructor takes an nsStyleContext, so how
  // could oldContext be null?
  if (oldContext) {
    oldContext->AddRef();
    nsIAtom* const pseudoTag = oldContext->GetPseudo();
    const nsCSSPseudoElements::Type pseudoType = oldContext->GetPseudoType();
    nsIContent* localContent = aFrame->GetContent();
    // |content| is the node that we used for rule matching of
    // normal elements (not pseudo-elements) and for which we generate
    // framechange hints if we need them.
    // XXXldb Why does it make sense to use aParentContent?  (See
    // comment above assertion at start of function.)
    nsIContent* content = localContent ? localContent : aParentContent;

    nsStyleContext* parentContext;
    nsIFrame* resolvedChild = nsnull;
    // Get the frame providing the parent style context.  If it is a
    // child, then resolve the provider first.
    nsIFrame* providerFrame = nsnull;
    PRBool providerIsChild = PR_FALSE;
    aFrame->GetParentStyleContextFrame(aPresContext,
                                       &providerFrame, &providerIsChild); 
    if (!providerIsChild) {
      if (providerFrame)
        parentContext = providerFrame->GetStyleContext();
      else
        parentContext = nsnull;
    }
    else {
      // resolve the provider here (before aFrame below).

      // assumeDifferenceHint forces the parent's change to be also
      // applied to this frame, no matter what
      // nsStyleContext::CalcStyleDifference says. CalcStyleDifference
      // can't be trusted because it assumes any changes to the parent
      // style context provider will be automatically propagated to
      // the frame(s) with child style contexts.

      // Accessibility: we don't need to fire a11y events for child provider
      // frame because it is visible or hidden withitn this frame.
      assumeDifferenceHint = ReResolveStyleContext(aPresContext, providerFrame,
                                                   aParentContent, aChangeList,
                                                   aMinChange, PR_FALSE);

      // The provider's new context becomes the parent context of
      // aFrame's context.
      parentContext = providerFrame->GetStyleContext();
      // Set |resolvedChild| so we don't bother resolving the
      // provider again.
      resolvedChild = providerFrame;
    }

#ifdef DEBUG
    {
      // Check that our assumption that continuations of the same
      // pseudo-type and with the same style context parent have the
      // same style context is valid before the reresolution.  (We need
      // to check the pseudo-type and style context parent because of
      // :first-letter and :first-line, where we create styled and
      // unstyled letter/line frames distinguished by pseudo-type, and
      // then need to distinguish their descendants based on having
      // different parents.)
      nsIFrame *nextContinuation = aFrame->GetNextContinuation();
      if (nextContinuation) {
        nsStyleContext *nextContinuationContext =
          nextContinuation->GetStyleContext();
        NS_ASSERTION(oldContext == nextContinuationContext ||
                     oldContext->GetPseudo() !=
                       nextContinuationContext->GetPseudo() ||
                     oldContext->GetParent() !=
                       nextContinuationContext->GetParent(),
                     "continuations should have the same style context");
      }
    }
#endif

    // do primary context
    nsRefPtr<nsStyleContext> newContext;
    nsIFrame *prevContinuation = aFrame->GetPrevContinuation();
    nsStyleContext *prevContinuationContext;
    PRBool copyFromContinuation =
      prevContinuation &&
      (prevContinuationContext = prevContinuation->GetStyleContext())
        ->GetPseudo() == oldContext->GetPseudo() &&
       prevContinuationContext->GetParent() == parentContext;
    if (copyFromContinuation) {
      // Just use the style context from the frame's previous
      // continuation (see assertion about aFrame->GetNextContinuation()
      // above, which we would have previously hit for aFrame's previous
      // continuation).
      newContext = prevContinuationContext;
    }
    else if (pseudoTag == nsCSSAnonBoxes::mozNonElement) {
      NS_ASSERTION(localContent,
                   "non pseudo-element frame without content node");
      newContext = styleSet->ResolveStyleForNonElement(parentContext);
    }
    else if (pseudoTag) {
      // XXXldb This choice of pseudoContent seems incorrect for anon
      // boxes and perhaps other cases.
      // See also the comment above the assertion at the start of this
      // function.
      nsIContent* pseudoContent =
          aParentContent ? aParentContent : localContent;
      if (pseudoTag == nsCSSPseudoElements::before ||
          pseudoTag == nsCSSPseudoElements::after) {
        // XXX what other pseudos do we need to treat like this?
        newContext = styleSet->ProbePseudoElementStyle(pseudoContent,
                                                       pseudoType,
                                                       parentContext);
        if (!newContext) {
          // This pseudo should no longer exist; gotta reframe
          NS_UpdateHint(aMinChange, nsChangeHint_ReconstructFrame);
          aChangeList->AppendChange(aFrame, pseudoContent,
                                    nsChangeHint_ReconstructFrame);
          // We're reframing anyway; just keep the same context
          newContext = oldContext;
        }
      } else if (pseudoType == nsCSSPseudoElements::ePseudo_AnonBox) {
        newContext = styleSet->ResolveAnonymousBoxStyle(pseudoTag,
                                                        parentContext);
      } else {
        // Don't expect XUL tree stuff here, since it needs a comparator and
        // all.
        NS_ASSERTION(pseudoType <
                       nsCSSPseudoElements::ePseudo_PseudoElementCount,
                     "Unexpected pseudo type");
        if (pseudoTag == nsCSSPseudoElements::firstLetter) {
          NS_ASSERTION(aFrame->GetType() == nsGkAtoms::letterFrame, 
                       "firstLetter pseudoTag without a nsFirstLetterFrame");
          nsBlockFrame* block = nsBlockFrame::GetNearestAncestorBlock(aFrame);
          pseudoContent = block->GetContent();
        }
        newContext = styleSet->ResolvePseudoElementStyle(pseudoContent,
                                                         pseudoType,
                                                         parentContext);
      }
    }
    else {
      NS_ASSERTION(localContent,
                   "non pseudo-element frame without content node");
      newContext = styleSet->ResolveStyleFor(content, parentContext);
    }
    NS_ASSERTION(newContext, "failed to get new style context");
    if (newContext) {
      if (!parentContext) {
        if (oldContext->GetRuleNode() == newContext->GetRuleNode()) {
          // We're the root of the style context tree and the new style
          // context returned has the same rule node.  This means that
          // we can use FindChildWithRules to keep a lot of the old
          // style contexts around.  However, we need to start from the
          // same root.
          newContext = oldContext;
        }
      }

      if (newContext != oldContext) {
        if (!copyFromContinuation) {
          TryStartingTransition(aPresContext, aFrame->GetContent(),
                                oldContext, &newContext);
        }

        aMinChange = CaptureChange(oldContext, newContext, aFrame,
                                   content, aChangeList, aMinChange,
                                   assumeDifferenceHint);
        if (!(aMinChange & nsChangeHint_ReconstructFrame)) {
          // if frame gets regenerated, let it keep old context
          aFrame->SetStyleContext(newContext);
        }
      }
      oldContext->Release();
    }
    else {
      NS_ERROR("resolve style context failed");
      newContext = oldContext;  // new context failed, recover...
    }

    // do additional contexts 
    PRInt32 contextIndex = -1;
    while (1 == 1) {
      nsStyleContext* oldExtraContext = nsnull;
      oldExtraContext = aFrame->GetAdditionalStyleContext(++contextIndex);
      if (oldExtraContext) {
        nsRefPtr<nsStyleContext> newExtraContext;
        nsIAtom* const extraPseudoTag = oldExtraContext->GetPseudo();
        const nsCSSPseudoElements::Type extraPseudoType =
          oldExtraContext->GetPseudoType();
        NS_ASSERTION(extraPseudoTag &&
                     extraPseudoTag != nsCSSAnonBoxes::mozNonElement,
                     "extra style context is not pseudo element");
        if (extraPseudoType == nsCSSPseudoElements::ePseudo_AnonBox) {
          newExtraContext = styleSet->ResolveAnonymousBoxStyle(extraPseudoTag,
                                                               newContext);
        }
        else {
          // Don't expect XUL tree stuff here, since it needs a comparator and
          // all.
          NS_ASSERTION(extraPseudoType <
                         nsCSSPseudoElements::ePseudo_PseudoElementCount,
                       "Unexpected type");
          newExtraContext = styleSet->ResolvePseudoElementStyle(content,
                                                                extraPseudoType,
                                                                newContext);
        }
        if (newExtraContext) {
          if (oldExtraContext != newExtraContext) {
            aMinChange = CaptureChange(oldExtraContext, newExtraContext,
                                       aFrame, content, aChangeList,
                                       aMinChange, assumeDifferenceHint);
            if (!(aMinChange & nsChangeHint_ReconstructFrame)) {
              aFrame->SetAdditionalStyleContext(contextIndex, newExtraContext);
            }
          }
        }
      }
      else {
        break;
      }
    }

    // now look for undisplayed child content and pseudos

    // When the root element is display:none, we still construct *some*
    // frames that have the root element as their mContent, down to the
    // DocElementContainingBlock.
    PRBool checkUndisplayed;
    nsIContent *undisplayedParent;
    if (pseudoTag) {
      checkUndisplayed = aFrame == mPresShell->FrameConstructor()->
                                     GetDocElementContainingBlock();
      undisplayedParent = nsnull;
    } else {
      checkUndisplayed = !!localContent;
      undisplayedParent = localContent;
    }
    if (checkUndisplayed && mUndisplayedMap) {
      for (UndisplayedNode* undisplayed =
                              mUndisplayedMap->GetFirstNode(undisplayedParent);
           undisplayed; undisplayed = undisplayed->mNext) {
        NS_ASSERTION(undisplayedParent ||
                     undisplayed->mContent ==
                       mPresShell->GetDocument()->GetRootContent(),
                     "undisplayed node child of null must be root");
        NS_ASSERTION(!undisplayed->mStyle->GetPseudo(),
                     "Shouldn't have random pseudo style contexts in the "
                     "undisplayed map");
        nsRefPtr<nsStyleContext> undisplayedContext =
          styleSet->ResolveStyleFor(undisplayed->mContent, newContext);
        if (undisplayedContext) {
          const nsStyleDisplay* display = undisplayedContext->GetStyleDisplay();
          if (display->mDisplay != NS_STYLE_DISPLAY_NONE) {
            aChangeList->AppendChange(nsnull,
                                      undisplayed->mContent
                                      ? static_cast<nsIContent*>
                                                   (undisplayed->mContent)
                                      : localContent, 
                                      NS_STYLE_HINT_FRAMECHANGE);
            // The node should be removed from the undisplayed map when
            // we reframe it.
          } else {
            // update the undisplayed node with the new context
            undisplayed->mStyle = undisplayedContext;
          }
        }
      }
    }

    if (!(aMinChange & nsChangeHint_ReconstructFrame)) {
      // Make sure not to do this for pseudo-frames -- those can't have :before
      // or :after content.  Neither can non-elements or leaf frames.
      if (!pseudoTag && localContent &&
          localContent->IsNodeOfType(nsINode::eELEMENT) &&
          !aFrame->IsLeaf()) {
        // Check for a new :before pseudo and an existing :before
        // frame, but only if the frame is the first continuation.
        nsIFrame* prevContinuation = aFrame->GetPrevContinuation();
        if (!prevContinuation) {
          // Checking for a :before frame is cheaper than getting the
          // :before style context.
          if (!nsLayoutUtils::GetBeforeFrame(aFrame) &&
              nsLayoutUtils::HasPseudoStyle(localContent, newContext,
                                            nsCSSPseudoElements::ePseudo_before,
                                            aPresContext)) {
            // Have to create the new :before frame
            NS_UpdateHint(aMinChange, nsChangeHint_ReconstructFrame);
            aChangeList->AppendChange(aFrame, content,
                                      nsChangeHint_ReconstructFrame);
          }
        }
      }
    }

    
    if (!(aMinChange & nsChangeHint_ReconstructFrame)) {
      // Make sure not to do this for pseudo-frames -- those can't have :before
      // or :after content.  Neither can non-elements or leaf frames.
      if (!pseudoTag && localContent &&
          localContent->IsNodeOfType(nsINode::eELEMENT) &&
          !aFrame->IsLeaf()) {
        // Check for new :after content, but only if the frame is the
        // last continuation.
        nsIFrame* nextContinuation = aFrame->GetNextContinuation();

        if (!nextContinuation) {
          // Getting the :after frame is more expensive than getting the pseudo
          // context, so get the pseudo context first.
          if (nsLayoutUtils::HasPseudoStyle(localContent, newContext,
                                            nsCSSPseudoElements::ePseudo_after,
                                            aPresContext) &&
              !nsLayoutUtils::GetAfterFrame(aFrame)) {
            // have to create the new :after frame
            NS_UpdateHint(aMinChange, nsChangeHint_ReconstructFrame);
            aChangeList->AppendChange(aFrame, content,
                                      nsChangeHint_ReconstructFrame);
          }
        }      
      }
    }

    PRBool fireAccessibilityEvents = aFireAccessibilityEvents;
#ifdef ACCESSIBILITY
    if (fireAccessibilityEvents && mPresShell->IsAccessibilityActive() &&
        aFrame->GetStyleVisibility()->IsVisible() != isVisible &&
        !aFrame->GetPrevContinuation()) {
      // A significant enough change occured that this part
      // of the accessible tree is no longer valid. Fire event for primary
      // frames only and if it wasn't fired for parent frame already.

      // XXX: bug 355521. Visibility does not affect descendents with
      // visibility set. Work on a separate, accurate mechanism for dealing with
      // visibility changes.
      nsCOMPtr<nsIAccessibilityService> accService = 
        do_GetService("@mozilla.org/accessibilityService;1");
      if (accService) {
        PRUint32 changeType = isVisible ?
          nsIAccessibilityService::FRAME_HIDE :
          nsIAccessibilityService::FRAME_SHOW;

        accService->InvalidateSubtreeFor(mPresShell, aFrame->GetContent(),
                                         changeType);
        fireAccessibilityEvents = PR_FALSE;
      }
    }
#endif

    if (!(aMinChange & nsChangeHint_ReconstructFrame)) {
      
      // There is no need to waste time crawling into a frame's children on a frame change.
      // The act of reconstructing frames will force new style contexts to be resolved on all
      // of this frame's descendants anyway, so we want to avoid wasting time processing
      // style contexts that we're just going to throw away anyway. - dwh

      // now do children
      PRInt32 listIndex = 0;
      nsIAtom* childList = nsnull;

      do {
        nsIFrame* child = aFrame->GetFirstChild(childList);
        while (child) {
          if (!(child->GetStateBits() & NS_FRAME_OUT_OF_FLOW)
              || (child->GetStateBits() & NS_FRAME_IS_OVERFLOW_CONTAINER)) {
            // only do frames that don't have placeholders
            if (nsGkAtoms::placeholderFrame == child->GetType()) { // placeholder
              // get out of flow frame and recur there
              nsIFrame* outOfFlowFrame =
                nsPlaceholderFrame::GetRealFrameForPlaceholder(child);
              NS_ASSERTION(outOfFlowFrame, "no out-of-flow frame");
              NS_ASSERTION(outOfFlowFrame != resolvedChild,
                           "out-of-flow frame not a true descendant");

              // Note that the out-of-flow may not be a geometric descendant of
              // the frame where we started the reresolve.  Therefore, even if
              // aMinChange already includes nsChangeHint_ReflowFrame we don't
              // want to pass that on to the out-of-flow reresolve, since that
              // can lead to the out-of-flow not getting reflowed when it should
              // be (eg a reresolve starting at <body> that involves reflowing
              // the <body> would miss reflowing fixed-pos nodes that also need
              // reflow).  In the cases when the out-of-flow _is_ a geometric
              // descendant of a frame we already have a reflow hint for,
              // reflow coalescing should keep us from doing the work twice.

              // |nsFrame::GetParentStyleContextFrame| checks being out
              // of flow so that this works correctly.
              ReResolveStyleContext(aPresContext, outOfFlowFrame,
                                    content, aChangeList,
                                    NS_SubtractHint(aMinChange,
                                                    nsChangeHint_ReflowFrame),
                                    fireAccessibilityEvents);

              // reresolve placeholder's context under the same parent
              // as the out-of-flow frame
              ReResolveStyleContext(aPresContext, child, content,
                                    aChangeList, aMinChange,
                                    fireAccessibilityEvents);
            }
            else {  // regular child frame
              if (child != resolvedChild) {
                ReResolveStyleContext(aPresContext, child, content,
                                      aChangeList, aMinChange,
                                      fireAccessibilityEvents);
              } else {
                NOISY_TRACE_FRAME("child frame already resolved as descendant, skipping",aFrame);
              }
            }
          }
          child = child->GetNextSibling();
        }

        childList = aFrame->GetAdditionalChildListName(listIndex++);
      } while (childList);
      // XXX need to do overflow frames???
    }

  }

  return aMinChange;
}

void
nsFrameManager::ComputeStyleChangeFor(nsIFrame          *aFrame, 
                                      nsStyleChangeList *aChangeList,
                                      nsChangeHint       aMinChange)
{
  if (aMinChange) {
    aChangeList->AppendChange(aFrame, aFrame->GetContent(), aMinChange);
  }

  nsChangeHint topLevelChange = aMinChange;

  nsIFrame* frame = aFrame;
  nsIFrame* frame2 = aFrame;

  NS_ASSERTION(!frame->GetPrevContinuation(), "must start with the first in flow");

  // We want to start with this frame and walk all its next-in-flows,
  // as well as all its special siblings and their next-in-flows,
  // reresolving style on all the frames we encounter in this walk.

  nsPropertyTable *propTable = GetPresContext()->PropertyTable();

  do {
    // Outer loop over special siblings
    do {
      // Inner loop over next-in-flows of the current frame
      nsChangeHint frameChange =
        ReResolveStyleContext(GetPresContext(), frame, nsnull,
                              aChangeList, topLevelChange, PR_TRUE);
      NS_UpdateHint(topLevelChange, frameChange);

      if (topLevelChange & nsChangeHint_ReconstructFrame) {
        // If it's going to cause a framechange, then don't bother
        // with the continuations or special siblings since they'll be
        // clobbered by the frame reconstruct anyway.
        NS_ASSERTION(!frame->GetPrevContinuation(),
                     "continuing frame had more severe impact than first-in-flow");
        return;
      }

      frame = frame->GetNextContinuation();
    } while (frame);

    // Might we have special siblings?
    if (!(frame2->GetStateBits() & NS_FRAME_IS_SPECIAL)) {
      // nothing more to do here
      return;
    }
    
    frame2 = static_cast<nsIFrame*>
                        (propTable->GetProperty(frame2, nsGkAtoms::IBSplitSpecialSibling));
    frame = frame2;
  } while (frame2);
}


nsReStyleHint
nsFrameManager::HasAttributeDependentStyle(nsIContent *aContent,
                                           nsIAtom *aAttribute,
                                           PRInt32 aModType,
                                           PRBool aAttrHasChanged)
{
  nsReStyleHint hint = mStyleSet->HasAttributeDependentStyle(GetPresContext(),
                                                             aContent,
                                                             aAttribute,
                                                             aModType,
                                                             aAttrHasChanged);

  if (aAttrHasChanged && aAttribute == nsGkAtoms::style) {
    // Perhaps should check that it's XUL, SVG, (or HTML) namespace, but
    // it doesn't really matter.  Or we could even let
    // HTMLCSSStyleSheetImpl::HasAttributeDependentStyle handle it.
    hint = nsReStyleHint(hint | eReStyle_Self);
  }

  return hint;
}

// Capture state for a given frame.
// Accept a content id here, in some cases we may not have content (scroll position)
void
nsFrameManager::CaptureFrameStateFor(nsIFrame* aFrame,
                                     nsILayoutHistoryState* aState,
                                     nsIStatefulFrame::SpecialStateID aID)
{
  if (!aFrame || !aState) {
    NS_WARNING("null frame, or state");
    return;
  }

  // Only capture state for stateful frames
  nsIStatefulFrame* statefulFrame = do_QueryFrame(aFrame);
  if (!statefulFrame) {
    return;
  }

  // Capture the state, exit early if we get null (nothing to save)
  nsAutoPtr<nsPresState> frameState;
  nsresult rv = statefulFrame->SaveState(aID, getter_Transfers(frameState));
  if (!frameState) {
    return;
  }

  // Generate the hash key to store the state under
  // Exit early if we get empty key
  nsCAutoString stateKey;
  nsIContent* content = aFrame->GetContent();
  nsIDocument* doc = content ? content->GetCurrentDoc() : nsnull;
  rv = nsContentUtils::GenerateStateKey(content, doc, aID, stateKey);
  if(NS_FAILED(rv) || stateKey.IsEmpty()) {
    return;
  }

  // Store the state
  rv = aState->AddState(stateKey, frameState);
  if (NS_SUCCEEDED(rv)) {
    // aState owns frameState now.
    frameState.forget();
  }
}

void
nsFrameManager::CaptureFrameState(nsIFrame* aFrame,
                                  nsILayoutHistoryState* aState)
{
  NS_PRECONDITION(nsnull != aFrame && nsnull != aState, "null parameters passed in");

  CaptureFrameStateFor(aFrame, aState);

  // Now capture state recursively for the frame hierarchy rooted at aFrame
  nsIAtom*  childListName = nsnull;
  PRInt32   childListIndex = 0;
  do {    
    nsIFrame* childFrame = aFrame->GetFirstChild(childListName);
    while (childFrame) {             
      CaptureFrameState(childFrame, aState);
      // Get the next sibling child frame
      childFrame = childFrame->GetNextSibling();
    }
    childListName = aFrame->GetAdditionalChildListName(childListIndex++);
  } while (childListName);
}

// Restore state for a given frame.
// Accept a content id here, in some cases we may not have content (scroll position)
void
nsFrameManager::RestoreFrameStateFor(nsIFrame* aFrame,
                                     nsILayoutHistoryState* aState,
                                     nsIStatefulFrame::SpecialStateID aID)
{
  if (!aFrame || !aState) {
    NS_WARNING("null frame or state");
    return;
  }

  // Only restore state for stateful frames
  nsIStatefulFrame* statefulFrame = do_QueryFrame(aFrame);
  if (!statefulFrame) {
    return;
  }

  // Generate the hash key the state was stored under
  // Exit early if we get empty key
  nsIContent* content = aFrame->GetContent();
  // If we don't have content, we can't generate a hash
  // key and there's probably no state information for us.
  if (!content) {
    return;
  }

  nsCAutoString stateKey;
  nsIDocument* doc = content->GetCurrentDoc();
  nsresult rv = nsContentUtils::GenerateStateKey(content, doc, aID, stateKey);
  if (NS_FAILED(rv) || stateKey.IsEmpty()) {
    return;
  }

  // Get the state from the hash
  nsPresState *frameState;
  rv = aState->GetState(stateKey, &frameState);
  if (!frameState) {
    return;
  }

  // Restore it
  rv = statefulFrame->RestoreState(frameState);
  if (NS_FAILED(rv)) {
    return;
  }

  // If we restore ok, remove the state from the state table
  aState->RemoveState(stateKey);
}

void
nsFrameManager::RestoreFrameState(nsIFrame* aFrame,
                                  nsILayoutHistoryState* aState)
{
  NS_PRECONDITION(nsnull != aFrame && nsnull != aState, "null parameters passed in");
  
  RestoreFrameStateFor(aFrame, aState);

  // Now restore state recursively for the frame hierarchy rooted at aFrame
  nsIAtom*  childListName = nsnull;
  PRInt32   childListIndex = 0;
  do {    
    nsIFrame* childFrame = aFrame->GetFirstChild(childListName);
    while (childFrame) {
      RestoreFrameState(childFrame, aState);
      // Get the next sibling child frame
      childFrame = childFrame->GetNextSibling();
    }
    childListName = aFrame->GetAdditionalChildListName(childListIndex++);
  } while (childListName);
}

//----------------------------------------------------------------------

static PLHashNumber
HashKey(void* key)
{
  return NS_PTR_TO_INT32(key);
}

static PRIntn
CompareKeys(void* key1, void* key2)
{
  return key1 == key2;
}

//----------------------------------------------------------------------

nsFrameManagerBase::UndisplayedMap::UndisplayedMap(PRUint32 aNumBuckets)
{
  MOZ_COUNT_CTOR(nsFrameManagerBase::UndisplayedMap);
  mTable = PL_NewHashTable(aNumBuckets, (PLHashFunction)HashKey,
                           (PLHashComparator)CompareKeys,
                           (PLHashComparator)nsnull,
                           nsnull, nsnull);
  mLastLookup = nsnull;
}

nsFrameManagerBase::UndisplayedMap::~UndisplayedMap(void)
{
  MOZ_COUNT_DTOR(nsFrameManagerBase::UndisplayedMap);
  Clear();
  PL_HashTableDestroy(mTable);
}

PLHashEntry**  
nsFrameManagerBase::UndisplayedMap::GetEntryFor(nsIContent* aParentContent)
{
  if (mLastLookup && (aParentContent == (*mLastLookup)->key)) {
    return mLastLookup;
  }
  PLHashNumber hashCode = NS_PTR_TO_INT32(aParentContent);
  PLHashEntry** entry = PL_HashTableRawLookup(mTable, hashCode, aParentContent);
  if (*entry) {
    mLastLookup = entry;
  }
  return entry;
}

UndisplayedNode* 
nsFrameManagerBase::UndisplayedMap::GetFirstNode(nsIContent* aParentContent)
{
  PLHashEntry** entry = GetEntryFor(aParentContent);
  if (*entry) {
    return (UndisplayedNode*)((*entry)->value);
  }
  return nsnull;
}

void
nsFrameManagerBase::UndisplayedMap::AppendNodeFor(UndisplayedNode* aNode,
                                                  nsIContent* aParentContent)
{
  PLHashEntry** entry = GetEntryFor(aParentContent);
  if (*entry) {
    UndisplayedNode*  node = (UndisplayedNode*)((*entry)->value);
    while (node->mNext) {
      if (node->mContent == aNode->mContent) {
        // We actually need to check this in optimized builds because
        // there are some callers that do this.  See bug 118014, bug
        // 136704, etc.
        NS_NOTREACHED("node in map twice");
        delete aNode;
        return;
      }
      node = node->mNext;
    }
    node->mNext = aNode;
  }
  else {
    PLHashNumber hashCode = NS_PTR_TO_INT32(aParentContent);
    PL_HashTableRawAdd(mTable, entry, hashCode, aParentContent, aNode);
    mLastLookup = nsnull; // hashtable may have shifted bucket out from under us
  }
}

nsresult 
nsFrameManagerBase::UndisplayedMap::AddNodeFor(nsIContent* aParentContent,
                                               nsIContent* aChild, 
                                               nsStyleContext* aStyle)
{
  UndisplayedNode*  node = new UndisplayedNode(aChild, aStyle);
  if (! node) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  AppendNodeFor(node, aParentContent);
  return NS_OK;
}

void
nsFrameManagerBase::UndisplayedMap::RemoveNodeFor(nsIContent* aParentContent,
                                                  UndisplayedNode* aNode)
{
  PLHashEntry** entry = GetEntryFor(aParentContent);
  NS_ASSERTION(*entry, "content not in map");
  if (*entry) {
    if ((UndisplayedNode*)((*entry)->value) == aNode) {  // first node
      if (aNode->mNext) {
        (*entry)->value = aNode->mNext;
        aNode->mNext = nsnull;
      }
      else {
        PL_HashTableRawRemove(mTable, entry, *entry);
        mLastLookup = nsnull; // hashtable may have shifted bucket out from under us
      }
    }
    else {
      UndisplayedNode*  node = (UndisplayedNode*)((*entry)->value);
      while (node->mNext) {
        if (node->mNext == aNode) {
          node->mNext = aNode->mNext;
          aNode->mNext = nsnull;
          break;
        }
        node = node->mNext;
      }
    }
  }
  delete aNode;
}

void
nsFrameManagerBase::UndisplayedMap::RemoveNodesFor(nsIContent* aParentContent)
{
  PLHashEntry** entry = GetEntryFor(aParentContent);
  NS_ASSERTION(entry, "content not in map");
  if (*entry) {
    UndisplayedNode*  node = (UndisplayedNode*)((*entry)->value);
    NS_ASSERTION(node, "null node for non-null entry in UndisplayedMap");
    delete node;
    PL_HashTableRawRemove(mTable, entry, *entry);
    mLastLookup = nsnull; // hashtable may have shifted bucket out from under us
  }
}

static PRIntn
RemoveUndisplayedEntry(PLHashEntry* he, PRIntn i, void* arg)
{
  UndisplayedNode*  node = (UndisplayedNode*)(he->value);
  delete node;
  // Remove and free this entry and continue enumerating
  return HT_ENUMERATE_REMOVE | HT_ENUMERATE_NEXT;
}

void
nsFrameManagerBase::UndisplayedMap::Clear(void)
{
  mLastLookup = nsnull;
  PL_HashTableEnumerateEntries(mTable, RemoveUndisplayedEntry, 0);
}
