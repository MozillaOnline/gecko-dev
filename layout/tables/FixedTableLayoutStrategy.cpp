/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
// vim:cindent:ts=4:et:sw=4:
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
 * The Original Code is Mozilla's table layout code.
 *
 * The Initial Developer of the Original Code is the Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2006
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   L. David Baron <dbaron@dbaron.org> (original author)
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

/*
 * Algorithms that determine column and table widths used for CSS2's
 * 'table-layout: fixed'.
 */

#include "FixedTableLayoutStrategy.h"
#include "nsTableFrame.h"
#include "nsTableColFrame.h"
#include "nsTableCellFrame.h"

FixedTableLayoutStrategy::FixedTableLayoutStrategy(nsTableFrame *aTableFrame)
  : nsITableLayoutStrategy(nsITableLayoutStrategy::Fixed)
  , mTableFrame(aTableFrame)
{
    MarkIntrinsicWidthsDirty();
}

/* virtual */
FixedTableLayoutStrategy::~FixedTableLayoutStrategy()
{
}

/* virtual */ nscoord
FixedTableLayoutStrategy::GetMinWidth(nsIRenderingContext* aRenderingContext)
{
    DISPLAY_MIN_WIDTH(mTableFrame, mMinWidth);
    if (mMinWidth != NS_INTRINSIC_WIDTH_UNKNOWN)
        return mMinWidth;

    // It's theoretically possible to do something much better here that
    // depends only on the columns and the first row (where we look at
    // intrinsic widths inside the first row and then reverse the
    // algorithm to find the narrowest width that would hold all of
    // those intrinsic widths), but it wouldn't be compatible with other
    // browsers, or with the use of GetMinWidth by
    // nsTableFrame::ComputeSize to determine the width of a fixed
    // layout table, since CSS2.1 says:
    //   The width of the table is then the greater of the value of the
    //   'width' property for the table element and the sum of the
    //   column widths (plus cell spacing or borders).

    // XXX Should we really ignore 'min-width' and 'max-width'?
    // XXX Should we really ignore widths on column groups?

    nsTableCellMap *cellMap = mTableFrame->GetCellMap();
    PRInt32 colCount = cellMap->GetColCount();
    nscoord spacing = mTableFrame->GetCellSpacingX();

    nscoord result = 0;

    if (colCount > 0) {
        result += spacing * (colCount + 1);
    }

    for (PRInt32 col = 0; col < colCount; ++col) {
        nsTableColFrame *colFrame = mTableFrame->GetColFrame(col);
        if (!colFrame) {
            NS_ERROR("column frames out of sync with cell map");
            continue;
        }
        const nsStyleCoord *styleWidth =
            &colFrame->GetStylePosition()->mWidth;
        if (styleWidth->GetUnit() == eStyleUnit_Coord) {
            result += nsLayoutUtils::ComputeWidthValue(aRenderingContext,
                        colFrame, 0, 0, 0, *styleWidth);
        } else if (styleWidth->GetUnit() == eStyleUnit_Percent) {
            // do nothing
        } else {
            NS_ASSERTION(styleWidth->GetUnit() == eStyleUnit_Auto ||
                         styleWidth->GetUnit() == eStyleUnit_Enumerated,
                         "bad width");

            // The 'table-layout: fixed' algorithm considers only cells
            // in the first row.
            PRBool originates;
            PRInt32 colSpan;
            nsTableCellFrame *cellFrame =
                cellMap->GetCellInfoAt(0, col, &originates, &colSpan);
            if (cellFrame) {
                styleWidth = &cellFrame->GetStylePosition()->mWidth;
                if (styleWidth->GetUnit() == eStyleUnit_Coord ||
                    (styleWidth->GetUnit() == eStyleUnit_Enumerated &&
                     (styleWidth->GetIntValue() == NS_STYLE_WIDTH_MAX_CONTENT ||
                      styleWidth->GetIntValue() == NS_STYLE_WIDTH_MIN_CONTENT))) {
                    nscoord cellWidth = nsLayoutUtils::IntrinsicForContainer(
                        aRenderingContext, cellFrame, nsLayoutUtils::MIN_WIDTH);
                    if (colSpan > 1) {
                        // If a column-spanning cell is in the first
                        // row, split up the space evenly.  (XXX This
                        // isn't quite right if some of the columns it's
                        // in have specified widths.  Should we care?)
                        cellWidth = ((cellWidth + spacing) / colSpan) - spacing;
                    }
                    result += cellWidth;
                } else if (styleWidth->GetUnit() == eStyleUnit_Percent) {
                    if (colSpan > 1) {
                        // XXX Can this force columns to negative
                        // widths?
                        result -= spacing * (colSpan - 1);
                    }
                }
                // else, for 'auto', '-moz-available', and '-moz-fit-content'
                // do nothing
            }
        }
    }

    return (mMinWidth = result);
}

/* virtual */ nscoord
FixedTableLayoutStrategy::GetPrefWidth(nsIRenderingContext* aRenderingContext,
                                       PRBool aComputingSize)
{
    // It's theoretically possible to do something much better here that
    // depends only on the columns and the first row (where we look at
    // intrinsic widths inside the first row and then reverse the
    // algorithm to find the narrowest width that would hold all of
    // those intrinsic widths), but it wouldn't be compatible with other
    // browsers.
    nscoord result = nscoord_MAX;
    DISPLAY_PREF_WIDTH(mTableFrame, result);
    return result;
}

/* virtual */ void
FixedTableLayoutStrategy::MarkIntrinsicWidthsDirty()
{
    mMinWidth = NS_INTRINSIC_WIDTH_UNKNOWN;
    mLastCalcWidth = nscoord_MIN;
}

static inline nscoord
AllocateUnassigned(nscoord aUnassignedSpace, float aShare)
{
    if (aShare == 1.0f) {
        // This happens when the numbers we're dividing to get aShare
        // are equal.  We want to return unassignedSpace exactly, even
        // if it can't be precisely round-tripped through float.
        return aUnassignedSpace;
    }
    return NSToCoordRound(float(aUnassignedSpace) * aShare);
}

/* virtual */ void
FixedTableLayoutStrategy::ComputeColumnWidths(const nsHTMLReflowState& aReflowState)
{
    nscoord tableWidth = aReflowState.ComputedWidth();

    if (mLastCalcWidth == tableWidth)
        return;
    mLastCalcWidth = tableWidth;

    nsTableCellMap *cellMap = mTableFrame->GetCellMap();
    PRInt32 colCount = cellMap->GetColCount();
    nscoord spacing = mTableFrame->GetCellSpacingX();

    if (colCount == 0) {
        // No Columns - nothing to compute
        return;
    }

    // border-spacing isn't part of the basis for percentages.
    tableWidth -= spacing * (colCount + 1);
    
    // store the old column widths. We might call multiple times SetFinalWidth
    // on the columns, due to this we can't compare at the last call that the
    // width has changed with the respect to the last call to
    // ComputeColumnWidths. In order to overcome this we store the old values
    // in this array. A single call to SetFinalWidth would make it possible to
    // call GetFinalWidth before and to compare when setting the final width.
    nsTArray<nscoord> oldColWidths;

    // XXX This ignores the 'min-width' and 'max-width' properties
    // throughout.  Then again, that's what the CSS spec says to do.

    // XXX Should we really ignore widths on column groups?

    PRUint32 unassignedCount = 0;
    nscoord unassignedSpace = tableWidth;
    const nscoord unassignedMarker = nscoord_MIN;

    // We use the PrefPercent on the columns to store the percentages
    // used to compute column widths in case we need to shrink or expand
    // the columns.
    float pctTotal = 0.0f;

    // Accumulate the total specified (non-percent) on the columns for
    // distributing excess width to the columns.
    nscoord specTotal = 0;

    for (PRInt32 col = 0; col < colCount; ++col) {
        nsTableColFrame *colFrame = mTableFrame->GetColFrame(col);
        if (!colFrame) {
            oldColWidths.AppendElement(0);
            NS_ERROR("column frames out of sync with cell map");
            continue;
        }
        oldColWidths.AppendElement(colFrame->GetFinalWidth());
        colFrame->ResetPrefPercent();
        const nsStyleCoord *styleWidth =
            &colFrame->GetStylePosition()->mWidth;
        nscoord colWidth;
        if (styleWidth->GetUnit() == eStyleUnit_Coord) {
            colWidth = nsLayoutUtils::ComputeWidthValue(
                         aReflowState.rendContext,
                         colFrame, 0, 0, 0, *styleWidth);
            specTotal += colWidth;
        } else if (styleWidth->GetUnit() == eStyleUnit_Percent) {
            float pct = styleWidth->GetPercentValue();
            colWidth = NSToCoordFloor(pct * float(tableWidth));
            colFrame->AddPrefPercent(pct);
            pctTotal += pct;
        } else {
            NS_ASSERTION(styleWidth->GetUnit() == eStyleUnit_Auto ||
                         styleWidth->GetUnit() == eStyleUnit_Enumerated,
                         "bad width");

            // The 'table-layout: fixed' algorithm considers only cells
            // in the first row.
            PRBool originates;
            PRInt32 colSpan;
            nsTableCellFrame *cellFrame =
                cellMap->GetCellInfoAt(0, col, &originates, &colSpan);
            if (cellFrame) {
                styleWidth = &cellFrame->GetStylePosition()->mWidth;
                if (styleWidth->GetUnit() == eStyleUnit_Coord ||
                    (styleWidth->GetUnit() == eStyleUnit_Enumerated &&
                     (styleWidth->GetIntValue() == NS_STYLE_WIDTH_MAX_CONTENT ||
                      styleWidth->GetIntValue() == NS_STYLE_WIDTH_MIN_CONTENT))) {
                    // XXX This should use real percentage padding
                    // Note that the difference between MIN_WIDTH and
                    // PREF_WIDTH shouldn't matter for any of these
                    // values of styleWidth; use MIN_WIDTH for symmetry
                    // with GetMinWidth above, just in case there is a
                    // difference.
                    colWidth = nsLayoutUtils::IntrinsicForContainer(
                                 aReflowState.rendContext,
                                 cellFrame, nsLayoutUtils::MIN_WIDTH);
                } else if (styleWidth->GetUnit() == eStyleUnit_Percent) {
                    // XXX This should use real percentage padding
                    nsIFrame::IntrinsicWidthOffsetData offsets =
                        cellFrame->IntrinsicWidthOffsets(aReflowState.rendContext);
                    float pct = styleWidth->GetPercentValue();
                    colWidth = NSToCoordFloor(pct * float(tableWidth)) +
                               offsets.hPadding + offsets.hBorder;
                    pct /= float(colSpan);
                    colFrame->AddPrefPercent(pct);
                    pctTotal += pct;
                } else {
                    // 'auto', '-moz-available', and '-moz-fit-content'
                    colWidth = unassignedMarker;
                }
                if (colWidth != unassignedMarker) {
                    if (colSpan > 1) {
                        // If a column-spanning cell is in the first
                        // row, split up the space evenly.  (XXX This
                        // isn't quite right if some of the columns it's
                        // in have specified widths.  Should we care?)
                        colWidth = ((colWidth + spacing) / colSpan) - spacing;
                        if (colWidth < 0)
                            colWidth = 0;
                    }
                    if (styleWidth->GetUnit() != eStyleUnit_Percent) {
                        specTotal += colWidth;
                    }
                }
            } else {
                colWidth = unassignedMarker;
            }
        }

        colFrame->SetFinalWidth(colWidth);

        if (colWidth == unassignedMarker) {
            ++unassignedCount;
        } else {
            unassignedSpace -= colWidth;
        }
    }

    if (unassignedSpace < 0) {
        if (pctTotal > 0) {
            // If the columns took up too much space, reduce those that
            // had percentage widths.  The spec doesn't say to do this,
            // but we've always done it in the past, and so does WinIE6.
            nscoord pctUsed = NSToCoordFloor(pctTotal * float(tableWidth));
            nscoord reduce = NS_MIN(pctUsed, -unassignedSpace);
            float reduceRatio = float(reduce) / pctTotal;
            for (PRInt32 col = 0; col < colCount; ++col) {
                nsTableColFrame *colFrame = mTableFrame->GetColFrame(col);
                if (!colFrame) {
                    NS_ERROR("column frames out of sync with cell map");
                    continue;
                }
                nscoord colWidth = colFrame->GetFinalWidth();
                colWidth -= NSToCoordFloor(colFrame->GetPrefPercent() *
                                           reduceRatio);
                if (colWidth < 0)
                    colWidth = 0;
                colFrame->SetFinalWidth(colWidth);
            }
        }
        unassignedSpace = 0;
    }

    if (unassignedCount > 0) {
        // The spec says to distribute the remaining space evenly among
        // the columns.
        nscoord toAssign = unassignedSpace / unassignedCount;
        for (PRInt32 col = 0; col < colCount; ++col) {
            nsTableColFrame *colFrame = mTableFrame->GetColFrame(col);
            if (!colFrame) {
                NS_ERROR("column frames out of sync with cell map");
                continue;
            }
            if (colFrame->GetFinalWidth() == unassignedMarker)
                colFrame->SetFinalWidth(toAssign);
        }
    } else if (unassignedSpace > 0) {
        // The spec doesn't say how to distribute the unassigned space.
        if (specTotal > 0) {
            // Distribute proportionally to non-percentage columns.
            nscoord specUndist = specTotal;
            for (PRInt32 col = 0; col < colCount; ++col) {
                nsTableColFrame *colFrame = mTableFrame->GetColFrame(col);
                if (!colFrame) {
                    NS_ERROR("column frames out of sync with cell map");
                    continue;
                }
                if (colFrame->GetPrefPercent() == 0.0f) {
                    NS_ASSERTION(colFrame->GetFinalWidth() <= specUndist,
                                 "widths don't add up");
                    nscoord toAdd = AllocateUnassigned(unassignedSpace,
                       float(colFrame->GetFinalWidth()) / float(specUndist));
                    specUndist -= colFrame->GetFinalWidth();
                    colFrame->SetFinalWidth(colFrame->GetFinalWidth() + toAdd);
                    unassignedSpace -= toAdd;
                    if (specUndist <= 0) {
                        NS_ASSERTION(specUndist == 0,
                                     "math should be exact");
                        break;
                    }
                }
            }
            NS_ASSERTION(unassignedSpace == 0, "failed to redistribute");
        } else if (pctTotal > 0) {
            // Distribute proportionally to percentage columns.
            float pctUndist = pctTotal;
            for (PRInt32 col = 0; col < colCount; ++col) {
                nsTableColFrame *colFrame = mTableFrame->GetColFrame(col);
                if (!colFrame) {
                    NS_ERROR("column frames out of sync with cell map");
                    continue;
                }
                if (pctUndist < colFrame->GetPrefPercent()) {
                    // This can happen with floating-point math.
                    NS_ASSERTION(colFrame->GetPrefPercent() - pctUndist
                                   < 0.0001,
                                 "widths don't add up");
                    pctUndist = colFrame->GetPrefPercent();
                }
                nscoord toAdd = AllocateUnassigned(unassignedSpace,
                    colFrame->GetPrefPercent() / pctUndist);
                colFrame->SetFinalWidth(colFrame->GetFinalWidth() + toAdd);
                unassignedSpace -= toAdd;
                pctUndist -= colFrame->GetPrefPercent();
                if (pctUndist <= 0.0f) {
                    break;
                }
            }
            NS_ASSERTION(unassignedSpace == 0, "failed to redistribute");
        } else {
            // Distribute equally to the zero-width columns.
            PRInt32 colsLeft = colCount;
            for (PRInt32 col = 0; col < colCount; ++col) {
                nsTableColFrame *colFrame = mTableFrame->GetColFrame(col);
                if (!colFrame) {
                    NS_ERROR("column frames out of sync with cell map");
                    continue;
                }
                NS_ASSERTION(colFrame->GetFinalWidth() == 0, "yikes");
                nscoord toAdd = AllocateUnassigned(unassignedSpace,
                                                   1.0f / float(colsLeft));
                colFrame->SetFinalWidth(toAdd);
                unassignedSpace -= toAdd;
                --colsLeft;
            }
            NS_ASSERTION(unassignedSpace == 0, "failed to redistribute");
        }
    }
    for (PRInt32 col = 0; col < colCount; ++col) {
        nsTableColFrame *colFrame = mTableFrame->GetColFrame(col);
        if (!colFrame) {
            NS_ERROR("column frames out of sync with cell map");
            continue;
        }
        if (oldColWidths.ElementAt(col) != colFrame->GetFinalWidth()) {
            mTableFrame->DidResizeColumns();
        }
            break;
    }
}
