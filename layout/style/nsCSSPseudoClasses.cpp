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
 * The Original Code is atom lists for CSS pseudos.
 *
 * The Initial Developer of the Original Code is 
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   L. David Baron <dbaron@dbaron.org>
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

/* atom list for CSS pseudo-classes */

#include "nsCSSPseudoClasses.h"
#include "nsAtomListUtils.h"
#include "nsStaticAtom.h"
#include "nsMemory.h"

// define storage for all atoms
#define CSS_PSEUDO_CLASS(_name, _value) \
  nsICSSPseudoClass* nsCSSPseudoClasses::_name;
#include "nsCSSPseudoClassList.h"
#undef CSS_PSEUDO_CLASS

static const nsStaticAtom CSSPseudoClasses_info[] = {
#define CSS_PSEUDO_CLASS(name_, value_) \
  { value_, (nsIAtom**)&nsCSSPseudoClasses::name_ },
#include "nsCSSPseudoClassList.h"
#undef CSS_PSEUDO_CLASS
};

void nsCSSPseudoClasses::AddRefAtoms()
{
  NS_RegisterStaticAtoms(CSSPseudoClasses_info,
                         NS_ARRAY_LENGTH(CSSPseudoClasses_info));
}

PRBool nsCSSPseudoClasses::IsPseudoClass(nsIAtom *aAtom)
{
  return nsAtomListUtils::IsMember(aAtom,CSSPseudoClasses_info,
                                   NS_ARRAY_LENGTH(CSSPseudoClasses_info));
}

PRBool
nsCSSPseudoClasses::HasStringArg(nsIAtom* aAtom)
{
  return aAtom == nsCSSPseudoClasses::lang ||
         aAtom == nsCSSPseudoClasses::mozEmptyExceptChildrenWithLocalname ||
         aAtom == nsCSSPseudoClasses::mozSystemMetric ||
         aAtom == nsCSSPseudoClasses::mozLocaleDir;
}

PRBool
nsCSSPseudoClasses::HasNthPairArg(nsIAtom* aAtom)
{
  return aAtom == nsCSSPseudoClasses::nthChild ||
         aAtom == nsCSSPseudoClasses::nthLastChild ||
         aAtom == nsCSSPseudoClasses::nthOfType ||
         aAtom == nsCSSPseudoClasses::nthLastOfType;
}

nsCSSPseudoClasses::Type
nsCSSPseudoClasses::GetPseudoType(nsIAtom* aAtom)
{
  for (PRUint32 i = 0; i < NS_ARRAY_LENGTH(CSSPseudoClasses_info); ++i) {
    if (*CSSPseudoClasses_info[i].mAtom == aAtom) {
      return Type(i);
    }
  }

  return nsCSSPseudoClasses::ePseudoClass_NotPseudoClass;
}
