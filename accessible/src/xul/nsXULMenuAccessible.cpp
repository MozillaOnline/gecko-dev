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
 *   Author: Aaron Leventhal (aaronl@netscape.com)
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

#include "nsXULMenuAccessible.h"
#include "nsIDOMElement.h"
#include "nsIDOMXULElement.h"
#include "nsIMutableArray.h"
#include "nsIDOMXULContainerElement.h"
#include "nsIDOMXULSelectCntrlItemEl.h"
#include "nsIDOMXULMultSelectCntrlEl.h"
#include "nsIDOMKeyEvent.h"
#include "nsIPrefService.h"
#include "nsIPrefBranch.h"
#include "nsIServiceManager.h"
#include "nsIPresShell.h"
#include "nsIContent.h"
#include "nsGUIEvent.h"
#include "nsXULFormControlAccessible.h"
#include "nsILookAndFeel.h"
#include "nsWidgetsCID.h"


static NS_DEFINE_CID(kLookAndFeelCID, NS_LOOKANDFEEL_CID);

/** ------------------------------------------------------ */
/**  Impl. of nsXULSelectableAccessible                    */
/** ------------------------------------------------------ */

// Helper methos
nsXULSelectableAccessible::nsXULSelectableAccessible(nsIDOMNode* aDOMNode,
                                                     nsIWeakReference* aShell):
nsAccessibleWrap(aDOMNode, aShell)
{
  mSelectControl = do_QueryInterface(aDOMNode);
}

NS_IMPL_ISUPPORTS_INHERITED1(nsXULSelectableAccessible, nsAccessible, nsIAccessibleSelectable)

nsresult
nsXULSelectableAccessible::Shutdown()
{
  mSelectControl = nsnull;
  return nsAccessibleWrap::Shutdown();
}

nsresult nsXULSelectableAccessible::ChangeSelection(PRInt32 aIndex, PRUint8 aMethod, PRBool *aSelState)
{
  *aSelState = PR_FALSE;

  if (!mSelectControl) {
    return NS_ERROR_FAILURE;
  }
  nsAccessible* child = GetChildAt(aIndex);
  NS_ENSURE_TRUE(child, NS_ERROR_FAILURE);

  nsCOMPtr<nsIDOMNode> childNode;
  child->GetDOMNode(getter_AddRefs(childNode));
  nsCOMPtr<nsIDOMXULSelectControlItemElement> item(do_QueryInterface(childNode));
  NS_ENSURE_TRUE(item, NS_ERROR_FAILURE);

  item->GetSelected(aSelState);
  if (eSelection_GetState == aMethod) {
    return NS_OK;
  }

  nsCOMPtr<nsIDOMXULMultiSelectControlElement> xulMultiSelect =
    do_QueryInterface(mSelectControl);

  if (eSelection_Add == aMethod && !(*aSelState)) {
    return xulMultiSelect ? xulMultiSelect->AddItemToSelection(item) :
                            mSelectControl->SetSelectedItem(item);
  }
  if (eSelection_Remove == aMethod && (*aSelState)) {
    return xulMultiSelect ? xulMultiSelect->RemoveItemFromSelection(item) :
                            mSelectControl->SetSelectedItem(nsnull);
  }
  return NS_ERROR_FAILURE;
}

// Interface methods
NS_IMETHODIMP nsXULSelectableAccessible::GetSelectedChildren(nsIArray **aChildren)
{
  *aChildren = nsnull;
  if (!mSelectControl) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIMutableArray> selectedAccessibles =
    do_CreateInstance(NS_ARRAY_CONTRACTID);
  NS_ENSURE_STATE(selectedAccessibles);

  // For XUL multi-select control
  nsCOMPtr<nsIDOMXULMultiSelectControlElement> xulMultiSelect =
    do_QueryInterface(mSelectControl);
  nsCOMPtr<nsIAccessible> selectedAccessible;
  if (xulMultiSelect) {
    PRInt32 length = 0;
    xulMultiSelect->GetSelectedCount(&length);
    for (PRInt32 index = 0; index < length; index++) {
      nsCOMPtr<nsIDOMXULSelectControlItemElement> selectedItem;
      xulMultiSelect->GetSelectedItem(index, getter_AddRefs(selectedItem));
      nsCOMPtr<nsIDOMNode> selectedNode(do_QueryInterface(selectedItem));
      GetAccService()->GetAccessibleInWeakShell(selectedNode, mWeakShell,
                                            getter_AddRefs(selectedAccessible));
      if (selectedAccessible)
        selectedAccessibles->AppendElement(selectedAccessible, PR_FALSE);
    }
  }
  else {  // Single select?
    nsCOMPtr<nsIDOMXULSelectControlItemElement> selectedItem;
    mSelectControl->GetSelectedItem(getter_AddRefs(selectedItem));
    nsCOMPtr<nsIDOMNode> selectedNode(do_QueryInterface(selectedItem));
    if(selectedNode) {
      GetAccService()->GetAccessibleInWeakShell(selectedNode, mWeakShell,
                                            getter_AddRefs(selectedAccessible));
      if (selectedAccessible)
        selectedAccessibles->AppendElement(selectedAccessible, PR_FALSE);
    }
  }

  PRUint32 uLength = 0;
  selectedAccessibles->GetLength(&uLength);
  if (uLength != 0) { // length of nsIArray containing selected options
    NS_ADDREF(*aChildren = selectedAccessibles);
  }

  return NS_OK;
}

// return the nth selected child's nsIAccessible object
NS_IMETHODIMP nsXULSelectableAccessible::RefSelection(PRInt32 aIndex, nsIAccessible **aAccessible)
{
  *aAccessible = nsnull;
  if (!mSelectControl) {
    return NS_ERROR_FAILURE;
  }

  nsCOMPtr<nsIDOMXULSelectControlItemElement> selectedItem;
  nsCOMPtr<nsIDOMXULMultiSelectControlElement> xulMultiSelect =
    do_QueryInterface(mSelectControl);
  if (xulMultiSelect)
    xulMultiSelect->GetSelectedItem(aIndex, getter_AddRefs(selectedItem));

  if (aIndex == 0)
    mSelectControl->GetSelectedItem(getter_AddRefs(selectedItem));

  if (selectedItem)
    GetAccService()->GetAccessibleInWeakShell(selectedItem, mWeakShell,
                                              aAccessible);

  return (*aAccessible) ? NS_OK : NS_ERROR_FAILURE;
}

NS_IMETHODIMP nsXULSelectableAccessible::GetSelectionCount(PRInt32 *aSelectionCount)
{
  *aSelectionCount = 0;
  if (!mSelectControl) {
    return NS_ERROR_FAILURE;
  }

  // For XUL multi-select control
  nsCOMPtr<nsIDOMXULMultiSelectControlElement> xulMultiSelect =
    do_QueryInterface(mSelectControl);
  if (xulMultiSelect)
    return xulMultiSelect->GetSelectedCount(aSelectionCount);

  // For XUL single-select control/menulist
  PRInt32 index;
  mSelectControl->GetSelectedIndex(&index);
  if (index >= 0)
    *aSelectionCount = 1;
  return NS_OK;
}

NS_IMETHODIMP nsXULSelectableAccessible::AddChildToSelection(PRInt32 aIndex)
{
  PRBool isSelected;
  return ChangeSelection(aIndex, eSelection_Add, &isSelected);
}

NS_IMETHODIMP nsXULSelectableAccessible::RemoveChildFromSelection(PRInt32 aIndex)
{
  PRBool isSelected;
  return ChangeSelection(aIndex, eSelection_Remove, &isSelected);
}

NS_IMETHODIMP nsXULSelectableAccessible::IsChildSelected(PRInt32 aIndex, PRBool *aIsSelected)
{
  *aIsSelected = PR_FALSE;
  return ChangeSelection(aIndex, eSelection_GetState, aIsSelected);
}

NS_IMETHODIMP nsXULSelectableAccessible::ClearSelection()
{
  if (!mSelectControl) {
    return NS_ERROR_FAILURE;
  }
  nsCOMPtr<nsIDOMXULMultiSelectControlElement> xulMultiSelect =
    do_QueryInterface(mSelectControl);
  return xulMultiSelect ? xulMultiSelect->ClearSelection() : mSelectControl->SetSelectedIndex(-1);
}

NS_IMETHODIMP nsXULSelectableAccessible::SelectAllSelection(PRBool *aSucceeded)
{
  *aSucceeded = PR_TRUE;

  nsCOMPtr<nsIDOMXULMultiSelectControlElement> xulMultiSelect =
    do_QueryInterface(mSelectControl);
  if (xulMultiSelect)
    return xulMultiSelect->SelectAll();

  // otherwise, don't support this method
  *aSucceeded = PR_FALSE;
  return NS_ERROR_NOT_IMPLEMENTED;
}


// ------------------------ Menu Item -----------------------------

nsXULMenuitemAccessible::nsXULMenuitemAccessible(nsIDOMNode* aDOMNode, nsIWeakReference* aShell): 
nsAccessibleWrap(aDOMNode, aShell)
{ 
}

nsresult
nsXULMenuitemAccessible::Init()
{
  nsresult rv = nsAccessibleWrap::Init();
  nsCoreUtils::GeneratePopupTree(mDOMNode);
  return rv;
}

nsresult
nsXULMenuitemAccessible::GetStateInternal(PRUint32 *aState,
                                          PRUint32 *aExtraState)
{
  nsresult rv = nsAccessible::GetStateInternal(aState, aExtraState);
  NS_ENSURE_A11Y_SUCCESS(rv, rv);

  // Focused?
  nsCOMPtr<nsIDOMElement> element(do_QueryInterface(mDOMNode));
  if (!element)
    return NS_ERROR_FAILURE;
  PRBool isFocused = PR_FALSE;
  element->HasAttribute(NS_LITERAL_STRING("_moz-menuactive"), &isFocused); 
  if (isFocused)
    *aState |= nsIAccessibleStates::STATE_FOCUSED;

  // Has Popup?
  nsAutoString tagName;
  element->GetLocalName(tagName);
  if (tagName.EqualsLiteral("menu")) {
    *aState |= nsIAccessibleStates::STATE_HASPOPUP;
    PRBool isOpen;
    element->HasAttribute(NS_LITERAL_STRING("open"), &isOpen);
    if (isOpen) {
      *aState |= nsIAccessibleStates::STATE_EXPANDED;
    }
    else {
      *aState |= nsIAccessibleStates::STATE_COLLAPSED;
    }
  }

  nsAutoString menuItemType;
  element->GetAttribute(NS_LITERAL_STRING("type"), menuItemType); 

  if (!menuItemType.IsEmpty()) {
    // Checkable?
    if (menuItemType.EqualsIgnoreCase("radio") ||
        menuItemType.EqualsIgnoreCase("checkbox"))
      *aState |= nsIAccessibleStates::STATE_CHECKABLE;

    // Checked?
    nsAutoString checkValue;
    element->GetAttribute(NS_LITERAL_STRING("checked"), checkValue);
    if (checkValue.EqualsLiteral("true")) {
      *aState |= nsIAccessibleStates::STATE_CHECKED;
    }
  }

  // Combo box listitem
  PRBool isComboboxOption =
    (nsAccUtils::Role(this) == nsIAccessibleRole::ROLE_COMBOBOX_OPTION);
  if (isComboboxOption) {
    // Is selected?
    PRBool isSelected = PR_FALSE;
    nsCOMPtr<nsIDOMXULSelectControlItemElement>
      item(do_QueryInterface(mDOMNode));
    NS_ENSURE_TRUE(item, NS_ERROR_FAILURE);
    item->GetSelected(&isSelected);

    // Is collapsed?
    PRBool isCollapsed = PR_FALSE;
    nsAccessible* parentAcc = GetParent();
    if (nsAccUtils::State(parentAcc) & nsIAccessibleStates::STATE_INVISIBLE)
      isCollapsed = PR_TRUE;

    if (isSelected) {
      *aState |= nsIAccessibleStates::STATE_SELECTED;

      // Selected and collapsed?
      if (isCollapsed) {
        // Set selected option offscreen/invisible according to combobox state
        nsAccessible* grandParentAcc = parentAcc->GetParent();
        NS_ENSURE_TRUE(grandParentAcc, NS_ERROR_FAILURE);
        NS_ASSERTION(nsAccUtils::Role(grandParentAcc) == nsIAccessibleRole::ROLE_COMBOBOX,
                     "grandparent of combobox listitem is not combobox");
        PRUint32 grandParentState, grandParentExtState;
        grandParentAcc->GetState(&grandParentState, &grandParentExtState);
        *aState &= ~(nsIAccessibleStates::STATE_OFFSCREEN |
                     nsIAccessibleStates::STATE_INVISIBLE);
        *aState |= (grandParentState & nsIAccessibleStates::STATE_OFFSCREEN) |
                   (grandParentState & nsIAccessibleStates::STATE_INVISIBLE);
        if (aExtraState) {
          *aExtraState |=
            grandParentExtState & nsIAccessibleStates::EXT_STATE_OPAQUE;
        }
      } // isCollapsed
    } // isSelected
  } // ROLE_COMBOBOX_OPTION

  // Set focusable and selectable for items that are available
  // and whose metric setting does allow disabled items to be focused.
  if (*aState & nsIAccessibleStates::STATE_UNAVAILABLE) {
    // Honour the LookAndFeel metric.
    nsCOMPtr<nsILookAndFeel> lookNFeel(do_GetService(kLookAndFeelCID));
    PRInt32 skipDisabledMenuItems = 0;
    lookNFeel->GetMetric(nsILookAndFeel::eMetric_SkipNavigatingDisabledMenuItem,
                         skipDisabledMenuItems);
    // We don't want the focusable and selectable states for combobox items,
    // so exclude them here as well.
    if (skipDisabledMenuItems || isComboboxOption) {
      return NS_OK;
    }
  }
  *aState|= (nsIAccessibleStates::STATE_FOCUSABLE |
             nsIAccessibleStates::STATE_SELECTABLE);

  return NS_OK;
}

nsresult
nsXULMenuitemAccessible::GetNameInternal(nsAString& aName)
{
  nsCOMPtr<nsIContent> content(do_QueryInterface(mDOMNode));
  content->GetAttr(kNameSpaceID_None, nsAccessibilityAtoms::label, aName);
  return NS_OK;
}

NS_IMETHODIMP nsXULMenuitemAccessible::GetDescription(nsAString& aDescription)
{
  nsCOMPtr<nsIDOMElement> element(do_QueryInterface(mDOMNode));
  if (!element) {
    return NS_ERROR_FAILURE;
  }
  element->GetAttribute(NS_LITERAL_STRING("description"), aDescription);

  return NS_OK;
}

//return menu accesskey: N or Alt+F
NS_IMETHODIMP
nsXULMenuitemAccessible::GetKeyboardShortcut(nsAString& aAccessKey)
{
  aAccessKey.Truncate();

  static PRInt32 gMenuAccesskeyModifier = -1;  // magic value of -1 indicates unitialized state

  nsCOMPtr<nsIDOMElement> elt(do_QueryInterface(mDOMNode));
  if (elt) {
    nsAutoString accesskey;
    // We do not use nsCoreUtils::GetAccesskeyFor() because accesskeys for
    // menu are't registered by nsIEventStateManager.
    elt->GetAttribute(NS_LITERAL_STRING("accesskey"), accesskey);
    if (accesskey.IsEmpty())
      return NS_OK;

    nsAccessible* parentAcc = GetParent();
    if (parentAcc) {
      if (nsAccUtils::RoleInternal(parentAcc) ==
          nsIAccessibleRole::ROLE_MENUBAR) {
        // If top level menu item, add Alt+ or whatever modifier text to string
        // No need to cache pref service, this happens rarely
        if (gMenuAccesskeyModifier == -1) {
          // Need to initialize cached global accesskey pref
          gMenuAccesskeyModifier = 0;
          nsCOMPtr<nsIPrefBranch> prefBranch(do_GetService(NS_PREFSERVICE_CONTRACTID));
          if (prefBranch)
            prefBranch->GetIntPref("ui.key.menuAccessKey", &gMenuAccesskeyModifier);
        }
        nsAutoString propertyKey;
        switch (gMenuAccesskeyModifier) {
          case nsIDOMKeyEvent::DOM_VK_CONTROL: propertyKey.AssignLiteral("VK_CONTROL"); break;
          case nsIDOMKeyEvent::DOM_VK_ALT: propertyKey.AssignLiteral("VK_ALT"); break;
          case nsIDOMKeyEvent::DOM_VK_META: propertyKey.AssignLiteral("VK_META"); break;
        }
        if (!propertyKey.IsEmpty())
          nsAccessible::GetFullKeyName(propertyKey, accesskey, aAccessKey);
      }
    }
    if (aAccessKey.IsEmpty())
      aAccessKey = accesskey;
    return NS_OK;
  }
  return NS_ERROR_FAILURE;
}

//return menu shortcut: Ctrl+F or Ctrl+Shift+L
NS_IMETHODIMP
nsXULMenuitemAccessible::GetDefaultKeyBinding(nsAString& aKeyBinding)
{
  aKeyBinding.Truncate();

  nsCOMPtr<nsIDOMElement> elt(do_QueryInterface(mDOMNode));
  NS_ENSURE_TRUE(elt, NS_ERROR_FAILURE);

  nsAutoString accelText;
  elt->GetAttribute(NS_LITERAL_STRING("acceltext"), accelText);
  if (accelText.IsEmpty())
    return NS_OK;

  aKeyBinding = accelText;

  return NS_OK;
}

nsresult
nsXULMenuitemAccessible::GetRoleInternal(PRUint32 *aRole)
{
  nsCOMPtr<nsIDOMXULContainerElement> xulContainer(do_QueryInterface(mDOMNode));
  if (xulContainer) {
    *aRole = nsIAccessibleRole::ROLE_PARENT_MENUITEM;
    return NS_OK;
  }

  nsCOMPtr<nsIAccessible> parent;
  GetParent(getter_AddRefs(parent));
  if (nsAccUtils::Role(parent) == nsIAccessibleRole::ROLE_COMBOBOX_LIST) {
    *aRole = nsIAccessibleRole::ROLE_COMBOBOX_OPTION;
    return NS_OK;
  }

  *aRole = nsIAccessibleRole::ROLE_MENUITEM;
  nsCOMPtr<nsIDOMElement> element(do_QueryInterface(mDOMNode));
  if (!element)
    return NS_ERROR_FAILURE;
  nsAutoString menuItemType;
  element->GetAttribute(NS_LITERAL_STRING("type"), menuItemType);
  if (menuItemType.EqualsIgnoreCase("radio"))
    *aRole = nsIAccessibleRole::ROLE_RADIO_MENU_ITEM;
  else if (menuItemType.EqualsIgnoreCase("checkbox"))
    *aRole = nsIAccessibleRole::ROLE_CHECK_MENU_ITEM;

  return NS_OK;
}

PRInt32
nsXULMenuitemAccessible::GetLevelInternal()
{
  return nsAccUtils::GetLevelForXULContainerItem(mDOMNode);
}

void
nsXULMenuitemAccessible::GetPositionAndSizeInternal(PRInt32 *aPosInSet,
                                                    PRInt32 *aSetSize)
{
  nsAccUtils::GetPositionAndSizeForXULContainerItem(mDOMNode, aPosInSet,
                                                    aSetSize);
}

PRBool
nsXULMenuitemAccessible::GetAllowsAnonChildAccessibles()
{
  // That indicates we don't walk anonymous children for menuitems
  return PR_FALSE;
}

NS_IMETHODIMP nsXULMenuitemAccessible::DoAction(PRUint8 index)
{
  if (index == eAction_Click) {   // default action
    DoCommand();
    return NS_OK;
  }

  return NS_ERROR_INVALID_ARG;
}

/** select us! close combo box if necessary*/
NS_IMETHODIMP nsXULMenuitemAccessible::GetActionName(PRUint8 aIndex, nsAString& aName)
{
  if (aIndex == eAction_Click) {
    aName.AssignLiteral("click"); 
    return NS_OK;
  }
  return NS_ERROR_INVALID_ARG;
}

NS_IMETHODIMP nsXULMenuitemAccessible::GetNumActions(PRUint8 *_retval)
{
  *_retval = 1;
  return NS_OK;
}


// ------------------------ Menu Separator ----------------------------

nsXULMenuSeparatorAccessible::nsXULMenuSeparatorAccessible(nsIDOMNode* aDOMNode, nsIWeakReference* aShell): 
nsXULMenuitemAccessible(aDOMNode, aShell)
{ 
}

nsresult
nsXULMenuSeparatorAccessible::GetStateInternal(PRUint32 *aState,
                                               PRUint32 *aExtraState)
{
  // Isn't focusable, but can be offscreen/invisible -- only copy those states
  nsresult rv = nsXULMenuitemAccessible::GetStateInternal(aState, aExtraState);
  NS_ENSURE_A11Y_SUCCESS(rv, rv);

  *aState &= (nsIAccessibleStates::STATE_OFFSCREEN | 
              nsIAccessibleStates::STATE_INVISIBLE);

  return NS_OK;
}

nsresult
nsXULMenuSeparatorAccessible::GetNameInternal(nsAString& aName)
{
  return NS_OK;
}

nsresult
nsXULMenuSeparatorAccessible::GetRoleInternal(PRUint32 *aRole)
{
  *aRole = nsIAccessibleRole::ROLE_SEPARATOR;
  return NS_OK;
}

NS_IMETHODIMP nsXULMenuSeparatorAccessible::DoAction(PRUint8 index)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsXULMenuSeparatorAccessible::GetActionName(PRUint8 aIndex, nsAString& aName)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP nsXULMenuSeparatorAccessible::GetNumActions(PRUint8 *_retval)
{
  return NS_ERROR_NOT_IMPLEMENTED;
}
// ------------------------ Menu Popup -----------------------------

nsXULMenupopupAccessible::nsXULMenupopupAccessible(nsIDOMNode* aDOMNode, nsIWeakReference* aShell): 
  nsXULSelectableAccessible(aDOMNode, aShell)
{ 
  // May be the anonymous <menupopup> inside <menulist> (a combobox)
  nsCOMPtr<nsIDOMNode> parentNode;
  aDOMNode->GetParentNode(getter_AddRefs(parentNode));
  mSelectControl = do_QueryInterface(parentNode);
}

nsresult
nsXULMenupopupAccessible::GetStateInternal(PRUint32 *aState,
                                           PRUint32 *aExtraState)
{
  nsresult rv = nsAccessible::GetStateInternal(aState, aExtraState);
  NS_ENSURE_A11Y_SUCCESS(rv, rv);

#ifdef DEBUG_A11Y
  // We are onscreen if our parent is active
  PRBool isActive = PR_FALSE;

  nsCOMPtr<nsIDOMElement> element(do_QueryInterface(mDOMNode));
  element->HasAttribute(NS_LITERAL_STRING("menuactive"), &isActive);
  if (!isActive) {
    nsCOMPtr<nsIAccessible> parent(GetParent());
    nsCOMPtr<nsIDOMNode> parentNode;
    nsCOMPtr<nsIAccessNode> accessNode(do_QueryInterface(parent));
    if (accessNode) 
      accessNode->GetDOMNode(getter_AddRefs(parentNode));
    element = do_QueryInterface(parentNode);
    if (element)
      element->HasAttribute(NS_LITERAL_STRING("open"), &isActive);
  }
  NS_ASSERTION(isActive || *aState & nsIAccessibleStates::STATE_INVISIBLE,
               "XULMenupopup doesn't have STATE_INVISIBLE when it's inactive");
#endif

  if (*aState & nsIAccessibleStates::STATE_INVISIBLE)
    *aState |= (nsIAccessibleStates::STATE_OFFSCREEN |
                nsIAccessibleStates::STATE_COLLAPSED);

  return NS_OK;
}

nsresult
nsXULMenupopupAccessible::GetNameInternal(nsAString& aName)
{
  nsCOMPtr<nsIContent> content(do_QueryInterface(mDOMNode));
  while (content && aName.IsEmpty()) {
    content->GetAttr(kNameSpaceID_None, nsAccessibilityAtoms::label, aName);
    content = content->GetParent();
  }

  return NS_OK;
}

nsresult
nsXULMenupopupAccessible::GetRoleInternal(PRUint32 *aRole)
{
  nsCOMPtr<nsIContent> content(do_QueryInterface(mDOMNode));
  if (!content) {
    return NS_ERROR_FAILURE;
  }
  nsCOMPtr<nsIAccessible> parent;
  GetParent(getter_AddRefs(parent));
  if (parent) {
    PRUint32 role = nsAccUtils::Role(parent);
    if (role == nsIAccessibleRole::ROLE_COMBOBOX ||
        role == nsIAccessibleRole::ROLE_AUTOCOMPLETE) {
      *aRole = nsIAccessibleRole::ROLE_COMBOBOX_LIST;
      return NS_OK;

    } else if (role == nsIAccessibleRole::ROLE_PUSHBUTTON) {
      // Some widgets like the search bar have several popups, owned by buttons.
      nsCOMPtr<nsIAccessible> grandParent;
      parent->GetParent(getter_AddRefs(grandParent));
      if (role == nsIAccessibleRole::ROLE_AUTOCOMPLETE) {
        *aRole = nsIAccessibleRole::ROLE_COMBOBOX_LIST;
        return NS_OK;
      }
    }
  }

  *aRole = nsIAccessibleRole::ROLE_MENUPOPUP;
  return NS_OK;
}

// ------------------------ Menu Bar -----------------------------

nsXULMenubarAccessible::nsXULMenubarAccessible(nsIDOMNode* aDOMNode, nsIWeakReference* aShell): 
  nsAccessibleWrap(aDOMNode, aShell)
{ 
}

nsresult
nsXULMenubarAccessible::GetStateInternal(PRUint32 *aState,
                                         PRUint32 *aExtraState)
{
  nsresult rv = nsAccessible::GetStateInternal(aState, aExtraState);
  NS_ENSURE_A11Y_SUCCESS(rv, rv);

  // Menu bar iteself is not actually focusable
  *aState &= ~nsIAccessibleStates::STATE_FOCUSABLE;
  return rv;
}


nsresult
nsXULMenubarAccessible::GetNameInternal(nsAString& aName)
{
  aName.AssignLiteral("Application");
  return NS_OK;
}

nsresult
nsXULMenubarAccessible::GetRoleInternal(PRUint32 *aRole)
{
  *aRole = nsIAccessibleRole::ROLE_MENUBAR;
  return NS_OK;
}

