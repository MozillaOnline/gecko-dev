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
 *   John Gaunt (jgaunt@netscape.com) (original author)
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

#include "nsHTMLWin32ObjectAccessible.h"
#include "nsAccessibleWrap.h"

////////////////////////////////////////////////////////////////////////////////
// nsHTMLWin32ObjectOwnerAccessible
////////////////////////////////////////////////////////////////////////////////

nsHTMLWin32ObjectOwnerAccessible::
  nsHTMLWin32ObjectOwnerAccessible(nsIDOMNode* aNode, nsIWeakReference* aShell,
                                   void* aHwnd) :
  nsAccessibleWrap(aNode, aShell)
{
  mHwnd = aHwnd;

  // Our only child is a nsHTMLWin32ObjectAccessible object.
  mNativeAccessible = new nsHTMLWin32ObjectAccessible(mHwnd);
}

////////////////////////////////////////////////////////////////////////////////
// nsHTMLWin32ObjectOwnerAccessible: nsAccessNode implementation

nsresult
nsHTMLWin32ObjectOwnerAccessible::Shutdown()
{
  nsAccessibleWrap::Shutdown();
  mNativeAccessible = nsnull;
  return NS_OK;
}

////////////////////////////////////////////////////////////////////////////////
// nsHTMLWin32ObjectOwnerAccessible: nsAccessible implementation

nsresult
nsHTMLWin32ObjectOwnerAccessible::GetRoleInternal(PRUint32 *aRole)
{
  NS_ENSURE_ARG_POINTER(aRole);

  *aRole = nsIAccessibleRole::ROLE_EMBEDDED_OBJECT;
  return NS_OK;
}

nsresult
nsHTMLWin32ObjectOwnerAccessible::GetStateInternal(PRUint32 *aState,
                                                   PRUint32 *aExtraState)
{
  nsresult rv = nsAccessibleWrap::GetStateInternal(aState, aExtraState);
  if (rv == NS_OK_DEFUNCT_OBJECT)
    return rv;

  // XXX: No HWND means this is windowless plugin which is not accessible in
  // the meantime.
  if (!mHwnd)
    *aState = nsIAccessibleStates::STATE_UNAVAILABLE;

  return rv;
}

////////////////////////////////////////////////////////////////////////////////
// nsHTMLWin32ObjectOwnerAccessible: nsAccessible protected implementation

void
nsHTMLWin32ObjectOwnerAccessible::CacheChildren()
{
  if (mNativeAccessible) {
    mChildren.AppendElement(mNativeAccessible);
    mNativeAccessible->SetParent(this);
  }
}


////////////////////////////////////////////////////////////////////////////////
// nsHTMLWin32ObjectAccessible
////////////////////////////////////////////////////////////////////////////////

nsHTMLWin32ObjectAccessible::nsHTMLWin32ObjectAccessible(void* aHwnd):
nsLeafAccessible(nsnull, nsnull)
{
  mHwnd = aHwnd;
  if (mHwnd) {
    // The plugin is not windowless. In this situation we use 
    // use its inner child owned by the plugin so that we don't get
    // in an infinite loop, where the WM_GETOBJECT's get forwarded
    // back to us and create another nsHTMLWin32ObjectAccessible
    HWND childWnd = ::GetWindow((HWND)aHwnd, GW_CHILD);
    if (childWnd) {
      mHwnd = childWnd;
    }
  }
}

NS_IMPL_ISUPPORTS_INHERITED1(nsHTMLWin32ObjectAccessible, nsAccessible, nsIAccessibleWin32Object)

NS_IMETHODIMP nsHTMLWin32ObjectAccessible::GetHwnd(void **aHwnd) 
{
  *aHwnd = mHwnd;
  return NS_OK;
}

