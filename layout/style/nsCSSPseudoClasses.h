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

#ifndef nsCSSPseudoClasses_h___
#define nsCSSPseudoClasses_h___

#include "nsIAtom.h"

// Empty class derived from nsIAtom so that function signatures can
// require an atom from this atom list.
class nsICSSPseudoClass : public nsIAtom {};

class nsCSSPseudoClasses {
public:

  static void AddRefAtoms();

  static PRBool IsPseudoClass(nsIAtom *aAtom);
  static PRBool HasStringArg(nsIAtom* aAtom);
  static PRBool HasNthPairArg(nsIAtom* aAtom);

#define CSS_PSEUDO_CLASS(_name, _value) static nsICSSPseudoClass* _name;
#include "nsCSSPseudoClassList.h"
#undef CSS_PSEUDO_CLASS

  enum Type {
#define CSS_PSEUDO_CLASS(_name, _value) \
    ePseudoClass_##_name,
#include "nsCSSPseudoClassList.h"
#undef CSS_PSEUDO_CLASS
    ePseudoClass_Count,
    ePseudoClass_NotPseudoClass /* This value MUST be last!  SelectorMatches
                                   depends on it. */
  };

  static Type GetPseudoType(nsIAtom* aAtom);
};

#endif /* nsCSSPseudoClasses_h___ */
