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
 * style sheet and style rule processor representing style attributes
 */

#ifndef nsHTMLCSSStyleSheet_h_
#define nsHTMLCSSStyleSheet_h_

#include "nsIStyleSheet.h"
#include "nsIStyleRuleProcessor.h"

class nsHTMLCSSStyleSheet : public nsIStyleSheet,
                            public nsIStyleRuleProcessor {
public:
  nsHTMLCSSStyleSheet();

  NS_DECL_ISUPPORTS

  nsresult Init(nsIURI* aURL, nsIDocument* aDocument);
  nsresult Reset(nsIURI* aURL);

  // nsIStyleSheet
  NS_IMETHOD GetSheetURI(nsIURI** aSheetURL) const;
  NS_IMETHOD GetBaseURI(nsIURI** aBaseURL) const;
  NS_IMETHOD GetTitle(nsString& aTitle) const;
  NS_IMETHOD GetType(nsString& aType) const;
  NS_IMETHOD_(PRBool) HasRules() const;
  NS_IMETHOD GetApplicable(PRBool& aApplicable) const;
  NS_IMETHOD SetEnabled(PRBool aEnabled);
  NS_IMETHOD GetComplete(PRBool& aComplete) const;
  NS_IMETHOD SetComplete();
  NS_IMETHOD GetParentSheet(nsIStyleSheet*& aParent) const;  // will be null
  NS_IMETHOD GetOwningDocument(nsIDocument*& aDocument) const;
  NS_IMETHOD SetOwningDocument(nsIDocument* aDocument);
#ifdef DEBUG
  virtual void List(FILE* out = stdout, PRInt32 aIndent = 0) const;
#endif

  // nsIStyleRuleProcessor
  NS_IMETHOD RulesMatching(ElementRuleProcessorData* aData);
  NS_IMETHOD RulesMatching(PseudoElementRuleProcessorData* aData);
  NS_IMETHOD RulesMatching(AnonBoxRuleProcessorData* aData);
#ifdef MOZ_XUL
  NS_IMETHOD RulesMatching(XULTreeRuleProcessorData* aData);
#endif
  virtual nsReStyleHint HasStateDependentStyle(StateRuleProcessorData* aData);
  virtual nsReStyleHint
    HasAttributeDependentStyle(AttributeRuleProcessorData* aData);
  NS_IMETHOD MediumFeaturesChanged(nsPresContext* aPresContext,
                                  PRBool* aResult);

private: 
  // These are not supported and are not implemented! 
  nsHTMLCSSStyleSheet(const nsHTMLCSSStyleSheet& aCopy); 
  nsHTMLCSSStyleSheet& operator=(const nsHTMLCSSStyleSheet& aCopy); 

protected:
  virtual ~nsHTMLCSSStyleSheet();

protected:
  nsIURI*         mURL;
  nsIDocument*    mDocument;
};

#endif /* !defined(nsHTMLCSSStyleSheet_h_) */
