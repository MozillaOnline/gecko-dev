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
 *   Aaron Leventhal (aaronl@netscape.com)
 *   Kyle Yuan (kyle.yuan@sun.com)
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

#include "nsHTMLTextAccessible.h"
#include "nsIAccessibleDocument.h"
#include "nsIAccessibleEvent.h"
#include "nsIFrame.h"
#include "nsPresContext.h"
#include "nsIPresShell.h"
#include "nsISelection.h"
#include "nsISelectionController.h"
#include "nsComponentManagerUtils.h"

////////////////////////////////////////////////////////////////////////////////
// nsHTMLTextAccessible
////////////////////////////////////////////////////////////////////////////////

nsHTMLTextAccessible::nsHTMLTextAccessible(nsIDOMNode* aDomNode, nsIWeakReference* aShell):
nsTextAccessibleWrap(aDomNode, aShell)
{ 
}

NS_IMPL_ISUPPORTS_INHERITED0(nsHTMLTextAccessible, nsTextAccessible)

NS_IMETHODIMP
nsHTMLTextAccessible::GetName(nsAString& aName)
{
  // Text node, ARIA can't be used.
  aName.Truncate();
  return AppendTextTo(aName, 0, PR_UINT32_MAX);
}

nsresult
nsHTMLTextAccessible::GetRoleInternal(PRUint32 *aRole)
{
  nsIFrame *frame = GetFrame();
  // Don't return on null frame -- we still return a role
  // after accessible is shutdown/DEFUNCT
  if (frame && frame->IsGeneratedContentFrame()) {
    *aRole = nsIAccessibleRole::ROLE_STATICTEXT;
    return NS_OK;
  }

  return nsTextAccessible::GetRoleInternal(aRole);
}

nsresult
nsHTMLTextAccessible::GetStateInternal(PRUint32 *aState, PRUint32 *aExtraState)
{
  nsresult rv = nsTextAccessible::GetStateInternal(aState, aExtraState);
  NS_ENSURE_A11Y_SUCCESS(rv, rv);

  nsCOMPtr<nsIAccessible> docAccessible = 
    do_QueryInterface(nsCOMPtr<nsIAccessibleDocument>(GetDocAccessible()));
  if (docAccessible) {
     PRUint32 state, extState;
     docAccessible->GetState(&state, &extState);
     if (0 == (extState & nsIAccessibleStates::EXT_STATE_EDITABLE)) {
       *aState |= nsIAccessibleStates::STATE_READONLY; // Links not focusable in editor
     }
  }

  return NS_OK;
}

nsresult
nsHTMLTextAccessible::GetAttributesInternal(nsIPersistentProperties *aAttributes)
{
  if (!mDOMNode) {
    return NS_ERROR_FAILURE;  // Node already shut down
  }

  PRUint32 role;
  GetRoleInternal(&role);
  if (role == nsIAccessibleRole::ROLE_STATICTEXT) {
    nsAutoString oldValueUnused;
    aAttributes->SetStringProperty(NS_LITERAL_CSTRING("auto-generated"),
                                  NS_LITERAL_STRING("true"), oldValueUnused);
  }

  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsHTMLHRAccessible
////////////////////////////////////////////////////////////////////////////////

nsHTMLHRAccessible::nsHTMLHRAccessible(nsIDOMNode* aDomNode, nsIWeakReference* aShell):
nsLeafAccessible(aDomNode, aShell)
{ 
}

nsresult
nsHTMLHRAccessible::GetRoleInternal(PRUint32 *aRole)
{
  *aRole = nsIAccessibleRole::ROLE_SEPARATOR;
  return NS_OK;
}

nsHTMLBRAccessible::nsHTMLBRAccessible(nsIDOMNode* aDomNode, nsIWeakReference* aShell):
nsLeafAccessible(aDomNode, aShell)
{ 
}

nsresult
nsHTMLBRAccessible::GetRoleInternal(PRUint32 *aRole)
{
  *aRole = nsIAccessibleRole::ROLE_WHITESPACE;
  return NS_OK;
}

nsresult
nsHTMLBRAccessible::GetStateInternal(PRUint32 *aState, PRUint32 *aExtraState)
{
  *aState = 0;

  if (IsDefunct()) {
    if (aExtraState)
      *aExtraState = nsIAccessibleStates::EXT_STATE_DEFUNCT;

    return NS_OK_DEFUNCT_OBJECT;
  }

  *aState = nsIAccessibleStates::STATE_READONLY;
  if (aExtraState)
    *aExtraState = 0;

  return NS_OK;
}

nsresult
nsHTMLBRAccessible::GetNameInternal(nsAString& aName)
{
  aName = static_cast<PRUnichar>('\n');    // Newline char
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsHTMLLabelAccessible
////////////////////////////////////////////////////////////////////////////////

NS_IMPL_ISUPPORTS_INHERITED0(nsHTMLLabelAccessible, nsHyperTextAccessible)

nsHTMLLabelAccessible::
  nsHTMLLabelAccessible(nsIDOMNode* aDomNode, nsIWeakReference* aShell) :
  nsHyperTextAccessibleWrap(aDomNode, aShell)
{
}

nsresult
nsHTMLLabelAccessible::GetNameInternal(nsAString& aName)
{
  return nsTextEquivUtils::GetNameFromSubtree(this, aName);
}

nsresult
nsHTMLLabelAccessible::GetRoleInternal(PRUint32 *aRole)
{
  *aRole = nsIAccessibleRole::ROLE_LABEL;
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsHTMLLIAccessible
////////////////////////////////////////////////////////////////////////////////

nsHTMLLIAccessible::
  nsHTMLLIAccessible(nsIDOMNode *aDOMNode, nsIWeakReference* aShell, 
                     const nsAString& aBulletText):
  nsHyperTextAccessibleWrap(aDOMNode, aShell)
{
  if (!aBulletText.IsEmpty()) {
    mBulletAccessible = new nsHTMLListBulletAccessible(mDOMNode, mWeakShell, 
                                                       aBulletText);
    if (mBulletAccessible)
      mBulletAccessible->Init();
  }
}

NS_IMPL_ISUPPORTS_INHERITED0(nsHTMLLIAccessible, nsHyperTextAccessible)

nsresult
nsHTMLLIAccessible::Shutdown()
{
  if (mBulletAccessible) {
    // Ensure that pointer to this is nulled out.
    mBulletAccessible->Shutdown();
  }

  nsresult rv = nsHyperTextAccessibleWrap::Shutdown();
  mBulletAccessible = nsnull;
  return rv;
}

nsresult
nsHTMLLIAccessible::GetRoleInternal(PRUint32 *aRole)
{
  *aRole = nsIAccessibleRole::ROLE_LISTITEM;
  return NS_OK;
}

nsresult
nsHTMLLIAccessible::GetStateInternal(PRUint32 *aState, PRUint32 *aExtraState)
{
  nsresult rv = nsHyperTextAccessibleWrap::GetStateInternal(aState,
                                                            aExtraState);
  NS_ENSURE_A11Y_SUCCESS(rv, rv);

  *aState |= nsIAccessibleStates::STATE_READONLY;
  return NS_OK;
}

NS_IMETHODIMP nsHTMLLIAccessible::GetBounds(PRInt32 *x, PRInt32 *y, PRInt32 *width, PRInt32 *height)
{
  nsresult rv = nsAccessibleWrap::GetBounds(x, y, width, height);
  if (NS_FAILED(rv) || !mBulletAccessible) {
    return rv;
  }

  PRInt32 bulletX, bulletY, bulletWidth, bulletHeight;
  rv = mBulletAccessible->GetBounds(&bulletX, &bulletY, &bulletWidth, &bulletHeight);
  NS_ENSURE_SUCCESS(rv, rv);

  *x = bulletX; // Move x coordinate of list item over to cover bullet as well
  *width += bulletWidth;
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsHTMLLIAccessible: nsAccessible protected

void
nsHTMLLIAccessible::CacheChildren()
{
  if (mBulletAccessible) {
    mChildren.AppendElement(mBulletAccessible);
    mBulletAccessible->SetParent(this);
  }

  // Cache children from subtree.
  nsAccessibleWrap::CacheChildren();
}

////////////////////////////////////////////////////////////////////////////////
// nsHTMLListBulletAccessible
////////////////////////////////////////////////////////////////////////////////

nsHTMLListBulletAccessible::
  nsHTMLListBulletAccessible(nsIDOMNode* aDomNode, nsIWeakReference* aShell,
                             const nsAString& aBulletText) :
    nsLeafAccessible(aDomNode, aShell), mBulletText(aBulletText)
{
  mBulletText += ' '; // Otherwise bullets are jammed up against list text
}

NS_IMETHODIMP
nsHTMLListBulletAccessible::GetUniqueID(void **aUniqueID)
{
  // Since mDOMNode is same as for list item, use |this| pointer as the unique Id
  *aUniqueID = static_cast<void*>(this);
  return NS_OK;
}

nsresult
nsHTMLListBulletAccessible::Shutdown()
{
  mBulletText.Truncate();
  return nsLeafAccessible::Shutdown();
}

NS_IMETHODIMP
nsHTMLListBulletAccessible::GetName(nsAString &aName)
{
  // Native anonymous content, ARIA can't be used.
  aName = mBulletText;
  return NS_OK;
}

nsresult
nsHTMLListBulletAccessible::GetRoleInternal(PRUint32 *aRole)
{
  *aRole = nsIAccessibleRole::ROLE_STATICTEXT;
  return NS_OK;
}

nsresult
nsHTMLListBulletAccessible::GetStateInternal(PRUint32 *aState, PRUint32 *aExtraState)
{
  nsresult rv = nsLeafAccessible::GetStateInternal(aState, aExtraState);
  NS_ENSURE_A11Y_SUCCESS(rv, rv);

  *aState &= ~nsIAccessibleStates::STATE_FOCUSABLE;
  *aState |= nsIAccessibleStates::STATE_READONLY;
  return NS_OK;
}

nsresult
nsHTMLListBulletAccessible::AppendTextTo(nsAString& aText, PRUint32 aStartOffset,
                                         PRUint32 aLength)
{
  PRUint32 maxLength = mBulletText.Length() - aStartOffset;
  if (aLength > maxLength) {
    aLength = maxLength;
  }
  aText += Substring(mBulletText, aStartOffset, aLength);
  return NS_OK;
}

nsAccessible*
nsHTMLListBulletAccessible::GetParent()
{
  return mParent;
}

////////////////////////////////////////////////////////////////////////////////
// nsHTMLListAccessible
////////////////////////////////////////////////////////////////////////////////

NS_IMPL_ISUPPORTS_INHERITED0(nsHTMLListAccessible, nsHyperTextAccessible)

nsresult
nsHTMLListAccessible::GetRoleInternal(PRUint32 *aRole)
{
  *aRole = nsIAccessibleRole::ROLE_LIST;
  return NS_OK;
}

nsresult
nsHTMLListAccessible::GetStateInternal(PRUint32 *aState, PRUint32 *aExtraState)
{
  nsresult rv = nsHyperTextAccessibleWrap::GetStateInternal(aState,
                                                            aExtraState);
  NS_ENSURE_A11Y_SUCCESS(rv, rv);

  *aState |= nsIAccessibleStates::STATE_READONLY;
  return NS_OK;
}

