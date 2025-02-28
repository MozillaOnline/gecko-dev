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
 *   Michael Ventnor <m.ventnor@gmail.com>
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

/* base class #2 for rendering objects that have child lists */

#include "nsHTMLContainerFrame.h"
#include "nsFirstLetterFrame.h"
#include "nsIRenderingContext.h"
#include "nsPresContext.h"
#include "nsIPresShell.h"
#include "nsStyleContext.h"
#include "nsStyleConsts.h"
#include "nsIContent.h"
#include "nsGkAtoms.h"
#include "nsLayoutUtils.h"
#include "nsCSSAnonBoxes.h"
#include "nsILinkHandler.h"
#include "nsGUIEvent.h"
#include "nsIDocument.h"
#include "nsIURL.h"
#include "nsPlaceholderFrame.h"
#include "nsHTMLParts.h"
#include "nsIView.h"
#include "nsIViewManager.h"
#include "nsIDOMEvent.h"
#include "nsWidgetsCID.h"
#include "nsCOMPtr.h"
#include "nsIDeviceContext.h"
#include "nsIFontMetrics.h"
#include "nsIThebesFontMetrics.h"
#include "gfxFont.h"
#include "nsCSSFrameConstructor.h"
#include "nsDisplayList.h"
#include "nsBlockFrame.h"
#include "nsLineBox.h"
#include "nsDisplayList.h"
#include "nsCSSRendering.h"

class nsDisplayTextDecoration : public nsDisplayItem {
public:
  nsDisplayTextDecoration(nsHTMLContainerFrame* aFrame, PRUint8 aDecoration,
                          nscolor aColor, nsLineBox* aLine)
    : nsDisplayItem(aFrame), mLine(aLine), mColor(aColor),
      mDecoration(aDecoration) {
    MOZ_COUNT_CTOR(nsDisplayTextDecoration);
  }
#ifdef NS_BUILD_REFCNT_LOGGING
  virtual ~nsDisplayTextDecoration() {
    MOZ_COUNT_DTOR(nsDisplayTextDecoration);
  }
#endif

  virtual void Paint(nsDisplayListBuilder* aBuilder,
                     nsIRenderingContext* aCtx);
  virtual nsRect GetBounds(nsDisplayListBuilder* aBuilder);
  NS_DISPLAY_DECL_NAME("TextDecoration")
private:
  nsLineBox*            mLine;
  nscolor               mColor;
  PRUint8               mDecoration;
};

void
nsDisplayTextDecoration::Paint(nsDisplayListBuilder* aBuilder,
                               nsIRenderingContext* aCtx)
{
  nsCOMPtr<nsIFontMetrics> fm;
  nsLayoutUtils::GetFontMetricsForFrame(mFrame, getter_AddRefs(fm));
  nsIThebesFontMetrics* tfm = static_cast<nsIThebesFontMetrics*>(fm.get());
  gfxFontGroup* fontGroup = tfm->GetThebesFontGroup();
  gfxFont* firstFont = fontGroup->GetFontAt(0);
  if (!firstFont)
    return; // OOM
  const gfxFont::Metrics& metrics = firstFont->GetMetrics();

  gfxFloat ascent;
  // The ascent of first-letter frame's text may not be the same as the ascent
  // of the font metrics. Because that may use the tight box of the actual
  // glyph.
  if (mFrame->GetType() == nsGkAtoms::letterFrame) {
    // Note that nsFirstLetterFrame::GetFirstLetterBaseline() returns
    // |border-top + padding-top + ascent|. But we only need the ascent value.
    // Because they will be added in PaintTextDecorationLine.
    nsFirstLetterFrame* letterFrame = static_cast<nsFirstLetterFrame*>(mFrame);
    nscoord tmp = letterFrame->GetFirstLetterBaseline();
    tmp -= letterFrame->GetUsedBorderAndPadding().top;
    ascent = letterFrame->PresContext()->AppUnitsToGfxUnits(tmp);
  } else {
    ascent = metrics.maxAscent;
  }

  nsPoint pt = aBuilder->ToReferenceFrame(mFrame);

  nsHTMLContainerFrame* f = static_cast<nsHTMLContainerFrame*>(mFrame);
  if (mDecoration == NS_STYLE_TEXT_DECORATION_UNDERLINE) {
    gfxFloat underlineOffset = fontGroup->GetUnderlineOffset();
    f->PaintTextDecorationLine(aCtx->ThebesContext(), pt, mLine, mColor,
                               underlineOffset, ascent,
                               metrics.underlineSize, mDecoration);
  } else if (mDecoration == NS_STYLE_TEXT_DECORATION_OVERLINE) {
    f->PaintTextDecorationLine(aCtx->ThebesContext(), pt, mLine, mColor,
                               metrics.maxAscent, ascent,
                               metrics.underlineSize, mDecoration);
  } else {
    f->PaintTextDecorationLine(aCtx->ThebesContext(), pt, mLine, mColor,
                               metrics.strikeoutOffset, ascent,
                               metrics.strikeoutSize, mDecoration);
  }
}

nsRect
nsDisplayTextDecoration::GetBounds(nsDisplayListBuilder* aBuilder)
{
  return mFrame->GetOverflowRect() + aBuilder->ToReferenceFrame(mFrame);
}

class nsDisplayTextShadow : public nsDisplayItem {
public:
  nsDisplayTextShadow(nsHTMLContainerFrame* aFrame, const PRUint8 aDecoration,
                      const nscolor& aColor, nsLineBox* aLine,
                      const nscoord& aBlurRadius, const nsPoint& aOffset)
    : nsDisplayItem(aFrame), mLine(aLine), mColor(aColor),
      mDecorationFlags(aDecoration),
      mBlurRadius(aBlurRadius), mOffset(aOffset) {
    MOZ_COUNT_CTOR(nsDisplayTextShadow);
  }
  virtual ~nsDisplayTextShadow() {
    MOZ_COUNT_DTOR(nsDisplayTextShadow);
  }

  virtual void Paint(nsDisplayListBuilder* aBuilder,
                     nsIRenderingContext* aCtx);
  virtual nsRect GetBounds(nsDisplayListBuilder* aBuilder);
  NS_DISPLAY_DECL_NAME("TextShadow")
private:
  nsLineBox*    mLine;
  nscolor       mColor;
  PRUint8       mDecorationFlags;
  nscoord       mBlurRadius;
  nsPoint       mOffset;
};

void
nsDisplayTextShadow::Paint(nsDisplayListBuilder* aBuilder,
                           nsIRenderingContext* aCtx)
{
  mBlurRadius = NS_MAX(mBlurRadius, 0);

  nsCOMPtr<nsIFontMetrics> fm;
  nsLayoutUtils::GetFontMetricsForFrame(mFrame, getter_AddRefs(fm));
  nsIThebesFontMetrics* tfm = static_cast<nsIThebesFontMetrics*>(fm.get());
  gfxFontGroup* fontGroup = tfm->GetThebesFontGroup();
  gfxFont* firstFont = fontGroup->GetFontAt(0);
  if (!firstFont)
    return; // OOM

  const gfxFont::Metrics& metrics = firstFont->GetMetrics();
  gfxFloat underlineOffset = fontGroup->GetUnderlineOffset();

  nsHTMLContainerFrame* f = static_cast<nsHTMLContainerFrame*>(mFrame);
  nsPoint pt = aBuilder->ToReferenceFrame(mFrame) + mOffset;
  nsPresContext* presContext = mFrame->PresContext();

  gfxFloat lineWidth;
  nsPoint linePt;
  gfxFloat ascent;
  if (mLine) {
    // Block frames give us an nsLineBox, so we must use that
    nscoord start = mLine->mBounds.x;
    nscoord width = mLine->mBounds.width;
    f->AdjustForTextIndent(mLine, start, width);
    if (width <= 0)
      return;

    lineWidth = presContext->AppUnitsToGfxUnits(width);
    linePt = nsPoint(start + pt.x, mLine->mBounds.y + pt.y);
    ascent = presContext->AppUnitsToGfxUnits(mLine->GetAscent());
  } else {
    // For inline frames, we must use the frame's geometry
    nsRect contentRect = mFrame->GetContentRect() - mFrame->GetPosition() + pt;

    lineWidth = presContext->AppUnitsToGfxUnits(contentRect.width);
    linePt = contentRect.TopLeft();

    // The ascent of :first-letter frame's text may not be the same as the ascent
    // of the font metrics, because it may use the tight box of the actual
    // glyph.
    if (mFrame->GetType() == nsGkAtoms::letterFrame) {
      // Note that nsFirstLetterFrame::GetFirstLetterBaseline() returns
      // |border-top + padding-top + ascent|. But we only need the ascent value,
      // because those will be added in PaintTextDecorationLine.
      nsFirstLetterFrame* letterFrame = static_cast<nsFirstLetterFrame*>(mFrame);
      nscoord tmp = letterFrame->GetFirstLetterBaseline();
      tmp -= letterFrame->GetUsedBorderAndPadding().top;
      ascent = presContext->AppUnitsToGfxUnits(tmp);
    } else {
      ascent = metrics.maxAscent;
    }
  }

  gfxContext* thebesCtx = aCtx->ThebesContext();
  nsRect shadowRect(0, 0, 0, 0);

  // Get the rects for each text decoration line, and union them together so we
  // know the minimum size we can make our shadow-painting surface.
  if (mDecorationFlags & NS_STYLE_TEXT_DECORATION_UNDERLINE) {
    gfxSize size(lineWidth, metrics.underlineSize);
    nsRect rect = nsCSSRendering::GetTextDecorationRect(presContext, size,
                     ascent, underlineOffset, NS_STYLE_TEXT_DECORATION_UNDERLINE,
                     nsCSSRendering::DECORATION_STYLE_SOLID);
    shadowRect.UnionRect(shadowRect, rect + linePt);
  }
  if (mDecorationFlags & NS_STYLE_TEXT_DECORATION_OVERLINE) {
    gfxSize size(lineWidth, metrics.underlineSize);
    nsRect rect = nsCSSRendering::GetTextDecorationRect(presContext, size,
                     ascent, metrics.maxAscent, NS_STYLE_TEXT_DECORATION_OVERLINE,
                     nsCSSRendering::DECORATION_STYLE_SOLID);
    shadowRect.UnionRect(shadowRect, rect + linePt);
  }
  if (mDecorationFlags & NS_STYLE_TEXT_DECORATION_LINE_THROUGH) {
    gfxSize size(lineWidth, metrics.strikeoutSize);
    nsRect rect = nsCSSRendering::GetTextDecorationRect(presContext, size,
                     ascent, metrics.strikeoutOffset, NS_STYLE_TEXT_DECORATION_LINE_THROUGH,
                     nsCSSRendering::DECORATION_STYLE_SOLID);
    shadowRect.UnionRect(shadowRect, rect + linePt);
  }

  // Create our shadow surface, then paint the text decorations onto it
  nsContextBoxBlur contextBoxBlur;
  gfxContext* shadowCtx = contextBoxBlur.Init(shadowRect, mBlurRadius,
                                              presContext->AppUnitsPerDevPixel(),
                                              thebesCtx, mVisibleRect);
  if (!shadowCtx)
    return;

  thebesCtx->Save();
  thebesCtx->NewPath();
  thebesCtx->SetColor(gfxRGBA(mColor));

  if (mDecorationFlags & NS_STYLE_TEXT_DECORATION_UNDERLINE) {
    f->PaintTextDecorationLine(shadowCtx, pt, mLine, mColor,
                               underlineOffset, ascent,
                               metrics.underlineSize, NS_STYLE_TEXT_DECORATION_UNDERLINE);
  }
  if (mDecorationFlags & NS_STYLE_TEXT_DECORATION_OVERLINE) {
    f->PaintTextDecorationLine(shadowCtx, pt, mLine, mColor,
                               metrics.maxAscent, ascent,
                               metrics.underlineSize, NS_STYLE_TEXT_DECORATION_OVERLINE);
  }
  if (mDecorationFlags & NS_STYLE_TEXT_DECORATION_LINE_THROUGH) {
    f->PaintTextDecorationLine(shadowCtx, pt, mLine, mColor,
                               metrics.strikeoutOffset, ascent,
                               metrics.strikeoutSize, NS_STYLE_TEXT_DECORATION_LINE_THROUGH);
  }

  contextBoxBlur.DoPaint();
  thebesCtx->Restore();
}

nsRect
nsDisplayTextShadow::GetBounds(nsDisplayListBuilder* aBuilder)
{
  // Shadows are always painted in the overflow rect
  return mFrame->GetOverflowRect() + aBuilder->ToReferenceFrame(mFrame);
}

nsresult
nsHTMLContainerFrame::DisplayTextDecorations(nsDisplayListBuilder* aBuilder,
                                             nsDisplayList* aBelowTextDecorations,
                                             nsDisplayList* aAboveTextDecorations,
                                             nsLineBox* aLine)
{
  if (eCompatibility_NavQuirks == PresContext()->CompatibilityMode())
    return NS_OK;
  if (!IsVisibleForPainting(aBuilder))
    return NS_OK;
  
  // Do standards mode painting of 'text-decoration's: under+overline
  // behind children, line-through in front.  For Quirks mode, see
  // nsTextFrame::PaintTextDecorations.  (See bug 1777.)
  nscolor underColor, overColor, strikeColor;
  PRUint8 decorations = NS_STYLE_TEXT_DECORATION_NONE;
  GetTextDecorations(PresContext(), aLine != nsnull, decorations, underColor, 
                     overColor, strikeColor);

  if (decorations == NS_STYLE_TEXT_DECORATION_NONE)
    return NS_OK;

  // The text-shadow spec says that any text decorations must also have a shadow applied to
  // it. So draw the shadows as part of the display list.
  const nsStyleText* textStyle = GetStyleText();

  if (textStyle->mTextShadow) {
    for (PRUint32 i = textStyle->mTextShadow->Length(); i > 0; --i) {
      nsCSSShadowItem* shadow = textStyle->mTextShadow->ShadowAt(i - 1);
      nscoord blurRadius = shadow->mRadius;
      nscolor shadowColor;

      if (shadow->mHasColor)
        shadowColor = shadow->mColor;
      else
        shadowColor = GetStyleColor()->mColor;

      nsPoint offset(shadow->mXOffset, shadow->mYOffset);

      // Add it to the display list so it is painted underneath the text and all decorations
      nsresult rv = aBelowTextDecorations->AppendNewToTop(new (aBuilder)
        nsDisplayTextShadow(this, decorations, shadowColor, aLine, blurRadius, offset));
      NS_ENSURE_SUCCESS(rv, rv);
    }
  }

  if (decorations & NS_STYLE_TEXT_DECORATION_UNDERLINE) {
    nsresult rv = aBelowTextDecorations->AppendNewToTop(new (aBuilder)
      nsDisplayTextDecoration(this, NS_STYLE_TEXT_DECORATION_UNDERLINE, underColor, aLine));
    NS_ENSURE_SUCCESS(rv, rv);
  }
  if (decorations & NS_STYLE_TEXT_DECORATION_OVERLINE) {
    nsresult rv = aBelowTextDecorations->AppendNewToTop(new (aBuilder)
      nsDisplayTextDecoration(this, NS_STYLE_TEXT_DECORATION_OVERLINE, overColor, aLine));
    NS_ENSURE_SUCCESS(rv, rv);
  }
  if (decorations & NS_STYLE_TEXT_DECORATION_LINE_THROUGH) {
    nsresult rv = aAboveTextDecorations->AppendNewToTop(new (aBuilder)
      nsDisplayTextDecoration(this, NS_STYLE_TEXT_DECORATION_LINE_THROUGH, strikeColor, aLine));
    NS_ENSURE_SUCCESS(rv, rv);
  }
  return NS_OK;
}

nsresult
nsHTMLContainerFrame::DisplayTextDecorationsAndChildren(
    nsDisplayListBuilder* aBuilder, const nsRect& aDirtyRect,
    const nsDisplayListSet& aLists)
{
  nsDisplayList aboveChildrenDecorations;
  nsresult rv = DisplayTextDecorations(aBuilder, aLists.Content(),
      &aboveChildrenDecorations, nsnull);
  NS_ENSURE_SUCCESS(rv, rv);
  
  rv = BuildDisplayListForNonBlockChildren(aBuilder, aDirtyRect, aLists,
                                           DISPLAY_CHILD_INLINE);
  NS_ENSURE_SUCCESS(rv, rv);
  
  aLists.Content()->AppendToTop(&aboveChildrenDecorations);
  return NS_OK;
}

NS_IMETHODIMP
nsHTMLContainerFrame::BuildDisplayList(nsDisplayListBuilder*   aBuilder,
                                       const nsRect&           aDirtyRect,
                                       const nsDisplayListSet& aLists) {
  nsresult rv = DisplayBorderBackgroundOutline(aBuilder, aLists);
  NS_ENSURE_SUCCESS(rv, rv);

  return DisplayTextDecorationsAndChildren(aBuilder, aDirtyRect, aLists);
}

static PRBool 
HasTextFrameDescendantOrInFlow(nsIFrame* aFrame);

/*virtual*/ void
nsHTMLContainerFrame::PaintTextDecorationLine(
                   gfxContext* aCtx, 
                   const nsPoint& aPt,
                   nsLineBox* aLine,
                   nscolor aColor, 
                   gfxFloat aOffset, 
                   gfxFloat aAscent, 
                   gfxFloat aSize,
                   const PRUint8 aDecoration) 
{
  NS_ASSERTION(!aLine, "Should not have passed a linebox to a non-block frame");
  nsMargin bp = GetUsedBorderAndPadding();
  PRIntn skip = GetSkipSides();
  NS_FOR_CSS_SIDES(side) {
    if (skip & (1 << side)) {
      bp.side(side) = 0;
    }
  }
  nscoord innerWidth = mRect.width - bp.left - bp.right;
  gfxPoint pt(PresContext()->AppUnitsToGfxUnits(bp.left + aPt.x),
              PresContext()->AppUnitsToGfxUnits(bp.top + aPt.y));
  gfxSize size(PresContext()->AppUnitsToGfxUnits(innerWidth), aSize);
  nsCSSRendering::PaintDecorationLine(aCtx, aColor, pt, size, aAscent, aOffset,
                    aDecoration, nsCSSRendering::DECORATION_STYLE_SOLID);
}

/*virtual*/ void
nsHTMLContainerFrame::AdjustForTextIndent(const nsLineBox* aLine,
                                          nscoord& start,
                                          nscoord& width)
{
  // This function is not for us.
  // It allows nsBlockFrame to adjust the width/X position of its
  // shadowed decorations if a text-indent rule is in effect.
}

void
nsHTMLContainerFrame::GetTextDecorations(nsPresContext* aPresContext, 
                                         PRBool aIsBlock,
                                         PRUint8& aDecorations,
                                         nscolor& aUnderColor, 
                                         nscolor& aOverColor, 
                                         nscolor& aStrikeColor)
{
  aDecorations = NS_STYLE_TEXT_DECORATION_NONE;
  if (!mStyleContext->HasTextDecorations()) {
    // This is a necessary, but not sufficient, condition for text
    // decorations.
    return; 
  }

  // A mask of all possible decorations.
  PRUint8 decorMask = NS_STYLE_TEXT_DECORATION_UNDERLINE |
                      NS_STYLE_TEXT_DECORATION_OVERLINE |
                      NS_STYLE_TEXT_DECORATION_LINE_THROUGH; 

  if (!aIsBlock) {
    aDecorations = GetStyleTextReset()->mTextDecoration  & decorMask;
    if (aDecorations) {
      const nsStyleColor* styleColor = GetStyleColor();
      aUnderColor = styleColor->mColor;
      aOverColor = styleColor->mColor;
      aStrikeColor = styleColor->mColor;
    }
  }
  else {
    // walk tree
    for (nsIFrame *frame = this; frame && decorMask; frame = frame->GetParent()) {
      // find text-decorations.  "Inherit" from parent *block* frames

      nsStyleContext* styleContext = frame->GetStyleContext();
      const nsStyleDisplay* styleDisplay = styleContext->GetStyleDisplay();
      if (!styleDisplay->IsBlockInside() &&
          styleDisplay->mDisplay != NS_STYLE_DISPLAY_TABLE_CELL &&
          styleDisplay->mDisplay != NS_STYLE_DISPLAY_TABLE_CAPTION) {
        // If an inline frame is discovered while walking up the tree,
        // we should stop according to CSS3 draft. CSS2 is rather vague
        // about this.
        break;
      }

      const nsStyleTextReset* styleText = styleContext->GetStyleTextReset();
      PRUint8 decors = decorMask & styleText->mTextDecoration;
      if (decors) {
        // A *new* text-decoration is found.
        nscolor color = styleContext->GetStyleColor()->mColor;

        if (NS_STYLE_TEXT_DECORATION_UNDERLINE & decors) {
          aUnderColor = color;
          decorMask &= ~NS_STYLE_TEXT_DECORATION_UNDERLINE;
          aDecorations |= NS_STYLE_TEXT_DECORATION_UNDERLINE;
        }
        if (NS_STYLE_TEXT_DECORATION_OVERLINE & decors) {
          aOverColor = color;
          decorMask &= ~NS_STYLE_TEXT_DECORATION_OVERLINE;
          aDecorations |= NS_STYLE_TEXT_DECORATION_OVERLINE;
        }
        if (NS_STYLE_TEXT_DECORATION_LINE_THROUGH & decors) {
          aStrikeColor = color;
          decorMask &= ~NS_STYLE_TEXT_DECORATION_LINE_THROUGH;
          aDecorations |= NS_STYLE_TEXT_DECORATION_LINE_THROUGH;
        }
      }
    }
  }
  
  if (aDecorations) {
    // If this frame contains no text, we're required to ignore this property
    if (!HasTextFrameDescendantOrInFlow(this)) {
      aDecorations = NS_STYLE_TEXT_DECORATION_NONE;
    }
  }
}

static PRBool 
HasTextFrameDescendant(nsIFrame* aParent)
{
  for (nsIFrame* kid = aParent->GetFirstChild(nsnull); kid;
       kid = kid->GetNextSibling())
  {
    if (kid->GetType() == nsGkAtoms::textFrame) {
      // This is only a candidate. We need to determine if this text
      // frame is empty, as in containing only (non-pre) whitespace.
      // See bug 20163.
      if (!kid->IsEmpty()) {
        return PR_TRUE;
      }
    }
    if (HasTextFrameDescendant(kid)) {
      return PR_TRUE;
    }
  }
  return PR_FALSE;
}

static PRBool 
HasTextFrameDescendantOrInFlow(nsIFrame* aFrame)
{
  for (nsIFrame *f = aFrame->GetFirstInFlow(); f; f = f->GetNextInFlow()) {
    if (HasTextFrameDescendant(f))
      return PR_TRUE;
  }
  return PR_FALSE;
}

/*
 * Create a next-in-flow for aFrame. Will return the newly created
 * frame in aNextInFlowResult <b>if and only if</b> a new frame is
 * created; otherwise nsnull is returned in aNextInFlowResult.
 */
nsresult
nsHTMLContainerFrame::CreateNextInFlow(nsPresContext* aPresContext,
                                       nsIFrame*       aFrame,
                                       nsIFrame*&      aNextInFlowResult)
{
  NS_PRECONDITION(GetType() != nsGkAtoms::blockFrame,
                  "you should have called nsBlockFrame::CreateContinuationFor instead");
  NS_PRECONDITION(mFrames.ContainsFrame(aFrame), "expected an in-flow child frame");

  aNextInFlowResult = nsnull;

  nsIFrame* nextInFlow = aFrame->GetNextInFlow();
  if (nsnull == nextInFlow) {
    // Create a continuation frame for the child frame and insert it
    // into our child list.
    nsresult rv = aPresContext->PresShell()->FrameConstructor()->
      CreateContinuingFrame(aPresContext, aFrame, this, &nextInFlow);
    if (NS_FAILED(rv)) {
      return rv;
    }
    mFrames.InsertFrame(nsnull, aFrame, nextInFlow);

    NS_FRAME_LOG(NS_FRAME_TRACE_NEW_FRAMES,
       ("nsHTMLContainerFrame::CreateNextInFlow: frame=%p nextInFlow=%p",
        aFrame, nextInFlow));

    aNextInFlowResult = nextInFlow;
  }
  return NS_OK;
}

static nsresult
ReparentFrameViewTo(nsIFrame*       aFrame,
                    nsIViewManager* aViewManager,
                    nsIView*        aNewParentView,
                    nsIView*        aOldParentView)
{

  // XXX What to do about placeholder views for "position: fixed" elements?
  // They should be reparented too.

  // Does aFrame have a view?
  if (aFrame->HasView()) {
#ifdef MOZ_XUL
    if (aFrame->GetType() == nsGkAtoms::menuPopupFrame) {
      // This view must be parented by the root view, don't reparent it.
      return NS_OK;
    }
#endif
    nsIView* view = aFrame->GetView();
    // Verify that the current parent view is what we think it is
    //nsIView*  parentView;
    //NS_ASSERTION(parentView == aOldParentView, "unexpected parent view");

    aViewManager->RemoveChild(view);
    
    // The view will remember the Z-order and other attributes that have been set on it.
    nsIView* insertBefore = nsLayoutUtils::FindSiblingViewFor(aNewParentView, aFrame);
    aViewManager->InsertChild(aNewParentView, view, insertBefore, insertBefore != nsnull);
  } else {
    PRInt32 listIndex = 0;
    nsIAtom* listName = nsnull;
    // This loop iterates through every child list name, and also
    // executes once with listName == nsnull.
    do {
      // Iterate the child frames, and check each child frame to see if it has
      // a view
      nsIFrame* childFrame = aFrame->GetFirstChild(listName);
      for (; childFrame; childFrame = childFrame->GetNextSibling()) {
        ReparentFrameViewTo(childFrame, aViewManager,
                            aNewParentView, aOldParentView);
      }
      listName = aFrame->GetAdditionalChildListName(listIndex++);
    } while (listName);
  }

  return NS_OK;
}

nsresult
nsHTMLContainerFrame::ReparentFrameView(nsPresContext* aPresContext,
                                        nsIFrame*       aChildFrame,
                                        nsIFrame*       aOldParentFrame,
                                        nsIFrame*       aNewParentFrame)
{
  NS_PRECONDITION(aChildFrame, "null child frame pointer");
  NS_PRECONDITION(aOldParentFrame, "null old parent frame pointer");
  NS_PRECONDITION(aNewParentFrame, "null new parent frame pointer");
  NS_PRECONDITION(aOldParentFrame != aNewParentFrame, "same old and new parent frame");

  // See if either the old parent frame or the new parent frame have a view
  while (!aOldParentFrame->HasView() && !aNewParentFrame->HasView()) {
    // Walk up both the old parent frame and the new parent frame nodes
    // stopping when we either find a common parent or views for one
    // or both of the frames.
    //
    // This works well in the common case where we push/pull and the old parent
    // frame and the new parent frame are part of the same flow. They will
    // typically be the same distance (height wise) from the
    aOldParentFrame = aOldParentFrame->GetParent();
    aNewParentFrame = aNewParentFrame->GetParent();
    
    // We should never walk all the way to the root frame without finding
    // a view
    NS_ASSERTION(aOldParentFrame && aNewParentFrame, "didn't find view");

    // See if we reached a common ancestor
    if (aOldParentFrame == aNewParentFrame) {
      break;
    }
  }

  // See if we found a common parent frame
  if (aOldParentFrame == aNewParentFrame) {
    // We found a common parent and there are no views between the old parent
    // and the common parent or the new parent frame and the common parent.
    // Because neither the old parent frame nor the new parent frame have views,
    // then any child views don't need reparenting
    return NS_OK;
  }

  // We found views for one or both of the ancestor frames before we
  // found a common ancestor.
  nsIView* oldParentView = aOldParentFrame->GetClosestView();
  nsIView* newParentView = aNewParentFrame->GetClosestView();
  
  // See if the old parent frame and the new parent frame are in the
  // same view sub-hierarchy. If they are then we don't have to do
  // anything
  if (oldParentView != newParentView) {
    // They're not so we need to reparent any child views
    return ReparentFrameViewTo(aChildFrame, oldParentView->GetViewManager(), newParentView,
                               oldParentView);
  }

  return NS_OK;
}

nsresult
nsHTMLContainerFrame::ReparentFrameViewList(nsPresContext*     aPresContext,
                                            const nsFrameList& aChildFrameList,
                                            nsIFrame*          aOldParentFrame,
                                            nsIFrame*          aNewParentFrame)
{
  NS_PRECONDITION(aChildFrameList.NotEmpty(), "empty child frame list");
  NS_PRECONDITION(aOldParentFrame, "null old parent frame pointer");
  NS_PRECONDITION(aNewParentFrame, "null new parent frame pointer");
  NS_PRECONDITION(aOldParentFrame != aNewParentFrame, "same old and new parent frame");

  // See if either the old parent frame or the new parent frame have a view
  while (!aOldParentFrame->HasView() && !aNewParentFrame->HasView()) {
    // Walk up both the old parent frame and the new parent frame nodes
    // stopping when we either find a common parent or views for one
    // or both of the frames.
    //
    // This works well in the common case where we push/pull and the old parent
    // frame and the new parent frame are part of the same flow. They will
    // typically be the same distance (height wise) from the
    aOldParentFrame = aOldParentFrame->GetParent();
    aNewParentFrame = aNewParentFrame->GetParent();
    
    // We should never walk all the way to the root frame without finding
    // a view
    NS_ASSERTION(aOldParentFrame && aNewParentFrame, "didn't find view");

    // See if we reached a common ancestor
    if (aOldParentFrame == aNewParentFrame) {
      break;
    }
  }


  // See if we found a common parent frame
  if (aOldParentFrame == aNewParentFrame) {
    // We found a common parent and there are no views between the old parent
    // and the common parent or the new parent frame and the common parent.
    // Because neither the old parent frame nor the new parent frame have views,
    // then any child views don't need reparenting
    return NS_OK;
  }

  // We found views for one or both of the ancestor frames before we
  // found a common ancestor.
  nsIView* oldParentView = aOldParentFrame->GetClosestView();
  nsIView* newParentView = aNewParentFrame->GetClosestView();
  
  // See if the old parent frame and the new parent frame are in the
  // same view sub-hierarchy. If they are then we don't have to do
  // anything
  if (oldParentView != newParentView) {
    nsIViewManager* viewManager = oldParentView->GetViewManager();

    // They're not so we need to reparent any child views
    for (nsFrameList::Enumerator e(aChildFrameList); !e.AtEnd(); e.Next()) {
      ReparentFrameViewTo(e.get(), viewManager, newParentView, oldParentView);
    }
  }

  return NS_OK;
}

nsresult
nsHTMLContainerFrame::CreateViewForFrame(nsIFrame* aFrame,
                                         PRBool aForce)
{
  if (aFrame->HasView()) {
    return NS_OK;
  }

  // If we don't yet have a view, see if we need a view
  if (!aForce && !aFrame->NeedsView()) {
    // don't need a view
    return NS_OK;
  }

  nsIView* parentView = aFrame->GetParent()->GetClosestView();
  NS_ASSERTION(parentView, "no parent with view");

  nsIViewManager* viewManager = parentView->GetViewManager();
  NS_ASSERTION(viewManager, "null view manager");
    
  // Create a view
  nsIView* view = viewManager->CreateView(aFrame->GetRect(), parentView);
  if (!view)
    return NS_ERROR_OUT_OF_MEMORY;

  SyncFrameViewProperties(aFrame->PresContext(), aFrame, nsnull, view);

  nsIView* insertBefore = nsLayoutUtils::FindSiblingViewFor(parentView, aFrame);
  // we insert this view 'above' the insertBefore view, unless insertBefore is null,
  // in which case we want to call with aAbove == PR_FALSE to insert at the beginning
  // in document order
  viewManager->InsertChild(parentView, view, insertBefore, insertBefore != nsnull);

  // REVIEW: Don't create a widget for fixed-pos elements anymore.
  // ComputeRepaintRegionForCopy will calculate the right area to repaint
  // when we scroll.
  // Reparent views on any child frames (or their descendants) to this
  // view. We can just call ReparentFrameViewTo on this frame because
  // we know this frame has no view, so it will crawl the children. Also,
  // we know that any descendants with views must have 'parentView' as their
  // parent view.
  ReparentFrameViewTo(aFrame, viewManager, view, parentView);

  // Remember our view
  aFrame->SetView(view);

  NS_FRAME_LOG(NS_FRAME_TRACE_CALLS,
               ("nsHTMLContainerFrame::CreateViewForFrame: frame=%p view=%p",
                aFrame));
  return NS_OK;
}

NS_IMPL_FRAMEARENA_HELPERS(nsHTMLContainerFrame)
