/* vim: se cin sw=2 ts=2 et : */
/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 *
 * ***** BEGIN LICENSE BLOCK *****
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
 * Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Rob Arnold <tellrob@gmail.com>
 *   Jim Mathies <jmathies@mozilla.com>
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

#if MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_WIN7

#include "WinTaskbar.h"
#include "TaskbarPreview.h"
#include <nsITaskbarPreviewController.h>

#include <nsError.h>
#include <nsCOMPtr.h>
#include <nsIWidget.h>
#include <nsIBaseWindow.h>
#include <nsIObserverService.h>
#include <nsServiceManagerUtils.h>
#include <nsAutoPtr.h>
#include "nsIJumpListBuilder.h"
#include "nsUXThemeData.h"
#include "nsWindow.h"
#include "TaskbarTabPreview.h"
#include "TaskbarWindowPreview.h"
#include "JumpListBuilder.h"
#include "nsWidgetsCID.h"
#include <io.h>

const PRUnichar kShellLibraryName[] =  L"shell32.dll";

static NS_DEFINE_CID(kJumpListBuilderCID, NS_WIN_JUMPLISTBUILDER_CID);

namespace {
HWND
GetHWNDFromDocShell(nsIDocShell *aShell) {
  nsCOMPtr<nsIBaseWindow> baseWindow(do_QueryInterface(reinterpret_cast<nsISupports*>(aShell)));

  if (!baseWindow)
    return NULL;

  nsCOMPtr<nsIWidget> widget;
  baseWindow->GetMainWidget(getter_AddRefs(widget));

  return widget ? (HWND)widget->GetNativeData(NS_NATIVE_WINDOW) : NULL;
}

// Default controller for TaskbarWindowPreviews
class DefaultController : public nsITaskbarPreviewController
{
  HWND mWnd;
public:
  DefaultController(HWND hWnd) 
    : mWnd(hWnd)
  {
  }

  NS_DECL_ISUPPORTS
  NS_DECL_NSITASKBARPREVIEWCONTROLLER
};

NS_IMETHODIMP
DefaultController::GetWidth(PRUint32 *aWidth)
{
  RECT r;
  ::GetClientRect(mWnd, &r);
  *aWidth = r.right;
  return NS_OK;
}

NS_IMETHODIMP
DefaultController::GetHeight(PRUint32 *aHeight)
{
  RECT r;
  ::GetClientRect(mWnd, &r);
  *aHeight = r.bottom;
  return NS_OK;
}

NS_IMETHODIMP
DefaultController::GetThumbnailAspectRatio(float *aThumbnailAspectRatio) {
  PRUint32 width, height;
  GetWidth(&width);
  GetHeight(&height);
  if (!height)
    height = 1;

  *aThumbnailAspectRatio = width/float(height);
  return NS_OK;
}

NS_IMETHODIMP
DefaultController::DrawPreview(nsIDOMCanvasRenderingContext2D *ctx, PRBool *rDrawFrame) {
  *rDrawFrame = PR_TRUE;
  return NS_OK;
}

NS_IMETHODIMP
DefaultController::DrawThumbnail(nsIDOMCanvasRenderingContext2D *ctx, PRUint32 width, PRUint32 height, PRBool *rDrawFrame) {
  *rDrawFrame = PR_FALSE;
  return NS_OK;
}

NS_IMETHODIMP
DefaultController::OnClose(void) {
  NS_NOTREACHED("OnClose should not be called for TaskbarWindowPreviews");
  return NS_OK;
}

NS_IMETHODIMP
DefaultController::OnActivate(PRBool *rAcceptActivation) {
  *rAcceptActivation = PR_TRUE;
  NS_NOTREACHED("OnActivate should not be called for TaskbarWindowPreviews");
  return NS_OK;
}

NS_IMETHODIMP
DefaultController::OnClick(nsITaskbarPreviewButton *button) {
  return NS_OK;
}

NS_IMPL_ISUPPORTS1(DefaultController, nsITaskbarPreviewController);
}

namespace mozilla {
namespace widget {

// Unique identifier for a particular application. Windows uses this to group
// windows from the same process and to tie jump lists to processes. The ID
// should be unique per application version. If developers plan on using jump
// list links (nsIJumpListLink) this ID should also be associated with the
// prog id of the protocol handler of the links. To override the default below,
// define MOZ_TASKBAR_ID.
#ifndef MOZ_TASKBAR_ID
#define MOZ_COMPANY Mozilla
#define MOZTBID1(x) L#x
#define MOZTBID2(a,b,c) MOZTBID1(a##.##b##.##c)
#define MOZTBID(a,b,c) MOZTBID2(a,b,c)
#define MOZ_TASKBAR_ID MOZTBID(MOZ_COMPANY, MOZ_BUILD_APP, MOZILLA_VERSION_U)
#endif
const wchar_t *gMozillaJumpListIDGeneric = MOZ_TASKBAR_ID;

NS_IMPL_ISUPPORTS1(WinTaskbar, nsIWinTaskbar)

WinTaskbar::WinTaskbar() 
  : mTaskbar(nsnull) {
  // Perf regression fix: slow registry lookups for non-existent com interfaces
  // on freshly rebooted, memory starved machines (like talos). (bug 520837)
  if (nsWindow::GetWindowsVersion() < WIN7_VERSION)
    return;

  ::CoInitialize(NULL);
  HRESULT hr = ::CoCreateInstance(CLSID_TaskbarList,
                                  NULL,
                                  CLSCTX_INPROC_SERVER,
                                  IID_ITaskbarList4,
                                  (void**)&mTaskbar);
  if (FAILED(hr))
    return;

  hr = mTaskbar->HrInit();
  if (FAILED(hr)) {
    NS_WARNING("Unable to initialize taskbar");
    NS_RELEASE(mTaskbar);
  }
}

WinTaskbar::~WinTaskbar() {
  NS_IF_RELEASE(mTaskbar);
  ::CoUninitialize();
}

// (static) Called from AppShell
PRBool WinTaskbar::SetAppUserModelID()
{
  if (nsWindow::GetWindowsVersion() < WIN7_VERSION)
    return PR_FALSE;

  SetCurrentProcessExplicitAppUserModelIDPtr funcAppUserModelID = nsnull;
  PRBool retVal = PR_FALSE;

  // #define MOZ_TASKBAR_ID L""
  if (*gMozillaJumpListIDGeneric == nsnull)
    return PR_FALSE;

  HMODULE hDLL = ::LoadLibraryW(kShellLibraryName);

  funcAppUserModelID = (SetCurrentProcessExplicitAppUserModelIDPtr)
                        GetProcAddress(hDLL, "SetCurrentProcessExplicitAppUserModelID");

  if (funcAppUserModelID && SUCCEEDED(funcAppUserModelID(gMozillaJumpListIDGeneric)))
    retVal = PR_TRUE;

  if (hDLL)
    ::FreeLibrary(hDLL);

  return retVal;
}

NS_IMETHODIMP
WinTaskbar::GetAvailable(PRBool *aAvailable) {
  *aAvailable = mTaskbar != nsnull;
  return NS_OK;
}

NS_IMETHODIMP
WinTaskbar::CreateTaskbarTabPreview(nsIDocShell *shell, nsITaskbarPreviewController *controller, nsITaskbarTabPreview **_retval) {
  if (!mTaskbar)
    return NS_ERROR_NOT_INITIALIZED;

  NS_ENSURE_ARG_POINTER(shell);
  NS_ENSURE_ARG_POINTER(controller);

  HWND toplevelHWND = ::GetAncestor(GetHWNDFromDocShell(shell), GA_ROOT);

  nsRefPtr<TaskbarTabPreview> preview(new TaskbarTabPreview(mTaskbar, controller, toplevelHWND, shell));
  if (!preview)
    return NS_ERROR_OUT_OF_MEMORY;

  preview.forget(_retval);

  return NS_OK;
}

NS_IMETHODIMP
WinTaskbar::GetTaskbarWindowPreview(nsIDocShell *shell, nsITaskbarWindowPreview **_retval) {
  if (!mTaskbar)
    return NS_ERROR_NOT_INITIALIZED;

  NS_ENSURE_ARG_POINTER(shell);

  HWND toplevelHWND = ::GetAncestor(GetHWNDFromDocShell(shell), GA_ROOT);

  nsWindow *window = nsWindow::GetNSWindowPtr(toplevelHWND);

  if (!window)
    return NS_ERROR_FAILURE;

  nsCOMPtr<nsITaskbarWindowPreview> preview = window->GetTaskbarPreview();
  if (!preview) {
    nsRefPtr<DefaultController> defaultController = new DefaultController(toplevelHWND);
    preview = new TaskbarWindowPreview(mTaskbar, defaultController, toplevelHWND, shell);
    if (!preview)
      return NS_ERROR_OUT_OF_MEMORY;
    window->SetTaskbarPreview(preview);
  }

  preview.forget(_retval);

  return NS_OK;
}

NS_IMETHODIMP
WinTaskbar::GetTaskbarProgress(nsIDocShell *shell, nsITaskbarProgress **_retval) {
  nsCOMPtr<nsITaskbarWindowPreview> preview;
  nsresult rv = GetTaskbarWindowPreview(shell, getter_AddRefs(preview));
  NS_ENSURE_SUCCESS(rv, rv);

  return CallQueryInterface(preview, _retval);
}

/* nsIJumpListBuilder createJumpListBuilder(); */
NS_IMETHODIMP WinTaskbar::CreateJumpListBuilder(nsIJumpListBuilder * *aJumpListBuilder)
{
  nsresult rv;

  if (JumpListBuilder::sBuildingList)
    return NS_ERROR_ALREADY_INITIALIZED;

  nsCOMPtr<nsIJumpListBuilder> builder = 
    do_CreateInstance(kJumpListBuilderCID, &rv);
  if (NS_FAILED(rv))
    return NS_ERROR_UNEXPECTED;

  NS_IF_ADDREF(*aJumpListBuilder = builder);

  return NS_OK;
}

} // namespace widget
} // namespace mozilla

#endif // MOZ_WINSDK_TARGETVER >= MOZ_NTDDI_WIN7
