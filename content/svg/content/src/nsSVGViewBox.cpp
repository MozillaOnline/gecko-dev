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
 * The Original Code is Mozilla SVG Project code.
 *
 * The Initial Developer of the Original Code is
 * Jonathan Watt.
 * Portions created by the Initial Developer are Copyright (C) 2004
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Craig Topper <craig.topper@gmail.com> (original author)
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

#include "nsSVGViewBox.h"
#include "prdtoa.h"
#include "nsTextFormatter.h"
#ifdef MOZ_SMIL
#include "nsSMILValue.h"
#include "SVGViewBoxSMILType.h"
#endif // MOZ_SMIL

using namespace mozilla;

/* Implementation of nsSVGViewBoxRect methods */

PRBool
nsSVGViewBoxRect::operator==(const nsSVGViewBoxRect& aOther) const
{
  if (&aOther == this)
    return PR_TRUE;

  return x == aOther.x &&
    y == aOther.y &&
    width == aOther.width &&
    height == aOther.height;
}

/* Cycle collection macros for nsSVGViewBox */

NS_SVG_VAL_IMPL_CYCLE_COLLECTION(nsSVGViewBox::DOMBaseVal, mSVGElement)
NS_SVG_VAL_IMPL_CYCLE_COLLECTION(nsSVGViewBox::DOMAnimVal, mSVGElement)
NS_SVG_VAL_IMPL_CYCLE_COLLECTION(nsSVGViewBox::DOMAnimatedRect, mSVGElement)

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsSVGViewBox::DOMBaseVal)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsSVGViewBox::DOMBaseVal)

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsSVGViewBox::DOMAnimVal)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsSVGViewBox::DOMAnimVal)

NS_IMPL_CYCLE_COLLECTING_ADDREF(nsSVGViewBox::DOMAnimatedRect)
NS_IMPL_CYCLE_COLLECTING_RELEASE(nsSVGViewBox::DOMAnimatedRect)

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsSVGViewBox::DOMBaseVal)
  NS_INTERFACE_MAP_ENTRY(nsIDOMSVGRect)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_INTERFACE_MAP_ENTRY_CONTENT_CLASSINFO(SVGRect)
NS_INTERFACE_MAP_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsSVGViewBox::DOMAnimVal)
  NS_INTERFACE_MAP_ENTRY(nsIDOMSVGRect)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_INTERFACE_MAP_ENTRY_CONTENT_CLASSINFO(SVGRect)
NS_INTERFACE_MAP_END

NS_INTERFACE_MAP_BEGIN_CYCLE_COLLECTION(nsSVGViewBox::DOMAnimatedRect)
  NS_INTERFACE_MAP_ENTRY(nsIDOMSVGAnimatedRect)
  NS_INTERFACE_MAP_ENTRY(nsISupports)
  NS_INTERFACE_MAP_ENTRY_CONTENT_CLASSINFO(SVGAnimatedRect)
NS_INTERFACE_MAP_END

/* Implementation of nsSVGViewBox methods */

void
nsSVGViewBox::Init()
{
  mBaseVal = nsSVGViewBoxRect();
  mAnimVal = nsnull;
  mHasBaseVal = PR_FALSE;
}

void
nsSVGViewBox::SetAnimValue(float aX, float aY, float aWidth, float aHeight,
                           nsSVGElement *aSVGElement)
{
  if (!mAnimVal) {
    // it's okay if allocation fails - and no point in reporting that
    mAnimVal = new nsSVGViewBoxRect(aX, aY, aWidth, aHeight);
  } else {
    mAnimVal->x = aX;
    mAnimVal->y = aY;
    mAnimVal->width = aWidth;
    mAnimVal->height = aHeight;
  }
  aSVGElement->DidAnimateViewBox();
}

void
nsSVGViewBox::SetBaseValue(float aX, float aY, float aWidth, float aHeight,
                           nsSVGElement *aSVGElement, PRBool aDoSetAttr)
{
  mBaseVal = nsSVGViewBoxRect(aX, aY, aWidth, aHeight);
  mHasBaseVal = PR_TRUE;

  aSVGElement->DidChangeViewBox(aDoSetAttr);
#ifdef MOZ_SMIL
  if (mAnimVal) {
    aSVGElement->AnimationNeedsResample();
  }
#endif
}

static nsresult
ToSVGViewBoxRect(const nsAString& aStr, nsSVGViewBoxRect *aViewBox)
{
  nsresult rv = NS_OK;

  char *str = ToNewUTF8String(aStr);

  char *rest = str;
  char *token;
  const char *delimiters = ",\x20\x9\xD\xA";

  float vals[4];
  PRUint32 i;
  for (i = 0; i < 4; ++i) {
    if (!(token = nsCRT::strtok(rest, delimiters, &rest))) break; // parse error

    char *end;
    vals[i] = float(PR_strtod(token, &end));
    if (*end != '\0' || !NS_FloatIsFinite(vals[i])) break; // parse error
  }
  if (i!=4 || nsCRT::strtok(rest, delimiters, &rest)!=0) {
    // there was a parse error.
    rv = NS_ERROR_DOM_SYNTAX_ERR;
  } else {
    aViewBox->x = vals[0];
    aViewBox->y = vals[1];
    aViewBox->width = vals[2];
    aViewBox->height = vals[3];
  }

  nsMemory::Free(str);

  return rv;
}

nsresult
nsSVGViewBox::SetBaseValueString(const nsAString& aValue,
                                 nsSVGElement *aSVGElement,
                                 PRBool aDoSetAttr)
{
  nsSVGViewBoxRect viewBox;
  nsresult res = ToSVGViewBoxRect(aValue, &viewBox);
  if (NS_SUCCEEDED(res)) {
    SetBaseValue(viewBox.x, viewBox.y, viewBox.width, viewBox.height, aSVGElement, aDoSetAttr);
  }
  return res;
}

void
nsSVGViewBox::GetBaseValueString(nsAString& aValue) const
{
  PRUnichar buf[200];
  nsTextFormatter::snprintf(buf, sizeof(buf)/sizeof(PRUnichar),
                            NS_LITERAL_STRING("%g %g %g %g").get(),
                            (double)mBaseVal.x, (double)mBaseVal.y,
                            (double)mBaseVal.width, (double)mBaseVal.height);
  aValue.Assign(buf);
}

nsresult
nsSVGViewBox::ToDOMAnimatedRect(nsIDOMSVGAnimatedRect **aResult,
                                nsSVGElement* aSVGElement)
{
  *aResult = new DOMAnimatedRect(this, aSVGElement);
  NS_ENSURE_TRUE(*aResult, NS_ERROR_OUT_OF_MEMORY);

  NS_ADDREF(*aResult);
  return NS_OK;
}

NS_IMETHODIMP
nsSVGViewBox::DOMAnimatedRect::GetBaseVal(nsIDOMSVGRect **aResult)
{
  *aResult = new nsSVGViewBox::DOMBaseVal(mVal, mSVGElement);
  NS_ENSURE_TRUE(*aResult, NS_ERROR_OUT_OF_MEMORY);

  NS_ADDREF(*aResult);
  return NS_OK;
}

NS_IMETHODIMP
nsSVGViewBox::DOMAnimatedRect::GetAnimVal(nsIDOMSVGRect **aResult)
{
  *aResult = new nsSVGViewBox::DOMAnimVal(mVal, mSVGElement);
  NS_ENSURE_TRUE(*aResult, NS_ERROR_OUT_OF_MEMORY);

  NS_ADDREF(*aResult);
  return NS_OK;
}

NS_IMETHODIMP
nsSVGViewBox::DOMBaseVal::SetX(float aX)
{
  nsSVGViewBoxRect rect = mVal->GetBaseValue();
  rect.x = aX;
  mVal->SetBaseValue(rect.x, rect.y, rect.width, rect.height,
                     mSVGElement, PR_TRUE);
  return NS_OK;
}

NS_IMETHODIMP
nsSVGViewBox::DOMBaseVal::SetY(float aY)
{
  nsSVGViewBoxRect rect = mVal->GetBaseValue();
  rect.y = aY;
  mVal->SetBaseValue(rect.x, rect.y, rect.width, rect.height,
                     mSVGElement, PR_TRUE);
  return NS_OK;
}

NS_IMETHODIMP
nsSVGViewBox::DOMBaseVal::SetWidth(float aWidth)
{
  nsSVGViewBoxRect rect = mVal->GetBaseValue();
  rect.width = aWidth;
  mVal->SetBaseValue(rect.x, rect.y, rect.width, rect.height,
                     mSVGElement, PR_TRUE);
  return NS_OK;
}

NS_IMETHODIMP
nsSVGViewBox::DOMBaseVal::SetHeight(float aHeight)
{
  nsSVGViewBoxRect rect = mVal->GetBaseValue();
  rect.height = aHeight;
  mVal->SetBaseValue(rect.x, rect.y, rect.width, rect.height,
                     mSVGElement, PR_TRUE);
  return NS_OK;
}

#ifdef MOZ_SMIL
nsISMILAttr*
nsSVGViewBox::ToSMILAttr(nsSVGElement *aSVGElement)
{
  return new SMILViewBox(this, aSVGElement);
}

nsresult
nsSVGViewBox::SMILViewBox
            ::ValueFromString(const nsAString& aStr,
                              const nsISMILAnimationElement* /*aSrcElement*/,
                              nsSMILValue& aValue,
                              PRBool& aCanCache) const
{
  nsSVGViewBoxRect viewBox;
  nsresult res = ToSVGViewBoxRect(aStr, &viewBox);
  if (NS_FAILED(res)) {
    return res;
  }
  nsSMILValue val(&SVGViewBoxSMILType::sSingleton);
  // Check for OOM when the nsSMILValue ctor called SVGViewBoxSMILType::Init:
  if (val.IsNull()) {
    return NS_ERROR_OUT_OF_MEMORY;
  }
  *static_cast<nsSVGViewBoxRect*>(val.mU.mPtr) = viewBox;
  aValue = val;
  aCanCache = PR_TRUE;
  
  return NS_OK;
}

nsSMILValue
nsSVGViewBox::SMILViewBox::GetBaseValue() const
{
  nsSMILValue val(&SVGViewBoxSMILType::sSingleton);
  // Check for OOM when the nsSMILValue ctor called SVGViewBoxSMILType::Init:
  if (!val.IsNull()) {
    *static_cast<nsSVGViewBoxRect*>(val.mU.mPtr) = mVal->mBaseVal;
  }
  return val;
}

void
nsSVGViewBox::SMILViewBox::ClearAnimValue()
{
  if (mVal->mAnimVal) {
    mVal->mAnimVal = nsnull;
    mSVGElement->DidAnimateViewBox();
  }
}

nsresult
nsSVGViewBox::SMILViewBox::SetAnimValue(const nsSMILValue& aValue)
{
  NS_ASSERTION(aValue.mType == &SVGViewBoxSMILType::sSingleton,
               "Unexpected type to assign animated value");
  if (aValue.mType == &SVGViewBoxSMILType::sSingleton) {
    nsSVGViewBoxRect &vb = *static_cast<nsSVGViewBoxRect*>(aValue.mU.mPtr);
    mVal->SetAnimValue(vb.x, vb.y, vb.width, vb.height, mSVGElement);
  }
  return NS_OK;
}
#endif // MOZ_SMIL
