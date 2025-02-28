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
 * The Original Code is the Mozilla GNOME integration code.
 *
 * The Initial Developer of the Original Code is
 * IBM Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2004
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Brian Ryner <bryner@brianryner.com>
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

#include "nsToolkitCompsCID.h"
#include "nsIGenericFactory.h"

#ifdef MOZ_ENABLE_GCONF
#include "nsGConfService.h"
NS_GENERIC_FACTORY_CONSTRUCTOR_INIT(nsGConfService, Init)
#endif
#ifdef MOZ_ENABLE_GNOMEVFS
#include "nsGnomeVFSService.h"
NS_GENERIC_FACTORY_CONSTRUCTOR_INIT(nsGnomeVFSService, Init)
#endif
#ifdef MOZ_ENABLE_GIO
#include "nsGIOService.h"
NS_GENERIC_FACTORY_CONSTRUCTOR_INIT(nsGIOService, Init)
#endif
#ifdef MOZ_ENABLE_LIBNOTIFY
#include "nsAlertsService.h"
NS_GENERIC_FACTORY_CONSTRUCTOR_INIT(nsAlertsService, Init)
#endif

static const nsModuleComponentInfo components[] = {
#ifdef MOZ_ENABLE_GCONF
  { "GConf Service",
    NS_GCONFSERVICE_CID,
    NS_GCONFSERVICE_CONTRACTID,
    nsGConfServiceConstructor },
#endif
#ifdef MOZ_ENABLE_GNOMEVFS
  { "GnomeVFS Service",
    NS_GNOMEVFSSERVICE_CID,
    NS_GNOMEVFSSERVICE_CONTRACTID,
    nsGnomeVFSServiceConstructor },
#endif
#ifdef MOZ_ENABLE_GIO
  { "GIO Service",
    NS_GIOSERVICE_CID,
    NS_GIOSERVICE_CONTRACTID,
    nsGIOServiceConstructor },
#endif
#ifdef MOZ_ENABLE_LIBNOTIFY
  { "Gnome Alerts Service",
    NS_SYSTEMALERTSSERVICE_CID,
    NS_SYSTEMALERTSERVICE_CONTRACTID,
    nsAlertsServiceConstructor },
#endif
};

NS_IMPL_NSGETMODULE(mozgnome, components)
