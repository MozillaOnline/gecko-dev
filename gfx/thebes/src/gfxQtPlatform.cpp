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
 * The Original Code is Mozilla Foundation code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2005
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Vladimir Vukicevic <vladimir@pobox.com>
 *   Masayuki Nakano <masayuki@d-toybox.com>
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

#include <QPixmap>
#include <QX11Info>
#include <QApplication>
#include <QWidget>

#include "gfxQtPlatform.h"

#include "gfxFontconfigUtils.h"

#include "cairo.h"

#include "gfxImageSurface.h"
#include "gfxQPainterSurface.h"

#include "gfxFT2Fonts.h"

#include "nsUnicharUtils.h"

#include <fontconfig/fontconfig.h>

#include "nsMathUtils.h"
#include "nsTArray.h"
#ifdef MOZ_X11
#include "gfxXlibSurface.h"
#endif

#include "qcms.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include "nsIPrefBranch.h"
#include "nsIPrefService.h"

#define DEFAULT_RENDER_MODE RENDER_SHARED_IMAGE

gfxFontconfigUtils *gfxQtPlatform::sFontconfigUtils = nsnull;
static cairo_user_data_key_t cairo_qt_pixmap_key;
static void do_qt_pixmap_unref (void *data)
{
    QPixmap *pmap = (QPixmap*)data;
    delete pmap;
}

typedef nsDataHashtable<nsStringHashKey, nsRefPtr<FontFamily> > FontTable;
typedef nsDataHashtable<nsCStringHashKey, nsTArray<nsRefPtr<FontEntry> > > PrefFontTable;
static FontTable *gPlatformFonts = NULL;
static FontTable *gPlatformFontAliases = NULL;
static PrefFontTable *gPrefFonts = NULL;
static gfxSparseBitSet *gCodepointsWithNoFonts = NULL;
static FT_Library gPlatformFTLibrary = NULL;


gfxQtPlatform::gfxQtPlatform()
{
    mPrefFonts.Init(50);

    if (!sFontconfigUtils)
        sFontconfigUtils = gfxFontconfigUtils::GetFontconfigUtils();


    FT_Init_FreeType(&gPlatformFTLibrary);

    gPlatformFonts = new FontTable();
    gPlatformFonts->Init(100);
    gPlatformFontAliases = new FontTable();
    gPlatformFontAliases->Init(100);
    gPrefFonts = new PrefFontTable();
    gPrefFonts->Init(100);
    gCodepointsWithNoFonts = new gfxSparseBitSet();
    UpdateFontList();

    nsresult rv;
    PRInt32 ival;
    // 0 - default gfxQPainterSurface
    // 1 - gfxXlibSurface
    // 2 - gfxImageSurface
    nsCOMPtr<nsIPrefBranch> prefs = do_GetService(NS_PREFSERVICE_CONTRACTID, &rv);
    if (prefs) {
      rv = prefs->GetIntPref("mozilla.widget-qt.render-mode", &ival);
      if (NS_FAILED(rv))
          ival = DEFAULT_RENDER_MODE;
    }

    const char *envTypeOverride = getenv("MOZ_QT_RENDER_TYPE");
    if (envTypeOverride)
        ival = atoi(envTypeOverride);

    switch (ival) {
        case 0:
            mRenderMode = RENDER_QPAINTER;
            break;
        case 1:
            mRenderMode = RENDER_XLIB;
            break;
        case 2:
            mRenderMode = RENDER_SHARED_IMAGE;
            break;
        default:
            mRenderMode = RENDER_QPAINTER;
    }
}

gfxQtPlatform::~gfxQtPlatform()
{
    gfxFontconfigUtils::Shutdown();
    sFontconfigUtils = nsnull;

    delete gPlatformFonts;
    gPlatformFonts = NULL;
    delete gPlatformFontAliases;
    gPlatformFontAliases = NULL;
    delete gPrefFonts;
    gPrefFonts = NULL;
    delete gCodepointsWithNoFonts;
    gCodepointsWithNoFonts = NULL;

    cairo_debug_reset_static_data();

    FT_Done_FreeType(gPlatformFTLibrary);
    gPlatformFTLibrary = NULL;


#if 0
    // It would be nice to do this (although it might need to be after
    // the cairo shutdown that happens in ~gfxPlatform).  It even looks
    // idempotent.  But it has fatal assertions that fire if stuff is
    // leaked, and we hit them.
    FcFini();
#endif
}

already_AddRefed<gfxASurface>
gfxQtPlatform::CreateOffscreenSurface(const gfxIntSize& size,
                                      gfxASurface::gfxImageFormat imageFormat)
{
    nsRefPtr<gfxASurface> newSurface = nsnull;

    if (mRenderMode == RENDER_QPAINTER) {
      newSurface = new gfxQPainterSurface(size, gfxASurface::ContentFromFormat(imageFormat));
      return newSurface.forget();
    }

    if (mRenderMode == RENDER_SHARED_IMAGE) {
      newSurface = new gfxImageSurface(size, imageFormat);
      return newSurface.forget();
    }

#ifdef MOZ_X11
    int xrenderFormatID = -1;
    switch (imageFormat) {
        case gfxASurface::ImageFormatARGB32:
            xrenderFormatID = PictStandardARGB32;
            break;
        case gfxASurface::ImageFormatRGB24:
            xrenderFormatID = PictStandardRGB24;
            break;
        case gfxASurface::ImageFormatA8:
            xrenderFormatID = PictStandardA8;
            break;
        case gfxASurface::ImageFormatA1:
            xrenderFormatID = PictStandardA1;
            break;
        default:
            return nsnull;
    }

    // XXX we really need a different interface here, something that passes
    // in more context, including the display and/or target surface type that
    // we should try to match
    XRenderPictFormat* xrenderFormat =
        XRenderFindStandardFormat(QX11Info().display(), xrenderFormatID);

    newSurface = new gfxXlibSurface((Display*)QX11Info().display(),
                                    xrenderFormat,
                                    size);
#endif

    if (newSurface) {
        gfxContext ctx(newSurface);
        ctx.SetOperator(gfxContext::OPERATOR_CLEAR);
        ctx.Paint();
    }

    return newSurface.forget();
}

nsresult
gfxQtPlatform::GetFontList(nsIAtom *aLangGroup,
                           const nsACString& aGenericFamily,
                           nsTArray<nsString>& aListOfFonts)
{
    return sFontconfigUtils->GetFontList(aLangGroup, aGenericFamily,
                                         aListOfFonts);
}

nsresult
gfxQtPlatform::UpdateFontList()
{
    FcPattern *pat = NULL;
    FcObjectSet *os = NULL;
    FcFontSet *fs = NULL;

    pat = FcPatternCreate();
    os = FcObjectSetBuild(FC_FAMILY, FC_FILE, FC_INDEX, FC_WEIGHT, FC_SLANT, FC_WIDTH, NULL);

    fs = FcFontList(NULL, pat, os);


    for (int i = 0; i < fs->nfont; i++) {
        char *str;

        if (FcPatternGetString(fs->fonts[i], FC_FAMILY, 0, (FcChar8 **) &str) != FcResultMatch)
            continue;

        nsAutoString name(NS_ConvertUTF8toUTF16(nsDependentCString(str)).get());
        nsAutoString key(name);
        ToLowerCase(key);
        nsRefPtr<FontFamily> ff;
        if (!gPlatformFonts->Get(key, &ff)) {
            ff = new FontFamily(name);
            gPlatformFonts->Put(key, ff);
        }

        FontEntry *fe = new FontEntry(ff->Name());
        ff->AddFontEntry(fe);

        if (FcPatternGetString(fs->fonts[i], FC_FILE, 0, (FcChar8 **) &str) == FcResultMatch) {
            fe->mFilename = nsDependentCString(str);
        }

        int x;
        if (FcPatternGetInteger(fs->fonts[i], FC_INDEX, 0, &x) == FcResultMatch) {
            fe->mFTFontIndex = x;
        } else {
            fe->mFTFontIndex = 0;
        }

        if (FcPatternGetInteger(fs->fonts[i], FC_WEIGHT, 0, &x) == FcResultMatch) {
            switch(x) {
            case 0:
                fe->mWeight = 100;
                break;
            case 40:
                fe->mWeight = 200;
                break;
            case 50:
                fe->mWeight = 300;
                break;
            case 75:
            case 80:
                fe->mWeight = 400;
                break;
            case 100:
                fe->mWeight = 500;
                break;
            case 180:
                fe->mWeight = 600;
                break;
            case 200:
                fe->mWeight = 700;
                break;
            case 205:
                fe->mWeight = 800;
                break;
            case 210:
                fe->mWeight = 900;
                break;
            default:
                // rough estimate
                fe->mWeight = (((x * 4) + 100) / 100) * 100;
                break;
            }
        }

        fe->mItalic = PR_FALSE;
        if (FcPatternGetInteger(fs->fonts[i], FC_SLANT, 0, &x) == FcResultMatch) {
            switch (x) {
            case FC_SLANT_ITALIC:
            case FC_SLANT_OBLIQUE:
                fe->mItalic = PR_TRUE;
            }
        }

        // XXX deal with font-stretch (FC_WIDTH) stuff later
    }

    if (pat)
        FcPatternDestroy(pat);
    if (os)
        FcObjectSetDestroy(os);
    if (fs)
        FcFontSetDestroy(fs);

    return sFontconfigUtils->UpdateFontList();
}

nsresult
gfxQtPlatform::ResolveFontName(const nsAString& aFontName,
                                FontResolverCallback aCallback,
                                void *aClosure,
                                PRBool& aAborted)
{

    nsAutoString name(aFontName);
    ToLowerCase(name);

    nsRefPtr<FontFamily> ff;
    if (gPlatformFonts->Get(name, &ff) ||
        gPlatformFontAliases->Get(name, &ff)) {
        aAborted = !(*aCallback)(ff->Name(), aClosure);
        return NS_OK;
    }

    nsCAutoString utf8Name = NS_ConvertUTF16toUTF8(aFontName);

    FcPattern *npat = FcPatternCreate();
    FcPatternAddString(npat, FC_FAMILY, (FcChar8*)utf8Name.get());
    FcObjectSet *nos = FcObjectSetBuild(FC_FAMILY, NULL);
    FcFontSet *nfs = FcFontList(NULL, npat, nos);

    for (int k = 0; k < nfs->nfont; k++) {
        FcChar8 *str;
        if (FcPatternGetString(nfs->fonts[k], FC_FAMILY, 0, (FcChar8 **) &str) != FcResultMatch)
            continue;
        nsAutoString altName = NS_ConvertUTF8toUTF16(nsDependentCString(reinterpret_cast<char*>(str)));
        ToLowerCase(altName);
        if (gPlatformFonts->Get(altName, &ff)) {
            gPlatformFontAliases->Put(name, ff);
            aAborted = !(*aCallback)(NS_ConvertUTF8toUTF16(nsDependentCString(reinterpret_cast<char*>(str))), aClosure);
            goto DONE;
        }
    }

    FcPatternDestroy(npat);
    FcObjectSetDestroy(nos);
    FcFontSetDestroy(nfs);

    {
    npat = FcPatternCreate();
    FcPatternAddString(npat, FC_FAMILY, (FcChar8*)utf8Name.get());
    FcPatternDel(npat, FC_LANG);
    FcConfigSubstitute(NULL, npat, FcMatchPattern);
    FcDefaultSubstitute(npat);

    nos = FcObjectSetBuild(FC_FAMILY, NULL);
    nfs = FcFontList(NULL, npat, nos);

    FcResult fresult;

    FcPattern *match = FcFontMatch(NULL, npat, &fresult);
    if (match)
        FcFontSetAdd(nfs, match);

    for (int k = 0; k < nfs->nfont; k++) {
        FcChar8 *str;
        if (FcPatternGetString(nfs->fonts[k], FC_FAMILY, 0, (FcChar8 **) &str) != FcResultMatch)
            continue;
        nsAutoString altName = NS_ConvertUTF8toUTF16(nsDependentCString(reinterpret_cast<char*>(str)));
        ToLowerCase(altName);
        if (gPlatformFonts->Get(altName, &ff)) {
            gPlatformFontAliases->Put(name, ff);
            aAborted = !(*aCallback)(NS_ConvertUTF8toUTF16(nsDependentCString(reinterpret_cast<char*>(str))), aClosure);
            goto DONE;
        }
    }
    }
 DONE:
    FcPatternDestroy(npat);
    FcObjectSetDestroy(nos);
    FcFontSetDestroy(nfs);

    return NS_OK;
}

nsresult
gfxQtPlatform::GetStandardFamilyName(const nsAString& aFontName, nsAString& aFamilyName)
{
    return sFontconfigUtils->GetStandardFamilyName(aFontName, aFamilyName);
}

gfxFontGroup *
gfxQtPlatform::CreateFontGroup(const nsAString &aFamilies,
                               const gfxFontStyle *aStyle,
                               gfxUserFontSet* aUserFontSet)
{
    return new gfxFT2FontGroup(aFamilies, aStyle);
}

qcms_profile*
gfxQtPlatform::GetPlatformCMSOutputProfile()
{
    return nsnull;
}

FT_Library
gfxQtPlatform::GetFTLibrary()
{
    return gPlatformFTLibrary;
}

FontFamily *
gfxQtPlatform::FindFontFamily(const nsAString& aName)
{
    nsAutoString name(aName);
    ToLowerCase(name);

    nsRefPtr<FontFamily> ff;
    if (!gPlatformFonts->Get(name, &ff)) {
        return nsnull;
    }
    return ff.get();
}

FontEntry *
gfxQtPlatform::FindFontEntry(const nsAString& aName, const gfxFontStyle& aFontStyle)
{
    nsRefPtr<FontFamily> ff = FindFontFamily(aName);
    if (!ff)
        return nsnull;

    return ff->FindFontEntry(aFontStyle);
}

static PLDHashOperator
FindFontForCharProc(nsStringHashKey::KeyType aKey,
                    nsRefPtr<FontFamily>& aFontFamily,
                    void* aUserArg)
{
    FontSearch *data = (FontSearch*)aUserArg;
    aFontFamily->FindFontForChar(data);
    return PL_DHASH_NEXT;
}

already_AddRefed<gfxFont>
gfxQtPlatform::FindFontForChar(PRUint32 aCh, gfxFont *aFont)
{
    if (!gPlatformFonts || !gCodepointsWithNoFonts)
        return nsnull;

    // is codepoint with no matching font? return null immediately
    if (gCodepointsWithNoFonts->test(aCh)) {
        return nsnull;
    }

    FontSearch data(aCh, aFont);

    // find fonts that support the character
    gPlatformFonts->Enumerate(FindFontForCharProc, &data);

    if (data.mBestMatch) {
        nsRefPtr<gfxFT2Font> font =
            gfxFT2Font::GetOrMakeFont(static_cast<FontEntry*>(data.mBestMatch.get()),
                                      aFont->GetStyle()); 
        gfxFont* ret = font.forget().get();
        return already_AddRefed<gfxFont>(ret);
    }

    // no match? add to set of non-matching codepoints
    gCodepointsWithNoFonts->set(aCh);

    return nsnull;
}

PRBool
gfxQtPlatform::GetPrefFontEntries(const nsCString& aKey, nsTArray<nsRefPtr<gfxFontEntry> > *array)
{
    return mPrefFonts.Get(aKey, array);
}

void
gfxQtPlatform::SetPrefFontEntries(const nsCString& aKey, nsTArray<nsRefPtr<gfxFontEntry> >& array)
{
    mPrefFonts.Put(aKey, array);
}
