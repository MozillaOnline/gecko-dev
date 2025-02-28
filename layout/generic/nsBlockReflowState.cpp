/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
// vim:cindent:ts=2:et:sw=2:
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
 *   Steve Clark <buster@netscape.com>
 *   Robert O'Callahan <roc+moz@cs.cmu.edu>
 *   L. David Baron <dbaron@dbaron.org>
 *   Mats Palmgren <mats.palmgren@bredband.net>
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

/* state used in reflow of block frames */

#include "nsBlockReflowContext.h"
#include "nsBlockReflowState.h"
#include "nsBlockFrame.h"
#include "nsLineLayout.h"
#include "nsPresContext.h"
#include "nsGkAtoms.h"
#include "nsIFrame.h"
#include "nsFrameManager.h"

#include "nsINameSpaceManager.h"


#ifdef DEBUG
#include "nsBlockDebugFlags.h"
#endif

nsBlockReflowState::nsBlockReflowState(const nsHTMLReflowState& aReflowState,
                                       nsPresContext* aPresContext,
                                       nsBlockFrame* aFrame,
                                       const nsHTMLReflowMetrics& aMetrics,
                                       PRBool aTopMarginRoot,
                                       PRBool aBottomMarginRoot,
                                       PRBool aBlockNeedsFloatManager)
  : mBlock(aFrame),
    mPresContext(aPresContext),
    mReflowState(aReflowState),
    mOverflowTracker(nsnull),
    mPrevBottomMargin(),
    mLineNumber(0),
    mFlags(0),
    mFloatBreakType(NS_STYLE_CLEAR_NONE)
{
  SetFlag(BRS_ISFIRSTINFLOW, aFrame->GetPrevInFlow() == nsnull);
  SetFlag(BRS_ISOVERFLOWCONTAINER,
          IS_TRUE_OVERFLOW_CONTAINER(aFrame));

  const nsMargin& borderPadding = BorderPadding();

  if (aTopMarginRoot || 0 != aReflowState.mComputedBorderPadding.top) {
    SetFlag(BRS_ISTOPMARGINROOT, PR_TRUE);
  }
  if (aBottomMarginRoot || 0 != aReflowState.mComputedBorderPadding.bottom) {
    SetFlag(BRS_ISBOTTOMMARGINROOT, PR_TRUE);
  }
  if (GetFlag(BRS_ISTOPMARGINROOT)) {
    SetFlag(BRS_APPLYTOPMARGIN, PR_TRUE);
  }
  if (aBlockNeedsFloatManager) {
    SetFlag(BRS_FLOAT_MGR, PR_TRUE);
  }
  
  mFloatManager = aReflowState.mFloatManager;

  NS_ASSERTION(mFloatManager,
               "FloatManager should be set in nsBlockReflowState" );
  if (mFloatManager) {
    // Translate into our content area and then save the 
    // coordinate system origin for later.
    mFloatManager->Translate(borderPadding.left, borderPadding.top);
    mFloatManager->GetTranslation(mFloatManagerX, mFloatManagerY);
    mFloatManager->PushState(&mFloatManagerStateBefore); // never popped
  }

  mReflowStatus = NS_FRAME_COMPLETE;

  mPresContext = aPresContext;
  mNextInFlow = static_cast<nsBlockFrame*>(mBlock->GetNextInFlow());

  NS_WARN_IF_FALSE(NS_UNCONSTRAINEDSIZE != aReflowState.ComputedWidth(),
                   "have unconstrained width; this should only result from "
                   "very large sizes, not attempts at intrinsic width "
                   "calculation");
  mContentArea.width = aReflowState.ComputedWidth();

  // Compute content area height. Unlike the width, if we have a
  // specified style height we ignore it since extra content is
  // managed by the "overflow" property. When we don't have a
  // specified style height then we may end up limiting our height if
  // the availableHeight is constrained (this situation occurs when we
  // are paginated).
  if (NS_UNCONSTRAINEDSIZE != aReflowState.availableHeight) {
    // We are in a paginated situation. The bottom edge is just inside
    // the bottom border and padding. The content area height doesn't
    // include either border or padding edge.
    mBottomEdge = aReflowState.availableHeight - borderPadding.bottom;
    mContentArea.height = NS_MAX(0, mBottomEdge - borderPadding.top);
  }
  else {
    // When we are not in a paginated situation then we always use
    // an constrained height.
    SetFlag(BRS_UNCONSTRAINEDHEIGHT, PR_TRUE);
    mContentArea.height = mBottomEdge = NS_UNCONSTRAINEDSIZE;
  }

  mY = borderPadding.top;

  mPrevChild = nsnull;
  mCurrentLine = aFrame->end_lines();

  mMinLineHeight = aReflowState.CalcLineHeight();
}

nsBlockReflowState::~nsBlockReflowState()
{
  NS_ASSERTION(mFloatContinuations.IsEmpty(),
               "Leaking float continuation frames");

  // Restore the coordinate system, unless the float manager is null,
  // which means it was just destroyed.
  if (mFloatManager) {
    const nsMargin& borderPadding = BorderPadding();
    mFloatManager->Translate(-borderPadding.left, -borderPadding.top);
  }

  if (GetFlag(BRS_PROPTABLE_FLOATCLIST)) {
    mBlock->UnsetProperty(nsGkAtoms::floatContinuationProperty);
  }
}

nsLineBox*
nsBlockReflowState::NewLineBox(nsIFrame* aFrame,
                               PRInt32 aCount,
                               PRBool aIsBlock)
{
  return NS_NewLineBox(mPresContext->PresShell(), aFrame, aCount, aIsBlock);
}

void
nsBlockReflowState::FreeLineBox(nsLineBox* aLine)
{
  if (aLine) {
    aLine->Destroy(mPresContext->PresShell());
  }
}

void
nsBlockReflowState::ComputeReplacedBlockOffsetsForFloats(nsIFrame* aFrame,
                                                         const nsRect& aFloatAvailableSpace,
                                                         nscoord& aLeftResult,
                                                         nscoord& aRightResult,
                                                         nsBlockFrame::
                                                      ReplacedElementWidthToClear
                                                                 *aReplacedWidth)
{
  // The frame is clueless about the float manager and therefore we
  // only give it free space. An example is a table frame - the
  // tables do not flow around floats.
  // However, we can let its margins intersect floats.
  NS_ASSERTION(aFloatAvailableSpace.x >= 0, "bad avail space rect x");
  NS_ASSERTION(aFloatAvailableSpace.width == 0 ||
               aFloatAvailableSpace.XMost() <= mContentArea.width,
               "bad avail space rect width");

  nscoord leftOffset, rightOffset;
  if (aFloatAvailableSpace.width == mContentArea.width) {
    // We don't need to compute margins when there are no floats around.
    leftOffset = 0;
    rightOffset = 0;
  } else {
    // We pass in aReplacedWidth to make handling outer table frames
    // work correctly.  For outer table frames, we need to subtract off
    // the margin that's going to be at the edge of them, since we're
    // dealing with margin that it's really the child's responsibility
    // to place.
    nsCSSOffsetState os(aFrame, mReflowState.rendContext, mContentArea.width);
    NS_ASSERTION(!aReplacedWidth ||
                 aFrame->GetType() == nsGkAtoms::tableOuterFrame ||
                 (aReplacedWidth->marginLeft  == os.mComputedMargin.left &&
                  aReplacedWidth->marginRight == os.mComputedMargin.right),
                 "unexpected aReplacedWidth");

    nscoord leftFloatXOffset = aFloatAvailableSpace.x;
    leftOffset = NS_MAX(leftFloatXOffset, os.mComputedMargin.left) -
                 (aReplacedWidth ? aReplacedWidth->marginLeft
                                 : os.mComputedMargin.left);
    leftOffset = NS_MAX(leftOffset, 0); // in case of negative margin
    nscoord rightFloatXOffset =
      mContentArea.width - aFloatAvailableSpace.XMost();
    rightOffset = NS_MAX(rightFloatXOffset, os.mComputedMargin.right) -
                  (aReplacedWidth ? aReplacedWidth->marginRight
                                  : os.mComputedMargin.right);
    rightOffset = NS_MAX(rightOffset, 0); // in case of negative margin
  }
  aLeftResult = leftOffset;
  aRightResult = rightOffset;
}

// Compute the amount of available space for reflowing a block frame
// at the current Y coordinate. This method assumes that
// GetAvailableSpace has already been called.
void
nsBlockReflowState::ComputeBlockAvailSpace(nsIFrame* aFrame,
                                           const nsStyleDisplay* aDisplay,
                                           const nsFlowAreaRect& aFloatAvailableSpace,
                                           PRBool aBlockAvoidsFloats,
                                           nsRect& aResult)
{
#ifdef REALLY_NOISY_REFLOW
  printf("CBAS frame=%p has floats %d\n",
         aFrame, aFloatAvailableSpace.mHasFloats);
#endif
  aResult.y = mY;
  aResult.height = GetFlag(BRS_UNCONSTRAINEDHEIGHT)
    ? NS_UNCONSTRAINEDSIZE
    : NS_MAX(0, mReflowState.availableHeight - mY);
  // mY might be greater than mBottomEdge if the block's top margin pushes
  // it off the page/column. Negative available height can confuse other code
  // and is nonsense in principle.

  const nsMargin& borderPadding = BorderPadding();

  // XXX Do we really want this condition to be this restrictive (i.e.,
  // more restrictive than it used to be)?  The |else| here is allowed
  // by the CSS spec, but only out of desperation given implementations,
  // and the behavior it leads to is quite undesirable (it can cause
  // things to become extremely narrow when they'd fit quite well a
  // little bit lower).  Should the else be a quirk or something that
  // applies to a specific set of frame classes and no new ones?
  // If we did that, then for those frames where the condition below is
  // true but nsBlockFrame::BlockCanIntersectFloats is false,
  // nsBlockFrame::WidthToClearPastFloats would need to use the
  // shrink-wrap formula, max(MIN_WIDTH, min(avail width, PREF_WIDTH))
  // rather than just using MIN_WIDTH.
  NS_ASSERTION(nsBlockFrame::BlockCanIntersectFloats(aFrame) == 
                 !aBlockAvoidsFloats,
               "unexpected replaced width");
  if (!aBlockAvoidsFloats) {
    if (aFloatAvailableSpace.mHasFloats) {
      // Use the float-edge property to determine how the child block
      // will interact with the float.
      const nsStyleBorder* borderStyle = aFrame->GetStyleBorder();
      switch (borderStyle->mFloatEdge) {
        default:
        case NS_STYLE_FLOAT_EDGE_CONTENT:  // content and only content does runaround of floats
          // The child block will flow around the float. Therefore
          // give it all of the available space.
          aResult.x = borderPadding.left;
          aResult.width = mContentArea.width;
          break;
        case NS_STYLE_FLOAT_EDGE_MARGIN:
          {
            // The child block's margins should be placed adjacent to,
            // but not overlap the float.
            aResult.x = aFloatAvailableSpace.mRect.x + borderPadding.left;
            aResult.width = aFloatAvailableSpace.mRect.width;
          }
          break;
      }
    }
    else {
      // Since there are no floats present the float-edge property
      // doesn't matter therefore give the block element all of the
      // available space since it will flow around the float itself.
      aResult.x = borderPadding.left;
      aResult.width = mContentArea.width;
    }
  }
  else {
    nsBlockFrame::ReplacedElementWidthToClear replacedWidthStruct;
    nsBlockFrame::ReplacedElementWidthToClear *replacedWidth = nsnull;
    if (aFrame->GetType() == nsGkAtoms::tableOuterFrame) {
      replacedWidth = &replacedWidthStruct;
      replacedWidthStruct =
        nsBlockFrame::WidthToClearPastFloats(*this, aFloatAvailableSpace.mRect,
                                             aFrame);
    }

    nscoord leftOffset, rightOffset;
    ComputeReplacedBlockOffsetsForFloats(aFrame, aFloatAvailableSpace.mRect,
                                         leftOffset, rightOffset,
                                         replacedWidth);
    aResult.x = borderPadding.left + leftOffset;
    aResult.width = mContentArea.width - leftOffset - rightOffset;
  }

#ifdef REALLY_NOISY_REFLOW
  printf("  CBAS: result %d %d %d %d\n", aResult.x, aResult.y, aResult.width, aResult.height);
#endif
}

nsFlowAreaRect
nsBlockReflowState::GetFloatAvailableSpaceWithState(
                      nscoord aY, PRBool aRelaxHeightConstraint,
                      nsFloatManager::SavedState *aState) const
{
#ifdef DEBUG
  // Verify that the caller setup the coordinate system properly
  nscoord wx, wy;
  mFloatManager->GetTranslation(wx, wy);
  NS_ASSERTION((wx == mFloatManagerX) && (wy == mFloatManagerY),
               "bad coord system");
#endif

  nsFlowAreaRect result =
    mFloatManager->GetFlowArea(aY - BorderPadding().top, 
                               nsFloatManager::BAND_FROM_POINT,
                               aRelaxHeightConstraint ? nscoord_MAX
                                                      : mContentArea.height,
                               mContentArea.width, aState);
  // Keep the width >= 0 for compatibility with nsSpaceManager.
  if (result.mRect.width < 0)
    result.mRect.width = 0;

#ifdef DEBUG
  if (nsBlockFrame::gNoisyReflow) {
    nsFrame::IndentBy(stdout, nsBlockFrame::gNoiseIndent);
    printf("GetAvailableSpace: band=%d,%d,%d,%d hasfloats=%d\n",
           result.mRect.x, result.mRect.y, result.mRect.width,
           result.mRect.height, result.mHasFloats);
  }
#endif
  return result;
}

nsFlowAreaRect
nsBlockReflowState::GetFloatAvailableSpaceForHeight(
                      nscoord aY, nscoord aHeight,
                      nsFloatManager::SavedState *aState) const
{
#ifdef DEBUG
  // Verify that the caller setup the coordinate system properly
  nscoord wx, wy;
  mFloatManager->GetTranslation(wx, wy);
  NS_ASSERTION((wx == mFloatManagerX) && (wy == mFloatManagerY),
               "bad coord system");
#endif

  nsFlowAreaRect result =
    mFloatManager->GetFlowArea(aY - BorderPadding().top, 
                               nsFloatManager::WIDTH_WITHIN_HEIGHT,
                               aHeight, mContentArea.width, aState);
  // Keep the width >= 0 for compatibility with nsSpaceManager.
  if (result.mRect.width < 0)
    result.mRect.width = 0;

#ifdef DEBUG
  if (nsBlockFrame::gNoisyReflow) {
    nsFrame::IndentBy(stdout, nsBlockFrame::gNoiseIndent);
    printf("GetAvailableSpaceForHeight: space=%d,%d,%d,%d hasfloats=%d\n",
           result.mRect.x, result.mRect.y, result.mRect.width,
           result.mRect.height, result.mHasFloats);
  }
#endif
  return result;
}

/*
 * Reconstruct the vertical margin before the line |aLine| in order to
 * do an incremental reflow that begins with |aLine| without reflowing
 * the line before it.  |aLine| may point to the fencepost at the end of
 * the line list, and it is used this way since we (for now, anyway)
 * always need to recover margins at the end of a block.
 *
 * The reconstruction involves walking backward through the line list to
 * find any collapsed margins preceding the line that would have been in
 * the reflow state's |mPrevBottomMargin| when we reflowed that line in
 * a full reflow (under the rule in CSS2 that all adjacent vertical
 * margins of blocks collapse).
 */
void
nsBlockReflowState::ReconstructMarginAbove(nsLineList::iterator aLine)
{
  mPrevBottomMargin.Zero();
  nsBlockFrame *block = mBlock;

  nsLineList::iterator firstLine = block->begin_lines();
  for (;;) {
    --aLine;
    if (aLine->IsBlock()) {
      mPrevBottomMargin = aLine->GetCarriedOutBottomMargin();
      break;
    }
    if (!aLine->IsEmpty()) {
      break;
    }
    if (aLine == firstLine) {
      // If the top margin was carried out (and thus already applied),
      // set it to zero.  Either way, we're done.
      if (!GetFlag(BRS_ISTOPMARGINROOT)) {
        mPrevBottomMargin.Zero();
      }
      break;
    }
  }
}

void
nsBlockReflowState::SetupFloatContinuationList()
{
  if (!GetFlag(BRS_PROPTABLE_FLOATCLIST)) {
    mBlock->SetProperty(nsGkAtoms::floatContinuationProperty,
                        &mFloatContinuations, nsnull);
    SetFlag(BRS_PROPTABLE_FLOATCLIST, PR_TRUE);
  }
}

/**
 * Restore information about floats into the float manager for an
 * incremental reflow, and simultaneously push the floats by
 * |aDeltaY|, which is the amount |aLine| was pushed relative to its
 * parent.  The recovery of state is one of the things that makes
 * incremental reflow O(N^2) and this state should really be kept
 * around, attached to the frame tree.
 */
void
nsBlockReflowState::RecoverFloats(nsLineList::iterator aLine,
                                  nscoord aDeltaY)
{
  if (aLine->HasFloats()) {
    // Place the floats into the space-manager again. Also slide
    // them, just like the regular frames on the line.
    nsFloatCache* fc = aLine->GetFirstFloat();
    while (fc) {
      nsIFrame* floatFrame = fc->mFloat;
      if (aDeltaY != 0) {
        nsPoint p = floatFrame->GetPosition();
        floatFrame->SetPosition(nsPoint(p.x, p.y + aDeltaY));
        nsContainerFrame::PositionFrameView(floatFrame);
        nsContainerFrame::PositionChildViews(floatFrame);
      }
#ifdef DEBUG
      if (nsBlockFrame::gNoisyReflow || nsBlockFrame::gNoisyFloatManager) {
        nscoord tx, ty;
        mFloatManager->GetTranslation(tx, ty);
        nsFrame::IndentBy(stdout, nsBlockFrame::gNoiseIndent);
        printf("RecoverFloats: txy=%d,%d (%d,%d) ",
               tx, ty, mFloatManagerX, mFloatManagerY);
        nsFrame::ListTag(stdout, floatFrame);
        nsRect region = nsFloatManager::GetRegionFor(floatFrame);
        printf(" aDeltaY=%d region={%d,%d,%d,%d}\n",
               aDeltaY, region.x, region.y, region.width, region.height);
      }
#endif
      mFloatManager->AddFloat(floatFrame,
                              nsFloatManager::GetRegionFor(floatFrame));
      fc = fc->Next();
    }
  } else if (aLine->IsBlock()) {
    nsBlockFrame::RecoverFloatsFor(aLine->mFirstChild, *mFloatManager);
  }
}

/**
 * Everything done in this function is done O(N) times for each pass of
 * reflow so it is O(N*M) where M is the number of incremental reflow
 * passes.  That's bad.  Don't do stuff here.
 *
 * When this function is called, |aLine| has just been slid by |aDeltaY|
 * and the purpose of RecoverStateFrom is to ensure that the
 * nsBlockReflowState is in the same state that it would have been in
 * had the line just been reflowed.
 *
 * Most of the state recovery that we have to do involves floats.
 */
void
nsBlockReflowState::RecoverStateFrom(nsLineList::iterator aLine,
                                     nscoord aDeltaY)
{
  // Make the line being recovered the current line
  mCurrentLine = aLine;

  // Place floats for this line into the float manager
  if (aLine->HasFloats() || aLine->IsBlock()) {
    // Undo border/padding translation since the nsFloatCache's
    // coordinates are relative to the frame not relative to the
    // border/padding.
    const nsMargin& bp = BorderPadding();
    mFloatManager->Translate(-bp.left, -bp.top);

    RecoverFloats(aLine, aDeltaY);

#ifdef DEBUG
    if (nsBlockFrame::gNoisyReflow || nsBlockFrame::gNoisyFloatManager) {
      mFloatManager->List(stdout);
    }
#endif
    // And then put the translation back again
    mFloatManager->Translate(bp.left, bp.top);
  }
}

// This is called by the line layout's AddFloat method when a
// place-holder frame is reflowed in a line. If the float is a
// left-most child (it's x coordinate is at the line's left margin)
// then the float is place immediately, otherwise the float
// placement is deferred until the line has been reflowed.

// XXXldb This behavior doesn't quite fit with CSS1 and CSS2 --
// technically we're supposed let the current line flow around the
// float as well unless it won't fit next to what we already have.
// But nobody else implements it that way...
PRBool
nsBlockReflowState::AddFloat(nsLineLayout*       aLineLayout,
                             nsIFrame*           aFloat,
                             nscoord             aAvailableWidth,
                             nsReflowStatus&     aReflowStatus)
{
  NS_PRECONDITION(!aLineLayout || mBlock->end_lines() != mCurrentLine, "null ptr");
  NS_PRECONDITION(aFloat->GetStateBits() & NS_FRAME_OUT_OF_FLOW,
                  "aFloat must be an out-of-flow frame");

  // Set the geometric parent of the float
  aFloat->SetParent(mBlock);

  aReflowStatus = NS_FRAME_COMPLETE;

  // Because we are in the middle of reflowing a placeholder frame
  // within a line (and possibly nested in an inline frame or two
  // that's a child of our block) we need to restore the space
  // manager's translation to the space that the block resides in
  // before placing the float.
  nscoord ox, oy;
  mFloatManager->GetTranslation(ox, oy);
  nscoord dx = ox - mFloatManagerX;
  nscoord dy = oy - mFloatManagerY;
  mFloatManager->Translate(-dx, -dy);

  PRBool placed;

  // Now place the float immediately if possible. Otherwise stash it
  // away in mPendingFloats and place it later.
  // If one or more floats has already been pushed to the next line,
  // don't let this one go on the current line, since that would violate
  // float ordering.
  nsRect floatAvailableSpace = GetFloatAvailableSpace().mRect;
  if (!aLineLayout ||
      (mBelowCurrentLineFloats.IsEmpty() &&
       (aLineLayout->LineIsEmpty() ||
        mBlock->ComputeFloatWidth(*this, floatAvailableSpace, aFloat)
        <= aAvailableWidth))) {
    // And then place it
    // force it to fit if we're at the top of the block and we can't
    // break before this
    PRBool forceFit = !aLineLayout ||
                      (IsAdjacentWithTop() && !aLineLayout->LineIsBreakable());
    placed = FlowAndPlaceFloat(aFloat, aReflowStatus, forceFit);
    NS_ASSERTION(placed || !forceFit,
                 "If we asked for force-fit, it should have been placed");
    if (forceFit || (placed && !NS_FRAME_IS_TRUNCATED(aReflowStatus))) {
      // Pass on updated available space to the current inline reflow engine
      nsFlowAreaRect floatAvailSpace =
        GetFloatAvailableSpace(mY, forceFit);
      nsRect availSpace(nsPoint(floatAvailSpace.mRect.x + BorderPadding().left,
                                mY),
                        floatAvailSpace.mRect.Size());
      if (aLineLayout) {
        aLineLayout->UpdateBand(availSpace, aFloat);
        // Record this float in the current-line list
        mCurrentLineFloats.Append(mFloatCacheFreeList.Alloc(aFloat));
      }
      // If we can't break here, hide the fact that it's truncated
      // XXX We can probably do this more cleanly
      aReflowStatus &= ~NS_FRAME_TRUNCATED;
    }
    else {
      if (IsAdjacentWithTop()) {
        // Pushing the line to the next page won't give us any more space;
        // therefore, we break.
        NS_ASSERTION(aLineLayout->LineIsBreakable(),
                     "We can't get here unless forceFit is false");
        aReflowStatus = NS_INLINE_LINE_BREAK_BEFORE();
      } else {
        // Make sure we propagate the truncated status; this signals the
        // block to push the line to the next page.
        aReflowStatus |= NS_FRAME_TRUNCATED;
      }
    }
  }
  else {
    // Always claim to be placed; we don't know whether we fit yet, so we
    // deal with this in PlaceBelowCurrentLineFloats
    placed = PR_TRUE;
    // This float will be placed after the line is done (it is a
    // below-current-line float).
    mBelowCurrentLineFloats.Append(mFloatCacheFreeList.Alloc(aFloat));
  }

  // Restore coordinate system
  mFloatManager->Translate(dx, dy);

  return placed;
}

PRBool
nsBlockReflowState::CanPlaceFloat(const nsSize& aFloatSize, PRUint8 aFloats,
                                  const nsFlowAreaRect& aFloatAvailableSpace,
                                  PRBool aForceFit)
{
  // If the current Y coordinate is not impacted by any floats
  // then by definition the float fits.
  PRBool result = PR_TRUE;
  if (aFloatAvailableSpace.mHasFloats) {
    // XXX We should allow overflow by up to half a pixel here (bug 21193).
    if (aFloatAvailableSpace.mRect.width < aFloatSize.width) {
      // The available width is too narrow (and it's been impacted by a
      // prior float)
      result = PR_FALSE;
    }
  }

  if (!result)
    return result;

  // At this point we know that there is enough horizontal space for
  // the float (somewhere). Lets see if there is enough vertical
  // space.
  if (NSCoordGreaterThan(aFloatSize.height,
                         aFloatAvailableSpace.mRect.height)) {
    // The available height is too short. However, it's possible that
    // there is enough open space below which is not impacted by a
    // float.
    //
    // Compute the X coordinate for the float based on its float
    // type, assuming it's placed on the current line. This is
    // where the float will be placed horizontally if it can go
    // here.
    nscoord xa;
    if (NS_STYLE_FLOAT_LEFT == aFloats) {
      xa = aFloatAvailableSpace.mRect.x;
    }
    else {
      xa = aFloatAvailableSpace.mRect.XMost() - aFloatSize.width;

      // In case the float is too big, don't go past the left edge
      // XXXldb This seems wrong, but we might want to fix bug 6976
      // first.
      if (xa < aFloatAvailableSpace.mRect.x) {
        xa = aFloatAvailableSpace.mRect.x;
      }
    }
    nscoord xb = xa + aFloatSize.width;

    // Calculate the top and bottom y coordinates, again assuming
    // that the float is placed on the current line.
    const nsMargin& borderPadding = BorderPadding();
    nscoord ya = mY - borderPadding.top;
    if (ya < 0) {
      // CSS2 spec, 9.5.1 rule [4]: "A floating box's outer top may not
      // be higher than the top of its containing block."  (Since the
      // containing block is the content edge of the block box, this
      // means the margin edge of the float can't be higher than the
      // content edge of the block that contains it.)
      ya = 0;
    }
    nscoord yb = ya + aFloatSize.height;

    nscoord saveY = mY;
    nsFlowAreaRect floatAvailableSpace(aFloatAvailableSpace);
    for (;;) {
      // Get the available space at the new Y coordinate
      if (floatAvailableSpace.mRect.height <= 0) {
        // there is no more available space. We lose.
        result = PR_FALSE;
        break;
      }

      mY += floatAvailableSpace.mRect.height;
      floatAvailableSpace = GetFloatAvailableSpace(mY, aForceFit);

      if (floatAvailableSpace.mHasFloats) {
        if (xa < floatAvailableSpace.mRect.x ||
            xb > floatAvailableSpace.mRect.XMost()) {
          // The float can't go here.
          result = PR_FALSE;
          break;
        }
      }

      // See if there is now enough height for the float.
      if (yb <= mY + floatAvailableSpace.mRect.height) {
        // Winner. The bottom Y coordinate of the float is in
        // this band.
        break;
      }
    }

    // Restore Y coordinate
    mY = saveY;
  }

  return result;
}

PRBool
nsBlockReflowState::FlowAndPlaceFloat(nsIFrame*       aFloat,
                                      nsReflowStatus& aReflowStatus,
                                      PRBool          aForceFit)
{
  aReflowStatus = NS_FRAME_COMPLETE;
  // Save away the Y coordinate before placing the float. We will
  // restore mY at the end after placing the float. This is
  // necessary because any adjustments to mY during the float
  // placement are for the float only, not for any non-floating
  // content.
  nscoord saveY = mY;

  // Grab the float's display information
  const nsStyleDisplay* floatDisplay = aFloat->GetStyleDisplay();

  // The float's old region, so we can propagate damage.
  nsRect oldRegion = nsFloatManager::GetRegionFor(aFloat);

  // Enforce CSS2 9.5.1 rule [2], i.e., make sure that a float isn't
  // ``above'' another float that preceded it in the flow.
  mY = NS_MAX(mFloatManager->GetLowestFloatTop() + BorderPadding().top, mY);

  // See if the float should clear any preceding floats...
  // XXX We need to mark this float somehow so that it gets reflowed
  // when floats are inserted before it.
  if (NS_STYLE_CLEAR_NONE != floatDisplay->mBreakType) {
    // XXXldb Does this handle vertical margins correctly?
    mY = ClearFloats(mY, floatDisplay->mBreakType);
  }
    // Get the band of available space
  nsFlowAreaRect floatAvailableSpace = GetFloatAvailableSpace(mY, aForceFit);

  NS_ASSERTION(aFloat->GetParent() == mBlock,
               "Float frame has wrong parent");

  // Reflow the float
  nsMargin floatMargin; // computed margin
  mBlock->ReflowFloat(*this, floatAvailableSpace.mRect, aFloat,
                      floatMargin, aReflowStatus);
  if (aFloat->GetPrevInFlow())
    floatMargin.top = 0;
  if (NS_FRAME_IS_NOT_COMPLETE(aReflowStatus))
    floatMargin.bottom = 0;

#ifdef DEBUG
  if (nsBlockFrame::gNoisyReflow) {
    nsRect region = aFloat->GetRect();
    nsFrame::IndentBy(stdout, nsBlockFrame::gNoiseIndent);
    printf("flowed float: ");
    nsFrame::ListTag(stdout, aFloat);
    printf(" (%d,%d,%d,%d)\n",
	   region.x, region.y, region.width, region.height);
  }
#endif

  nsSize floatSize = aFloat->GetSize() +
                     nsSize(floatMargin.LeftRight(), floatMargin.TopBottom());

  // Find a place to place the float. The CSS2 spec doesn't want
  // floats overlapping each other or sticking out of the containing
  // block if possible (CSS2 spec section 9.5.1, see the rule list).
  NS_ASSERTION((NS_STYLE_FLOAT_LEFT == floatDisplay->mFloats) ||
	       (NS_STYLE_FLOAT_RIGHT == floatDisplay->mFloats),
	       "invalid float type");

  // Can the float fit here?
  PRBool keepFloatOnSameLine = PR_FALSE;

  while (!CanPlaceFloat(floatSize, floatDisplay->mFloats, floatAvailableSpace,
                        aForceFit)) {
    if (floatAvailableSpace.mRect.height <= 0) {
      // No space, nowhere to put anything.
      mY = saveY;
      return PR_FALSE;
    }

    // Nope. try to advance to the next band.
    if (NS_STYLE_DISPLAY_TABLE != floatDisplay->mDisplay ||
          eCompatibility_NavQuirks != mPresContext->CompatibilityMode() ) {

      mY += floatAvailableSpace.mRect.height;
      floatAvailableSpace = GetFloatAvailableSpace(mY, aForceFit);
    } else {
      // This quirk matches the one in nsBlockFrame::ReflowFloat
      // IE handles float tables in a very special way

      // see if the previous float is also a table and has "align"
      nsFloatCache* fc = mCurrentLineFloats.Head();
      nsIFrame* prevFrame = nsnull;
      while (fc) {
        if (fc->mFloat == aFloat) {
          break;
        }
        prevFrame = fc->mFloat;
        fc = fc->Next();
      }
      
      if(prevFrame) {
        //get the frame type
        if (nsGkAtoms::tableOuterFrame == prevFrame->GetType()) {
          //see if it has "align="
          // IE makes a difference between align and he float property
          nsIContent* content = prevFrame->GetContent();
          if (content) {
            // we're interested only if previous frame is align=left
            // IE messes things up when "right" (overlapping frames) 
            if (content->AttrValueIs(kNameSpaceID_None, nsGkAtoms::align,
                                     NS_LITERAL_STRING("left"), eIgnoreCase)) {
              keepFloatOnSameLine = PR_TRUE;
              // don't advance to next line (IE quirkie behaviour)
              // it breaks rule CSS2/9.5.1/1, but what the hell
              // since we cannot evangelize the world
              break;
            }
          }
        }
      }

      // the table does not fit anymore in this line so advance to next band 
      mY += floatAvailableSpace.mRect.height;
      floatAvailableSpace = GetFloatAvailableSpace(mY, aForceFit);
      // reflow the float again now since we have more space
      // XXXldb We really don't need to Reflow in a loop, we just need
      // to ComputeSize in a loop (once ComputeSize depends on
      // availableWidth, which should make this work again).
      mBlock->ReflowFloat(*this, floatAvailableSpace.mRect, aFloat,
                          floatMargin, aReflowStatus);
      // Get the floats bounding box and margin information
      floatSize = aFloat->GetSize() +
                     nsSize(floatMargin.LeftRight(), floatMargin.TopBottom());
    }
  }
  // If the float is continued, it will get the same absolute x value as its prev-in-flow

  // We don't worry about the geometry of the prev in flow, let the continuation
  // place and size itself as required.

  // Assign an x and y coordinate to the float. Note that the x,y
  // coordinates are computed <b>relative to the translation in the
  // spacemanager</b> which means that the impacted region will be
  // <b>inside</b> the border/padding area.
  nscoord floatX, floatY;
  if (NS_STYLE_FLOAT_LEFT == floatDisplay->mFloats) {
    floatX = floatAvailableSpace.mRect.x;
  }
  else {
    if (!keepFloatOnSameLine) {
      floatX = floatAvailableSpace.mRect.XMost() - floatSize.width;
    } 
    else {
      // this is the IE quirk (see few lines above)
      // the table is kept in the same line: don't let it overlap the
      // previous float 
      floatX = floatAvailableSpace.mRect.x;
    }
  }
  const nsMargin& borderPadding = BorderPadding();
  floatY = mY - borderPadding.top;
  if (floatY < 0) {
    // CSS2 spec, 9.5.1 rule [4]: "A floating box's outer top may not
    // be higher than the top of its containing block."  (Since the
    // containing block is the content edge of the block box, this
    // means the margin edge of the float can't be higher than the
    // content edge of the block that contains it.)
    floatY = 0;
  }

  // Calculate the actual origin of the float frame's border rect
  // relative to the parent block; floatX/Y must be converted from space-manager
  // coordinates to parent coordinates, and the margin must be added in
  // to get the border rect
  nsPoint origin(borderPadding.left + floatMargin.left + floatX,
                 borderPadding.top + floatMargin.top + floatY);

  // If float is relatively positioned, factor that in as well
  origin += aFloat->GetRelativeOffset(floatDisplay);

  // Position the float and make sure and views are properly
  // positioned. We need to explicitly position its child views as
  // well, since we're moving the float after flowing it.
  aFloat->SetPosition(origin);
  nsContainerFrame::PositionFrameView(aFloat);
  nsContainerFrame::PositionChildViews(aFloat);

  // Update the float combined area state
  nsRect combinedArea = aFloat->GetOverflowRect() + origin;

  // XXX Floats should really just get invalidated here if necessary
  mFloatCombinedArea.UnionRect(combinedArea, mFloatCombinedArea);

  // Place the float in the float manager
  // calculate region
  nsRect region = nsFloatManager::CalculateRegionFor(aFloat, floatMargin);
  // if the float split, then take up all of the vertical height
  if (NS_FRAME_IS_NOT_COMPLETE(aReflowStatus) &&
      (NS_UNCONSTRAINEDSIZE != mContentArea.height)) {
    region.height = NS_MAX(region.height, mContentArea.height - floatY);
  }
  nsresult rv =
  // spacemanager translation is inset by the border+padding.
  mFloatManager->AddFloat(aFloat,
                          region - nsPoint(borderPadding.left, borderPadding.top));
  NS_ABORT_IF_FALSE(NS_SUCCEEDED(rv), "bad float placement");
  // store region
  rv = nsFloatManager::StoreRegionFor(aFloat, region);
  NS_ABORT_IF_FALSE(NS_SUCCEEDED(rv), "float region storage failed");

  // If the float's dimensions have changed, note the damage in the
  // float manager.
  if (region != oldRegion) {
    // XXXwaterson conservative: we could probably get away with noting
    // less damage; e.g., if only height has changed, then only note the
    // area into which the float has grown or from which the float has
    // shrunk.
    nscoord top = NS_MIN(region.y, oldRegion.y) - borderPadding.top;
    nscoord bottom = NS_MAX(region.YMost(), oldRegion.YMost()) - borderPadding.left;
    mFloatManager->IncludeInDamage(top, bottom);
  }

#ifdef NOISY_FLOATMANAGER
  nscoord tx, ty;
  mFloatManager->GetTranslation(tx, ty);
  nsFrame::ListTag(stdout, mBlock);
  printf(": FlowAndPlaceFloat: AddFloat: txy=%d,%d (%d,%d) {%d,%d,%d,%d}\n",
         tx, ty, mFloatManagerX, mFloatManagerY,
         region.x, region.y, region.width, region.height);
#endif

  // Now restore mY
  mY = saveY;

#ifdef DEBUG
  if (nsBlockFrame::gNoisyReflow) {
    nsRect r = aFloat->GetRect();
    nsFrame::IndentBy(stdout, nsBlockFrame::gNoiseIndent);
    printf("placed float: ");
    nsFrame::ListTag(stdout, aFloat);
    printf(" %d,%d,%d,%d\n", r.x, r.y, r.width, r.height);
  }
#endif

  return PR_TRUE;
}

/**
 * Place below-current-line floats.
 */
PRBool
nsBlockReflowState::PlaceBelowCurrentLineFloats(nsFloatCacheFreeList& aList, PRBool aForceFit)
{
  nsFloatCache* fc = aList.Head();
  while (fc) {
    {
#ifdef DEBUG
      if (nsBlockFrame::gNoisyReflow) {
        nsFrame::IndentBy(stdout, nsBlockFrame::gNoiseIndent);
        printf("placing bcl float: ");
        nsFrame::ListTag(stdout, fc->mFloat);
        printf("\n");
      }
#endif
      // Place the float
      nsReflowStatus reflowStatus;
      PRBool placed = FlowAndPlaceFloat(fc->mFloat, reflowStatus, aForceFit);
      NS_ASSERTION(placed || !aForceFit,
                   "If we're in force-fit mode, we should have placed the float");

      if (!placed || (NS_FRAME_IS_TRUNCATED(reflowStatus) && !aForceFit)) {
        // return before processing all of the floats, since the line will be pushed.
        return PR_FALSE;
      }
      else if (!NS_FRAME_IS_FULLY_COMPLETE(reflowStatus)) {
        // Create a continuation for the incomplete float
        nsresult rv = mBlock->SplitFloat(*this, fc->mFloat, reflowStatus);
        if (NS_FAILED(rv))
          return PR_FALSE;
      } else {
        // XXX We could deal with truncated frames better by breaking before
        // the associated placeholder
        NS_WARN_IF_FALSE(!NS_FRAME_IS_TRUNCATED(reflowStatus),
                         "This situation currently leads to data not printing");
        // Float is complete.
      }
    }
    fc = fc->Next();
  }
  return PR_TRUE;
}

nscoord
nsBlockReflowState::ClearFloats(nscoord aY, PRUint8 aBreakType,
                                nsIFrame *aReplacedBlock)
{
#ifdef DEBUG
  if (nsBlockFrame::gNoisyReflow) {
    nsFrame::IndentBy(stdout, nsBlockFrame::gNoiseIndent);
    printf("clear floats: in: aY=%d(%d)\n",
           aY, aY - BorderPadding().top);
  }
#endif

#ifdef NOISY_FLOAT_CLEARING
  printf("nsBlockReflowState::ClearFloats: aY=%d breakType=%d\n",
         aY, aBreakType);
  mFloatManager->List(stdout);
#endif
  
  const nsMargin& bp = BorderPadding();
  nscoord newY = aY;

  if (aBreakType != NS_STYLE_CLEAR_NONE) {
    newY = bp.top + mFloatManager->ClearFloats(newY - bp.top, aBreakType);
  }

  if (aReplacedBlock) {
    for (;;) {
      nsFlowAreaRect floatAvailableSpace = 
        GetFloatAvailableSpace(newY, PR_FALSE);
      nsBlockFrame::ReplacedElementWidthToClear replacedWidth =
        nsBlockFrame::WidthToClearPastFloats(*this, floatAvailableSpace.mRect,
                                             aReplacedBlock);
      if (!floatAvailableSpace.mHasFloats ||
          NS_MAX(floatAvailableSpace.mRect.x, replacedWidth.marginLeft) +
            replacedWidth.borderBoxWidth +
            NS_MAX(mContentArea.width -
                     NS_MIN(mContentArea.width,
                            floatAvailableSpace.mRect.XMost()),
                   replacedWidth.marginRight) <=
          mContentArea.width) {
        break;
      }
      // See the analogous code for inlines in nsBlockFrame::DoReflowInlineFrames
      if (floatAvailableSpace.mRect.height > 0) {
        // See if there's room in the next band.
        newY += floatAvailableSpace.mRect.height;
      } else {
        if (mReflowState.availableHeight != NS_UNCONSTRAINEDSIZE) {
          // Stop trying to clear here; we'll just get pushed to the
          // next column or page and try again there.
          break;
        }
        NS_NOTREACHED("avail space rect with zero height!");
        newY += 1;
      }
    }
  }

#ifdef DEBUG
  if (nsBlockFrame::gNoisyReflow) {
    nsFrame::IndentBy(stdout, nsBlockFrame::gNoiseIndent);
    printf("clear floats: out: y=%d(%d)\n", newY, newY - bp.top);
  }
#endif

  return newY;
}

