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
#ifndef nsPrintObject_h___
#define nsPrintObject_h___

// Interfaces
#include "nsCOMPtr.h"
#include "nsIContent.h"
#include "nsPresContext.h"
#include "nsIPresShell.h"
#include "nsStyleSet.h"
#include "nsIViewManager.h"
#include "nsIDocShell.h"
#include "nsIDocument.h"
#include "nsIWidget.h"

// nsPrintObject Document Type
enum PrintObjectType  {eDoc = 0, eFrame = 1, eIFrame = 2, eFrameSet = 3};

//---------------------------------------------------
//-- nsPrintObject Class
//---------------------------------------------------
class nsPrintObject
{

public:
  nsPrintObject();
  ~nsPrintObject(); // non-virtual

  // Methods
  nsresult Init(nsIDocShell* aDocShell, nsIDOMDocument* aDoc,
                PRBool aPrintPreview);

  PRBool IsPrintable()  { return !mDontPrint; }
  void   DestroyPresentation();

  // Data Members
  nsCOMPtr<nsIDocShell>    mDocShell;
  nsCOMPtr<nsIDocument>    mDocument;

  nsCOMPtr<nsPresContext>  mPresContext;
  nsCOMPtr<nsIPresShell>   mPresShell;
  nsCOMPtr<nsIViewManager> mViewManager;
  nsCOMPtr<nsIWidget>      mWindow;

  nsCOMPtr<nsIContent>     mContent;
  PrintObjectType  mFrameType;
  
  nsTArray<nsPrintObject*> mKids;
  nsPrintObject*   mParent;
  PRPackedBool     mHasBeenPrinted;
  PRPackedBool     mDontPrint;
  PRPackedBool     mPrintAsIs;
  PRPackedBool     mSharedPresShell;
  PRPackedBool     mInvisible;        // Indicates PO is set to not visible by CSS
  PRPackedBool     mPrintPreview;
  float            mShrinkRatio;
  float            mZoomRatio;

private:
  nsPrintObject& operator=(const nsPrintObject& aOther); // not implemented

};



#endif /* nsPrintObject_h___ */

