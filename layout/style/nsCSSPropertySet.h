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
 * The Original Code is nsCSSDataBlock.h.
 *
 * The Initial Developer of the Original Code is L. David Baron.
 * Portions created by the Initial Developer are Copyright (C) 2003
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

/* bit vectors for sets of CSS properties */

#ifndef nsCSSPropertySet_h__
#define nsCSSPropertySet_h__

#include "nsCSSProperty.h"

/**
 * nsCSSPropertySet maintains a set of non-shorthand CSS properties.  In
 * other words, for each longhand CSS property we support, it has a bit
 * for whether that property is in the set.
 */
class nsCSSPropertySet {
public:
    nsCSSPropertySet() { Empty(); }
    // auto-generated copy-constructor OK

    void AssertInSetRange(nsCSSProperty aProperty) const {
        NS_ASSERTION(0 <= aProperty &&
                     aProperty < eCSSProperty_COUNT_no_shorthands,
                     "out of bounds");
    }

    void AddProperty(nsCSSProperty aProperty) {
        AssertInSetRange(aProperty);
        mProperties[aProperty / kBitsInChunk] |=
            property_set_type(1 << (aProperty % kBitsInChunk));
    }

    void RemoveProperty(nsCSSProperty aProperty) {
        AssertInSetRange(aProperty);
        mProperties[aProperty / kBitsInChunk] &=
            ~property_set_type(1 << (aProperty % kBitsInChunk));
    }

    PRBool HasProperty(nsCSSProperty aProperty) const {
        AssertInSetRange(aProperty);
        return (mProperties[aProperty / kBitsInChunk] &
                (1 << (aProperty % kBitsInChunk))) != 0;
    }

    void Empty() {
        memset(mProperties, 0, sizeof(mProperties));
    }

    void AssertIsEmpty(const char* aText) const {
        for (PRUint32 i = 0; i < NS_ARRAY_LENGTH(mProperties); ++i) {
            NS_ASSERTION(mProperties[i] == 0, aText);
        }
    }

private:
    typedef PRUint8 property_set_type;
public:
    enum { kBitsInChunk = 8 }; // number of bits in
                                          // |property_set_type|.
    // number of |property_set_type|s in the set
    enum { kChunkCount =
             (eCSSProperty_COUNT_no_shorthands + (kBitsInChunk-1)) /
             kBitsInChunk };

    /*
     * For fast enumeration of all the bits that are set, callers can
     * check each chunk against zero (since in normal cases few bits are
     * likely to be set).
     */
    PRBool HasPropertyInChunk(PRUint32 aChunk) const {
        return mProperties[aChunk] != 0;
    }
    PRBool HasPropertyAt(PRUint32 aChunk, PRInt32 aBit) const {
        return (mProperties[aChunk] & (1 << aBit)) != 0;
    }
    static nsCSSProperty CSSPropertyAt(PRUint32 aChunk, PRInt32 aBit) {
        return nsCSSProperty(aChunk * kBitsInChunk + aBit);
    }

private:
    property_set_type mProperties[kChunkCount];
};

#endif /* !defined(nsCSSPropertySet_h__) */
