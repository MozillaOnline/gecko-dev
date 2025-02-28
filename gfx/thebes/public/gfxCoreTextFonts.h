/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
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
 * The Original Code is thebes gfx code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2006-2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Vladimir Vukicevic <vladimir@pobox.com>
 *   Masayuki Nakano <masayuki@d-toybox.com>
 *   John Daggett <jdaggett@mozilla.com>
 *   Jonathan Kew <jfkthame@gmail.com>
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

#ifndef GFX_CORETEXTFONTS_H
#define GFX_CORETEXTFONTS_H

#include "cairo.h"
#include "gfxTypes.h"
#include "gfxFont.h"
#include "gfxFontUtils.h"
#include "gfxPlatform.h"

#include <Carbon/Carbon.h>

class MacOSFontEntry;

class gfxCoreTextFont : public gfxFont {
public:

    gfxCoreTextFont(MacOSFontEntry *aFontEntry,
                    const gfxFontStyle *fontStyle, PRBool aNeedsBold);

    virtual ~gfxCoreTextFont();

    virtual const gfxFont::Metrics& GetMetrics() {
        NS_ASSERTION(mHasMetrics == PR_TRUE, "metrics not initialized");
        return mMetrics;
    }

    float GetCharWidth(PRUnichar c, PRUint32 *aGlyphID = nsnull);
    float GetCharHeight(PRUnichar c);

    ATSFontRef GetATSFont() {
        return mATSFont;
    }

    CTFontRef GetCTFont() {
        return mCTFont;
    }

    CFDictionaryRef GetAttributesDictionary() {
        return mAttributesDict;
    }

    cairo_font_face_t *CairoFontFace() {
        return mFontFace;
    }

    cairo_scaled_font_t *CairoScaledFont() {
        return mScaledFont;
    }

    virtual nsString GetUniqueName() {
        return GetName();
    }

    virtual PRUint32 GetSpaceGlyph() {
        return mSpaceGlyph;
    }

    PRBool TestCharacterMap(PRUint32 aCh);

    MacOSFontEntry* GetFontEntry();

    // clean up static objects that may have been cached
    static void Shutdown();

    static CTFontRef CreateCTFontWithDisabledLigatures(ATSFontRef aFontRef, CGFloat aSize);

protected:
    const gfxFontStyle *mFontStyle;

    ATSFontRef mATSFont;
    CTFontRef mCTFont;
    CFDictionaryRef mAttributesDict;

    PRBool mHasMetrics;

    nsString mUniqueName;

    cairo_font_face_t *mFontFace;
    cairo_scaled_font_t *mScaledFont;

    gfxFont::Metrics mMetrics;

    gfxFloat mAdjustedSize;
    PRUint32 mSpaceGlyph;    

    void InitMetrics();

    virtual void InitTextRun(gfxTextRun *aTextRun,
                             const PRUnichar *aString,
                             PRUint32 aRunStart,
                             PRUint32 aRunLength);

    nsresult SetGlyphsFromRun(gfxTextRun *aTextRun,
                              CTRunRef aCTRun,
                              PRInt32 aStringOffset,
                              PRInt32 aLayoutStart,
                              PRInt32 aLayoutLength);

    virtual PRBool SetupCairoFont(gfxContext *aContext);

    static void CreateDefaultFeaturesDescriptor();

    static CTFontDescriptorRef GetDefaultFeaturesDescriptor() {
        if (sDefaultFeaturesDescriptor == NULL)
            CreateDefaultFeaturesDescriptor();
        return sDefaultFeaturesDescriptor;
    }

    // cached font descriptor, created the first time it's needed
    static CTFontDescriptorRef    sDefaultFeaturesDescriptor;
    // cached descriptor for adding disable-ligatures setting to a font
    static CTFontDescriptorRef    sDisableLigaturesDescriptor;
};

#endif /* GFX_CORETEXTFONTS_H */
