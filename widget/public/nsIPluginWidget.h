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
 * Portions created by the Initial Developer are Copyright (C) 2002
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
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

#include "nsISupports.h"

/* starting interface:    nsIPluginWidget */
#define NS_IPLUGINWIDGET_IID_STR "034E8A7E-BE36-4039-B229-39C41E9D4CD2"

#define NS_IPLUGINWIDGET_IID    \
  { 0x034E8A7E, 0xBE36, 0x4039, \
    { 0xB2, 0x29, 0x39, 0xC4, 0x1E, 0x9D, 0x4C, 0xD2 } }

struct nsIntPoint;
class nsIPluginInstanceOwner;

class NS_NO_VTABLE nsIPluginWidget : public nsISupports
{
 public: 

  NS_DECLARE_STATIC_IID_ACCESSOR(NS_IPLUGINWIDGET_IID)

  NS_IMETHOD GetPluginClipRect(nsIntRect& outClipRect, nsIntPoint& outOrigin, PRBool& outWidgetVisible) = 0;

  NS_IMETHOD StartDrawPlugin(void) = 0;

  NS_IMETHOD EndDrawPlugin(void) = 0;

  NS_IMETHOD SetPluginInstanceOwner(nsIPluginInstanceOwner* pluginInstanceOwner) = 0;

  NS_IMETHOD SetPluginEventModel(int inEventModel) = 0;

  NS_IMETHOD GetPluginEventModel(int* outEventModel) = 0;
};

NS_DEFINE_STATIC_IID_ACCESSOR(nsIPluginWidget, NS_IPLUGINWIDGET_IID)
