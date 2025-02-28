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

#include "nsGfxButtonControlFrame.h"
#include "nsWidgetsCID.h"
#include "nsIFontMetrics.h"
#include "nsFormControlFrame.h"
#include "nsIFormControl.h"
#include "nsINameSpaceManager.h"
#ifdef ACCESSIBILITY
#include "nsIAccessibilityService.h"
#endif
#include "nsIServiceManager.h"
#include "nsIDOMNode.h"
#include "nsGkAtoms.h"
#include "nsAutoPtr.h"
#include "nsStyleSet.h"
#include "nsContentUtils.h"
// MouseEvent suppression in PP
#include "nsGUIEvent.h"
#include "nsContentCreatorFunctions.h"

#include "nsNodeInfoManager.h"
#include "nsIDOMHTMLInputElement.h"

const nscoord kSuggestedNotSet = -1;

nsGfxButtonControlFrame::nsGfxButtonControlFrame(nsStyleContext* aContext):
  nsHTMLButtonControlFrame(aContext)
{
}

nsIFrame*
NS_NewGfxButtonControlFrame(nsIPresShell* aPresShell, nsStyleContext* aContext)
{
  return new (aPresShell) nsGfxButtonControlFrame(aContext);
}

NS_IMPL_FRAMEARENA_HELPERS(nsGfxButtonControlFrame)

void nsGfxButtonControlFrame::DestroyFrom(nsIFrame* aDestructRoot)
{
  nsContentUtils::DestroyAnonymousContent(&mTextContent);
  nsHTMLButtonControlFrame::DestroyFrom(aDestructRoot);
}

nsIAtom*
nsGfxButtonControlFrame::GetType() const
{
  return nsGkAtoms::gfxButtonControlFrame;
}

// Special check for the browse button of a file input.
//
// We'll return PR_TRUE if type is NS_FORM_INPUT_BUTTON and our parent
// is a file input.
PRBool
nsGfxButtonControlFrame::IsFileBrowseButton(PRInt32 type)
{
  PRBool rv = PR_FALSE;
  if (NS_FORM_INPUT_BUTTON == type) {
    // Check to see if parent is a file input
    nsCOMPtr<nsIFormControl> formCtrl =
      do_QueryInterface(mContent->GetParent());

    rv = formCtrl && formCtrl->GetType() == NS_FORM_INPUT_FILE;
  }
  return rv;
}

#ifdef DEBUG
NS_IMETHODIMP
nsGfxButtonControlFrame::GetFrameName(nsAString& aResult) const
{
  return MakeFrameName(NS_LITERAL_STRING("ButtonControl"), aResult);
}
#endif

// Create the text content used as label for the button.
// The frame will be generated by the frame constructor.
nsresult
nsGfxButtonControlFrame::CreateAnonymousContent(nsTArray<nsIContent*>& aElements)
{
  nsXPIDLString label;
  GetLabel(label);

  // Add a child text content node for the label
  NS_NewTextNode(getter_AddRefs(mTextContent),
                 mContent->NodeInfo()->NodeInfoManager());
  if (!mTextContent)
    return NS_ERROR_OUT_OF_MEMORY;

  // set the value of the text node and add it to the child list
  mTextContent->SetText(label, PR_FALSE);
  if (!aElements.AppendElement(mTextContent))
    return NS_ERROR_OUT_OF_MEMORY;
  return NS_OK;
}

void
nsGfxButtonControlFrame::AppendAnonymousContentTo(nsBaseContentList& aElements)
{
  aElements.MaybeAppendElement(mTextContent);
}

// Create the text content used as label for the button.
// The frame will be generated by the frame constructor.
nsIFrame*
nsGfxButtonControlFrame::CreateFrameFor(nsIContent*      aContent)
{
  nsIFrame * newFrame = nsnull;

  if (aContent == mTextContent) {
    nsIFrame * parentFrame = mFrames.FirstChild();

    nsPresContext* presContext = PresContext();
    nsRefPtr<nsStyleContext> textStyleContext;
    textStyleContext = presContext->StyleSet()->
      ResolveStyleForNonElement(mStyleContext);

    if (textStyleContext) {
      newFrame = NS_NewTextFrame(presContext->PresShell(), textStyleContext);
      if (newFrame) {
        // initialize the text frame
        newFrame->Init(mTextContent, parentFrame, nsnull);
      }
    }
  }

  return newFrame;
}

nsresult
nsGfxButtonControlFrame::GetFormProperty(nsIAtom* aName, nsAString& aValue) const
{
  nsresult rv = NS_OK;
  if (nsGkAtoms::defaultLabel == aName) {
    // This property is used by accessibility to get
    // the default label of the button.
    nsXPIDLString temp;
    rv = const_cast<nsGfxButtonControlFrame*>(this)->GetDefaultLabel(temp);
    aValue = temp;
  } else {
    aValue.Truncate();
  }
  return rv;
}

#ifdef ACCESSIBILITY
NS_IMETHODIMP nsGfxButtonControlFrame::GetAccessible(nsIAccessible** aAccessible)
{
  nsCOMPtr<nsIAccessibilityService> accService = do_GetService("@mozilla.org/accessibilityService;1");

  if (accService) {
    return accService->CreateHTMLButtonAccessible(static_cast<nsIFrame*>(this), aAccessible);
  }

  return NS_ERROR_FAILURE;
}
#endif

NS_QUERYFRAME_HEAD(nsGfxButtonControlFrame)
  NS_QUERYFRAME_ENTRY(nsIAnonymousContentCreator)
NS_QUERYFRAME_TAIL_INHERITING(nsHTMLButtonControlFrame)

// Initially we hardcoded the default strings here.
// Next, we used html.css to store the default label for various types
// of buttons. (nsGfxButtonControlFrame::DoNavQuirksReflow rev 1.20)
// However, since html.css is not internationalized, we now grab the default
// label from a string bundle as is done for all other UI strings.
// See bug 16999 for further details.
nsresult
nsGfxButtonControlFrame::GetDefaultLabel(nsXPIDLString& aString)
{
  nsCOMPtr<nsIFormControl> form = do_QueryInterface(mContent);
  NS_ENSURE_TRUE(form, NS_ERROR_UNEXPECTED);

  PRInt32 type = form->GetType();
  const char *prop;
  if (type == NS_FORM_INPUT_RESET) {
    prop = "Reset";
  } 
  else if (type == NS_FORM_INPUT_SUBMIT) {
    prop = "Submit";
  } 
  else if (IsFileBrowseButton(type)) {
    prop = "Browse";
  }
  else {
    aString.Truncate();
    return NS_OK;
  }

  return nsContentUtils::GetLocalizedString(nsContentUtils::eFORMS_PROPERTIES,
                                            prop, aString);
}

nsresult
nsGfxButtonControlFrame::GetLabel(nsXPIDLString& aLabel)
{
  // Get the text from the "value" property on our content if there is
  // one; otherwise set it to a default value (localized).
  nsresult rv;
  nsCOMPtr<nsIDOMHTMLInputElement> elt = do_QueryInterface(mContent);
  if (mContent->HasAttr(kNameSpaceID_None, nsGkAtoms::value) && elt) {
    rv = elt->GetValue(aLabel);
  } else {
    // Generate localized label.
    // We can't make any assumption as to what the default would be
    // because the value is localized for non-english platforms, thus
    // it might not be the string "Reset", "Submit Query", or "Browse..."
    rv = GetDefaultLabel(aLabel);
  }

  NS_ENSURE_SUCCESS(rv, rv);

  // Compress whitespace out of label if needed.
  if (!GetStyleText()->WhiteSpaceIsSignificant()) {
    aLabel.CompressWhitespace();
  } else if (aLabel.Length() > 2 && aLabel.First() == ' ' &&
             aLabel.CharAt(aLabel.Length() - 1) == ' ') {
    // This is a bit of a hack.  The reason this is here is as follows: we now
    // have default padding on our buttons to make them non-ugly.
    // Unfortunately, IE-windows does not have such padding, so people will
    // stick values like " ok " (with the spaces) in the buttons in an attempt
    // to make them look decent.  Unfortunately, if they do this the button
    // looks way too big in Mozilla.  Worse yet, if they do this _and_ set a
    // fixed width for the button we run into trouble because our focus-rect
    // border/padding and outer border take up 10px of the horizontal button
    // space or so; the result is that the text is misaligned, even with the
    // recentering we do in nsHTMLButtonFrame::Reflow.  So to solve this, even
    // if the whitespace is significant, single leading and trailing _spaces_
    // (and not other whitespace) are removed.  The proper solution, of
    // course, is to not have the focus rect painting taking up 6px of
    // horizontal space. We should do that instead (via XBL form controls or
    // changing the renderer) and remove this.
    aLabel.Cut(0, 1);
    aLabel.Truncate(aLabel.Length() - 1);
  }

  return NS_OK;
}

NS_IMETHODIMP
nsGfxButtonControlFrame::AttributeChanged(PRInt32         aNameSpaceID,
                                          nsIAtom*        aAttribute,
                                          PRInt32         aModType)
{
  nsresult rv = NS_OK;

  // If the value attribute is set, update the text of the label
  if (nsGkAtoms::value == aAttribute) {
    if (mTextContent && mContent) {
      nsXPIDLString label;
      rv = GetLabel(label);
      NS_ENSURE_SUCCESS(rv, rv);
    
      mTextContent->SetText(label, PR_TRUE);
    } else {
      rv = NS_ERROR_UNEXPECTED;
    }

  // defer to HTMLButtonControlFrame
  } else {
    rv = nsHTMLButtonControlFrame::AttributeChanged(aNameSpaceID, aAttribute, aModType);
  }
  return rv;
}

PRBool
nsGfxButtonControlFrame::IsLeaf() const
{
  return PR_TRUE;
}

nsIFrame*
nsGfxButtonControlFrame::GetContentInsertionFrame()
{
  return this;
}

NS_IMETHODIMP
nsGfxButtonControlFrame::HandleEvent(nsPresContext* aPresContext, 
                                      nsGUIEvent*     aEvent,
                                      nsEventStatus*  aEventStatus)
{
  // Override the HandleEvent to prevent the nsFrame::HandleEvent
  // from being called. The nsFrame::HandleEvent causes the button label
  // to be selected (Drawn with an XOR rectangle over the label)

  // do we have user-input style?
  const nsStyleUserInterface* uiStyle = GetStyleUserInterface();
  if (uiStyle->mUserInput == NS_STYLE_USER_INPUT_NONE || uiStyle->mUserInput == NS_STYLE_USER_INPUT_DISABLED)
    return nsFrame::HandleEvent(aPresContext, aEvent, aEventStatus);
  
  return NS_OK;
}
