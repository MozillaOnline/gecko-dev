/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
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
 * The Original Code is thebes gfx
 *
 * The Initial Developer of the Original Code is
 * mozilla.org.
 * Portions created by the Initial Developer are Copyright (C) 2005
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Vladimir Vukicevic <vladimir@pobox.com>
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

#ifndef NSTHEBESRENDERINGCONTEXT__H__
#define NSTHEBESRENDERINGCONTEXT__H__

#include "nsCOMPtr.h"
#include "nsTArray.h"
#include "nsIRenderingContext.h"
#include "nsIDeviceContext.h"
#include "nsIFontMetrics.h"
#include "nsIWidget.h"
#include "nsPoint.h"
#include "nsSize.h"
#include "nsColor.h"
#include "nsRect.h"
#include "nsIRegion.h"
#include "nsTransform2D.h"
#include "nsIThebesFontMetrics.h"
#include "gfxContext.h"

class nsThebesRenderingContext : public nsIRenderingContext
{
public:
    nsThebesRenderingContext();
    virtual ~nsThebesRenderingContext();

    NS_DECL_ISUPPORTS


    /**
     * Return the maximum length of a string that can be handled by the platform
     * using the current font metrics.
     */
    virtual PRInt32 GetMaxStringLength();

    // Safe string method variants: by default, these defer to the more
    // elaborate methods below
    NS_IMETHOD GetWidth(const nsString& aString, nscoord &aWidth,
                        PRInt32 *aFontID = nsnull);
    NS_IMETHOD GetWidth(const char* aString, nscoord& aWidth);
    NS_IMETHOD DrawString(const nsString& aString, nscoord aX, nscoord aY,
                          PRInt32 aFontID = -1,
                          const nscoord* aSpacing = nsnull);

    // Safe string methods
    NS_IMETHOD GetWidth(const char* aString, PRUint32 aLength,
                        nscoord& aWidth);
    NS_IMETHOD GetWidth(const PRUnichar *aString, PRUint32 aLength,
                        nscoord &aWidth, PRInt32 *aFontID = nsnull);
    NS_IMETHOD GetWidth(char aC, nscoord &aWidth);
    NS_IMETHOD GetWidth(PRUnichar aC, nscoord &aWidth,
                        PRInt32 *aFontID);

    NS_IMETHOD GetTextDimensions(const char* aString, PRUint32 aLength,
                                 nsTextDimensions& aDimensions);
    NS_IMETHOD GetTextDimensions(const PRUnichar* aString, PRUint32 aLength,
                                 nsTextDimensions& aDimensions, PRInt32* aFontID = nsnull);

#if defined(_WIN32) || defined(XP_OS2) || defined(MOZ_X11) || defined(XP_BEOS)
    NS_IMETHOD GetTextDimensions(const char*       aString,
                                 PRInt32           aLength,
                                 PRInt32           aAvailWidth,
                                 PRInt32*          aBreaks,
                                 PRInt32           aNumBreaks,
                                 nsTextDimensions& aDimensions,
                                 PRInt32&          aNumCharsFit,
                                 nsTextDimensions& aLastWordDimensions,
                                 PRInt32*          aFontID = nsnull);

    NS_IMETHOD GetTextDimensions(const PRUnichar*  aString,
                                 PRInt32           aLength,
                                 PRInt32           aAvailWidth,
                                 PRInt32*          aBreaks,
                                 PRInt32           aNumBreaks,
                                 nsTextDimensions& aDimensions,
                                 PRInt32&          aNumCharsFit,
                                 nsTextDimensions& aLastWordDimensions,
                                 PRInt32*          aFontID = nsnull);
#endif
#ifdef MOZ_MATHML
    NS_IMETHOD GetBoundingMetrics(const char*        aString,
                                  PRUint32           aLength,
                                  nsBoundingMetrics& aBoundingMetrics);
    NS_IMETHOD GetBoundingMetrics(const PRUnichar*   aString,
                                  PRUint32           aLength,
                                  nsBoundingMetrics& aBoundingMetrics,
                                  PRInt32*           aFontID = nsnull);
#endif
    NS_IMETHOD DrawString(const char *aString, PRUint32 aLength,
                          nscoord aX, nscoord aY,
                          const nscoord* aSpacing = nsnull);
    NS_IMETHOD DrawString(const PRUnichar *aString, PRUint32 aLength,
                          nscoord aX, nscoord aY,
                          PRInt32 aFontID = -1,
                          const nscoord* aSpacing = nsnull);

    NS_IMETHOD Init(nsIDeviceContext* aContext, gfxASurface* aThebesSurface);
    NS_IMETHOD Init(nsIDeviceContext* aContext, gfxContext* aThebesContext);

    NS_IMETHOD Init(nsIDeviceContext* aContext, nsIWidget *aWidget);
    NS_IMETHOD CommonInit(void);
    NS_IMETHOD GetDeviceContext(nsIDeviceContext *& aDeviceContext);
    NS_IMETHOD PushState(void);
    NS_IMETHOD PopState(void);
    NS_IMETHOD SetClipRect(const nsRect& aRect, nsClipCombine aCombine);
    NS_IMETHOD SetLineStyle(nsLineStyle aLineStyle);
    NS_IMETHOD SetClipRegion(const nsIRegion& aRegion, nsClipCombine aCombine);
    NS_IMETHOD SetColor(nscolor aColor);
    NS_IMETHOD GetColor(nscolor &aColor) const;
    NS_IMETHOD SetFont(const nsFont& aFont, nsIAtom* aLanguage,
                       gfxUserFontSet *aUserFontSet);
    NS_IMETHOD SetFont(const nsFont& aFont,
                       gfxUserFontSet *aUserFontSet);
    NS_IMETHOD SetFont(nsIFontMetrics *aFontMetrics);
    NS_IMETHOD GetFontMetrics(nsIFontMetrics *&aFontMetrics);
    NS_IMETHOD Translate(nscoord aX, nscoord aY);
    NS_IMETHOD Scale(float aSx, float aSy);
    NS_IMETHOD GetCurrentTransform(nsTransform2D *&aTransform);

    NS_IMETHOD DrawLine(nscoord aX0, nscoord aY0, nscoord aX1, nscoord aY1);
    NS_IMETHOD DrawRect(const nsRect& aRect);
    NS_IMETHOD DrawRect(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight);
    NS_IMETHOD FillRect(const nsRect& aRect);
    NS_IMETHOD FillRect(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight);
    NS_IMETHOD InvertRect(const nsRect& aRect);
    NS_IMETHOD InvertRect(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight);
    NS_IMETHOD FillPolygon(const nsPoint aPoints[], PRInt32 aNumPoints);
    NS_IMETHOD DrawEllipse(const nsRect& aRect);
    NS_IMETHOD DrawEllipse(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight);
    NS_IMETHOD FillEllipse(const nsRect& aRect);
    NS_IMETHOD FillEllipse(nscoord aX, nscoord aY, nscoord aWidth, nscoord aHeight);

    NS_IMETHOD PushFilter(const nsRect& aRect, PRBool aAreaIsOpaque, float aOpacity);
    NS_IMETHOD PopFilter();

    virtual void* GetNativeGraphicData(GraphicDataType aType);

    NS_IMETHOD PushTranslation(PushedTranslation* aState);
    NS_IMETHOD PopTranslation(PushedTranslation* aState);
    NS_IMETHOD SetTranslation(nscoord aX, nscoord aY);

    /**
     * Let the device context know whether we want text reordered with
     * right-to-left base direction
     */
    NS_IMETHOD SetRightToLeftText(PRBool aIsRTL);
    NS_IMETHOD GetRightToLeftText(PRBool* aIsRTL);
    virtual void SetTextRunRTL(PRBool aIsRTL);

    virtual PRInt32 GetPosition(const PRUnichar *aText,
                                PRUint32 aLength,
                                nsPoint aPt);
    NS_IMETHOD GetRangeWidth(const PRUnichar *aText,
                             PRUint32 aLength,
                             PRUint32 aStart,
                             PRUint32 aEnd,
                             PRUint32 &aWidth);
    NS_IMETHOD GetRangeWidth(const char *aText,
                             PRUint32 aLength,
                             PRUint32 aStart,
                             PRUint32 aEnd,
                             PRUint32 &aWidth);

    NS_IMETHOD RenderEPS(const nsRect& aRect, FILE *aDataFile);

    // Thebes specific stuff

    gfxContext *ThebesContext() { return mThebes; }

    nsTransform2D& CurrentTransform();

    void TransformCoord (nscoord *aX, nscoord *aY);

protected:
    nsresult GetWidthInternal(const char *aString, PRUint32 aLength, nscoord &aWidth);
    nsresult GetWidthInternal(const PRUnichar *aString, PRUint32 aLength, nscoord &aWidth,
                              PRInt32 *aFontID = nsnull);

    nsresult DrawStringInternal(const char *aString, PRUint32 aLength,
                                nscoord aX, nscoord aY,
                                const nscoord* aSpacing = nsnull);
    nsresult DrawStringInternal(const PRUnichar *aString, PRUint32 aLength,
                                nscoord aX, nscoord aY,
                                PRInt32 aFontID = -1,
                                const nscoord* aSpacing = nsnull);

    nsresult GetTextDimensionsInternal(const char*       aString,
                                       PRUint32          aLength,
                                       nsTextDimensions& aDimensions);
    nsresult GetTextDimensionsInternal(const PRUnichar*  aString,
                                       PRUint32          aLength,
                                       nsTextDimensions& aDimensions,
                                       PRInt32*          aFontID = nsnull);
    nsresult GetTextDimensionsInternal(const char*       aString,
                                       PRInt32           aLength,
                                       PRInt32           aAvailWidth,
                                       PRInt32*          aBreaks,
                                       PRInt32           aNumBreaks,
                                       nsTextDimensions& aDimensions,
                                       PRInt32&          aNumCharsFit,
                                       nsTextDimensions& aLastWordDimensions,
                                       PRInt32*          aFontID = nsnull);
    nsresult GetTextDimensionsInternal(const PRUnichar*  aString,
                                       PRInt32           aLength,
                                       PRInt32           aAvailWidth,
                                       PRInt32*          aBreaks,
                                       PRInt32           aNumBreaks,
                                       nsTextDimensions& aDimensions,
                                       PRInt32&          aNumCharsFit,
                                       nsTextDimensions& aLastWordDimensions,
                                       PRInt32*          aFontID = nsnull);

#ifdef MOZ_MATHML
    /**
     * Returns metrics (in app units) of an 8-bit character string
     */
    nsresult GetBoundingMetricsInternal(const char*        aString,
                                        PRUint32           aLength,
                                        nsBoundingMetrics& aBoundingMetrics);

    /**
     * Returns metrics (in app units) of a Unicode character string
     */
    nsresult GetBoundingMetricsInternal(const PRUnichar*   aString,
                                        PRUint32           aLength,
                                        nsBoundingMetrics& aBoundingMetrics,
                                        PRInt32*           aFontID = nsnull);

#endif /* MOZ_MATHML */

    nsCOMPtr<nsIDeviceContext> mDeviceContext;
    // cached app units per device pixel value
    double mP2A;

    nsCOMPtr<nsIWidget> mWidget;

    // we need to manage our own clip region (since we can't get at
    // the old one from cairo)
    nsCOMPtr<nsIThebesFontMetrics> mFontMetrics;

    nsLineStyle mLineStyle;
    nscolor mColor;

    // the rendering context
    nsRefPtr<gfxContext> mThebes;

    // for handing out to people
    void UpdateTempTransformMatrix();
    nsTransform2D mTempTransform;

    // keeping track of pushgroup/popgroup opacities
    nsTArray<float> mOpacityArray;
};

#endif  // NSCAIRORENDERINGCONTEXT__H__
