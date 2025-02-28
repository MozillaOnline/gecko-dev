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
 *   Daniel Glazman <glazman@netscape.com>
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
 * temporary (expanded) representation of the property-value pairs
 * within a CSS declaration using during parsing and mutation, and
 * representation of complex values for CSS properties
 */

#ifndef nsCSSStruct_h___
#define nsCSSStruct_h___

#include "nsCSSValue.h"
#include "nsStyleConsts.h"

// Prefer nsCSSValue::Array for lists of fixed size.
struct nsCSSValueList {
  nsCSSValueList() : mNext(nsnull) { MOZ_COUNT_CTOR(nsCSSValueList); }
  ~nsCSSValueList();

  nsCSSValueList* Clone() const { return Clone(PR_TRUE); }

  static PRBool Equal(nsCSSValueList* aList1, nsCSSValueList* aList2);

  nsCSSValue      mValue;
  nsCSSValueList* mNext;

private:
  nsCSSValueList(const nsCSSValueList& aCopy) // makes a shallow copy
    : mValue(aCopy.mValue), mNext(nsnull)
  {
    MOZ_COUNT_CTOR(nsCSSValueList);
  }
  nsCSSValueList* Clone(PRBool aDeep) const;
};

struct nsCSSRect {
  nsCSSRect(void);
  nsCSSRect(const nsCSSRect& aCopy);
  ~nsCSSRect();

  PRBool operator==(const nsCSSRect& aOther) const {
    return mTop == aOther.mTop &&
           mRight == aOther.mRight &&
           mBottom == aOther.mBottom &&
           mLeft == aOther.mLeft;
  }

  PRBool operator!=(const nsCSSRect& aOther) const {
    return mTop != aOther.mTop ||
           mRight != aOther.mRight ||
           mBottom != aOther.mBottom ||
           mLeft != aOther.mLeft;
  }

  void SetAllSidesTo(const nsCSSValue& aValue);

  void Reset() {
    mTop.Reset();
    mRight.Reset();
    mBottom.Reset();
    mLeft.Reset();
  }

  PRBool HasValue() const {
    return
      mTop.GetUnit() != eCSSUnit_Null ||
      mRight.GetUnit() != eCSSUnit_Null ||
      mBottom.GetUnit() != eCSSUnit_Null ||
      mLeft.GetUnit() != eCSSUnit_Null;
  }
  
  nsCSSValue mTop;
  nsCSSValue mRight;
  nsCSSValue mBottom;
  nsCSSValue mLeft;

  typedef nsCSSValue nsCSSRect::*side_type;
  static const side_type sides[4];
};

struct nsCSSValuePair {
  nsCSSValuePair()
  {
    MOZ_COUNT_CTOR(nsCSSValuePair);
  }
  nsCSSValuePair(const nsCSSValuePair& aCopy)
    : mXValue(aCopy.mXValue),
      mYValue(aCopy.mYValue)
  { 
    MOZ_COUNT_CTOR(nsCSSValuePair);
  }
  ~nsCSSValuePair()
  {
    MOZ_COUNT_DTOR(nsCSSValuePair);
  }

  PRBool operator==(const nsCSSValuePair& aOther) const {
    return mXValue == aOther.mXValue &&
           mYValue == aOther.mYValue;
  }

  PRBool operator!=(const nsCSSValuePair& aOther) const {
    return mXValue != aOther.mXValue ||
           mYValue != aOther.mYValue;
  }

  void SetBothValuesTo(const nsCSSValue& aValue) {
    mXValue = aValue;
    mYValue = aValue;
  }

  void Reset() {
    mXValue.Reset();
    mYValue.Reset();
  }

  PRBool HasValue() const {
    return mXValue.GetUnit() != eCSSUnit_Null ||
           mYValue.GetUnit() != eCSSUnit_Null;
  }

  nsCSSValue mXValue;
  nsCSSValue mYValue;
};

struct nsCSSCornerSizes {
  nsCSSCornerSizes(void);
  nsCSSCornerSizes(const nsCSSCornerSizes& aCopy);
  ~nsCSSCornerSizes();

  // argument is a "full corner" constant from nsStyleConsts.h
  nsCSSValuePair const & GetFullCorner(PRUint32 aCorner) const {
    return (this->*corners[aCorner]);
  }
  nsCSSValuePair & GetFullCorner(PRUint32 aCorner) {
    return (this->*corners[aCorner]);
  }

  // argument is a "half corner" constant from nsStyleConsts.h
  const nsCSSValue& GetHalfCorner(PRUint32 hc) const {
    nsCSSValuePair const & fc = this->*corners[NS_HALF_TO_FULL_CORNER(hc)];
    return NS_HALF_CORNER_IS_X(hc) ? fc.mXValue : fc.mYValue;
  }
  nsCSSValue & GetHalfCorner(PRUint32 hc) {
    nsCSSValuePair& fc = this->*corners[NS_HALF_TO_FULL_CORNER(hc)];
    return NS_HALF_CORNER_IS_X(hc) ? fc.mXValue : fc.mYValue;
  }
  
  PRBool operator==(const nsCSSCornerSizes& aOther) const {
    NS_FOR_CSS_FULL_CORNERS(corner) {
      if (this->GetFullCorner(corner) != aOther.GetFullCorner(corner))
        return PR_FALSE;
    }
    return PR_TRUE;
  }

  PRBool operator!=(const nsCSSCornerSizes& aOther) const {
    NS_FOR_CSS_FULL_CORNERS(corner) {
      if (this->GetFullCorner(corner) != aOther.GetFullCorner(corner))
        return PR_TRUE;
    }
    return PR_FALSE;
  }

  PRBool HasValue() const {
    NS_FOR_CSS_FULL_CORNERS(corner) {
      if (this->GetFullCorner(corner).HasValue())
        return PR_TRUE;
    }
    return PR_FALSE;
  }

  void SetAllCornersTo(const nsCSSValue& aValue);
  void Reset();

  nsCSSValuePair mTopLeft;
  nsCSSValuePair mTopRight;
  nsCSSValuePair mBottomRight;
  nsCSSValuePair mBottomLeft;

protected:
  typedef nsCSSValuePair nsCSSCornerSizes::*corner_type;
  static const corner_type corners[4];
};

struct nsCSSValueListRect {
  nsCSSValueListRect(void);
  nsCSSValueListRect(const nsCSSValueListRect& aCopy);
  ~nsCSSValueListRect();

  nsCSSValueList* mTop;
  nsCSSValueList* mRight;
  nsCSSValueList* mBottom;
  nsCSSValueList* mLeft;

  typedef nsCSSValueList* nsCSSValueListRect::*side_type;
  static const side_type sides[4];
};

// Maybe should be replaced with nsCSSValueList and nsCSSValue::Array?
struct nsCSSValuePairList {
  nsCSSValuePairList() : mNext(nsnull) { MOZ_COUNT_CTOR(nsCSSValuePairList); }
  ~nsCSSValuePairList();

  nsCSSValuePairList* Clone() const { return Clone(PR_TRUE); }

  static PRBool Equal(nsCSSValuePairList* aList1, nsCSSValuePairList* aList2);

  nsCSSValue          mXValue;
  nsCSSValue          mYValue;
  nsCSSValuePairList* mNext;

private:
  nsCSSValuePairList(const nsCSSValuePairList& aCopy) // makes a shallow copy
    : mXValue(aCopy.mXValue), mYValue(aCopy.mYValue), mNext(nsnull)
  {
    MOZ_COUNT_CTOR(nsCSSValuePairList);
  }
  nsCSSValuePairList* Clone(PRBool aDeep) const;
};

/****************************************************************************/

struct nsCSSStruct {
  // EMPTY on purpose.  ABSTRACT with no virtuals (typedef void nsCSSStruct?)
};

// We use the nsCSS* structures for storing nsCSSDeclaration's
// *temporary* data during parsing and modification.  (They are too big
// for permanent storage.)  We also use them for nsRuleData, with some
// additions of things that the style system must cascade, but that
// aren't CSS properties.  Thus we use typedefs and inheritance
// (forwards, when the rule data needs extra data) to make the rule data
// structs from the declaration structs.
// NOTE:  For compilation speed, this typedef also appears in nsRuleNode.h
typedef nsCSSStruct nsRuleDataStruct;


struct nsCSSFont : public nsCSSStruct {
  nsCSSFont(void);
  ~nsCSSFont(void);

  nsCSSValue mSystemFont;
  nsCSSValue mFamily;
  nsCSSValue mStyle;
  nsCSSValue mVariant;
  nsCSSValue mWeight;
  nsCSSValue mSize;
  nsCSSValue mSizeAdjust; // NEW
  nsCSSValue mStretch; // NEW

#ifdef MOZ_MATHML
  nsCSSValue mScriptLevel; // Integer values mean "relative", Number values mean "absolute" 
  nsCSSValue mScriptSizeMultiplier;
  nsCSSValue mScriptMinSize;
#endif

private:
  nsCSSFont(const nsCSSFont& aOther); // NOT IMPLEMENTED
};

struct nsRuleDataFont : public nsCSSFont {
  PRBool mFamilyFromHTML; // Is the family from an HTML FONT element
  nsRuleDataFont() {}
private:
  nsRuleDataFont(const nsRuleDataFont& aOther); // NOT IMPLEMENTED
};

struct nsCSSColor : public nsCSSStruct  {
  nsCSSColor(void);
  ~nsCSSColor(void);

  nsCSSValue      mColor;
  nsCSSValue      mBackColor;
  nsCSSValueList* mBackImage;
  nsCSSValueList* mBackRepeat;
  nsCSSValueList* mBackAttachment;
  nsCSSValuePairList* mBackPosition;
  nsCSSValuePairList* mBackSize;
  nsCSSValueList* mBackClip;
  nsCSSValueList* mBackOrigin;
  nsCSSValue      mBackInlinePolicy;
private:
  nsCSSColor(const nsCSSColor& aOther); // NOT IMPLEMENTED
};

struct nsRuleDataColor : public nsCSSColor {
  nsRuleDataColor() {}

  // A little bit of a hack here:  now that background-image is
  // represented by a value list, attribute mapping code needs a place
  // to store one item in a value list in order to map a simple value.
  nsCSSValueList mTempBackImage;
private:
  nsRuleDataColor(const nsRuleDataColor& aOther); // NOT IMPLEMENTED
};

struct nsCSSText : public nsCSSStruct  {
  nsCSSText(void);
  ~nsCSSText(void);

  nsCSSValue mTabSize;
  nsCSSValue mWordSpacing;
  nsCSSValue mLetterSpacing;
  nsCSSValue mVerticalAlign;
  nsCSSValue mTextTransform;
  nsCSSValue mTextAlign;
  nsCSSValue mTextIndent;
  nsCSSValue mDecoration;
  nsCSSValueList* mTextShadow; // NEW
  nsCSSValue mUnicodeBidi;  // NEW
  nsCSSValue mLineHeight;
  nsCSSValue mWhiteSpace;
  nsCSSValue mWordWrap;
private:
  nsCSSText(const nsCSSText& aOther); // NOT IMPLEMENTED
};

struct nsRuleDataText : public nsCSSText {
  nsRuleDataText() {}
private:
  nsRuleDataText(const nsRuleDataText& aOther); // NOT IMPLEMENTED
};

struct nsCSSDisplay : public nsCSSStruct  {
  nsCSSDisplay(void);
  ~nsCSSDisplay(void);

  nsCSSValue mDirection;
  nsCSSValue mDisplay;
  nsCSSValue mBinding;
  nsCSSValue mAppearance;
  nsCSSValue mPosition;
  nsCSSValue mFloat;
  nsCSSValue mClear;
  nsCSSRect  mClip;
  nsCSSValue mOverflowX;
  nsCSSValue mOverflowY;
  nsCSSValue mPointerEvents;
  nsCSSValue mVisibility;
  nsCSSValue mOpacity;
  nsCSSValueList *mTransform; // List of Arrays containing transform information
  nsCSSValuePair mTransformOrigin;
  nsCSSValueList* mTransitionProperty;
  nsCSSValueList* mTransitionDuration;
  nsCSSValueList* mTransitionTimingFunction;
  nsCSSValueList* mTransitionDelay;

  // temp fix for bug 24000 
  nsCSSValue mBreakBefore;
  nsCSSValue mBreakAfter;
  // end temp fix
private:
  nsCSSDisplay(const nsCSSDisplay& aOther); // NOT IMPLEMENTED
};

struct nsRuleDataDisplay : public nsCSSDisplay {
  nsCSSValue mLang;
  nsRuleDataDisplay() {}
private:
  nsRuleDataDisplay(const nsRuleDataDisplay& aOther); // NOT IMPLEMENTED
};

struct nsCSSMargin : public nsCSSStruct  {
  nsCSSMargin(void);
  ~nsCSSMargin(void);

  nsCSSRect   mMargin;
  nsCSSValue  mMarginStart;
  nsCSSValue  mMarginEnd;
  nsCSSValue  mMarginLeftLTRSource;
  nsCSSValue  mMarginLeftRTLSource;
  nsCSSValue  mMarginRightLTRSource;
  nsCSSValue  mMarginRightRTLSource;
  nsCSSRect   mPadding;
  nsCSSValue  mPaddingStart;
  nsCSSValue  mPaddingEnd;
  nsCSSValue  mPaddingLeftLTRSource;
  nsCSSValue  mPaddingLeftRTLSource;
  nsCSSValue  mPaddingRightLTRSource;
  nsCSSValue  mPaddingRightRTLSource;
  nsCSSRect   mBorderWidth;
  nsCSSValue  mBorderStartWidth;
  nsCSSValue  mBorderEndWidth;
  nsCSSValue  mBorderLeftWidthLTRSource;
  nsCSSValue  mBorderLeftWidthRTLSource;
  nsCSSValue  mBorderRightWidthLTRSource;
  nsCSSValue  mBorderRightWidthRTLSource;
  nsCSSRect   mBorderColor;
  nsCSSValue  mBorderStartColor;
  nsCSSValue  mBorderEndColor;
  nsCSSValue  mBorderLeftColorLTRSource;
  nsCSSValue  mBorderLeftColorRTLSource;
  nsCSSValue  mBorderRightColorLTRSource;
  nsCSSValue  mBorderRightColorRTLSource;
  nsCSSValueListRect mBorderColors;
  nsCSSRect   mBorderStyle;
  nsCSSValue  mBorderStartStyle;
  nsCSSValue  mBorderEndStyle;
  nsCSSValue  mBorderLeftStyleLTRSource;
  nsCSSValue  mBorderLeftStyleRTLSource;
  nsCSSValue  mBorderRightStyleLTRSource;
  nsCSSValue  mBorderRightStyleRTLSource;
  nsCSSCornerSizes mBorderRadius;
  nsCSSValue  mOutlineWidth;
  nsCSSValue  mOutlineColor;
  nsCSSValue  mOutlineStyle;
  nsCSSValue  mOutlineOffset;
  nsCSSCornerSizes mOutlineRadius;
  nsCSSValue  mFloatEdge; // NEW
  nsCSSValue  mBorderImage;
  nsCSSValueList* mBoxShadow;
private:
  nsCSSMargin(const nsCSSMargin& aOther); // NOT IMPLEMENTED
};

struct nsRuleDataMargin : public nsCSSMargin {
  nsRuleDataMargin() {}
private:
  nsRuleDataMargin(const nsRuleDataMargin& aOther); // NOT IMPLEMENTED
};

struct nsCSSPosition : public nsCSSStruct  {
  nsCSSPosition(void);
  ~nsCSSPosition(void);

  nsCSSValue  mWidth;
  nsCSSValue  mMinWidth;
  nsCSSValue  mMaxWidth;
  nsCSSValue  mHeight;
  nsCSSValue  mMinHeight;
  nsCSSValue  mMaxHeight;
  nsCSSValue  mBoxSizing; // NEW
  nsCSSRect   mOffset;
  nsCSSValue  mZIndex;
private:
  nsCSSPosition(const nsCSSPosition& aOther); // NOT IMPLEMENTED
};

struct nsRuleDataPosition : public nsCSSPosition {
  nsRuleDataPosition() {}
private:
  nsRuleDataPosition(const nsRuleDataPosition& aOther); // NOT IMPLEMENTED
};

struct nsCSSList : public nsCSSStruct  {
  nsCSSList(void);
  ~nsCSSList(void);

  nsCSSValue mType;
  nsCSSValue mImage;
  nsCSSValue mPosition;
  nsCSSRect  mImageRegion;
private:
  nsCSSList(const nsCSSList& aOther); // NOT IMPLEMENTED
};

struct nsRuleDataList : public nsCSSList {
  nsRuleDataList() {}
private:
  nsRuleDataList(const nsRuleDataList& aOther); // NOT IMPLEMENTED
};

struct nsCSSTable : public nsCSSStruct  { // NEW
  nsCSSTable(void);
  ~nsCSSTable(void);

  nsCSSValue mBorderCollapse;
  nsCSSValuePair mBorderSpacing;
  nsCSSValue mCaptionSide;
  nsCSSValue mEmptyCells;
  
  nsCSSValue mLayout;
  nsCSSValue mSpan; // Not mappable via CSS, only using HTML4 table attrs.
  nsCSSValue mCols; // Not mappable via CSS, only using HTML4 table attrs.
private:
  nsCSSTable(const nsCSSTable& aOther); // NOT IMPLEMENTED
};

struct nsRuleDataTable : public nsCSSTable {
  nsRuleDataTable() {}
private:
  nsRuleDataTable(const nsRuleDataTable& aOther); // NOT IMPLEMENTED
};

struct nsCSSBreaks : public nsCSSStruct  { // NEW
  nsCSSBreaks(void);
  ~nsCSSBreaks(void);

  nsCSSValue mOrphans;
  nsCSSValue mWidows;
  nsCSSValue mPage;
  // temp fix for bug 24000 
  //nsCSSValue mPageBreakAfter;
  //nsCSSValue mPageBreakBefore;
  nsCSSValue mPageBreakInside;
private:
  nsCSSBreaks(const nsCSSBreaks& aOther); // NOT IMPLEMENTED
};

struct nsRuleDataBreaks : public nsCSSBreaks {
  nsRuleDataBreaks() {}
private:
  nsRuleDataBreaks(const nsRuleDataBreaks& aOther); // NOT IMPLEMENTED
};

struct nsCSSPage : public nsCSSStruct  { // NEW
  nsCSSPage(void);
  ~nsCSSPage(void);

  nsCSSValue mMarks;
  nsCSSValuePair mSize;
private:
  nsCSSPage(const nsCSSPage& aOther); // NOT IMPLEMENTED
};

struct nsRuleDataPage : public nsCSSPage {
  nsRuleDataPage() {}
private:
  nsRuleDataPage(const nsRuleDataPage& aOther); // NOT IMPLEMENTED
};

struct nsCSSContent : public nsCSSStruct  {
  nsCSSContent(void);
  ~nsCSSContent(void);

  nsCSSValueList*     mContent;
  nsCSSValuePairList* mCounterIncrement;
  nsCSSValuePairList* mCounterReset;
  nsCSSValue          mMarkerOffset;
  nsCSSValuePairList* mQuotes;
private:
  nsCSSContent(const nsCSSContent& aOther); // NOT IMPLEMENTED
};

struct nsRuleDataContent : public nsCSSContent {
  nsRuleDataContent() {}
private:
  nsRuleDataContent(const nsRuleDataContent& aOther); // NOT IMPLEMENTED
};

struct nsCSSUserInterface : public nsCSSStruct  { // NEW
  nsCSSUserInterface(void);
  ~nsCSSUserInterface(void);

  nsCSSValue      mUserInput;
  nsCSSValue      mUserModify;
  nsCSSValue      mUserSelect;
  nsCSSValue      mUserFocus;
  
  nsCSSValueList* mCursor;
  nsCSSValue      mForceBrokenImageIcon;
  nsCSSValue      mIMEMode;
  nsCSSValue      mWindowShadow;
private:
  nsCSSUserInterface(const nsCSSUserInterface& aOther); // NOT IMPLEMENTED
};

struct nsRuleDataUserInterface : public nsCSSUserInterface {
  nsRuleDataUserInterface() {}
private:
  nsRuleDataUserInterface(const nsRuleDataUserInterface& aOther); // NOT IMPLEMENTED
};

struct nsCSSAural : public nsCSSStruct  { // NEW
  nsCSSAural(void);
  ~nsCSSAural(void);

  nsCSSValue mAzimuth;
  nsCSSValue mElevation;
  nsCSSValue mCueAfter;
  nsCSSValue mCueBefore;
  nsCSSValue mPauseAfter;
  nsCSSValue mPauseBefore;
  nsCSSValue mPitch;
  nsCSSValue mPitchRange;
  nsCSSValue mRichness;
  nsCSSValue mSpeak;
  nsCSSValue mSpeakHeader;
  nsCSSValue mSpeakNumeral;
  nsCSSValue mSpeakPunctuation;
  nsCSSValue mSpeechRate;
  nsCSSValue mStress;
  nsCSSValue mVoiceFamily;
  nsCSSValue mVolume;
private:
  nsCSSAural(const nsCSSAural& aOther); // NOT IMPLEMENTED
};

struct nsRuleDataAural : public nsCSSAural {
  nsRuleDataAural() {}
private:
  nsRuleDataAural(const nsRuleDataAural& aOther); // NOT IMPLEMENTED
};

struct nsCSSXUL : public nsCSSStruct  {
  nsCSSXUL(void);
  ~nsCSSXUL(void);

  nsCSSValue  mBoxAlign;
  nsCSSValue  mBoxDirection;
  nsCSSValue  mBoxFlex;
  nsCSSValue  mBoxOrient;
  nsCSSValue  mBoxPack;
  nsCSSValue  mBoxOrdinal;
  nsCSSValue  mStackSizing;
private:
  nsCSSXUL(const nsCSSXUL& aOther); // NOT IMPLEMENTED
};

struct nsRuleDataXUL : public nsCSSXUL {
  nsRuleDataXUL() {}
private:
  nsRuleDataXUL(const nsRuleDataXUL& aOther); // NOT IMPLEMENTED
};

struct nsCSSColumn : public nsCSSStruct  {
  nsCSSColumn(void);
  ~nsCSSColumn(void);

  nsCSSValue  mColumnCount;
  nsCSSValue  mColumnWidth;
  nsCSSValue  mColumnGap;
  nsCSSValue  mColumnRuleColor;
  nsCSSValue  mColumnRuleWidth;
  nsCSSValue  mColumnRuleStyle;
private:
  nsCSSColumn(const nsCSSColumn& aOther); // NOT IMPLEMENTED
};

struct nsRuleDataColumn : public nsCSSColumn {
  nsRuleDataColumn() {}
private:
  nsRuleDataColumn(const nsRuleDataColumn& aOther); // NOT IMPLEMENTED
};

struct nsCSSSVG : public nsCSSStruct {
  nsCSSSVG(void);
  ~nsCSSSVG(void);

  nsCSSValue mClipPath;
  nsCSSValue mClipRule;
  nsCSSValue mColorInterpolation;
  nsCSSValue mColorInterpolationFilters;
  nsCSSValue mDominantBaseline;
  nsCSSValuePair mFill;
  nsCSSValue mFillOpacity;
  nsCSSValue mFillRule;
  nsCSSValue mFilter;
  nsCSSValue mFloodColor;
  nsCSSValue mFloodOpacity;
  nsCSSValue mImageRendering;
  nsCSSValue mLightingColor;
  nsCSSValue mMarkerEnd;
  nsCSSValue mMarkerMid;
  nsCSSValue mMarkerStart;
  nsCSSValue mMask;
  nsCSSValue mShapeRendering;
  nsCSSValue mStopColor;
  nsCSSValue mStopOpacity;
  nsCSSValuePair mStroke;
  nsCSSValueList *mStrokeDasharray;
  nsCSSValue mStrokeDashoffset;
  nsCSSValue mStrokeLinecap;
  nsCSSValue mStrokeLinejoin;
  nsCSSValue mStrokeMiterlimit;
  nsCSSValue mStrokeOpacity;
  nsCSSValue mStrokeWidth;
  nsCSSValue mTextAnchor;
  nsCSSValue mTextRendering;
private:
  nsCSSSVG(const nsCSSSVG& aOther); // NOT IMPLEMENTED
};

struct nsRuleDataSVG : public nsCSSSVG {
  nsRuleDataSVG() {}
private:
  nsRuleDataSVG(const nsRuleDataSVG& aOther); // NOT IMPLEMENTED
};

#endif /* nsCSSStruct_h___ */
