/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 sw=2 et tw=80: */
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
#include "nsCOMPtr.h"
#include "nsTableFrame.h"
#include "nsIRenderingContext.h"
#include "nsStyleContext.h"
#include "nsStyleConsts.h"
#include "nsIContent.h"
#include "nsCellMap.h"
#include "nsTableCellFrame.h"
#include "nsHTMLParts.h"
#include "nsTableColFrame.h"
#include "nsTableColGroupFrame.h"
#include "nsTableRowFrame.h"
#include "nsTableRowGroupFrame.h"
#include "nsTableOuterFrame.h"
#include "nsTablePainter.h"

#include "BasicTableLayoutStrategy.h"
#include "FixedTableLayoutStrategy.h"

#include "nsPresContext.h"
#include "nsCSSRendering.h"
#include "nsStyleConsts.h"
#include "nsGkAtoms.h"
#include "nsCSSAnonBoxes.h"
#include "nsIPresShell.h"
#include "nsIDOMElement.h"
#include "nsIDOMHTMLElement.h"
#include "nsIDOMHTMLBodyElement.h"
#include "nsFrameManager.h"
#include "nsCSSRendering.h"
#include "nsLayoutErrors.h"
#include "nsAutoPtr.h"
#include "nsCSSFrameConstructor.h"
#include "nsStyleSet.h"
#include "nsDisplayList.h"
#include "nsIScrollableFrame.h"

/********************************************************************************
 ** nsTableReflowState                                                         **
 ********************************************************************************/

struct nsTableReflowState {

  // the real reflow state
  const nsHTMLReflowState& reflowState;

  // The table's available size 
  nsSize availSize;

  // Stationary x-offset
  nscoord x;

  // Running y-offset
  nscoord y;

  nsTableReflowState(nsPresContext&           aPresContext,
                     const nsHTMLReflowState& aReflowState,
                     nsTableFrame&            aTableFrame,
                     nscoord                  aAvailWidth,
                     nscoord                  aAvailHeight)
    : reflowState(aReflowState)
  {
    Init(aPresContext, aTableFrame, aAvailWidth, aAvailHeight);
  }

  void Init(nsPresContext&  aPresContext,
            nsTableFrame&   aTableFrame,
            nscoord         aAvailWidth,
            nscoord         aAvailHeight)
  {
    nsTableFrame* table = (nsTableFrame*)aTableFrame.GetFirstInFlow();
    nsMargin borderPadding = table->GetChildAreaOffset(&reflowState);
    nscoord cellSpacingX = table->GetCellSpacingX();

    x = borderPadding.left + cellSpacingX;
    y = borderPadding.top; //cellspacing added during reflow

    availSize.width  = aAvailWidth;
    if (NS_UNCONSTRAINEDSIZE != availSize.width) {
      availSize.width -= borderPadding.left + borderPadding.right
                         + (2 * cellSpacingX);
      availSize.width = NS_MAX(0, availSize.width);
    }

    availSize.height = aAvailHeight;
    if (NS_UNCONSTRAINEDSIZE != availSize.height) {
      availSize.height -= borderPadding.top + borderPadding.bottom
                          + (2 * table->GetCellSpacingY());
      availSize.height = NS_MAX(0, availSize.height);
    }
  }

  nsTableReflowState(nsPresContext&           aPresContext,
                     const nsHTMLReflowState& aReflowState,
                     nsTableFrame&            aTableFrame)
    : reflowState(aReflowState)
  {
    Init(aPresContext, aTableFrame, aReflowState.availableWidth, aReflowState.availableHeight);
  }

};

/********************************************************************************
 ** nsTableFrame                                                               **
 ********************************************************************************/

struct BCPropertyData
{
  BCPropertyData() { mDamageArea.x = mDamageArea.y = mDamageArea.width =
                     mDamageArea.height = mTopBorderWidth = mRightBorderWidth =
                     mBottomBorderWidth = mLeftBorderWidth =
                     mLeftCellBorderWidth = mRightCellBorderWidth = 0; }
  nsRect  mDamageArea;
  BCPixelSize mTopBorderWidth;
  BCPixelSize mRightBorderWidth;
  BCPixelSize mBottomBorderWidth;
  BCPixelSize mLeftBorderWidth;
  BCPixelSize mLeftCellBorderWidth;
  BCPixelSize mRightCellBorderWidth;
};

NS_IMETHODIMP 
nsTableFrame::GetParentStyleContextFrame(nsPresContext*  aPresContext,
                                         nsIFrame**      aProviderFrame,
                                         PRBool*         aIsChild)
{
  // Since our parent, the table outer frame, returned this frame, we
  // must return whatever our parent would normally have returned.

  NS_PRECONDITION(mParent, "table constructed without outer table");
  if (!mContent->GetParent() && !GetStyleContext()->GetPseudo()) {
    // We're the root.  We have no style context parent.
    *aIsChild = PR_FALSE;
    *aProviderFrame = nsnull;
    return NS_OK;
  }
    
  return static_cast<nsFrame*>(mParent)->
          DoGetParentStyleContextFrame(aPresContext, aProviderFrame, aIsChild);
}


nsIAtom*
nsTableFrame::GetType() const
{
  return nsGkAtoms::tableFrame; 
}


nsTableFrame::nsTableFrame(nsStyleContext* aContext)
  : nsHTMLContainerFrame(aContext),
    mCellMap(nsnull),
    mTableLayoutStrategy(nsnull)
{
  mBits.mHaveReflowedColGroups  = PR_FALSE;
  mBits.mCellSpansPctCol        = PR_FALSE;
  mBits.mNeedToCalcBCBorders    = PR_FALSE;
  mBits.mIsBorderCollapse       = PR_FALSE;
  mBits.mResizedColumns         = PR_FALSE; // only really matters if splitting
  mBits.mGeometryDirty          = PR_FALSE;
}

NS_QUERYFRAME_HEAD(nsTableFrame)
  NS_QUERYFRAME_ENTRY(nsITableLayout)
NS_QUERYFRAME_TAIL_INHERITING(nsHTMLContainerFrame)

NS_IMETHODIMP
nsTableFrame::Init(nsIContent*      aContent,
                   nsIFrame*        aParent,
                   nsIFrame*        aPrevInFlow)
{
  nsresult  rv;

  // Let the base class do its processing
  rv = nsHTMLContainerFrame::Init(aContent, aParent, aPrevInFlow);

  // see if border collapse is on, if so set it
  const nsStyleTableBorder* tableStyle = GetStyleTableBorder();
  PRBool borderCollapse = (NS_STYLE_BORDER_COLLAPSE == tableStyle->mBorderCollapse);
  SetBorderCollapse(borderCollapse);
  // Create the cell map
  if (!aPrevInFlow) {
    mCellMap = new nsTableCellMap(*this, borderCollapse);
    if (!mCellMap)
      return NS_ERROR_OUT_OF_MEMORY;
  } else {
    mCellMap = nsnull;
  }

  if (aPrevInFlow) {
    // set my width, because all frames in a table flow are the same width and
    // code in nsTableOuterFrame depends on this being set
    mRect.width = aPrevInFlow->GetSize().width;
  }
  else {
    NS_ASSERTION(!mTableLayoutStrategy, "strategy was created before Init was called");
    // create the strategy
    if (IsAutoLayout())
      mTableLayoutStrategy = new BasicTableLayoutStrategy(this);
    else
      mTableLayoutStrategy = new FixedTableLayoutStrategy(this);
    if (!mTableLayoutStrategy)
      return NS_ERROR_OUT_OF_MEMORY;
  }

  return rv;
}


nsTableFrame::~nsTableFrame()
{
  if (nsnull!=mCellMap) {
    delete mCellMap; 
    mCellMap = nsnull;
  }

  if (nsnull!=mTableLayoutStrategy) {
    delete mTableLayoutStrategy;
    mTableLayoutStrategy = nsnull;
  }
}

void
nsTableFrame::DestroyFrom(nsIFrame* aDestructRoot)
{
  mColGroups.DestroyFramesFrom(aDestructRoot);
  nsHTMLContainerFrame::DestroyFrom(aDestructRoot);
}

// Make sure any views are positioned properly
void
nsTableFrame::RePositionViews(nsIFrame* aFrame)
{
  nsContainerFrame::PositionFrameView(aFrame);
  nsContainerFrame::PositionChildViews(aFrame);
}

static PRBool
IsRepeatedFrame(nsIFrame* kidFrame)
{
  return (kidFrame->GetType() == nsGkAtoms::tableRowFrame ||
          kidFrame->GetType() == nsGkAtoms::tableRowGroupFrame) &&
         (kidFrame->GetStateBits() & NS_REPEATED_ROW_OR_ROWGROUP);
}

PRBool
nsTableFrame::PageBreakAfter(nsIFrame& aSourceFrame,
                             nsIFrame* aNextFrame)
{
  const nsStyleDisplay* display = aSourceFrame.GetStyleDisplay();
  // don't allow a page break after a repeated element ...
  if (display->mBreakAfter && !IsRepeatedFrame(&aSourceFrame)) {
    return !(aNextFrame && IsRepeatedFrame(aNextFrame)); // or before
  }

  if (aNextFrame) {
    display = aNextFrame->GetStyleDisplay();
    // don't allow a page break before a repeated element ...
    if (display->mBreakBefore && !IsRepeatedFrame(aNextFrame)) {
      return !IsRepeatedFrame(&aSourceFrame); // or after
    }
  }
  return PR_FALSE;
}

// XXX this needs to be cleaned up so that the frame constructor breaks out col group
// frames into a separate child list, bug 343048.
NS_IMETHODIMP
nsTableFrame::SetInitialChildList(nsIAtom*        aListName,
                                  nsFrameList&    aChildList)
{

  if (!mFrames.IsEmpty() || !mColGroups.IsEmpty()) {
    // We already have child frames which means we've already been
    // initialized
    NS_NOTREACHED("unexpected second call to SetInitialChildList");
    return NS_ERROR_UNEXPECTED;
  }
  if (aListName) {
    // All we know about is the unnamed principal child list
    NS_NOTREACHED("unknown frame list");
    return NS_ERROR_INVALID_ARG;
  } 

  // XXXbz the below code is an icky cesspit that's only needed in its current
  // form for two reasons:
  // 1) Both rowgroups and column groups come in on the principal child list.
  while (aChildList.NotEmpty()) {
    nsIFrame* childFrame = aChildList.FirstChild();
    aChildList.RemoveFirstChild();
    const nsStyleDisplay* childDisplay = childFrame->GetStyleDisplay();

    if (NS_STYLE_DISPLAY_TABLE_COLUMN_GROUP == childDisplay->mDisplay) {
      NS_ASSERTION(nsGkAtoms::tableColGroupFrame == childFrame->GetType(),
                   "This is not a colgroup");
      mColGroups.AppendFrame(nsnull, childFrame);
    }
    else { // row groups and unknown frames go on the main list for now
      mFrames.AppendFrame(nsnull, childFrame);
    }
  }

  // If we have a prev-in-flow, then we're a table that has been split and
  // so don't treat this like an append
  if (!GetPrevInFlow()) {
    // process col groups first so that real cols get constructed before
    // anonymous ones due to cells in rows.
    InsertColGroups(0, mColGroups);
    InsertRowGroups(mFrames);
    // calc collapsing borders 
    if (IsBorderCollapse()) {
      nsRect damageArea(0, 0, GetColCount(), GetRowCount());
      SetBCDamageArea(damageArea);
    }
  }

  return NS_OK;
}

/* virtual */ PRBool
nsTableFrame::IsContainingBlock() const
{
  return PR_TRUE;
}

void nsTableFrame::AttributeChangedFor(nsIFrame*       aFrame,
                                       nsIContent*     aContent, 
                                       nsIAtom*        aAttribute)
{
  nsTableCellFrame *cellFrame = do_QueryFrame(aFrame);
  if (cellFrame) {
    if ((nsGkAtoms::rowspan == aAttribute) || 
        (nsGkAtoms::colspan == aAttribute)) {
      nsTableCellMap* cellMap = GetCellMap();
      if (cellMap) {
        // for now just remove the cell from the map and reinsert it
        PRInt32 rowIndex, colIndex;
        cellFrame->GetRowIndex(rowIndex);
        cellFrame->GetColIndex(colIndex);
        RemoveCell(cellFrame, rowIndex);
        nsAutoTArray<nsTableCellFrame*, 1> cells;
        cells.AppendElement(cellFrame);
        InsertCells(cells, rowIndex, colIndex - 1);

        // XXX Should this use eStyleChange?  It currently doesn't need
        // to, but it might given more optimization.
        PresContext()->PresShell()->
          FrameNeedsReflow(this, nsIPresShell::eTreeChange, NS_FRAME_IS_DIRTY);
      }
    }
  }
}


/* ****** CellMap methods ******* */

/* return the effective col count */
PRInt32 nsTableFrame::GetEffectiveColCount() const
{
  PRInt32 colCount = GetColCount();
  if (LayoutStrategy()->GetType() == nsITableLayoutStrategy::Auto) {
    nsTableCellMap* cellMap = GetCellMap();
    if (!cellMap) {
      return 0;
    }
    // don't count cols at the end that don't have originating cells
    for (PRInt32 colX = colCount - 1; colX >= 0; colX--) {
      if (cellMap->GetNumCellsOriginatingInCol(colX) > 0) { 
        break;
      }
      colCount--;
    }
  }
  return colCount;
}

PRInt32 nsTableFrame::GetIndexOfLastRealCol()
{
  PRInt32 numCols = mColFrames.Length();
  if (numCols > 0) {
    for (PRInt32 colX = numCols - 1; colX >= 0; colX--) { 
      nsTableColFrame* colFrame = GetColFrame(colX);
      if (colFrame) {
        if (eColAnonymousCell != colFrame->GetColType()) {
          return colX;
        }
      }
    }
  }
  return -1; 
}

nsTableColFrame*
nsTableFrame::GetColFrame(PRInt32 aColIndex) const
{
  NS_ASSERTION(!GetPrevInFlow(), "GetColFrame called on next in flow");
  PRInt32 numCols = mColFrames.Length();
  if ((aColIndex >= 0) && (aColIndex < numCols)) {
    return mColFrames.ElementAt(aColIndex);
  }
  else {
    NS_ERROR("invalid col index");
    return nsnull;
  }
}

PRInt32 nsTableFrame::GetEffectiveRowSpan(PRInt32                 aRowIndex,
                                          const nsTableCellFrame& aCell) const
{
  nsTableCellMap* cellMap = GetCellMap();
  NS_PRECONDITION (nsnull != cellMap, "bad call, cellMap not yet allocated.");

  PRInt32 colIndex;
  aCell.GetColIndex(colIndex);
  return cellMap->GetEffectiveRowSpan(aRowIndex, colIndex);
}

PRInt32 nsTableFrame::GetEffectiveRowSpan(const nsTableCellFrame& aCell,
                                          nsCellMap*              aCellMap)
{
  nsTableCellMap* tableCellMap = GetCellMap(); if (!tableCellMap) ABORT1(1);

  PRInt32 colIndex, rowIndex;
  aCell.GetColIndex(colIndex);
  aCell.GetRowIndex(rowIndex);

  if (aCellMap) 
    return aCellMap->GetRowSpan(rowIndex, colIndex, PR_TRUE);
  else
    return tableCellMap->GetEffectiveRowSpan(rowIndex, colIndex);
}

PRInt32 nsTableFrame::GetEffectiveColSpan(const nsTableCellFrame& aCell,
                                          nsCellMap*              aCellMap) const
{
  nsTableCellMap* tableCellMap = GetCellMap(); if (!tableCellMap) ABORT1(1);

  PRInt32 colIndex, rowIndex;
  aCell.GetColIndex(colIndex);
  aCell.GetRowIndex(rowIndex);
  PRBool ignore;

  if (aCellMap) 
    return aCellMap->GetEffectiveColSpan(*tableCellMap, rowIndex, colIndex, ignore);
  else
    return tableCellMap->GetEffectiveColSpan(rowIndex, colIndex);
}

PRBool nsTableFrame::HasMoreThanOneCell(PRInt32 aRowIndex) const
{
  nsTableCellMap* tableCellMap = GetCellMap(); if (!tableCellMap) ABORT1(1);
  return tableCellMap->HasMoreThanOneCell(aRowIndex);
}

PRInt32 nsTableFrame::GetEffectiveCOLSAttribute()
{
  NS_PRECONDITION (GetCellMap(), "null cellMap.");

  PRInt32 result;
  result = GetStyleTable()->mCols;
  PRInt32 numCols = GetColCount();
  if (result > numCols)
    result = numCols;
  return result;
}

void nsTableFrame::AdjustRowIndices(PRInt32         aRowIndex,
                                    PRInt32         aAdjustment)
{
  // Iterate over the row groups and adjust the row indices of all rows 
  // whose index is >= aRowIndex.
  RowGroupArray rowGroups;
  OrderRowGroups(rowGroups);

  for (PRUint32 rgX = 0; rgX < rowGroups.Length(); rgX++) {
    rowGroups[rgX]->AdjustRowIndices(aRowIndex, aAdjustment);
  }
}


void nsTableFrame::ResetRowIndices(const nsFrameList::Slice& aRowGroupsToExclude)
{
  // Iterate over the row groups and adjust the row indices of all rows
  // omit the rowgroups that will be inserted later
  RowGroupArray rowGroups;
  OrderRowGroups(rowGroups);

  PRInt32 rowIndex = 0;
  nsTHashtable<nsPtrHashKey<nsTableRowGroupFrame> > excludeRowGroups;
  if (!excludeRowGroups.Init()) {
    NS_ERROR("Failed to initialize excludeRowGroups hash.");
    return;
  }
  nsFrameList::Enumerator excludeRowGroupsEnumerator(aRowGroupsToExclude);
  while (!excludeRowGroupsEnumerator.AtEnd()) {
    excludeRowGroups.PutEntry(static_cast<nsTableRowGroupFrame*>(excludeRowGroupsEnumerator.get()));
    excludeRowGroupsEnumerator.Next();
  }

  for (PRUint32 rgX = 0; rgX < rowGroups.Length(); rgX++) {
    nsTableRowGroupFrame* rgFrame = rowGroups[rgX];
    if (!excludeRowGroups.GetEntry(rgFrame)) {
      const nsFrameList& rowFrames = rgFrame->GetChildList(nsnull);
      for (nsFrameList::Enumerator rows(rowFrames); !rows.AtEnd(); rows.Next()) {
        if (NS_STYLE_DISPLAY_TABLE_ROW==rows.get()->GetStyleDisplay()->mDisplay) {
          ((nsTableRowFrame *)rows.get())->SetRowIndex(rowIndex);
          rowIndex++;
        }
      }
    }
  }
}
void nsTableFrame::InsertColGroups(PRInt32                   aStartColIndex,
                                   const nsFrameList::Slice& aColGroups)
{
  PRInt32 colIndex = aStartColIndex;
  nsFrameList::Enumerator colGroups(aColGroups);
  for (; !colGroups.AtEnd(); colGroups.Next()) {
    nsTableColGroupFrame* cgFrame =
      static_cast<nsTableColGroupFrame*>(colGroups.get());
    cgFrame->SetStartColumnIndex(colIndex);
    // XXXbz this sucks.  AddColsToTable will actually remove colgroups from
    // the list we're traversing!  Need to fix things here.  :( I guess this is
    // why the old code used pointer-to-last-frame as opposed to
    // pointer-to-frame-after-last....

    // How about dealing with this by storing a const reference to the
    // mNextSibling of the framelist's last frame, instead of storing a pointer
    // to the first-after-next frame?  Will involve making nsFrameList friend
    // of nsIFrame, but it's time for that anyway.
    cgFrame->AddColsToTable(colIndex, PR_FALSE,
                              colGroups.get()->GetChildList(nsnull));
    PRInt32 numCols = cgFrame->GetColCount();
    colIndex += numCols;
  }

  nsFrameList::Enumerator remainingColgroups = colGroups.GetUnlimitedEnumerator();
  if (!remainingColgroups.AtEnd()) {
    nsTableColGroupFrame::ResetColIndices(
      static_cast<nsTableColGroupFrame*>(remainingColgroups.get()), colIndex);
  }
}

void nsTableFrame::InsertCol(nsTableColFrame& aColFrame,
                             PRInt32          aColIndex)
{
  mColFrames.InsertElementAt(aColIndex, &aColFrame);
  nsTableColType insertedColType = aColFrame.GetColType();
  PRInt32 numCacheCols = mColFrames.Length();
  nsTableCellMap* cellMap = GetCellMap();
  if (cellMap) {
    PRInt32 numMapCols = cellMap->GetColCount();
    if (numCacheCols > numMapCols) {
      PRBool removedFromCache = PR_FALSE;
      if (eColAnonymousCell != insertedColType) {
        nsTableColFrame* lastCol = mColFrames.ElementAt(numCacheCols - 1);
        if (lastCol) {
          nsTableColType lastColType = lastCol->GetColType();
          if (eColAnonymousCell == lastColType) {
            // remove the col from the cache
            mColFrames.RemoveElementAt(numCacheCols - 1);
            // remove the col from the eColGroupAnonymousCell col group
            nsTableColGroupFrame* lastColGroup = (nsTableColGroupFrame *)mColGroups.LastChild();
            if (lastColGroup) {
              lastColGroup->RemoveChild(*lastCol, PR_FALSE);
            }
            // remove the col group if it is empty
            if (lastColGroup->GetColCount() <= 0) {
              mColGroups.DestroyFrame((nsIFrame*)lastColGroup);
            }
            removedFromCache = PR_TRUE;
          }
        }
      }
      if (!removedFromCache) {
        cellMap->AddColsAtEnd(1);
      }
    }
  }
  // for now, just bail and recalc all of the collapsing borders
  if (IsBorderCollapse()) {
    nsRect damageArea(0, 0, NS_MAX(1, GetColCount()), NS_MAX(1, GetRowCount()));
    SetBCDamageArea(damageArea);
  }
}

void nsTableFrame::RemoveCol(nsTableColGroupFrame* aColGroupFrame,
                             PRInt32               aColIndex,
                             PRBool                aRemoveFromCache,
                             PRBool                aRemoveFromCellMap)
{
  if (aRemoveFromCache) {
    mColFrames.RemoveElementAt(aColIndex);
  }
  if (aRemoveFromCellMap) {
    nsTableCellMap* cellMap = GetCellMap();
    if (cellMap) {
      AppendAnonymousColFrames(1);
    }
  }
  // for now, just bail and recalc all of the collapsing borders
  if (IsBorderCollapse()) {
    nsRect damageArea(0, 0, GetColCount(), GetRowCount());
    SetBCDamageArea(damageArea);
  }
}

/** Get the cell map for this table frame.  It is not always mCellMap.
  * Only the firstInFlow has a legit cell map
  */
nsTableCellMap* nsTableFrame::GetCellMap() const
{
  nsTableFrame* firstInFlow = (nsTableFrame *)GetFirstInFlow();
  return firstInFlow->mCellMap;
}

// XXX this needs to be moved to nsCSSFrameConstructor
nsTableColGroupFrame*
nsTableFrame::CreateAnonymousColGroupFrame(nsTableColGroupType aColGroupType)
{
  nsIContent* colGroupContent = GetContent();
  nsPresContext* presContext = PresContext();
  nsIPresShell *shell = presContext->PresShell();

  nsRefPtr<nsStyleContext> colGroupStyle;
  colGroupStyle = shell->StyleSet()->
    ResolveAnonymousBoxStyle(nsCSSAnonBoxes::tableColGroup, mStyleContext);
  // Create a col group frame
  nsIFrame* newFrame = NS_NewTableColGroupFrame(shell, colGroupStyle);
  if (newFrame) {
    ((nsTableColGroupFrame *)newFrame)->SetColType(aColGroupType);
    newFrame->Init(colGroupContent, this, nsnull);
  }
  return (nsTableColGroupFrame *)newFrame;
}

void
nsTableFrame::AppendAnonymousColFrames(PRInt32 aNumColsToAdd)
{
  // get the last col group frame
  nsTableColGroupFrame* colGroupFrame =
    static_cast<nsTableColGroupFrame*>(mColGroups.LastChild());

  if (!colGroupFrame ||
      (colGroupFrame->GetColType() != eColGroupAnonymousCell)) {
    PRInt32 colIndex = (colGroupFrame) ?
                        colGroupFrame->GetStartColumnIndex() +
                        colGroupFrame->GetColCount() : 0;
    colGroupFrame = CreateAnonymousColGroupFrame(eColGroupAnonymousCell);
    if (!colGroupFrame) {
      return;
    }
    // add the new frame to the child list
    mColGroups.AppendFrame(this, colGroupFrame);
    colGroupFrame->SetStartColumnIndex(colIndex);
  }
  AppendAnonymousColFrames(colGroupFrame, aNumColsToAdd, eColAnonymousCell,
                           PR_TRUE);

}

// XXX this needs to be moved to nsCSSFrameConstructor
// Right now it only creates the col frames at the end 
void
nsTableFrame::AppendAnonymousColFrames(nsTableColGroupFrame* aColGroupFrame,
                                       PRInt32               aNumColsToAdd,
                                       nsTableColType        aColType,
                                       PRBool                aAddToTable)
{
  NS_PRECONDITION(aColGroupFrame, "null frame");
  NS_PRECONDITION(aColType != eColAnonymousCol, "Shouldn't happen");

  nsIPresShell *shell = PresContext()->PresShell();

  // Get the last col frame
  nsFrameList newColFrames;

  PRInt32 startIndex = mColFrames.Length();
  PRInt32 lastIndex  = startIndex + aNumColsToAdd - 1; 

  for (PRInt32 childX = startIndex; childX <= lastIndex; childX++) {
    nsIContent* iContent;
    nsRefPtr<nsStyleContext> styleContext;
    nsStyleContext* parentStyleContext;

    // all anonymous cols that we create here use a pseudo style context of the
    // col group
    iContent = aColGroupFrame->GetContent();
    parentStyleContext = aColGroupFrame->GetStyleContext();
    styleContext = shell->StyleSet()->
      ResolveAnonymousBoxStyle(nsCSSAnonBoxes::tableCol, parentStyleContext);
    // ASSERTION to check for bug 54454 sneaking back in...
    NS_ASSERTION(iContent, "null content in CreateAnonymousColFrames");

    // create the new col frame
    nsIFrame* colFrame = NS_NewTableColFrame(shell, styleContext);
    ((nsTableColFrame *) colFrame)->SetColType(aColType);
    colFrame->Init(iContent, aColGroupFrame, nsnull);

    newColFrames.AppendFrame(nsnull, colFrame);
  }
  nsFrameList& cols = aColGroupFrame->GetWritableChildList();
  nsIFrame* oldLastCol = cols.LastChild();
  const nsFrameList::Slice& newCols =
    cols.InsertFrames(nsnull, oldLastCol, newColFrames);
  if (aAddToTable) {
    // get the starting col index in the cache
    PRInt32 startColIndex;
    if (oldLastCol) {
      startColIndex =
        static_cast<nsTableColFrame*>(oldLastCol)->GetColIndex() + 1;
    } else {
      startColIndex = aColGroupFrame->GetStartColumnIndex();
    }

    aColGroupFrame->AddColsToTable(startColIndex, PR_TRUE, newCols);
  }
}

void
nsTableFrame::MatchCellMapToColCache(nsTableCellMap* aCellMap)
{
  PRInt32 numColsInMap   = GetColCount();
  PRInt32 numColsInCache = mColFrames.Length();
  PRInt32 numColsToAdd = numColsInMap - numColsInCache;
  if (numColsToAdd > 0) {
    // this sets the child list, updates the col cache and cell map
    AppendAnonymousColFrames(numColsToAdd);
  }
  if (numColsToAdd < 0) {
    PRInt32 numColsNotRemoved = DestroyAnonymousColFrames(-numColsToAdd);
    // if the cell map has fewer cols than the cache, correct it
    if (numColsNotRemoved > 0) {
      aCellMap->AddColsAtEnd(numColsNotRemoved);
    }
  }
  if (numColsToAdd && HasZeroColSpans()) {
    SetNeedColSpanExpansion(PR_TRUE);
  }
  if (NeedColSpanExpansion()) {
    // This flag can be set in two ways -- either by changing
    // the number of columns (that happens in the block above),
    // or by adding a cell with colspan="0" to the cellmap.  To
    // handle the latter case we need to explicitly check the
    // flag here -- it may be set even if the number of columns
    // did not change.
    //
    // @see nsCellMap::AppendCell

    aCellMap->ExpandZeroColSpans();
  }
}

void
nsTableFrame::DidResizeColumns()
{
  NS_PRECONDITION(!GetPrevInFlow(),
                  "should only be called on first-in-flow");
  if (mBits.mResizedColumns)
    return; // already marked

  for (nsTableFrame *f = this; f;
       f = static_cast<nsTableFrame*>(f->GetNextInFlow()))
    f->mBits.mResizedColumns = PR_TRUE;
}

void
nsTableFrame::AppendCell(nsTableCellFrame& aCellFrame,
                         PRInt32           aRowIndex)
{
  nsTableCellMap* cellMap = GetCellMap();
  if (cellMap) {
    nsRect damageArea(0,0,0,0);
    cellMap->AppendCell(aCellFrame, aRowIndex, PR_TRUE, damageArea);
    MatchCellMapToColCache(cellMap);
    if (IsBorderCollapse()) {
      SetBCDamageArea(damageArea);
    }
  }
}

void nsTableFrame::InsertCells(nsTArray<nsTableCellFrame*>& aCellFrames,
                               PRInt32                      aRowIndex,
                               PRInt32                      aColIndexBefore)
{
  nsTableCellMap* cellMap = GetCellMap();
  if (cellMap) {
    nsRect damageArea(0,0,0,0);
    cellMap->InsertCells(aCellFrames, aRowIndex, aColIndexBefore, damageArea);
    MatchCellMapToColCache(cellMap);
    if (IsBorderCollapse()) {
      SetBCDamageArea(damageArea);
    }
  }
}

// this removes the frames from the col group and table, but not the cell map
PRInt32 
nsTableFrame::DestroyAnonymousColFrames(PRInt32 aNumFrames)
{
  // only remove cols that are of type eTypeAnonymous cell (they are at the end)
  PRInt32 endIndex   = mColFrames.Length() - 1;
  PRInt32 startIndex = (endIndex - aNumFrames) + 1;
  PRInt32 numColsRemoved = 0;
  for (PRInt32 colX = endIndex; colX >= startIndex; colX--) {
    nsTableColFrame* colFrame = GetColFrame(colX);
    if (colFrame && (eColAnonymousCell == colFrame->GetColType())) {
      nsTableColGroupFrame* cgFrame =
        static_cast<nsTableColGroupFrame*>(colFrame->GetParent());
      // remove the frame from the colgroup
      cgFrame->RemoveChild(*colFrame, PR_FALSE);
      // remove the frame from the cache, but not the cell map 
      RemoveCol(nsnull, colX, PR_TRUE, PR_FALSE);
      numColsRemoved++;
    }
    else {
      break; 
    }
  }
  return (aNumFrames - numColsRemoved);
}

void nsTableFrame::RemoveCell(nsTableCellFrame* aCellFrame,
                              PRInt32           aRowIndex)
{
  nsTableCellMap* cellMap = GetCellMap();
  if (cellMap) {
    nsRect damageArea(0,0,0,0);
    cellMap->RemoveCell(aCellFrame, aRowIndex, damageArea);
    MatchCellMapToColCache(cellMap);
    if (IsBorderCollapse()) {
      SetBCDamageArea(damageArea);
    }
  }
}

PRInt32
nsTableFrame::GetStartRowIndex(nsTableRowGroupFrame* aRowGroupFrame)
{
  RowGroupArray orderedRowGroups;
  OrderRowGroups(orderedRowGroups);

  PRInt32 rowIndex = 0;
  for (PRUint32 rgIndex = 0; rgIndex < orderedRowGroups.Length(); rgIndex++) {
    nsTableRowGroupFrame* rgFrame = orderedRowGroups[rgIndex];
    if (rgFrame == aRowGroupFrame) {
      break;
    }
    PRInt32 numRows = rgFrame->GetRowCount();
    rowIndex += numRows;
  }
  return rowIndex;
}

// this cannot extend beyond a single row group
void nsTableFrame::AppendRows(nsTableRowGroupFrame*       aRowGroupFrame,
                              PRInt32                     aRowIndex,
                              nsTArray<nsTableRowFrame*>& aRowFrames)
{
  nsTableCellMap* cellMap = GetCellMap();
  if (cellMap) {
    PRInt32 absRowIndex = GetStartRowIndex(aRowGroupFrame) + aRowIndex;
    InsertRows(aRowGroupFrame, aRowFrames, absRowIndex, PR_TRUE);
  }
}

// this cannot extend beyond a single row group
PRInt32
nsTableFrame::InsertRows(nsTableRowGroupFrame*       aRowGroupFrame,
                         nsTArray<nsTableRowFrame*>& aRowFrames,
                         PRInt32                     aRowIndex,
                         PRBool                      aConsiderSpans)
{
#ifdef DEBUG_TABLE_CELLMAP
  printf("=== insertRowsBefore firstRow=%d \n", aRowIndex);
  Dump(PR_TRUE, PR_FALSE, PR_TRUE);
#endif

  PRInt32 numColsToAdd = 0;
  nsTableCellMap* cellMap = GetCellMap();
  if (cellMap) {
    nsRect damageArea(0,0,0,0);
    PRInt32 origNumRows = cellMap->GetRowCount();
    PRInt32 numNewRows = aRowFrames.Length();
    cellMap->InsertRows(aRowGroupFrame, aRowFrames, aRowIndex, aConsiderSpans, damageArea);
    MatchCellMapToColCache(cellMap);
    if (aRowIndex < origNumRows) {
      AdjustRowIndices(aRowIndex, numNewRows);
    }
    // assign the correct row indices to the new rows. If they were adjusted above
    // it may not have been done correctly because each row is constructed with index 0
    for (PRInt32 rowY = 0; rowY < numNewRows; rowY++) {
      nsTableRowFrame* rowFrame = aRowFrames.ElementAt(rowY);
      rowFrame->SetRowIndex(aRowIndex + rowY);
    }
    if (IsBorderCollapse()) {
      SetBCDamageArea(damageArea);
    }
  }
#ifdef DEBUG_TABLE_CELLMAP
  printf("=== insertRowsAfter \n");
  Dump(PR_TRUE, PR_FALSE, PR_TRUE);
#endif

  return numColsToAdd;
}

// this cannot extend beyond a single row group
void nsTableFrame::RemoveRows(nsTableRowFrame& aFirstRowFrame,
                              PRInt32          aNumRowsToRemove,
                              PRBool           aConsiderSpans)
{
#ifdef TBD_OPTIMIZATION
  // decide if we need to rebalance. we have to do this here because the row group 
  // cannot do it when it gets the dirty reflow corresponding to the frame being destroyed
  PRBool stopTelling = PR_FALSE;
  for (nsIFrame* kidFrame = aFirstFrame.FirstChild(); (kidFrame && !stopAsking);
       kidFrame = kidFrame->GetNextSibling()) {
    nsTableCellFrame *cellFrame = do_QueryFrame(kidFrame);
    if (cellFrame) {
      stopTelling = tableFrame->CellChangedWidth(*cellFrame, cellFrame->GetPass1MaxElementWidth(), 
                                                 cellFrame->GetMaximumWidth(), PR_TRUE);
    }
  }
  // XXX need to consider what happens if there are cells that have rowspans 
  // into the deleted row. Need to consider moving rows if a rebalance doesn't happen
#endif

  PRInt32 firstRowIndex = aFirstRowFrame.GetRowIndex();
#ifdef DEBUG_TABLE_CELLMAP
  printf("=== removeRowsBefore firstRow=%d numRows=%d\n", firstRowIndex, aNumRowsToRemove);
  Dump(PR_TRUE, PR_FALSE, PR_TRUE);
#endif
  nsTableCellMap* cellMap = GetCellMap();
  if (cellMap) {
    nsRect damageArea(0,0,0,0);
    cellMap->RemoveRows(firstRowIndex, aNumRowsToRemove, aConsiderSpans, damageArea);
    MatchCellMapToColCache(cellMap);
    if (IsBorderCollapse()) {
      SetBCDamageArea(damageArea);
    }
  }
  AdjustRowIndices(firstRowIndex, -aNumRowsToRemove);
#ifdef DEBUG_TABLE_CELLMAP
  printf("=== removeRowsAfter\n");
  Dump(PR_TRUE, PR_TRUE, PR_TRUE);
#endif
}

// collect the rows ancestors of aFrame
PRInt32
nsTableFrame::CollectRows(nsIFrame*                   aFrame,
                          nsTArray<nsTableRowFrame*>& aCollection)
{
  NS_PRECONDITION(aFrame, "null frame");
  PRInt32 numRows = 0;
  nsIFrame* childFrame = aFrame->GetFirstChild(nsnull);
  while (childFrame) {
    aCollection.AppendElement(static_cast<nsTableRowFrame*>(childFrame));
    numRows++;
    childFrame = childFrame->GetNextSibling();
  }
  return numRows;
}

void
nsTableFrame::InsertRowGroups(const nsFrameList::Slice& aRowGroups)
{
#ifdef DEBUG_TABLE_CELLMAP
  printf("=== insertRowGroupsBefore\n");
  Dump(PR_TRUE, PR_FALSE, PR_TRUE);
#endif
  nsTableCellMap* cellMap = GetCellMap();
  if (cellMap) {
    RowGroupArray orderedRowGroups;
    OrderRowGroups(orderedRowGroups);

    nsAutoTArray<nsTableRowFrame*, 8> rows;
    // Loop over the rowgroups and check if some of them are new, if they are
    // insert cellmaps in the order that is predefined by OrderRowGroups,
    // XXXbz this code is O(N*M) where N is number of new rowgroups
    // and M is number of rowgroups we have!
    PRUint32 rgIndex;
    for (rgIndex = 0; rgIndex < orderedRowGroups.Length(); rgIndex++) {
      for (nsFrameList::Enumerator rowgroups(aRowGroups); !rowgroups.AtEnd();
           rowgroups.Next()) {
        if (orderedRowGroups[rgIndex] == rowgroups.get()) {
          nsTableRowGroupFrame* priorRG =
            (0 == rgIndex) ? nsnull : orderedRowGroups[rgIndex - 1]; 
          // create and add the cell map for the row group
          cellMap->InsertGroupCellMap(orderedRowGroups[rgIndex], priorRG);
        
          break;
        }
      }
    }
    cellMap->Synchronize(this);
    ResetRowIndices(aRowGroups);

    //now that the cellmaps are reordered too insert the rows
    for (rgIndex = 0; rgIndex < orderedRowGroups.Length(); rgIndex++) {
      for (nsFrameList::Enumerator rowgroups(aRowGroups); !rowgroups.AtEnd();
           rowgroups.Next()) {
        if (orderedRowGroups[rgIndex] == rowgroups.get()) {
          nsTableRowGroupFrame* priorRG =
            (0 == rgIndex) ? nsnull : orderedRowGroups[rgIndex - 1]; 
          // collect the new row frames in an array and add them to the table
          PRInt32 numRows = CollectRows(rowgroups.get(), rows);
          if (numRows > 0) {
            PRInt32 rowIndex = 0;
            if (priorRG) {
              PRInt32 priorNumRows = priorRG->GetRowCount();
              rowIndex = priorRG->GetStartRowIndex() + priorNumRows;
            }
            InsertRows(orderedRowGroups[rgIndex], rows, rowIndex, PR_TRUE);
            rows.Clear();
          }
          break;
        }
      }
    }    
    
  }
#ifdef DEBUG_TABLE_CELLMAP
  printf("=== insertRowGroupsAfter\n");
  Dump(PR_TRUE, PR_TRUE, PR_TRUE);
#endif
}


/////////////////////////////////////////////////////////////////////////////
// Child frame enumeration

nsFrameList
nsTableFrame::GetChildList(nsIAtom* aListName) const
{
  if (aListName == nsGkAtoms::colGroupList) {
    return mColGroups;
  }

  return nsHTMLContainerFrame::GetChildList(aListName);
}

nsIAtom*
nsTableFrame::GetAdditionalChildListName(PRInt32 aIndex) const
{
  if (aIndex == NS_TABLE_FRAME_COLGROUP_LIST_INDEX) {
    return nsGkAtoms::colGroupList;
  }
  if (aIndex == NS_TABLE_FRAME_OVERFLOW_LIST_INDEX) {
    return nsGkAtoms::overflowList;
  } 
  return nsnull;
}

nsRect
nsDisplayTableItem::GetBounds(nsDisplayListBuilder* aBuilder) {
  return mFrame->GetOverflowRect() + aBuilder->ToReferenceFrame(mFrame);
}

PRBool
nsDisplayTableItem::IsVaryingRelativeToMovingFrame(nsDisplayListBuilder* aBuilder)
{
  if (!mPartHasFixedBackground)
    return PR_FALSE;

  // aAncestorFrame is the frame that is going to be moved.
  // Check if mFrame is equal to aAncestorFrame or aAncestorFrame is an
  // ancestor of mFrame in the same document. If this is true, mFrame
  // will move relative to its viewport, which means this display item will
  // change when it is moved.  If they are in different documents, we do not
  // want to return true because mFrame won't move relative to its viewport.
  nsIFrame* rootMover = aBuilder->GetRootMovingFrame();
  return mFrame == rootMover ||
    nsLayoutUtils::IsProperAncestorFrame(rootMover, mFrame);
}

/* static */ void
nsDisplayTableItem::UpdateForFrameBackground(nsIFrame* aFrame)
{
  const nsStyleBackground* bg;
  if (!nsCSSRendering::FindBackground(aFrame->PresContext(), aFrame, &bg))
    return;
  if (!bg->HasFixedBackground())
    return;

  mPartHasFixedBackground = PR_TRUE;
}

class nsDisplayTableBorderBackground : public nsDisplayTableItem {
public:
  nsDisplayTableBorderBackground(nsTableFrame* aFrame) : nsDisplayTableItem(aFrame) {
    MOZ_COUNT_CTOR(nsDisplayTableBorderBackground);
  }
#ifdef NS_BUILD_REFCNT_LOGGING
  virtual ~nsDisplayTableBorderBackground() {
    MOZ_COUNT_DTOR(nsDisplayTableBorderBackground);
  }
#endif

  virtual void Paint(nsDisplayListBuilder* aBuilder,
                     nsIRenderingContext* aCtx);
  NS_DISPLAY_DECL_NAME("TableBorderBackground")
};

void
nsDisplayTableBorderBackground::Paint(nsDisplayListBuilder* aBuilder,
                                      nsIRenderingContext* aCtx)
{
  static_cast<nsTableFrame*>(mFrame)->
    PaintTableBorderBackground(*aCtx, mVisibleRect,
                               aBuilder->ToReferenceFrame(mFrame),
                               aBuilder->GetBackgroundPaintFlags());
}

static PRInt32 GetTablePartRank(nsDisplayItem* aItem)
{
  nsIAtom* type = aItem->GetUnderlyingFrame()->GetType();
  if (type == nsGkAtoms::tableFrame)
    return 0;
  if (type == nsGkAtoms::tableRowGroupFrame)
    return 1;
  if (type == nsGkAtoms::tableRowFrame)
    return 2;
  return 3;
}

static PRBool CompareByTablePartRank(nsDisplayItem* aItem1, nsDisplayItem* aItem2,
                                     void* aClosure)
{
  return GetTablePartRank(aItem1) <= GetTablePartRank(aItem2);
}

/* static */ nsresult
nsTableFrame::GenericTraversal(nsDisplayListBuilder* aBuilder, nsFrame* aFrame,
                               const nsRect& aDirtyRect, const nsDisplayListSet& aLists)
{
  // This is similar to what nsContainerFrame::BuildDisplayListForNonBlockChildren
  // does, except that we allow the children's background and borders to go
  // in our BorderBackground list. This doesn't really affect background
  // painting --- the children won't actually draw their own backgrounds
  // because the nsTableFrame already drew them, unless a child has its own
  // stacking context, in which case the child won't use its passed-in
  // BorderBackground list anyway. It does affect cell borders though; this
  // lets us get cell borders into the nsTableFrame's BorderBackground list.
  nsIFrame* kid = aFrame->GetFirstChild(nsnull);
  while (kid) {
    nsresult rv = aFrame->BuildDisplayListForChild(aBuilder, kid, aDirtyRect, aLists);
    NS_ENSURE_SUCCESS(rv, rv);
    kid = kid->GetNextSibling();
  }
  return NS_OK;
}

/* static */ nsresult
nsTableFrame::DisplayGenericTablePart(nsDisplayListBuilder* aBuilder,
                                      nsFrame* aFrame,
                                      const nsRect& aDirtyRect,
                                      const nsDisplayListSet& aLists,
                                      nsDisplayTableItem* aDisplayItem,
                                      DisplayGenericTablePartTraversal aTraversal)
{
  nsDisplayList eventsBorderBackground;
  // If we need to sort the event backgrounds, then we'll put descendants'
  // display items into their own set of lists.
  PRBool sortEventBackgrounds = aDisplayItem && aBuilder->IsForEventDelivery();
  nsDisplayListCollection separatedCollection;
  const nsDisplayListSet* lists = sortEventBackgrounds ? &separatedCollection : &aLists;
  
  nsAutoPushCurrentTableItem pushTableItem;
  if (aDisplayItem) {
    pushTableItem.Push(aBuilder, aDisplayItem);
  }

  if (aFrame->IsVisibleForPainting(aBuilder)) {
    nsDisplayTableItem* currentItem = aBuilder->GetCurrentTableItem();
    // currentItem may be null, when none of the table parts have a
    // background or border
    if (currentItem) {
      currentItem->UpdateForFrameBackground(aFrame);
    }

    // Paint the outset box-shadows for the table frames
    PRBool hasBoxShadow = aFrame->GetStyleBorder()->mBoxShadow != nsnull;
    if (hasBoxShadow) {
      nsDisplayItem* item = new (aBuilder) nsDisplayBoxShadowOuter(aFrame);
      nsresult rv = lists->BorderBackground()->AppendNewToTop(item);
      NS_ENSURE_SUCCESS(rv, rv);
    }

    // Create dedicated background display items per-frame when we're
    // handling events.
    // XXX how to handle collapsed borders?
    if (aBuilder->IsForEventDelivery()) {
      nsresult rv = lists->BorderBackground()->AppendNewToTop(new (aBuilder)
          nsDisplayBackground(aFrame));
      NS_ENSURE_SUCCESS(rv, rv);
    }

    // Paint the inset box-shadows for the table frames
    if (hasBoxShadow) {
      nsDisplayItem* item = new (aBuilder) nsDisplayBoxShadowInner(aFrame);
      nsresult rv = lists->BorderBackground()->AppendNewToTop(item);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  nsresult rv = aTraversal(aBuilder, aFrame, aDirtyRect, *lists);
  NS_ENSURE_SUCCESS(rv, rv);

  if (sortEventBackgrounds) {
    // Ensure that the table frame event background goes before the
    // table rowgroups event backgrounds, before the table row event backgrounds,
    // before everything else (cells and their blocks)
    separatedCollection.BorderBackground()->Sort(aBuilder, CompareByTablePartRank, nsnull);
    separatedCollection.MoveTo(aLists);
  }
  
  return aFrame->DisplayOutline(aBuilder, aLists);
}

#ifdef DEBUG
static PRBool
IsFrameAllowedInTable(nsIAtom* aType)
{
  return IS_TABLE_CELL(aType) ||
         nsGkAtoms::tableRowFrame == aType ||
         nsGkAtoms::tableRowGroupFrame == aType ||
         nsGkAtoms::scrollFrame == aType ||
         nsGkAtoms::tableFrame == aType ||
         nsGkAtoms::tableColFrame == aType ||
         nsGkAtoms::tableColGroupFrame == aType;
}
#endif

static PRBool
AnyTablePartHasBorderOrBackground(nsIFrame* aStart, nsIFrame* aEnd)
{
  for (nsIFrame* f = aStart; f != aEnd; f = f->GetNextSibling()) {
    NS_ASSERTION(IsFrameAllowedInTable(f->GetType()), "unexpected frame type");

    if (f->GetStyleVisibility()->IsVisible() &&
        (!f->GetStyleBackground()->IsTransparent() ||
         f->GetStyleDisplay()->mAppearance ||
         f->HasBorder()))
      return PR_TRUE;

    nsTableCellFrame *cellFrame = do_QueryFrame(f);
    if (cellFrame)
      continue;

    if (AnyTablePartHasBorderOrBackground(f->GetChildList(nsnull).FirstChild(), nsnull))
      return PR_TRUE;
  }

  return PR_FALSE;
}

// table paint code is concerned primarily with borders and bg color
// SEC: TODO: adjust the rect for captions 
NS_IMETHODIMP
nsTableFrame::BuildDisplayList(nsDisplayListBuilder*   aBuilder,
                               const nsRect&           aDirtyRect,
                               const nsDisplayListSet& aLists)
{
  if (!IsVisibleInSelection(aBuilder))
    return NS_OK;

  DO_GLOBAL_REFLOW_COUNT_DSP_COLOR("nsTableFrame", NS_RGB(255,128,255));

  if (GetStyleVisibility()->IsVisible()) {
    nsMargin deflate = GetDeflationForBackground(PresContext());
    // If 'deflate' is (0,0,0,0) then we can paint the table background
    // in its own display item, so do that to take advantage of
    // opacity and visibility optimizations
    if (deflate.IsZero()) {
      nsresult rv = DisplayBackgroundUnconditional(aBuilder, aLists, PR_FALSE);
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  nsDisplayTableItem* item = nsnull;
  // This background is created if any of the table parts are visible,
  // or if we're doing event handling (since DisplayGenericTablePart
  // needs the item for the |sortEventBackgrounds|-dependent code).
  // Specific visibility decisions are delegated to the table background
  // painter, which handles borders and backgrounds for the table.
  if (aBuilder->IsForEventDelivery() ||
      AnyTablePartHasBorderOrBackground(this, GetNextSibling()) ||
      AnyTablePartHasBorderOrBackground(mColGroups.FirstChild(), nsnull)) {
    item = new (aBuilder) nsDisplayTableBorderBackground(this);
    nsresult rv = aLists.BorderBackground()->AppendNewToTop(item);
    NS_ENSURE_SUCCESS(rv, rv);
  }

  return DisplayGenericTablePart(aBuilder, this, aDirtyRect, aLists, item);
}

nsMargin
nsTableFrame::GetDeflationForBackground(nsPresContext* aPresContext) const
{
  if (eCompatibility_NavQuirks != aPresContext->CompatibilityMode() ||
      !IsBorderCollapse())
    return nsMargin(0,0,0,0);

  return GetOuterBCBorder();
}

// XXX We don't put the borders and backgrounds in tree order like we should.
// That requires some major surgery which we aren't going to do right now.
void
nsTableFrame::PaintTableBorderBackground(nsIRenderingContext& aRenderingContext,
                                         const nsRect& aDirtyRect,
                                         nsPoint aPt, PRUint32 aBGPaintFlags)
{
  nsPresContext* presContext = PresContext();

  TableBackgroundPainter painter(this, TableBackgroundPainter::eOrigin_Table,
                                 presContext, aRenderingContext,
                                 aDirtyRect, aPt, aBGPaintFlags);
  nsMargin deflate = GetDeflationForBackground(presContext);
  // If 'deflate' is (0,0,0,0) then we'll paint the table background
  // in a separate display item, so don't do it here.
  nsresult rv = painter.PaintTable(this, deflate, !deflate.IsZero());
  if (NS_FAILED(rv)) return;

  if (GetStyleVisibility()->IsVisible()) {
    const nsStyleBorder* border = GetStyleBorder();
    if (!IsBorderCollapse()) {
      PRIntn skipSides = GetSkipSides();
      nsRect rect(aPt, mRect.Size());
      nsCSSRendering::PaintBorder(presContext, aRenderingContext, this,
                                  aDirtyRect, rect, *border, mStyleContext,
                                  skipSides);
    }
    else {
      // XXX we should probably get rid of this translation at some stage
      // But that would mean modifying PaintBCBorders, ugh
      nsIRenderingContext::AutoPushTranslation translate(&aRenderingContext, aPt.x, aPt.y);
      PaintBCBorders(aRenderingContext, aDirtyRect - aPt);
    }
  }
}

PRIntn
nsTableFrame::GetSkipSides() const
{
  PRIntn skip = 0;
  // frame attribute was accounted for in nsHTMLTableElement::MapTableBorderInto
  // account for pagination
  if (nsnull != GetPrevInFlow()) {
    skip |= 1 << NS_SIDE_TOP;
  }
  if (nsnull != GetNextInFlow()) {
    skip |= 1 << NS_SIDE_BOTTOM;
  }
  return skip;
}

void
nsTableFrame::SetColumnDimensions(nscoord         aHeight,
                                  const nsMargin& aBorderPadding)
{
  nscoord cellSpacingX = GetCellSpacingX();
  nscoord cellSpacingY = GetCellSpacingY();
  nscoord colHeight = aHeight -= aBorderPadding.top + aBorderPadding.bottom +
                                 2* cellSpacingY;

  nsTableIterator iter(mColGroups); 
  nsIFrame* colGroupFrame = iter.First();
  PRBool tableIsLTR = GetStyleVisibility()->mDirection == NS_STYLE_DIRECTION_LTR;
  PRInt32 colX =tableIsLTR ? 0 : NS_MAX(0, GetColCount() - 1);
  PRInt32 tableColIncr = tableIsLTR ? 1 : -1; 
  nsPoint colGroupOrigin(aBorderPadding.left + cellSpacingX,
                         aBorderPadding.top + cellSpacingY);
  while (nsnull != colGroupFrame) {
    nscoord colGroupWidth = 0;
    nsTableIterator iterCol(*colGroupFrame);  
    nsIFrame* colFrame = iterCol.First();
    nsPoint colOrigin(0,0);
    while (nsnull != colFrame) {
      if (NS_STYLE_DISPLAY_TABLE_COLUMN ==
          colFrame->GetStyleDisplay()->mDisplay) {
        NS_ASSERTION(colX < GetColCount(), "invalid number of columns");
        nscoord colWidth = GetColumnWidth(colX);
        nsRect colRect(colOrigin.x, colOrigin.y, colWidth, colHeight);
        colFrame->SetRect(colRect);
        colOrigin.x += colWidth + cellSpacingX;
        colGroupWidth += colWidth + cellSpacingX;
        colX += tableColIncr;
      }
      colFrame = iterCol.Next();      
    }
    if (colGroupWidth) {
      colGroupWidth -= cellSpacingX;
    }

    nsRect colGroupRect(colGroupOrigin.x, colGroupOrigin.y, colGroupWidth, colHeight);
    colGroupFrame->SetRect(colGroupRect);
    colGroupFrame = iter.Next();
    colGroupOrigin.x += colGroupWidth + cellSpacingX;
  }
}

// SEC: TODO need to worry about continuing frames prev/next in flow for splitting across pages.

// XXX this could be made more general to handle row modifications that change the
// table height, but first we need to scrutinize every Invalidate
void
nsTableFrame::ProcessRowInserted(nscoord aNewHeight)
{
  SetRowInserted(PR_FALSE); // reset the bit that got us here
  nsTableFrame::RowGroupArray rowGroups;
  OrderRowGroups(rowGroups);
  // find the row group containing the inserted row
  for (PRUint32 rgX = 0; rgX < rowGroups.Length(); rgX++) {
    nsTableRowGroupFrame* rgFrame = rowGroups[rgX];
    NS_ASSERTION(rgFrame, "Must have rgFrame here");
    nsIFrame* childFrame = rgFrame->GetFirstChild(nsnull);
    // find the row that was inserted first
    while (childFrame) {
      nsTableRowFrame *rowFrame = do_QueryFrame(childFrame);
      if (rowFrame) {
        if (rowFrame->IsFirstInserted()) {
          rowFrame->SetFirstInserted(PR_FALSE);
          // damage the table from the 1st row inserted to the end of the table
          nscoord damageY = rgFrame->GetPosition().y + rowFrame->GetPosition().y;
          nsRect damageRect(0, damageY, GetSize().width, aNewHeight - damageY);

          Invalidate(damageRect);
          // XXXbz didn't we do this up front?  Why do we need to do it again?
          SetRowInserted(PR_FALSE);
          return; // found it, so leave
        }
      }
      childFrame = childFrame->GetNextSibling();
    }
  }
}

/* virtual */ void
nsTableFrame::MarkIntrinsicWidthsDirty()
{
  LayoutStrategy()->MarkIntrinsicWidthsDirty();

  // XXXldb Call SetBCDamageArea?

  nsHTMLContainerFrame::MarkIntrinsicWidthsDirty();
}

/* virtual */ nscoord
nsTableFrame::GetMinWidth(nsIRenderingContext *aRenderingContext)
{
  if (NeedToCalcBCBorders())
    CalcBCBorders();

  ReflowColGroups(aRenderingContext);

  return LayoutStrategy()->GetMinWidth(aRenderingContext);
}

/* virtual */ nscoord
nsTableFrame::GetPrefWidth(nsIRenderingContext *aRenderingContext)
{
  if (NeedToCalcBCBorders())
    CalcBCBorders();

  ReflowColGroups(aRenderingContext);

  return LayoutStrategy()->GetPrefWidth(aRenderingContext, PR_FALSE);
}

/* virtual */ nsIFrame::IntrinsicWidthOffsetData
nsTableFrame::IntrinsicWidthOffsets(nsIRenderingContext* aRenderingContext)
{
  IntrinsicWidthOffsetData result =
    nsHTMLContainerFrame::IntrinsicWidthOffsets(aRenderingContext);

  if (IsBorderCollapse()) {
    result.hPadding = 0;
    result.hPctPadding = 0;

    nsMargin outerBC = GetIncludedOuterBCBorder();
    result.hBorder = outerBC.LeftRight();
  }

  return result;
}

/* virtual */ nsSize
nsTableFrame::ComputeSize(nsIRenderingContext *aRenderingContext,
                          nsSize aCBSize, nscoord aAvailableWidth,
                          nsSize aMargin, nsSize aBorder, nsSize aPadding,
                          PRBool aShrinkWrap)
{
  nsSize result =
    nsHTMLContainerFrame::ComputeSize(aRenderingContext, aCBSize,
                                      aAvailableWidth,
                                      aMargin, aBorder, aPadding, aShrinkWrap);

  // Tables never shrink below their min width.
  nscoord minWidth = GetMinWidth(aRenderingContext);
  if (minWidth > result.width)
    result.width = minWidth;

  return result;
}

nscoord
nsTableFrame::TableShrinkWidthToFit(nsIRenderingContext *aRenderingContext,
                                    nscoord aWidthInCB)
{
  nscoord result;
  nscoord minWidth = GetMinWidth(aRenderingContext);
  if (minWidth > aWidthInCB) {
    result = minWidth;
  } else {
    // Tables shrink width to fit with a slightly different algorithm
    // from the one they use for their intrinsic widths (the difference
    // relates to handling of percentage widths on columns).  So this
    // function differs from nsFrame::ShrinkWidthToFit by only the
    // following line.
    // Since we've already called GetMinWidth, we don't need to do any
    // of the other stuff GetPrefWidth does.
    nscoord prefWidth =
      LayoutStrategy()->GetPrefWidth(aRenderingContext, PR_TRUE);
    if (prefWidth > aWidthInCB) {
      result = aWidthInCB;
    } else {
      result = prefWidth;
    }
  }
  return result;
}

/* virtual */ nsSize
nsTableFrame::ComputeAutoSize(nsIRenderingContext *aRenderingContext,
                              nsSize aCBSize, nscoord aAvailableWidth,
                              nsSize aMargin, nsSize aBorder, nsSize aPadding,
                              PRBool aShrinkWrap)
{
  // Tables always shrink-wrap.
  nscoord cbBased = aAvailableWidth - aMargin.width - aBorder.width -
                    aPadding.width;
  return nsSize(TableShrinkWidthToFit(aRenderingContext, cbBased),
                NS_UNCONSTRAINEDSIZE);
}

// Return true if aParentReflowState.frame or any of its ancestors within
// the containing table have non-auto height. (e.g. pct or fixed height)
PRBool
nsTableFrame::AncestorsHaveStyleHeight(const nsHTMLReflowState& aParentReflowState)
{
  for (const nsHTMLReflowState* rs = &aParentReflowState;
       rs && rs->frame; rs = rs->parentReflowState) {
    nsIAtom* frameType = rs->frame->GetType();
    if (IS_TABLE_CELL(frameType)                     ||
        (nsGkAtoms::tableRowFrame      == frameType) ||
        (nsGkAtoms::tableRowGroupFrame == frameType)) {
      if (rs->mStylePosition->mHeight.GetUnit() != eStyleUnit_Auto) {
        return PR_TRUE;
      }
    }
    else if (nsGkAtoms::tableFrame == frameType) {
      // we reached the containing table, so always return
      if (rs->mStylePosition->mHeight.GetUnit() != eStyleUnit_Auto) {
        return PR_TRUE;
      }
      else return PR_FALSE;
    }
  }
  return PR_FALSE;
}

// See if a special height reflow needs to occur and if so, call RequestSpecialHeightReflow
void
nsTableFrame::CheckRequestSpecialHeightReflow(const nsHTMLReflowState& aReflowState)
{
  if (!aReflowState.frame->GetPrevInFlow() &&  // 1st in flow
      (NS_UNCONSTRAINEDSIZE == aReflowState.ComputedHeight() ||  // no computed height
       0                    == aReflowState.ComputedHeight()) && 
      eStyleUnit_Percent == aReflowState.mStylePosition->mHeight.GetUnit() && // pct height
      nsTableFrame::AncestorsHaveStyleHeight(*aReflowState.parentReflowState)) {
    nsTableFrame::RequestSpecialHeightReflow(aReflowState);
  }
}

// Notify the frame and its ancestors (up to the containing table) that a special
// height reflow will occur. During a special height reflow, a table, row group,
// row, or cell returns the last size it was reflowed at. However, the table may 
// change the height of row groups, rows, cells in DistributeHeightToRows after. 
// And the row group can change the height of rows, cells in CalculateRowHeights.
void
nsTableFrame::RequestSpecialHeightReflow(const nsHTMLReflowState& aReflowState)
{
  // notify the frame and its ancestors of the special reflow, stopping at the containing table
  for (const nsHTMLReflowState* rs = &aReflowState; rs && rs->frame; rs = rs->parentReflowState) {
    nsIAtom* frameType = rs->frame->GetType();
    NS_ASSERTION(IS_TABLE_CELL(frameType) ||
                 nsGkAtoms::tableRowFrame == frameType ||
                 nsGkAtoms::tableRowGroupFrame == frameType ||
                 nsGkAtoms::tableFrame == frameType,
                 "unexpected frame type");
                 
    rs->frame->AddStateBits(NS_FRAME_CONTAINS_RELATIVE_HEIGHT);
    if (nsGkAtoms::tableFrame == frameType) {
      NS_ASSERTION(rs != &aReflowState,
                   "should not request special height reflow for table");
      // always stop when we reach a table
      break;
    }
  }
}

/******************************************************************************************
 * Before reflow, intrinsic width calculation is done using GetMinWidth
 * and GetPrefWidth.  This used to be known as pass 1 reflow.
 *
 * After the intrinsic width calculation, the table determines the
 * column widths using BalanceColumnWidths() and
 * then reflows each child again with a constrained avail width. This reflow is referred to
 * as the pass 2 reflow. 
 *
 * A special height reflow (pass 3 reflow) can occur during an initial or resize reflow
 * if (a) a row group, row, cell, or a frame inside a cell has a percent height but no computed 
 * height or (b) in paginated mode, a table has a height. (a) supports percent nested tables 
 * contained inside cells whose heights aren't known until after the pass 2 reflow. (b) is 
 * necessary because the table cannot split until after the pass 2 reflow. The mechanics of 
 * the special height reflow (variety a) are as follows: 
 * 
 * 1) Each table related frame (table, row group, row, cell) implements NeedsSpecialReflow()
 *    to indicate that it should get the reflow. It does this when it has a percent height but 
 *    no computed height by calling CheckRequestSpecialHeightReflow(). This method calls
 *    RequestSpecialHeightReflow() which calls SetNeedSpecialReflow() on its ancestors until 
 *    it reaches the containing table and calls SetNeedToInitiateSpecialReflow() on it. For 
 *    percent height frames inside cells, during DidReflow(), the cell's NotifyPercentHeight()
 *    is called (the cell is the reflow state's mPercentHeightObserver in this case). 
 *    NotifyPercentHeight() calls RequestSpecialHeightReflow().
 *
 * 2) After the pass 2 reflow, if the table's NeedToInitiateSpecialReflow(true) was called, it
 *    will do the special height reflow, setting the reflow state's mFlags.mSpecialHeightReflow
 *    to true and mSpecialHeightInitiator to itself. It won't do this if IsPrematureSpecialHeightReflow()
 *    returns true because in that case another special height reflow will be coming along with the
 *    containing table as the mSpecialHeightInitiator. It is only relevant to do the reflow when
 *    the mSpecialHeightInitiator is the containing table, because if it is a remote ancestor, then
 *    appropriate heights will not be known.
 *
 * 3) Since the heights of the table, row groups, rows, and cells was determined during the pass 2
 *    reflow, they return their last desired sizes during the special height reflow. The reflow only
 *    permits percent height frames inside the cells to resize based on the cells height and that height
 *    was determined during the pass 2 reflow.
 *
 * So, in the case of deeply nested tables, all of the tables that were told to initiate a special
 * reflow will do so, but if a table is already in a special reflow, it won't inititate the reflow
 * until the current initiator is its containing table. Since these reflows are only received by
 * frames that need them and they don't cause any rebalancing of tables, the extra overhead is minimal.
 *
 * The type of special reflow that occurs during printing (variety b) follows the same mechanism except
 * that all frames will receive the reflow even if they don't really need them.
 *
 * Open issues with the special height reflow:
 *
 * 1) At some point there should be 2 kinds of special height reflows because (a) and (b) above are 
 *    really quite different. This would avoid unnecessary reflows during printing. 
 * 2) When a cell contains frames whose percent heights > 100%, there is data loss (see bug 115245). 
 *    However, this can also occur if a cell has a fixed height and there is no special height reflow. 
 *
 * XXXldb Special height reflow should really be its own method, not
 * part of nsIFrame::Reflow.  It should then call nsIFrame::Reflow on
 * the contents of the cells to do the necessary vertical resizing.
 *
 ******************************************************************************************/

/* Layout the entire inner table. */
NS_METHOD nsTableFrame::Reflow(nsPresContext*           aPresContext,
                               nsHTMLReflowMetrics&     aDesiredSize,
                               const nsHTMLReflowState& aReflowState,
                               nsReflowStatus&          aStatus)
{
  DO_GLOBAL_REFLOW_COUNT("nsTableFrame");
  DISPLAY_REFLOW(aPresContext, this, aReflowState, aDesiredSize, aStatus);
  PRBool isPaginated = aPresContext->IsPaginated();

  aStatus = NS_FRAME_COMPLETE; 
  if (!GetPrevInFlow() && !mTableLayoutStrategy) {
    NS_ASSERTION(PR_FALSE, "strategy should have been created in Init");
    return NS_ERROR_NULL_POINTER;
  }
  nsresult rv = NS_OK;

  // see if collapsing borders need to be calculated
  if (!GetPrevInFlow() && IsBorderCollapse() && NeedToCalcBCBorders()) {
    CalcBCBorders();
  }

  aDesiredSize.width = aReflowState.availableWidth;

  // Check for an overflow list, and append any row group frames being pushed
  MoveOverflowToChildList(aPresContext);

  PRBool haveDesiredHeight = PR_FALSE;
  SetHaveReflowedColGroups(PR_FALSE);

  // Reflow the entire table (pass 2 and possibly pass 3). This phase is necessary during a 
  // constrained initial reflow and other reflows which require either a strategy init or balance. 
  // This isn't done during an unconstrained reflow, because it will occur later when the parent 
  // reflows with a constrained width.
  if (NS_SUBTREE_DIRTY(this) ||
      aReflowState.ShouldReflowAllKids() ||
      IsGeometryDirty() ||
      aReflowState.mFlags.mVResize) {

    if (aReflowState.ComputedHeight() != NS_UNCONSTRAINEDSIZE ||
        // Also check mVResize, to handle the first Reflow preceding a
        // special height Reflow, when we've already had a special height
        // Reflow (where mComputedHeight would not be
        // NS_UNCONSTRAINEDSIZE, but without a style change in between).
        aReflowState.mFlags.mVResize) {
      // XXX Eventually, we should modify DistributeHeightToRows to use
      // nsTableRowFrame::GetHeight instead of nsIFrame::GetSize().height.
      // That way, it will make its calculations based on internal table
      // frame heights as they are before they ever had any extra height
      // distributed to them.  In the meantime, this reflows all the
      // internal table frames, which restores them to their state before
      // DistributeHeightToRows was called.
      SetGeometryDirty();
    }

    PRBool needToInitiateSpecialReflow =
      !!(GetStateBits() & NS_FRAME_CONTAINS_RELATIVE_HEIGHT);
    // see if an extra reflow will be necessary in pagination mode when there is a specified table height 
    if (isPaginated && !GetPrevInFlow() && (NS_UNCONSTRAINEDSIZE != aReflowState.availableHeight)) {
      nscoord tableSpecifiedHeight = CalcBorderBoxHeight(aReflowState);
      if ((tableSpecifiedHeight > 0) && 
          (tableSpecifiedHeight != NS_UNCONSTRAINEDSIZE)) {
        needToInitiateSpecialReflow = PR_TRUE;
      }
    }
    nsIFrame* lastChildReflowed = nsnull;

    NS_ASSERTION(!aReflowState.mFlags.mSpecialHeightReflow,
                 "Shouldn't be in special height reflow here!");

    // do the pass 2 reflow unless this is a special height reflow and we will be 
    // initiating a special height reflow
    // XXXldb I changed this.  Should I change it back?

    // if we need to initiate a special height reflow, then don't constrain the 
    // height of the reflow before that
    nscoord availHeight = needToInitiateSpecialReflow 
                          ? NS_UNCONSTRAINEDSIZE : aReflowState.availableHeight;

    ReflowTable(aDesiredSize, aReflowState, availHeight,
                lastChildReflowed, aStatus);

    // reevaluate special height reflow conditions
    if (GetStateBits() & NS_FRAME_CONTAINS_RELATIVE_HEIGHT)
      needToInitiateSpecialReflow = PR_TRUE;

    // XXXldb Are all these conditions correct?
    if (needToInitiateSpecialReflow && NS_FRAME_IS_COMPLETE(aStatus)) {
      // XXXldb Do we need to set the mVResize flag on any reflow states?

      nsHTMLReflowState &mutable_rs =
        const_cast<nsHTMLReflowState&>(aReflowState);

      // distribute extra vertical space to rows
      CalcDesiredHeight(aReflowState, aDesiredSize); 
      mutable_rs.mFlags.mSpecialHeightReflow = PR_TRUE;

      ReflowTable(aDesiredSize, aReflowState, aReflowState.availableHeight, 
                  lastChildReflowed, aStatus);

      if (lastChildReflowed && NS_FRAME_IS_NOT_COMPLETE(aStatus)) {
        // if there is an incomplete child, then set the desired height to include it but not the next one
        nsMargin borderPadding = GetChildAreaOffset(&aReflowState);
        aDesiredSize.height = borderPadding.bottom + GetCellSpacingY() +
                              lastChildReflowed->GetRect().YMost();
      }
      haveDesiredHeight = PR_TRUE;

      mutable_rs.mFlags.mSpecialHeightReflow = PR_FALSE;
    }
  }
  else {
    // Calculate the overflow area contribution from our children.
    for (nsIFrame* kid = GetFirstChild(nsnull); kid; kid = kid->GetNextSibling()) {
      ConsiderChildOverflow(aDesiredSize.mOverflowArea, kid);
    }
  }

  aDesiredSize.width = aReflowState.ComputedWidth() +
                       aReflowState.mComputedBorderPadding.LeftRight();
  if (!haveDesiredHeight) {
    CalcDesiredHeight(aReflowState, aDesiredSize); 
  }
  if (IsRowInserted()) {
    ProcessRowInserted(aDesiredSize.height);
  }

  nsMargin borderPadding = GetChildAreaOffset(&aReflowState);
  SetColumnDimensions(aDesiredSize.height, borderPadding);
  if (NeedToCollapse() &&
      (NS_UNCONSTRAINEDSIZE != aReflowState.availableWidth)) {
    AdjustForCollapsingRowsCols(aDesiredSize, borderPadding);
  }

  // make sure the table overflow area does include the table rect.
  nsRect tableRect(0, 0, aDesiredSize.width, aDesiredSize.height) ;
  
  if (!aReflowState.mStyleDisplay->IsTableClip()) {
    // collapsed border may leak out
    nsMargin bcMargin = GetExcludedOuterBCBorder();
    tableRect.Inflate(bcMargin);
  }
  aDesiredSize.mOverflowArea.UnionRect(aDesiredSize.mOverflowArea, tableRect);
  
  if (GetStateBits() & NS_FRAME_FIRST_REFLOW) {
    // Fulfill the promise InvalidateFrame makes.
    Invalidate(aDesiredSize.mOverflowArea);
  } else {
    CheckInvalidateSizeChange(aDesiredSize);
  }

  FinishAndStoreOverflow(&aDesiredSize);
  NS_FRAME_SET_TRUNCATION(aStatus, aReflowState, aDesiredSize);
  return rv;
}

nsresult 
nsTableFrame::ReflowTable(nsHTMLReflowMetrics&     aDesiredSize,
                          const nsHTMLReflowState& aReflowState,
                          nscoord                  aAvailHeight,
                          nsIFrame*&               aLastChildReflowed,
                          nsReflowStatus&          aStatus)
{
  nsresult rv = NS_OK;
  aLastChildReflowed = nsnull;

  if (!GetPrevInFlow()) {
    mTableLayoutStrategy->ComputeColumnWidths(aReflowState);
  }
  // Constrain our reflow width to the computed table width (of the 1st in flow).
  // and our reflow height to our avail height minus border, padding, cellspacing
  aDesiredSize.width = aReflowState.ComputedWidth() +
                       aReflowState.mComputedBorderPadding.LeftRight();
  nsTableReflowState reflowState(*PresContext(), aReflowState, *this,
                                 aDesiredSize.width, aAvailHeight);
  ReflowChildren(reflowState, aStatus, aLastChildReflowed,
                 aDesiredSize.mOverflowArea);

  ReflowColGroups(aReflowState.rendContext);
  return rv;
}

nsIFrame*
nsTableFrame::GetFirstBodyRowGroupFrame()
{
  nsIFrame* headerFrame = nsnull;
  nsIFrame* footerFrame = nsnull;

  for (nsIFrame* kidFrame = mFrames.FirstChild(); nsnull != kidFrame; ) {
    const nsStyleDisplay* childDisplay = kidFrame->GetStyleDisplay();

    // We expect the header and footer row group frames to be first, and we only
    // allow one header and one footer
    if (NS_STYLE_DISPLAY_TABLE_HEADER_GROUP == childDisplay->mDisplay) {
      if (headerFrame) {
        // We already have a header frame and so this header frame is treated
        // like an ordinary body row group frame
        return kidFrame;
      }
      headerFrame = kidFrame;
    
    } else if (NS_STYLE_DISPLAY_TABLE_FOOTER_GROUP == childDisplay->mDisplay) {
      if (footerFrame) {
        // We already have a footer frame and so this footer frame is treated
        // like an ordinary body row group frame
        return kidFrame;
      }
      footerFrame = kidFrame;

    } else if (NS_STYLE_DISPLAY_TABLE_ROW_GROUP == childDisplay->mDisplay) {
      return kidFrame;
    }

    // Get the next child
    kidFrame = kidFrame->GetNextSibling();
  }

  return nsnull;
}

// Table specific version that takes into account repeated header and footer
// frames when continuing table frames
void
nsTableFrame::PushChildren(const RowGroupArray& aRowGroups,
                           PRInt32 aPushFrom)
{
  NS_PRECONDITION(aPushFrom > 0, "pushing first child");

  // extract the frames from the array into a sibling list
  nsFrameList frames;
  PRUint32 childX;
  for (childX = aPushFrom; childX < aRowGroups.Length(); ++childX) {
    nsTableRowGroupFrame* rgFrame = aRowGroups[childX];
    if (!rgFrame || !rgFrame->IsRepeatable()) {
      mFrames.RemoveFrame(rgFrame);
      frames.AppendFrame(nsnull, rgFrame);
    }
  }

  if (nsnull != GetNextInFlow()) {
    nsTableFrame* nextInFlow = (nsTableFrame*)GetNextInFlow();

    // Insert the frames after any repeated header and footer frames
    nsIFrame* firstBodyFrame = nextInFlow->GetFirstBodyRowGroupFrame();
    nsIFrame* prevSibling = nsnull;
    if (firstBodyFrame) {
      prevSibling = firstBodyFrame->GetPrevSibling();
    }
    // When pushing and pulling frames we need to check for whether any
    // views need to be reparented.
    ReparentFrameViewList(PresContext(), frames, this, nextInFlow);
    nextInFlow->mFrames.InsertFrames(nextInFlow, prevSibling,
                                     frames);
  }
  else if (frames.NotEmpty()) {
    // Add the frames to our overflow list
    SetOverflowFrames(PresContext(), frames);
  }
}

// collapsing row groups, rows, col groups and cols are accounted for after both passes of
// reflow so that it has no effect on the calculations of reflow.
void
nsTableFrame::AdjustForCollapsingRowsCols(nsHTMLReflowMetrics& aDesiredSize,
                                          nsMargin             aBorderPadding)
{
  nscoord yTotalOffset = 0; // total offset among all rows in all row groups

  // reset the bit, it will be set again if row/rowgroup or col/colgroup are
  // collapsed
  SetNeedToCollapse(PR_FALSE);

  // collapse the rows and/or row groups as necessary
  // Get the ordered children
  RowGroupArray rowGroups;
  OrderRowGroups(rowGroups);
  
  nsTableFrame* firstInFlow = static_cast<nsTableFrame*> (GetFirstInFlow());
  nscoord width = firstInFlow->GetCollapsedWidth(aBorderPadding);
  nscoord rgWidth = width - 2 * GetCellSpacingX();
  nsRect overflowArea(0, 0, 0, 0);
  // Walk the list of children
  for (PRUint32 childX = 0; childX < rowGroups.Length(); childX++) {
    nsTableRowGroupFrame* rgFrame = rowGroups[childX];
    NS_ASSERTION(rgFrame, "Must have row group frame here");
    yTotalOffset += rgFrame->CollapseRowGroupIfNecessary(yTotalOffset, rgWidth);
    ConsiderChildOverflow(overflowArea, rgFrame);
  } 

  aDesiredSize.height -= yTotalOffset;
  aDesiredSize.width   = width;
  overflowArea.UnionRect(nsRect(0, 0, aDesiredSize.width, aDesiredSize.height),
                         overflowArea);
  FinishAndStoreOverflow(&overflowArea,
                         nsSize(aDesiredSize.width, aDesiredSize.height));
}


nscoord
nsTableFrame::GetCollapsedWidth(nsMargin aBorderPadding)
{
  NS_ASSERTION(!GetPrevInFlow(), "GetCollapsedWidth called on next in flow");
  nscoord cellSpacingX = GetCellSpacingX();
  nscoord width = cellSpacingX;
  width += aBorderPadding.left + aBorderPadding.right;
  for (nsIFrame* groupFrame = mColGroups.FirstChild(); groupFrame;
         groupFrame = groupFrame->GetNextSibling()) {
    const nsStyleVisibility* groupVis = groupFrame->GetStyleVisibility();
    PRBool collapseGroup = (NS_STYLE_VISIBILITY_COLLAPSE == groupVis->mVisible);
    nsTableColGroupFrame* cgFrame = (nsTableColGroupFrame*)groupFrame;
    for (nsTableColFrame* colFrame = cgFrame->GetFirstColumn(); colFrame;
         colFrame = colFrame->GetNextCol()) {
      const nsStyleDisplay* colDisplay = colFrame->GetStyleDisplay();
      PRInt32 colX = colFrame->GetColIndex();
      if (NS_STYLE_DISPLAY_TABLE_COLUMN == colDisplay->mDisplay) {
        const nsStyleVisibility* colVis = colFrame->GetStyleVisibility();
        PRBool collapseCol = (NS_STYLE_VISIBILITY_COLLAPSE == colVis->mVisible);
        PRInt32 colWidth = GetColumnWidth(colX);
        if (!collapseGroup && !collapseCol) {
          width += colWidth;
          if (ColumnHasCellSpacingBefore(colX))
            width += cellSpacingX;
        }
        else {
          SetNeedToCollapse(PR_TRUE);
        }
      }
    }
  }
  return width;
}

/* virtual */ void
nsTableFrame::DidSetStyleContext(nsStyleContext* aOldStyleContext)
{
   if (!aOldStyleContext) //avoid this on init
     return;
   
   if (IsBorderCollapse() &&
       BCRecalcNeeded(aOldStyleContext, GetStyleContext())) {
     nsRect damageArea(0, 0, GetColCount(), GetRowCount());
     SetBCDamageArea(damageArea);
   }

   //avoid this on init or nextinflow
   if (!mTableLayoutStrategy || GetPrevInFlow())
     return;
     
   PRBool isAuto = IsAutoLayout();
   if (isAuto != (LayoutStrategy()->GetType() == nsITableLayoutStrategy::Auto)) {
     nsITableLayoutStrategy* temp;
     if (isAuto)
       temp = new BasicTableLayoutStrategy(this);
     else
       temp = new FixedTableLayoutStrategy(this);
     
     if (temp) {
       delete mTableLayoutStrategy;
       mTableLayoutStrategy = temp;
     }
  }
}



NS_IMETHODIMP
nsTableFrame::AppendFrames(nsIAtom*        aListName,
                           nsFrameList&    aFrameList)
{
  NS_ASSERTION(!aListName || aListName == nsGkAtoms::colGroupList,
               "unexpected child list");

  // Because we actually have two child lists, one for col group frames and one
  // for everything else, we need to look at each frame individually
  // XXX The frame construction code should be separating out child frames
  // based on the type, bug 343048.
  while (!aFrameList.IsEmpty()) {
    nsIFrame* f = aFrameList.FirstChild();
    aFrameList.RemoveFrame(f);

    // See what kind of frame we have
    const nsStyleDisplay* display = f->GetStyleDisplay();

    if (NS_STYLE_DISPLAY_TABLE_COLUMN_GROUP == display->mDisplay) {
      nsTableColGroupFrame* lastColGroup =
        nsTableColGroupFrame::GetLastRealColGroup(this);
      PRInt32 startColIndex = (lastColGroup) 
        ? lastColGroup->GetStartColumnIndex() + lastColGroup->GetColCount() : 0;
      mColGroups.InsertFrame(nsnull, lastColGroup, f);
      // Insert the colgroup and its cols into the table
      InsertColGroups(startColIndex,
                      nsFrameList::Slice(mColGroups, f, f->GetNextSibling()));
    } else if (IsRowGroup(display->mDisplay)) {
      // Append the new row group frame to the sibling chain
      mFrames.AppendFrame(nsnull, f);

      // insert the row group and its rows into the table
      InsertRowGroups(nsFrameList::Slice(mFrames, f, nsnull));
    } else {
      // Nothing special to do, just add the frame to our child list
      NS_NOTREACHED("How did we get here?  Frame construction screwed up");
      mFrames.AppendFrame(nsnull, f);
    }
  }

#ifdef DEBUG_TABLE_CELLMAP
  printf("=== TableFrame::AppendFrames\n");
  Dump(PR_TRUE, PR_TRUE, PR_TRUE);
#endif
  PresContext()->PresShell()->FrameNeedsReflow(this, nsIPresShell::eTreeChange,
                                               NS_FRAME_HAS_DIRTY_CHILDREN);
  SetGeometryDirty();

  return NS_OK;
}

NS_IMETHODIMP
nsTableFrame::InsertFrames(nsIAtom*        aListName,
                           nsIFrame*       aPrevFrame,
                           nsFrameList&    aFrameList)
{
  // Asssume there's only one frame being inserted. The problem is that
  // row group frames and col group frames go in separate child lists and
  // so if there's more than one type of frames this gets messy...
  // XXX The frame construction code should be separating out child frames
  // based on the type, bug 343048.
 
  NS_ASSERTION(!aPrevFrame || aPrevFrame->GetParent() == this,
               "inserting after sibling frame with different parent");

  if ((aPrevFrame && !aPrevFrame->GetNextSibling()) ||
      (!aPrevFrame && GetChildList(aListName).IsEmpty())) {
    // Treat this like an append; still a workaround for bug 343048.
    return AppendFrames(aListName, aFrameList);
  }

  // See what kind of frame we have
  const nsStyleDisplay* display = aFrameList.FirstChild()->GetStyleDisplay();
#ifdef DEBUG
  // verify that all sibling have the same type, if they do not, expect cellmap issues
  for (nsFrameList::Enumerator e(aFrameList); !e.AtEnd(); e.Next()) {
    const nsStyleDisplay* nextDisplay = e.get()->GetStyleDisplay();
    NS_ASSERTION((display->mDisplay == NS_STYLE_DISPLAY_TABLE_COLUMN_GROUP) ==
        (nextDisplay->mDisplay == NS_STYLE_DISPLAY_TABLE_COLUMN_GROUP),
      "heterogenous childlist");  
  }
#endif  
  if (aPrevFrame) {
    const nsStyleDisplay* prevDisplay = aPrevFrame->GetStyleDisplay();
    // Make sure they belong on the same frame list
    if ((display->mDisplay == NS_STYLE_DISPLAY_TABLE_COLUMN_GROUP) !=
        (prevDisplay->mDisplay == NS_STYLE_DISPLAY_TABLE_COLUMN_GROUP)) {
      // the previous frame is not valid, see comment at ::AppendFrames
      // XXXbz Using content indices here means XBL will get screwed
      // over...  Oh, well.
      nsIFrame* pseudoFrame = aFrameList.FirstChild();
      nsIContent* parentContent = GetContent();
      nsIContent* content;
      aPrevFrame = nsnull;
      while (pseudoFrame  && (parentContent ==
                              (content = pseudoFrame->GetContent()))) {
        pseudoFrame = pseudoFrame->GetFirstChild(nsnull);
      }
      nsCOMPtr<nsIContent> container = content->GetParent();
      if (NS_LIKELY(container)) { // XXX need this null-check, see bug 411823.
        PRInt32 newIndex = container->IndexOf(content);
        nsIFrame* kidFrame;
        PRBool isColGroup = (NS_STYLE_DISPLAY_TABLE_COLUMN_GROUP ==
                             display->mDisplay);
        nsTableColGroupFrame* lastColGroup;
        if (isColGroup) {
          kidFrame = mColGroups.FirstChild();
          lastColGroup = nsTableColGroupFrame::GetLastRealColGroup(this);
        }
        else {
          kidFrame = mFrames.FirstChild();
        }
        // Important: need to start at a value smaller than all valid indices
        PRInt32 lastIndex = -1;
        while (kidFrame) {
          if (isColGroup) {
            if (kidFrame == lastColGroup) {
              aPrevFrame = kidFrame; // there is no real colgroup after this one
              break;
            }
          }
          pseudoFrame = kidFrame;
          while (pseudoFrame  && (parentContent ==
                                  (content = pseudoFrame->GetContent()))) {
            pseudoFrame = pseudoFrame->GetFirstChild(nsnull);
          }
          PRInt32 index = container->IndexOf(content);
          if (index > lastIndex && index < newIndex) {
            lastIndex = index;
            aPrevFrame = kidFrame;
          }
          kidFrame = kidFrame->GetNextSibling();
        }
      }
    }
  }
  if (NS_STYLE_DISPLAY_TABLE_COLUMN_GROUP == display->mDisplay) {
    NS_ASSERTION(!aListName || aListName == nsGkAtoms::colGroupList,
                 "unexpected child list");
    // Insert the column group frames
    const nsFrameList::Slice& newColgroups =
      mColGroups.InsertFrames(nsnull, aPrevFrame, aFrameList);
    // find the starting col index for the first new col group
    PRInt32 startColIndex = 0;
    if (aPrevFrame) {
      nsTableColGroupFrame* prevColGroup = 
        (nsTableColGroupFrame*)GetFrameAtOrBefore(this, aPrevFrame,
                                                  nsGkAtoms::tableColGroupFrame);
      if (prevColGroup) {
        startColIndex = prevColGroup->GetStartColumnIndex() + prevColGroup->GetColCount();
      }
    }
    InsertColGroups(startColIndex, newColgroups);
  } else if (IsRowGroup(display->mDisplay)) {
    NS_ASSERTION(!aListName, "unexpected child list");
    // Insert the frames in the sibling chain
    const nsFrameList::Slice& newRowGroups =
      mFrames.InsertFrames(nsnull, aPrevFrame, aFrameList);

    InsertRowGroups(newRowGroups);
  } else {
    NS_ASSERTION(!aListName, "unexpected child list");
    NS_NOTREACHED("How did we even get here?");
    // Just insert the frame and don't worry about reflowing it
    mFrames.InsertFrames(nsnull, aPrevFrame, aFrameList);
    return NS_OK;
  }

  PresContext()->PresShell()->FrameNeedsReflow(this, nsIPresShell::eTreeChange,
                                               NS_FRAME_HAS_DIRTY_CHILDREN);
  SetGeometryDirty();
#ifdef DEBUG_TABLE_CELLMAP
  printf("=== TableFrame::InsertFrames\n");
  Dump(PR_TRUE, PR_TRUE, PR_TRUE);
#endif
  return NS_OK;
}

NS_IMETHODIMP
nsTableFrame::RemoveFrame(nsIAtom*        aListName,
                          nsIFrame*       aOldFrame)
{
  NS_ASSERTION(aListName == nsGkAtoms::colGroupList ||
               NS_STYLE_DISPLAY_TABLE_COLUMN_GROUP !=
                 aOldFrame->GetStyleDisplay()->mDisplay,
               "Wrong list name; use nsGkAtoms::colGroupList iff colgroup");
  if (aListName == nsGkAtoms::colGroupList) {
    nsIFrame* nextColGroupFrame = aOldFrame->GetNextSibling();
    nsTableColGroupFrame* colGroup = (nsTableColGroupFrame*)aOldFrame;
    PRInt32 firstColIndex = colGroup->GetStartColumnIndex();
    PRInt32 lastColIndex  = firstColIndex + colGroup->GetColCount() - 1;
    mColGroups.DestroyFrame(aOldFrame);
    nsTableColGroupFrame::ResetColIndices(nextColGroupFrame, firstColIndex);
    // remove the cols from the table
    PRInt32 colX;
    for (colX = lastColIndex; colX >= firstColIndex; colX--) {
      nsTableColFrame* colFrame = mColFrames.SafeElementAt(colX);
      if (colFrame) {
        RemoveCol(colGroup, colX, PR_TRUE, PR_FALSE);
      }
    }

    PRInt32 numAnonymousColsToAdd = GetColCount() - mColFrames.Length();
    if (numAnonymousColsToAdd > 0) {
      // this sets the child list, updates the col cache and cell map
      AppendAnonymousColFrames(numAnonymousColsToAdd);
    }

  } else {
    NS_ASSERTION(!aListName, "unexpected child list");
    nsTableRowGroupFrame* rgFrame =
      static_cast<nsTableRowGroupFrame*>(aOldFrame);
    // remove the row group from the cell map
    nsTableCellMap* cellMap = GetCellMap();
    if (cellMap) {
      cellMap->RemoveGroupCellMap(rgFrame);
    }

    // remove the row group frame from the sibling chain
    mFrames.DestroyFrame(aOldFrame);

    // the removal of a row group changes the cellmap, the columns might change
    if (cellMap) {
      cellMap->Synchronize(this);
      // Create an empty slice
      ResetRowIndices(nsFrameList::Slice(mFrames, nsnull, nsnull));
      nsRect damageArea;
      cellMap->RebuildConsideringCells(nsnull, nsnull, 0, 0, PR_FALSE, damageArea);
    }

    MatchCellMapToColCache(cellMap);
  }
  // for now, just bail and recalc all of the collapsing borders
  // as the cellmap changes we need to recalc
  if (IsBorderCollapse()) {
    nsRect damageArea(0, 0, NS_MAX(1, GetColCount()), NS_MAX(1, GetRowCount()));
    SetBCDamageArea(damageArea);
  }
  PresContext()->PresShell()->FrameNeedsReflow(this, nsIPresShell::eTreeChange,
                                               NS_FRAME_HAS_DIRTY_CHILDREN);
  SetGeometryDirty();
#ifdef DEBUG_TABLE_CELLMAP
  printf("=== TableFrame::RemoveFrame\n");
  Dump(PR_TRUE, PR_TRUE, PR_TRUE);
#endif
  return NS_OK;
}

/* virtual */ nsMargin
nsTableFrame::GetUsedBorder() const
{
  if (!IsBorderCollapse())
    return nsHTMLContainerFrame::GetUsedBorder();
  
  return GetIncludedOuterBCBorder();
}

/* virtual */ nsMargin
nsTableFrame::GetUsedPadding() const
{
  if (!IsBorderCollapse())
    return nsHTMLContainerFrame::GetUsedPadding();

  return nsMargin(0,0,0,0);
}

static void
DivideBCBorderSize(BCPixelSize  aPixelSize,
                   BCPixelSize& aSmallHalf,
                   BCPixelSize& aLargeHalf)
{
  aSmallHalf = aPixelSize / 2;
  aLargeHalf = aPixelSize - aSmallHalf;
}

nsMargin
nsTableFrame::GetOuterBCBorder() const
{
  if (NeedToCalcBCBorders())
    const_cast<nsTableFrame*>(this)->CalcBCBorders();

  nsMargin border(0, 0, 0, 0);
  PRInt32 p2t = nsPresContext::AppUnitsPerCSSPixel();
  BCPropertyData* propData = 
    (BCPropertyData*)nsTableFrame::GetProperty((nsIFrame*)this, nsGkAtoms::tableBCProperty, PR_FALSE);
  if (propData) {
    border.top    = BC_BORDER_TOP_HALF_COORD(p2t, propData->mTopBorderWidth);
    border.right  = BC_BORDER_RIGHT_HALF_COORD(p2t, propData->mRightBorderWidth);
    border.bottom = BC_BORDER_BOTTOM_HALF_COORD(p2t, propData->mBottomBorderWidth);
    border.left   = BC_BORDER_LEFT_HALF_COORD(p2t, propData->mLeftBorderWidth);
  }
  return border;
}

nsMargin
nsTableFrame::GetIncludedOuterBCBorder() const
{
  if (NeedToCalcBCBorders())
    const_cast<nsTableFrame*>(this)->CalcBCBorders();

  nsMargin border(0, 0, 0, 0);
  PRInt32 p2t = nsPresContext::AppUnitsPerCSSPixel();
  BCPropertyData* propData =
    (BCPropertyData*)nsTableFrame::GetProperty((nsIFrame*)this,
                                                nsGkAtoms::tableBCProperty,
                                                PR_FALSE);
  if (propData) {
    border.top += BC_BORDER_TOP_HALF_COORD(p2t, propData->mTopBorderWidth);
    border.right += BC_BORDER_RIGHT_HALF_COORD(p2t, propData->mRightCellBorderWidth);
    border.bottom += BC_BORDER_BOTTOM_HALF_COORD(p2t, propData->mBottomBorderWidth);
    border.left += BC_BORDER_LEFT_HALF_COORD(p2t, propData->mLeftCellBorderWidth);
  }
  return border;
}

nsMargin
nsTableFrame::GetExcludedOuterBCBorder() const
{
  return GetOuterBCBorder() - GetIncludedOuterBCBorder();
}

static
void GetSeparateModelBorderPadding(const nsHTMLReflowState* aReflowState,
                                   nsStyleContext&          aStyleContext,
                                   nsMargin&                aBorderPadding)
{
  // XXXbz Either we _do_ have a reflow state and then we can use its
  // mComputedBorderPadding or we don't and then we get the padding
  // wrong!
  const nsStyleBorder* border = aStyleContext.GetStyleBorder();
  aBorderPadding = border->GetActualBorder();
  if (aReflowState) {
    aBorderPadding += aReflowState->mComputedPadding;
  }
}

nsMargin 
nsTableFrame::GetChildAreaOffset(const nsHTMLReflowState* aReflowState) const
{
  nsMargin offset(0,0,0,0);
  if (IsBorderCollapse()) {
    offset = GetIncludedOuterBCBorder();
  }
  else {
    GetSeparateModelBorderPadding(aReflowState, *mStyleContext, offset);
  }
  return offset;
}

void
nsTableFrame::InitChildReflowState(nsHTMLReflowState& aReflowState)                                    
{
  nsMargin collapseBorder;
  nsMargin padding(0,0,0,0);
  nsMargin* pCollapseBorder = nsnull;
  nsPresContext* presContext = PresContext();
  if (IsBorderCollapse()) {
    nsTableRowGroupFrame* rgFrame =
       static_cast<nsTableRowGroupFrame*>(aReflowState.frame);
    pCollapseBorder = rgFrame->GetBCBorderWidth(collapseBorder);
  }
  aReflowState.Init(presContext, -1, -1, pCollapseBorder, &padding);

  NS_ASSERTION(!mBits.mResizedColumns ||
               !aReflowState.parentReflowState->mFlags.mSpecialHeightReflow,
               "should not resize columns on special height reflow");
  if (mBits.mResizedColumns) {
    aReflowState.mFlags.mHResize = PR_TRUE;
  }
}

// Position and size aKidFrame and update our reflow state. The origin of
// aKidRect is relative to the upper-left origin of our frame
void nsTableFrame::PlaceChild(nsTableReflowState&  aReflowState,
                              nsIFrame*            aKidFrame,
                              nsHTMLReflowMetrics& aKidDesiredSize,
                              const nsRect&        aOriginalKidRect,
                              const nsRect&        aOriginalKidOverflowRect)
{
  PRBool isFirstReflow =
    (aKidFrame->GetStateBits() & NS_FRAME_FIRST_REFLOW) != 0;
  
  // Place and size the child
  FinishReflowChild(aKidFrame, PresContext(), nsnull, aKidDesiredSize,
                    aReflowState.x, aReflowState.y, 0);

  InvalidateFrame(aKidFrame, aOriginalKidRect, aOriginalKidOverflowRect,
                  isFirstReflow);

  // Adjust the running y-offset
  aReflowState.y += aKidDesiredSize.height;

  // If our height is constrained, then update the available height
  if (NS_UNCONSTRAINEDSIZE != aReflowState.availSize.height) {
    aReflowState.availSize.height -= aKidDesiredSize.height;
  }
}

void
nsTableFrame::OrderRowGroups(RowGroupArray& aChildren,
                             nsTableRowGroupFrame** aHead,
                             nsTableRowGroupFrame** aFoot) const
{
  aChildren.Clear();
  nsTableRowGroupFrame* head = nsnull;
  nsTableRowGroupFrame* foot = nsnull;
  
  nsIFrame* kidFrame = mFrames.FirstChild();
  while (kidFrame) {
    const nsStyleDisplay* kidDisplay = kidFrame->GetStyleDisplay();
    nsTableRowGroupFrame* rowGroup =
      static_cast<nsTableRowGroupFrame*>(kidFrame);

    switch (kidDisplay->mDisplay) {
    case NS_STYLE_DISPLAY_TABLE_HEADER_GROUP:
      if (head) { // treat additional thead like tbody
        aChildren.AppendElement(rowGroup);
      }
      else {
        head = rowGroup;
      }
      break;
    case NS_STYLE_DISPLAY_TABLE_FOOTER_GROUP:
      if (foot) { // treat additional tfoot like tbody
        aChildren.AppendElement(rowGroup);
      }
      else {
        foot = rowGroup;
      }
      break;
    case NS_STYLE_DISPLAY_TABLE_ROW_GROUP:
      aChildren.AppendElement(rowGroup);
      break;
    default:
      NS_NOTREACHED("How did this produce an nsTableRowGroupFrame?");
      // Just ignore it
      break;
    }
    // Get the next sibling but skip it if it's also the next-in-flow, since
    // a next-in-flow will not be part of the current table.
    while (kidFrame) {
      nsIFrame* nif = kidFrame->GetNextInFlow();
      kidFrame = kidFrame->GetNextSibling();
      if (kidFrame != nif) 
        break;
    }
  }

  // put the thead first
  if (head) {
    aChildren.InsertElementAt(0, head);
  }
  if (aHead)
    *aHead = head;
  // put the tfoot after the last tbody
  if (foot) {
    aChildren.AppendElement(foot);
  }
  if (aFoot)
    *aFoot = foot;
}

nsTableRowGroupFrame*
nsTableFrame::GetTHead() const
{
  nsIFrame* kidFrame = mFrames.FirstChild();
  while (kidFrame) {
    if (kidFrame->GetStyleDisplay()->mDisplay ==
          NS_STYLE_DISPLAY_TABLE_HEADER_GROUP) {
      return static_cast<nsTableRowGroupFrame*>(kidFrame);
    }

    // Get the next sibling but skip it if it's also the next-in-flow, since
    // a next-in-flow will not be part of the current table.
    while (kidFrame) {
      nsIFrame* nif = kidFrame->GetNextInFlow();
      kidFrame = kidFrame->GetNextSibling();
      if (kidFrame != nif) 
        break;
    }
  }

  return nsnull;
}

nsTableRowGroupFrame*
nsTableFrame::GetTFoot() const
{
  nsIFrame* kidFrame = mFrames.FirstChild();
  while (kidFrame) {
    if (kidFrame->GetStyleDisplay()->mDisplay ==
          NS_STYLE_DISPLAY_TABLE_FOOTER_GROUP) {
      return static_cast<nsTableRowGroupFrame*>(kidFrame);
    }

    // Get the next sibling but skip it if it's also the next-in-flow, since
    // a next-in-flow will not be part of the current table.
    while (kidFrame) {
      nsIFrame* nif = kidFrame->GetNextInFlow();
      kidFrame = kidFrame->GetNextSibling();
      if (kidFrame != nif) 
        break;
    }
  }

  return nsnull;
}

static PRBool
IsRepeatable(nscoord aFrameHeight, nscoord aPageHeight)
{
  return aFrameHeight < (aPageHeight / 4);
}

nsresult
nsTableFrame::SetupHeaderFooterChild(const nsTableReflowState& aReflowState,
                                     nsTableRowGroupFrame* aFrame,
                                     nscoord* aDesiredHeight)
{
  nsPresContext* presContext = PresContext();
  nscoord pageHeight = presContext->GetPageSize().height;

  // Reflow the child with unconstrainted height
  nsHTMLReflowState kidReflowState(presContext, aReflowState.reflowState,
                                   aFrame,
                                   nsSize(aReflowState.availSize.width, NS_UNCONSTRAINEDSIZE),
                                   -1, -1, PR_FALSE);
  InitChildReflowState(kidReflowState);
  kidReflowState.mFlags.mIsTopOfPage = PR_TRUE;
  nsHTMLReflowMetrics desiredSize;
  desiredSize.width = desiredSize.height = 0;
  nsReflowStatus status;
  nsresult rv = ReflowChild(aFrame, presContext, desiredSize, kidReflowState,
                            aReflowState.x, aReflowState.y, 0, status);
  NS_ENSURE_SUCCESS(rv, rv);
  // The child will be reflowed again "for real" so no need to place it now

  aFrame->SetRepeatable(IsRepeatable(desiredSize.height, pageHeight));
  *aDesiredHeight = desiredSize.height;
  return NS_OK;
}

// Reflow the children based on the avail size and reason in aReflowState
// update aReflowMetrics a aStatus
NS_METHOD 
nsTableFrame::ReflowChildren(nsTableReflowState& aReflowState,
                             nsReflowStatus&     aStatus,
                             nsIFrame*&          aLastChildReflowed,
                             nsRect&             aOverflowArea)
{
  aStatus = NS_FRAME_COMPLETE;
  aLastChildReflowed = nsnull;

  nsIFrame* prevKidFrame = nsnull;
  nsresult  rv = NS_OK;
  nscoord   cellSpacingY = GetCellSpacingY();

  nsPresContext* presContext = PresContext();
  // XXXldb Should we be checking constrained height instead?
  PRBool isPaginated = presContext->IsPaginated();

  aOverflowArea = nsRect (0, 0, 0, 0);

  PRBool reflowAllKids = aReflowState.reflowState.ShouldReflowAllKids() ||
                         mBits.mResizedColumns ||
                         IsGeometryDirty();

  RowGroupArray rowGroups;
  nsTableRowGroupFrame *thead, *tfoot;
  OrderRowGroups(rowGroups, &thead, &tfoot);
  PRBool pageBreak = PR_FALSE;
  nscoord footerHeight = 0;

  // Determine the repeatablility of headers and footers, and also the desired
  // height of any repeatable footer.
  // The repeatability of headers on continued tables is handled
  // when they are created in nsCSSFrameConstructor::CreateContinuingTableFrame.
  // We handle the repeatability of footers again here because we need to
  // determine the footer's height anyway. We could perhaps optimize by
  // using the footer's prev-in-flow's height instead of reflowing it again,
  // but there's no real need.
  if (isPaginated) {
    if (thead && !GetPrevInFlow()) {
      nscoord desiredHeight;
      rv = SetupHeaderFooterChild(aReflowState, thead, &desiredHeight);
      if (NS_FAILED(rv))
        return rv;
    }
    if (tfoot) {
      rv = SetupHeaderFooterChild(aReflowState, tfoot, &footerHeight);
      if (NS_FAILED(rv))
        return rv;
    }
  }

  for (PRUint32 childX = 0; childX < rowGroups.Length(); childX++) {
    nsIFrame* kidFrame = rowGroups[childX];
    // Get the frame state bits
    // See if we should only reflow the dirty child frames
    if (reflowAllKids ||
        NS_SUBTREE_DIRTY(kidFrame) ||
        (aReflowState.reflowState.mFlags.mSpecialHeightReflow &&
         (isPaginated || (kidFrame->GetStateBits() &
                          NS_FRAME_CONTAINS_RELATIVE_HEIGHT)))) {
      if (pageBreak) {
        PushChildren(rowGroups, childX);
        aStatus = NS_FRAME_NOT_COMPLETE;
        break;
      }

      nsSize kidAvailSize(aReflowState.availSize);
      // if the child is a tbody in paginated mode reduce the height by a repeated footer
      PRBool allowRepeatedFooter = PR_FALSE;
      if (isPaginated && (NS_UNCONSTRAINEDSIZE != kidAvailSize.height)) {
        nsTableRowGroupFrame* kidRG =
          static_cast<nsTableRowGroupFrame*>(kidFrame);
        if (kidRG != thead && kidRG != tfoot && tfoot && tfoot->IsRepeatable()) {
          // the child is a tbody and there is a repeatable footer
          NS_ASSERTION(tfoot == rowGroups[rowGroups.Length() - 1], "Missing footer!");
          if (footerHeight + cellSpacingY < kidAvailSize.height) {
            allowRepeatedFooter = PR_TRUE;
            kidAvailSize.height -= footerHeight + cellSpacingY;
          }
        }
      }

      nsRect oldKidRect = kidFrame->GetRect();
      nsRect oldKidOverflowRect = kidFrame->GetOverflowRect();

      nsHTMLReflowMetrics desiredSize;
      desiredSize.width = desiredSize.height = 0;
  
      // Reflow the child into the available space
      nsHTMLReflowState kidReflowState(presContext, aReflowState.reflowState,
                                       kidFrame, kidAvailSize,
                                       -1, -1, PR_FALSE);
      InitChildReflowState(kidReflowState);

      // If this isn't the first row group, and the previous row group has a
      // nonzero YMost, then we can't be at the top of the page.
      // We ignore the head row group in this check, because a head row group
      // may be automatically added at the top of *every* page.  This prevents
      // infinite loops in some circumstances - see bug 344883.
      if (childX > (thead ? 1 : 0) &&
          (rowGroups[childX - 1]->GetRect().YMost() > 0)) {
        kidReflowState.mFlags.mIsTopOfPage = PR_FALSE;
      }
      aReflowState.y += cellSpacingY;
      if (NS_UNCONSTRAINEDSIZE != aReflowState.availSize.height) {
        aReflowState.availSize.height -= cellSpacingY;
      }
      // record the presence of a next in flow, it might get destroyed so we
      // need to reorder the row group array
      nsIFrame* kidNextInFlow = kidFrame->GetNextInFlow();
      PRBool reorder = PR_FALSE;
      if (kidFrame->GetNextInFlow())
        reorder = PR_TRUE;

      rv = ReflowChild(kidFrame, presContext, desiredSize, kidReflowState,
                       aReflowState.x, aReflowState.y,
                       NS_FRAME_INVALIDATE_ON_MOVE, aStatus);

      if (reorder) {
        // reorder row groups the reflow may have changed the nextinflows
        OrderRowGroups(rowGroups, &thead, &tfoot);
        childX = rowGroups.IndexOf(kidFrame);
        if (childX == RowGroupArray::NoIndex) {
          // XXXbz can this happen?
          childX = rowGroups.Length();
        }
      }
      // see if the rowgroup did not fit on this page might be pushed on
      // the next page
      if (NS_FRAME_IS_COMPLETE(aStatus) && isPaginated &&
          (NS_UNCONSTRAINEDSIZE != kidReflowState.availableHeight) &&
          kidReflowState.availableHeight < desiredSize.height) {
        // if we are on top of the page place with dataloss
        if (kidReflowState.mFlags.mIsTopOfPage) {
          if (childX+1 < rowGroups.Length()) {
            nsIFrame* nextRowGroupFrame = rowGroups[childX + 1];
            if (nextRowGroupFrame) {
              PlaceChild(aReflowState, kidFrame, desiredSize, oldKidRect,
                         oldKidOverflowRect);
              aStatus = NS_FRAME_NOT_COMPLETE;
              PushChildren(rowGroups, childX + 1);
              aLastChildReflowed = kidFrame;
              break;
            }
          }
        }
        else { // we are not on top, push this rowgroup onto the next page
          if (prevKidFrame) { // we had a rowgroup before so push this
            // XXXroc shouldn't we add a repeated footer here?
            aStatus = NS_FRAME_NOT_COMPLETE;
            PushChildren(rowGroups, childX);
            aLastChildReflowed = prevKidFrame;
            break;
          }
        }
      }

      aLastChildReflowed   = kidFrame;

      pageBreak = PR_FALSE;
      // see if there is a page break after this row group or before the next one
      if (NS_FRAME_IS_COMPLETE(aStatus) && isPaginated && 
          (NS_UNCONSTRAINEDSIZE != kidReflowState.availableHeight)) {
        nsIFrame* nextKid =
          (childX + 1 < rowGroups.Length()) ? rowGroups[childX + 1] : nsnull;
        pageBreak = PageBreakAfter(*kidFrame, nextKid);
      }

      // Place the child
      PlaceChild(aReflowState, kidFrame, desiredSize, oldKidRect,
                 oldKidOverflowRect);

      // Remember where we just were in case we end up pushing children
      prevKidFrame = kidFrame;

      // Special handling for incomplete children
      if (NS_FRAME_IS_NOT_COMPLETE(aStatus)) {         
        kidNextInFlow = kidFrame->GetNextInFlow();
        if (!kidNextInFlow) {
          // The child doesn't have a next-in-flow so create a continuing
          // frame. This hooks the child into the flow
          rv = presContext->PresShell()->FrameConstructor()->
            CreateContinuingFrame(presContext, kidFrame, this, &kidNextInFlow);
          if (NS_FAILED(rv)) {
            aStatus = NS_FRAME_COMPLETE;
            break;
          }

          // Insert the continuing frame into the sibling list.
          mFrames.InsertFrame(nsnull, kidFrame, kidNextInFlow);

          // Fall through and update |rowGroups| with the new rowgroup, just as
          // it would have been if we had called OrderRowGroups again.
          // Note that rowGroups doesn't get used again after we PushChildren
          // below, anyway.
        }

        // Put the nextinflow so that it will get pushed
        rowGroups.InsertElementAt(childX + 1,
                           static_cast <nsTableRowGroupFrame*>(kidNextInFlow));

        // We've used up all of our available space so push the remaining
        // children to the next-in-flow
        nsIFrame* nextSibling = kidFrame->GetNextSibling();
        if (nsnull != nextSibling) {
          PushChildren(rowGroups, childX + 1);
        }
        if (allowRepeatedFooter) {
          kidAvailSize.height = footerHeight;
          nsHTMLReflowState footerReflowState(presContext,
                                              aReflowState.reflowState,
                                              tfoot, kidAvailSize,
                                              -1, -1, PR_FALSE);
          InitChildReflowState(footerReflowState);
          aReflowState.y += cellSpacingY;

          nsRect origTfootRect = tfoot->GetRect();
          nsRect origTfootOverflowRect = tfoot->GetOverflowRect();
          
          nsReflowStatus footerStatus;
          rv = ReflowChild(tfoot, presContext, desiredSize, footerReflowState,
                           aReflowState.x, aReflowState.y,
                           NS_FRAME_INVALIDATE_ON_MOVE, footerStatus);
          PlaceChild(aReflowState, tfoot, desiredSize, origTfootRect,
                     origTfootOverflowRect);
        }
        break;
      }
    }
    else { // it isn't being reflowed
      aReflowState.y += cellSpacingY;
      nsRect kidRect = kidFrame->GetRect();
      if (kidRect.y != aReflowState.y) {
        // invalidate the old position
        kidFrame->InvalidateOverflowRect();
        kidRect.y = aReflowState.y;
        kidFrame->SetRect(kidRect);        // move to the new position
        RePositionViews(kidFrame);
        // invalidate the new position
        kidFrame->InvalidateOverflowRect();
      }
      aReflowState.y += kidRect.height;

      // If our height is constrained then update the available height.
      if (NS_UNCONSTRAINEDSIZE != aReflowState.availSize.height) {
        aReflowState.availSize.height -= cellSpacingY + kidRect.height;
      }
    }
    ConsiderChildOverflow(aOverflowArea, kidFrame);
  }
  
  // We've now propagated the column resizes and geometry changes to all
  // the children.
  mBits.mResizedColumns = PR_FALSE;
  ClearGeometryDirty();

  return rv;
}

void
nsTableFrame::ReflowColGroups(nsIRenderingContext *aRenderingContext)
{
  if (!GetPrevInFlow() && !HaveReflowedColGroups()) {
    nsHTMLReflowMetrics kidMet;
    nsPresContext *presContext = PresContext();
    for (nsIFrame* kidFrame = mColGroups.FirstChild(); kidFrame;
         kidFrame = kidFrame->GetNextSibling()) {
      if (NS_SUBTREE_DIRTY(kidFrame)) {
        // The column groups don't care about dimensions or reflow states.
        nsHTMLReflowState kidReflowState(presContext, kidFrame,
                                       aRenderingContext, nsSize(0,0));
        nsReflowStatus cgStatus;
        ReflowChild(kidFrame, presContext, kidMet, kidReflowState, 0, 0, 0,
                    cgStatus);
        FinishReflowChild(kidFrame, presContext, nsnull, kidMet, 0, 0, 0);
      }
    }
    SetHaveReflowedColGroups(PR_TRUE);
  }
}

void 
nsTableFrame::CalcDesiredHeight(const nsHTMLReflowState& aReflowState, nsHTMLReflowMetrics& aDesiredSize) 
{
  nsTableCellMap* cellMap = GetCellMap();
  if (!cellMap) {
    NS_ASSERTION(PR_FALSE, "never ever call me until the cell map is built!");
    aDesiredSize.height = 0;
    return;
  }
  nscoord  cellSpacingY = GetCellSpacingY();
  nsMargin borderPadding = GetChildAreaOffset(&aReflowState);

  // get the natural height based on the last child's (row group) rect
  RowGroupArray rowGroups;
  OrderRowGroups(rowGroups);
  if (rowGroups.IsEmpty()) {
    // tables can be used as rectangular items without content
    nscoord tableSpecifiedHeight = CalcBorderBoxHeight(aReflowState);
    if ((NS_UNCONSTRAINEDSIZE != tableSpecifiedHeight) &&
        (tableSpecifiedHeight > 0) &&
        eCompatibility_NavQuirks != PresContext()->CompatibilityMode()) {
          // empty tables should not have a size in quirks mode
      aDesiredSize.height = tableSpecifiedHeight;
    } 
    else
      aDesiredSize.height = 0;
    return;
  }
  PRInt32 rowCount = cellMap->GetRowCount();
  PRInt32 colCount = cellMap->GetColCount();
  nscoord desiredHeight = borderPadding.top + borderPadding.bottom;
  if (rowCount > 0 && colCount > 0) {
    desiredHeight += cellSpacingY;
    for (PRUint32 rgX = 0; rgX < rowGroups.Length(); rgX++) {
      desiredHeight += rowGroups[rgX]->GetSize().height + cellSpacingY;
    }
  }

  // see if a specified table height requires dividing additional space to rows
  if (!GetPrevInFlow()) {
    nscoord tableSpecifiedHeight = CalcBorderBoxHeight(aReflowState);
    if ((tableSpecifiedHeight > 0) && 
        (tableSpecifiedHeight != NS_UNCONSTRAINEDSIZE) &&
        (tableSpecifiedHeight > desiredHeight)) {
      // proportionately distribute the excess height to unconstrained rows in each
      // unconstrained row group.
      DistributeHeightToRows(aReflowState, tableSpecifiedHeight - desiredHeight);
      // this might have changed the overflow area incorporate the childframe overflow area.
      for (nsIFrame* kidFrame = mFrames.FirstChild(); kidFrame; kidFrame = kidFrame->GetNextSibling()) {
        ConsiderChildOverflow(aDesiredSize.mOverflowArea, kidFrame);
      } 
      desiredHeight = tableSpecifiedHeight;
    }
  }
  aDesiredSize.height = desiredHeight;
}

static
void ResizeCells(nsTableFrame& aTableFrame)
{
  nsTableFrame::RowGroupArray rowGroups;
  aTableFrame.OrderRowGroups(rowGroups);
  nsHTMLReflowMetrics tableDesiredSize;
  nsRect tableRect = aTableFrame.GetRect();
  tableDesiredSize.width = tableRect.width;
  tableDesiredSize.height = tableRect.height;
  tableDesiredSize.mOverflowArea = nsRect(0, 0, tableRect.width,
                                          tableRect.height);

  for (PRUint32 rgX = 0; rgX < rowGroups.Length(); rgX++) {
    nsTableRowGroupFrame* rgFrame = rowGroups[rgX];
   
    nsRect rowGroupRect = rgFrame->GetRect();
    nsHTMLReflowMetrics groupDesiredSize;
    groupDesiredSize.width = rowGroupRect.width;
    groupDesiredSize.height = rowGroupRect.height;
    groupDesiredSize.mOverflowArea = nsRect(0, 0, groupDesiredSize.width,
                                      groupDesiredSize.height);
    nsTableRowFrame* rowFrame = rgFrame->GetFirstRow();
    while (rowFrame) {
      rowFrame->DidResize();
      rgFrame->ConsiderChildOverflow(groupDesiredSize.mOverflowArea, rowFrame);
      rowFrame = rowFrame->GetNextRow();
    }
    rgFrame->FinishAndStoreOverflow(&groupDesiredSize.mOverflowArea,
                                    nsSize(groupDesiredSize.width, groupDesiredSize.height));
    // make the coordinates of |desiredSize.mOverflowArea| incorrect
    // since it's about to go away:
    groupDesiredSize.mOverflowArea.MoveBy(rgFrame->GetPosition());
    tableDesiredSize.mOverflowArea.UnionRect(tableDesiredSize.mOverflowArea, groupDesiredSize.mOverflowArea);
  }
  aTableFrame.FinishAndStoreOverflow(&tableDesiredSize.mOverflowArea,
                                     nsSize(tableDesiredSize.width, tableDesiredSize.height));
}

void
nsTableFrame::DistributeHeightToRows(const nsHTMLReflowState& aReflowState,
                                     nscoord                  aAmount)
{
  nscoord cellSpacingY = GetCellSpacingY();

  nsMargin borderPadding = GetChildAreaOffset(&aReflowState);
  
  RowGroupArray rowGroups;
  OrderRowGroups(rowGroups);

  nscoord amountUsed = 0;
  // distribute space to each pct height row whose row group doesn't have a computed 
  // height, and base the pct on the table height. If the row group had a computed 
  // height, then this was already done in nsTableRowGroupFrame::CalculateRowHeights
  nscoord pctBasis = aReflowState.ComputedHeight() - (GetCellSpacingY() * (GetRowCount() + 1));
  nscoord yOriginRG = borderPadding.top + GetCellSpacingY();
  nscoord yEndRG = yOriginRG;
  PRUint32 rgX;
  for (rgX = 0; rgX < rowGroups.Length(); rgX++) {
    nsTableRowGroupFrame* rgFrame = rowGroups[rgX];
    nscoord amountUsedByRG = 0;
    nscoord yOriginRow = 0;
    nsRect rgRect = rgFrame->GetRect();
    if (!rgFrame->HasStyleHeight()) {
      nsTableRowFrame* rowFrame = rgFrame->GetFirstRow();
      while (rowFrame) {
        nsRect rowRect = rowFrame->GetRect();
        if ((amountUsed < aAmount) && rowFrame->HasPctHeight()) {
          nscoord pctHeight = rowFrame->GetHeight(pctBasis);
          nscoord amountForRow = NS_MIN(aAmount - amountUsed, pctHeight - rowRect.height);
          if (amountForRow > 0) {
            nsRect oldRowRect = rowRect;
            rowRect.height += amountForRow;
            // XXXbz we don't need to change rowRect.y to be yOriginRow?
            rowFrame->SetRect(rowRect);
            yOriginRow += rowRect.height + cellSpacingY;
            yEndRG += rowRect.height + cellSpacingY;
            amountUsed += amountForRow;
            amountUsedByRG += amountForRow;
            //rowFrame->DidResize();        
            nsTableFrame::RePositionViews(rowFrame);

            rgFrame->InvalidateRectDifference(oldRowRect, rowRect);
          }
        }
        else {
          if (amountUsed > 0 && yOriginRow != rowRect.y &&
              !(GetStateBits() & NS_FRAME_FIRST_REFLOW)) {
            rowFrame->InvalidateOverflowRect();
            rowFrame->SetPosition(nsPoint(rowRect.x, yOriginRow));
            nsTableFrame::RePositionViews(rowFrame);
            rowFrame->InvalidateOverflowRect();
          }
          yOriginRow += rowRect.height + cellSpacingY;
          yEndRG += rowRect.height + cellSpacingY;
        }
        rowFrame = rowFrame->GetNextRow();
      }
      if (amountUsed > 0) {
        if (rgRect.y != yOriginRG) {
          rgFrame->InvalidateOverflowRect();
        }

        nsRect origRgRect = rgRect;
        nsRect origRgOverflowRect = rgFrame->GetOverflowRect();
        
        rgRect.y = yOriginRG;
        rgRect.height += amountUsedByRG;
        
        rgFrame->SetRect(rgRect);

        nsTableFrame::InvalidateFrame(rgFrame, origRgRect, origRgOverflowRect,
                                      PR_FALSE);
      }
    }
    else if (amountUsed > 0 && yOriginRG != rgRect.y) {
      rgFrame->InvalidateOverflowRect();
      rgFrame->SetPosition(nsPoint(rgRect.x, yOriginRG));
      // Make sure child views are properly positioned
      nsTableFrame::RePositionViews(rgFrame);
      rgFrame->InvalidateOverflowRect();
    }
    yOriginRG = yEndRG;
  }

  if (amountUsed >= aAmount) {
    ResizeCells(*this);
    return;
  }

  // get the first row without a style height where its row group has an
  // unconstrained height
  nsTableRowGroupFrame* firstUnStyledRG  = nsnull;
  nsTableRowFrame*      firstUnStyledRow = nsnull;
  for (rgX = 0; rgX < rowGroups.Length() && !firstUnStyledRG; rgX++) {
    nsTableRowGroupFrame* rgFrame = rowGroups[rgX];
    if (!rgFrame->HasStyleHeight()) {
      nsTableRowFrame* rowFrame = rgFrame->GetFirstRow();
      while (rowFrame) {
        if (!rowFrame->HasStyleHeight()) {
          firstUnStyledRG = rgFrame;
          firstUnStyledRow = rowFrame;
          break;
        }
        rowFrame = rowFrame->GetNextRow();
      }
    }
  }

  nsTableRowFrame* lastEligibleRow = nsnull;
  // Accumulate the correct divisor. This will be the total total height of all
  // unstyled rows inside unstyled row groups, unless there are none, in which
  // case, it will be number of all rows. If the unstyled rows don't have a
  // height, divide the space equally among them.
  nscoord divisor = 0;
  PRInt32 eligibleRows = 0;
  PRBool expandEmptyRows = PR_FALSE;

  if (!firstUnStyledRow) {
    // there is no unstyled row
    divisor = GetRowCount();
  }
  else {
    for (rgX = 0; rgX < rowGroups.Length(); rgX++) {
      nsTableRowGroupFrame* rgFrame = rowGroups[rgX];
      if (!firstUnStyledRG || !rgFrame->HasStyleHeight()) {
        nsTableRowFrame* rowFrame = rgFrame->GetFirstRow();
        while (rowFrame) {
          if (!firstUnStyledRG || !rowFrame->HasStyleHeight()) {
            NS_ASSERTION(rowFrame->GetSize().height >= 0,
                         "negative row frame height");
            divisor += rowFrame->GetSize().height;
            eligibleRows++;
            lastEligibleRow = rowFrame;
          }
          rowFrame = rowFrame->GetNextRow();
        }
      }
    }
    if (divisor <= 0) {
      if (eligibleRows > 0) {
        expandEmptyRows = PR_TRUE;
      }
      else {
        NS_ERROR("invalid divisor");
        return;
      }
    }
  }
  // allocate the extra height to the unstyled row groups and rows
  nscoord heightToDistribute = aAmount - amountUsed;
  yOriginRG = borderPadding.top + cellSpacingY;
  yEndRG = yOriginRG;
  for (rgX = 0; rgX < rowGroups.Length(); rgX++) {
    nsTableRowGroupFrame* rgFrame = rowGroups[rgX];
    nscoord amountUsedByRG = 0;
    nscoord yOriginRow = 0;
    nsRect rgRect = rgFrame->GetRect();
    nsRect rgOverflowRect = rgFrame->GetOverflowRect();
    // see if there is an eligible row group or we distribute to all rows
    if (!firstUnStyledRG || !rgFrame->HasStyleHeight() || !eligibleRows) {
      nsTableRowFrame* rowFrame = rgFrame->GetFirstRow();
      while (rowFrame) {
        nsRect rowRect = rowFrame->GetRect();
        nsRect rowOverflowRect = rowFrame->GetOverflowRect();
        // see if there is an eligible row or we distribute to all rows
        if (!firstUnStyledRow || !rowFrame->HasStyleHeight() || !eligibleRows) {          
          float ratio;
          if (eligibleRows) {
            if (!expandEmptyRows) {
              // The amount of additional space each row gets is proportional to
              // its height
              ratio = float(rowRect.height) / float(divisor);
            } else {
              // empty rows get all the same additional space
              ratio = 1.0f / float(eligibleRows);
            }
          }
          else {
            // all rows get the same additional space
            ratio = 1.0f / float(divisor);
          }
          // give rows their additional space, except for the last row which
          // gets the remainder
          nscoord amountForRow = (rowFrame == lastEligibleRow) 
                                 ? aAmount - amountUsed : NSToCoordRound(((float)(heightToDistribute)) * ratio);
          amountForRow = NS_MIN(amountForRow, aAmount - amountUsed);

          if (yOriginRow != rowRect.y) {
            rowFrame->InvalidateOverflowRect();
          }
          
          // update the row height
          nsRect newRowRect(rowRect.x, yOriginRow, rowRect.width,
                            rowRect.height + amountForRow);
          rowFrame->SetRect(newRowRect);

          yOriginRow += newRowRect.height + cellSpacingY;
          yEndRG += newRowRect.height + cellSpacingY;

          amountUsed += amountForRow;
          amountUsedByRG += amountForRow;
          NS_ASSERTION((amountUsed <= aAmount), "invalid row allocation");
          //rowFrame->DidResize();        
          nsTableFrame::RePositionViews(rowFrame);

          nsTableFrame::InvalidateFrame(rowFrame, rowRect, rowOverflowRect,
                                        PR_FALSE);
        }
        else {
          if (amountUsed > 0 && yOriginRow != rowRect.y) {
            rowFrame->InvalidateOverflowRect();
            rowFrame->SetPosition(nsPoint(rowRect.x, yOriginRow));
            nsTableFrame::RePositionViews(rowFrame);
            rowFrame->InvalidateOverflowRect();
          }
          yOriginRow += rowRect.height + cellSpacingY;
          yEndRG += rowRect.height + cellSpacingY;
        }
        rowFrame = rowFrame->GetNextRow();
      }
      if (amountUsed > 0) {
        if (rgRect.y != yOriginRG) {
          rgFrame->InvalidateOverflowRect();
        }
        
        rgFrame->SetRect(nsRect(rgRect.x, yOriginRG, rgRect.width,
                                rgRect.height + amountUsedByRG));

        nsTableFrame::InvalidateFrame(rgFrame, rgRect, rgOverflowRect,
                                      PR_FALSE);
      }
      // Make sure child views are properly positioned
    }
    else if (amountUsed > 0 && yOriginRG != rgRect.y) {
      rgFrame->InvalidateOverflowRect();
      rgFrame->SetPosition(nsPoint(rgRect.x, yOriginRG));
      // Make sure child views are properly positioned
      nsTableFrame::RePositionViews(rgFrame);
      rgFrame->InvalidateOverflowRect();
    }
    yOriginRG = yEndRG;
  }

  ResizeCells(*this);
}

PRBool 
nsTableFrame::IsPctHeight(nsStyleContext* aStyleContext) 
{
  PRBool result = PR_FALSE;
  if (aStyleContext) {
    result = (eStyleUnit_Percent ==
              aStyleContext->GetStylePosition()->mHeight.GetUnit());
  }
  return result;
}

PRInt32 nsTableFrame::GetColumnWidth(PRInt32 aColIndex)
{
  nsTableFrame * firstInFlow = (nsTableFrame *)GetFirstInFlow();
  NS_ASSERTION(firstInFlow, "illegal state -- no first in flow");
  PRInt32 result = 0;
  if (this == firstInFlow) {
    nsTableColFrame* colFrame = GetColFrame(aColIndex);
    if (colFrame) {
      result = colFrame->GetFinalWidth();
    }
  }
  else {
    result = firstInFlow->GetColumnWidth(aColIndex);
  }

  return result;
}

void nsTableFrame::SetColumnWidth(PRInt32 aColIndex, nscoord aWidth)
{
  nsTableFrame* firstInFlow = (nsTableFrame *)GetFirstInFlow();
  NS_ASSERTION(firstInFlow, "illegal state -- no first in flow");

  if (this == firstInFlow) {
    nsTableColFrame* colFrame = GetColFrame(aColIndex);
    if (colFrame) {
      colFrame->SetFinalWidth(aWidth);
    }
    else {
      NS_ASSERTION(PR_FALSE, "null col frame");
    }
  }
  else {
    firstInFlow->SetColumnWidth(aColIndex, aWidth);
  }
}

// XXX: could cache this.  But be sure to check style changes if you do!
nscoord nsTableFrame::GetCellSpacingX()
{
  if (IsBorderCollapse())
    return 0;

  return GetStyleTableBorder()->mBorderSpacingX;
}

// XXX: could cache this. But be sure to check style changes if you do!
nscoord nsTableFrame::GetCellSpacingY()
{
  if (IsBorderCollapse())
    return 0;

  return GetStyleTableBorder()->mBorderSpacingY;
}


/* virtual */ nscoord
nsTableFrame::GetBaseline() const
{
  nscoord ascent = 0;
  RowGroupArray orderedRowGroups;
  OrderRowGroups(orderedRowGroups);
  nsTableRowFrame* firstRow = nsnull;
  for (PRUint32 rgIndex = 0; rgIndex < orderedRowGroups.Length(); rgIndex++) {
    nsTableRowGroupFrame* rgFrame = orderedRowGroups[rgIndex];
    if (rgFrame->GetRowCount()) {
      firstRow = rgFrame->GetFirstRow(); 
      ascent = rgFrame->GetRect().y + firstRow->GetRect().y + firstRow->GetRowBaseline();
      break;
    }
  }
  if (!firstRow)
    ascent = GetRect().height;
  return ascent;
}
/* ----- global methods ----- */

nsIFrame*
NS_NewTableFrame(nsIPresShell* aPresShell, nsStyleContext* aContext)
{
  return new (aPresShell) nsTableFrame(aContext);
}

NS_IMPL_FRAMEARENA_HELPERS(nsTableFrame)

nsTableFrame*
nsTableFrame::GetTableFrame(nsIFrame* aSourceFrame)
{
  if (aSourceFrame) {
    // "result" is the result of intermediate calls, not the result we return from this method
    for (nsIFrame* parentFrame = aSourceFrame->GetParent(); parentFrame;
         parentFrame = parentFrame->GetParent()) {
      if (nsGkAtoms::tableFrame == parentFrame->GetType()) {
        return (nsTableFrame*)parentFrame;
      }
    }
  }
  NS_NOTREACHED("unable to find table parent");
  return nsnull;
}

PRBool 
nsTableFrame::IsAutoWidth(PRBool* aIsPctWidth)
{
  const nsStyleCoord& width = GetStylePosition()->mWidth;

  if (aIsPctWidth) {
    // XXX The old code also made the return value true for 0%, but that
    // seems silly.
    *aIsPctWidth = width.GetUnit() == eStyleUnit_Percent &&
                   width.GetPercentValue() > 0.0f;
    // Should this handle -moz-available and -moz-fit-content?
  }
  return width.GetUnit() == eStyleUnit_Auto;
}

PRBool 
nsTableFrame::IsAutoHeight()
{
  PRBool isAuto = PR_TRUE;  // the default

  const nsStylePosition* position = GetStylePosition();

  switch (position->mHeight.GetUnit()) {
    case eStyleUnit_Auto:         // specified auto width
      break;
    case eStyleUnit_Coord:
      isAuto = PR_FALSE;
      break;
    case eStyleUnit_Percent:
      if (position->mHeight.GetPercentValue() > 0.0f) {
        isAuto = PR_FALSE;
      }
      break;
    default:
      break;
  }

  return isAuto; 
}

nscoord 
nsTableFrame::CalcBorderBoxHeight(const nsHTMLReflowState& aState)
{
  nscoord height = aState.ComputedHeight();
  if (NS_AUTOHEIGHT != height) {
    nsMargin borderPadding = GetChildAreaOffset(&aState);
    height += borderPadding.top + borderPadding.bottom;
  }
  height = NS_MAX(0, height);

  return height;
}

PRBool 
nsTableFrame::IsAutoLayout()
{
  if (GetStyleTable()->mLayoutStrategy == NS_STYLE_TABLE_LAYOUT_AUTO)
    return PR_TRUE;
  // a fixed-layout inline-table must have a width
  // and tables with 'width: -moz-max-content' must be auto-layout
  // (at least as long as FixedTableLayoutStrategy::GetPrefWidth returns
  // nscoord_MAX)
  const nsStyleCoord &width = GetStylePosition()->mWidth;
  return (width.GetUnit() == eStyleUnit_Auto) ||
         (width.GetUnit() == eStyleUnit_Enumerated &&
          width.GetIntValue() == NS_STYLE_WIDTH_MAX_CONTENT);
}

#ifdef DEBUG
NS_IMETHODIMP
nsTableFrame::GetFrameName(nsAString& aResult) const
{
  return MakeFrameName(NS_LITERAL_STRING("Table"), aResult);
}
#endif

// Find the closet sibling before aPriorChildFrame (including aPriorChildFrame) that
// is of type aChildType
nsIFrame* 
nsTableFrame::GetFrameAtOrBefore(nsIFrame*       aParentFrame,
                                 nsIFrame*       aPriorChildFrame,
                                 nsIAtom*        aChildType)
{
  nsIFrame* result = nsnull;
  if (!aPriorChildFrame) {
    return result;
  }
  if (aChildType == aPriorChildFrame->GetType()) {
    return aPriorChildFrame;
  }

  // aPriorChildFrame is not of type aChildType, so we need start from 
  // the beginnng and find the closest one 
  nsIFrame* lastMatchingFrame = nsnull;
  nsIFrame* childFrame = aParentFrame->GetFirstChild(nsnull);
  while (childFrame && (childFrame != aPriorChildFrame)) {
    if (aChildType == childFrame->GetType()) {
      lastMatchingFrame = childFrame;
    }
    childFrame = childFrame->GetNextSibling();
  }
  return lastMatchingFrame;
}

#ifdef DEBUG
void 
nsTableFrame::DumpRowGroup(nsIFrame* aKidFrame)
{
  if (!aKidFrame)
    return;

  nsIFrame* cFrame = aKidFrame->GetFirstChild(nsnull);
  while (cFrame) {
    nsTableRowFrame *rowFrame = do_QueryFrame(cFrame);
    if (rowFrame) {
      printf("row(%d)=%p ", rowFrame->GetRowIndex(),
             static_cast<void*>(rowFrame));
      nsIFrame* childFrame = cFrame->GetFirstChild(nsnull);
      while (childFrame) {
        nsTableCellFrame *cellFrame = do_QueryFrame(childFrame);
        if (cellFrame) {
          PRInt32 colIndex;
          cellFrame->GetColIndex(colIndex);
          printf("cell(%d)=%p ", colIndex, static_cast<void*>(childFrame));
        }
        childFrame = childFrame->GetNextSibling();
      }
      printf("\n");
    }
    else {
      DumpRowGroup(rowFrame);
    }
    cFrame = cFrame->GetNextSibling();
  }
}

void 
nsTableFrame::Dump(PRBool          aDumpRows,
                   PRBool          aDumpCols, 
                   PRBool          aDumpCellMap)
{
  printf("***START TABLE DUMP*** \n");
  // dump the columns widths array
  printf("mColWidths=");
  PRInt32 numCols = GetColCount();
  PRInt32 colX;
  for (colX = 0; colX < numCols; colX++) {
    printf("%d ", GetColumnWidth(colX));
  }
  printf("\n");

  if (aDumpRows) {
    nsIFrame* kidFrame = mFrames.FirstChild();
    while (kidFrame) {
      DumpRowGroup(kidFrame);
      kidFrame = kidFrame->GetNextSibling();
    }
  }

  if (aDumpCols) {
	  // output col frame cache
    printf("\n col frame cache ->");
	   for (colX = 0; colX < numCols; colX++) {
      nsTableColFrame* colFrame = mColFrames.ElementAt(colX);
      if (0 == (colX % 8)) {
        printf("\n");
      }
      printf ("%d=%p ", colX, static_cast<void*>(colFrame));
      nsTableColType colType = colFrame->GetColType();
      switch (colType) {
      case eColContent:
        printf(" content ");
        break;
      case eColAnonymousCol: 
        printf(" anonymous-column ");
        break;
      case eColAnonymousColGroup:
        printf(" anonymous-colgroup ");
        break;
      case eColAnonymousCell: 
        printf(" anonymous-cell ");
        break;
      }
    }
    printf("\n colgroups->");
    for (nsIFrame* childFrame = mColGroups.FirstChild(); childFrame;
         childFrame = childFrame->GetNextSibling()) {
      if (nsGkAtoms::tableColGroupFrame == childFrame->GetType()) {
        nsTableColGroupFrame* colGroupFrame = (nsTableColGroupFrame *)childFrame;
        colGroupFrame->Dump(1);
      }
    }
    for (colX = 0; colX < numCols; colX++) {
      printf("\n");
      nsTableColFrame* colFrame = GetColFrame(colX);
      colFrame->Dump(1);
    }
  }
  if (aDumpCellMap) {
    nsTableCellMap* cellMap = GetCellMap();
    cellMap->Dump();
  }
  printf(" ***END TABLE DUMP*** \n");
}
#endif

// nsTableIterator
nsTableIterator::nsTableIterator(nsIFrame& aSource)
{
  nsIFrame* firstChild = aSource.GetFirstChild(nsnull);
  Init(firstChild);
}

nsTableIterator::nsTableIterator(nsFrameList& aSource)
{
  nsIFrame* firstChild = aSource.FirstChild();
  Init(firstChild);
}

void nsTableIterator::Init(nsIFrame* aFirstChild)
{
  mFirstListChild = aFirstChild;
  mFirstChild     = aFirstChild;
  mCurrentChild   = nsnull;
  mLeftToRight    = PR_TRUE;
  mCount          = -1;

  if (!mFirstChild) {
    return;
  }

  nsTableFrame* table = nsTableFrame::GetTableFrame(mFirstChild);
  if (table) {
    mLeftToRight = (NS_STYLE_DIRECTION_LTR ==
                    table->GetStyleVisibility()->mDirection);
  }
  else {
    NS_NOTREACHED("source of table iterator is not part of a table");
    return;
  }

  if (!mLeftToRight) {
    mCount = 0;
    nsIFrame* nextChild = mFirstChild->GetNextSibling();
    while (nsnull != nextChild) {
      mCount++;
      mFirstChild = nextChild;
      nextChild = nextChild->GetNextSibling();
    }
  } 
}

nsIFrame* nsTableIterator::First()
{
  mCurrentChild = mFirstChild;
  return mCurrentChild;
}
      
nsIFrame* nsTableIterator::Next()
{
  if (!mCurrentChild) {
    return nsnull;
  }

  if (mLeftToRight) {
    mCurrentChild = mCurrentChild->GetNextSibling();
    return mCurrentChild;
  }
  else {
    nsIFrame* targetChild = mCurrentChild;
    mCurrentChild = nsnull;
    nsIFrame* child = mFirstListChild;
    while (child && (child != targetChild)) {
      mCurrentChild = child;
      child = child->GetNextSibling();
    }
    return mCurrentChild;
  }
}

PRBool nsTableIterator::IsLeftToRight()
{
  return mLeftToRight;
}

PRInt32 nsTableIterator::Count()
{
  if (-1 == mCount) {
    mCount = 0;
    nsIFrame* child = mFirstListChild;
    while (nsnull != child) {
      mCount++;
      child = child->GetNextSibling();
    }
  }
  return mCount;
}

/*------------------ nsITableLayout methods ------------------------------*/
NS_IMETHODIMP 
nsTableFrame::GetCellDataAt(PRInt32        aRowIndex, 
                            PRInt32        aColIndex,
                            nsIDOMElement* &aCell,   //out params
                            PRInt32&       aStartRowIndex, 
                            PRInt32&       aStartColIndex, 
                            PRInt32&       aRowSpan, 
                            PRInt32&       aColSpan,
                            PRInt32&       aActualRowSpan, 
                            PRInt32&       aActualColSpan,
                            PRBool&        aIsSelected)
{
  // Initialize out params
  aCell = nsnull;
  aStartRowIndex = 0;
  aStartColIndex = 0;
  aRowSpan = 0;
  aColSpan = 0;
  aIsSelected = PR_FALSE;

  nsTableCellMap* cellMap = GetCellMap();
  if (!cellMap) { return NS_ERROR_NOT_INITIALIZED;}

  PRBool originates;
  PRInt32 colSpan; // Is this the "effective" or "html" value?

  nsTableCellFrame *cellFrame = cellMap->GetCellInfoAt(aRowIndex, aColIndex, &originates, &colSpan);
  if (!cellFrame) return NS_TABLELAYOUT_CELL_NOT_FOUND;

  nsresult result= cellFrame->GetRowIndex(aStartRowIndex);
  if (NS_FAILED(result)) return result;
  result = cellFrame->GetColIndex(aStartColIndex);
  if (NS_FAILED(result)) return result;
  //This returns HTML value, which may be 0
  aRowSpan = cellFrame->GetRowSpan();
  aColSpan = cellFrame->GetColSpan();
  aActualRowSpan = GetEffectiveRowSpan(*cellFrame);
  aActualColSpan = GetEffectiveColSpan(*cellFrame);

  // If these aren't at least 1, we have a cellmap error
  if (aActualRowSpan == 0 || aActualColSpan == 0)
    return NS_ERROR_FAILURE;

  result = cellFrame->GetSelected(&aIsSelected);
  if (NS_FAILED(result)) return result;

  // do this last, because it addrefs, 
  // and we don't want the caller leaking it on error
  nsIContent* content = cellFrame->GetContent();
  if (!content) return NS_ERROR_FAILURE;   
  
  return CallQueryInterface(content, &aCell);                                      
}

NS_IMETHODIMP nsTableFrame::GetTableSize(PRInt32& aRowCount, PRInt32& aColCount)
{
  nsTableCellMap* cellMap = GetCellMap();
  // Initialize out params
  aRowCount = 0;
  aColCount = 0;
  if (!cellMap) { return NS_ERROR_NOT_INITIALIZED;}

  aRowCount = cellMap->GetRowCount();
  aColCount = cellMap->GetColCount();
  return NS_OK;
}

NS_IMETHODIMP
nsTableFrame::GetIndexByRowAndColumn(PRInt32 aRow, PRInt32 aColumn,
                                     PRInt32 *aIndex)
{
  NS_ENSURE_ARG_POINTER(aIndex);
  *aIndex = -1;

  nsTableCellMap* cellMap = GetCellMap();
  if (!cellMap)
    return NS_ERROR_NOT_INITIALIZED;

  *aIndex = cellMap->GetIndexByRowAndColumn(aRow, aColumn);
  return (*aIndex == -1) ? NS_TABLELAYOUT_CELL_NOT_FOUND : NS_OK;
}

NS_IMETHODIMP
nsTableFrame::GetRowAndColumnByIndex(PRInt32 aIndex,
                                    PRInt32 *aRow, PRInt32 *aColumn)
{
  NS_ENSURE_ARG_POINTER(aRow);
  *aRow = -1;

  NS_ENSURE_ARG_POINTER(aColumn);
  *aColumn = -1;

  nsTableCellMap* cellMap = GetCellMap();
  if (!cellMap)
    return NS_ERROR_NOT_INITIALIZED;

  cellMap->GetRowAndColumnByIndex(aIndex, aRow, aColumn);
  return NS_OK;
}

/*---------------- end of nsITableLayout implementation ------------------*/

PRBool
nsTableFrame::ColumnHasCellSpacingBefore(PRInt32 aColIndex) const
{
  // Since fixed-layout tables should not have their column sizes change
  // as they load, we assume that all columns are significant.
  if (LayoutStrategy()->GetType() == nsITableLayoutStrategy::Fixed)
    return PR_TRUE;
  // the first column is always significant
  if (aColIndex == 0)
    return PR_TRUE;
  nsTableCellMap* cellMap = GetCellMap();
  if (!cellMap) 
    return PR_FALSE;
  return cellMap->GetNumCellsOriginatingInCol(aColIndex) > 0;
}

static void
CheckFixDamageArea(PRInt32 aNumRows,
                   PRInt32 aNumCols,
                   nsRect& aDamageArea)
{
  if (((aDamageArea.XMost() > aNumCols) && (aDamageArea.width  != 1) && (aNumCols != 0)) || 
      ((aDamageArea.YMost() > aNumRows) && (aDamageArea.height != 1) && (aNumRows != 0))) {
    // the damage area was set incorrectly, just be safe and make it the entire table
    NS_ASSERTION(PR_FALSE, "invalid BC damage area");
    aDamageArea.x      = 0;
    aDamageArea.y      = 0;
    aDamageArea.width  = aNumCols;
    aDamageArea.height = aNumRows;
  }
}

/********************************************************************************
 * Collapsing Borders
 *
 *  The CSS spec says to resolve border conflicts in this order:
 *  1) any border with the style HIDDEN wins
 *  2) the widest border with a style that is not NONE wins
 *  3) the border styles are ranked in this order, highest to lowest precedence: 
 *     double, solid, dashed, dotted, ridge, outset, groove, inset
 *  4) borders that are of equal width and style (differ only in color) have this precedence:
 *     cell, row, rowgroup, col, colgroup, table
 *  5) if all border styles are NONE, then that's the computed border style.
 *******************************************************************************/

void 
nsTableFrame::SetBCDamageArea(const nsRect& aValue)
{
  nsRect newRect(aValue);
  newRect.width  = NS_MAX(1, newRect.width);
  newRect.height = NS_MAX(1, newRect.height);

  if (!IsBorderCollapse()) {
    NS_ASSERTION(PR_FALSE, "invalid call - not border collapse model");
    return;
  }
  SetNeedToCalcBCBorders(PR_TRUE);
  // Get the property 
  BCPropertyData* value = (BCPropertyData*)nsTableFrame::GetProperty(this, nsGkAtoms::tableBCProperty, PR_TRUE);
  if (value) {
    // for now just construct a union of the new and old damage areas
    value->mDamageArea.UnionRect(value->mDamageArea, newRect);
    CheckFixDamageArea(GetRowCount(), GetColCount(), value->mDamageArea);
  }
}

/* BCCellBorder represents a border segment which can be either a horizontal
 * or a vertical segment. For each segment we need to know the color, width,
 * style, who owns it and how long it is in cellmap coordinates.
 * Ownership of these segments is important to calculate which corners should
 * be bevelled. This structure has dual use, its used first to compute the
 * dominant border for horizontal and vertical segments and to store the
 * preliminary computed border results in the BCCellBorders structure.
 * This temporary storage is not symmetric with respect to horizontal and
 * vertical border segments, its always column oriented. For each column in
 * the cellmap there is a temporary stored vertical and horizontal segment.
 * XXX_Bernd this asymmetry is the root of those rowspan bc border errors
 */
struct BCCellBorder
{
  BCCellBorder() { Reset(0, 1); }
  void Reset(PRUint32 aRowIndex, PRUint32 aRowSpan);
  nscolor       color;    // border segment color
  BCPixelSize   width;    // border segment width in pixel coordinates !!
  PRUint8       style;    // border segment style, possible values are defined
                          // in nsStyleConsts.h as NS_STYLE_BORDER_STYLE_*
  BCBorderOwner owner;    // border segment owner, possible values are defined
                          // in celldata.h. In the cellmap for each border
                          // segment we store the owner and later when
                          // painting we know the owner and can retrieve the
                          // style info from the corresponding frame
  PRInt32       rowIndex; // rowIndex of temporary stored horizontal border
                          // segments relative to the table
  PRInt32       rowSpan;  // row span of temporary stored horizontal border
                          // segments
};

void
BCCellBorder::Reset(PRUint32 aRowIndex,
                    PRUint32 aRowSpan)
{
  style = NS_STYLE_BORDER_STYLE_NONE;
  color = 0;
  width = 0;
  owner = eTableOwner;
  rowIndex = aRowIndex;
  rowSpan  = aRowSpan;
}

class BCMapCellIterator;

/*****************************************************************
 *  BCMapCellInfo
 * This structure stores information about the cellmap and all involved
 * table related frames that are used during the computation of winning borders
 * in CalcBCBorders so that they do need to be looked up again and again when
 * iterating over the cells.
 ****************************************************************/
struct BCMapCellInfo 
{
  BCMapCellInfo(nsTableFrame* aTableFrame);
  void ResetCellInfo();
  void SetInfo(nsTableRowFrame*   aNewRow,
               PRInt32            aColIndex,
               BCCellData*        aCellData,
               BCMapCellIterator* aIter,
               nsCellMap*         aCellMap = nsnull);
  // The BCMapCellInfo has functions to set the continous
  // border widths (see nsTablePainter.cpp for a description of the continous
  // borders concept). The widths are computed inside these functions based on
  // the current position inside the table and the cached frames that correspond
  // to this position. The widths are stored in member variables of the internal
  // table frames.
  void SetTableTopLeftContBCBorder();
  void SetRowGroupLeftContBCBorder();
  void SetRowGroupRightContBCBorder();
  void SetRowGroupBottomContBCBorder();
  void SetRowLeftContBCBorder();
  void SetRowRightContBCBorder();
  void SetColumnTopRightContBCBorder();
  void SetColumnBottomContBCBorder();
  void SetColGroupBottomContBCBorder();
  void SetInnerRowGroupBottomContBCBorder(const nsIFrame* aNextRowGroup,
                                          nsTableRowFrame* aNextRow);

  // functions to set the border widths on the table related frames, where the
  // knowledge about the current position in the table is used.
  void SetTableTopBorderWidth(BCPixelSize aWidth);
  void SetTableLeftBorderWidth(PRInt32 aRowY, BCPixelSize aWidth);
  void SetTableRightBorderWidth(PRInt32 aRowY, BCPixelSize aWidth);
  void SetTableBottomBorderWidth(BCPixelSize aWidth);
  void SetLeftBorderWidths(BCPixelSize aWidth);
  void SetRightBorderWidths(BCPixelSize aWidth);
  void SetTopBorderWidths(BCPixelSize aWidth);
  void SetBottomBorderWidths(BCPixelSize aWidth);

  // functions to compute the borders; they depend on the
  // knowledge about the current position in the table. The edge functions
  // should be called if a table edge is involved, otherwise the internal
  // functions should be called.
  BCCellBorder GetTopEdgeBorder();
  BCCellBorder GetBottomEdgeBorder();
  BCCellBorder GetLeftEdgeBorder();
  BCCellBorder GetRightEdgeBorder();
  BCCellBorder GetRightInternalBorder();
  BCCellBorder GetLeftInternalBorder();
  BCCellBorder GetTopInternalBorder();
  BCCellBorder GetBottomInternalBorder();

  // functions to set the interal position information
  void SetColumn(PRInt32 aColX);
  // Increment the row as we loop over the rows of a rowspan
  void IncrementRow(PRBool aResetToTopRowOfCell = PR_FALSE);
  
  // Helper functions to get extent of the cell
  PRInt32 GetCellEndRowIndex() const;
  PRInt32 GetCellEndColIndex() const;

  // storage of table information
  nsTableFrame*         mTableFrame;
  PRInt32               mNumTableRows;
  PRInt32               mNumTableCols;
  BCPropertyData*       mTableBCData;

  // storage of table ltr information, the border collapse code swaps the sides
  // to account for rtl tables, this is done through mStartSide and mEndSide
  PRPackedBool          mTableIsLTR;
  PRUint8               mStartSide;
  PRUint8               mEndSide;
  
  // a cell can only belong to one rowgroup
  nsTableRowGroupFrame* mRowGroup;

  // a cell with a rowspan has a top and a bottom row, and rows in between
  nsTableRowFrame*      mTopRow;
  nsTableRowFrame*      mBottomRow;
  nsTableRowFrame*      mCurrentRowFrame;

  // a cell with a colspan has a left and right column and columns in between
  // they can belong to different colgroups
  nsTableColGroupFrame* mColGroup;
  nsTableColGroupFrame* mCurrentColGroupFrame;
 
  nsTableColFrame*      mLeftCol;
  nsTableColFrame*      mRightCol;
  nsTableColFrame*      mCurrentColFrame;
  
  // cell information
  BCCellData*           mCellData;
  nsBCTableCellFrame*   mCell;

  PRInt32               mRowIndex;
  PRInt32               mRowSpan;
  PRInt32               mColIndex;
  PRInt32               mColSpan;

  // flags to describe the position of the cell with respect to the row- and
  // colgroups, for instance mRgAtTop documents that the top cell border hits
  // a rowgroup border
  PRPackedBool          mRgAtTop;
  PRPackedBool          mRgAtBottom;
  PRPackedBool          mCgAtLeft;
  PRPackedBool          mCgAtRight;
 
};


BCMapCellInfo::BCMapCellInfo(nsTableFrame* aTableFrame)
{
  mTableFrame = aTableFrame;
  mTableIsLTR =
    aTableFrame->GetStyleVisibility()->mDirection == NS_STYLE_DIRECTION_LTR;
  if (mTableIsLTR) {
    mStartSide = NS_SIDE_LEFT;
    mEndSide = NS_SIDE_RIGHT;
  }
  else {
    mStartSide = NS_SIDE_RIGHT;
    mEndSide = NS_SIDE_LEFT;
  }
  mNumTableRows = mTableFrame->GetRowCount();
  mNumTableCols = mTableFrame->GetColCount();
  mTableBCData =
    static_cast <BCPropertyData*>(nsTableFrame::GetProperty(mTableFrame,
                 nsGkAtoms::tableBCProperty, PR_FALSE));
                 
  ResetCellInfo();
}

void BCMapCellInfo::ResetCellInfo()
{
  mCellData  = nsnull;
  mRowGroup  = nsnull;
  mTopRow    = nsnull;
  mBottomRow = nsnull;
  mColGroup  = nsnull;
  mLeftCol   = nsnull;
  mRightCol  = nsnull;
  mCell      = nsnull;
  mRowIndex  = mRowSpan = mColIndex = mColSpan = 0;
  mRgAtTop = mRgAtBottom = mCgAtLeft = mCgAtRight = PR_FALSE;
}

inline PRInt32 BCMapCellInfo::GetCellEndRowIndex() const
{
  return mRowIndex + mRowSpan - 1;
}

inline PRInt32 BCMapCellInfo::GetCellEndColIndex() const
{
  return mColIndex + mColSpan - 1;
}


class BCMapCellIterator
{
public:
  BCMapCellIterator(nsTableFrame* aTableFrame,
                    const nsRect& aDamageArea);

  void First(BCMapCellInfo& aMapCellInfo);

  void Next(BCMapCellInfo& aMapCellInfo);

  void PeekRight(BCMapCellInfo& aRefInfo,
                 PRUint32     aRowIndex,
                 BCMapCellInfo& aAjaInfo);

  void PeekBottom(BCMapCellInfo& aRefInfo,
                  PRUint32     aColIndex,
                  BCMapCellInfo& aAjaInfo);

  PRBool IsNewRow() { return mIsNewRow; }

  nsTableRowFrame* GetPrevRow() const { return mPrevRow; }
  nsTableRowFrame* GetCurrentRow() const { return mRow; }
  nsTableRowGroupFrame* GetCurrentRowGroup() const { return mRowGroup;}

  PRInt32    mRowGroupStart;
  PRInt32    mRowGroupEnd;
  PRBool     mAtEnd;
  nsCellMap* mCellMap;

private:
  PRBool SetNewRow(nsTableRowFrame* row = nsnull);
  PRBool SetNewRowGroup(PRBool aFindFirstDamagedRow);

  nsTableFrame*         mTableFrame;
  nsTableCellMap*       mTableCellMap;
  nsTableFrame::RowGroupArray mRowGroups;
  nsTableRowGroupFrame* mRowGroup;
  PRInt32               mRowGroupIndex;
  PRUint32              mNumTableRows;
  nsTableRowFrame*      mRow;
  nsTableRowFrame*      mPrevRow;
  PRBool                mIsNewRow;
  PRInt32               mRowIndex;
  PRUint32              mNumTableCols;
  PRInt32               mColIndex;
  nsPoint               mAreaStart;
  nsPoint               mAreaEnd;
};

BCMapCellIterator::BCMapCellIterator(nsTableFrame* aTableFrame,
                                     const nsRect& aDamageArea)
:mTableFrame(aTableFrame)
{
  mTableCellMap  = aTableFrame->GetCellMap();

  mAreaStart.x   = aDamageArea.x;
  mAreaStart.y   = aDamageArea.y;
  mAreaEnd.y     = aDamageArea.y + aDamageArea.height - 1;
  mAreaEnd.x     = aDamageArea.x + aDamageArea.width - 1;

  mNumTableRows  = mTableFrame->GetRowCount();
  mRow           = nsnull;
  mRowIndex      = 0;
  mNumTableCols  = mTableFrame->GetColCount();
  mColIndex      = 0;
  mRowGroupIndex = -1;

  // Get the ordered row groups 
  aTableFrame->OrderRowGroups(mRowGroups);

  mAtEnd = PR_TRUE; // gets reset when First() is called
}

// fill fields that we need for border collapse computation on a given cell
void
BCMapCellInfo::SetInfo(nsTableRowFrame*   aNewRow,
                       PRInt32            aColIndex,
                       BCCellData*        aCellData,
                       BCMapCellIterator* aIter,
                       nsCellMap*         aCellMap)
{
  // fill the cell information
  mCellData = aCellData;
  mColIndex = aColIndex;

  // initialize the row information if it was not previously set for cells in
  // this row
  mRowIndex = 0;
  if (aNewRow) {
    mTopRow = aNewRow;
    mRowIndex = aNewRow->GetRowIndex();
  }

  // fill cell frame info and row information
  mCell      = nsnull;
  mRowSpan   = 1;
  mColSpan   = 1;
  if (aCellData) {
    mCell = static_cast<nsBCTableCellFrame*>(aCellData->GetCellFrame());
    if (mCell) {
      if (!mTopRow) {
        mTopRow = static_cast<nsTableRowFrame*>(mCell->GetParent());
        if (!mTopRow) ABORT0();
        mRowIndex = mTopRow->GetRowIndex();
      }
      mColSpan = mTableFrame->GetEffectiveColSpan(*mCell, aCellMap);
      mRowSpan = mTableFrame->GetEffectiveRowSpan(*mCell, aCellMap);
    }
  }
  
  if (!mTopRow) {
    mTopRow = aIter->GetCurrentRow();
  }
  if (1 == mRowSpan) {
    mBottomRow = mTopRow;
  }
  else {
    mBottomRow = mTopRow->GetNextRow();
    if (mBottomRow) {
      for (PRInt32 spanY = 2; mBottomRow && (spanY < mRowSpan); spanY++) {
        mBottomRow = mBottomRow->GetNextRow();
      }
      NS_ASSERTION(mBottomRow, "spanned row not found");
    }
    else {
      NS_ASSERTION(PR_FALSE, "error in cell map");
      mRowSpan = 1;
      mBottomRow = mTopRow;
    }
  }
  // row group frame info
  // try to reuse the rgStart and rgEnd from the iterator as calls to
  // GetRowCount() are computationally expensive and should be avoided if
  // possible
  PRUint32 rgStart  = aIter->mRowGroupStart;
  PRUint32 rgEnd    = aIter->mRowGroupEnd;
  mRowGroup = static_cast<nsTableRowGroupFrame*>(mTopRow->GetParent());
  if (mRowGroup != aIter->GetCurrentRowGroup()) {
    rgStart = mRowGroup->GetStartRowIndex();
    rgEnd   = rgStart + mRowGroup->GetRowCount() - 1;
  }
  PRUint32 rowIndex = mTopRow->GetRowIndex();
  mRgAtTop    = (rgStart == rowIndex);
  mRgAtBottom = (rgEnd == rowIndex + mRowSpan - 1);
  
   // col frame info
  mLeftCol = mTableFrame->GetColFrame(aColIndex);
  if (!mLeftCol) ABORT0();

  mRightCol = mLeftCol;
  if (mColSpan > 1) {
    nsTableColFrame* colFrame = mTableFrame->GetColFrame(aColIndex +
                                                         mColSpan -1);
    if (!colFrame) ABORT0();
    mRightCol = colFrame;
  }

  // col group frame info
  mColGroup = static_cast<nsTableColGroupFrame*>(mLeftCol->GetParent());
  PRInt32 cgStart = mColGroup->GetStartColumnIndex();
  PRInt32 cgEnd = NS_MAX(0, cgStart + mColGroup->GetColCount() - 1);
  mCgAtLeft  = (cgStart == aColIndex);
  mCgAtRight = (cgEnd == aColIndex + mColSpan - 1);
}

PRBool
BCMapCellIterator::SetNewRow(nsTableRowFrame* aRow)
{
  mAtEnd   = PR_TRUE;
  mPrevRow = mRow;
  if (aRow) {
    mRow = aRow;
  }
  else if (mRow) {
    mRow = mRow->GetNextRow();
  }
  if (mRow) {
    mRowIndex = mRow->GetRowIndex();
    // get to the first entry with an originating cell
    PRInt32 rgRowIndex = mRowIndex - mRowGroupStart;
    if (PRUint32(rgRowIndex) >= mCellMap->mRows.Length()) 
      ABORT1(PR_FALSE);
    const nsCellMap::CellDataArray& row = mCellMap->mRows[rgRowIndex];

    for (mColIndex = mAreaStart.x; mColIndex <= mAreaEnd.x; mColIndex++) {
      CellData* cellData = row.SafeElementAt(mColIndex);
      if (!cellData) { // add a dead cell data
        nsRect damageArea;
        cellData = mCellMap->AppendCell(*mTableCellMap, nsnull, rgRowIndex, PR_FALSE, damageArea); if (!cellData) ABORT1(PR_FALSE);
      }
      if (cellData && (cellData->IsOrig() || cellData->IsDead())) {
        break;
      }
    }
    mIsNewRow = PR_TRUE;
    mAtEnd    = PR_FALSE;
  }
  else ABORT1(PR_FALSE);

  return !mAtEnd;
}

PRBool
BCMapCellIterator::SetNewRowGroup(PRBool aFindFirstDamagedRow)
{
   mAtEnd = PR_TRUE;  
  PRInt32 numRowGroups = mRowGroups.Length();
  mCellMap = nsnull;
  for (mRowGroupIndex++; mRowGroupIndex < numRowGroups; mRowGroupIndex++) {
    mRowGroup = mRowGroups[mRowGroupIndex];
    PRInt32 rowCount = mRowGroup->GetRowCount();
    mRowGroupStart = mRowGroup->GetStartRowIndex();
    mRowGroupEnd   = mRowGroupStart + rowCount - 1;
    if (rowCount > 0) {
      mCellMap = mTableCellMap->GetMapFor(mRowGroup, mCellMap);
      if (!mCellMap) ABORT1(PR_FALSE);
      nsTableRowFrame* firstRow = mRowGroup->GetFirstRow();
      if (aFindFirstDamagedRow) {
        if ((mAreaStart.y >= mRowGroupStart) && (mAreaStart.y <= mRowGroupEnd)) {
          // the damage area starts in the row group 
          if (aFindFirstDamagedRow) {
            // find the correct first damaged row
            PRInt32 numRows = mAreaStart.y - mRowGroupStart;
            for (PRInt32 i = 0; i < numRows; i++) {
              firstRow = firstRow->GetNextRow();
              if (!firstRow) ABORT1(PR_FALSE);
            }
          }
        }
        else {     
          continue;
        }
      }
      if (SetNewRow(firstRow)) { // sets mAtEnd
        break;
      }
    }
  }
    
  return !mAtEnd;
}

void 
BCMapCellIterator::First(BCMapCellInfo& aMapInfo)
{
  aMapInfo.ResetCellInfo();

  SetNewRowGroup(PR_TRUE); // sets mAtEnd
  while (!mAtEnd) {
    if ((mAreaStart.y >= mRowGroupStart) && (mAreaStart.y <= mRowGroupEnd)) {
      BCCellData* cellData =
        static_cast<BCCellData*>(mCellMap->GetDataAt(mAreaStart.y -
                                                      mRowGroupStart,
                                                      mAreaStart.x));
      if (cellData && cellData->IsOrig()) {
        aMapInfo.SetInfo(mRow, mAreaStart.x, cellData, this);
      }
      else {
        NS_ASSERTION(((0 == mAreaStart.x) && (mRowGroupStart == mAreaStart.y)) ,
                     "damage area expanded incorrectly");
        mAtEnd = PR_TRUE;
      }
      break;
    }
    SetNewRowGroup(PR_TRUE); // sets mAtEnd
  } 
}

void 
BCMapCellIterator::Next(BCMapCellInfo& aMapInfo)
{
  if (mAtEnd) ABORT0();
  aMapInfo.ResetCellInfo();

  mIsNewRow = PR_FALSE;
  mColIndex++;
  while ((mRowIndex <= mAreaEnd.y) && !mAtEnd) {
    for (; mColIndex <= mAreaEnd.x; mColIndex++) {
      PRInt32 rgRowIndex = mRowIndex - mRowGroupStart;
      BCCellData* cellData =
         static_cast<BCCellData*>(mCellMap->GetDataAt(rgRowIndex, mColIndex));
      if (!cellData) { // add a dead cell data
        nsRect damageArea;
        cellData =
          static_cast<BCCellData*>(mCellMap->AppendCell(*mTableCellMap, nsnull,
                                                         rgRowIndex, PR_FALSE,
                                                         damageArea));
        if (!cellData) ABORT0();
      }
      if (cellData && (cellData->IsOrig() || cellData->IsDead())) {
        aMapInfo.SetInfo(mRow, mColIndex, cellData, this);
        return;
      }
    }
    if (mRowIndex >= mRowGroupEnd) {
      SetNewRowGroup(PR_FALSE); // could set mAtEnd
    }
    else {
      SetNewRow(); // could set mAtEnd
    }
  }
  mAtEnd = PR_TRUE;
}

void 
BCMapCellIterator::PeekRight(BCMapCellInfo&   aRefInfo,
                             PRUint32         aRowIndex,
                             BCMapCellInfo&   aAjaInfo)
{
  aAjaInfo.ResetCellInfo();
  PRInt32 colIndex = aRefInfo.mColIndex + aRefInfo.mColSpan;
  PRUint32 rgRowIndex = aRowIndex - mRowGroupStart;

  BCCellData* cellData =
    static_cast<BCCellData*>(mCellMap->GetDataAt(rgRowIndex, colIndex));
  if (!cellData) { // add a dead cell data
    NS_ASSERTION(colIndex < mTableCellMap->GetColCount(), "program error");
    nsRect damageArea;
    cellData =
      static_cast<BCCellData*>(mCellMap->AppendCell(*mTableCellMap, nsnull,
                                                     rgRowIndex, PR_FALSE,
                                                     damageArea));
    if (!cellData) ABORT0();
  }
  nsTableRowFrame* row = nsnull;
  if (cellData->IsRowSpan()) {
    rgRowIndex -= cellData->GetRowSpanOffset();
    cellData =
      static_cast<BCCellData*>(mCellMap->GetDataAt(rgRowIndex, colIndex));
    if (!cellData)
      ABORT0();
  }
  else {
    row = mRow;
  }
  aAjaInfo.SetInfo(row, colIndex, cellData, this);
}

void 
BCMapCellIterator::PeekBottom(BCMapCellInfo&   aRefInfo,
                              PRUint32         aColIndex,
                              BCMapCellInfo&   aAjaInfo)
{
  aAjaInfo.ResetCellInfo();
  PRInt32 rowIndex = aRefInfo.mRowIndex + aRefInfo.mRowSpan;
  PRInt32 rgRowIndex = rowIndex - mRowGroupStart;
  nsTableRowGroupFrame* rg = mRowGroup;
  nsCellMap* cellMap = mCellMap;
  nsTableRowFrame* nextRow = nsnull;
  if (rowIndex > mRowGroupEnd) {
    PRInt32 nextRgIndex = mRowGroupIndex;
    do {
      nextRgIndex++;
      rg = mRowGroups.SafeElementAt(nextRgIndex);
      if (rg) {
        cellMap = mTableCellMap->GetMapFor(rg, cellMap); if (!cellMap) ABORT0();
        rgRowIndex = 0;
        nextRow = rg->GetFirstRow();
      }
    }
    while (rg && !nextRow);
    if(!rg) return;
  }
  else {
    // get the row within the same row group
    nextRow = mRow;
    for (PRInt32 i = 0; i < aRefInfo.mRowSpan; i++) {
      nextRow = nextRow->GetNextRow(); if (!nextRow) ABORT0();
    }
  }

  BCCellData* cellData =
    static_cast<BCCellData*>(cellMap->GetDataAt(rgRowIndex, aColIndex));
  if (!cellData) { // add a dead cell data
    NS_ASSERTION(rgRowIndex < cellMap->GetRowCount(), "program error");
    nsRect damageArea;
    cellData =
      static_cast<BCCellData*>(cellMap->AppendCell(*mTableCellMap, nsnull,
                                                    rgRowIndex, PR_FALSE,
                                                    damageArea));
    if (!cellData) ABORT0();
  }
  if (cellData->IsColSpan()) {
    aColIndex -= cellData->GetColSpanOffset();
    cellData =
      static_cast<BCCellData*>(cellMap->GetDataAt(rgRowIndex, aColIndex));
  }
  aAjaInfo.SetInfo(nextRow, aColIndex, cellData, this, cellMap);
}

// Assign priorities to border styles. For example, styleToPriority(NS_STYLE_BORDER_STYLE_SOLID)
// will return the priority of NS_STYLE_BORDER_STYLE_SOLID. 
static PRUint8 styleToPriority[13] = { 0,  // NS_STYLE_BORDER_STYLE_NONE
                                       2,  // NS_STYLE_BORDER_STYLE_GROOVE
                                       4,  // NS_STYLE_BORDER_STYLE_RIDGE
                                       5,  // NS_STYLE_BORDER_STYLE_DOTTED
                                       6,  // NS_STYLE_BORDER_STYLE_DASHED
                                       7,  // NS_STYLE_BORDER_STYLE_SOLID
                                       8,  // NS_STYLE_BORDER_STYLE_DOUBLE
                                       1,  // NS_STYLE_BORDER_STYLE_INSET
                                       3,  // NS_STYLE_BORDER_STYLE_OUTSET
                                       9 };// NS_STYLE_BORDER_STYLE_HIDDEN
// priority rules follow CSS 2.1 spec
// 'hidden', 'double', 'solid', 'dashed', 'dotted', 'ridge', 'outset', 'groove',
// and the lowest: 'inset'. none is even weaker
#define CELL_CORNER PR_TRUE

/** return the border style, border color for a given frame and side
  * @param aFrame           - query the info for this frame 
  * @param aSide            - the side of the frame
  * @param aStyle           - the border style
  * @param aColor           - the border color
  * @param aTableIsLTR      - table direction is LTR
  */
static void 
GetColorAndStyle(const nsIFrame*  aFrame,
                 PRUint8          aSide,
                 PRUint8&         aStyle,
                 nscolor&         aColor,
                 PRBool           aTableIsLTR)
{
  NS_PRECONDITION(aFrame, "null frame");
  // initialize out arg
  aColor = 0;
  const nsStyleBorder* styleData = aFrame->GetStyleBorder();
  if(!aTableIsLTR) { // revert the directions
    if (NS_SIDE_RIGHT == aSide) {
      aSide = NS_SIDE_LEFT;
    }
    else if (NS_SIDE_LEFT == aSide) {
      aSide = NS_SIDE_RIGHT;
    }
  }
  aStyle = styleData->GetBorderStyle(aSide);

  if ((NS_STYLE_BORDER_STYLE_NONE == aStyle) ||
      (NS_STYLE_BORDER_STYLE_HIDDEN == aStyle)) {
    return;
  }
  PRBool foreground;
  styleData->GetBorderColor(aSide, aColor, foreground);
  if (foreground) {
    aColor = aFrame->GetStyleColor()->mColor;
  }
}

/** coerce the paint style as required by CSS2.1
  * @param aFrame           - query the info for this frame 
  * @param aSide            - the side of the frame
  * @param aStyle           - the border style
  * @param aColor           - the border color
  * @param aTableIsLTR      - table direction is LTR
  */
static void
GetPaintStyleInfo(const nsIFrame*  aFrame,
                  PRUint8          aSide,
                  PRUint8&         aStyle,
                  nscolor&         aColor,
                  PRBool           aTableIsLTR)
{
  GetColorAndStyle(aFrame, aSide, aStyle, aColor, aTableIsLTR);
  if (NS_STYLE_BORDER_STYLE_INSET    == aStyle) {
    aStyle = NS_STYLE_BORDER_STYLE_RIDGE;
  }
  else if (NS_STYLE_BORDER_STYLE_OUTSET    == aStyle) {
    aStyle = NS_STYLE_BORDER_STYLE_GROOVE;
  }
}

/** return the border style, border color and the width in pixel for a given
  * frame and side
  * @param aFrame           - query the info for this frame 
  * @param aSide            - the side of the frame
  * @param aStyle           - the border style
  * @param aColor           - the border color
  * @param aTableIsLTR      - table direction is LTR
  * @param aWidth           - the border width in px.
  * @param aTwipsToPixels   - conversion factor from twips to pixel
  */
static void
GetColorAndStyle(const nsIFrame*  aFrame,
                 PRUint8          aSide,
                 PRUint8&         aStyle,
                 nscolor&         aColor,
                 PRBool           aTableIsLTR,
                 BCPixelSize&     aWidth)
{
  GetColorAndStyle(aFrame, aSide, aStyle, aColor, aTableIsLTR);
  if ((NS_STYLE_BORDER_STYLE_NONE == aStyle) ||
      (NS_STYLE_BORDER_STYLE_HIDDEN == aStyle)) {
    aWidth = 0;
    return;
  }
  const nsStyleBorder* styleData = aFrame->GetStyleBorder();
  nscoord width;
  if(!aTableIsLTR) { // revert the directions
    if (NS_SIDE_RIGHT == aSide) {
      aSide = NS_SIDE_LEFT;
    }
    else if (NS_SIDE_LEFT == aSide) {
      aSide = NS_SIDE_RIGHT;
    }
  }
  width = styleData->GetActualBorderWidth(aSide);
  aWidth = nsPresContext::AppUnitsToIntCSSPixels(width);
}

class nsDelayedCalcBCBorders : public nsRunnable {
public:
  nsDelayedCalcBCBorders(nsIFrame* aFrame) :
    mFrame(aFrame) {}

  NS_IMETHOD Run() {
    if (mFrame) {
      nsTableFrame* tableFrame = static_cast <nsTableFrame*>(mFrame.GetFrame());
      if (tableFrame->NeedToCalcBCBorders()) {
        tableFrame->CalcBCBorders();
      }
    }
    return NS_OK;
  }
private:
  nsWeakFrame mFrame;
};
  
PRBool
nsTableFrame::BCRecalcNeeded(nsStyleContext* aOldStyleContext,
                             nsStyleContext* aNewStyleContext)
{
  // Attention: the old style context is the one we're forgetting,
  // and hence possibly completely bogus for GetStyle* purposes.
  // We use PeekStyleData instead.

  const nsStyleBorder* oldStyleData = static_cast<const nsStyleBorder*>
                        (aOldStyleContext->PeekStyleData(eStyleStruct_Border));
  if (!oldStyleData)
    return PR_FALSE;

  const nsStyleBorder* newStyleData = aNewStyleContext->GetStyleBorder();
  nsChangeHint change = newStyleData->CalcDifference(*oldStyleData);
  if (!change)
    return PR_FALSE;
  if (change & nsChangeHint_ReflowFrame)
    return PR_TRUE; // the caller only needs to mark the bc damage area
  if (change & nsChangeHint_RepaintFrame) {
    // we need to recompute the borders and the caller needs to mark
    // the bc damage area
    // XXX In principle this should only be necessary for border style changes
    // However the bc painting code tries to maximize the drawn border segments
    // so it stores in the cellmap where a new border segment starts and this
    // introduces a unwanted cellmap data dependence on color
    nsCOMPtr<nsIRunnable> evt = new nsDelayedCalcBCBorders(this);
    NS_DispatchToCurrentThread(evt);
    return PR_TRUE;
  }
  return PR_FALSE;
}


// Compare two border segments, this comparison depends whether the two
// segments meet at a corner and whether the second segment is horizontal.
// The return value is whichever of aBorder1 or aBorder2 dominates.
static const BCCellBorder&
CompareBorders(PRBool              aIsCorner, // Pass PR_TRUE for corner calculations
               const BCCellBorder& aBorder1,
               const BCCellBorder& aBorder2,
               PRBool              aSecondIsHorizontal,
               PRBool*             aFirstDominates = nsnull)
{
  PRBool firstDominates = PR_TRUE;
  
  if (NS_STYLE_BORDER_STYLE_HIDDEN == aBorder1.style) {
    firstDominates = (aIsCorner) ? PR_FALSE : PR_TRUE;
  }
  else if (NS_STYLE_BORDER_STYLE_HIDDEN == aBorder2.style) {
    firstDominates = (aIsCorner) ? PR_TRUE : PR_FALSE;
  }
  else if (aBorder1.width < aBorder2.width) {
    firstDominates = PR_FALSE;
  }
  else if (aBorder1.width == aBorder2.width) {
    if (styleToPriority[aBorder1.style] < styleToPriority[aBorder2.style]) {
      firstDominates = PR_FALSE;
    }
    else if (styleToPriority[aBorder1.style] == styleToPriority[aBorder2.style]) {
      if (aBorder1.owner == aBorder2.owner) {
        firstDominates = !aSecondIsHorizontal;
      }
      else if (aBorder1.owner < aBorder2.owner) {
        firstDominates = PR_FALSE;
      }
    }
  }

  if (aFirstDominates)
    *aFirstDominates = firstDominates;

  if (firstDominates)
    return aBorder1;
  return aBorder2;
}

/** calc the dominant border by considering the table, row/col group, row/col,
  * cell. 
  * Depending on whether the side is vertical or horizontal and whether
  * adjacent frames are taken into account the ownership of a single border
  * segment is defined. The return value is the dominating border
  * The cellmap stores only top and left borders for each cellmap position.
  * If the cell border is owned by the cell that is left of the border
  * it will be an adjacent owner aka eAjaCellOwner. See celldata.h for the other
  * scenarios with a adjacent owner.
  * @param xxxFrame         - the frame for style information, might be zero if
  *                           it should not be considered
  * @param aSide            - side of the frames that should be considered
  * @param aAja             - the border comparison takes place from the point of
  *                           a frame that is adjacent to the cellmap entry, for
  *                           when a cell owns its lower border it will be the
  *                           adjacent owner as in the cellmap only top and left
  *                           borders are stored. 
  * @param aTwipsToPixels   - conversion factor as borders need to be drawn pixel
  *                           aligned.
  */
static BCCellBorder
CompareBorders(const nsIFrame*  aTableFrame,
               const nsIFrame*  aColGroupFrame,
               const nsIFrame*  aColFrame,
               const nsIFrame*  aRowGroupFrame,
               const nsIFrame*  aRowFrame,
               const nsIFrame*  aCellFrame,
               PRBool           aTableIsLTR,
               PRUint8          aSide,
               PRBool           aAja)
{
  BCCellBorder border, tempBorder;
  PRBool horizontal = (NS_SIDE_TOP == aSide) || (NS_SIDE_BOTTOM == aSide);

  // start with the table as dominant if present
  if (aTableFrame) {
    GetColorAndStyle(aTableFrame, aSide, border.style, border.color, aTableIsLTR, border.width);
    border.owner = eTableOwner;
    if (NS_STYLE_BORDER_STYLE_HIDDEN == border.style) {
      return border;
    }
  }
  // see if the colgroup is dominant
  if (aColGroupFrame) {
    GetColorAndStyle(aColGroupFrame, aSide, tempBorder.style, tempBorder.color, aTableIsLTR, tempBorder.width);
    tempBorder.owner = (aAja && !horizontal) ? eAjaColGroupOwner : eColGroupOwner;
    // pass here and below PR_FALSE for aSecondIsHorizontal as it is only used for corner calculations.
    border = CompareBorders(!CELL_CORNER, border, tempBorder, PR_FALSE);
    if (NS_STYLE_BORDER_STYLE_HIDDEN == border.style) {
      return border;
    }
  }
  // see if the col is dominant
  if (aColFrame) {
    GetColorAndStyle(aColFrame, aSide, tempBorder.style, tempBorder.color, aTableIsLTR, tempBorder.width);
    tempBorder.owner = (aAja && !horizontal) ? eAjaColOwner : eColOwner;
    border = CompareBorders(!CELL_CORNER, border, tempBorder, PR_FALSE);
    if (NS_STYLE_BORDER_STYLE_HIDDEN == border.style) {
      return border;
    }
  }
  // see if the rowgroup is dominant
  if (aRowGroupFrame) {
    GetColorAndStyle(aRowGroupFrame, aSide, tempBorder.style, tempBorder.color, aTableIsLTR, tempBorder.width);
    tempBorder.owner = (aAja && horizontal) ? eAjaRowGroupOwner : eRowGroupOwner;
    border = CompareBorders(!CELL_CORNER, border, tempBorder, PR_FALSE);
    if (NS_STYLE_BORDER_STYLE_HIDDEN == border.style) {
      return border;
    }
  }
  // see if the row is dominant
  if (aRowFrame) {
    GetColorAndStyle(aRowFrame, aSide, tempBorder.style, tempBorder.color, aTableIsLTR, tempBorder.width);
    tempBorder.owner = (aAja && horizontal) ? eAjaRowOwner : eRowOwner;
    border = CompareBorders(!CELL_CORNER, border, tempBorder, PR_FALSE);
    if (NS_STYLE_BORDER_STYLE_HIDDEN == border.style) {
      return border;
    }
  }
  // see if the cell is dominant
  if (aCellFrame) {
    GetColorAndStyle(aCellFrame, aSide, tempBorder.style, tempBorder.color, aTableIsLTR, tempBorder.width);
    tempBorder.owner = (aAja) ? eAjaCellOwner : eCellOwner;
    border = CompareBorders(!CELL_CORNER, border, tempBorder, PR_FALSE);
  }
  return border;
}

static PRBool 
Perpendicular(PRUint8 aSide1, 
              PRUint8 aSide2)
{
  switch (aSide1) {
  case NS_SIDE_TOP:
    return (NS_SIDE_BOTTOM != aSide2);
  case NS_SIDE_RIGHT:
    return (NS_SIDE_LEFT != aSide2);
  case NS_SIDE_BOTTOM:
    return (NS_SIDE_TOP != aSide2);
  default: // NS_SIDE_LEFT
    return (NS_SIDE_RIGHT != aSide2);
  }
}

// XXX allocate this as number-of-cols+1 instead of number-of-cols+1 * number-of-rows+1
struct BCCornerInfo 
{
  BCCornerInfo() { ownerColor = 0; ownerWidth = subWidth = ownerSide = ownerElem = subSide = 
                   subElem = hasDashDot = numSegs = bevel = 0;
                   ownerStyle = 0xFF; subStyle = NS_STYLE_BORDER_STYLE_SOLID;  }
  void Set(PRUint8       aSide,
           BCCellBorder  border);

  void Update(PRUint8       aSide,
              BCCellBorder  border);

  nscolor   ownerColor;     // color of borderOwner
  PRUint16  ownerWidth;     // pixel width of borderOwner 
  PRUint16  subWidth;       // pixel width of the largest border intersecting the border perpendicular 
                            // to ownerSide
  PRUint32  ownerSide:2;    // side (e.g NS_SIDE_TOP, NS_SIDE_RIGHT, etc) of the border owning 
                            // the corner relative to the corner
  PRUint32  ownerElem:3;    // elem type (e.g. eTable, eGroup, etc) owning the corner
  PRUint32  ownerStyle:8;   // border style of ownerElem
  PRUint32  subSide:2;      // side of border with subWidth relative to the corner
  PRUint32  subElem:3;      // elem type (e.g. eTable, eGroup, etc) of sub owner
  PRUint32  subStyle:8;     // border style of subElem
  PRUint32  hasDashDot:1;   // does a dashed, dotted segment enter the corner, they cannot be beveled
  PRUint32  numSegs:3;      // number of segments entering corner
  PRUint32  bevel:1;        // is the corner beveled (uses the above two fields together with subWidth)
  PRUint32  unused:1;
};

void 
BCCornerInfo::Set(PRUint8       aSide,
                  BCCellBorder  aBorder)
{
  ownerElem  = aBorder.owner;
  ownerStyle = aBorder.style;
  ownerWidth = aBorder.width;
  ownerColor = aBorder.color;
  ownerSide  = aSide;
  hasDashDot = 0;
  numSegs    = 0;
  if (aBorder.width > 0) {
    numSegs++;
    hasDashDot = (NS_STYLE_BORDER_STYLE_DASHED == aBorder.style) ||
                 (NS_STYLE_BORDER_STYLE_DOTTED == aBorder.style);
  }
  bevel      = 0;
  subWidth   = 0;
  // the following will get set later
  subSide    = ((aSide == NS_SIDE_LEFT) || (aSide == NS_SIDE_RIGHT)) ? NS_SIDE_TOP : NS_SIDE_LEFT; 
  subElem    = eTableOwner;
  subStyle   = NS_STYLE_BORDER_STYLE_SOLID; 
}

void 
BCCornerInfo::Update(PRUint8       aSide,
                     BCCellBorder  aBorder)
{
  PRBool existingWins = PR_FALSE;
  if (0xFF == ownerStyle) { // initial value indiating that it hasn't been set yet
    Set(aSide, aBorder);
  }
  else {
    PRBool horizontal = (NS_SIDE_LEFT == aSide) || (NS_SIDE_RIGHT == aSide); // relative to the corner
    BCCellBorder oldBorder, tempBorder;
    oldBorder.owner  = (BCBorderOwner) ownerElem;
    oldBorder.style =  ownerStyle;
    oldBorder.width =  ownerWidth;
    oldBorder.color =  ownerColor;

    PRUint8 oldSide  = ownerSide;
    
    tempBorder = CompareBorders(CELL_CORNER, oldBorder, aBorder, horizontal, &existingWins); 
                         
    ownerElem  = tempBorder.owner;
    ownerStyle = tempBorder.style;
    ownerWidth = tempBorder.width;
    ownerColor = tempBorder.color;
    if (existingWins) { // existing corner is dominant
      if (::Perpendicular(ownerSide, aSide)) {
        // see if the new sub info replaces the old
        BCCellBorder subBorder;
        subBorder.owner = (BCBorderOwner) subElem;
        subBorder.style =  subStyle;
        subBorder.width =  subWidth;
        subBorder.color = 0; // we are not interested in subBorder color
        PRBool firstWins;

        tempBorder = CompareBorders(CELL_CORNER, subBorder, aBorder, horizontal, &firstWins);
        
        subElem  = tempBorder.owner;
        subStyle = tempBorder.style;
        subWidth = tempBorder.width;
        if (!firstWins) {
          subSide = aSide; 
        }
      }
    }
    else { // input args are dominant
      ownerSide = aSide;
      if (::Perpendicular(oldSide, ownerSide)) {
        subElem  = oldBorder.owner;
        subStyle = oldBorder.style;
        subWidth = oldBorder.width;
        subSide  = oldSide;
      }
    }
    if (aBorder.width > 0) {
      numSegs++;
      if (!hasDashDot && ((NS_STYLE_BORDER_STYLE_DASHED == aBorder.style) ||
                          (NS_STYLE_BORDER_STYLE_DOTTED == aBorder.style))) {
        hasDashDot = 1;
      }
    }
  
    // bevel the corner if only two perpendicular non dashed/dotted segments enter the corner
    bevel = (2 == numSegs) && (subWidth > 1) && (0 == hasDashDot);
  }
}

struct BCCorners
{
  BCCorners(PRInt32 aNumCorners,
            PRInt32 aStartIndex);

  ~BCCorners() { delete [] corners; }
  
  BCCornerInfo& operator [](PRInt32 i) const
  { NS_ASSERTION((i >= startIndex) && (i <= endIndex), "program error");
    return corners[NS_MAX(NS_MIN(i, endIndex), startIndex) - startIndex]; }

  PRInt32       startIndex;
  PRInt32       endIndex;
  BCCornerInfo* corners;
};
  
BCCorners::BCCorners(PRInt32 aNumCorners,
                     PRInt32 aStartIndex)
{
  NS_ASSERTION((aNumCorners > 0) && (aStartIndex >= 0), "program error");
  startIndex = aStartIndex;
  endIndex   = aStartIndex + aNumCorners - 1;
  corners    = new BCCornerInfo[aNumCorners]; 
}


struct BCCellBorders
{
  BCCellBorders(PRInt32 aNumBorders,
                PRInt32 aStartIndex);

  ~BCCellBorders() { delete [] borders; }
  
  BCCellBorder& operator [](PRInt32 i) const
  { NS_ASSERTION((i >= startIndex) && (i <= endIndex), "program error");
    return borders[NS_MAX(NS_MIN(i, endIndex), startIndex) - startIndex]; }

  PRInt32       startIndex;
  PRInt32       endIndex;
  BCCellBorder* borders;
};
  
BCCellBorders::BCCellBorders(PRInt32 aNumBorders,
                             PRInt32 aStartIndex)
{
  NS_ASSERTION((aNumBorders > 0) && (aStartIndex >= 0), "program error");
  startIndex = aStartIndex;
  endIndex   = aStartIndex + aNumBorders - 1;
  borders    = new BCCellBorder[aNumBorders]; 
}

// this function sets the new border properties and returns true if the border
// segment will start a new segment and not be accumulated into the previous
// segment.
static PRBool
SetBorder(const BCCellBorder&   aNewBorder,
          BCCellBorder&         aBorder)
{
  PRBool changed = (aNewBorder.style != aBorder.style) ||
                   (aNewBorder.width != aBorder.width) ||
                   (aNewBorder.color != aBorder.color);
  aBorder.color        = aNewBorder.color;
  aBorder.width        = aNewBorder.width;
  aBorder.style        = aNewBorder.style;
  aBorder.owner        = aNewBorder.owner;

  return changed;
}

// this function will set the horizontal border. It will return true if the 
// existing segment will not be continued. Having a vertical owner of a corner
// should also start a new segment.
static PRBool
SetHorBorder(const BCCellBorder& aNewBorder,
             const BCCornerInfo& aCorner,
             BCCellBorder&       aBorder)
{
  PRBool startSeg = ::SetBorder(aNewBorder, aBorder);
  if (!startSeg) {
    startSeg = ((NS_SIDE_LEFT != aCorner.ownerSide) && (NS_SIDE_RIGHT != aCorner.ownerSide));
  }
  return startSeg;
}

// Make the damage area larger on the top and bottom by at least one row and on the left and right 
// at least one column. This is done so that adjacent elements are part of the border calculations. 
// The extra segments and borders outside the actual damage area will not be updated in the cell map, 
// because they in turn would need info from adjacent segments outside the damage area to be accurate.
void
nsTableFrame::ExpandBCDamageArea(nsRect& aRect) const
{
  PRInt32 numRows = GetRowCount();
  PRInt32 numCols = GetColCount();

  PRInt32 dStartX = aRect.x;
  PRInt32 dEndX   = aRect.XMost() - 1;
  PRInt32 dStartY = aRect.y;
  PRInt32 dEndY   = aRect.YMost() - 1;

  // expand the damage area in each direction
  if (dStartX > 0) {
    dStartX--;
  }
  if (dEndX < (numCols - 1)) {
    dEndX++;
  }
  if (dStartY > 0) {
    dStartY--;
  }
  if (dEndY < (numRows - 1)) {
    dEndY++;
  }
  // Check the damage area so that there are no cells spanning in or out. If there are any then
  // make the damage area as big as the table, similarly to the way the cell map decides whether
  // to rebuild versus expand. This could be optimized to expand to the smallest area that contains
  // no spanners, but it may not be worth the effort in general, and it would need to be done in the
  // cell map as well.
  PRBool haveSpanner = PR_FALSE;
  if ((dStartX > 0) || (dEndX < (numCols - 1)) || (dStartY > 0) || (dEndY < (numRows - 1))) {
    nsTableCellMap* tableCellMap = GetCellMap(); if (!tableCellMap) ABORT0();
    // Get the ordered row groups 
    RowGroupArray rowGroups;
    OrderRowGroups(rowGroups);

    // Scope outside loop to be used as hint.
    nsCellMap* cellMap = nsnull;
    for (PRUint32 rgX = 0; rgX < rowGroups.Length(); rgX++) {
      nsTableRowGroupFrame* rgFrame = rowGroups[rgX];
      PRInt32 rgStartY = rgFrame->GetStartRowIndex();
      PRInt32 rgEndY   = rgStartY + rgFrame->GetRowCount() - 1;
      if (dEndY < rgStartY) 
        break;
      cellMap = tableCellMap->GetMapFor(rgFrame, cellMap);
      if (!cellMap) ABORT0();
      // check for spanners from above and below
      if ((dStartY > 0) && (dStartY >= rgStartY) && (dStartY <= rgEndY)) {
        if (PRUint32(dStartY - rgStartY) >= cellMap->mRows.Length()) 
          ABORT0();
        const nsCellMap::CellDataArray& row =
          cellMap->mRows[dStartY - rgStartY];
        for (PRInt32 x = dStartX; x <= dEndX; x++) {
          CellData* cellData = row.SafeElementAt(x);
          if (cellData && (cellData->IsRowSpan())) {
             haveSpanner = PR_TRUE;
             break;
          }
        }
        if (dEndY < rgEndY) {
          if (PRUint32(dEndY + 1 - rgStartY) >= cellMap->mRows.Length()) 
            ABORT0();
          const nsCellMap::CellDataArray& row2 =
            cellMap->mRows[dEndY + 1 - rgStartY];
          for (PRInt32 x = dStartX; x <= dEndX; x++) {
            CellData* cellData = row2.SafeElementAt(x);
            if (cellData && (cellData->IsRowSpan())) {
              haveSpanner = PR_TRUE;
              break;
            }
          }
        }
      }
      // check for spanners on the left and right
      PRInt32 iterStartY = -1;
      PRInt32 iterEndY   = -1;
      if ((dStartY >= rgStartY) && (dStartY <= rgEndY)) {
        // the damage area starts in the row group
        iterStartY = dStartY;
        iterEndY   = NS_MIN(dEndY, rgEndY);
      }
      else if ((dEndY >= rgStartY) && (dEndY <= rgEndY)) {
        // the damage area ends in the row group
        iterStartY = rgStartY;
        iterEndY   = NS_MIN(dEndY, rgStartY);
      }
      else if ((rgStartY >= dStartY) && (rgEndY <= dEndY)) {
        // the damage area contains the row group
        iterStartY = rgStartY;
        iterEndY   = rgEndY;
      }
      if ((iterStartY >= 0) && (iterEndY >= 0)) {
        for (PRInt32 y = iterStartY; y <= iterEndY; y++) {
          if (PRUint32(y - rgStartY) >= cellMap->mRows.Length()) 
            ABORT0();
          const nsCellMap::CellDataArray& row =
            cellMap->mRows[y - rgStartY];
          CellData* cellData = row.SafeElementAt(dStartX);
          if (cellData && (cellData->IsColSpan())) {
            haveSpanner = PR_TRUE;
            break;
          }
          if (dEndX < (numCols - 1)) {
            cellData = row.SafeElementAt(dEndX + 1);
            if (cellData && (cellData->IsColSpan())) {
              haveSpanner = PR_TRUE;
              break;
            }
          }
        }
      }
    }
  }
  if (haveSpanner) {
    // make the damage area the whole table
    aRect.x      = 0;
    aRect.y      = 0;
    aRect.width  = numCols;
    aRect.height = numRows;
  }
  else {
    aRect.x      = dStartX;
    aRect.y      = dStartY;
    aRect.width  = 1 + dEndX - dStartX;
    aRect.height = 1 + dEndY - dStartY;
  }
}

#define MAX_TABLE_BORDER_WIDTH 255
static PRUint8
LimitBorderWidth(PRUint16 aWidth)
{
  return NS_MIN(PRUint16(MAX_TABLE_BORDER_WIDTH), aWidth);
}

#define ADJACENT    PR_TRUE
#define HORIZONTAL  PR_TRUE

void
BCMapCellInfo::SetTableTopLeftContBCBorder()
{
  BCCellBorder currentBorder;
  //calculate continuous top first row & rowgroup border: special case
  //because it must include the table in the collapse
  if (mTopRow) {
    currentBorder = CompareBorders(mTableFrame, nsnull, nsnull, mRowGroup,
                                   mTopRow, nsnull, mTableIsLTR,
                                   NS_SIDE_TOP, !ADJACENT);
    mTopRow->SetContinuousBCBorderWidth(NS_SIDE_TOP, currentBorder.width);
  }
  if (mCgAtRight && mColGroup) {
    //calculate continuous top colgroup border once per colgroup
    currentBorder = CompareBorders(mTableFrame, mColGroup, nsnull, mRowGroup,
                                   mTopRow, nsnull, mTableIsLTR,
                                   NS_SIDE_TOP, !ADJACENT);
    mColGroup->SetContinuousBCBorderWidth(NS_SIDE_TOP, currentBorder.width);
  }
  if (0 == mColIndex) {
    currentBorder = CompareBorders(mTableFrame, mColGroup, mLeftCol, nsnull,
                                   nsnull, nsnull, mTableIsLTR, NS_SIDE_LEFT,
                                   !ADJACENT);
    mTableFrame->SetContinuousLeftBCBorderWidth(currentBorder.width);
  }
}

void
BCMapCellInfo::SetRowGroupLeftContBCBorder()
{
  BCCellBorder currentBorder;
  //get row group continuous borders
  if (mRgAtBottom && mRowGroup) { //once per row group, so check for bottom
     currentBorder = CompareBorders(mTableFrame, mColGroup, mLeftCol, mRowGroup,
                                    nsnull, nsnull, mTableIsLTR, NS_SIDE_LEFT,
                                    !ADJACENT);
     mRowGroup->SetContinuousBCBorderWidth(mStartSide, currentBorder.width);
  }
}

void
BCMapCellInfo::SetRowGroupRightContBCBorder()
{
  BCCellBorder currentBorder;
  //get row group continuous borders
  if (mRgAtBottom && mRowGroup) { //once per mRowGroup, so check for bottom
    currentBorder = CompareBorders(mTableFrame, mColGroup, mRightCol, mRowGroup,
                                   nsnull, nsnull, mTableIsLTR, NS_SIDE_RIGHT,
                                   ADJACENT);
    mRowGroup->SetContinuousBCBorderWidth(mEndSide, currentBorder.width);
  }
}

void
BCMapCellInfo::SetColumnTopRightContBCBorder()
{
  BCCellBorder currentBorder;
  //calculate column continuous borders
  //we only need to do this once, so we'll do it only on the first row
  currentBorder = CompareBorders(mTableFrame, mCurrentColGroupFrame,
                                 mCurrentColFrame, mRowGroup, mTopRow, nsnull,
                                 mTableIsLTR, NS_SIDE_TOP, !ADJACENT);
  ((nsTableColFrame*) mCurrentColFrame)->SetContinuousBCBorderWidth(NS_SIDE_TOP,
                                                           currentBorder.width);
  if (mNumTableCols == GetCellEndColIndex() + 1) {
    currentBorder = CompareBorders(mTableFrame, mCurrentColGroupFrame,
                                   mCurrentColFrame, nsnull, nsnull, nsnull,
                                   mTableIsLTR, NS_SIDE_RIGHT, !ADJACENT);
  }
  else {
    currentBorder = CompareBorders(nsnull, mCurrentColGroupFrame,
                                   mCurrentColFrame, nsnull,nsnull, nsnull,
                                   mTableIsLTR, NS_SIDE_RIGHT, !ADJACENT);
  }
  mCurrentColFrame->SetContinuousBCBorderWidth(NS_SIDE_RIGHT,
                                               currentBorder.width);
}

void
BCMapCellInfo::SetColumnBottomContBCBorder()
{
  BCCellBorder currentBorder;
  //get col continuous border
  currentBorder = CompareBorders(mTableFrame, mCurrentColGroupFrame,
                                 mCurrentColFrame, mRowGroup, mBottomRow,
                                 nsnull, mTableIsLTR, NS_SIDE_BOTTOM, ADJACENT);
  mCurrentColFrame->SetContinuousBCBorderWidth(NS_SIDE_BOTTOM,
                                               currentBorder.width);
}

void
BCMapCellInfo::SetColGroupBottomContBCBorder()
{
  BCCellBorder currentBorder;
  if (mColGroup) {
    currentBorder = CompareBorders(mTableFrame, mColGroup, nsnull, mRowGroup,
                                   mBottomRow, nsnull, mTableIsLTR,
                                   NS_SIDE_BOTTOM, ADJACENT);
    mColGroup->SetContinuousBCBorderWidth(NS_SIDE_BOTTOM, currentBorder.width);
  }
}

void
BCMapCellInfo::SetRowGroupBottomContBCBorder()
{
  BCCellBorder currentBorder;
  if (mRowGroup) {
    currentBorder = CompareBorders(mTableFrame, nsnull, nsnull, mRowGroup,
                                   mBottomRow, nsnull, mTableIsLTR,
                                   NS_SIDE_BOTTOM, ADJACENT);
    mRowGroup->SetContinuousBCBorderWidth(NS_SIDE_BOTTOM, currentBorder.width);
  }
}

void
BCMapCellInfo::SetInnerRowGroupBottomContBCBorder(const nsIFrame* aNextRowGroup,
                                                  nsTableRowFrame* aNextRow)
{
  BCCellBorder currentBorder, adjacentBorder;
  
  const nsIFrame* rowgroup = (mRgAtBottom) ? mRowGroup : nsnull;
  currentBorder = CompareBorders(nsnull, nsnull, nsnull, rowgroup, mBottomRow,
                                 nsnull, mTableIsLTR, NS_SIDE_BOTTOM, ADJACENT);

  adjacentBorder = CompareBorders(nsnull, nsnull, nsnull, aNextRowGroup,
                                  aNextRow, nsnull, mTableIsLTR, NS_SIDE_TOP,
                                  !ADJACENT);
  currentBorder = CompareBorders(PR_FALSE, currentBorder, adjacentBorder,
                                 HORIZONTAL);
  if (aNextRow) {
    aNextRow->SetContinuousBCBorderWidth(NS_SIDE_TOP, currentBorder.width);
  }
  if (mRgAtBottom && mRowGroup) {
    mRowGroup->SetContinuousBCBorderWidth(NS_SIDE_BOTTOM, currentBorder.width);
  }
}

void
BCMapCellInfo::SetRowLeftContBCBorder()
{
  //get row continuous borders
  if (mCurrentRowFrame) {
    BCCellBorder currentBorder;
    currentBorder = CompareBorders(mTableFrame, mColGroup, mLeftCol, mRowGroup,
                                   mCurrentRowFrame, nsnull, mTableIsLTR,
                                   NS_SIDE_LEFT, !ADJACENT);
    mCurrentRowFrame->SetContinuousBCBorderWidth(mStartSide,
                                                 currentBorder.width);
  }
}

void
BCMapCellInfo::SetRowRightContBCBorder()
{
  if (mCurrentRowFrame) {
    BCCellBorder currentBorder;
    currentBorder = CompareBorders(mTableFrame, mColGroup, mRightCol, mRowGroup,
                                   mCurrentRowFrame, nsnull, mTableIsLTR,
                                   NS_SIDE_RIGHT, ADJACENT);
    mCurrentRowFrame->SetContinuousBCBorderWidth(mEndSide,
                                                 currentBorder.width);
  }
}
void
BCMapCellInfo::SetTableTopBorderWidth(BCPixelSize aWidth)
{
  mTableBCData->mTopBorderWidth =
     LimitBorderWidth(NS_MAX(mTableBCData->mTopBorderWidth, aWidth));
}

void
BCMapCellInfo::SetTableLeftBorderWidth(PRInt32 aRowY, BCPixelSize aWidth)
{
  // update the left/right first cell border
  if (aRowY == 0) {
    if (mTableIsLTR) {
      mTableBCData->mLeftCellBorderWidth = aWidth;
    }
    else {
      mTableBCData->mRightCellBorderWidth = aWidth;
    }
  }
  mTableBCData->mLeftBorderWidth =
               LimitBorderWidth(NS_MAX(mTableBCData->mLeftBorderWidth, aWidth));
}

void
BCMapCellInfo::SetTableRightBorderWidth(PRInt32 aRowY, BCPixelSize aWidth)
{
  // update the left/right first cell border
  if (aRowY == 0) {
    if (mTableIsLTR) {
      mTableBCData->mRightCellBorderWidth = aWidth;
    }
    else {
      mTableBCData->mLeftCellBorderWidth = aWidth;
    }
  }
  mTableBCData->mRightBorderWidth =
              LimitBorderWidth(NS_MAX(mTableBCData->mRightBorderWidth, aWidth));
}

void
BCMapCellInfo::SetRightBorderWidths(BCPixelSize aWidth)
{
   // update the borders of the cells and cols affected
  if (mCell) {
    mCell->SetBorderWidth(mEndSide, NS_MAX(aWidth,
                          mCell->GetBorderWidth(mEndSide)));
  }
  if (mRightCol) {
    BCPixelSize half = BC_BORDER_LEFT_HALF(aWidth);
    mRightCol->SetRightBorderWidth(NS_MAX(nscoord(half),
                                   mRightCol->GetRightBorderWidth()));
  }
}

void
BCMapCellInfo::SetBottomBorderWidths(BCPixelSize aWidth)
{
  // update the borders of the affected cells and rows
  if (mCell) {
    mCell->SetBorderWidth(NS_SIDE_BOTTOM, NS_MAX(aWidth,
                          mCell->GetBorderWidth(NS_SIDE_BOTTOM)));
  }
  if (mBottomRow) {
    BCPixelSize half = BC_BORDER_TOP_HALF(aWidth);
    mBottomRow->SetBottomBCBorderWidth(NS_MAX(nscoord(half),
                                       mBottomRow->GetBottomBCBorderWidth()));
  }
}
void
BCMapCellInfo::SetTopBorderWidths(BCPixelSize aWidth)
{
 if (mCell) {
     mCell->SetBorderWidth(NS_SIDE_TOP, NS_MAX(aWidth,
                           mCell->GetBorderWidth(NS_SIDE_TOP)));
  }
  if (mTopRow) {
    BCPixelSize half = BC_BORDER_BOTTOM_HALF(aWidth);
    mTopRow->SetTopBCBorderWidth(NS_MAX(nscoord(half),
                                        mTopRow->GetTopBCBorderWidth()));
  }
}
void
BCMapCellInfo::SetLeftBorderWidths(BCPixelSize aWidth)
{
  if (mCell) {
    mCell->SetBorderWidth(mStartSide, NS_MAX(aWidth,
                          mCell->GetBorderWidth(mStartSide)));
  }
  if (mLeftCol) {
    BCPixelSize half = BC_BORDER_RIGHT_HALF(aWidth);
    mLeftCol->SetLeftBorderWidth(NS_MAX(nscoord(half),
                                        mLeftCol->GetLeftBorderWidth()));
  }
}

void
BCMapCellInfo::SetTableBottomBorderWidth(BCPixelSize aWidth)
{
  mTableBCData->mBottomBorderWidth =
             LimitBorderWidth(NS_MAX(mTableBCData->mBottomBorderWidth, aWidth));
}

void
BCMapCellInfo::SetColumn(PRInt32 aColX)
{
  mCurrentColFrame = mTableFrame->GetColFrame(aColX);
  if (!mCurrentColFrame) {
    NS_ERROR("null mCurrentColFrame");
  }
  mCurrentColGroupFrame = static_cast<nsTableColGroupFrame*>
                            (mCurrentColFrame->GetParent());
  if (!mCurrentColGroupFrame) {
    NS_ERROR("null mCurrentColGroupFrame");
  }
}

void
BCMapCellInfo::IncrementRow(PRBool aResetToTopRowOfCell)
{
  mCurrentRowFrame = (aResetToTopRowOfCell) ? mTopRow :
                                                mCurrentRowFrame->GetNextRow();
}

BCCellBorder
BCMapCellInfo::GetTopEdgeBorder()
{
  return CompareBorders(mTableFrame, mCurrentColGroupFrame, mCurrentColFrame,
                        mRowGroup, mTopRow, mCell, mTableIsLTR, NS_SIDE_TOP,
                        !ADJACENT);
}

BCCellBorder
BCMapCellInfo::GetBottomEdgeBorder()
{
  return CompareBorders(mTableFrame, mCurrentColGroupFrame, mCurrentColFrame,
                        mRowGroup, mBottomRow, mCell, mTableIsLTR,
                        NS_SIDE_BOTTOM, ADJACENT);
}
BCCellBorder
BCMapCellInfo::GetLeftEdgeBorder()
{
  return CompareBorders(mTableFrame, mColGroup, mLeftCol, mRowGroup,
                        mCurrentRowFrame, mCell, mTableIsLTR, NS_SIDE_LEFT,
                        !ADJACENT);
}
BCCellBorder
BCMapCellInfo::GetRightEdgeBorder()
{
  return CompareBorders(mTableFrame, mColGroup, mRightCol, mRowGroup,
                        mCurrentRowFrame, mCell, mTableIsLTR, NS_SIDE_RIGHT, 
                        ADJACENT);
}
BCCellBorder
BCMapCellInfo::GetRightInternalBorder()
{
  const nsIFrame* cg = (mCgAtRight) ? mColGroup : nsnull;
  return CompareBorders(nsnull, cg, mRightCol, nsnull, nsnull, mCell,
                        mTableIsLTR, NS_SIDE_RIGHT, ADJACENT);
}

BCCellBorder
BCMapCellInfo::GetLeftInternalBorder()
{
  const nsIFrame* cg = (mCgAtLeft) ? mColGroup : nsnull;
  return CompareBorders(nsnull, cg, mLeftCol, nsnull, nsnull, mCell,
                        mTableIsLTR, NS_SIDE_LEFT, !ADJACENT);
}

BCCellBorder
BCMapCellInfo::GetBottomInternalBorder()
{
  const nsIFrame* rg = (mRgAtBottom) ? mRowGroup : nsnull;
  return CompareBorders(nsnull, nsnull, nsnull, rg, mBottomRow, mCell,
                        mTableIsLTR, NS_SIDE_BOTTOM, ADJACENT);
}

BCCellBorder
BCMapCellInfo::GetTopInternalBorder()
{
  const nsIFrame* rg = (mRgAtTop) ? mRowGroup : nsnull;
  return CompareBorders(nsnull, nsnull, nsnull, rg, mTopRow, mCell,
                        mTableIsLTR, NS_SIDE_TOP, !ADJACENT);
}

/* Here is the order for storing border edges in the cell map as a cell is processed. There are 
   n=colspan top and bottom border edges per cell and n=rowspan left and right border edges per cell.

   1) On the top edge of the table, store the top edge. Never store the top edge otherwise, since
      a bottom edge from a cell above will take care of it.
   2) On the left edge of the table, store the left edge. Never store the left edge othewise, since
      a right edge from a cell to the left will take care of it.
   3) Store the right edge (or edges if a row span) 
   4) Store the bottom edge (or edges if a col span)
    
   Since corners are computed with only an array of BCCornerInfo indexed by the number-of-cols, corner
   calculations are somewhat complicated. Using an array with number-of-rows * number-of-col entries
   would simplify this, but at an extra in memory cost of nearly 12 bytes per cell map entry. Collapsing 
   borders already have about an extra 8 byte per cell map entry overhead (this could be
   reduced to 4 bytes if we are willing to not store border widths in nsTableCellFrame), Here are the 
   rules in priority order for storing cornes in the cell map as a cell is processed. top-left means the
   left endpoint of the border edge on the top of the cell. There are n=colspan top and bottom border 
   edges per cell and n=rowspan left and right border edges per cell.

   1) On the top edge of the table, store the top-left corner, unless on the left edge of the table.
      Never store the top-right corner, since it will get stored as a right-top corner.
   2) On the left edge of the table, store the left-top corner. Never store the left-bottom corner,
      since it will get stored as a bottom-left corner.
   3) Store the right-top corner if (a) it is the top right corner of the table or (b) it is not on
      the top edge of the table. Never store the right-bottom corner since it will get stored as a 
      bottom-right corner.
   4) Store the bottom-right corner, if it is the bottom right corner of the table. Never store it 
      otherwise, since it will get stored as either a right-top corner by a cell below or
      a bottom-left corner from a cell to the right.
   5) Store the bottom-left corner, if (a) on the bottom edge of the table or (b) if the left edge hits 
      the top side of a colspan in its interior. Never store the corner otherwise, since it will 
      get stored as a right-top corner by a cell from below.

   XXX the BC-RTL hack - The correct fix would be a rewrite as described in bug 203686.
   In order to draw borders in rtl conditions somehow correct, the existing structure which relies
   heavily on the assumption that the next cell sibling will be on the right side, has been modified.
   We flip the border during painting and during style lookup. Look for tableIsLTR for places where
   the flipping is done.
 */



// Calc the dominant border at every cell edge and corner within the current damage area
void 
nsTableFrame::CalcBCBorders()
{
  NS_ASSERTION(IsBorderCollapse(),
               "calling CalcBCBorders on separated-border table");
  nsTableCellMap* tableCellMap = GetCellMap(); if (!tableCellMap) ABORT0();
  PRInt32 numRows = GetRowCount();
  PRInt32 numCols = GetColCount();
  if (!numRows || !numCols)
    return; // nothing to do

  // Get the property holding the table damage area and border widths
  BCPropertyData* propData =
    (BCPropertyData*)nsTableFrame::GetProperty(this, nsGkAtoms::tableBCProperty,
                                               PR_FALSE);
  if (!propData) ABORT0();

  
  
  CheckFixDamageArea(numRows, numCols, propData->mDamageArea);
  // calculate an expanded damage area 
  nsRect damageArea(propData->mDamageArea);
  ExpandBCDamageArea(damageArea);

  // segments that are on the table border edges need
  // to be initialized only once
  PRBool tableBorderReset[4];
  for (PRUint32 sideX = NS_SIDE_TOP; sideX <= NS_SIDE_LEFT; sideX++) {
    tableBorderReset[sideX] = PR_FALSE;
  }

  // vertical borders indexed in x-direction (cols)
  BCCellBorders lastVerBorders(damageArea.width + 1, damageArea.x);
  if (!lastVerBorders.borders) ABORT0();
  BCCellBorder  lastTopBorder, lastBottomBorder;
  // horizontal borders indexed in x-direction (cols)
  BCCellBorders lastBottomBorders(damageArea.width + 1, damageArea.x);
  if (!lastBottomBorders.borders) ABORT0();
  PRBool startSeg;
  PRBool gotRowBorder = PR_FALSE;

  BCMapCellInfo  info(this), ajaInfo(this);

  BCCellBorder currentBorder, adjacentBorder;
  BCCorners topCorners(damageArea.width + 1, damageArea.x);
  if (!topCorners.corners) ABORT0();
  BCCorners bottomCorners(damageArea.width + 1, damageArea.x);
  if (!bottomCorners.corners) ABORT0();

  BCMapCellIterator iter(this, damageArea);
  for (iter.First(info); !iter.mAtEnd; iter.Next(info)) {
    PRBool bottomRowSpan = PR_FALSE;
    // see if lastTopBorder, lastBottomBorder need to be reset
    if (iter.IsNewRow()) { 
      gotRowBorder = PR_FALSE;
      lastTopBorder.Reset(info.mRowIndex, info.mRowSpan);
      lastBottomBorder.Reset(info.GetCellEndRowIndex() + 1, info.mRowSpan);
    }
    else if (info.mColIndex > damageArea.x) {
      lastBottomBorder = lastBottomBorders[info.mColIndex - 1];
      if (info.mRowIndex >
          (lastBottomBorder.rowIndex - lastBottomBorder.rowSpan)) {
        // the top border's left edge butts against the middle of a rowspan
        lastTopBorder.Reset(info.mRowIndex, info.mRowSpan);
      }
      if (lastBottomBorder.rowIndex > (info.GetCellEndRowIndex() + 1)) {
        // the bottom border's left edge butts against the middle of a rowspan
        lastBottomBorder.Reset(info.GetCellEndRowIndex() + 1, info.mRowSpan);
        bottomRowSpan = PR_TRUE;
      }
    }

    // find the dominant border considering the cell's top border and the table,
    // row group, row if the border is at the top of the table, otherwise it was
    // processed in a previous row
    if (0 == info.mRowIndex) {
      if (!tableBorderReset[NS_SIDE_TOP]) {
        propData->mTopBorderWidth = 0;
        tableBorderReset[NS_SIDE_TOP] = PR_TRUE;
      }
      for (PRInt32 colX = info.mColIndex; colX <= info.GetCellEndColIndex();
           colX++) {
        info.SetColumn(colX);
        currentBorder = info.GetTopEdgeBorder();
        // update/store the top left & top right corners of the seg 
        BCCornerInfo& tlCorner = topCorners[colX]; // top left
        if (0 == colX) {
          // we are on right hand side of the corner
          tlCorner.Set(NS_SIDE_RIGHT, currentBorder);
        }
        else {
          tlCorner.Update(NS_SIDE_RIGHT, currentBorder);
          tableCellMap->SetBCBorderCorner(eTopLeft, *iter.mCellMap, 0, 0, colX,
                                          tlCorner.ownerSide, tlCorner.subWidth,
                                          tlCorner.bevel);
        }
        topCorners[colX + 1].Set(NS_SIDE_LEFT, currentBorder); // top right
        // update lastTopBorder and see if a new segment starts
        startSeg = SetHorBorder(currentBorder, tlCorner, lastTopBorder);
        // store the border segment in the cell map
        tableCellMap->SetBCBorderEdge(NS_SIDE_TOP, *iter.mCellMap, 0, 0, colX,
                                      1, currentBorder.owner,
                                      currentBorder.width, startSeg);
       
        info.SetTableTopBorderWidth(currentBorder.width);
        info.SetTopBorderWidths(currentBorder.width);
        info.SetColumnTopRightContBCBorder();
      }
      info.SetTableTopLeftContBCBorder();
    }
    else {
      // see if the top border needs to be the start of a segment due to a
      // vertical border owning the corner
      if (info.mColIndex > 0) {
        BCData& data = info.mCellData->mData;
        if (!data.IsTopStart()) {
          PRUint8 cornerSide;
          PRPackedBool bevel;
          data.GetCorner(cornerSide, bevel);
          if ((NS_SIDE_TOP == cornerSide) || (NS_SIDE_BOTTOM == cornerSide)) {
            data.SetTopStart(PR_TRUE);
          }
        }
      }  
    }

    // find the dominant border considering the cell's left border and the
    // table, col group, col if the border is at the left of the table,
    // otherwise it was processed in a previous col
    if (0 == info.mColIndex) {
      if (!tableBorderReset[NS_SIDE_LEFT]) {
        propData->mLeftBorderWidth = 0;
        tableBorderReset[NS_SIDE_LEFT] = PR_TRUE;
      }
      info.mCurrentRowFrame = nsnull;
      for (PRInt32 rowY = info.mRowIndex; rowY <= info.GetCellEndRowIndex();
           rowY++) {
        info.IncrementRow(rowY == info.mRowIndex);
        currentBorder = info.GetLeftEdgeBorder();
        BCCornerInfo& tlCorner = (0 == rowY) ? topCorners[0] : bottomCorners[0];
        tlCorner.Update(NS_SIDE_BOTTOM, currentBorder);
        tableCellMap->SetBCBorderCorner(eTopLeft, *iter.mCellMap,
                                        iter.mRowGroupStart, rowY, 0,
                                        tlCorner.ownerSide, tlCorner.subWidth,
                                        tlCorner.bevel);
        bottomCorners[0].Set(NS_SIDE_TOP, currentBorder); // bottom left
       
        // update lastVerBordersBorder and see if a new segment starts
        startSeg = SetBorder(currentBorder, lastVerBorders[0]);
        // store the border segment in the cell map 
        tableCellMap->SetBCBorderEdge(NS_SIDE_LEFT, *iter.mCellMap,
                                      iter.mRowGroupStart, rowY, info.mColIndex,
                                      1, currentBorder.owner,
                                      currentBorder.width, startSeg);
        info.SetTableLeftBorderWidth(rowY , currentBorder.width);
        info.SetLeftBorderWidths(currentBorder.width);
        info.SetRowLeftContBCBorder();
      }
      info.SetRowGroupLeftContBCBorder();
    }

    // find the dominant border considering the cell's right border, adjacent
    // cells and the table, row group, row
    if (info.mNumTableCols == info.GetCellEndColIndex() + 1) {
      // touches right edge of table
      if (!tableBorderReset[NS_SIDE_RIGHT]) {
        propData->mRightBorderWidth = 0;
        tableBorderReset[NS_SIDE_RIGHT] = PR_TRUE;
      }
      info.mCurrentRowFrame = nsnull;
      for (PRInt32 rowY = info.mRowIndex; rowY <= info.GetCellEndRowIndex();
           rowY++) {
        info.IncrementRow(rowY == info.mRowIndex);
        currentBorder = info.GetRightEdgeBorder();
        // update/store the top right & bottom right corners 
        BCCornerInfo& trCorner = (0 == rowY) ?
                                 topCorners[info.GetCellEndColIndex() + 1] :
                                 bottomCorners[info.GetCellEndColIndex() + 1];
        trCorner.Update(NS_SIDE_BOTTOM, currentBorder);   // top right
        tableCellMap->SetBCBorderCorner(eTopRight, *iter.mCellMap,
                                        iter.mRowGroupStart, rowY,
                                        info.GetCellEndColIndex(),
                                        trCorner.ownerSide, trCorner.subWidth,
                                        trCorner.bevel);
        BCCornerInfo& brCorner = bottomCorners[info.GetCellEndColIndex() + 1];
        brCorner.Set(NS_SIDE_TOP, currentBorder); // bottom right
        tableCellMap->SetBCBorderCorner(eBottomRight, *iter.mCellMap,
                                        iter.mRowGroupStart, rowY,
                                        info.GetCellEndColIndex(),
                                        brCorner.ownerSide, brCorner.subWidth,
                                        brCorner.bevel);
        // update lastVerBorders and see if a new segment starts
        startSeg = SetBorder(currentBorder,
                             lastVerBorders[info.GetCellEndColIndex() + 1]);
        // store the border segment in the cell map and update cellBorders
        tableCellMap->SetBCBorderEdge(NS_SIDE_RIGHT, *iter.mCellMap,
                                      iter.mRowGroupStart, rowY,
                                      info.GetCellEndColIndex(), 1,
                                      currentBorder.owner, currentBorder.width,
                                      startSeg);
        info.SetTableRightBorderWidth(rowY, currentBorder.width);
        info.SetRightBorderWidths(currentBorder.width);
        info.SetRowRightContBCBorder();
      }
      info.SetRowGroupRightContBCBorder();
    }
    else {
      PRInt32 segLength = 0;
      BCMapCellInfo priorAjaInfo(this);
      for (PRInt32 rowY = info.mRowIndex; rowY <= info.GetCellEndRowIndex();
           rowY += segLength) {
        iter.PeekRight(info, rowY, ajaInfo);
        currentBorder  = info.GetRightInternalBorder();
        adjacentBorder = ajaInfo.GetLeftInternalBorder();
        currentBorder = CompareBorders(!CELL_CORNER, currentBorder,
                                        adjacentBorder, !HORIZONTAL);
                          
        segLength = NS_MAX(1, ajaInfo.mRowIndex + ajaInfo.mRowSpan - rowY);
        segLength = NS_MIN(segLength, info.mRowIndex + info.mRowSpan - rowY);

        // update lastVerBorders and see if a new segment starts
        startSeg = SetBorder(currentBorder,
                             lastVerBorders[info.GetCellEndColIndex() + 1]);
        // store the border segment in the cell map and update cellBorders
        if (info.GetCellEndColIndex() < damageArea.XMost() &&
            rowY >= damageArea.y && rowY < damageArea.YMost()) {
          tableCellMap->SetBCBorderEdge(NS_SIDE_RIGHT, *iter.mCellMap,
                                        iter.mRowGroupStart, rowY,
                                        info.GetCellEndColIndex(), segLength,
                                        currentBorder.owner,
                                        currentBorder.width, startSeg);
          info.SetRightBorderWidths(currentBorder.width);
          ajaInfo.SetLeftBorderWidths(currentBorder.width);
        }
        // update the top right corner
        PRBool hitsSpanOnRight = (rowY > ajaInfo.mRowIndex) &&
                                  (rowY < ajaInfo.mRowIndex + ajaInfo.mRowSpan);
        BCCornerInfo* trCorner = ((0 == rowY) || hitsSpanOnRight) ?
                                 &topCorners[info.GetCellEndColIndex() + 1] :
                                 &bottomCorners[info.GetCellEndColIndex() + 1];
        trCorner->Update(NS_SIDE_BOTTOM, currentBorder);
        // if this is not the first time through,
        // consider the segment to the right
        if (rowY != info.mRowIndex) {
          currentBorder  = priorAjaInfo.GetBottomInternalBorder();
          adjacentBorder = ajaInfo.GetTopInternalBorder();
          currentBorder = CompareBorders(!CELL_CORNER, currentBorder,
                                          adjacentBorder, HORIZONTAL);
          trCorner->Update(NS_SIDE_RIGHT, currentBorder);
        }
        // store the top right corner in the cell map 
        if (info.GetCellEndColIndex() < damageArea.XMost() &&
            rowY >= damageArea.y) {
          if (0 != rowY) {
            tableCellMap->SetBCBorderCorner(eTopRight, *iter.mCellMap,
                                            iter.mRowGroupStart, rowY,
                                            info.GetCellEndColIndex(),
                                            trCorner->ownerSide,
                                            trCorner->subWidth,
                                            trCorner->bevel);
          }
          // store any corners this cell spans together with the aja cell
          for (PRInt32 rX = rowY + 1; rX < rowY + segLength; rX++) {
            tableCellMap->SetBCBorderCorner(eBottomRight, *iter.mCellMap,
                                            iter.mRowGroupStart, rX,
                                            info.GetCellEndColIndex(),
                                            trCorner->ownerSide,
                                            trCorner->subWidth, PR_FALSE);
          }
        }
        // update bottom right corner, topCorners, bottomCorners
        hitsSpanOnRight = (rowY + segLength <
                           ajaInfo.mRowIndex + ajaInfo.mRowSpan);
        BCCornerInfo& brCorner = (hitsSpanOnRight) ?
                                 topCorners[info.GetCellEndColIndex() + 1] :
                                 bottomCorners[info.GetCellEndColIndex() + 1];
        brCorner.Set(NS_SIDE_TOP, currentBorder);
        priorAjaInfo = ajaInfo;
      }
    }
    for (PRInt32 colX = info.mColIndex + 1; colX <= info.GetCellEndColIndex();
         colX++) {
      lastVerBorders[colX].Reset(0,1);
    }

    // find the dominant border considering the cell's bottom border, adjacent
    // cells and the table, row group, row
    if (info.mNumTableRows == info.GetCellEndRowIndex() + 1) {
      // touches bottom edge of table
      if (!tableBorderReset[NS_SIDE_BOTTOM]) {
        propData->mBottomBorderWidth = 0;
        tableBorderReset[NS_SIDE_BOTTOM] = PR_TRUE;
      }
      for (PRInt32 colX = info.mColIndex; colX <= info.GetCellEndColIndex();
           colX++) {
        info.SetColumn(colX);
        currentBorder = info.GetBottomEdgeBorder();
        // update/store the bottom left & bottom right corners 
        BCCornerInfo& blCorner = bottomCorners[colX]; // bottom left
        blCorner.Update(NS_SIDE_RIGHT, currentBorder);
        tableCellMap->SetBCBorderCorner(eBottomLeft, *iter.mCellMap,
                                        iter.mRowGroupStart,
                                        info.GetCellEndRowIndex(),
                                        colX, blCorner.ownerSide,
                                        blCorner.subWidth, blCorner.bevel);
        BCCornerInfo& brCorner = bottomCorners[colX + 1]; // bottom right
        brCorner.Update(NS_SIDE_LEFT, currentBorder);
        if (info.mNumTableCols == colX + 1) { // lower right corner of the table
          tableCellMap->SetBCBorderCorner(eBottomRight, *iter.mCellMap,
                                          iter.mRowGroupStart,
                                          info.GetCellEndRowIndex(),colX,
                                          brCorner.ownerSide, brCorner.subWidth,
                                          brCorner.bevel, PR_TRUE);
        }
        // update lastBottomBorder and see if a new segment starts
        startSeg = SetHorBorder(currentBorder, blCorner, lastBottomBorder);
        if (!startSeg) { 
           // make sure that we did not compare apples to oranges i.e. the
           // current border should be a continuation of the lastBottomBorder,
           // as it is a bottom border
           // add 1 to the info.GetCellEndRowIndex()
           startSeg = (lastBottomBorder.rowIndex !=
                       (info.GetCellEndRowIndex() + 1));
        }
        // store the border segment in the cell map and update cellBorders
        tableCellMap->SetBCBorderEdge(NS_SIDE_BOTTOM, *iter.mCellMap,
                                      iter.mRowGroupStart,
                                      info.GetCellEndRowIndex(),
                                      colX, 1, currentBorder.owner,
                                      currentBorder.width, startSeg);
        // update lastBottomBorders
        lastBottomBorder.rowIndex = info.GetCellEndRowIndex() + 1;
        lastBottomBorder.rowSpan = info.mRowSpan;
        lastBottomBorders[colX] = lastBottomBorder;
        
        info.SetBottomBorderWidths(currentBorder.width);
        info.SetTableBottomBorderWidth(currentBorder.width);
        info.SetColumnBottomContBCBorder();
      }
      info.SetRowGroupBottomContBCBorder();
      info.SetColGroupBottomContBCBorder();
    }
    else {
      PRInt32 segLength = 0;
      for (PRInt32 colX = info.mColIndex; colX <= info.GetCellEndColIndex();
           colX += segLength) {
        iter.PeekBottom(info, colX, ajaInfo);
        currentBorder  = info.GetBottomInternalBorder();
        adjacentBorder = ajaInfo.GetTopInternalBorder();
        currentBorder = CompareBorders(!CELL_CORNER, currentBorder,
                                        adjacentBorder, HORIZONTAL);
        segLength = NS_MAX(1, ajaInfo.mColIndex + ajaInfo.mColSpan - colX);
        segLength = NS_MIN(segLength, info.mColIndex + info.mColSpan - colX);

        // update, store the bottom left corner
        BCCornerInfo& blCorner = bottomCorners[colX]; // bottom left
        PRBool hitsSpanBelow = (colX > ajaInfo.mColIndex) &&
                               (colX < ajaInfo.mColIndex + ajaInfo.mColSpan);
        PRBool update = PR_TRUE;
        if ((colX == info.mColIndex) && (colX > damageArea.x)) {
          PRInt32 prevRowIndex = lastBottomBorders[colX - 1].rowIndex;
          if (prevRowIndex > info.GetCellEndRowIndex() + 1) {
            // hits a rowspan on the right
            update = PR_FALSE;
            // the corner was taken care of during the cell on the left
          }
          else if (prevRowIndex < info.GetCellEndRowIndex() + 1) {
            // spans below the cell to the left
            topCorners[colX] = blCorner;
            blCorner.Set(NS_SIDE_RIGHT, currentBorder);
            update = PR_FALSE;
          }
        }
        if (update) {
          blCorner.Update(NS_SIDE_RIGHT, currentBorder);
        }
        if (info.GetCellEndRowIndex() < damageArea.YMost() &&
            (colX >= damageArea.x)) {
          if (hitsSpanBelow) {
            tableCellMap->SetBCBorderCorner(eBottomLeft, *iter.mCellMap,
                                            iter.mRowGroupStart,
                                            info.GetCellEndRowIndex(), colX,
                                            blCorner.ownerSide,
                                            blCorner.subWidth, blCorner.bevel);
          }
          // store any corners this cell spans together with the aja cell
          for (PRInt32 cX = colX + 1; cX < colX + segLength; cX++) {
            BCCornerInfo& corner = bottomCorners[cX];
            corner.Set(NS_SIDE_RIGHT, currentBorder);
            tableCellMap->SetBCBorderCorner(eBottomLeft, *iter.mCellMap,
                                            iter.mRowGroupStart,
                                            info.GetCellEndRowIndex(), cX,
                                            corner.ownerSide, corner.subWidth,
                                            PR_FALSE);
          }
        }
        // update lastBottomBorders and see if a new segment starts
        startSeg = SetHorBorder(currentBorder, blCorner, lastBottomBorder);
        if (!startSeg) { 
           // make sure that we did not compare apples to oranges i.e. the
           // current border should be a continuation of the lastBottomBorder,
           // as it is a bottom border
           // add 1 to the info.GetCellEndRowIndex()
           startSeg = (lastBottomBorder.rowIndex !=
                       info.GetCellEndRowIndex() + 1);
        }
        lastBottomBorder.rowIndex = info.GetCellEndRowIndex() + 1;
        lastBottomBorder.rowSpan = info.mRowSpan;
        for (PRInt32 cX = colX; cX < colX + segLength; cX++) {
          lastBottomBorders[cX] = lastBottomBorder;
        }

        // store the border segment the cell map and update cellBorders
        if (info.GetCellEndRowIndex() < damageArea.YMost() &&
            (colX >= damageArea.x) &&
            (colX < damageArea.XMost())) {
          tableCellMap->SetBCBorderEdge(NS_SIDE_BOTTOM, *iter.mCellMap,
                                        iter.mRowGroupStart,
                                        info.GetCellEndRowIndex(),
                                        colX, segLength, currentBorder.owner,
                                        currentBorder.width, startSeg);
          info.SetBottomBorderWidths(currentBorder.width);
          ajaInfo.SetTopBorderWidths(currentBorder.width);
        }
        // update bottom right corner
        BCCornerInfo& brCorner = bottomCorners[colX + segLength];
        brCorner.Update(NS_SIDE_LEFT, currentBorder);
      }
      if (!gotRowBorder && 1 == info.mRowSpan &&
          (ajaInfo.mTopRow || info.mRgAtBottom)) {
        //get continuous row/row group border
        //we need to check the row group's bottom border if this is
        //the last row in the row group, but only a cell with rowspan=1
        //will know whether *this* row is at the bottom
        const nsIFrame* nextRowGroup = (ajaInfo.mRgAtTop) ? ajaInfo.mRowGroup :
                                                             nsnull;
        info.SetInnerRowGroupBottomContBCBorder(nextRowGroup, ajaInfo.mTopRow);
        gotRowBorder = PR_TRUE;
      }
    }

    // see if the cell to the right had a rowspan and its lower left border
    // needs be joined with this one's bottom
    // if  there is a cell to the right and the cell to right was a rowspan
    if ((info.mNumTableCols != info.GetCellEndColIndex() + 1) &&
        (lastBottomBorders[info.GetCellEndColIndex() + 1].rowSpan > 1)) {
      BCCornerInfo& corner = bottomCorners[info.GetCellEndColIndex() + 1];
      if ((NS_SIDE_TOP != corner.ownerSide) &&
          (NS_SIDE_BOTTOM != corner.ownerSide)) {
        // not a vertical owner
        BCCellBorder& thisBorder = lastBottomBorder;
        BCCellBorder& nextBorder = lastBottomBorders[info.mColIndex + 1];
        if ((thisBorder.color == nextBorder.color) &&
            (thisBorder.width == nextBorder.width) &&
            (thisBorder.style == nextBorder.style)) {
          // set the flag on the next border indicating it is not the start of a
          // new segment
          if (iter.mCellMap) {
            tableCellMap->SetNotTopStart(NS_SIDE_BOTTOM, *iter.mCellMap,
                                         info.GetCellEndRowIndex(),
                                         info.GetCellEndColIndex() + 1);
          }
        }
      }
    }
  } // for (iter.First(info); info.mCell; iter.Next(info)) {
  // reset the bc flag and damage area
  SetNeedToCalcBCBorders(PR_FALSE);
  propData->mDamageArea.x = propData->mDamageArea.y = propData->mDamageArea.width = propData->mDamageArea.height = 0;
#ifdef DEBUG_TABLE_CELLMAP
  mCellMap->Dump();
#endif
}

class BCPaintBorderIterator;

struct BCVerticalSeg
{
  BCVerticalSeg();
 
  void Start(BCPaintBorderIterator& aIter,
             BCBorderOwner          aBorderOwner,
             BCPixelSize            aVerSegWidth,
             BCPixelSize            aHorSegHeight);

  void Initialize(BCPaintBorderIterator& aIter);
  void GetBottomCorner(BCPaintBorderIterator& aIter,
                       BCPixelSize            aHorSegHeight);


   void Paint(BCPaintBorderIterator& aIter,
              nsIRenderingContext&   aRenderingContext,
              BCPixelSize            aHorSegHeight);
  void AdvanceOffsetY();
  void IncludeCurrentBorder(BCPaintBorderIterator& aIter);
         
  
  union {
    nsTableColFrame*  mCol;
    PRInt32           mColWidth;
  };
  nscoord               mOffsetX;    // x-offset with respect to the table edge
  nscoord               mOffsetY;    // y-offset with respect to the table edge
  nscoord               mLength;     // vertical length including corners
  BCPixelSize           mWidth;      // width in pixels

  nsTableCellFrame*     mAjaCell;       // previous sibling to the first cell
                                        // where the segment starts, it can be
                                        // the owner of a segment
  nsTableCellFrame*     mFirstCell;     // cell at the start of the segment
  nsTableRowGroupFrame* mFirstRowGroup; // row group at the start of the segment
  nsTableRowFrame*      mFirstRow;      // row at the start of the segment
  nsTableCellFrame*     mLastCell;      // cell at the current end of the
                                        // segment


  PRUint8               mOwner;         // owner of the border, defines the
                                        // style
  PRUint8               mTopBevelSide;  // direction to bevel at the top
  nscoord               mTopBevelOffset; // how much to bevel at the top
  BCPixelSize           mBottomHorSegHeight; // height of the crossing
                                        //horizontal border
  nscoord               mBottomOffset;  // how much longer is the segment due
                                        // to the horizontal border, by this
                                        // amount the next segment needs to be
                                        // shifted.
  PRBool                mIsBottomBevel; // should we bevel at the bottom
};

struct BCHorizontalSeg
{
  BCHorizontalSeg();

  void Start(BCPaintBorderIterator& aIter,
             BCBorderOwner          aBorderOwner,
             BCPixelSize            aBottomVerSegWidth,
             BCPixelSize            aHorSegHeight);
   void GetRightCorner(BCPaintBorderIterator& aIter,
                       BCPixelSize            aLeftSegWidth);
   void AdvanceOffsetX(PRInt32 aIncrement);
   void IncludeCurrentBorder(BCPaintBorderIterator& aIter);
   void Paint(BCPaintBorderIterator& aIter,
              nsIRenderingContext&   aRenderingContext);
  
  nscoord            mOffsetX;       // x-offset with respect to the table edge
  nscoord            mOffsetY;       // y-offset with respect to the table edge
  nscoord            mLength;        // horizontal length including corners
  BCPixelSize        mWidth;         // border width in pixels
  nscoord            mLeftBevelOffset;   // how much to bevel at the left
  PRUint8            mLeftBevelSide;     // direction to bevel at the left
  PRBool             mIsRightBevel;        // should we bevel at the right end
  nscoord            mRightBevelOffset;  // how much to bevel at the right
  PRUint8            mRightBevelSide;    // direction to bevel at the right
  nscoord            mEndOffset;         // how much longer is the segment due
                                         // to the vertical border, by this
                                         // amount the next segment needs to be
                                         // shifted.
  PRUint8            mOwner;             // owner of the border, defines the
                                         // style
  nsTableCellFrame*  mFirstCell;         // cell at the start of the segment
  nsTableCellFrame*  mAjaCell;           // neighboring cell to the first cell
                                         // where the segment starts, it can be
                                         // the owner of a segment
};

// Iterates over borders (left border, corner, top border) in the cell map within a damage area
// from left to right, top to bottom. All members are in terms of the 1st in flow frames, except 
// where suffixed by InFlow.  
class BCPaintBorderIterator
{
public:


  BCPaintBorderIterator(nsTableFrame* aTable);
  ~BCPaintBorderIterator() { if (mVerInfo) {
                              delete [] mVerInfo;
                           }}
  void Reset();

  PRBool SetDamageArea(nsRect aDirtyRect);
  void First();
  void Next();
  void AccumulateOrPaintHorizontalSegment(nsIRenderingContext& aRenderingContext);
  void AccumulateOrPaintVerticalSegment(nsIRenderingContext& aRenderingContext);
  void ResetVerInfo();
  void StoreColumnWidth(PRInt32 aIndex);
  PRBool VerticalSegmentOwnsCorner();

  nsTableFrame*         mTable;
  nsTableFrame*         mTableFirstInFlow;
  nsTableCellMap*       mTableCellMap;
  nsCellMap*            mCellMap;
  PRBool                mTableIsLTR;
  PRInt32               mColInc;            // +1 for ltr -1 for rtl
  const nsStyleBackground* mTableBgColor;
  nsTableFrame::RowGroupArray mRowGroups;

  nsTableRowGroupFrame* mPrevRg;
  nsTableRowGroupFrame* mRg;
  PRBool                mIsRepeatedHeader;
  PRBool                mIsRepeatedFooter;
  nsTableRowGroupFrame* mStartRg; // first row group in the damagearea
  PRInt32               mRgIndex; // current row group index in the
                                        // mRowgroups array
  PRInt32               mFifRgFirstRowIndex; // start row index of the first in
                                           // flow of the row group
  PRInt32               mRgFirstRowIndex; // row index of the first row in the
                                          // row group
  PRInt32               mRgLastRowIndex; // row index of the last row in the row
                                         // group
  PRInt32               mNumTableRows;   // number of rows in the table and all
                                         // continuations
  PRInt32               mNumTableCols;   // number of columns in the table
  PRInt32               mColIndex;       // with respect to the table
  PRInt32               mRowIndex;       // with respect to the table
  PRInt32               mRepeatedHeaderRowIndex; // row index in a repeated
                                            //header, it's equivalent to
                                            // mRowIndex when we're in a repeated
                                            // header, and set to the last row
                                            // index of a repeated header when
                                            // we're not
  PRBool                mIsNewRow;
  PRBool                mAtEnd;             // the iterator cycled over all
                                             // borders
  nsTableRowFrame*      mPrevRow;
  nsTableRowFrame*      mRow;
  nsTableRowFrame*      mStartRow;    //first row in a inside the damagearea


  // cell properties
  nsTableCellFrame*     mPrevCell;
  nsTableCellFrame*     mCell;
  BCCellData*           mPrevCellData;
  BCCellData*           mCellData;
  BCData*               mBCData;

  PRBool                IsTableTopMost()    {return (mRowIndex == 0) && !mTable->GetPrevInFlow();}
  PRBool                IsTableRightMost()  {return (mColIndex >= mNumTableCols);}
  PRBool                IsTableBottomMost() {return (mRowIndex >= mNumTableRows) && !mTable->GetNextInFlow();}
  PRBool                IsTableLeftMost()   {return (mColIndex == 0);}
  PRBool                IsDamageAreaTopMost()    {return (mRowIndex == mDamageArea.y);}
  PRBool                IsDamageAreaRightMost()  {return (mColIndex >= mDamageArea.XMost());}
  PRBool                IsDamageAreaBottomMost() {return (mRowIndex >= mDamageArea.YMost());}
  PRBool                IsDamageAreaLeftMost()   {return (mColIndex == mDamageArea.x);}
  PRInt32               GetRelativeColIndex() {return (mColIndex - mDamageArea.x);}

  nsRect                mDamageArea;        // damageArea in cellmap coordinates
  PRBool                IsAfterRepeatedHeader() { return !mIsRepeatedHeader && (mRowIndex == (mRepeatedHeaderRowIndex + 1));}
  PRBool                StartRepeatedFooter() {return mIsRepeatedFooter && (mRowIndex == mRgFirstRowIndex) && (mRowIndex != mDamageArea.y);}
  nscoord               mInitialOffsetX;  // offsetX of the first border with
                                            // respect to the table
  nscoord               mInitialOffsetY;    // offsetY of the first border with
                                            // respect to the table
  nscoord               mNextOffsetY;       // offsetY of the next segment
  BCVerticalSeg*        mVerInfo; // this array is used differently when
                                  // horizontal and vertical borders are drawn
                                  // When horizontal border are drawn we cache
                                  // the column widths and the width of the
                                  // vertical borders that arrive from top
                                  // When we draw vertical borders we store
                                  // lengths and width for vertical borders
                                  // before they are drawn while we  move over
                                  // the columns in the damage area
                                  // It has one more elements than columns are
                                  //in the table.
  BCHorizontalSeg       mHorSeg;            // the horizontal segment while we
                                            // move over the colums
  BCPixelSize           mPrevHorSegHeight;  // the height of the previous
                                            // horizontal border

private:

  PRBool SetNewRow(nsTableRowFrame* aRow = nsnull);
  PRBool SetNewRowGroup();
  void   SetNewData(PRInt32 aRowIndex, PRInt32 aColIndex);

};



BCPaintBorderIterator::BCPaintBorderIterator(nsTableFrame* aTable)
{
  mTable      = aTable;
  mVerInfo    = nsnull;
  nsMargin childAreaOffset = mTable->GetChildAreaOffset(nsnull);
  mTableFirstInFlow    = (nsTableFrame*) mTable->GetFirstInFlow();
  mTableCellMap        = mTable->GetCellMap();
  // y position of first row in damage area
  mInitialOffsetY = (mTable->GetPrevInFlow()) ? 0 : childAreaOffset.top;
  mNumTableRows  = mTable->GetRowCount();
  mNumTableCols  = mTable->GetColCount();

  // Get the ordered row groups
  mTable->OrderRowGroups(mRowGroups);
  // initialize to a non existing index
  mRepeatedHeaderRowIndex = -99;
  
  mTableIsLTR = mTable->GetStyleVisibility()->mDirection ==
                   NS_STYLE_DIRECTION_LTR;
  mColInc = (mTableIsLTR) ? 1 : -1;

  nsStyleContext* bgContext =
    nsCSSRendering::FindNonTransparentBackground(mTable->GetStyleContext());
  mTableBgColor = bgContext->GetStyleBackground();
}

/**
 * determine the damage area in terms of rows and columns and finalize
   mInitialOffsetY and mInitialOffsetY
   @param aDirtyRect - dirty rect in table coordinates
   @return - do we need to paint at all
 */
PRBool
BCPaintBorderIterator::SetDamageArea(nsRect aDirtyRect)
{

  PRUint32 startRowIndex, endRowIndex, startColIndex, endColIndex;
  startRowIndex = endRowIndex = startColIndex = endColIndex = 0;
  PRBool done = PR_FALSE;
  PRBool haveIntersect = PR_FALSE;
  // find startRowIndex, endRowIndex, startRowY
  PRInt32 rowY = mInitialOffsetY;
  for (PRUint32 rgX = 0; rgX < mRowGroups.Length() && !done; rgX++) {
    nsTableRowGroupFrame* rgFrame = mRowGroups[rgX];
    for (nsTableRowFrame* rowFrame = rgFrame->GetFirstRow(); rowFrame;
         rowFrame = rowFrame->GetNextRow()) {
      // conservatively estimate the half border widths outside the row
      nscoord topBorderHalf    = (mTable->GetPrevInFlow()) ? 0 :
       nsPresContext::CSSPixelsToAppUnits(rowFrame->GetTopBCBorderWidth() + 1);
      nscoord bottomBorderHalf = (mTable->GetNextInFlow()) ? 0 :
        nsPresContext::CSSPixelsToAppUnits(rowFrame->GetBottomBCBorderWidth() + 1);
      // get the row rect relative to the table rather than the row group
      nsSize rowSize = rowFrame->GetSize();
      if (haveIntersect) {
        if (aDirtyRect.YMost() >= (rowY - topBorderHalf)) {
          nsTableRowFrame* fifRow =
            (nsTableRowFrame*)rowFrame->GetFirstInFlow();
          if (!fifRow) ABORT1(PR_FALSE);
          endRowIndex = fifRow->GetRowIndex();
        }
        else done = PR_TRUE;
      }
      else {
        if ((rowY + rowSize.height + bottomBorderHalf) >= aDirtyRect.y) {
          mStartRg  = rgFrame;
          mStartRow = rowFrame;
          nsTableRowFrame* fifRow =
            (nsTableRowFrame*)rowFrame->GetFirstInFlow();
          if (!fifRow) ABORT1(PR_FALSE);
          startRowIndex = endRowIndex = fifRow->GetRowIndex();
          haveIntersect = PR_TRUE;
        }
        else {
          mInitialOffsetY += rowSize.height;
        }
      }
      rowY += rowSize.height;
    }
  }
  mNextOffsetY = mInitialOffsetY;

  // XXX comment refers to the obsolete NS_FRAME_OUTSIDE_CHILDREN flag
  // XXX but I don't understand it, so not changing it for now
  // outer table borders overflow the table, so the table might be
  // target to other areas as the NS_FRAME_OUTSIDE_CHILDREN is set
  // on the table
  if (!haveIntersect)
    return PR_FALSE;
  // find startColIndex, endColIndex, startColX
  haveIntersect = PR_FALSE;
  if (0 == mNumTableCols)
    return PR_FALSE;
  PRInt32 leftCol, rightCol; // columns are in the range [leftCol, rightCol)

  nsMargin childAreaOffset = mTable->GetChildAreaOffset(nsnull);
  if (mTableIsLTR) {
    mInitialOffsetX = childAreaOffset.left; // x position of first col in
                                            // damage area
    leftCol = 0;
    rightCol = mNumTableCols;
  } else {
    // x position of first col in damage area
    mInitialOffsetX = mTable->GetRect().width - childAreaOffset.right;
    leftCol = mNumTableCols-1;
    rightCol = -1;
  }
  nscoord x = 0;
  PRInt32 colX;
  for (colX = leftCol; colX != rightCol; colX += mColInc) {
    nsTableColFrame* colFrame = mTableFirstInFlow->GetColFrame(colX);
    if (!colFrame) ABORT1(PR_FALSE);
    // conservatively estimate the half border widths outside the col
    nscoord leftBorderHalf =
       nsPresContext::CSSPixelsToAppUnits(colFrame->GetLeftBorderWidth() + 1);
    nscoord rightBorderHalf =
      nsPresContext::CSSPixelsToAppUnits(colFrame->GetRightBorderWidth() + 1);
    // get the col rect relative to the table rather than the col group
    nsSize size = colFrame->GetSize();
    if (haveIntersect) {
      if (aDirtyRect.XMost() >= (x - leftBorderHalf)) {
        endColIndex = colX;
      }
      else break;
    }
    else {
      if ((x + size.width + rightBorderHalf) >= aDirtyRect.x) {
        startColIndex = endColIndex = colX;
        haveIntersect = PR_TRUE;
      }
      else {
        mInitialOffsetX += mColInc * size.width;
      }
    }
    x += size.width;
  }
  if (!mTableIsLTR) {
    PRUint32 temp;
    mInitialOffsetX = mTable->GetRect().width - childAreaOffset.right;
    temp = startColIndex; startColIndex = endColIndex; endColIndex = temp;
    for (PRUint32 column = 0; column < startColIndex; column++) {
      nsTableColFrame* colFrame = mTableFirstInFlow->GetColFrame(column);
      if (!colFrame) ABORT1(PR_FALSE);
      nsSize size = colFrame->GetSize();
      mInitialOffsetX += mColInc * size.width;
    }
  }
  if (!haveIntersect)
    return PR_FALSE;
  mDamageArea = nsRect(startColIndex, startRowIndex,
                       1 + PR_ABS(PRInt32(endColIndex - startColIndex)),
                       1 + endRowIndex - startRowIndex);

  Reset();
  mVerInfo = new BCVerticalSeg[mDamageArea.width + 1];
  if (!mVerInfo)
    return PR_FALSE;
  return PR_TRUE;
}

void
BCPaintBorderIterator::Reset()
{
  mAtEnd = PR_TRUE; // gets reset when First() is called
  mRg = mStartRg;
  mPrevRow  = nsnull;
  mRow      = mStartRow;
  mRowIndex      = 0;
  mColIndex      = 0;
  mRgIndex       = -1;
  mPrevCell      = nsnull;
  mCell          = nsnull;
  mPrevCellData  = nsnull;
  mCellData      = nsnull;
  mBCData        = nsnull;
  ResetVerInfo();
 }

/**
 * Set the iterator data to a new cellmap coordinate
 * @param aRowIndex - the row index
 * @param aColIndex - the col index
 */
void
BCPaintBorderIterator::SetNewData(PRInt32 aY,
                                PRInt32 aX)
{
  if (!mTableCellMap || !mTableCellMap->mBCInfo) ABORT0();

  mColIndex    = aX;
  mRowIndex    = aY;
  mPrevCellData = mCellData;
  if (IsTableRightMost() && IsTableBottomMost()) {
   mCell = nsnull;
   mBCData = &mTableCellMap->mBCInfo->mLowerRightCorner;
  }
  else if (IsTableRightMost()) {
    mCellData = nsnull;
    mBCData = &mTableCellMap->mBCInfo->mRightBorders.ElementAt(aY);
  }
  else if (IsTableBottomMost()) {
    mCellData = nsnull;
    mBCData = &mTableCellMap->mBCInfo->mBottomBorders.ElementAt(aX);
  }
  else {
    if (PRUint32(mRowIndex - mFifRgFirstRowIndex) < mCellMap->mRows.Length()) {
      mBCData = nsnull;
      mCellData =
        (BCCellData*)mCellMap->mRows[mRowIndex - mFifRgFirstRowIndex].SafeElementAt(mColIndex);
      if (mCellData) {
        mBCData = &mCellData->mData;
        if (!mCellData->IsOrig()) {
          if (mCellData->IsRowSpan()) {
            aY -= mCellData->GetRowSpanOffset();
          }
          if (mCellData->IsColSpan()) {
            aX -= mCellData->GetColSpanOffset();
          }
          if ((aX >= 0) && (aY >= 0)) {
            mCellData = (BCCellData*)mCellMap->mRows[aY - mFifRgFirstRowIndex][aX];
          }
        }
        if (mCellData->IsOrig()) {
          mPrevCell = mCell;
          mCell = mCellData->GetCellFrame();
        }
      }
    }
  }
}

/**
 * Set the iterator to a new row
 * @param aRow - the new row frame, if null the iterator will advance to the
 *               next row
 */
PRBool
BCPaintBorderIterator::SetNewRow(nsTableRowFrame* aRow)
{
  mPrevRow = mRow;
  mRow     = (aRow) ? aRow : mRow->GetNextRow();
  if (mRow) {
    mIsNewRow = PR_TRUE;
    mRowIndex = mRow->GetRowIndex();
    mColIndex = mDamageArea.x;
    mPrevHorSegHeight = 0;
    if (mIsRepeatedHeader) {
      mRepeatedHeaderRowIndex = mRowIndex;
    }
  }
  else {
    mAtEnd = PR_TRUE;
  }
  return !mAtEnd;
}

/**
 * Advance the iterator to the next row group
 */
PRBool
BCPaintBorderIterator::SetNewRowGroup()
{

  mRgIndex++;

  mIsRepeatedHeader = PR_FALSE;
  mIsRepeatedFooter = PR_FALSE;

  if (mRgIndex < mRowGroups.Length()) {
    mPrevRg = mRg;
    mRg = mRowGroups[mRgIndex];
    mFifRgFirstRowIndex = ((nsTableRowGroupFrame*)mRg->GetFirstInFlow())->GetStartRowIndex();
    mRgFirstRowIndex    = mRg->GetStartRowIndex();
    mRgLastRowIndex     = mRgFirstRowIndex + mRg->GetRowCount() - 1;

    if (SetNewRow(mRg->GetFirstRow())) {
      mCellMap =
        mTableCellMap->GetMapFor((nsTableRowGroupFrame*)mRg->GetFirstInFlow(),
                                  nsnull);
      if (!mCellMap) ABORT1(PR_FALSE);
    }
    if (mRg && mTable->GetPrevInFlow() && !mRg->GetPrevInFlow()) {
      // if mRowGroup doesn't have a prev in flow, then it may be a repeated
      // header or footer
      const nsStyleDisplay* display = mRg->GetStyleDisplay();
      if (mRowIndex == mDamageArea.y) {
        mIsRepeatedHeader = (NS_STYLE_DISPLAY_TABLE_HEADER_GROUP == display->mDisplay);
      }
      else {
        mIsRepeatedFooter = (NS_STYLE_DISPLAY_TABLE_FOOTER_GROUP == display->mDisplay);
      }
    }
  }
  else {
    mAtEnd = PR_TRUE;
  }
  return !mAtEnd;
}

/**
 *  Move the iterator to the first position in the damageArea
 */
void
BCPaintBorderIterator::First()
{
  if (!mTable || (mDamageArea.x >= mNumTableCols) ||
      (mDamageArea.y >= mNumTableRows)) ABORT0();

  mAtEnd = PR_FALSE;

  PRUint32 numRowGroups = mRowGroups.Length();
  for (PRUint32 rgY = 0; rgY < numRowGroups; rgY++) {
    nsTableRowGroupFrame* rowG = mRowGroups[rgY];
    PRInt32 start = rowG->GetStartRowIndex();
    PRInt32 end   = start + rowG->GetRowCount() - 1;
    if ((mDamageArea.y >= start) && (mDamageArea.y <= end)) {
      mRgIndex = rgY - 1; // SetNewRowGroup increments rowGroupIndex
      if (SetNewRowGroup()) {
        while ((mRowIndex < mDamageArea.y) && !mAtEnd) {
          SetNewRow();
        }
        if (!mAtEnd) {
          SetNewData(mDamageArea.y, mDamageArea.x);
        }
      }
      return;
    }
  }
  mAtEnd = PR_TRUE;
}

/**
 * Advance the iterator to the next position
 */
void
BCPaintBorderIterator::Next()
{
  if (mAtEnd) ABORT0();
  mIsNewRow = PR_FALSE;

  mColIndex++;
  if (mColIndex > mDamageArea.XMost()) {
    mRowIndex++;
    if (mRowIndex == mDamageArea.YMost()) {
      mColIndex = mDamageArea.x;
    }
    else if (mRowIndex < mDamageArea.YMost()) {
      if (mRowIndex <= mRgLastRowIndex) {
        SetNewRow();
      }
      else {
        SetNewRowGroup();
      }
    }
    else {
      mAtEnd = PR_TRUE;
    }
  }
  if (!mAtEnd) {
    SetNewData(mRowIndex, mColIndex);
  }
}

// XXX if CalcVerCornerOffset and CalcHorCornerOffset remain similar, combine
// them
/** Compute the vertical offset of a vertical border segment
  * @param aCornerOwnerSide - which side owns the corner
  * @param aCornerSubWidth  - how wide is the nonwinning side of the corner
  * @param aHorWidth        - how wide is the horizontal edge of the corner
  * @param aIsStartOfSeg    - does this corner start a new segment
  * @param aIsBevel         - is this corner beveled
  * @return                 - offset in twips
  */
static nscoord
CalcVerCornerOffset(PRUint8     aCornerOwnerSide,
                    BCPixelSize aCornerSubWidth,
                    BCPixelSize aHorWidth,
                    PRBool      aIsStartOfSeg,
                    PRBool      aIsBevel)
{
  nscoord offset = 0;
  // XXX These should be replaced with appropriate side-specific macros (which?)
  BCPixelSize smallHalf, largeHalf;
  if ((NS_SIDE_TOP == aCornerOwnerSide) ||
      (NS_SIDE_BOTTOM == aCornerOwnerSide)) {
    DivideBCBorderSize(aCornerSubWidth, smallHalf, largeHalf);
    if (aIsBevel) {
      offset = (aIsStartOfSeg) ? -largeHalf : smallHalf;
    }
    else {
      offset = (NS_SIDE_TOP == aCornerOwnerSide) ? smallHalf : -largeHalf;
    }
  }
  else {
    DivideBCBorderSize(aHorWidth, smallHalf, largeHalf);
    if (aIsBevel) {
      offset = (aIsStartOfSeg) ? -largeHalf : smallHalf;
    }
    else {
      offset = (aIsStartOfSeg) ? smallHalf : -largeHalf;
    }
  }
  return nsPresContext::CSSPixelsToAppUnits(offset);
}

/** Compute the horizontal offset of a horizontal border segment
  * @param aCornerOwnerSide - which side owns the corner
  * @param aCornerSubWidth  - how wide is the nonwinning side of the corner
  * @param aVerWidth        - how wide is the vertical edge of the corner
  * @param aIsStartOfSeg    - does this corner start a new segment
  * @param aIsBevel         - is this corner beveled
  * @param aTableIsLTR      - direction, the computation depends on ltr or rtl
  * @return                 - offset in twips
  */
static nscoord
CalcHorCornerOffset(PRUint8     aCornerOwnerSide,
                    BCPixelSize aCornerSubWidth,
                    BCPixelSize aVerWidth,
                    PRBool      aIsStartOfSeg,
                    PRBool      aIsBevel,
                    PRBool      aTableIsLTR)
{
  nscoord offset = 0;
  // XXX These should be replaced with appropriate side-specific macros (which?)
  BCPixelSize smallHalf, largeHalf;
  if ((NS_SIDE_LEFT == aCornerOwnerSide) ||
      (NS_SIDE_RIGHT == aCornerOwnerSide)) {
    if (aTableIsLTR) {
      DivideBCBorderSize(aCornerSubWidth, smallHalf, largeHalf);
    }
    else {
      DivideBCBorderSize(aCornerSubWidth, largeHalf, smallHalf);
    }
    if (aIsBevel) {
      offset = (aIsStartOfSeg) ? -largeHalf : smallHalf;
    }
    else {
      offset = (NS_SIDE_LEFT == aCornerOwnerSide) ? smallHalf : -largeHalf;
    }
  }
  else {
    if (aTableIsLTR) {
      DivideBCBorderSize(aVerWidth, smallHalf, largeHalf);
    }
    else {
      DivideBCBorderSize(aVerWidth, largeHalf, smallHalf);
    }
    if (aIsBevel) {
      offset = (aIsStartOfSeg) ? -largeHalf : smallHalf;
    }
    else {
      offset = (aIsStartOfSeg) ? smallHalf : -largeHalf;
    }
  }
  return nsPresContext::CSSPixelsToAppUnits(offset);
}

BCVerticalSeg::BCVerticalSeg()
{
  mCol = nsnull;
  mFirstCell = mLastCell = mAjaCell = nsnull;
  mOffsetX = mOffsetY = mLength = mWidth = mTopBevelOffset = mTopBevelSide = 0;
  mOwner = eCellOwner;
}

/**
 * Start a new vertical segment
 * @param aIter         - iterator containing the structural information
 * @param aBorderOwner  - determines the border style
 * @param aVerSegWidth  - the width of segment in pixel
 * @param aHorSegHeight - the width of the horizontal segment joining the corner
 *                        at the start
 */
void
BCVerticalSeg::Start(BCPaintBorderIterator& aIter,
                     BCBorderOwner          aBorderOwner,
                     BCPixelSize            aVerSegWidth,
                     BCPixelSize            aHorSegHeight)
{
  PRUint8      ownerSide   = 0;
  PRPackedBool bevel       = PR_FALSE;


  nscoord cornerSubWidth  = (aIter.mBCData) ?
                               aIter.mBCData->GetCorner(ownerSide, bevel) : 0;

  PRBool  topBevel        = (aVerSegWidth > 0) ? bevel : PR_FALSE;
  BCPixelSize maxHorSegHeight = PR_MAX(aIter.mPrevHorSegHeight, aHorSegHeight);
  nscoord offset          = CalcVerCornerOffset(ownerSide, cornerSubWidth,
                                                maxHorSegHeight, PR_TRUE,
                                                topBevel);

  mTopBevelOffset   = (topBevel) ?
                      nsPresContext::CSSPixelsToAppUnits(maxHorSegHeight): 0;
  // XXX this assumes that only corners where 2 segments join can be beveled
  mTopBevelSide     = (aHorSegHeight > 0) ? NS_SIDE_RIGHT : NS_SIDE_LEFT;
  mOffsetY      += offset;
  mLength        = -offset;
  mWidth         = aVerSegWidth;
  mOwner         = aBorderOwner;
  mFirstCell     = aIter.mCell;
  mFirstRowGroup = aIter.mRg;
  mFirstRow      = aIter.mRow;
  if (aIter.GetRelativeColIndex() > 0) {
    mAjaCell = aIter.mVerInfo[aIter.GetRelativeColIndex() - 1].mLastCell;
  }
}

/**
 * Initialize the vertical segments with information that will persist for any
 * vertical segment in this column
 * @param aIter - iterator containing the structural information
 */
void
BCVerticalSeg::Initialize(BCPaintBorderIterator& aIter)
{
  PRInt32 relColIndex = aIter.GetRelativeColIndex();
  mCol = aIter.IsTableRightMost() ? aIter.mVerInfo[relColIndex - 1].mCol :
           aIter.mTableFirstInFlow->GetColFrame(aIter.mColIndex);
  if (!mCol) ABORT0();
  if (0 == relColIndex) {
    mOffsetX = aIter.mInitialOffsetX;
  }
  // set colX for the next column
  if (!aIter.IsDamageAreaRightMost()) {
    aIter.mVerInfo[relColIndex + 1].mOffsetX = mOffsetX +
                                         aIter.mColInc * mCol->GetSize().width;
  }
  mOffsetY = aIter.mInitialOffsetY;
  mLastCell = aIter.mCell;
}

/**
 * Compute the offsets for the bottom corner of a vertical segment
 * @param aIter         - iterator containing the structural information
 * @param aHorSegHeight - the width of the horizontal segment joining the corner
 *                        at the start
 */
void
BCVerticalSeg::GetBottomCorner(BCPaintBorderIterator& aIter,
                               BCPixelSize            aHorSegHeight)
{
   PRUint8 ownerSide = 0;
   nscoord cornerSubWidth = 0;
   PRPackedBool bevel = PR_FALSE;
   if (aIter.mBCData) {
     cornerSubWidth = aIter.mBCData->GetCorner(ownerSide, bevel);
   }
   mIsBottomBevel = (mWidth > 0) ? bevel : PR_FALSE;
   mBottomHorSegHeight = PR_MAX(aIter.mPrevHorSegHeight, aHorSegHeight);
   mBottomOffset = CalcVerCornerOffset(ownerSide, cornerSubWidth,
                                    mBottomHorSegHeight,
                                    PR_FALSE, mIsBottomBevel);
   mLength += mBottomOffset;
}

/**
 * Paint the vertical segment
 * @param aIter         - iterator containing the structural information
 * @param aRenderingContext - the rendering context
 * @param aHorSegHeight - the width of the horizontal segment joining the corner
 *                        at the start
 */
void
BCVerticalSeg::Paint(BCPaintBorderIterator& aIter,
                     nsIRenderingContext&   aRenderingContext,
                     BCPixelSize            aHorSegHeight)
{
  // get the border style, color and paint the segment
  PRUint8 side = (aIter.IsDamageAreaRightMost()) ? NS_SIDE_RIGHT :
                                                    NS_SIDE_LEFT;
  PRInt32 relColIndex = aIter.GetRelativeColIndex();
  nsTableColFrame* col           = mCol; if (!col) ABORT0();
  nsTableCellFrame* cell         = mFirstCell; // ???
  nsIFrame* owner = nsnull;
  PRUint8 style = NS_STYLE_BORDER_STYLE_SOLID;
  nscolor color = 0xFFFFFFFF;

  switch (mOwner) {
    case eTableOwner:
      owner = aIter.mTable;
      break;
    case eAjaColGroupOwner:
      side = NS_SIDE_RIGHT;
      if (!aIter.IsTableRightMost() && (relColIndex > 0)) {
        col = aIter.mVerInfo[relColIndex - 1].mCol;
      } // and fall through
    case eColGroupOwner:
      if (col) {
        owner = col->GetParent();
      }
      break;
    case eAjaColOwner:
      side = NS_SIDE_RIGHT;
      if (!aIter.IsTableRightMost() && (relColIndex > 0)) {
        col = aIter.mVerInfo[relColIndex - 1].mCol;
      } // and fall through
    case eColOwner:
      owner = col;
      break;
    case eAjaRowGroupOwner:
      NS_ERROR("a neighboring rowgroup can never own a vertical border");
      // and fall through
    case eRowGroupOwner:
      NS_ASSERTION(aIter.IsTableLeftMost() || aIter.IsTableRightMost(),
                  "row group can own border only at table edge");
      owner = mFirstRowGroup;
      break;
    case eAjaRowOwner:
      NS_ASSERTION(PR_FALSE, "program error"); // and fall through
    case eRowOwner:
      NS_ASSERTION(aIter.IsTableLeftMost() || aIter.IsTableRightMost(),
                   "row can own border only at table edge");
      owner = mFirstRow;
      break;
    case eAjaCellOwner:
      side = NS_SIDE_RIGHT;
      cell = mAjaCell; // and fall through
    case eCellOwner:
      owner = cell;
      break;
  }
  if (owner) {
    ::GetPaintStyleInfo(owner, side, style, color, aIter.mTableIsLTR);
  }
  BCPixelSize smallHalf, largeHalf;
  DivideBCBorderSize(mWidth, smallHalf, largeHalf);
  nsRect segRect(mOffsetX - nsPresContext::CSSPixelsToAppUnits(largeHalf),
                 mOffsetY,
                 nsPresContext::CSSPixelsToAppUnits(mWidth), mLength);
  nscoord bottomBevelOffset = (mIsBottomBevel) ?
                  nsPresContext::CSSPixelsToAppUnits(mBottomHorSegHeight) : 0;
  PRUint8 bottomBevelSide = ((aHorSegHeight > 0) ^ !aIter.mTableIsLTR) ?
                            NS_SIDE_RIGHT : NS_SIDE_LEFT;
  PRUint8 topBevelSide = ((mTopBevelSide == NS_SIDE_RIGHT) ^ !aIter.mTableIsLTR)?
                         NS_SIDE_RIGHT : NS_SIDE_LEFT;
  nsCSSRendering::DrawTableBorderSegment(aRenderingContext, style, color,
                                         aIter.mTableBgColor, segRect,
                                         nsPresContext::AppUnitsPerCSSPixel(),
                                         topBevelSide, mTopBevelOffset,
                                         bottomBevelSide, bottomBevelOffset);
}

/**
 * Advance the start point of a segment
 */
void
BCVerticalSeg::AdvanceOffsetY()
{
  mOffsetY +=  mLength - mBottomOffset;
}

/**
 * Accumulate the current segment
 */
void
BCVerticalSeg::IncludeCurrentBorder(BCPaintBorderIterator& aIter)
{
  mLastCell = aIter.mCell;
  mLength  += aIter.mRow->GetRect().height;
}

BCHorizontalSeg::BCHorizontalSeg()
{
  mOffsetX = mOffsetY = mLength = mWidth =  mLeftBevelOffset = 0;
  mLeftBevelSide = 0;
  mFirstCell = mAjaCell = nsnull;
}
 
/** Initialize a horizontal border segment for painting
  * @param aIter              - iterator storing the current and adjacent frames
  * @param aBorderOwner       - which frame owns the border
  * @param aBottomVerSegWidth - vertical segment width coming from up
  * @param aHorSegHeight      - the height of the segment
  +  */
void
BCHorizontalSeg::Start(BCPaintBorderIterator& aIter,
                       BCBorderOwner        aBorderOwner,
                       BCPixelSize          aBottomVerSegWidth,
                       BCPixelSize          aHorSegHeight)
{
  PRUint8      cornerOwnerSide = 0;
  PRPackedBool bevel     = PR_FALSE;

  mOwner = aBorderOwner;
  nscoord cornerSubWidth  = (aIter.mBCData) ?
                             aIter.mBCData->GetCorner(cornerOwnerSide,
                                                       bevel) : 0;

  PRBool  leftBevel = (aHorSegHeight > 0) ? bevel : PR_FALSE;
  PRInt32 relColIndex = aIter.GetRelativeColIndex();
  nscoord maxVerSegWidth = PR_MAX(aIter.mVerInfo[relColIndex].mWidth,
                                  aBottomVerSegWidth);
  nscoord offset = CalcHorCornerOffset(cornerOwnerSide, cornerSubWidth,
                                       maxVerSegWidth, PR_TRUE, leftBevel,
                                       aIter.mTableIsLTR);
  mLeftBevelOffset = (leftBevel && (aHorSegHeight > 0)) ? maxVerSegWidth : 0;
  // XXX this assumes that only corners where 2 segments join can be beveled
  mLeftBevelSide   = (aBottomVerSegWidth > 0) ? NS_SIDE_BOTTOM : NS_SIDE_TOP;
  if (aIter.mTableIsLTR) {
    mOffsetX += offset;
  }
  else {
    mOffsetX -= offset;
  }
  mLength          = -offset;
  mWidth           = aHorSegHeight;
  mFirstCell       = aIter.mCell;
  mAjaCell         = (aIter.IsDamageAreaTopMost()) ? nsnull :
                     aIter.mVerInfo[relColIndex].mLastCell;
}

/**
 * Compute the offsets for the right corner of a horizontal segment
 * @param aIter         - iterator containing the structural information
 * @param aLeftSegWidth - the width of the vertical segment joining the corner
 *                        at the start
 */
void
BCHorizontalSeg::GetRightCorner(BCPaintBorderIterator& aIter,
                                BCPixelSize            aLeftSegWidth)
{
  PRUint8 ownerSide = 0;
  nscoord cornerSubWidth = 0;
  PRPackedBool bevel = PR_FALSE;
  if (aIter.mBCData) {
    cornerSubWidth = aIter.mBCData->GetCorner(ownerSide, bevel);
  }

  mIsRightBevel = (mWidth > 0) ? bevel : 0;
  PRInt32 relColIndex = aIter.GetRelativeColIndex();
  nscoord verWidth = PR_MAX(aIter.mVerInfo[relColIndex].mWidth, aLeftSegWidth);
  mEndOffset = CalcHorCornerOffset(ownerSide, cornerSubWidth, verWidth,
                                   PR_FALSE, mIsRightBevel, aIter.mTableIsLTR);
  mLength += mEndOffset;
  mRightBevelOffset = (mIsRightBevel) ?
                       nsPresContext::CSSPixelsToAppUnits(verWidth) : 0;
  mRightBevelSide = (aLeftSegWidth > 0) ? NS_SIDE_BOTTOM : NS_SIDE_TOP;
}

/**
 * Paint the horizontal segment
 * @param aIter         - iterator containing the structural information
 * @param aRenderingContext - the rendering context
 */
void
BCHorizontalSeg::Paint(BCPaintBorderIterator& aIter,
                       nsIRenderingContext&   aRenderingContext)
{
  // get the border style, color and paint the segment
  PRUint8 side = (aIter.IsDamageAreaBottomMost()) ? NS_SIDE_BOTTOM :
                                                     NS_SIDE_TOP;
  nsIFrame* rg   = aIter.mRg;  if (!rg) ABORT0();
  nsIFrame* row  = aIter.mRow; if (!row) ABORT0();
  nsIFrame* cell = mFirstCell; if (!cell) ABORT0(); // ????
  nsIFrame* col;
  nsIFrame* owner = nsnull;

  PRUint8 style = NS_STYLE_BORDER_STYLE_SOLID;
  nscolor color = 0xFFFFFFFF;
 
  
  switch (mOwner) {
    case eTableOwner:
      owner = aIter.mTable;
      break;
    case eAjaColGroupOwner:
      NS_ERROR("neighboring colgroups can never own a horizontal border");
      // and fall through
    case eColGroupOwner: 
      NS_ASSERTION(aIter.IsTableTopMost() || aIter.IsTableBottomMost(),
                   "col group can own border only at the table edge");
      col = aIter.mTableFirstInFlow->GetColFrame(aIter.mColIndex - 1);
      if (!col) ABORT0();
      owner = col->GetParent(); 
      break;
    case eAjaColOwner:
      NS_ERROR("neighboring column can never own a horizontal border");
      // and fall through
    case eColOwner:
      NS_ASSERTION(aIter.IsTableTopMost() || aIter.IsTableBottomMost(),
                   "col can own border only at the table edge");
      owner = aIter.mTableFirstInFlow->GetColFrame(aIter.mColIndex - 1);
      break;
    case eAjaRowGroupOwner:
      side = NS_SIDE_BOTTOM;
      rg = (aIter.IsTableBottomMost()) ? aIter.mRg : aIter.mPrevRg;
      // and fall through
    case eRowGroupOwner:
      owner = rg;
      break;
    case eAjaRowOwner:
      side = NS_SIDE_BOTTOM;
      row = (aIter.IsTableBottomMost()) ? aIter.mRow : aIter.mPrevRow;
      // and fall through
      case eRowOwner:
      owner = row;
      break;
    case eAjaCellOwner:
      side = NS_SIDE_BOTTOM;
      // if this is null due to the damage area origin-y > 0, then the border
      // won't show up anyway
      cell = mAjaCell;
      // and fall through
    case eCellOwner:
      owner = cell;
      break;
  }
  if (owner) {            
    ::GetPaintStyleInfo(owner, side, style, color, aIter.mTableIsLTR);
  }
  BCPixelSize smallHalf, largeHalf;
  DivideBCBorderSize(mWidth, smallHalf, largeHalf);
  nsRect segRect(mOffsetX,
                 mOffsetY - nsPresContext::CSSPixelsToAppUnits(largeHalf),
                 mLength,
                 nsPresContext::CSSPixelsToAppUnits(mWidth));
  if (aIter.mTableIsLTR) {
    nsCSSRendering::DrawTableBorderSegment(aRenderingContext, style, color,
                                           aIter.mTableBgColor, segRect,
                                           nsPresContext::AppUnitsPerCSSPixel(),
                                           mLeftBevelSide,
                                           nsPresContext::CSSPixelsToAppUnits(mLeftBevelOffset), 
                                           mRightBevelSide, mRightBevelOffset);
  }
  else {
    segRect.x -= segRect.width;
    nsCSSRendering::DrawTableBorderSegment(aRenderingContext, style, color,
                                           aIter.mTableBgColor, segRect,
                                           nsPresContext::AppUnitsPerCSSPixel(),
                                           mRightBevelSide, mRightBevelOffset,
                                           mLeftBevelSide,
                                           nsPresContext::CSSPixelsToAppUnits(mLeftBevelOffset));
  }
}

/**
 * Advance the start point of a segment
 */
void
BCHorizontalSeg::AdvanceOffsetX(PRInt32 aIncrement)
{
  mOffsetX += aIncrement * (mLength - mEndOffset);
}

/**
 * Accumulate the current segment
 */
void
BCHorizontalSeg::IncludeCurrentBorder(BCPaintBorderIterator& aIter)
{
  mLength += aIter.mVerInfo[aIter.GetRelativeColIndex()].mColWidth;
}

/**
 * store the column width information while painting horizontal segment
 */
void
BCPaintBorderIterator::StoreColumnWidth(PRInt32 aIndex)
{
  if (IsTableRightMost()) {
      mVerInfo[aIndex].mColWidth = mVerInfo[aIndex - 1].mColWidth;
  }
  else {
    nsTableColFrame* col = mTableFirstInFlow->GetColFrame(mColIndex);
    if (!col) ABORT0();
    mVerInfo[aIndex].mColWidth = col->GetSize().width;
  }
}
/**
 * Determine if a vertical segment owns the corder
 */
PRBool
BCPaintBorderIterator::VerticalSegmentOwnsCorner()
{
  PRUint8 cornerOwnerSide = 0;
  PRPackedBool bevel = PR_FALSE;
  nscoord cornerSubWidth;
  cornerSubWidth = (mBCData) ? mBCData->GetCorner(cornerOwnerSide, bevel) : 0;
  // unitialized ownerside, bevel
  return  (NS_SIDE_TOP == cornerOwnerSide) ||
          (NS_SIDE_BOTTOM == cornerOwnerSide);
}

/**
 * Paint if necessary a horizontal segment, otherwise accumulate it
 * @param aRenderingContext - the rendering context
 */
void
BCPaintBorderIterator::AccumulateOrPaintHorizontalSegment(nsIRenderingContext& aRenderingContext)
{

  PRInt32 relColIndex = GetRelativeColIndex();
  // store the current col width if it hasn't been already
  if (mVerInfo[relColIndex].mColWidth < 0) {
    StoreColumnWidth(relColIndex);
  }
  
  BCBorderOwner borderOwner = eCellOwner;
  BCBorderOwner ignoreBorderOwner;
  PRBool isSegStart = PR_TRUE;
  PRBool ignoreSegStart;
  
  nscoord leftSegWidth = (mBCData) ? mBCData->GetLeftEdge(ignoreBorderOwner,
                                                          ignoreSegStart) : 0;
  nscoord topSegHeight = (mBCData) ? mBCData->GetTopEdge(borderOwner,
                                                         isSegStart) : 0;

  if (mIsNewRow || (IsDamageAreaLeftMost() && IsDamageAreaBottomMost())) {
    // reset for every new row and on the bottom of the last row
    mHorSeg.mOffsetY = mNextOffsetY;
    mNextOffsetY     = mNextOffsetY + mRow->GetSize().height;
    mHorSeg.mOffsetX = mInitialOffsetX;
    mHorSeg.Start(*this, borderOwner, leftSegWidth, topSegHeight);
  }

  if (!IsDamageAreaLeftMost() && (isSegStart || IsDamageAreaRightMost() ||
                                  VerticalSegmentOwnsCorner())) {
    // paint the previous seg or the current one if IsDamageAreaRightMost()
    if (mHorSeg.mLength > 0) {
      mHorSeg.GetRightCorner(*this, leftSegWidth);
      if (mHorSeg.mWidth > 0) {
        mHorSeg.Paint(*this, aRenderingContext);
      }
      mHorSeg.AdvanceOffsetX(mColInc);
    }
    mHorSeg.Start(*this, borderOwner, leftSegWidth, topSegHeight);
  }
  mHorSeg.IncludeCurrentBorder(*this);
  mVerInfo[relColIndex].mWidth = leftSegWidth;
  mVerInfo[relColIndex].mLastCell = mCell;
}
/**
 * Paint if necessary a vertical segment, otherwise  it
 * @param aRenderingContext - the rendering context
 */
void
BCPaintBorderIterator::AccumulateOrPaintVerticalSegment(nsIRenderingContext& aRenderingContext)
{
  BCBorderOwner borderOwner = eCellOwner;
  BCBorderOwner ignoreBorderOwner;
  PRBool isSegStart = PR_TRUE;
  PRBool ignoreSegStart;

  nscoord verSegWidth  = (mBCData) ? mBCData->GetLeftEdge(borderOwner,
                                                          isSegStart) : 0;
  nscoord horSegHeight = (mBCData) ? mBCData->GetTopEdge(ignoreBorderOwner,
                                                         ignoreSegStart) : 0;

  PRInt32 relColIndex = GetRelativeColIndex();
  BCVerticalSeg& verSeg = mVerInfo[relColIndex];
  if (!verSeg.mCol) { // on the first damaged row and the first segment in the
                      // col
    verSeg.Initialize(*this);
    verSeg.Start(*this, borderOwner, verSegWidth, horSegHeight);
  }

  if (!IsDamageAreaTopMost() && (isSegStart || IsDamageAreaBottomMost() ||
                                 IsAfterRepeatedHeader() ||
                                 StartRepeatedFooter())) {
    // paint the previous seg or the current one if IsDamageAreaBottomMost()
    if (verSeg.mLength > 0) {
      verSeg.GetBottomCorner(*this, horSegHeight);
      if (verSeg.mWidth > 0) {
        verSeg.Paint(*this, aRenderingContext, horSegHeight);
      }
      verSeg.AdvanceOffsetY();
    }
    verSeg.Start(*this, borderOwner, verSegWidth, horSegHeight);
  }
  verSeg.IncludeCurrentBorder(*this);
  mPrevHorSegHeight = horSegHeight;
}

/**
 * Reset the vertical information cache
 */
void
BCPaintBorderIterator::ResetVerInfo()
{
  if (mVerInfo) {
    memset(mVerInfo, 0, mDamageArea.width * sizeof(BCVerticalSeg));
    // XXX reinitialize properly
    for (PRInt32 xIndex = 0; xIndex < mDamageArea.width; xIndex++) {
      mVerInfo[xIndex].mColWidth = -1;
    }
  }
}

/**
 * Method to paint BCBorders, this does not use currently display lists although
 * it will do this in future
 * @param aRenderingContext - the rendering context
 * @param aDirtyRect        - inside this rectangle the BC Borders will redrawn
 */
void 
nsTableFrame::PaintBCBorders(nsIRenderingContext& aRenderingContext,
                             const nsRect&        aDirtyRect)
{
  // We first transfer the aDirtyRect into cellmap coordinates to compute which
  // cell borders need to be painted
  BCPaintBorderIterator iter(this);
  if (!iter.SetDamageArea(aDirtyRect))
    return;

  // First, paint all of the vertical borders from top to bottom and left to
  // right as they become complete. They are painted first, since they are less
  // efficient to paint than horizontal segments. They were stored with as few
  // segments as possible (since horizontal borders are painted last and
  // possibly over them). For every cell in a row that fails in the damage are
  // we look up if the current border would start a new segment, if so we paint
  // the previously stored vertical segment and start a new segment. After
  // this we  the now active segment with the current border. These
  // segments are stored in mVerInfo to be used on the next row
  for (iter.First(); !iter.mAtEnd; iter.Next()) {
    iter.AccumulateOrPaintVerticalSegment(aRenderingContext);
  }

  // Next, paint all of the horizontal border segments from top to bottom reuse
  // the mVerInfo array to keep track of col widths and vertical segments for
  // corner calculations
  iter.Reset();
  for (iter.First(); !iter.mAtEnd; iter.Next()) {
    iter.AccumulateOrPaintHorizontalSegment(aRenderingContext);
  }
}

PRBool nsTableFrame::RowHasSpanningCells(PRInt32 aRowIndex, PRInt32 aNumEffCols)
{
  PRBool result = PR_FALSE;
  nsTableCellMap* cellMap = GetCellMap();
  NS_PRECONDITION (cellMap, "bad call, cellMap not yet allocated.");
  if (cellMap) {
    result = cellMap->RowHasSpanningCells(aRowIndex, aNumEffCols);
  }
  return result;
}

PRBool nsTableFrame::RowIsSpannedInto(PRInt32 aRowIndex, PRInt32 aNumEffCols)
{
  PRBool result = PR_FALSE;
  nsTableCellMap* cellMap = GetCellMap();
  NS_PRECONDITION (cellMap, "bad call, cellMap not yet allocated.");
  if (cellMap) {
    result = cellMap->RowIsSpannedInto(aRowIndex, aNumEffCols);
  }
  return result;
}

PRBool nsTableFrame::ColHasSpanningCells(PRInt32 aColIndex)
{
  PRBool result = PR_FALSE;
  nsTableCellMap * cellMap = GetCellMap();
  NS_PRECONDITION (cellMap, "bad call, cellMap not yet allocated.");
  if (cellMap) {
    result = cellMap->ColHasSpanningCells(aColIndex);
  }
  return result;
}

PRBool nsTableFrame::ColIsSpannedInto(PRInt32 aColIndex)
{
  PRBool result = PR_FALSE;
  nsTableCellMap * cellMap = GetCellMap();
  NS_PRECONDITION (cellMap, "bad call, cellMap not yet allocated.");
  if (cellMap) {
    result = cellMap->ColIsSpannedInto(aColIndex);
  }
  return result;
}

// Destructor function for nscoord properties
static void
DestroyCoordFunc(void*           aFrame,
                 nsIAtom*        aPropertyName,
                 void*           aPropertyValue,
                 void*           aDtorData)
{
  delete static_cast<nscoord*>(aPropertyValue);
}

// Destructor function point properties
static void
DestroyPointFunc(void*           aFrame,
                 nsIAtom*        aPropertyName,
                 void*           aPropertyValue,
                 void*           aDtorData)
{
  delete static_cast<nsPoint*>(aPropertyValue);
}

// Destructor function for BCPropertyData properties
static void
DestroyBCPropertyDataFunc(void*           aFrame,
                          nsIAtom*        aPropertyName,
                          void*           aPropertyValue,
                          void*           aDtorData)
{
  delete static_cast<BCPropertyData*>(aPropertyValue);
}

void*
nsTableFrame::GetProperty(nsIFrame*            aFrame,
                          nsIAtom*             aPropertyName,
                          PRBool               aCreateIfNecessary)
{
  nsPropertyTable *propTable = aFrame->PresContext()->PropertyTable();
  void *value = propTable->GetProperty(aFrame, aPropertyName);
  if (value) {
    return (nsPoint*)value;  // the property already exists
  }
  if (aCreateIfNecessary) {
    // The property isn't set yet, so allocate a new value, set the property,
    // and return the newly allocated value
    NSPropertyDtorFunc dtorFunc = nsnull;
    if (aPropertyName == nsGkAtoms::collapseOffsetProperty) {
      value = new nsPoint(0, 0);
      dtorFunc = DestroyPointFunc;
    }
    else if (aPropertyName == nsGkAtoms::rowUnpaginatedHeightProperty) {
      value = new nscoord;
      dtorFunc = DestroyCoordFunc;
    }
    else if (aPropertyName == nsGkAtoms::tableBCProperty) {
      value = new BCPropertyData;
      dtorFunc = DestroyBCPropertyDataFunc;
    }
    if (value) {
      propTable->SetProperty(aFrame, aPropertyName, value, dtorFunc, nsnull);
    }
    return value;
  }
  return nsnull;
}

/* static */
void
nsTableFrame::InvalidateFrame(nsIFrame* aFrame,
                              const nsRect& aOrigRect,
                              const nsRect& aOrigOverflowRect,
                              PRBool aIsFirstReflow)
{
  nsIFrame* parent = aFrame->GetParent();
  NS_ASSERTION(parent, "What happened here?");

  if (parent->GetStateBits() & NS_FRAME_FIRST_REFLOW) {
    // Don't bother; we'll invalidate the parent's overflow rect when
    // we finish reflowing it.
    return;
  }

  // The part that looks at both the rect and the overflow rect is a
  // bit of a hack.  See nsBlockFrame::ReflowLine for an eloquent
  // description of its hackishness.
  nsRect overflowRect = aFrame->GetOverflowRect();
  if (aIsFirstReflow ||
      aOrigRect.TopLeft() != aFrame->GetPosition() ||
      aOrigOverflowRect.TopLeft() != overflowRect.TopLeft()) {
    // Invalidate the old and new overflow rects.  Note that if the
    // frame moved, we can't just use aOrigOverflowRect, since it's in
    // coordinates relative to the old position.  So invalidate via
    // aFrame's parent, and reposition that overflow rect to the right
    // place.
    // XXXbz this doesn't handle outlines, does it?
    aFrame->Invalidate(overflowRect);
    parent->Invalidate(aOrigOverflowRect + aOrigRect.TopLeft());
  } else {
    nsRect rect = aFrame->GetRect();
    aFrame->CheckInvalidateSizeChange(aOrigRect, aOrigOverflowRect,
                                      rect.Size());
    aFrame->InvalidateRectDifference(aOrigOverflowRect, overflowRect);
    parent->InvalidateRectDifference(aOrigRect, rect);
  }    
}
