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

#ifndef nsBlockReflowState_h__
#define nsBlockReflowState_h__

#include "nsFloatManager.h"
#include "nsLineBox.h"
#include "nsFrameList.h"
#include "nsBlockFrame.h"

  // block reflow state flags
#define BRS_UNCONSTRAINEDHEIGHT   0x00000001
#define BRS_ISTOPMARGINROOT       0x00000002  // Is this frame a root for top/bottom margin collapsing?
#define BRS_ISBOTTOMMARGINROOT    0x00000004
#define BRS_APPLYTOPMARGIN        0x00000008  // See ShouldApplyTopMargin
#define BRS_ISFIRSTINFLOW         0x00000010
// Set when mLineAdjacentToTop is valid
#define BRS_HAVELINEADJACENTTOTOP 0x00000020
// Set when the block has the equivalent of NS_BLOCK_FLOAT_MGR
#define BRS_FLOAT_MGR             0x00000040
// Set when nsLineLayout::LineIsEmpty was true at the end of reflowing
// the current line
#define BRS_LINE_LAYOUT_EMPTY     0x00000080
#define BRS_ISOVERFLOWCONTAINER   0x00000100
// Our mFloatContinuations list is stored on the blocks' proptable
#define BRS_PROPTABLE_FLOATCLIST  0x00000200
#define BRS_LASTFLAG              BRS_PROPTABLE_FLOATCLIST

class nsBlockReflowState {
public:
  nsBlockReflowState(const nsHTMLReflowState& aReflowState,
                     nsPresContext* aPresContext,
                     nsBlockFrame* aFrame,
                     const nsHTMLReflowMetrics& aMetrics,
                     PRBool aTopMarginRoot, PRBool aBottomMarginRoot,
                     PRBool aBlockNeedsFloatManager);

  ~nsBlockReflowState();

  /**
   * Get the available reflow space (the area not occupied by floats)
   * for the current y coordinate. The available space is relative to
   * our coordinate system, which is the content box, with (0, 0) in the
   * upper left.
   *
   * Returns whether there are floats present at the given vertical
   * coordinate and within the width of the content rect.
   */
  nsFlowAreaRect GetFloatAvailableSpace() const
    { return GetFloatAvailableSpace(mY, PR_FALSE); }
  nsFlowAreaRect GetFloatAvailableSpace(nscoord aY,
                                        PRBool aRelaxHeightConstraint) const
    { return GetFloatAvailableSpaceWithState(aY, aRelaxHeightConstraint,
                                             nsnull); }
  nsFlowAreaRect
    GetFloatAvailableSpaceWithState(nscoord aY, PRBool aRelaxHeightConstraint,
                                    nsFloatManager::SavedState *aState) const;
  nsFlowAreaRect
    GetFloatAvailableSpaceForHeight(nscoord aY, nscoord aHeight,
                                    nsFloatManager::SavedState *aState) const;

  /*
   * The following functions all return PR_TRUE if they were able to
   * place the float, PR_FALSE if the float did not fit in available
   * space.
   * aLineLayout is null when we are reflowing float continuations (because
   * they are not associated with a line box).
   */
  PRBool AddFloat(nsLineLayout*       aLineLayout,
                  nsIFrame*           aFloat,
                  nscoord             aAvailableWidth,
                  nsReflowStatus&     aReflowStatus);
  PRBool CanPlaceFloat(const nsSize& aFloatSize, PRUint8 aFloats,
                       const nsFlowAreaRect& aFloatAvailableSpace,
                       PRBool aForceFit);
  PRBool FlowAndPlaceFloat(nsIFrame*       aFloat,
                           nsReflowStatus& aReflowStatus,
                           PRBool          aForceFit);
  PRBool PlaceBelowCurrentLineFloats(nsFloatCacheFreeList& aFloats, PRBool aForceFit);

  // Returns the first coordinate >= aY that clears the
  // floats indicated by aBreakType and has enough width between floats
  // (or no floats remaining) to accomodate aReplacedBlock.
  nscoord ClearFloats(nscoord aY, PRUint8 aBreakType,
                      nsIFrame *aReplacedBlock = nsnull);

  PRBool IsAdjacentWithTop() const {
    return mY ==
      ((mFlags & BRS_ISFIRSTINFLOW) ? mReflowState.mComputedBorderPadding.top : 0);
  }

  /**
   * Adjusts the border/padding to return 0 for the top if
   * we are not the first in flow.
   */
  nsMargin BorderPadding() const {
    nsMargin result = mReflowState.mComputedBorderPadding;
    if (!(mFlags & BRS_ISFIRSTINFLOW)) {
      result.top = 0;
      if (mFlags & BRS_ISOVERFLOWCONTAINER) {
        result.bottom = 0;
      }
    }
    return result;
  }

  // XXX maybe we should do the same adjustment for continuations here
  const nsMargin& Margin() const {
    return mReflowState.mComputedMargin;
  }

  // Reconstruct the previous bottom margin that goes above |aLine|.
  void ReconstructMarginAbove(nsLineList::iterator aLine);

  // Caller must have called GetAvailableSpace for the correct position
  // (which need not be the current mY).  Callers need only pass
  // aReplacedWidth for outer table frames.
  void ComputeReplacedBlockOffsetsForFloats(nsIFrame* aFrame,
                                            const nsRect& aFloatAvailableSpace,
                                            nscoord& aLeftResult,
                                            nscoord& aRightResult,
                                       nsBlockFrame::ReplacedElementWidthToClear
                                                      *aReplacedWidth = nsnull);

  // Caller must have called GetAvailableSpace for the current mY
  void ComputeBlockAvailSpace(nsIFrame* aFrame,
                              const nsStyleDisplay* aDisplay,
                              const nsFlowAreaRect& aFloatAvailableSpace,
                              PRBool aBlockAvoidsFloats,
                              nsRect& aResult);

protected:
  void RecoverFloats(nsLineList::iterator aLine, nscoord aDeltaY);

public:
  void RecoverStateFrom(nsLineList::iterator aLine, nscoord aDeltaY);

  void AdvanceToNextLine() {
    if (GetFlag(BRS_LINE_LAYOUT_EMPTY)) {
      SetFlag(BRS_LINE_LAYOUT_EMPTY, PR_FALSE);
    } else {
      mLineNumber++;
    }
  }

  nsLineBox* NewLineBox(nsIFrame* aFrame, PRInt32 aCount, PRBool aIsBlock);

  void FreeLineBox(nsLineBox* aLine);

  //----------------------------------------

  // This state is the "global" state computed once for the reflow of
  // the block.

  // The block frame that is using this object
  nsBlockFrame* mBlock;

  nsPresContext* mPresContext;

  const nsHTMLReflowState& mReflowState;

  nsFloatManager* mFloatManager;

  // The coordinates within the float manager where the block is being
  // placed <b>after</b> taking into account the blocks border and
  // padding. This, therefore, represents the inner "content area" (in
  // spacemanager coordinates) where child frames will be placed,
  // including child blocks and floats.
  nscoord mFloatManagerX, mFloatManagerY;

  // XXX get rid of this
  nsReflowStatus mReflowStatus;

  // The float manager state as it was before the contents of this
  // block.  This is needed for positioning bullets, since we only want
  // to move the bullet to flow around floats that were before this
  // block, not floats inside of it.
  nsFloatManager::SavedState mFloatManagerStateBefore;

  nscoord mBottomEdge;

  // The content area to reflow child frames within. The x/y
  // coordinates are known to be mBorderPadding.left and
  // mBorderPadding.top. The width/height may be NS_UNCONSTRAINEDSIZE
  // if the container reflowing this frame has given the frame an
  // unconstrained area.
  nsSize mContentArea;

  // Continuation out-of-flow float frames that need to move to our
  // next in flow are placed here during reflow. At the end of reflow
  // they move to the end of the mFloats list.
  nsFrameList mFloatContinuations;
  // This method makes sure float continuations are accessible to
  // StealFrame. Call it before adding any frames to mFloatContinuations.
  void SetupFloatContinuationList();
  // Use this method to append to mFloatContinuations.
  void AppendFloatContinuation(nsIFrame* aFloatCont) {
    SetupFloatContinuationList();
    mFloatContinuations.AppendFrame(mBlock, aFloatCont);
  }

  // Track child overflow continuations.
  nsOverflowContinuationTracker* mOverflowTracker;

  //----------------------------------------

  // This state is "running" state updated by the reflow of each line
  // in the block. This same state is "recovered" when a line is not
  // dirty and is passed over during incremental reflow.

  // The current line being reflowed
  // If it is mBlock->end_lines(), then it is invalid.
  nsLineList::iterator mCurrentLine;

  // When BRS_HAVELINEADJACENTTOTOP is set, this refers to a line
  // which we know is adjacent to the top of the block (in other words,
  // all lines before it are empty and do not have clearance. This line is
  // always before the current line.
  nsLineList::iterator mLineAdjacentToTop;

  // The current Y coordinate in the block
  nscoord mY;

  // The combined area of all floats placed so far
  nsRect mFloatCombinedArea;

  nsFloatCacheFreeList mFloatCacheFreeList;

  // Previous child. This is used when pulling up a frame to update
  // the sibling list.
  nsIFrame* mPrevChild;

  // The previous child frames collapsed bottom margin value.
  nsCollapsingMargin mPrevBottomMargin;

  // The current next-in-flow for the block. When lines are pulled
  // from a next-in-flow, this is used to know which next-in-flow to
  // pull from. When a next-in-flow is emptied of lines, we advance
  // this to the next next-in-flow.
  nsBlockFrame* mNextInFlow;

  //----------------------------------------

  // Temporary line-reflow state. This state is used during the reflow
  // of a given line, but doesn't have meaning before or after.

  // The list of floats that are "current-line" floats. These are
  // added to the line after the line has been reflowed, to keep the
  // list fiddling from being N^2.
  nsFloatCacheFreeList mCurrentLineFloats;

  // The list of floats which are "below current-line"
  // floats. These are reflowed/placed after the line is reflowed
  // and placed. Again, this is done to keep the list fiddling from
  // being N^2.
  nsFloatCacheFreeList mBelowCurrentLineFloats;

  nscoord mMinLineHeight;

  PRInt32 mLineNumber;

  PRInt16 mFlags;
 
  PRUint8 mFloatBreakType;

  void SetFlag(PRUint32 aFlag, PRBool aValue)
  {
    NS_ASSERTION(aFlag<=BRS_LASTFLAG, "bad flag");
    NS_ASSERTION(aValue==PR_FALSE || aValue==PR_TRUE, "bad value");
    if (aValue) { // set flag
      mFlags |= aFlag;
    }
    else {        // unset flag
      mFlags &= ~aFlag;
    }
  }

  PRBool GetFlag(PRUint32 aFlag) const
  {
    NS_ASSERTION(aFlag<=BRS_LASTFLAG, "bad flag");
    return !!(mFlags & aFlag);
  }
};

#endif // nsBlockReflowState_h__
