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
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Pierre Phaneuf <pp@ludusdesign.com>
 *   Elika J. Etemad ("fantasai") <fantasai@inkedblade.net>
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

/* base class #1 for rendering objects that have child lists */

#include "nsContainerFrame.h"
#include "nsHTMLContainerFrame.h"
#include "nsIContent.h"
#include "nsIDocument.h"
#include "nsPresContext.h"
#include "nsIRenderingContext.h"
#include "nsStyleContext.h"
#include "nsRect.h"
#include "nsPoint.h"
#include "nsGUIEvent.h"
#include "nsStyleConsts.h"
#include "nsIView.h"
#include "nsHTMLContainerFrame.h"
#include "nsFrameManager.h"
#include "nsIPresShell.h"
#include "nsCOMPtr.h"
#include "nsGkAtoms.h"
#include "nsCSSAnonBoxes.h"
#include "nsIViewManager.h"
#include "nsIWidget.h"
#include "nsGfxCIID.h"
#include "nsIServiceManager.h"
#include "nsCSSRendering.h"
#include "nsTransform2D.h"
#include "nsRegion.h"
#include "nsLayoutErrors.h"
#include "nsDisplayList.h"
#include "nsContentErrors.h"
#include "nsIEventStateManager.h"
#include "nsListControlFrame.h"
#include "nsIBaseWindow.h"
#include "nsThemeConstants.h"
#include "nsCSSFrameConstructor.h"
#include "nsThemeConstants.h"

#ifdef NS_DEBUG
#undef NOISY
#else
#undef NOISY
#endif

NS_IMPL_FRAMEARENA_HELPERS(nsContainerFrame)

nsContainerFrame::~nsContainerFrame()
{
}

NS_QUERYFRAME_HEAD(nsContainerFrame)
  NS_QUERYFRAME_ENTRY(nsContainerFrame)
NS_QUERYFRAME_TAIL_INHERITING(nsSplittableFrame)

NS_IMETHODIMP
nsContainerFrame::Init(nsIContent* aContent,
                       nsIFrame*   aParent,
                       nsIFrame*   aPrevInFlow)
{
  nsresult rv = nsSplittableFrame::Init(aContent, aParent, aPrevInFlow);
  if (aPrevInFlow) {
    // Make sure we copy bits from our prev-in-flow that will affect
    // us. A continuation for a container frame needs to know if it
    // has a child with a view so that we'll properly reposition it.
    if (aPrevInFlow->GetStateBits() & NS_FRAME_HAS_CHILD_WITH_VIEW)
      AddStateBits(NS_FRAME_HAS_CHILD_WITH_VIEW);
  }
  return rv;
}

NS_IMETHODIMP
nsContainerFrame::SetInitialChildList(nsIAtom*     aListName,
                                      nsFrameList& aChildList)
{
  nsresult  result;
  if (mFrames.NotEmpty()) {
    // We already have child frames which means we've already been
    // initialized
    NS_NOTREACHED("unexpected second call to SetInitialChildList");
    result = NS_ERROR_UNEXPECTED;
  } else if (aListName) {
    // All we know about is the unnamed principal child list
    NS_NOTREACHED("unknown frame list");
    result = NS_ERROR_INVALID_ARG;
  } else {
#ifdef NS_DEBUG
    nsFrame::VerifyDirtyBitSet(aChildList);
#endif
    mFrames.SetFrames(aChildList);
    result = NS_OK;
  }
  return result;
}

NS_IMETHODIMP
nsContainerFrame::AppendFrames(nsIAtom*  aListName,
                               nsFrameList& aFrameList)
{
  if (nsnull != aListName) {
#ifdef IBMBIDI
    if (aListName != nsGkAtoms::nextBidi)
#endif
    {
      NS_ERROR("unexpected child list");
      return NS_ERROR_INVALID_ARG;
    }
  }
  if (aFrameList.NotEmpty()) {
    mFrames.AppendFrames(this, aFrameList);

    // Ask the parent frame to reflow me.
#ifdef IBMBIDI
    if (nsnull == aListName)
#endif
    {
      PresContext()->PresShell()->
        FrameNeedsReflow(this, nsIPresShell::eTreeChange,
                         NS_FRAME_HAS_DIRTY_CHILDREN);
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
nsContainerFrame::InsertFrames(nsIAtom*  aListName,
                               nsIFrame* aPrevFrame,
                               nsFrameList& aFrameList)
{
  NS_ASSERTION(!aPrevFrame || aPrevFrame->GetParent() == this,
               "inserting after sibling frame with different parent");

  if (nsnull != aListName) {
#ifdef IBMBIDI
    if (aListName != nsGkAtoms::nextBidi)
#endif
    {
      NS_ERROR("unexpected child list");
      return NS_ERROR_INVALID_ARG;
    }
  }
  if (aFrameList.NotEmpty()) {
    // Insert frames after aPrevFrame
    mFrames.InsertFrames(this, aPrevFrame, aFrameList);

#ifdef IBMBIDI
    if (nsnull == aListName)
#endif
    {
      PresContext()->PresShell()->
        FrameNeedsReflow(this, nsIPresShell::eTreeChange,
                         NS_FRAME_HAS_DIRTY_CHILDREN);
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
nsContainerFrame::RemoveFrame(nsIAtom*  aListName,
                              nsIFrame* aOldFrame)
{
  if (nsnull != aListName) {
#ifdef IBMBIDI
    if (nsGkAtoms::nextBidi != aListName)
#endif
    {
      NS_ERROR("unexpected child list");
      return NS_ERROR_INVALID_ARG;
    }
  }

  if (aOldFrame) {
    // Loop and destroy the frame and all of its continuations.
    // If the frame we are removing is a brFrame, we need a reflow so
    // the line the brFrame was on can attempt to pull up any frames
    // that can fit from lines below it.
    PRBool generateReflowCommand = PR_TRUE;
#ifdef IBMBIDI
    if (nsGkAtoms::nextBidi == aListName) {
      generateReflowCommand = PR_FALSE;
    }
#endif
    nsContainerFrame* parent = static_cast<nsContainerFrame*>(aOldFrame->GetParent());
    while (aOldFrame) {
      // When the parent is an inline frame we have a simple task - just
      // remove the frame from its parents list and generate a reflow
      // command.
      nsIFrame* oldFrameNextContinuation = aOldFrame->GetNextContinuation();
      //XXXfr probably should use StealFrame here. I'm not sure if we need to
      //      check the overflow lists atm, but we'll need a prescontext lookup
      //      for overflow containers once we can split abspos elements with
      //      inline containing blocks.
      if (parent == this) {
        if (!parent->mFrames.DestroyFrameIfPresent(aOldFrame)) {
          // Try to remove it from our overflow list, if we have one.
          // The simplest way is to reuse StealFrame.
          StealFrame(PresContext(), aOldFrame, PR_TRUE);
          aOldFrame->Destroy();
        }
      } else {
        // This recursive call takes care of all continuations after aOldFrame,
        // so we don't need to loop anymore.
        parent->RemoveFrame(nsnull, aOldFrame);
        break;
      }
      aOldFrame = oldFrameNextContinuation;
      if (aOldFrame) {
        parent = static_cast<nsContainerFrame*>(aOldFrame->GetParent());
      }
    }

    if (generateReflowCommand) {
      PresContext()->PresShell()->
        FrameNeedsReflow(this, nsIPresShell::eTreeChange,
                         NS_FRAME_HAS_DIRTY_CHILDREN);
    }
  }

  return NS_OK;
}

void
nsContainerFrame::DestroyFrom(nsIFrame* aDestructRoot)
{
  // Prevent event dispatch during destruction
  if (HasView()) {
    GetView()->SetClientData(nsnull);
  }

  // Delete the primary child list
  mFrames.DestroyFramesFrom(aDestructRoot);

  // Destroy auxiliary frame lists
  nsPresContext* prescontext = PresContext();

  DestroyOverflowList(prescontext, aDestructRoot);

  if (IsFrameOfType(nsIFrame::eCanContainOverflowContainers)) {
    nsFrameList* frameList = RemovePropTableFrames(prescontext,
                               nsGkAtoms::overflowContainersProperty);
    if (frameList)
      frameList->DestroyFrom(aDestructRoot);

    frameList = RemovePropTableFrames(prescontext,
                  nsGkAtoms::excessOverflowContainersProperty);
    if (frameList)
      frameList->DestroyFrom(aDestructRoot);
  }

  if (IsGeneratedContentFrame()) {
    // Make sure all the content nodes for the generated content inside
    // this frame know it's going away.
    // See also nsCSSFrameConstructor::CreateGeneratedContentFrame which
    // created this frame.
    nsCOMArray<nsIContent>* generatedContent =
      static_cast<nsCOMArray<nsIContent>*>(
        UnsetProperty(nsGkAtoms::generatedContent));

    if (generatedContent) {
      for (int i = generatedContent->Count() - 1; i >= 0; --i) {
        nsIContent* content = generatedContent->ObjectAt(i);
        // Tell the ESM that this content is going away now, so it'll update
        // its hover content, etc.
        PresContext()->EventStateManager()->
          ContentRemoved(content->GetCurrentDoc(), content);
        content->UnbindFromTree();
      }
      delete generatedContent;
    }
  }

  // Destroy the frame and remove the flow pointers
  nsSplittableFrame::DestroyFrom(aDestructRoot);
}

/////////////////////////////////////////////////////////////////////////////
// Child frame enumeration

nsFrameList
nsContainerFrame::GetChildList(nsIAtom* aListName) const
{
  // We only know about the unnamed principal child list and the overflow
  // lists
  if (nsnull == aListName) {
    return mFrames;
  }

  if (nsGkAtoms::overflowList == aListName) {
    nsFrameList* frameList = GetOverflowFrames();
    return frameList ? *frameList : nsFrameList::EmptyList();
  }

  if (nsGkAtoms::overflowContainersList == aListName) {
    nsFrameList* list = GetPropTableFrames(PresContext(),
                          nsGkAtoms::overflowContainersProperty);
    return list ? *list : nsFrameList::EmptyList();
  }

  if (nsGkAtoms::excessOverflowContainersList == aListName) {
    nsFrameList* list = GetPropTableFrames(PresContext(),
                          nsGkAtoms::excessOverflowContainersProperty);
    return list ? *list : nsFrameList::EmptyList();
  }

  return nsFrameList::EmptyList();
}

#define NS_CONTAINER_FRAME_OVERFLOW_LIST_INDEX                   0
#define NS_CONTAINER_FRAME_OVERFLOW_CONTAINERS_LIST_INDEX        1
#define NS_CONTAINER_FRAME_EXCESS_OVERFLOW_CONTAINERS_LIST_INDEX 2
// If adding/removing lists, don't forget to update count in .h file


nsIAtom*
nsContainerFrame::GetAdditionalChildListName(PRInt32 aIndex) const
{
  if (NS_CONTAINER_FRAME_OVERFLOW_LIST_INDEX == aIndex)
    return nsGkAtoms::overflowList;
  else if (IsFrameOfType(nsIFrame::eCanContainOverflowContainers)) {
    if (NS_CONTAINER_FRAME_OVERFLOW_CONTAINERS_LIST_INDEX == aIndex)
      return nsGkAtoms::overflowContainersList;
    else if (NS_CONTAINER_FRAME_EXCESS_OVERFLOW_CONTAINERS_LIST_INDEX == aIndex)
      return nsGkAtoms::excessOverflowContainersList;
  }
  return nsnull;
}

/////////////////////////////////////////////////////////////////////////////
// Painting/Events

NS_IMETHODIMP
nsContainerFrame::BuildDisplayList(nsDisplayListBuilder*   aBuilder,
                                   const nsRect&           aDirtyRect,
                                   const nsDisplayListSet& aLists)
{
  nsresult rv = DisplayBorderBackgroundOutline(aBuilder, aLists);
  NS_ENSURE_SUCCESS(rv, rv);

  return BuildDisplayListForNonBlockChildren(aBuilder, aDirtyRect, aLists);
}

nsresult
nsContainerFrame::BuildDisplayListForNonBlockChildren(nsDisplayListBuilder*   aBuilder,
                                                      const nsRect&           aDirtyRect,
                                                      const nsDisplayListSet& aLists,
                                                      PRUint32                aFlags)
{
  nsIFrame* kid = mFrames.FirstChild();
  // Put each child's background directly onto the content list
  nsDisplayListSet set(aLists, aLists.Content());
  // The children should be in content order
  while (kid) {
    nsresult rv = BuildDisplayListForChild(aBuilder, kid, aDirtyRect, set, aFlags);
    NS_ENSURE_SUCCESS(rv, rv);
    kid = kid->GetNextSibling();
  }
  return NS_OK;
}

/* virtual */ void
nsContainerFrame::ChildIsDirty(nsIFrame* aChild)
{
  AddStateBits(NS_FRAME_HAS_DIRTY_CHILDREN);
}

PRBool
nsContainerFrame::IsLeaf() const
{
  return PR_FALSE;
}

PRBool
nsContainerFrame::PeekOffsetNoAmount(PRBool aForward, PRInt32* aOffset)
{
  NS_ASSERTION (aOffset && *aOffset <= 1, "aOffset out of range");
  // Don't allow the caret to stay in an empty (leaf) container frame.
  return PR_FALSE;
}

PRBool
nsContainerFrame::PeekOffsetCharacter(PRBool aForward, PRInt32* aOffset)
{
  NS_ASSERTION (aOffset && *aOffset <= 1, "aOffset out of range");
  // Don't allow the caret to stay in an empty (leaf) container frame.
  return PR_FALSE;
}

/////////////////////////////////////////////////////////////////////////////
// Helper member functions

/**
 * Position the view associated with |aKidFrame|, if there is one. A
 * container frame should call this method after positioning a frame,
 * but before |Reflow|.
 */
void
nsContainerFrame::PositionFrameView(nsIFrame* aKidFrame)
{
  nsIFrame* parentFrame = aKidFrame->GetParent();
  if (!aKidFrame->HasView() || !parentFrame)
    return;

  nsIView* view = aKidFrame->GetView();
  nsIViewManager* vm = view->GetViewManager();
  nsPoint pt;
  nsIView* ancestorView = parentFrame->GetClosestView(&pt);

  if (ancestorView != view->GetParent()) {
    NS_ASSERTION(ancestorView == view->GetParent()->GetParent(),
                 "Allowed only one anonymous view between frames");
    // parentFrame is responsible for positioning aKidFrame's view
    // explicitly
    return;
  }

  pt += aKidFrame->GetPosition();
  vm->MoveViewTo(view, pt.x, pt.y);
}

static nsIWidget*
GetPresContextContainerWidget(nsPresContext* aPresContext)
{
  nsCOMPtr<nsISupports> container = aPresContext->Document()->GetContainer();
  nsCOMPtr<nsIBaseWindow> baseWindow = do_QueryInterface(container);
  if (!baseWindow)
    return nsnull;

  nsCOMPtr<nsIWidget> mainWidget;
  baseWindow->GetMainWidget(getter_AddRefs(mainWidget));
  return mainWidget;
}

static PRBool
IsTopLevelWidget(nsIWidget* aWidget)
{
  nsWindowType windowType;
  aWidget->GetWindowType(windowType);
  return windowType == eWindowType_toplevel ||
         windowType == eWindowType_dialog || 
         windowType == eWindowType_sheet;
  // popups aren't toplevel so they're not handled here
}

void
nsContainerFrame::SyncWindowProperties(nsPresContext*       aPresContext,
                                       nsIFrame*            aFrame,
                                       nsIView*             aView)
{
#ifdef MOZ_XUL
  if (!aView || !nsCSSRendering::IsCanvasFrame(aFrame) || !aView->HasWidget())
    return;

  nsIWidget* windowWidget = GetPresContextContainerWidget(aPresContext);
  if (!windowWidget || !IsTopLevelWidget(windowWidget))
    return;

  nsIViewManager* vm = aView->GetViewManager();
  nsIView* rootView;
  vm->GetRootView(rootView);

  if (aView != rootView)
    return;

  nsIContent* rootContent = aPresContext->Document()->GetRootContent();
  if (!rootContent || !rootContent->IsXUL()) {
    // Scrollframes use native widgets which don't work well with
    // translucent windows, at least in Windows XP. So if the document
    // has a root scrollrame it's useless to try to make it transparent,
    // we'll just get something broken.
    // nsCSSFrameConstructor::ConstructRootFrame constructs root
    // scrollframes whenever the root element is not a XUL element, so
    // we test for that here. We can't just call
    // presShell->GetRootScrollFrame() since that might not have
    // been constructed yet.
    // We can change this to allow translucent toplevel HTML documents
    // (e.g. to do something like Dashboard widgets), once we
    // have broad support for translucent scrolled documents, but be
    // careful because apparently some Firefox extensions expect
    // openDialog("something.html") to produce an opaque window
    // even if the HTML doesn't have a background-color set.
    return;
  }

  nsIFrame *rootFrame = aPresContext->PresShell()->FrameConstructor()->GetRootElementStyleFrame();
  if (!rootFrame)
    return;

  nsTransparencyMode mode = nsLayoutUtils::GetFrameTransparency(aFrame, rootFrame);
  nsIWidget* viewWidget = aView->GetWidget();
  viewWidget->SetTransparencyMode(mode);
  windowWidget->SetWindowShadowStyle(rootFrame->GetStyleUIReset()->mWindowShadow);
#endif
}

void
nsContainerFrame::SyncFrameViewAfterReflow(nsPresContext* aPresContext,
                                           nsIFrame*       aFrame,
                                           nsIView*        aView,
                                           const nsRect*   aCombinedArea,
                                           PRUint32        aFlags)
{
  if (!aView) {
    return;
  }

  NS_ASSERTION(aCombinedArea, "Combined area must be passed in now");

  // Make sure the view is sized and positioned correctly
  if (0 == (aFlags & NS_FRAME_NO_MOVE_VIEW)) {
    PositionFrameView(aFrame);
  }

  if (0 == (aFlags & NS_FRAME_NO_SIZE_VIEW)) {
    nsIViewManager* vm = aView->GetViewManager();

    vm->ResizeView(aView, *aCombinedArea, PR_TRUE);
  }
}

void
nsContainerFrame::SyncFrameViewProperties(nsPresContext*  aPresContext,
                                          nsIFrame*        aFrame,
                                          nsStyleContext*  aStyleContext,
                                          nsIView*         aView,
                                          PRUint32         aFlags)
{
  NS_ASSERTION(!aStyleContext || aFrame->GetStyleContext() == aStyleContext,
               "Wrong style context for frame?");

  if (!aView) {
    return;
  }

  nsIViewManager* vm = aView->GetViewManager();
 
  if (nsnull == aStyleContext) {
    aStyleContext = aFrame->GetStyleContext();
  }

  // Make sure visibility is correct. This only affects nsSubdocumentFrame.
  if (0 == (aFlags & NS_FRAME_NO_VISIBILITY) &&
      !aFrame->SupportsVisibilityHidden()) {
    // See if the view should be hidden or visible
    vm->SetViewVisibility(aView,
        aStyleContext->GetStyleVisibility()->IsVisible()
            ? nsViewVisibility_kShow : nsViewVisibility_kHide);
  }

  // See if the frame is being relatively positioned or absolutely
  // positioned
  PRBool isPositioned = aStyleContext->GetStyleDisplay()->IsPositioned();

  PRInt32 zIndex = 0;
  PRBool  autoZIndex = PR_FALSE;

  if (!isPositioned) {
    autoZIndex = PR_TRUE;
  } else {
    // Make sure z-index is correct
    const nsStylePosition* position = aStyleContext->GetStylePosition();

    if (position->mZIndex.GetUnit() == eStyleUnit_Integer) {
      zIndex = position->mZIndex.GetIntValue();
    } else if (position->mZIndex.GetUnit() == eStyleUnit_Auto) {
      autoZIndex = PR_TRUE;
    }
  }

  vm->SetViewZIndex(aView, autoZIndex, zIndex, isPositioned);
}

static nscoord GetCoord(const nsStyleCoord& aCoord, nscoord aIfNotCoord)
{
  return aCoord.GetUnit() == eStyleUnit_Coord
           ? aCoord.GetCoordValue()
           : aIfNotCoord;
}

void
nsContainerFrame::DoInlineIntrinsicWidth(nsIRenderingContext *aRenderingContext,
                                         InlineIntrinsicWidthData *aData,
                                         nsLayoutUtils::IntrinsicWidthType aType)
{
  if (GetPrevInFlow())
    return; // Already added.

  NS_PRECONDITION(aType == nsLayoutUtils::MIN_WIDTH ||
                  aType == nsLayoutUtils::PREF_WIDTH, "bad type");

  PRUint8 startSide, endSide;
  if (GetStyleVisibility()->mDirection == NS_STYLE_DIRECTION_LTR) {
    startSide = NS_SIDE_LEFT;
    endSide = NS_SIDE_RIGHT;
  } else {
    startSide = NS_SIDE_RIGHT;
    endSide = NS_SIDE_LEFT;
  }

  const nsStylePadding *stylePadding = GetStylePadding();
  const nsStyleBorder *styleBorder = GetStyleBorder();
  const nsStyleMargin *styleMargin = GetStyleMargin();

  // This goes at the beginning no matter how things are broken and how
  // messy the bidi situations are, since per CSS2.1 section 8.6
  // (implemented in bug 328168), the startSide border is always on the
  // first line.
  // This frame is a first-in-flow, but it might have a previous bidi
  // continuation, in which case that continuation should handle the startSide 
  // border.
  if (!GetPrevContinuation()) {
    aData->currentLine +=
      GetCoord(stylePadding->mPadding.Get(startSide), 0) +
      styleBorder->GetActualBorderWidth(startSide) +
      GetCoord(styleMargin->mMargin.Get(startSide), 0);
  }

  const nsLineList_iterator* savedLine = aData->line;
  nsIFrame* const savedLineContainer = aData->lineContainer;

  nsContainerFrame *lastInFlow;
  for (nsContainerFrame *nif = this; nif;
       nif = static_cast<nsContainerFrame*>(nif->GetNextInFlow())) {
    for (nsIFrame *kid = nif->mFrames.FirstChild(); kid;
         kid = kid->GetNextSibling()) {
      if (aType == nsLayoutUtils::MIN_WIDTH)
        kid->AddInlineMinWidth(aRenderingContext,
                               static_cast<InlineMinWidthData*>(aData));
      else
        kid->AddInlinePrefWidth(aRenderingContext,
                                static_cast<InlinePrefWidthData*>(aData));
    }
    
    // After we advance to our next-in-flow, the stored line and line container
    // may no longer be correct. Just forget them.
    aData->line = nsnull;
    aData->lineContainer = nsnull;

    lastInFlow = nif;
  }
  
  aData->line = savedLine;
  aData->lineContainer = savedLineContainer;

  // This goes at the end no matter how things are broken and how
  // messy the bidi situations are, since per CSS2.1 section 8.6
  // (implemented in bug 328168), the endSide border is always on the
  // last line.
  // We reached the last-in-flow, but it might have a next bidi
  // continuation, in which case that continuation should handle 
  // the endSide border.
  if (!lastInFlow->GetNextContinuation()) {
    aData->currentLine +=
      GetCoord(stylePadding->mPadding.Get(endSide), 0) +
      styleBorder->GetActualBorderWidth(endSide) +
      GetCoord(styleMargin->mMargin.Get(endSide), 0);
  }
}

/* virtual */ nsSize
nsContainerFrame::ComputeAutoSize(nsIRenderingContext *aRenderingContext,
                                  nsSize aCBSize, nscoord aAvailableWidth,
                                  nsSize aMargin, nsSize aBorder,
                                  nsSize aPadding, PRBool aShrinkWrap)
{
  nsSize result(0xdeadbeef, NS_UNCONSTRAINEDSIZE);
  nscoord availBased = aAvailableWidth - aMargin.width - aBorder.width -
                       aPadding.width;
  // replaced elements always shrink-wrap
  if (aShrinkWrap || IsFrameOfType(eReplaced)) {
    // don't bother setting it if the result won't be used
    if (GetStylePosition()->mWidth.GetUnit() == eStyleUnit_Auto) {
      result.width = ShrinkWidthToFit(aRenderingContext, availBased);
    }
  } else {
    result.width = availBased;
  }
  return result;
}

/**
 * Invokes the WillReflow() function, positions the frame and its view (if
 * requested), and then calls Reflow(). If the reflow succeeds and the child
 * frame is complete, deletes any next-in-flows using DeleteNextInFlowChild()
 */
nsresult
nsContainerFrame::ReflowChild(nsIFrame*                aKidFrame,
                              nsPresContext*           aPresContext,
                              nsHTMLReflowMetrics&     aDesiredSize,
                              const nsHTMLReflowState& aReflowState,
                              nscoord                  aX,
                              nscoord                  aY,
                              PRUint32                 aFlags,
                              nsReflowStatus&          aStatus,
                              nsOverflowContinuationTracker* aTracker)
{
  NS_PRECONDITION(aReflowState.frame == aKidFrame, "bad reflow state");

  nsresult  result;

  // Send the WillReflow() notification, and position the child frame
  // and its view if requested
  aKidFrame->WillReflow(aPresContext);

  if (NS_FRAME_NO_MOVE_FRAME != (aFlags & NS_FRAME_NO_MOVE_FRAME)) {
    if ((aFlags & NS_FRAME_INVALIDATE_ON_MOVE) &&
        !(aKidFrame->GetStateBits() & NS_FRAME_FIRST_REFLOW) &&
        aKidFrame->GetPosition() != nsPoint(aX, aY)) {
      aKidFrame->InvalidateOverflowRect();
    }
    aKidFrame->SetPosition(nsPoint(aX, aY));
  }

  if (0 == (aFlags & NS_FRAME_NO_MOVE_VIEW)) {
    PositionFrameView(aKidFrame);
  }

  // Reflow the child frame
  result = aKidFrame->Reflow(aPresContext, aDesiredSize, aReflowState,
                             aStatus);

  // If the reflow was successful and the child frame is complete, delete any
  // next-in-flows
  if (NS_SUCCEEDED(result) && NS_FRAME_IS_FULLY_COMPLETE(aStatus)) {
    nsIFrame* kidNextInFlow = aKidFrame->GetNextInFlow();
    if (nsnull != kidNextInFlow) {
      // Remove all of the childs next-in-flows. Make sure that we ask
      // the right parent to do the removal (it's possible that the
      // parent is not this because we are executing pullup code)
      if (aTracker) aTracker->Finish(aKidFrame);
      static_cast<nsContainerFrame*>(kidNextInFlow->GetParent())
        ->DeleteNextInFlowChild(aPresContext, kidNextInFlow, PR_TRUE);
    }
  }
  return result;
}


/**
 * Position the views of |aFrame|'s descendants. A container frame
 * should call this method if it moves a frame after |Reflow|.
 */
void
nsContainerFrame::PositionChildViews(nsIFrame* aFrame)
{
  if (!(aFrame->GetStateBits() & NS_FRAME_HAS_CHILD_WITH_VIEW)) {
    return;
  }

  nsIAtom*  childListName = nsnull;
  PRInt32   childListIndex = 0;

  do {
    // Recursively walk aFrame's child frames
    nsIFrame* childFrame = aFrame->GetFirstChild(childListName);
    while (childFrame) {
      // Position the frame's view (if it has one) otherwise recursively
      // process its children
      if (childFrame->HasView()) {
        PositionFrameView(childFrame);
      } else {
        PositionChildViews(childFrame);
      }

      // Get the next sibling child frame
      childFrame = childFrame->GetNextSibling();
    }

    // also process the additional child lists, but skip the popup list as the
    // view for popups is managed by the parent. Currently only nsMenuFrame
    // has a popupList and during layout will call nsMenuPopupFrame::AdjustView.
    do {
      childListName = aFrame->GetAdditionalChildListName(childListIndex++);
    } while (childListName == nsGkAtoms::popupList);
  } while (childListName);
}

/**
 * The second half of frame reflow. Does the following:
 * - sets the frame's bounds
 * - sizes and positions (if requested) the frame's view. If the frame's final
 *   position differs from the current position and the frame itself does not
 *   have a view, then any child frames with views are positioned so they stay
 *   in sync
 * - sets the view's visibility, opacity, content transparency, and clip
 * - invoked the DidReflow() function
 *
 * Flags:
 * NS_FRAME_NO_MOVE_FRAME - don't move the frame. aX and aY are ignored in this
 *    case. Also implies NS_FRAME_NO_MOVE_VIEW
 * NS_FRAME_NO_MOVE_VIEW - don't position the frame's view. Set this if you
 *    don't want to automatically sync the frame and view
 * NS_FRAME_NO_SIZE_VIEW - don't size the frame's view
 */
nsresult
nsContainerFrame::FinishReflowChild(nsIFrame*                  aKidFrame,
                                    nsPresContext*             aPresContext,
                                    const nsHTMLReflowState*   aReflowState,
                                    const nsHTMLReflowMetrics& aDesiredSize,
                                    nscoord                    aX,
                                    nscoord                    aY,
                                    PRUint32                   aFlags)
{
  nsPoint curOrigin = aKidFrame->GetPosition();
  nsRect  bounds(aX, aY, aDesiredSize.width, aDesiredSize.height);

  aKidFrame->SetRect(bounds);

  if (aKidFrame->HasView()) {
    nsIView* view = aKidFrame->GetView();
    // Make sure the frame's view is properly sized and positioned and has
    // things like opacity correct
    SyncFrameViewAfterReflow(aPresContext, aKidFrame, view,
                             &aDesiredSize.mOverflowArea,
                             aFlags);
  }

  if (!(aFlags & NS_FRAME_NO_MOVE_VIEW) &&
      (curOrigin.x != aX || curOrigin.y != aY)) {
    if (!aKidFrame->HasView()) {
      // If the frame has moved, then we need to make sure any child views are
      // correctly positioned
      PositionChildViews(aKidFrame);
    }

    // We also need to redraw everything associated with the frame
    // because if the frame's Reflow issued any invalidates, then they
    // will be at the wrong offset ... note that this includes
    // invalidates issued against the frame's children, so we need to
    // invalidate the overflow area too.
    aKidFrame->Invalidate(aDesiredSize.mOverflowArea);
  }
  
  return aKidFrame->DidReflow(aPresContext, aReflowState, NS_FRAME_REFLOW_FINISHED);
}

nsresult
nsContainerFrame::ReflowOverflowContainerChildren(nsPresContext*           aPresContext,
                                                  const nsHTMLReflowState& aReflowState,
                                                  nsRect&                  aOverflowRect,
                                                  PRUint32                 aFlags,
                                                  nsReflowStatus&          aStatus)
{
  NS_PRECONDITION(aPresContext, "null pointer");
  nsresult rv = NS_OK;

  nsFrameList* overflowContainers =
               GetPropTableFrames(aPresContext,
                                  nsGkAtoms::overflowContainersProperty);

  NS_ASSERTION(!(overflowContainers && GetPrevInFlow()
                 && static_cast<nsContainerFrame*>(GetPrevInFlow())
                      ->GetPropTableFrames(aPresContext,
                          nsGkAtoms::excessOverflowContainersProperty)),
               "conflicting overflow containers lists");

  if (!overflowContainers) {
    // Drain excess from previnflow
    nsContainerFrame* prev = (nsContainerFrame*) GetPrevInFlow();
    if (prev) {
      nsFrameList* excessFrames =
        prev->RemovePropTableFrames(aPresContext,
                nsGkAtoms::excessOverflowContainersProperty);
      if (excessFrames) {
        excessFrames->ApplySetParent(this);
        nsHTMLContainerFrame::ReparentFrameViewList(aPresContext, *excessFrames,
                                                    prev, this);
        overflowContainers = excessFrames;
        rv = SetPropTableFrames(aPresContext, overflowContainers,
                                nsGkAtoms::overflowContainersProperty);
        if (NS_FAILED(rv)) {
          excessFrames->DestroyFrames();
          delete excessFrames;
          return rv;
        }
      }
    }
  }

  if (!overflowContainers)
    return NS_OK; // nothing to reflow

  nsOverflowContinuationTracker tracker(aPresContext, this, PR_FALSE, PR_FALSE);
  for (nsIFrame* frame = overflowContainers->FirstChild(); frame;
       frame = frame->GetNextSibling()) {
    if (frame->GetPrevInFlow()->GetParent() != GetPrevInFlow()) {
      // frame's prevInFlow has moved, skip reflowing this frame;
      // it will get reflowed once it's been placed
      continue;
    }
    if (NS_SUBTREE_DIRTY(frame)) {
      // Get prev-in-flow
      nsIFrame* prevInFlow = frame->GetPrevInFlow();
      NS_ASSERTION(prevInFlow,
                   "overflow container frame must have a prev-in-flow");
      NS_ASSERTION(frame->GetStateBits() & NS_FRAME_IS_OVERFLOW_CONTAINER,
                   "overflow container frame must have overflow container bit set");
      nsRect prevRect = prevInFlow->GetRect();

      // Initialize reflow params
      nsSize availSpace(prevRect.width, aReflowState.availableHeight);
      nsHTMLReflowMetrics desiredSize;
      nsHTMLReflowState frameState(aPresContext, aReflowState,
                                   frame, availSpace);
      nsReflowStatus frameStatus = NS_FRAME_COMPLETE;

      // Cache old bounds
      nsRect oldRect = frame->GetRect();
      nsRect oldOverflow = frame->GetOverflowRect();

      // Reflow
      rv = ReflowChild(frame, aPresContext, desiredSize, frameState,
                       prevRect.x, 0, aFlags, frameStatus, &tracker);
      NS_ENSURE_SUCCESS(rv, rv);
      //XXXfr Do we need to override any shrinkwrap effects here?
      // e.g. desiredSize.width = prevRect.width;
      rv = FinishReflowChild(frame, aPresContext, &frameState, desiredSize,
                             prevRect.x, 0, aFlags);
      NS_ENSURE_SUCCESS(rv, rv);

      // Invalidate if there was a position or size change
      nsRect rect = frame->GetRect();
      if (rect != oldRect) {
        nsRect dirtyRect = oldOverflow;
        dirtyRect.MoveBy(oldRect.x, oldRect.y);
        Invalidate(dirtyRect);

        dirtyRect = frame->GetOverflowRect();
        dirtyRect.MoveBy(rect.x, rect.y);
        Invalidate(dirtyRect);
      }

      // Handle continuations
      if (!NS_FRAME_IS_FULLY_COMPLETE(frameStatus)) {
        if (frame->GetStateBits() & NS_FRAME_OUT_OF_FLOW) {
          // Abspos frames can't cause their parent to be incomplete,
          // only overflow incomplete.
          NS_FRAME_SET_OVERFLOW_INCOMPLETE(frameStatus);
        }
        else {
          NS_ASSERTION(NS_FRAME_IS_COMPLETE(frameStatus),
                       "overflow container frames can't be incomplete, only overflow-incomplete");
        }

        // Acquire a next-in-flow, creating it if necessary
        nsIFrame* nif = frame->GetNextInFlow();
        if (!nif) {
          NS_ASSERTION(frameStatus & NS_FRAME_REFLOW_NEXTINFLOW,
                       "Someone forgot a REFLOW_NEXTINFLOW flag");
          rv = aPresContext->PresShell()->FrameConstructor()->
                 CreateContinuingFrame(aPresContext, frame, this, &nif);
          NS_ENSURE_SUCCESS(rv, rv);
        }
        else if (!(nif->GetStateBits() & NS_FRAME_IS_OVERFLOW_CONTAINER)) {
          // used to be a normal next-in-flow; steal it from the child list
          rv = static_cast<nsContainerFrame*>(nif->GetParent())
                 ->StealFrame(aPresContext, nif);
          NS_ENSURE_SUCCESS(rv, rv);
        }

        tracker.Insert(nif, frameStatus);
      }
      NS_MergeReflowStatusInto(&aStatus, frameStatus);
      // At this point it would be nice to assert !frame->GetOverflowRect().IsEmpty(),
      // but we have some unsplittable frames that, when taller than
      // availableHeight will push zero-height content into a next-in-flow.
    }
    else {
      tracker.Skip(frame, aStatus);
      if (aReflowState.mFloatManager)
        nsBlockFrame::RecoverFloatsFor(frame, *aReflowState.mFloatManager);
    }
    ConsiderChildOverflow(aOverflowRect, frame);
  }

  return NS_OK;
}

void
nsContainerFrame::DisplayOverflowContainers(nsDisplayListBuilder*   aBuilder,
                                            const nsRect&           aDirtyRect,
                                            const nsDisplayListSet& aLists)
{
  nsFrameList* overflowconts = GetPropTableFrames(PresContext(),
                                 nsGkAtoms::overflowContainersProperty);
  if (overflowconts) {
    for (nsIFrame* frame = overflowconts->FirstChild(); frame;
         frame = frame->GetNextSibling()) {
      BuildDisplayListForChild(aBuilder, frame, aDirtyRect, aLists);
    }
  }
}

nsresult
nsContainerFrame::StealFrame(nsPresContext* aPresContext,
                             nsIFrame*      aChild,
                             PRBool         aForceNormal)
{
  PRBool removed = PR_TRUE;
  if ((aChild->GetStateBits() & NS_FRAME_IS_OVERFLOW_CONTAINER)
      && !aForceNormal) {
    // Try removing from the overflow container list
    if (!RemovePropTableFrame(aPresContext, aChild,
                              nsGkAtoms::overflowContainersProperty)) {
      // It must be in the excess overflow container list
      removed = RemovePropTableFrame(aPresContext, aChild,
                  nsGkAtoms::excessOverflowContainersProperty);
    }
  }
  else {
    if (!mFrames.RemoveFrameIfPresent(aChild)) {
      removed = PR_FALSE;
      // We didn't find the child in the parent's principal child list.
      // Maybe it's on the overflow list?
      nsFrameList* frameList = GetOverflowFrames();
      if (frameList) {
        removed = frameList->RemoveFrameIfPresent(aChild);
        if (frameList->IsEmpty()) {
          DestroyOverflowList(aPresContext);
        }
      }
    }
  }

  NS_POSTCONDITION(removed, "StealFrame: can't find aChild");
  return removed ? NS_OK : NS_ERROR_UNEXPECTED;
}

nsFrameList
nsContainerFrame::StealFramesAfter(nsIFrame* aChild)
{
  NS_ASSERTION(!aChild ||
               !(aChild->GetStateBits() & NS_FRAME_IS_OVERFLOW_CONTAINER),
               "StealFramesAfter doesn't handle overflow containers");
  NS_ASSERTION(GetType() != nsGkAtoms::blockFrame, "unexpected call");

  if (!aChild) {
    nsFrameList copy(mFrames);
    mFrames.Clear();
    return copy;
  }

  for (nsFrameList::FrameLinkEnumerator iter(mFrames); !iter.AtEnd();
       iter.Next()) {
    if (iter.PrevFrame() == aChild) {
      return mFrames.ExtractTail(iter);
    }
  }

  // We didn't find the child in the principal child list.
  // Maybe it's on the overflow list?
  nsFrameList* overflowFrames = GetOverflowFrames();
  if (overflowFrames) {
    for (nsFrameList::FrameLinkEnumerator iter(*overflowFrames); !iter.AtEnd();
         iter.Next()) {
      if (iter.PrevFrame() == aChild) {
        return overflowFrames->ExtractTail(iter);
      }
    }
  }

  NS_ERROR("StealFramesAfter: can't find aChild");
  return nsFrameList::EmptyList();
}

void
nsContainerFrame::DestroyOverflowList(nsPresContext* aPresContext,
                                      nsIFrame*      aDestructRoot)
{
  nsFrameList* list =
    RemovePropTableFrames(aPresContext, nsGkAtoms::overflowProperty);
  if (list)
    list->DestroyFrom(aDestructRoot);
}

/**
 * Remove and delete aNextInFlow and its next-in-flows. Updates the sibling and flow
 * pointers
 */
void
nsContainerFrame::DeleteNextInFlowChild(nsPresContext* aPresContext,
                                        nsIFrame*      aNextInFlow,
                                        PRBool         aDeletingEmptyFrames)
{
#ifdef DEBUG
  nsIFrame* prevInFlow = aNextInFlow->GetPrevInFlow();
#endif
  NS_PRECONDITION(prevInFlow, "bad prev-in-flow");

  // If the next-in-flow has a next-in-flow then delete it, too (and
  // delete it first).
  // Do this in a loop so we don't overflow the stack for frames
  // with very many next-in-flows
  nsIFrame* nextNextInFlow = aNextInFlow->GetNextInFlow();
  if (nextNextInFlow) {
    nsAutoTArray<nsIFrame*, 8> frames;
    for (nsIFrame* f = nextNextInFlow; f; f = f->GetNextInFlow()) {
      frames.AppendElement(f);
    }
    for (PRInt32 i = frames.Length() - 1; i >= 0; --i) {
      nsIFrame* delFrame = frames.ElementAt(i);
      static_cast<nsContainerFrame*>(delFrame->GetParent())
        ->DeleteNextInFlowChild(aPresContext, delFrame, aDeletingEmptyFrames);
    }
  }

  aNextInFlow->Invalidate(aNextInFlow->GetOverflowRect());

  // Take the next-in-flow out of the parent's child list
#ifdef DEBUG
  nsresult rv = 
#endif
    StealFrame(aPresContext, aNextInFlow);
  NS_ASSERTION(NS_SUCCEEDED(rv), "StealFrame failure");

  // Delete the next-in-flow frame and its descendants. This will also
  // remove it from its next-in-flow/prev-in-flow chain.
  aNextInFlow->Destroy();

  NS_POSTCONDITION(!prevInFlow->GetNextInFlow(), "non null next-in-flow");
}

// Destructor function for the proptable-stored framelists
static void
DestroyFrameList(void*           aFrame,
                 nsIAtom*        aPropertyName,
                 void*           aPropertyValue,
                 void*           aDtorData)
{
  if (aPropertyValue)
    static_cast<nsFrameList*>(aPropertyValue)->Destroy();
}

/**
 * Set the frames on the overflow list
 */
nsresult
nsContainerFrame::SetOverflowFrames(nsPresContext* aPresContext,
                                    const nsFrameList& aOverflowFrames)
{
  NS_PRECONDITION(aOverflowFrames.NotEmpty(), "Shouldn't be called");
  nsFrameList* newList = new nsFrameList(aOverflowFrames);
  if (!newList) {
    // XXXbz should really destroy the frames here, but callers are holding
    // pointers to them.... We should switch all callers to framelists, then
    // audit and do that.
    return NS_ERROR_OUT_OF_MEMORY;
  }

  nsresult rv =
    aPresContext->PropertyTable()->SetProperty(this,
                                               nsGkAtoms::overflowProperty,
                                               newList,
                                               DestroyFrameList,
                                               nsnull);
  if (NS_FAILED(rv)) {
    newList->Destroy();
  }

  // Verify that we didn't overwrite an existing overflow list
  NS_ASSERTION(rv != NS_PROPTABLE_PROP_OVERWRITTEN, "existing overflow list");

  return rv;
}

nsFrameList*
nsContainerFrame::GetPropTableFrames(nsPresContext*  aPresContext,
                                     nsIAtom*        aPropID) const
{
  nsPropertyTable* propTable = aPresContext->PropertyTable();
  return static_cast<nsFrameList*>(propTable->GetProperty(this, aPropID));
}

nsFrameList*
nsContainerFrame::RemovePropTableFrames(nsPresContext*  aPresContext,
                                        nsIAtom*        aPropID) const
{
  nsPropertyTable* propTable = aPresContext->PropertyTable();
  return static_cast<nsFrameList*>(propTable->UnsetProperty(this, aPropID));
}

PRBool
nsContainerFrame::RemovePropTableFrame(nsPresContext*  aPresContext,
                                       nsIFrame*       aFrame,
                                       nsIAtom*        aPropID) const
{
  nsFrameList* frameList = RemovePropTableFrames(aPresContext, aPropID);
  if (!frameList) {
    // No such list
    return PR_FALSE;
  }
  if (!frameList->RemoveFrameIfPresent(aFrame)) {
    // Found list, but it doesn't have the frame. Put list back.
    SetPropTableFrames(aPresContext, frameList, aPropID);
    return PR_FALSE;
  }

  if (frameList->IsEmpty()) {
    // Removed frame and now list is empty. Delete it.
    delete frameList;
  }
  else {
    // Removed frame, but list not empty. Put it back.
    SetPropTableFrames(aPresContext, frameList, aPropID);
  }
  return PR_TRUE;
}

nsresult
nsContainerFrame::SetPropTableFrames(nsPresContext*  aPresContext,
                                     nsFrameList*    aFrameList,
                                     nsIAtom*        aPropID) const
{
  NS_PRECONDITION(aPresContext && aPropID && aFrameList, "null ptr");
  nsresult rv = aPresContext->PropertyTable()->SetProperty(this, aPropID,
                  aFrameList, DestroyFrameList, nsnull);
  NS_ASSERTION(rv != NS_PROPTABLE_PROP_OVERWRITTEN, "existing framelist");
  return rv;
}

/**
 * Push aFromChild and its next siblings to the next-in-flow. Change the
 * geometric parent of each frame that's pushed. If there is no next-in-flow
 * the frames are placed on the overflow list (and the geometric parent is
 * left unchanged).
 *
 * Updates the next-in-flow's child count. Does <b>not</b> update the
 * pusher's child count.
 *
 * @param   aFromChild the first child frame to push. It is disconnected from
 *            aPrevSibling
 * @param   aPrevSibling aFromChild's previous sibling. Must not be null. It's
 *            an error to push a parent's first child frame
 */
void
nsContainerFrame::PushChildren(nsPresContext* aPresContext,
                               nsIFrame*       aFromChild,
                               nsIFrame*       aPrevSibling)
{
  NS_PRECONDITION(aFromChild, "null pointer");
  NS_PRECONDITION(aPrevSibling, "pushing first child");
  NS_PRECONDITION(aPrevSibling->GetNextSibling() == aFromChild, "bad prev sibling");

  // Disconnect aFromChild from its previous sibling
  nsFrameList tail = mFrames.RemoveFramesAfter(aPrevSibling);

  nsContainerFrame* nextInFlow =
    static_cast<nsContainerFrame*>(GetNextInFlow());
  if (nextInFlow) {
    // XXX This is not a very good thing to do. If it gets removed
    // then remove the copy of this routine that doesn't do this from
    // nsInlineFrame.
    // When pushing and pulling frames we need to check for whether any
    // views need to be reparented.
    for (nsIFrame* f = aFromChild; f; f = f->GetNextSibling()) {
      nsHTMLContainerFrame::ReparentFrameView(aPresContext, f, this, nextInFlow);
    }
    nextInFlow->mFrames.InsertFrames(nextInFlow, nsnull, tail);
  }
  else {
    // Add the frames to our overflow list
    SetOverflowFrames(aPresContext, tail);
  }
}

/**
 * Moves any frames on the overflow lists (the prev-in-flow's overflow list and
 * the receiver's overflow list) to the child list.
 *
 * Updates this frame's child count and content mapping.
 *
 * @return  PR_TRUE if any frames were moved and PR_FALSE otherwise
 */
PRBool
nsContainerFrame::MoveOverflowToChildList(nsPresContext* aPresContext)
{
  PRBool result = PR_FALSE;

  // Check for an overflow list with our prev-in-flow
  nsContainerFrame* prevInFlow = (nsContainerFrame*)GetPrevInFlow();
  if (nsnull != prevInFlow) {
    nsAutoPtr<nsFrameList> prevOverflowFrames(prevInFlow->StealOverflowFrames());
    if (prevOverflowFrames) {
      // Tables are special; they can have repeated header/footer
      // frames on mFrames at this point.
      NS_ASSERTION(mFrames.IsEmpty() || GetType() == nsGkAtoms::tableFrame,
                   "bad overflow list");
      // When pushing and pulling frames we need to check for whether any
      // views need to be reparented.
      nsHTMLContainerFrame::ReparentFrameViewList(aPresContext,
                                                  *prevOverflowFrames,
                                                  prevInFlow, this);
      mFrames.AppendFrames(this, *prevOverflowFrames);
      result = PR_TRUE;
    }
  }

  // It's also possible that we have an overflow list for ourselves
  nsAutoPtr<nsFrameList> overflowFrames(StealOverflowFrames());
  if (overflowFrames) {
    NS_ASSERTION(mFrames.NotEmpty(), "overflow list w/o frames");
    mFrames.AppendFrames(nsnull, *overflowFrames);
    result = PR_TRUE;
  }
  return result;
}

nsOverflowContinuationTracker::nsOverflowContinuationTracker(nsPresContext*    aPresContext,
                                                             nsContainerFrame* aFrame,
                                                             PRBool            aWalkOOFFrames,
                                                             PRBool            aSkipOverflowContainerChildren)
  : mOverflowContList(nsnull),
    mPrevOverflowCont(nsnull),
    mSentry(nsnull),
    mParent(aFrame),
    mSkipOverflowContainerChildren(aSkipOverflowContainerChildren),
    mWalkOOFFrames(aWalkOOFFrames)
{
  NS_PRECONDITION(aFrame, "null frame pointer");
  nsContainerFrame* next = static_cast<nsContainerFrame*>
                             (aFrame->GetNextInFlow());
  if (next) {
    mOverflowContList =
      next->GetPropTableFrames(aPresContext,
                               nsGkAtoms::overflowContainersProperty);
    if (mOverflowContList) {
      mParent = next;
      SetUpListWalker();
    }
  }
  if (!mOverflowContList) {
    mOverflowContList =
      mParent->GetPropTableFrames(aPresContext,
                                  nsGkAtoms::excessOverflowContainersProperty);
    if (mOverflowContList) {
      SetUpListWalker();
    }
  }
}

/**
 * Helper function to walk past overflow continuations whose prev-in-flow
 * isn't a normal child and to set mSentry and mPrevOverflowCont correctly.
 */
void
nsOverflowContinuationTracker::SetUpListWalker()
{
  NS_ASSERTION(!mSentry && !mPrevOverflowCont,
               "forgot to reset mSentry or mPrevOverflowCont");
  if (mOverflowContList) {
    nsIFrame* cur = mOverflowContList->FirstChild();
    if (mSkipOverflowContainerChildren) {
      while (cur && (cur->GetPrevInFlow()->GetStateBits()
                     & NS_FRAME_IS_OVERFLOW_CONTAINER)) {
        mPrevOverflowCont = cur;
        cur = cur->GetNextSibling();
      }
      while (cur && (!(cur->GetStateBits() & NS_FRAME_OUT_OF_FLOW)
                     == mWalkOOFFrames)) {
        mPrevOverflowCont = cur;
        cur = cur->GetNextSibling();
      }
    }
    if (cur) {
      mSentry = cur->GetPrevInFlow();
    }
  }
}

/**
 * Helper function to step forward through the overflow continuations list.
 * Sets mSentry and mPrevOverflowCont, skipping over OOF or non-OOF frames
 * as appropriate. May only be called when we have already set up an
 * mOverflowContList; mOverflowContList cannot be null.
 */
void
nsOverflowContinuationTracker::StepForward()
{
  NS_PRECONDITION(mOverflowContList, "null list");

  // Step forward
  if (mPrevOverflowCont) {
    mPrevOverflowCont = mPrevOverflowCont->GetNextSibling();
  }
  else {
    mPrevOverflowCont = mOverflowContList->FirstChild();
  }

  // Skip over oof or non-oof frames as appropriate
  if (mSkipOverflowContainerChildren) {
    nsIFrame* cur = mPrevOverflowCont->GetNextSibling();
    while (cur && (!(cur->GetStateBits() & NS_FRAME_OUT_OF_FLOW)
                   == mWalkOOFFrames)) {
      mPrevOverflowCont = cur;
      cur = cur->GetNextSibling();
    }
  }

  // Set up the sentry
  mSentry = (mPrevOverflowCont->GetNextSibling())
            ? mPrevOverflowCont->GetNextSibling()->GetPrevInFlow()
            : nsnull;
}

nsresult
nsOverflowContinuationTracker::Insert(nsIFrame*       aOverflowCont,
                                      nsReflowStatus& aReflowStatus)
{
  NS_PRECONDITION(aOverflowCont, "null frame pointer");
  NS_PRECONDITION(!mSkipOverflowContainerChildren || mWalkOOFFrames ==
                  !!(aOverflowCont->GetStateBits() & NS_FRAME_OUT_OF_FLOW),
                  "shouldn't insert frame that doesn't match walker type");
  NS_PRECONDITION(aOverflowCont->GetPrevInFlow(),
                  "overflow containers must have a prev-in-flow");
  nsresult rv = NS_OK;
  PRBool convertedToOverflowContainer = PR_FALSE;
  nsPresContext* presContext = aOverflowCont->PresContext();
  if (!mSentry || aOverflowCont != mSentry->GetNextInFlow()) {
    // Not in our list, so we need to add it
    if (aOverflowCont->GetStateBits() & NS_FRAME_IS_OVERFLOW_CONTAINER) {
      // aOverflowCont is in some other overflow container list,
      // steal it first
      NS_ASSERTION(!(mOverflowContList &&
                     mOverflowContList->ContainsFrame(aOverflowCont)),
                   "overflow containers out of order");
      rv = static_cast<nsContainerFrame*>(aOverflowCont->GetParent())
             ->StealFrame(presContext, aOverflowCont);
      NS_ENSURE_SUCCESS(rv, rv);
    }
    else {
      aOverflowCont->AddStateBits(NS_FRAME_IS_OVERFLOW_CONTAINER);
      convertedToOverflowContainer = PR_TRUE;
    }
    if (!mOverflowContList) {
      mOverflowContList = new nsFrameList();
      rv = mParent->SetPropTableFrames(presContext,
             mOverflowContList, nsGkAtoms::excessOverflowContainersProperty);
      NS_ENSURE_SUCCESS(rv, rv);
      SetUpListWalker();
    }
    if (aOverflowCont->GetParent() != mParent) {
      nsHTMLContainerFrame::ReparentFrameView(presContext, aOverflowCont,
                                              aOverflowCont->GetParent(),
                                              mParent);
    }
    mOverflowContList->InsertFrame(mParent, mPrevOverflowCont, aOverflowCont);
    aReflowStatus |= NS_FRAME_REFLOW_NEXTINFLOW;
  }

  // If we need to reflow it, mark it dirty
  if (aReflowStatus & NS_FRAME_REFLOW_NEXTINFLOW)
    aOverflowCont->AddStateBits(NS_FRAME_IS_DIRTY);

  // It's in our list, just step forward
  StepForward();
  NS_ASSERTION(mPrevOverflowCont == aOverflowCont ||
               (mSkipOverflowContainerChildren &&
                (mPrevOverflowCont->GetStateBits() & NS_FRAME_OUT_OF_FLOW) !=
                (aOverflowCont->GetStateBits() & NS_FRAME_OUT_OF_FLOW)),
              "OverflowContTracker in unexpected state");

  if (convertedToOverflowContainer) {
    // Convert all non-overflow-container continuations of aOverflowCont
    // into overflow containers and move them to our overflow
    // tracker. This preserves the invariant that the next-continuations
    // of an overflow container are also overflow containers.
    nsIFrame* f = aOverflowCont->GetNextContinuation();
    if (f && !(f->GetStateBits() & NS_FRAME_IS_OVERFLOW_CONTAINER)) {
      nsContainerFrame* parent = static_cast<nsContainerFrame*>(f->GetParent());
      rv = parent->StealFrame(presContext, f);
      NS_ENSURE_SUCCESS(rv, rv);
      Insert(f, aReflowStatus);
    }
  }
  return rv;
}

void
nsOverflowContinuationTracker::Finish(nsIFrame* aChild)
{
  NS_PRECONDITION(aChild, "null ptr");
  NS_PRECONDITION(aChild->GetNextInFlow(),
                "supposed to call Finish *before* deleting next-in-flow!");

  for (nsIFrame* f = aChild; f; f = f->GetNextInFlow()) {
    if (f == mSentry) {
      // Make sure we drop all references if this was the only frame
      // in the overflow containers list
      if (mOverflowContList->FirstChild() == f->GetNextInFlow()
          && !f->GetNextInFlow()->GetNextSibling()) {
        mOverflowContList = nsnull;
        mPrevOverflowCont = nsnull;
        mSentry = nsnull;
        mParent = static_cast<nsContainerFrame*>(f->GetParent());
        break;
      }
      else {
        // Step past aChild
        nsIFrame* prevOverflowCont = mPrevOverflowCont;
        StepForward();
        if (mPrevOverflowCont == f->GetNextInFlow()) {
          // Pull mPrevOverflowChild back to aChild's prevSibling:
          // aChild will be removed from our list by our caller
          mPrevOverflowCont = prevOverflowCont;
        }
      }
    }
  }
}

/////////////////////////////////////////////////////////////////////////////
// Debugging

#ifdef NS_DEBUG
NS_IMETHODIMP
nsContainerFrame::List(FILE* out, PRInt32 aIndent) const
{
  IndentBy(out, aIndent);
  ListTag(out);
#ifdef DEBUG_waterson
  fprintf(out, " [parent=%p]", static_cast<void*>(mParent));
#endif
  if (HasView()) {
    fprintf(out, " [view=%p]", static_cast<void*>(GetView()));
  }
  if (GetNextSibling()) {
    fprintf(out, " next=%p", static_cast<void*>(GetNextSibling()));
  }
  if (nsnull != GetPrevContinuation()) {
    fprintf(out, " prev-continuation=%p", static_cast<void*>(GetPrevContinuation()));
  }
  if (nsnull != GetNextContinuation()) {
    fprintf(out, " next-continuation=%p", static_cast<void*>(GetNextContinuation()));
  }
  void* IBsibling = GetProperty(nsGkAtoms::IBSplitSpecialSibling);
  if (IBsibling) {
    fprintf(out, " IBSplitSpecialSibling=%p", IBsibling);
  }
  void* IBprevsibling = GetProperty(nsGkAtoms::IBSplitSpecialPrevSibling);
  if (IBprevsibling) {
    fprintf(out, " IBSplitSpecialPrevSibling=%p", IBprevsibling);
  }
  fprintf(out, " {%d,%d,%d,%d}", mRect.x, mRect.y, mRect.width, mRect.height);
  if (0 != mState) {
    fprintf(out, " [state=%08x]", mState);
  }
  fprintf(out, " [content=%p]", static_cast<void*>(mContent));
  nsContainerFrame* f = const_cast<nsContainerFrame*>(this);
  if (f->HasOverflowRect()) {
    nsRect overflowArea = f->GetOverflowRect();
    fprintf(out, " [overflow=%d,%d,%d,%d]", overflowArea.x, overflowArea.y,
            overflowArea.width, overflowArea.height);
  }
  fprintf(out, " [sc=%p]", static_cast<void*>(mStyleContext));
  nsIAtom* pseudoTag = mStyleContext->GetPseudo();
  if (pseudoTag) {
    nsAutoString atomString;
    pseudoTag->ToString(atomString);
    fprintf(out, " pst=%s",
            NS_LossyConvertUTF16toASCII(atomString).get());
  }

  // Output the children
  nsIAtom* listName = nsnull;
  PRInt32 listIndex = 0;
  PRBool outputOneList = PR_FALSE;
  do {
    nsIFrame* kid = GetFirstChild(listName);
    if (nsnull != kid) {
      if (outputOneList) {
        IndentBy(out, aIndent);
      }
      outputOneList = PR_TRUE;
      nsAutoString tmp;
      if (nsnull != listName) {
        listName->ToString(tmp);
        fputs(NS_LossyConvertUTF16toASCII(tmp).get(), out);
      }
      fputs("<\n", out);
      while (nsnull != kid) {
        // Verify the child frame's parent frame pointer is correct
        NS_ASSERTION(kid->GetParent() == (nsIFrame*)this, "bad parent frame pointer");

        // Have the child frame list
        kid->List(out, aIndent + 1);
        kid = kid->GetNextSibling();
      }
      IndentBy(out, aIndent);
      fputs(">\n", out);
    }
    listName = GetAdditionalChildListName(listIndex++);
  } while(nsnull != listName);

  if (!outputOneList) {
    fputs("<>\n", out);
  }

  return NS_OK;
}
#endif
