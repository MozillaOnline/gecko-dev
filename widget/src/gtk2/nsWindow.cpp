/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim:expandtab:shiftwidth=4:tabstop=4:
 */
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
 * The Initial Developer of the Original Code is Christopher Blizzard
 * <blizzard@mozilla.org>.  Portions created by the Initial Developer
 * are Copyright (C) 2001 the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Mats Palmgren <mats.palmgren@bredband.net>
 *   Masayuki Nakano <masayuki@d-toybox.com>
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

#ifdef MOZ_PLATFORM_MAEMO
#define MAEMO_CHANGES
#endif

#include "prlink.h"

#include "nsWindow.h"
#include "nsGTKToolkit.h"
#include "nsIDeviceContext.h"
#include "nsIRenderingContext.h"
#include "nsIRegion.h"
#include "nsIRollupListener.h"
#include "nsIMenuRollup.h"
#include "nsIDOMNode.h"

#include "nsWidgetsCID.h"
#include "nsDragService.h"
#include "nsIDragSessionGTK.h"

#include "nsGtkKeyUtils.h"
#include "nsGtkCursors.h"

#include <gtk/gtk.h>
#ifdef MOZ_X11
#include <gdk/gdkx.h>
#include <X11/XF86keysym.h>
#include "gtk2xtbin.h"
#endif /* MOZ_X11 */
#include <gdk/gdkkeysyms.h>
#include <gtk/gtkprivate.h>

#include "nsWidgetAtoms.h"

#ifdef MOZ_ENABLE_STARTUP_NOTIFICATION
#define SN_API_NOT_YET_FROZEN
#include <startup-notification-1.0/libsn/sn.h>
#endif

#include "nsIPrefService.h"
#include "nsIPrefBranch.h"
#include "nsIServiceManager.h"
#include "nsIStringBundle.h"
#include "nsGfxCIID.h"
#include "nsIObserverService.h"

#include "nsIdleService.h"

#ifdef ACCESSIBILITY
#include "nsIAccessibilityService.h"
#include "nsIAccessibleRole.h"
#include "nsIAccessibleEvent.h"
#include "prenv.h"
#include "stdlib.h"
static PRBool sAccessibilityChecked = PR_FALSE;
/* static */
PRBool nsWindow::sAccessibilityEnabled = PR_FALSE;
static const char sSysPrefService [] = "@mozilla.org/system-preference-service;1";
static const char sAccEnv [] = "GNOME_ACCESSIBILITY";
static const char sAccessibilityKey [] = "config.use_system_prefs.accessibility";
#endif

/* For SetIcon */
#include "nsAppDirectoryServiceDefs.h"
#include "nsXPIDLString.h"
#include "nsIFile.h"
#include "nsILocalFile.h"

/* SetCursor(imgIContainer*) */
#include <gdk/gdk.h>
#include <wchar.h>
#include "imgIContainer.h"
#include "nsGfxCIID.h"
#include "nsImageToPixbuf.h"
#include "nsIInterfaceRequestorUtils.h"
#include "nsAutoPtr.h"

extern "C" {
#include "pixman.h"
}
#include "gfxPlatformGtk.h"
#include "gfxContext.h"
#include "gfxImageSurface.h"

#ifdef MOZ_X11
#include "gfxXlibSurface.h"
#endif

#ifdef MOZ_DFB
extern "C" {
#ifdef MOZ_DIRECT_DEBUG
#define DIRECT_ENABLE_DEBUG
#endif

#include <direct/debug.h>

D_DEBUG_DOMAIN( ns_Window, "nsWindow", "nsWindow" );
}
#include "gfxDirectFBSurface.h"
#define GDK_WINDOW_XWINDOW(_win) _win
#endif

// Don't put more than this many rects in the dirty region, just fluff
// out to the bounding-box if there are more
#define MAX_RECTS_IN_REGION 100

/* For PrepareNativeWidget */
static NS_DEFINE_IID(kDeviceContextCID, NS_DEVICE_CONTEXT_CID);

/* utility functions */
static PRBool     check_for_rollup(GdkWindow *aWindow,
                                   gdouble aMouseX, gdouble aMouseY,
                                   PRBool aIsWheel);
static PRBool     is_mouse_in_window(GdkWindow* aWindow,
                                     gdouble aMouseX, gdouble aMouseY);
static nsWindow  *get_window_for_gtk_widget(GtkWidget *widget);
static nsWindow  *get_window_for_gdk_window(GdkWindow *window);
static nsWindow  *get_owning_window_for_gdk_window(GdkWindow *window);
static GtkWidget *get_gtk_widget_for_gdk_window(GdkWindow *window);
static GdkCursor *get_gtk_cursor(nsCursor aCursor);

static GdkWindow *get_inner_gdk_window (GdkWindow *aWindow,
                                        gint x, gint y,
                                        gint *retx, gint *rety);

static inline PRBool is_context_menu_key(const nsKeyEvent& inKeyEvent);
static void   key_event_to_context_menu_event(nsMouseEvent &aEvent,
                                              GdkEventKey *aGdkEvent);

static int    is_parent_ungrab_enter(GdkEventCrossing *aEvent);
static int    is_parent_grab_leave(GdkEventCrossing *aEvent);

/* callbacks from widgets */
static gboolean expose_event_cb           (GtkWidget *widget,
                                           GdkEventExpose *event);
static gboolean configure_event_cb        (GtkWidget *widget,
                                           GdkEventConfigure *event);
static void     container_unrealize_cb    (GtkWidget *widget);
static void     size_allocate_cb          (GtkWidget *widget,
                                           GtkAllocation *allocation);
static gboolean delete_event_cb           (GtkWidget *widget,
                                           GdkEventAny *event);
static gboolean enter_notify_event_cb     (GtkWidget *widget,
                                           GdkEventCrossing *event);
static gboolean leave_notify_event_cb     (GtkWidget *widget,
                                           GdkEventCrossing *event);
static gboolean motion_notify_event_cb    (GtkWidget *widget,
                                           GdkEventMotion *event);
static gboolean button_press_event_cb     (GtkWidget *widget,
                                           GdkEventButton *event);
static gboolean button_release_event_cb   (GtkWidget *widget,
                                           GdkEventButton *event);
static gboolean focus_in_event_cb         (GtkWidget *widget,
                                           GdkEventFocus *event);
static gboolean focus_out_event_cb        (GtkWidget *widget,
                                           GdkEventFocus *event);
static gboolean key_press_event_cb        (GtkWidget *widget,
                                           GdkEventKey *event);
static gboolean key_release_event_cb      (GtkWidget *widget,
                                           GdkEventKey *event);
static gboolean scroll_event_cb           (GtkWidget *widget,
                                           GdkEventScroll *event);
static gboolean visibility_notify_event_cb(GtkWidget *widget,
                                           GdkEventVisibility *event);
static void     hierarchy_changed_cb      (GtkWidget *widget,
                                           GtkWidget *previous_toplevel);
static gboolean window_state_event_cb     (GtkWidget *widget,
                                           GdkEventWindowState *event);
static void     theme_changed_cb          (GtkSettings *settings,
                                           GParamSpec *pspec,
                                           nsWindow *data);
static nsWindow* GetFirstNSWindowForGDKWindow (GdkWindow *aGdkWindow);

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
#ifdef MOZ_X11
static GdkFilterReturn plugin_window_filter_func (GdkXEvent *gdk_xevent,
                                                  GdkEvent *event,
                                                  gpointer data);
static GdkFilterReturn plugin_client_message_filter (GdkXEvent *xevent,
                                                     GdkEvent *event,
                                                     gpointer data);
#endif /* MOZ_X11 */
#ifdef __cplusplus
}
#endif /* __cplusplus */

static gboolean drag_motion_event_cb      (GtkWidget *aWidget,
                                           GdkDragContext *aDragContext,
                                           gint aX,
                                           gint aY,
                                           guint aTime,
                                           gpointer aData);
static void     drag_leave_event_cb       (GtkWidget *aWidget,
                                           GdkDragContext *aDragContext,
                                           guint aTime,
                                           gpointer aData);
static gboolean drag_drop_event_cb        (GtkWidget *aWidget,
                                           GdkDragContext *aDragContext,
                                           gint aX,
                                           gint aY,
                                           guint aTime,
                                           gpointer *aData);
static void    drag_data_received_event_cb(GtkWidget *aWidget,
                                           GdkDragContext *aDragContext,
                                           gint aX,
                                           gint aY,
                                           GtkSelectionData  *aSelectionData,
                                           guint aInfo,
                                           guint32 aTime,
                                           gpointer aData);

static GdkModifierType gdk_keyboard_get_modifiers();
#ifdef MOZ_X11
static PRBool gdk_keyboard_get_modmap_masks(Display*  aDisplay,
                                            PRUint32* aCapsLockMask,
                                            PRUint32* aNumLockMask,
                                            PRUint32* aScrollLockMask);
#endif /* MOZ_X11 */

/* initialization static functions */
static nsresult    initialize_prefs        (void);

PRUint32        gLastInputEventTime = 0;

static void UpdateLastInputEventTime() {
  gLastInputEventTime = PR_IntervalToMicroseconds(PR_IntervalNow());
  nsCOMPtr<nsIIdleService> idleService = do_GetService("@mozilla.org/widget/idleservice;1");
  nsIdleService* is = static_cast<nsIdleService*>(idleService.get());
  if (is)
    is->IdleTimeWasModified();
}

// this is the last window that had a drag event happen on it.
nsWindow *nsWindow::mLastDragMotionWindow = NULL;
PRBool nsWindow::sIsDraggingOutOf = PR_FALSE;

// This is the time of the last button press event.  The drag service
// uses it as the time to start drags.
guint32   nsWindow::mLastButtonPressTime = 0;
// Time of the last button release event. We use it to detect when the
// drag ended before we could properly setup drag and drop.
guint32   nsWindow::mLastButtonReleaseTime = 0;

static NS_DEFINE_IID(kCDragServiceCID,  NS_DRAGSERVICE_CID);

// the current focus window
static nsWindow         *gFocusWindow          = NULL;
static PRBool            gGlobalsInitialized   = PR_FALSE;
static PRBool            gRaiseWindows         = PR_TRUE;
static nsWindow         *gPluginFocusWindow    = NULL;

static nsIRollupListener*          gRollupListener;
static nsIMenuRollup*              gMenuRollup;
static nsWeakPtr                   gRollupWindow;
static PRBool                      gConsumeRollupEvent;


#define NS_WINDOW_TITLE_MAX_LENGTH 4095

#ifdef USE_XIM

static nsWindow    *gIMEFocusWindow = NULL;
static GdkEventKey *gKeyEvent = NULL;
static PRBool       gKeyEventCommitted = PR_FALSE;
static PRBool       gKeyEventChanged = PR_FALSE;
static PRBool       gIMESuppressCommit = PR_FALSE;
#ifdef MOZ_PLATFORM_MAEMO
static PRBool       gIMEVirtualKeyboardOpened = PR_FALSE;
#endif

static void IM_commit_cb              (GtkIMContext *aContext,
                                       const gchar *aString,
                                       nsWindow *aWindow);
static void IM_commit_cb_internal     (const gchar *aString,
                                       nsWindow *aWindow);
static void IM_preedit_changed_cb     (GtkIMContext *aContext,
                                       nsWindow *aWindow);
static void IM_set_text_range         (const PRInt32 aLen,
                                       const gchar *aPreeditString,
                                       const gint aCursorPos,
                                       const PangoAttrList *aFeedback,
                                       PRUint32 *aTextRangeListLengthResult,
                                       nsTextRangeArray *aTextRangeListResult);

static GtkIMContext *IM_get_input_context(nsWindow *window);

// If after selecting profile window, the startup fail, please refer to
// http://bugzilla.gnome.org/show_bug.cgi?id=88940
#endif

// needed for imgIContainer cursors
// GdkDisplay* was added in 2.2
typedef struct _GdkDisplay GdkDisplay;
typedef GdkDisplay* (*_gdk_display_get_default_fn)(void);

typedef GdkCursor*  (*_gdk_cursor_new_from_pixbuf_fn)(GdkDisplay *display,
                                                      GdkPixbuf *pixbuf,
                                                      gint x,
                                                      gint y);
static _gdk_display_get_default_fn    _gdk_display_get_default;
static _gdk_cursor_new_from_pixbuf_fn _gdk_cursor_new_from_pixbuf;
static PRBool sPixbufCursorChecked;

// needed for GetAttention calls
// gdk_window_set_urgency_hint was added in 2.8
typedef void (*_gdk_window_set_urgency_hint_fn)(GdkWindow *window,
                                                gboolean urgency);

#define kWindowPositionSlop 20

// cursor cache
static GdkCursor *gCursorCache[eCursorCount];

// Global update pixmap
static PRBool gUseBufferPixmap = PR_FALSE;
static GdkPixmap *gBufferPixmap = nsnull;
static gfxIntSize gBufferPixmapSize(0,0);
static gfxIntSize gBufferPixmapMaxSize(0,0);
static int gBufferPixmapUsageCount = 0;

// imported in nsWidgetFactory.cpp
PRBool gDisableNativeTheme = PR_FALSE;

// If this is 1, then a 24bpp buffer surface is always
// created for exposes, even if the display has a different depth
static PRBool gForce24bpp = PR_FALSE;

static GtkWidget *gInvisibleContainer = NULL;

// Some gobject functions expect functions for gpointer arguments.
// gpointer is void* but C++ doesn't like casting functions to void*.
template<class T> gpointer
FuncToGpointer(T aFunction)
{
    return reinterpret_cast<gpointer>
        (reinterpret_cast<uintptr_t>
         // This cast just provides a warning if T is not a function.
         (reinterpret_cast<void (*)()>(aFunction)));
}

// nsAutoRef<pixman_region32> uses nsSimpleRef<> to know how to automatically
// destroy regions.
template <>
class nsSimpleRef<pixman_region32> : public pixman_region32 {
protected:
    typedef pixman_region32 RawRef;

    nsSimpleRef() { data = nsnull; }
    nsSimpleRef(const RawRef &aRawRef) : pixman_region32(aRawRef) { }

    static void Release(pixman_region32& region) {
        pixman_region32_fini(&region);
    }
    // Whether this needs to be released:
    PRBool HaveResource() const { return data != nsnull; }

    pixman_region32& get() { return *this; }
};

nsWindow::nsWindow()
{
    mIsTopLevel       = PR_FALSE;
    mIsDestroyed      = PR_FALSE;
    mNeedsResize      = PR_FALSE;
    mNeedsMove        = PR_FALSE;
    mListenForResizes = PR_FALSE;
    mIsShown          = PR_FALSE;
    mNeedsShow        = PR_FALSE;
    mEnabled          = PR_TRUE;
    mCreated          = PR_FALSE;
    mPlaced           = PR_FALSE;

    mContainer           = nsnull;
    mGdkWindow           = nsnull;
    mShell               = nsnull;
    mWindowGroup         = nsnull;
    mContainerGotFocus   = PR_FALSE;
    mContainerLostFocus  = PR_FALSE;
    mContainerBlockFocus = PR_FALSE;
    mHasMappedToplevel   = PR_FALSE;
    mIsFullyObscured     = PR_FALSE;
    mRetryPointerGrab    = PR_FALSE;
    mRetryKeyboardGrab   = PR_FALSE;
    mTransientParent     = nsnull;
    mWindowType          = eWindowType_child;
    mSizeState           = nsSizeMode_Normal;
    mLastSizeMode        = nsSizeMode_Normal;

#ifdef MOZ_X11
    mOldFocusWindow      = 0;
#endif /* MOZ_X11 */
    mPluginType          = PluginType_NONE;

    if (!gGlobalsInitialized) {
        gGlobalsInitialized = PR_TRUE;

        // It's OK if either of these fail, but it may not be one day.
        initialize_prefs();
    }

    memset(mKeyDownFlags, 0, sizeof(mKeyDownFlags));

    if (mLastDragMotionWindow == this)
        mLastDragMotionWindow = NULL;
    mDragMotionWidget = 0;
    mDragMotionContext = 0;
    mDragMotionX = 0;
    mDragMotionY = 0;
    mDragMotionTime = 0;
    mDragMotionTimerID = 0;
    mLastMotionPressure = 0;

#ifdef USE_XIM
    mIMEData = nsnull;
#endif

#ifdef ACCESSIBILITY
    mRootAccessible  = nsnull;
#endif

    mIsTransparent = PR_FALSE;
    mTransparencyBitmap = nsnull;

    mTransparencyBitmapWidth  = 0;
    mTransparencyBitmapHeight = 0;

#ifdef MOZ_DFB
    mDFBCursorX     = 0;
    mDFBCursorY     = 0;

    mDFBCursorCount = 0;

    mDFB            = NULL;
    mDFBLayer       = NULL;
#endif


    if (gUseBufferPixmap) {
        if (gBufferPixmapMaxSize.width == 0) {
            gBufferPixmapMaxSize.width = gdk_screen_width();
            gBufferPixmapMaxSize.height = gdk_screen_height();
        }

        gBufferPixmapUsageCount++;
    }

    // Set gLastInputEventTime to some valid number
    gLastInputEventTime = PR_IntervalToMicroseconds(PR_IntervalNow());
}

nsWindow::~nsWindow()
{
    LOG(("nsWindow::~nsWindow() [%p]\n", (void *)this));
    if (mLastDragMotionWindow == this) {
        mLastDragMotionWindow = NULL;
    }

    delete[] mTransparencyBitmap;
    mTransparencyBitmap = nsnull;

#ifdef MOZ_DFB
    if (mDFBLayer)
         mDFBLayer->Release( mDFBLayer );

    if (mDFB)
         mDFB->Release( mDFB );
#endif

    Destroy();
}

/* static */ void
nsWindow::ReleaseGlobals()
{
  for (PRUint32 i = 0; i < NS_ARRAY_LENGTH(gCursorCache); ++i) {
    if (gCursorCache[i]) {
      gdk_cursor_unref(gCursorCache[i]);
      gCursorCache[i] = nsnull;
    }
  }
}

NS_IMPL_ISUPPORTS_INHERITED1(nsWindow, nsBaseWidget,
                             nsISupportsWeakReference)

void
nsWindow::CommonCreate(nsIWidget *aParent, PRBool aListenForResizes)
{
    mParent = aParent;
    mListenForResizes = aListenForResizes;
    mCreated = PR_TRUE;
}

void
nsWindow::InitKeyEvent(nsKeyEvent &aEvent, GdkEventKey *aGdkEvent)
{
    aEvent.keyCode   = GdkKeyCodeToDOMKeyCode(aGdkEvent->keyval);
    aEvent.isShift   = (aGdkEvent->state & GDK_SHIFT_MASK)
        ? PR_TRUE : PR_FALSE;
    aEvent.isControl = (aGdkEvent->state & GDK_CONTROL_MASK)
        ? PR_TRUE : PR_FALSE;
    aEvent.isAlt     = (aGdkEvent->state & GDK_MOD1_MASK)
        ? PR_TRUE : PR_FALSE;
    aEvent.isMeta    = (aGdkEvent->state & GDK_MOD4_MASK)
        ? PR_TRUE : PR_FALSE;
    // The transformations above and in gdk for the keyval are not invertible
    // so link to the GdkEvent (which will vanish soon after return from the
    // event callback) to give plugins access to hardware_keycode and state.
    // (An XEvent would be nice but the GdkEvent is good enough.)
    aEvent.pluginEvent = (void *)aGdkEvent;

    aEvent.time      = aGdkEvent->time;
}

void
nsWindow::DispatchResizeEvent(nsIntRect &aRect, nsEventStatus &aStatus)
{
    nsSizeEvent event(PR_TRUE, NS_SIZE, this);

    event.windowSize = &aRect;
    event.refPoint.x = aRect.x;
    event.refPoint.y = aRect.y;
    event.mWinWidth = aRect.width;
    event.mWinHeight = aRect.height;

    nsEventStatus status;
    DispatchEvent(&event, status);
}

void
nsWindow::DispatchActivateEvent(void)
{
    NS_ASSERTION(mContainer || mIsDestroyed,
                 "DispatchActivateEvent only intended for container windows");

#ifdef ACCESSIBILITY
    DispatchActivateEventAccessible();
#endif //ACCESSIBILITY
    nsGUIEvent event(PR_TRUE, NS_ACTIVATE, this);
    nsEventStatus status;
    DispatchEvent(&event, status);
}

void
nsWindow::DispatchDeactivateEvent(void)
{
    nsGUIEvent event(PR_TRUE, NS_DEACTIVATE, this);
    nsEventStatus status;
    DispatchEvent(&event, status);

#ifdef ACCESSIBILITY
    DispatchDeactivateEventAccessible();
#endif //ACCESSIBILITY
}



nsresult
nsWindow::DispatchEvent(nsGUIEvent *aEvent, nsEventStatus &aStatus)
{
#ifdef DEBUG
    debug_DumpEvent(stdout, aEvent->widget, aEvent,
                    nsCAutoString("something"), 0);
#endif

    aStatus = nsEventStatus_eIgnore;

    // send it to the standard callback
    if (mEventCallback)
        aStatus = (* mEventCallback)(aEvent);

    return NS_OK;
}

void
nsWindow::OnDestroy(void)
{
    if (mOnDestroyCalled)
        return;

    mOnDestroyCalled = PR_TRUE;

    // release references to children, device context, toolkit + app shell
    nsBaseWidget::OnDestroy();

    // let go of our parent
    mParent = nsnull;

    nsCOMPtr<nsIWidget> kungFuDeathGrip = this;

    nsGUIEvent event(PR_TRUE, NS_DESTROY, this);
    nsEventStatus status;
    DispatchEvent(&event, status);
}

PRBool
nsWindow::AreBoundsSane(void)
{
    if (mBounds.width > 0 && mBounds.height > 0)
        return PR_TRUE;

    return PR_FALSE;
}

static GtkWidget*
EnsureInvisibleContainer()
{
    if (!gInvisibleContainer) {
        // GtkWidgets need to be anchored to a GtkWindow to be realized (to
        // have a window).  Using GTK_WINDOW_POPUP rather than
        // GTK_WINDOW_TOPLEVEL in the hope that POPUP results in less
        // initialization and window manager interaction.
        GtkWidget* window = gtk_window_new(GTK_WINDOW_POPUP);
        gInvisibleContainer = moz_container_new();
        gtk_container_add(GTK_CONTAINER(window), gInvisibleContainer);
        gtk_widget_realize(gInvisibleContainer);

    }
    return gInvisibleContainer;
}

static void
CheckDestroyInvisibleContainer()
{
    NS_PRECONDITION(gInvisibleContainer, "oh, no");

    if (!gdk_window_peek_children(gInvisibleContainer->window)) {
        // No children, so not in use.
        // Make sure to destroy the GtkWindow also.
        gtk_widget_destroy(gInvisibleContainer->parent);
        gInvisibleContainer = NULL;
    }
}

// Change the containing GtkWidget on a sub-hierarchy of GdkWindows belonging
// to aOldWidget and rooted at aWindow, and reparent any child GtkWidgets of
// the GdkWindow hierarchy to aNewWidget.
static void
SetWidgetForHierarchy(GdkWindow *aWindow,
                      GtkWidget *aOldWidget,
                      GtkWidget *aNewWidget)
{
    gpointer data;
    gdk_window_get_user_data(aWindow, &data);

    if (data != aOldWidget) {
        if (!GTK_IS_WIDGET(data))
            return;

        GtkWidget* widget = static_cast<GtkWidget*>(data);
        if (widget->parent != aOldWidget)
            return;

        // This window belongs to a child widget, which will no longer be a
        // child of aOldWidget.
        gtk_widget_reparent(widget, aNewWidget);

        return;
    }

    GList *children = gdk_window_get_children(aWindow);
    for(GList *list = children; list; list = list->next) {
        SetWidgetForHierarchy(GDK_WINDOW(list->data), aOldWidget, aNewWidget);
    }
    g_list_free(children);

    gdk_window_set_user_data(aWindow, aNewWidget);
}

// Walk the list of child windows and call destroy on them.
void
nsWindow::DestroyChildWindows()
{
    if (!mGdkWindow)
        return;

    while (GList *children = gdk_window_peek_children(mGdkWindow)) {
        GdkWindow *child = GDK_WINDOW(children->data);
        nsWindow *kid = get_window_for_gdk_window(child);
        if (kid) {
            kid->Destroy();
        } else {
            // This child is not an nsWindow.
            // Destroy the child GtkWidget.
            gpointer data;
            gdk_window_get_user_data(child, &data);
            if (GTK_IS_WIDGET(data)) {
                gtk_widget_destroy(static_cast<GtkWidget*>(data));
            }
        }
    }
}

NS_IMETHODIMP
nsWindow::Destroy(void)
{
    if (mIsDestroyed || !mCreated)
        return NS_OK;

    LOG(("nsWindow::Destroy [%p]\n", (void *)this));
    mIsDestroyed = PR_TRUE;
    mCreated = PR_FALSE;

    if (gUseBufferPixmap &&
        gBufferPixmapUsageCount &&
        --gBufferPixmapUsageCount == 0)
    {
        if (gBufferPixmap)
            g_object_unref(G_OBJECT(gBufferPixmap));

        gBufferPixmap = nsnull;
        gBufferPixmapSize.width = 0;
        gBufferPixmapSize.height = 0;
    }

    g_signal_handlers_disconnect_by_func(gtk_settings_get_default(),
                                         FuncToGpointer(theme_changed_cb),
                                         this);

    // ungrab if required
    nsCOMPtr<nsIWidget> rollupWidget = do_QueryReferent(gRollupWindow);
    if (static_cast<nsIWidget *>(this) == rollupWidget.get()) {
        if (gRollupListener)
            gRollupListener->Rollup(nsnull, nsnull);
        NS_IF_RELEASE(gMenuRollup);
        gRollupWindow = nsnull;
        gRollupListener = nsnull;
    }

    NativeShow(PR_FALSE);

#ifdef USE_XIM
    IMEDestroyContext();
#endif

    // make sure that we remove ourself as the focus window
    if (gFocusWindow == this) {
        LOGFOCUS(("automatically losing focus...\n"));
        gFocusWindow = nsnull;
    }

#ifdef MOZ_X11
    // make sure that we remove ourself as the plugin focus window
    if (gPluginFocusWindow == this) {
        gPluginFocusWindow->LoseNonXEmbedPluginFocus();
    }
#endif /* MOZ_X11 */

    if (mWindowGroup) {
        g_object_unref(G_OBJECT(mWindowGroup));
        mWindowGroup = nsnull;
    }

    // Destroy thebes surface now. Badness can happen if we destroy
    // the surface after its X Window.
    mThebesSurface = nsnull;

    if (mDragMotionTimerID) {
        g_source_remove(mDragMotionTimerID);
        mDragMotionTimerID = 0;
    }

    if (mDragLeaveTimer) {
        mDragLeaveTimer->Cancel();
        mDragLeaveTimer = nsnull;
    }

    GtkWidget *owningWidget = GetMozContainerWidget();
    if (mShell) {
        gtk_widget_destroy(mShell);
        mShell = nsnull;
        mContainer = nsnull;
        NS_ABORT_IF_FALSE(!mGdkWindow,
                          "mGdkWindow should be NULL when mContainer is destroyed");
    }
    else if (mContainer) {
        gtk_widget_destroy(GTK_WIDGET(mContainer));
        mContainer = nsnull;
        NS_ABORT_IF_FALSE(!mGdkWindow,
                          "mGdkWindow should be NULL when mContainer is destroyed");
    }
    else if (mGdkWindow) {
        // Destroy child windows to ensure that their mThebesSurfaces are
        // released and to remove references from GdkWindows back to their
        // container widget.  (OnContainerUnrealize() does this when the
        // MozContainer widget is destroyed.)
        DestroyChildWindows();

        gdk_window_set_user_data(mGdkWindow, NULL);
        g_object_set_data(G_OBJECT(mGdkWindow), "nsWindow", NULL);
        gdk_window_destroy(mGdkWindow);
        mGdkWindow = nsnull;
    }

    if (gInvisibleContainer && owningWidget == gInvisibleContainer) {
        CheckDestroyInvisibleContainer();
    }

    OnDestroy();

#ifdef ACCESSIBILITY
    if (mRootAccessible) {
        mRootAccessible = nsnull;
    }
#endif

    return NS_OK;
}

nsIWidget *
nsWindow::GetParent(void)
{
    return mParent;
}

NS_IMETHODIMP
nsWindow::SetParent(nsIWidget *aNewParent)
{
    if (mContainer || !mGdkWindow || !mParent) {
        NS_NOTREACHED("nsWindow::SetParent - reparenting a non-child window");
        return NS_ERROR_NOT_IMPLEMENTED;
    }

    // nsBaseWidget::SetZIndex adds child widgets to the parent's list.
    nsCOMPtr<nsIWidget> kungFuDeathGrip = this;
    mParent->RemoveChild(this);

    mParent = aNewParent;

    GtkWidget* oldContainer = GetMozContainerWidget();
    if (!oldContainer) {
        // The GdkWindows have been destroyed so there is nothing else to
        // reparent.
        NS_ABORT_IF_FALSE(GDK_WINDOW_OBJECT(mGdkWindow)->destroyed,
                          "live GdkWindow with no widget");
        return NS_OK;
    }

    NS_ABORT_IF_FALSE(!GDK_WINDOW_OBJECT(mGdkWindow)->destroyed,
                      "destroyed GdkWindow with widget");

    nsWindow* newParent = static_cast<nsWindow*>(aNewParent);
    GdkWindow* newParentWindow = NULL;
    GtkWidget* newContainer = NULL;
    if (aNewParent) {
        newParentWindow = newParent->mGdkWindow;
        if (newParentWindow) {
            newContainer = get_gtk_widget_for_gdk_window(newParentWindow);
        }
    } else {
        // aNewParent is NULL, but reparent to a hidden window to avoid
        // destroying the GdkWindow and its descendants.
        // An invisible container widget is needed to hold descendant
        // GtkWidgets.
        newContainer = EnsureInvisibleContainer();
        newParentWindow = newContainer->window;
    }

    if (!newContainer) {
        // The new parent GdkWindow has been destroyed.
        NS_ABORT_IF_FALSE(!newParentWindow ||
                          GDK_WINDOW_OBJECT(newParentWindow)->destroyed,
                          "live GdkWindow with no widget");
        Destroy();
    } else {
        if (newContainer != oldContainer) {
            NS_ABORT_IF_FALSE(!GDK_WINDOW_OBJECT(newParentWindow)->destroyed,
                              "destroyed GdkWindow with widget");
            SetWidgetForHierarchy(mGdkWindow, oldContainer, newContainer);
        }

        gdk_window_reparent(mGdkWindow, newParentWindow, 0, 0);
    }

    PRBool parentHasMappedToplevel =
        newParent && newParent->mHasMappedToplevel;
    if (mHasMappedToplevel != parentHasMappedToplevel) {
        SetHasMappedToplevel(parentHasMappedToplevel);
    }

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::SetModal(PRBool aModal)
{
    LOG(("nsWindow::SetModal [%p] %d\n", (void *)this, aModal));

    // find the toplevel window and set its modality
    GtkWidget *grabWidget = nsnull;

    GetToplevelWidget(&grabWidget);

    if (!grabWidget)
        return NS_ERROR_FAILURE;

    // block focus tracking via gFocusWindow internally in case the window
    // manager does not block focus to parents of modal windows
    if (mTransientParent) {
        GtkWidget *transientWidget = GTK_WIDGET(mTransientParent);
        nsRefPtr<nsWindow> parent = get_window_for_gtk_widget(transientWidget);
        if (!parent)
            return NS_ERROR_FAILURE;
        parent->mContainerBlockFocus = aModal;
    }

    if (aModal)
        gtk_window_set_modal(GTK_WINDOW(grabWidget), TRUE);
    else
        gtk_window_set_modal(GTK_WINDOW(grabWidget), FALSE);

    return NS_OK;
}

// nsIWidget method, which means IsShown.
NS_IMETHODIMP
nsWindow::IsVisible(PRBool& aState)
{
    aState = mIsShown;
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::ConstrainPosition(PRBool aAllowSlop, PRInt32 *aX, PRInt32 *aY)
{
    if (mIsTopLevel && mShell) {
        PRInt32 screenWidth = gdk_screen_width();
        PRInt32 screenHeight = gdk_screen_height();
        if (aAllowSlop) {
            if (*aX < (kWindowPositionSlop - mBounds.width))
                *aX = kWindowPositionSlop - mBounds.width;
            if (*aX > (screenWidth - kWindowPositionSlop))
                *aX = screenWidth - kWindowPositionSlop;
            if (*aY < (kWindowPositionSlop - mBounds.height))
                *aY = kWindowPositionSlop - mBounds.height;
            if (*aY > (screenHeight - kWindowPositionSlop))
                *aY = screenHeight - kWindowPositionSlop;
        } else {
            if (*aX < 0)
                *aX = 0;
            if (*aX > (screenWidth - mBounds.width))
                *aX = screenWidth - mBounds.width;
            if (*aY < 0)
                *aY = 0;
            if (*aY > (screenHeight - mBounds.height))
                *aY = screenHeight - mBounds.height;
        }
    }
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::Show(PRBool aState)
{
    if (aState == mIsShown)
        return NS_OK;

    mIsShown = aState;

    LOG(("nsWindow::Show [%p] state %d\n", (void *)this, aState));

    if (aState) {
        // Now that this window is shown, mHasMappedToplevel needs to be
        // tracked on viewable descendants.
        SetHasMappedToplevel(mHasMappedToplevel);
    }

    // Ok, someone called show on a window that isn't sized to a sane
    // value.  Mark this window as needing to have Show() called on it
    // and return.
    if ((aState && !AreBoundsSane()) || !mCreated) {
        LOG(("\tbounds are insane or window hasn't been created yet\n"));
        mNeedsShow = PR_TRUE;
        return NS_OK;
    }

    // If someone is hiding this widget, clear any needing show flag.
    if (!aState)
        mNeedsShow = PR_FALSE;

    // If someone is showing this window and it needs a resize then
    // resize the widget.
    if (aState) {
        if (mNeedsMove) {
            NativeResize(mBounds.x, mBounds.y, mBounds.width, mBounds.height,
                         PR_FALSE);
        } else if (mNeedsResize) {
            NativeResize(mBounds.width, mBounds.height, PR_FALSE);
        }
    }

    NativeShow(aState);

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::Resize(PRInt32 aWidth, PRInt32 aHeight, PRBool aRepaint)
{
    mBounds.SizeTo(GetSafeWindowSize(nsIntSize(aWidth, aHeight)));

    if (!mCreated)
        return NS_OK;

    // There are several cases here that we need to handle, based on a
    // matrix of the visibility of the widget, the sanity of this resize
    // and whether or not the widget was previously sane.

    // Has this widget been set to visible?
    if (mIsShown) {
        // Are the bounds sane?
        if (AreBoundsSane()) {
            // Yep?  Resize the window
            //Maybe, the toplevel has moved

            // Note that if the widget needs to be shown because it
            // was previously insane in Resize(x,y,w,h), then we need
            // to set the x and y here too, because the widget wasn't
            // moved back then
            if (mIsTopLevel || mNeedsShow)
                NativeResize(mBounds.x, mBounds.y,
                             mBounds.width, mBounds.height, aRepaint);
            else
                NativeResize(mBounds.width, mBounds.height, aRepaint);

            // Does it need to be shown because it was previously insane?
            if (mNeedsShow)
                NativeShow(PR_TRUE);
        }
        else {
            // If someone has set this so that the needs show flag is false
            // and it needs to be hidden, update the flag and hide the
            // window.  This flag will be cleared the next time someone
            // hides the window or shows it.  It also prevents us from
            // calling NativeShow(PR_FALSE) excessively on the window which
            // causes unneeded X traffic.
            if (!mNeedsShow) {
                mNeedsShow = PR_TRUE;
                NativeShow(PR_FALSE);
            }
        }
    }
    // If the widget hasn't been shown, mark the widget as needing to be
    // resized before it is shown.
    else {
        if (AreBoundsSane() && mListenForResizes) {
            // For widgets that we listen for resizes for (widgets created
            // with native parents) we apparently _always_ have to resize.  I
            // dunno why, but apparently we're lame like that.
            NativeResize(aWidth, aHeight, aRepaint);
        }
        else {
            mNeedsResize = PR_TRUE;
        }
    }

    // synthesize a resize event if this isn't a toplevel
    if (mIsTopLevel || mListenForResizes) {
        nsIntRect rect(mBounds.x, mBounds.y, aWidth, aHeight);
        nsEventStatus status;
        DispatchResizeEvent(rect, status);
    }

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::Resize(PRInt32 aX, PRInt32 aY, PRInt32 aWidth, PRInt32 aHeight,
                       PRBool aRepaint)
{
    mBounds.x = aX;
    mBounds.y = aY;
    mBounds.SizeTo(GetSafeWindowSize(nsIntSize(aWidth, aHeight)));

    mPlaced = PR_TRUE;

    if (!mCreated)
        return NS_OK;

    // There are several cases here that we need to handle, based on a
    // matrix of the visibility of the widget, the sanity of this resize
    // and whether or not the widget was previously sane.

    // Has this widget been set to visible?
    if (mIsShown) {
        // Are the bounds sane?
        if (AreBoundsSane()) {
            // Yep?  Resize the window
            NativeResize(aX, aY, aWidth, aHeight, aRepaint);
            // Does it need to be shown because it was previously insane?
            if (mNeedsShow)
                NativeShow(PR_TRUE);
        }
        else {
            // If someone has set this so that the needs show flag is false
            // and it needs to be hidden, update the flag and hide the
            // window.  This flag will be cleared the next time someone
            // hides the window or shows it.  It also prevents us from
            // calling NativeShow(PR_FALSE) excessively on the window which
            // causes unneeded X traffic.
            if (!mNeedsShow) {
                mNeedsShow = PR_TRUE;
                NativeShow(PR_FALSE);
            }
        }
    }
    // If the widget hasn't been shown, mark the widget as needing to be
    // resized before it is shown
    else {
        if (AreBoundsSane() && mListenForResizes){
            // For widgets that we listen for resizes for (widgets created
            // with native parents) we apparently _always_ have to resize.  I
            // dunno why, but apparently we're lame like that.
            NativeResize(aX, aY, aWidth, aHeight, aRepaint);
        }
        else {
            mNeedsResize = PR_TRUE;
            mNeedsMove = PR_TRUE;
        }
    }

    if (mIsTopLevel || mListenForResizes) {
        // synthesize a resize event
        nsIntRect rect(aX, aY, aWidth, aHeight);
        nsEventStatus status;
        DispatchResizeEvent(rect, status);
    }

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::Enable(PRBool aState)
{
    mEnabled = aState;

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::IsEnabled(PRBool *aState)
{
    *aState = mEnabled;

    return NS_OK;
}



NS_IMETHODIMP
nsWindow::Move(PRInt32 aX, PRInt32 aY)
{
    LOG(("nsWindow::Move [%p] %d %d\n", (void *)this,
         aX, aY));

    if (mWindowType == eWindowType_toplevel ||
        mWindowType == eWindowType_dialog) {
        SetSizeMode(nsSizeMode_Normal);
    }

    mPlaced = PR_TRUE;

    // Since a popup window's x/y coordinates are in relation to to
    // the parent, the parent might have moved so we always move a
    // popup window.
    if (aX == mBounds.x && aY == mBounds.y &&
        mWindowType != eWindowType_popup)
        return NS_OK;

    // XXX Should we do some AreBoundsSane check here?

    mBounds.x = aX;
    mBounds.y = aY;

    if (!mCreated)
        return NS_OK;

    if (mIsTopLevel) {
        gtk_window_move(GTK_WINDOW(mShell), aX, aY);
    }
    else if (mGdkWindow) {
        gdk_window_move(mGdkWindow, aX, aY);
    }

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::PlaceBehind(nsTopLevelWidgetZPlacement  aPlacement,
                      nsIWidget                  *aWidget,
                      PRBool                      aActivate)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWindow::SetZIndex(PRInt32 aZIndex)
{
    nsIWidget* oldPrev = GetPrevSibling();

    nsBaseWidget::SetZIndex(aZIndex);

    if (GetPrevSibling() == oldPrev) {
        return NS_OK;
    }

    NS_ASSERTION(!mContainer, "Expected Mozilla child widget");

    // We skip the nsWindows that don't have mGdkWindows.
    // These are probably in the process of being destroyed.

    if (!GetNextSibling()) {
        // We're to be on top.
        if (mGdkWindow)
            gdk_window_raise(mGdkWindow);
    } else {
        // All the siblings before us need to be below our widget.
        for (nsWindow* w = this; w;
             w = static_cast<nsWindow*>(w->GetPrevSibling())) {
            if (w->mGdkWindow)
                gdk_window_lower(w->mGdkWindow);
        }
    }
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::SetSizeMode(PRInt32 aMode)
{
    nsresult rv;

    LOG(("nsWindow::SetSizeMode [%p] %d\n", (void *)this, aMode));

    // Save the requested state.
    rv = nsBaseWidget::SetSizeMode(aMode);

    // return if there's no shell or our current state is the same as
    // the mode we were just set to.
    if (!mShell || mSizeState == mSizeMode) {
        return rv;
    }

    switch (aMode) {
    case nsSizeMode_Maximized:
        gtk_window_maximize(GTK_WINDOW(mShell));
        break;
    case nsSizeMode_Minimized:
        gtk_window_iconify(GTK_WINDOW(mShell));
        break;
    case nsSizeMode_Fullscreen:
        MakeFullScreen(PR_TRUE);
        break;

    default:
        // nsSizeMode_Normal, really.
        if (mSizeState == nsSizeMode_Minimized)
            gtk_window_deiconify(GTK_WINDOW(mShell));
        else if (mSizeState == nsSizeMode_Maximized)
            gtk_window_unmaximize(GTK_WINDOW(mShell));
        break;
    }

    mSizeState = mSizeMode;

    return rv;
}

typedef void (* SetUserTimeFunc)(GdkWindow* aWindow, guint32 aTimestamp);

// This will become obsolete when new GTK APIs are widely supported,
// as described here: http://bugzilla.gnome.org/show_bug.cgi?id=347375
static void
SetUserTimeAndStartupIDForActivatedWindow(GtkWidget* aWindow)
{
    nsCOMPtr<nsIToolkit> toolkit;
    NS_GetCurrentToolkit(getter_AddRefs(toolkit));
    if (!toolkit)
        return;

    nsGTKToolkit* GTKToolkit = static_cast<nsGTKToolkit*>
                                          (static_cast<nsIToolkit*>(toolkit));
    nsCAutoString desktopStartupID;
    GTKToolkit->GetDesktopStartupID(&desktopStartupID);
    if (desktopStartupID.IsEmpty()) {
        // We don't have the data we need. Fall back to an
        // approximation ... using the timestamp of the remote command
        // being received as a guess for the timestamp of the user event
        // that triggered it.
        PRUint32 timestamp = GTKToolkit->GetFocusTimestamp();
        if (timestamp) {
            gdk_window_focus(aWindow->window, timestamp);
            GTKToolkit->SetFocusTimestamp(0);
        }
        return;
    }

#ifdef MOZ_ENABLE_STARTUP_NOTIFICATION
    GdkDrawable* drawable = GDK_DRAWABLE(aWindow->window);
    GtkWindow* win = GTK_WINDOW(aWindow);
    if (!win) {
        NS_WARNING("Passed in widget was not a GdkWindow!");
        return;
    }
    GdkScreen* screen = gtk_window_get_screen(win);
    SnDisplay* snd =
        sn_display_new(gdk_x11_drawable_get_xdisplay(drawable), nsnull, nsnull);
    if (!snd)
        return;
    SnLauncheeContext* ctx =
        sn_launchee_context_new(snd, gdk_screen_get_number(screen),
                                desktopStartupID.get());
    if (!ctx) {
        sn_display_unref(snd);
        return;
    }

    if (sn_launchee_context_get_id_has_timestamp(ctx)) {
        PRLibrary* gtkLibrary;
        SetUserTimeFunc setUserTimeFunc = (SetUserTimeFunc)
            PR_FindFunctionSymbolAndLibrary("gdk_x11_window_set_user_time", &gtkLibrary);
        if (setUserTimeFunc) {
            setUserTimeFunc(aWindow->window, sn_launchee_context_get_timestamp(ctx));
            PR_UnloadLibrary(gtkLibrary);
        }
    }

    sn_launchee_context_setup_window(ctx, gdk_x11_drawable_get_xid(drawable));
    sn_launchee_context_complete(ctx);

    sn_launchee_context_unref(ctx);
    sn_display_unref(snd);
#endif

    GTKToolkit->SetDesktopStartupID(EmptyCString());
}

NS_IMETHODIMP
nsWindow::SetFocus(PRBool aRaise)
{
    // Make sure that our owning widget has focus.  If it doesn't try to
    // grab it.  Note that we don't set our focus flag in this case.

    LOGFOCUS(("  SetFocus [%p]\n", (void *)this));

    GtkWidget *owningWidget = GetMozContainerWidget();
    if (!owningWidget)
        return NS_ERROR_FAILURE;

    // Raise the window if someone passed in PR_TRUE and the prefs are
    // set properly.
    GtkWidget *toplevelWidget = gtk_widget_get_toplevel(owningWidget);

    if (gRaiseWindows && aRaise && toplevelWidget &&
        !GTK_WIDGET_HAS_FOCUS(owningWidget) &&
        !GTK_WIDGET_HAS_FOCUS(toplevelWidget)) {
        GtkWidget* top_window = nsnull;
        GetToplevelWidget(&top_window);
        if (top_window && (GTK_WIDGET_VISIBLE(top_window)))
        {
            gdk_window_show_unraised(top_window->window);
            // Unset the urgency hint if possible.
            SetUrgencyHint(top_window, PR_FALSE);
        }
    }

    nsRefPtr<nsWindow> owningWindow = get_window_for_gtk_widget(owningWidget);
    if (!owningWindow)
        return NS_ERROR_FAILURE;

    if (!GTK_WIDGET_HAS_FOCUS(owningWidget)) {
        LOGFOCUS(("  grabbing focus for the toplevel [%p]\n", (void *)this));
        owningWindow->mContainerBlockFocus = PR_FALSE;

        // Set focus to the window
        if (gRaiseWindows && aRaise && toplevelWidget &&
            !GTK_WIDGET_HAS_FOCUS(toplevelWidget) &&
            owningWindow->mIsShown && GTK_IS_WINDOW(owningWindow->mShell))
          gtk_window_present(GTK_WINDOW(owningWindow->mShell));

        gtk_widget_grab_focus(owningWidget);

        return NS_OK;
    }

    // If this is the widget that already has focus, return.
    if (gFocusWindow == this) {
        LOGFOCUS(("  already have focus [%p]\n", (void *)this));
        return NS_OK;
    }

#ifdef USE_XIM
    if (gFocusWindow) {
        // If the focus window and this window share the same input
        // context we don't have to change the focus of the IME
        // context
        nsRefPtr<nsWindow> kungFuDeathGrip = gFocusWindow;
        if (IM_get_input_context(this) !=
            IM_get_input_context(gFocusWindow))
            gFocusWindow->IMELoseFocus();
    }
#endif

    // Set this window to be the focused child window
    gFocusWindow = this;

#ifdef USE_XIM
    IMESetFocus();
#endif

    LOGFOCUS(("  widget now has focus in SetFocus() [%p]\n",
              (void *)this));

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::GetScreenBounds(nsIntRect &aRect)
{
    aRect = nsIntRect(WidgetToScreenOffset(), mBounds.Size());
    LOG(("GetScreenBounds %d %d | %d %d | %d %d\n",
         aRect.x, aRect.y,
         mBounds.width, mBounds.height,
         aRect.width, aRect.height));
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::SetForegroundColor(const nscolor &aColor)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWindow::SetBackgroundColor(const nscolor &aColor)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWindow::SetCursor(nsCursor aCursor)
{
    // if we're not the toplevel window pass up the cursor request to
    // the toplevel window to handle it.
    if (!mContainer && mGdkWindow) {
        nsWindow *window = GetContainerWindow();
        if (!window)
            return NS_ERROR_FAILURE;

        return window->SetCursor(aCursor);
    }

    // Only change cursor if it's actually been changed
    if (aCursor != mCursor) {
        GdkCursor *newCursor = NULL;

        newCursor = get_gtk_cursor(aCursor);

        if (nsnull != newCursor) {
            mCursor = aCursor;

            if (!mContainer)
                return NS_OK;

            gdk_window_set_cursor(GTK_WIDGET(mContainer)->window, newCursor);
        }
    }

    return NS_OK;
}


static
PRUint8* Data32BitTo1Bit(PRUint8* aImageData,
                         PRUint32 aImageBytesPerRow,
                         PRUint32 aWidth, PRUint32 aHeight)
{
  PRUint32 outBpr = (aWidth + 7) / 8;

  PRUint8* outData = new PRUint8[outBpr * aHeight];
  if (!outData)
      return NULL;

  PRUint8 *outRow = outData,
          *imageRow = aImageData;

  for (PRUint32 curRow = 0; curRow < aHeight; curRow++) {
      PRUint8 *irow = imageRow;
      PRUint8 *orow = outRow;
      PRUint8 imagePixels = 0;
      PRUint8 offset = 0;

      for (PRUint32 curCol = 0; curCol < aWidth; curCol++) {
          PRUint8 r = *imageRow++,
                  g = *imageRow++,
                  b = *imageRow++;
               /* a = * */imageRow++;

          if ((r + b + g) < 3 * 128)
              imagePixels |= (1 << offset);

          if (offset == 7) {
              *outRow++ = imagePixels;
              offset = 0;
              imagePixels = 0;
          } else {
              offset++;
          }
      }
      if (offset != 0)
          *outRow++ = imagePixels;

      imageRow = irow + aImageBytesPerRow;
      outRow = orow + outBpr;
  }

  return outData;
}



NS_IMETHODIMP
nsWindow::SetCursor(imgIContainer* aCursor,
                    PRUint32 aHotspotX, PRUint32 aHotspotY)
{
    // if we're not the toplevel window pass up the cursor request to
    // the toplevel window to handle it.
    if (!mContainer && mGdkWindow) {
        nsWindow *window = GetContainerWindow();
        if (!window)
            return NS_ERROR_FAILURE;

        return window->SetCursor(aCursor, aHotspotX, aHotspotY);
    }

    if (!sPixbufCursorChecked) {
        PRLibrary* lib;
        _gdk_cursor_new_from_pixbuf = (_gdk_cursor_new_from_pixbuf_fn)
            PR_FindFunctionSymbolAndLibrary("gdk_cursor_new_from_pixbuf", &lib);
        if (lib) {
            // We already link against GDK, so we can unload it.
            PR_UnloadLibrary(lib);
            lib = nsnull;
        }
        _gdk_display_get_default = (_gdk_display_get_default_fn)
            PR_FindFunctionSymbolAndLibrary("gdk_display_get_default", &lib);
        if (lib) {
            // We already link against GDK, so we can unload it.
            PR_UnloadLibrary(lib);
            lib = nsnull;
        }
        sPixbufCursorChecked = PR_TRUE;
    }
    mCursor = nsCursor(-1);

    // Get the image's current frame
    GdkPixbuf* pixbuf = nsImageToPixbuf::ImageToPixbuf(aCursor);
    if (!pixbuf)
        return NS_ERROR_NOT_AVAILABLE;

    int width = gdk_pixbuf_get_width(pixbuf);
    int height = gdk_pixbuf_get_height(pixbuf);
    // Reject cursors greater than 128 pixels in some direction, to prevent
    // spoofing.
    // XXX ideally we should rescale. Also, we could modify the API to
    // allow trusted content to set larger cursors.
    if (width > 128 || height > 128) {
        g_object_unref(pixbuf);
        return NS_ERROR_NOT_AVAILABLE;
    }

    // Looks like all cursors need an alpha channel (tested on Gtk 2.4.4). This
    // is of course not documented anywhere...
    // So add one if there isn't one yet
    if (!gdk_pixbuf_get_has_alpha(pixbuf)) {
        GdkPixbuf* alphaBuf = gdk_pixbuf_add_alpha(pixbuf, FALSE, 0, 0, 0);
        g_object_unref(pixbuf);
        if (!alphaBuf) {
            return NS_ERROR_OUT_OF_MEMORY;
        }
        pixbuf = alphaBuf;
    }

    GdkCursor* cursor;
    if (!_gdk_cursor_new_from_pixbuf || !_gdk_display_get_default) {
        // Fallback to a monochrome cursor
        GdkPixmap* mask = gdk_pixmap_new(NULL, width, height, 1);
        if (!mask) {
            g_object_unref(pixbuf);
            return NS_ERROR_OUT_OF_MEMORY;
        }

        PRUint8* data = Data32BitTo1Bit(gdk_pixbuf_get_pixels(pixbuf),
                                        gdk_pixbuf_get_rowstride(pixbuf),
                                        width, height);
        if (!data) {
            g_object_unref(mask);
            g_object_unref(pixbuf);
            return NS_ERROR_OUT_OF_MEMORY;
        }

        GdkPixmap* image = gdk_bitmap_create_from_data(NULL, (const gchar*)data, width,
                                                       height);
        delete[] data;
        if (!image) {
            g_object_unref(mask);
            g_object_unref(pixbuf);
            return NS_ERROR_OUT_OF_MEMORY;
        }

        gdk_pixbuf_render_threshold_alpha(pixbuf, mask, 0, 0, 0, 0, width,
                                          height, 1);

        GdkColor fg = { 0, 0, 0, 0 }; // Black
        GdkColor bg = { 0, 0xFFFF, 0xFFFF, 0xFFFF }; // White

        cursor = gdk_cursor_new_from_pixmap(image, mask, &fg, &bg, aHotspotX,
                                            aHotspotY);
        g_object_unref(image);
        g_object_unref(mask);
    } else {
        // Now create the cursor
        cursor = _gdk_cursor_new_from_pixbuf(_gdk_display_get_default(),
                                             pixbuf,
                                             aHotspotX, aHotspotY);
    }
    g_object_unref(pixbuf);
    nsresult rv = NS_ERROR_OUT_OF_MEMORY;
    if (cursor) {
        if (mContainer) {
            gdk_window_set_cursor(GTK_WIDGET(mContainer)->window, cursor);
            rv = NS_OK;
        }
        gdk_cursor_unref(cursor);
    }

    return rv;
}

NS_IMETHODIMP
nsWindow::Invalidate(const nsIntRect &aRect,
                     PRBool           aIsSynchronous)
{
    if (!mGdkWindow)
        return NS_OK;

    GdkRectangle rect;
    rect.x = aRect.x;
    rect.y = aRect.y;
    rect.width = aRect.width;
    rect.height = aRect.height;

    LOGDRAW(("Invalidate (rect) [%p]: %d %d %d %d (sync: %d)\n", (void *)this,
             rect.x, rect.y, rect.width, rect.height, aIsSynchronous));

    gdk_window_invalidate_rect(mGdkWindow, &rect, FALSE);
    if (aIsSynchronous)
        gdk_window_process_updates(mGdkWindow, FALSE);

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::Update()
{
    if (!mGdkWindow)
        return NS_OK;

    LOGDRAW(("Update [%p] %p\n", this, mGdkWindow));

    gdk_window_process_updates(mGdkWindow, FALSE);
    // Send the updates to the server.
    gdk_display_flush(gdk_drawable_get_display(GDK_DRAWABLE(mGdkWindow)));
    return NS_OK;
}

static pixman_box32
ToPixmanBox(const nsIntRect& aRect)
{
    pixman_box32_t result;
    result.x1 = aRect.x;
    result.y1 = aRect.y;
    result.x2 = aRect.XMost();
    result.y2 = aRect.YMost();
    return result;
}

static nsIntRect
ToIntRect(const pixman_box32& aBox)
{
    nsIntRect result;
    result.x = aBox.x1;
    result.y = aBox.y1;
    result.width = aBox.x2 - aBox.x1;
    result.height = aBox.y2 - aBox.y1;
    return result;
}

static void
InitRegion(pixman_region32* aRegion,
           const nsTArray<nsIntRect>& aRects)
{
    nsAutoTArray<pixman_box32,10> rects;
    rects.SetCapacity(aRects.Length());
    for (PRUint32 i = 0; i < aRects.Length (); ++i) {
        if (!aRects[i].IsEmpty()) {
            rects.AppendElement(ToPixmanBox(aRects[i]));
        }
    }

    pixman_region32_init_rects(aRegion,
                               rects.Elements(), rects.Length());
}

static void
GetIntRects(pixman_region32& aRegion, nsTArray<nsIntRect>* aRects)
{
    int nRects;
    pixman_box32* boxes = pixman_region32_rectangles(&aRegion, &nRects);
    aRects->SetCapacity(aRects->Length() + nRects);
    for (int i = 0; i < nRects; ++i) {
        aRects->AppendElement(ToIntRect(boxes[i]));
    }
}

/**
 * ScrollItemIter uses ScrollRectIterBase to order blit rectangles and
 * rectangular child clip regions in a way such that moving the items in this
 * order will avoid conflicts of blit rectangles.  Conflicts with child
 * windows are also avoided in situations with simple child window
 * arrangements.
 *
 * The blit rectangles must not intersect with any other rectangles (of either
 * blits or children).  Note that child clip regions are not guaranteed to be
 * exclusive of other child clip regions, so ScrollItemIter may not
 * necessarily provide an optimal order (if a child rectangle intersects
 * another child rectangle).
 */
class ScrollItemIter : public ScrollRectIterBase {
public:
    // Each aChildRects[i] corresponds to
    ScrollItemIter(const nsIntPoint& aDelta,
                   const nsTArray<nsIntRect>& aBlitRects,
                   const nsTArray<const nsIWidget::Configuration*>aChildConfs,
                   const nsTArray<nsIntRect>& aChildSubRects);

    PRBool IsBlit() const { return !Configuration(); };

private:
    struct ScrollItem : public ScrollRect {
        ScrollItem(const nsIntRect& aIntRect) : ScrollRect(aIntRect) {}

        const nsIWidget::Configuration *mChildConf;
    };

public:
    const nsIWidget::Configuration* Configuration() const
    {
        return static_cast<const ScrollItem&>(Rect()).mChildConf;
    }

private:
    // Copying is not supported.
    ScrollItemIter(const ScrollItemIter&);
    void operator=(const ScrollItemIter&);

    nsTArray<ScrollItem> mRects;
};

ScrollItemIter::ScrollItemIter(const nsIntPoint& aDelta,
                               const nsTArray<nsIntRect>& aBlitRects,
                               const nsTArray<const nsIWidget::Configuration*>aChildConfs,
                               const nsTArray<nsIntRect>& aChildSubRects)
  : mRects(aBlitRects.Length() + aChildConfs.Length())
{
    for (PRUint32 i = 0; i < aBlitRects.Length(); ++i) {
        if (ScrollItem* item = mRects.AppendElement(aBlitRects[i])) {
            item->mChildConf = nsnull;
        }
    }

    PRUint32 numChildren =
        NS_MIN(aChildConfs.Length(), aChildSubRects.Length());
    for (PRUint32 i = 0; i < numChildren; ++i) {
        if (ScrollItem* item = mRects.AppendElement(aChildSubRects[i])) {
            item->mChildConf = aChildConfs[i];
        }
    }

    // Link items into a chain.
    ScrollRect *next = nsnull;
    for (PRUint32 i = mRects.Length(); i--; ) {
        mRects[i].mNext = next;
        next = &mRects[i];
    }

    BaseInit(aDelta, next);
}

void
nsWindow::Scroll(const nsIntPoint& aDelta,
                 const nsTArray<nsIntRect>& aDestRects,
                 const nsTArray<Configuration>& aConfigurations)
{
    if (!mGdkWindow) {
        NS_ERROR("Cannot scroll widget");
        return;
    }

    // Empty Xlib's request buffer to reduce the likelihood of it getting
    // emptied mid way through the scroll, in the hope that the server gets
    // all the requests at once.
    gdk_display_flush(gdk_drawable_get_display(GDK_DRAWABLE(mGdkWindow)));

    // Collect the destination positions of moving child windows where they
    // will eventually obscure their parent.
    nsTArray<const Configuration*> movingChildren;
    nsTArray<nsIntRect> movingChildSubRects;

    for (PRUint32 i = 0; i < aConfigurations.Length(); ++i) {
        const Configuration* conf = &aConfigurations[i];
        nsWindow* w = static_cast<nsWindow*>(conf->mChild);
        NS_ASSERTION(w->GetParent() == this,
                     "Configured widget is not a child");

        if (!w->mIsShown)
            continue;

        // Set the clip region of all visible windows to the intersection of
        // the current and new region.  This reduces the conflict area with
        // other objects (including stationary objects).
        w->SetWindowClipRegion(conf->mClipRegion, PR_TRUE);

        if (conf->mBounds.TopLeft() == w->mBounds.TopLeft())
            continue; // window is not moving

        nsAutoTArray<nsIntRect,1> rects; // of clip region intersection
        w->GetWindowClipRegion(&rects);

        // ScrollItemIter is designed only for rectangular scroll items.
        //
        // It is not suitable to use the bounding rectangle of complex child
        // clip regions because that rectangle may intersect with blit
        // rectangles and ScrollItemIter would then not necessarily provide
        // the correct order for any such blit rectangles.  If child windows
        // are not moved in the optimal order there will be some flicker
        // during the scroll, which will be corrected through invalidations,
        // but, if blit rectangles were moved in the wrong order, then some
        // parts would get moved twice, which would not be corrected through
        // invalidations.
        //
        // Choosing a sub-rectangle for the scroll item would make some parts
        // of the child window scroll nicely but not others.  This can
        // actually look worse than the whole window being moved out of order,
        // so moving of child windows with complex clip regions is simply
        // delayed until after blitting.
        if (rects.Length() != 1)
            continue; // no moving content or non-rectangular clip-region

        movingChildren.AppendElement(conf);

        // Destination position wrt mGdkWindow top left.
        nsIntRect subRect = rects[0] + conf->mBounds.TopLeft();
        movingChildSubRects.AppendElement(subRect);
    }

    nsAutoRef<pixman_region32> blitRegion;
    InitRegion(&blitRegion, aDestRects);

    // Remove some parts of the moving parent region that will be covered by
    // moving child widgets.  These parts won't need drawing anyway, and it
    // breaks up the blit rectangles so that we have a chance of moving them
    // without conflicts with child rectangles.
    //
    // Also, subtracting the child sub-rectangles from the blit region ensures
    // that the blit rectangles will not overlap with any blit or child
    // rectangles, so ScrollItemIter will ensure that blit rectangles do not
    // conflict with each other.
    {
        nsAutoRef<pixman_region32> childRegion;
        InitRegion(&childRegion, movingChildSubRects);

        pixman_region32_subtract(&blitRegion, &blitRegion, &childRegion);
    }

    nsTArray<nsIntRect> blitRects;
    GetIntRects(blitRegion, &blitRects);

    GdkRegion* updateArea = gdk_region_new(); // aborts on OOM

    for (ScrollItemIter iter(aDelta, blitRects,
                             movingChildren, movingChildSubRects);
         !iter.IsDone(); ++iter) {
        if (iter.IsBlit()) {
            // The parts of source regions not covered by their destination
            // get marked invalid by gdk_window_move_region.  This is
            // necessary (until covered by another blit) because GDK
            // translates any pending expose events to the destination, and so
            // doesn't know whether an expose event might have been due on the
            // source.
            //
            // However, GDK 2.18 does not subtract the invalid regions at the
            // destinations from the update_area, so the seams between
            // different moves remain invalid.  GDK 2.18 also delays and
            // queues move operations.  If gdk_window_process_updates is
            // called before the moves are flushed, GDK 2.18 removes the
            // invalid seams from the move regions, so the seams are left with
            // their old content until they get redrawn.  Therefore, the
            // subtraction of destination invalid regions is performed here.
            GdkRegion* recentUpdates = gdk_window_get_update_area(mGdkWindow);
            if (recentUpdates) {
                gdk_region_union(updateArea, recentUpdates);
                gdk_region_destroy(recentUpdates);
            } 

            // We don't attempt to collect rects into regions because
            // gdk_window_move_region, up to GDK 2.16, has a bug where it
            // doesn't restrict blitting to the given region, and blits its
            // full bounding box.
            nsIntRect source = iter.Rect() - aDelta;
            GdkRectangle gdkSource =
                { source.x, source.y, source.width, source.height };
            GdkRegion* rectRegion = gdk_region_rectangle(&gdkSource);
            gdk_window_move_region(mGdkWindow, rectRegion,
                                   aDelta.x, aDelta.y);

            // The update_area on mGdkWindow from the move_region contains
            // invalidations from the move:
            // 1. The part of the source region not covered by the destination.
            // 2. Any destination regions for which the source was obscured by
            //    parent window clips or child windows.
            //
            // Our copy of the old invalid region needs adjusting.

            // The part of the old invalid region that is moving.
            GdkRegion* updateChanges = gdk_region_copy(rectRegion);
            gdk_region_intersect(updateChanges, updateArea);
            gdk_region_offset(updateChanges, aDelta.x, aDelta.y);

            // Make |rectRegion| the destination
            gdk_region_offset(rectRegion, aDelta.x, aDelta.y);
            // Remove any old invalid areas covered at the destination.
            gdk_region_subtract(updateArea, rectRegion);
            gdk_region_union(updateArea, updateChanges);

            gdk_region_destroy(updateChanges);
            gdk_region_destroy(rectRegion);
        } else {
            const Configuration *conf = iter.Configuration();
            nsWindow* w = static_cast<nsWindow*>(conf->mChild);
            const nsIntRect& newBounds = conf->mBounds;
            // (This move will modify the invalid_area on mGdkWindow to
            // include areas that are uncovered when the child moves.)
            w->Move(newBounds.x, newBounds.y);
        }
    }

    gdk_window_invalidate_region(mGdkWindow, updateArea, FALSE);
    gdk_region_destroy(updateArea);

    ConfigureChildren(aConfigurations);
}

void*
nsWindow::GetNativeData(PRUint32 aDataType)
{
    switch (aDataType) {
    case NS_NATIVE_WINDOW:
    case NS_NATIVE_WIDGET: {
        if (!mGdkWindow)
            return nsnull;

        return mGdkWindow;
        break;
    }

    case NS_NATIVE_PLUGIN_PORT:
        return SetupPluginPort();
        break;

    case NS_NATIVE_DISPLAY:
#ifdef MOZ_X11
        return GDK_DISPLAY();
#else
        return nsnull;
#endif /* MOZ_X11 */
        break;

    case NS_NATIVE_GRAPHIC: {
        NS_ASSERTION(nsnull != mToolkit, "NULL toolkit, unable to get a GC");
        return (void *)static_cast<nsGTKToolkit *>(mToolkit)->GetSharedGC();
        break;
    }

    case NS_NATIVE_SHELLWIDGET:
        return (void *) mShell;

    default:
        NS_WARNING("nsWindow::GetNativeData called with bad value");
        return nsnull;
    }
}

NS_IMETHODIMP
nsWindow::SetTitle(const nsAString& aTitle)
{
    if (!mShell)
        return NS_OK;

    // convert the string into utf8 and set the title.
#define UTF8_FOLLOWBYTE(ch) (((ch) & 0xC0) == 0x80)
    NS_ConvertUTF16toUTF8 titleUTF8(aTitle);
    if (titleUTF8.Length() > NS_WINDOW_TITLE_MAX_LENGTH) {
        // Truncate overlong titles (bug 167315). Make sure we chop after a
        // complete sequence by making sure the next char isn't a follow-byte.
        PRUint32 len = NS_WINDOW_TITLE_MAX_LENGTH;
        while(UTF8_FOLLOWBYTE(titleUTF8[len]))
            --len;
        titleUTF8.Truncate(len);
    }
    gtk_window_set_title(GTK_WINDOW(mShell), (const char *)titleUTF8.get());

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::SetIcon(const nsAString& aIconSpec)
{
    if (!mShell)
        return NS_OK;

    nsCOMPtr<nsILocalFile> iconFile;
    nsCAutoString path;
    nsTArray<nsCString> iconList;

    // Look for icons with the following suffixes appended to the base name.
    // The last two entries (for the old XPM format) will be ignored unless
    // no icons are found using the other suffixes. XPM icons are depricated.

    const char extensions[6][7] = { ".png", "16.png", "32.png", "48.png",
                                    ".xpm", "16.xpm" };

    for (PRUint32 i = 0; i < NS_ARRAY_LENGTH(extensions); i++) {
        // Don't bother looking for XPM versions if we found a PNG.
        if (i == NS_ARRAY_LENGTH(extensions) - 2 && iconList.Length())
            break;

        nsAutoString extension;
        extension.AppendASCII(extensions[i]);

        ResolveIconName(aIconSpec, extension, getter_AddRefs(iconFile));
        if (iconFile) {
            iconFile->GetNativePath(path);
            iconList.AppendElement(path);
        }
    }

    // leave the default icon intact if no matching icons were found
    if (iconList.Length() == 0)
        return NS_OK;

    return SetWindowIconList(iconList);
}

nsIntPoint
nsWindow::WidgetToScreenOffset()
{
    gint x = 0, y = 0;

    if (mContainer) {
        gdk_window_get_root_origin(GTK_WIDGET(mContainer)->window,
                                   &x, &y);
        LOG(("WidgetToScreenOffset (container) %d %d\n", x, y));
    }
    else if (mGdkWindow) {
        gdk_window_get_origin(mGdkWindow, &x, &y);
        LOG(("WidgetToScreenOffset (drawing) %d %d\n", x, y));
    }

    return nsIntPoint(x, y);
}

NS_IMETHODIMP
nsWindow::EnableDragDrop(PRBool aEnable)
{
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::CaptureMouse(PRBool aCapture)
{
    LOG(("CaptureMouse %p\n", (void *)this));

    if (!mGdkWindow)
        return NS_OK;

    GtkWidget *widget = GetMozContainerWidget();
    if (!widget)
        return NS_ERROR_FAILURE;

    if (aCapture) {
        gtk_grab_add(widget);
        GrabPointer();
    }
    else {
        ReleaseGrabs();
        gtk_grab_remove(widget);
    }

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::CaptureRollupEvents(nsIRollupListener *aListener,
                              nsIMenuRollup     *aMenuRollup,
                              PRBool             aDoCapture,
                              PRBool             aConsumeRollupEvent)
{
    if (!mGdkWindow)
        return NS_OK;

    GtkWidget *widget = GetMozContainerWidget();
    if (!widget)
        return NS_ERROR_FAILURE;

    LOG(("CaptureRollupEvents %p\n", (void *)this));

    if (aDoCapture) {
        gConsumeRollupEvent = aConsumeRollupEvent;
        gRollupListener = aListener;
        NS_IF_RELEASE(gMenuRollup);
        gMenuRollup = aMenuRollup;
        NS_IF_ADDREF(aMenuRollup);
        gRollupWindow = do_GetWeakReference(static_cast<nsIWidget*>
                                                       (this));
        // real grab is only done when there is no dragging
        if (!nsWindow::DragInProgress()) {
            gtk_grab_add(widget);
            GrabPointer();
            GrabKeyboard();
        }
    }
    else {
        if (!nsWindow::DragInProgress()) {
            ReleaseGrabs();
            gtk_grab_remove(widget);
        }
        gRollupListener = nsnull;
        NS_IF_RELEASE(gMenuRollup);
        gRollupWindow = nsnull;
    }

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::GetAttention(PRInt32 aCycleCount)
{
    LOG(("nsWindow::GetAttention [%p]\n", (void *)this));

    GtkWidget* top_window = nsnull;
    GtkWidget* top_focused_window = nsnull;
    GetToplevelWidget(&top_window);
    if (gFocusWindow)
        gFocusWindow->GetToplevelWidget(&top_focused_window);

    // Don't get attention if the window is focused anyway.
    if (top_window && (GTK_WIDGET_VISIBLE(top_window)) &&
        top_window != top_focused_window) {
        SetUrgencyHint(top_window, PR_TRUE);
    }

    return NS_OK;
}

PRBool
nsWindow::HasPendingInputEvent()
{
    // This sucks, but gtk/gdk has no way to answer the question we want while
    // excluding paint events, and there's no X API that will let us peek
    // without blocking or removing.  To prevent event reordering, peek
    // anything except expose events.  Reordering expose and others should be
    // ok, hopefully.
    PRBool haveEvent;
#ifdef MOZ_X11
    XEvent ev;
    haveEvent =
        XCheckMaskEvent(GDK_DISPLAY(),
                        KeyPressMask | KeyReleaseMask | ButtonPressMask |
                        ButtonReleaseMask | EnterWindowMask | LeaveWindowMask |
                        PointerMotionMask | PointerMotionHintMask |
                        Button1MotionMask | Button2MotionMask |
                        Button3MotionMask | Button4MotionMask |
                        Button5MotionMask | ButtonMotionMask | KeymapStateMask |
                        VisibilityChangeMask | StructureNotifyMask |
                        ResizeRedirectMask | SubstructureNotifyMask |
                        SubstructureRedirectMask | FocusChangeMask |
                        PropertyChangeMask | ColormapChangeMask |
                        OwnerGrabButtonMask, &ev);
    if (haveEvent) {
        XPutBackEvent(GDK_DISPLAY(), &ev);
    }
#else
    haveEvent = PR_FALSE;
#endif
    return haveEvent;
}

#if 0
#ifdef DEBUG
// Paint flashing code (disabled for cairo - see below)

#define CAPS_LOCK_IS_ON \
(gdk_keyboard_get_modifiers() & GDK_LOCK_MASK)

#define WANT_PAINT_FLASHING \
(debug_WantPaintFlashing() && CAPS_LOCK_IS_ON)

#ifdef MOZ_X11
static void
gdk_window_flash(GdkWindow *    aGdkWindow,
                 unsigned int   aTimes,
                 unsigned int   aInterval,  // Milliseconds
                 GdkRegion *    aRegion)
{
  gint         x;
  gint         y;
  gint         width;
  gint         height;
  guint        i;
  GdkGC *      gc = 0;
  GdkColor     white;

  gdk_window_get_geometry(aGdkWindow,
                          NULL,
                          NULL,
                          &width,
                          &height,
                          NULL);

  gdk_window_get_origin (aGdkWindow,
                         &x,
                         &y);

  gc = gdk_gc_new(GDK_ROOT_PARENT());

  white.pixel = WhitePixel(gdk_display,DefaultScreen(gdk_display));

  gdk_gc_set_foreground(gc,&white);
  gdk_gc_set_function(gc,GDK_XOR);
  gdk_gc_set_subwindow(gc,GDK_INCLUDE_INFERIORS);

  gdk_region_offset(aRegion, x, y);
  gdk_gc_set_clip_region(gc, aRegion);

  /*
   * Need to do this twice so that the XOR effect can replace
   * the original window contents.
   */
  for (i = 0; i < aTimes * 2; i++)
  {
    gdk_draw_rectangle(GDK_ROOT_PARENT(),
                       gc,
                       TRUE,
                       x,
                       y,
                       width,
                       height);

    gdk_flush();

    PR_Sleep(PR_MillisecondsToInterval(aInterval));
  }

  gdk_gc_destroy(gc);

  gdk_region_offset(aRegion, -x, -y);
}
#endif /* MOZ_X11 */
#endif // DEBUG
#endif

gboolean
nsWindow::OnExposeEvent(GtkWidget *aWidget, GdkEventExpose *aEvent)
{
    if (mIsDestroyed) {
        LOG(("Expose event on destroyed window [%p] window %p\n",
             (void *)this, (void *)aEvent->window));
        return FALSE;
    }

    // Windows that are not visible will be painted after they become visible.
    if (!mGdkWindow || mIsFullyObscured || !mHasMappedToplevel)
        return FALSE;

    static NS_DEFINE_CID(kRegionCID, NS_REGION_CID);

    nsCOMPtr<nsIRegion> updateRegion = do_CreateInstance(kRegionCID);
    if (!updateRegion)
        return FALSE;

    updateRegion->Init();

    GdkRectangle *rects;
    gint nrects;
    gdk_region_get_rectangles(aEvent->region, &rects, &nrects);
    if (NS_UNLIKELY(!rects)) // OOM
        return FALSE;

    if (nrects > MAX_RECTS_IN_REGION) {
        // Just use the bounding box
        rects[0] = aEvent->area;
        nrects = 1;
    }

    LOGDRAW(("sending expose event [%p] %p 0x%lx (rects follow):\n",
             (void *)this, (void *)aEvent->window,
             GDK_WINDOW_XWINDOW(aEvent->window)));

    GdkRectangle *r;
    GdkRectangle *r_end = rects + nrects;
    for (r = rects; r < r_end; ++r) {
        updateRegion->Union(r->x, r->y, r->width, r->height);
        LOGDRAW(("\t%d %d %d %d\n", r->x, r->y, r->width, r->height));
    }

    PRBool translucent = eTransparencyTransparent == GetTransparencyMode();
    if (!translucent) {
        GList *children =
            gdk_window_peek_children(mGdkWindow);
        while (children) {
            GdkWindow *gdkWin = GDK_WINDOW(children->data);
            nsWindow *kid = get_window_for_gdk_window(gdkWin);
            if (kid && gdk_window_is_visible(gdkWin)) {
                nsAutoTArray<nsIntRect,1> clipRects;
                kid->GetWindowClipRegion(&clipRects);
                nsIntRect bounds;
                kid->GetBounds(bounds);
                for (PRUint32 i = 0; i < clipRects.Length(); ++i) {
                    nsIntRect r = clipRects[i] + bounds.TopLeft();
                    updateRegion->Subtract(r.x, r.y, r.width, r.height);
                }
            }
            children = children->next;
        }
    }

    if (updateRegion->IsEmpty()) {
        g_free(rects);
        return TRUE;
    }

#ifdef MOZ_DFB
    nsCOMPtr<nsIRenderingContext> rc = getter_AddRefs(GetRenderingContext());
    if (NS_UNLIKELY(!rc)) {
        g_free(rects);
        return FALSE;
    }

    // do double-buffering and clipping here
    nsRefPtr<gfxContext> ctx = rc->ThebesContext();

    gfxPlatformGtk::GetPlatform()->SetGdkDrawable(ctx->OriginalSurface(),
                                                  GDK_DRAWABLE(mGdkWindow));

    // clip to the update region
    ctx->Save();
    ctx->NewPath();
    for (r = rects; r < r_end; ++r) {
        ctx->Rectangle(gfxRect(r->x, r->y, r->width, r->height));
    }
    ctx->Clip();
#endif

#ifdef MOZ_X11
    nsCOMPtr<nsIRenderingContext> rc = getter_AddRefs(GetRenderingContext());
    if (NS_UNLIKELY(!rc)) {
        g_free(rects);
        return FALSE;
    }

    nsIntRect boundsRect;

    GdkPixmap* bufferPixmap = nsnull;
    gfxIntSize bufferPixmapSize;

    nsRefPtr<gfxASurface> bufferPixmapSurface;

    updateRegion->GetBoundingBox(&boundsRect.x, &boundsRect.y,
                                 &boundsRect.width, &boundsRect.height);

    // do double-buffering and clipping here
    nsRefPtr<gfxContext> ctx = rc->ThebesContext();
    ctx->Save();
    ctx->NewPath();
    if (translucent) {
        // Collapse update area to the bounding box. This is so we only have to
        // call UpdateTranslucentWindowAlpha once. After we have dropped
        // support for non-Thebes graphics, UpdateTranslucentWindowAlpha will be
        // our private interface so we can rework things to avoid this.
        ctx->Rectangle(gfxRect(boundsRect.x, boundsRect.y,
                               boundsRect.width, boundsRect.height));
    } else {
        for (r = rects; r < r_end; ++r) {
            ctx->Rectangle(gfxRect(r->x, r->y, r->width, r->height));
        }
    }
    ctx->Clip();

    // double buffer
    if (translucent) {
        ctx->PushGroup(gfxASurface::CONTENT_COLOR_ALPHA);
    } else {
        // Instead of just doing PushGroup we're going to do a little dance
        // to ensure that GDK creates the pixmap, so it doesn't go all
        // XGetGeometry on us in gdk_pixmap_foreign_new_for_display when we
        // paint native themes
        gint depth;

        if (gForce24bpp) {
            depth = 24; // 24 always
        } else {
            depth = gdk_drawable_get_depth(GDK_DRAWABLE(mGdkWindow));
        }

        // Make sure we won't create something that will overload the X server
        nsIntSize safeSize = GetSafeWindowSize(boundsRect.Size());
        boundsRect.width = safeSize.width;
        boundsRect.height = safeSize.height;

        if (!gUseBufferPixmap ||
            boundsRect.width > gBufferPixmapMaxSize.width ||
            boundsRect.height > gBufferPixmapMaxSize.height)
        {
            // create a one-off always if we're not using the global pixmap
            // if gUseBufferPixmap == TRUE, who's redrawing an area bigger than the screen?
            bufferPixmap = gdk_pixmap_new(GDK_DRAWABLE(mGdkWindow),
                                          boundsRect.width, boundsRect.height,
                                          depth);
            bufferPixmapSize.width = boundsRect.width;
            bufferPixmapSize.height = boundsRect.height;
        } else if (boundsRect.width > gBufferPixmapSize.width ||
                   boundsRect.height > gBufferPixmapSize.height)
        {
            // grow the global pixmap
            if (gBufferPixmap)
                g_object_unref(G_OBJECT(gBufferPixmap));

            gBufferPixmapSize.width = PR_MAX(gBufferPixmapSize.width, boundsRect.width);
            gBufferPixmapSize.height = PR_MAX(gBufferPixmapSize.height, boundsRect.height);

            gBufferPixmap = gdk_pixmap_new(GDK_DRAWABLE(mGdkWindow),
                                           gBufferPixmapSize.width, gBufferPixmapSize.height,
                                           depth);

            // use the newly-resized global
            bufferPixmap = gBufferPixmap;
            bufferPixmapSize = gBufferPixmapSize;
        }  else {
            // global's big enough, just use it
            bufferPixmap = gBufferPixmap;
            bufferPixmapSize = gBufferPixmapSize;
        }

        if (bufferPixmap) {
            bufferPixmapSurface = GetSurfaceForGdkDrawable(GDK_DRAWABLE(bufferPixmap),
                                                           nsIntSize(bufferPixmapSize.width, bufferPixmapSize.height));

            if (bufferPixmapSurface && bufferPixmapSurface->CairoStatus()) {
                bufferPixmapSurface = nsnull;
            }
            if (bufferPixmapSurface) {
                gfxPlatformGtk::GetPlatform()->SetGdkDrawable(
                        static_cast<gfxASurface *>(bufferPixmapSurface),
                        GDK_DRAWABLE(bufferPixmap));

                bufferPixmapSurface->SetDeviceOffset(gfxPoint(-boundsRect.x, -boundsRect.y));
                nsCOMPtr<nsIRenderingContext> newRC;
                nsresult rv = GetDeviceContext()->
                    CreateRenderingContextInstance(*getter_AddRefs(newRC));
                if (NS_FAILED(rv)) {
                    bufferPixmapSurface = nsnull;
                } else {
                    rv = newRC->Init(GetDeviceContext(), bufferPixmapSurface);
                    if (NS_FAILED(rv)) {
                        bufferPixmapSurface = nsnull;
                    } else {
                        rc = newRC;
                    }
                }
            }
        }
    }

#if 0
    // NOTE: Paint flashing region would be wrong for cairo, since
    // cairo inflates the update region, etc.  So don't paint flash
    // for cairo.
#ifdef DEBUG
    if (WANT_PAINT_FLASHING && aEvent->window)
        gdk_window_flash(aEvent->window, 1, 100, aEvent->region);
#endif
#endif

#endif // MOZ_X11

    nsPaintEvent event(PR_TRUE, NS_PAINT, this);
    event.refPoint.x = aEvent->area.x;
    event.refPoint.y = aEvent->area.y;
    event.rect = nsnull;
    event.region = updateRegion;
    event.renderingContext = rc;

    nsEventStatus status;
    DispatchEvent(&event, status);

#ifdef MOZ_X11
    // DispatchEvent can Destroy us (bug 378273), avoid doing any paint
    // operations below if that happened - it will lead to XError and exit().
    if (NS_LIKELY(!mIsDestroyed)) {
        if (status != nsEventStatus_eIgnore) {
            if (translucent) {
                nsRefPtr<gfxPattern> pattern = ctx->PopGroup();
                ctx->SetOperator(gfxContext::OPERATOR_SOURCE);
                ctx->SetPattern(pattern);
                ctx->Paint();

                nsRefPtr<gfxImageSurface> img =
                    new gfxImageSurface(gfxIntSize(boundsRect.width, boundsRect.height),
                                        gfxImageSurface::ImageFormatA8);
                if (img && !img->CairoStatus()) {
                    img->SetDeviceOffset(gfxPoint(-boundsRect.x, -boundsRect.y));

                    nsRefPtr<gfxContext> imgCtx = new gfxContext(img);
                    if (imgCtx) {
                        imgCtx->SetPattern(pattern);
                        imgCtx->SetOperator(gfxContext::OPERATOR_SOURCE);
                        imgCtx->Paint();
                    }

                    UpdateTranslucentWindowAlphaInternal(nsIntRect(boundsRect.x, boundsRect.y,
                                                                   boundsRect.width, boundsRect.height),
                                                         img->Data(), img->Stride());
                }
            } else {
                if (bufferPixmapSurface) {
                    ctx->SetOperator(gfxContext::OPERATOR_SOURCE);
                    ctx->SetSource(bufferPixmapSurface);
                    ctx->Paint();
                }
            }
        } else {
            // ignore
            if (translucent)
                ctx->PopGroup();
        }

        // if we had to allocate a local pixmap, free it here
        if (bufferPixmap && bufferPixmap != gBufferPixmap)
            g_object_unref(G_OBJECT(bufferPixmap));

        ctx->Restore();
    }
#endif // MOZ_X11

#ifdef MOZ_DFB
    ctx->Restore();
#endif

    g_free(rects);

    // check the return value!
    return TRUE;
}

gboolean
nsWindow::OnConfigureEvent(GtkWidget *aWidget, GdkEventConfigure *aEvent)
{
    LOG(("configure event [%p] %d %d %d %d\n", (void *)this,
         aEvent->x, aEvent->y, aEvent->width, aEvent->height));

    // can we shortcut?
    if (mBounds.x == aEvent->x &&
        mBounds.y == aEvent->y)
        return FALSE;

    // Toplevel windows need to have their bounds set so that we can
    // keep track of our location.  It's not often that the x,y is set
    // by the layout engine.  Width and height are set elsewhere.
    if (mIsTopLevel) {
        mPlaced = PR_TRUE;
        // Need to translate this into the right coordinates
        mBounds.MoveTo(WidgetToScreenOffset());
    }

    nsGUIEvent event(PR_TRUE, NS_MOVE, this);

    event.refPoint.x = aEvent->x;
    event.refPoint.y = aEvent->y;

    // XXX mozilla will invalidate the entire window after this move
    // complete.  wtf?
    nsEventStatus status;
    DispatchEvent(&event, status);

    return FALSE;
}

void
nsWindow::OnContainerUnrealize(GtkWidget *aWidget)
{
    // The GdkWindows are about to be destroyed (but not deleted), so remove
    // their references back to their container widget while the GdkWindow
    // hierarchy is still available.

    NS_ASSERTION(mContainer == MOZ_CONTAINER(aWidget),
                 "unexpected \"unrealize\" signal");

    if (mGdkWindow) {
        DestroyChildWindows();

        g_object_set_data(G_OBJECT(mGdkWindow), "nsWindow", NULL);
        mGdkWindow = NULL;
    }
}

void
nsWindow::OnSizeAllocate(GtkWidget *aWidget, GtkAllocation *aAllocation)
{
    LOG(("size_allocate [%p] %d %d %d %d\n",
         (void *)this, aAllocation->x, aAllocation->y,
         aAllocation->width, aAllocation->height));

    nsIntRect rect(aAllocation->x, aAllocation->y,
                   aAllocation->width, aAllocation->height);

    ResizeTransparencyBitmap(rect.width, rect.height);

    mBounds.width = rect.width;
    mBounds.height = rect.height;

    if (!mGdkWindow)
        return;

    if (mTransparencyBitmap) {
      ApplyTransparencyBitmap();
    }

    nsEventStatus status;
    DispatchResizeEvent (rect, status);
}

void
nsWindow::OnDeleteEvent(GtkWidget *aWidget, GdkEventAny *aEvent)
{
    nsGUIEvent event(PR_TRUE, NS_XUL_CLOSE, this);

    event.refPoint.x = 0;
    event.refPoint.y = 0;

    nsEventStatus status;
    DispatchEvent(&event, status);
}

void
nsWindow::OnEnterNotifyEvent(GtkWidget *aWidget, GdkEventCrossing *aEvent)
{
    // XXXldb Is this the right test for embedding cases?
    if (aEvent->subwindow != NULL)
        return;

    nsMouseEvent event(PR_TRUE, NS_MOUSE_ENTER, this, nsMouseEvent::eReal);

    event.refPoint.x = nscoord(aEvent->x);
    event.refPoint.y = nscoord(aEvent->y);

    event.time = aEvent->time;

    LOG(("OnEnterNotify: %p\n", (void *)this));

    nsEventStatus status;
    DispatchEvent(&event, status);
}

static PRBool
is_top_level_mouse_exit(GdkWindow* aWindow, GdkEventCrossing *aEvent)
{
    gint x = gint(aEvent->x_root);
    gint y = gint(aEvent->y_root);
    GdkDisplay* display = gdk_drawable_get_display(aWindow);
    GdkWindow* winAtPt = gdk_display_get_window_at_pointer(display, &x, &y);
    if (!winAtPt)
        return PR_TRUE;
    GdkWindow* topLevelAtPt = gdk_window_get_toplevel(winAtPt);
    GdkWindow* topLevelWidget = gdk_window_get_toplevel(aWindow);
    return topLevelAtPt != topLevelWidget;
}

void
nsWindow::OnLeaveNotifyEvent(GtkWidget *aWidget, GdkEventCrossing *aEvent)
{
    // XXXldb Is this the right test for embedding cases?
    if (aEvent->subwindow != NULL)
        return;

    nsMouseEvent event(PR_TRUE, NS_MOUSE_EXIT, this, nsMouseEvent::eReal);

    event.refPoint.x = nscoord(aEvent->x);
    event.refPoint.y = nscoord(aEvent->y);

    event.time = aEvent->time;

    event.exit = is_top_level_mouse_exit(mGdkWindow, aEvent)
        ? nsMouseEvent::eTopLevel : nsMouseEvent::eChild;

    LOG(("OnLeaveNotify: %p\n", (void *)this));

    nsEventStatus status;
    DispatchEvent(&event, status);
}

#ifdef MOZ_DFB
void
nsWindow::OnMotionNotifyEvent(GtkWidget *aWidget, GdkEventMotion *aEvent)
{
    int cursorX = (int) aEvent->x_root;
    int cursorY = (int) aEvent->y_root;

    D_DEBUG_AT( ns_Window, "%s( %4d,%4d - [%d] )\n", __FUNCTION__, cursorX, cursorY, mDFBCursorCount );

    D_ASSUME( mDFBLayer != NULL );

    if (mDFBLayer)
         mDFBLayer->GetCursorPosition( mDFBLayer, &cursorX, &cursorY );

    mDFBCursorCount++;

#if D_DEBUG_ENABLED
    if (cursorX != (int) aEvent->x_root || cursorY != (int) aEvent->y_root)
         D_DEBUG_AT( ns_Window, "  -> forward to %4d,%4d\n", cursorX, cursorY );
#endif

    if (cursorX == mDFBCursorX && cursorY == mDFBCursorY) {
         D_DEBUG_AT( ns_Window, "  -> dropping %4d,%4d\n", cursorX, cursorY );

         /* drop zero motion */
         return;
    }

    mDFBCursorX = cursorX;
    mDFBCursorY = cursorY;


    // when we receive this, it must be that the gtk dragging is over,
    // it is dropped either in or out of mozilla, clear the flag
    sIsDraggingOutOf = PR_FALSE;

    nsMouseEvent event(PR_TRUE, NS_MOUSE_MOVE, this, nsMouseEvent::eReal);

    // should we move this into !synthEvent?
    gdouble pressure = 0;
    gdk_event_get_axis ((GdkEvent*)aEvent, GDK_AXIS_PRESSURE, &pressure);
    // Sometime gdk generate 0 pressure value between normal values
    // We have to ignore that and use last valid value
    if (pressure)
      mLastMotionPressure = pressure;
    event.pressure = mLastMotionPressure;

    event.refPoint = nsIntPoint(cursorX, cursorY) - WidgetToScreenOffset();

    event.isShift   = (aEvent->state & GDK_SHIFT_MASK)
        ? PR_TRUE : PR_FALSE;
    event.isControl = (aEvent->state & GDK_CONTROL_MASK)
        ? PR_TRUE : PR_FALSE;
    event.isAlt     = (aEvent->state & GDK_MOD1_MASK)
        ? PR_TRUE : PR_FALSE;

    event.time = aEvent->time;

    nsEventStatus status;
    DispatchEvent(&event, status);
}
#else
void
nsWindow::OnMotionNotifyEvent(GtkWidget *aWidget, GdkEventMotion *aEvent)
{
    // when we receive this, it must be that the gtk dragging is over,
    // it is dropped either in or out of mozilla, clear the flag
    sIsDraggingOutOf = PR_FALSE;

    // see if we can compress this event
    // XXXldb Why skip every other motion event when we have multiple,
    // but not more than that?
    PRPackedBool synthEvent = PR_FALSE;
#ifdef MOZ_X11
    XEvent xevent;

    while (XPending (GDK_WINDOW_XDISPLAY(aEvent->window))) {
        XEvent peeked;
        XPeekEvent (GDK_WINDOW_XDISPLAY(aEvent->window), &peeked);
        if (peeked.xany.window != GDK_WINDOW_XWINDOW(aEvent->window)
            || peeked.type != MotionNotify)
            break;

        synthEvent = PR_TRUE;
        XNextEvent (GDK_WINDOW_XDISPLAY(aEvent->window), &xevent);
    }

    // if plugins still keeps the focus, get it back
    if (gPluginFocusWindow && gPluginFocusWindow != this) {
        nsRefPtr<nsWindow> kungFuDeathGrip = gPluginFocusWindow;
        gPluginFocusWindow->LoseNonXEmbedPluginFocus();
    }
#endif /* MOZ_X11 */

    nsMouseEvent event(PR_TRUE, NS_MOUSE_MOVE, this, nsMouseEvent::eReal);

    gdouble pressure = 0;
    gdk_event_get_axis ((GdkEvent*)aEvent, GDK_AXIS_PRESSURE, &pressure);
    // Sometime gdk generate 0 pressure value between normal values
    // We have to ignore that and use last valid value
    if (pressure)
      mLastMotionPressure = pressure;
    event.pressure = mLastMotionPressure;

    if (synthEvent) {
#ifdef MOZ_X11
        event.refPoint.x = nscoord(xevent.xmotion.x);
        event.refPoint.y = nscoord(xevent.xmotion.y);

        event.isShift   = (xevent.xmotion.state & GDK_SHIFT_MASK)
            ? PR_TRUE : PR_FALSE;
        event.isControl = (xevent.xmotion.state & GDK_CONTROL_MASK)
            ? PR_TRUE : PR_FALSE;
        event.isAlt     = (xevent.xmotion.state & GDK_MOD1_MASK)
            ? PR_TRUE : PR_FALSE;

        event.time = xevent.xmotion.time;
#else
        event.refPoint.x = nscoord(aEvent->x);
        event.refPoint.y = nscoord(aEvent->y);

        event.isShift   = (aEvent->state & GDK_SHIFT_MASK)
            ? PR_TRUE : PR_FALSE;
        event.isControl = (aEvent->state & GDK_CONTROL_MASK)
            ? PR_TRUE : PR_FALSE;
        event.isAlt     = (aEvent->state & GDK_MOD1_MASK)
            ? PR_TRUE : PR_FALSE;

        event.time = aEvent->time;
#endif /* MOZ_X11 */
    }
    else {
        // XXX see OnScrollEvent()
        if (aEvent->window == mGdkWindow) {
            event.refPoint.x = nscoord(aEvent->x);
            event.refPoint.y = nscoord(aEvent->y);
        } else {
            nsIntPoint point(NSToIntFloor(aEvent->x_root), NSToIntFloor(aEvent->y_root));
            event.refPoint = point - WidgetToScreenOffset();
        }

        event.isShift   = (aEvent->state & GDK_SHIFT_MASK)
            ? PR_TRUE : PR_FALSE;
        event.isControl = (aEvent->state & GDK_CONTROL_MASK)
            ? PR_TRUE : PR_FALSE;
        event.isAlt     = (aEvent->state & GDK_MOD1_MASK)
            ? PR_TRUE : PR_FALSE;

        event.time = aEvent->time;
    }

    nsEventStatus status;
    DispatchEvent(&event, status);
}
#endif

void
nsWindow::InitButtonEvent(nsMouseEvent &aEvent,
                          GdkEventButton *aGdkEvent)
{
    // XXX see OnScrollEvent()
    if (aGdkEvent->window == mGdkWindow) {
        aEvent.refPoint.x = nscoord(aGdkEvent->x);
        aEvent.refPoint.y = nscoord(aGdkEvent->y);
    } else {
        nsIntPoint point(NSToIntFloor(aGdkEvent->x_root), NSToIntFloor(aGdkEvent->y_root));
        aEvent.refPoint = point - WidgetToScreenOffset();
    }

    aEvent.isShift   = (aGdkEvent->state & GDK_SHIFT_MASK) != 0;
    aEvent.isControl = (aGdkEvent->state & GDK_CONTROL_MASK) != 0;
    aEvent.isAlt     = (aGdkEvent->state & GDK_MOD1_MASK) != 0;
    aEvent.isMeta    = (aGdkEvent->state & GDK_MOD4_MASK) != 0;

    aEvent.time = aGdkEvent->time;

    switch (aGdkEvent->type) {
    case GDK_2BUTTON_PRESS:
        aEvent.clickCount = 2;
        break;
    case GDK_3BUTTON_PRESS:
        aEvent.clickCount = 3;
        break;
        // default is one click
    default:
        aEvent.clickCount = 1;
    }
}

void
nsWindow::OnButtonPressEvent(GtkWidget *aWidget, GdkEventButton *aEvent)
{
    nsEventStatus status;

    // If you double click in GDK, it will actually generate a single
    // click event before sending the double click event, and this is
    // different than the DOM spec.  GDK puts this in the queue
    // programatically, so it's safe to assume that if there's a
    // double click in the queue, it was generated so we can just drop
    // this click.
    GdkEvent *peekedEvent = gdk_event_peek();
    if (peekedEvent) {
        GdkEventType type = peekedEvent->any.type;
        gdk_event_free(peekedEvent);
        if (type == GDK_2BUTTON_PRESS || type == GDK_3BUTTON_PRESS)
            return;
    }

    // Always save the time of this event
    mLastButtonPressTime = aEvent->time;
    mLastButtonReleaseTime = 0;

    nsWindow *containerWindow = GetContainerWindow();
    if (!gFocusWindow && containerWindow) {
        containerWindow->DispatchActivateEvent();
    }

    // check to see if we should rollup
    PRBool rolledUp = check_for_rollup(aEvent->window, aEvent->x_root,
                                       aEvent->y_root, PR_FALSE);
    if (gConsumeRollupEvent && rolledUp)
            return;

    gdouble pressure = 0;
    gdk_event_get_axis ((GdkEvent*)aEvent, GDK_AXIS_PRESSURE, &pressure);
    mLastMotionPressure = pressure;

    PRUint16 domButton;
    switch (aEvent->button) {
    case 1:
        domButton = nsMouseEvent::eLeftButton;
        break;
    case 2:
        domButton = nsMouseEvent::eMiddleButton;
        break;
    case 3:
        domButton = nsMouseEvent::eRightButton;
        break;
    // These are mapped to horizontal scroll
    case 6:
    case 7:
        {
            nsMouseScrollEvent event(PR_TRUE, NS_MOUSE_SCROLL, this);
            event.pressure = mLastMotionPressure;
            event.scrollFlags = nsMouseScrollEvent::kIsHorizontal;
            event.refPoint.x = nscoord(aEvent->x);
            event.refPoint.y = nscoord(aEvent->y);
            // XXX Why is this delta value different from the scroll event?
            event.delta = (aEvent->button == 6) ? -2 : 2;

            event.isShift   = (aEvent->state & GDK_SHIFT_MASK) != 0;
            event.isControl = (aEvent->state & GDK_CONTROL_MASK) != 0;
            event.isAlt     = (aEvent->state & GDK_MOD1_MASK) != 0;
            event.isMeta    = (aEvent->state & GDK_MOD4_MASK) != 0;

            event.time = aEvent->time;

            nsEventStatus status;
            DispatchEvent(&event, status);
            return;
        }
    // Map buttons 8-9 to back/forward
    case 8:
        DispatchCommandEvent(nsWidgetAtoms::Back);
        return;
    case 9:
        DispatchCommandEvent(nsWidgetAtoms::Forward);
        return;
    default:
        return;
    }

    nsMouseEvent event(PR_TRUE, NS_MOUSE_BUTTON_DOWN, this, nsMouseEvent::eReal);
    event.button = domButton;
    InitButtonEvent(event, aEvent);
    event.pressure = mLastMotionPressure;

    DispatchEvent(&event, status);

    // right menu click on linux should also pop up a context menu
    if (domButton == nsMouseEvent::eRightButton &&
        NS_LIKELY(!mIsDestroyed)) {
        nsMouseEvent contextMenuEvent(PR_TRUE, NS_CONTEXTMENU, this,
                                      nsMouseEvent::eReal);
        InitButtonEvent(contextMenuEvent, aEvent);
        contextMenuEvent.pressure = mLastMotionPressure;
        DispatchEvent(&contextMenuEvent, status);
    }
}

void
nsWindow::OnButtonReleaseEvent(GtkWidget *aWidget, GdkEventButton *aEvent)
{
    PRUint16 domButton;
    mLastButtonReleaseTime = aEvent->time;

    switch (aEvent->button) {
    case 1:
        domButton = nsMouseEvent::eLeftButton;
        break;
    case 2:
        domButton = nsMouseEvent::eMiddleButton;
        break;
    case 3:
        domButton = nsMouseEvent::eRightButton;
        break;
    default:
        return;
    }

    nsMouseEvent event(PR_TRUE, NS_MOUSE_BUTTON_UP, this, nsMouseEvent::eReal);
    event.button = domButton;
    InitButtonEvent(event, aEvent);
    gdouble pressure = 0;
    gdk_event_get_axis ((GdkEvent*)aEvent, GDK_AXIS_PRESSURE, &pressure);
    event.pressure = pressure ? pressure : mLastMotionPressure;

    nsEventStatus status;
    DispatchEvent(&event, status);
    mLastMotionPressure = pressure;
}

void
nsWindow::OnContainerFocusInEvent(GtkWidget *aWidget, GdkEventFocus *aEvent)
{
    LOGFOCUS(("OnContainerFocusInEvent [%p]\n", (void *)this));
    // Return if someone has blocked events for this widget.
    if (mContainerBlockFocus) {
        LOGFOCUS(("Container focus is blocked [%p]\n", (void *)this));
        return;
    }

    // Unset the urgency hint, if possible
    GtkWidget* top_window = nsnull;
    GetToplevelWidget(&top_window);
    if (top_window && (GTK_WIDGET_VISIBLE(top_window)))
        SetUrgencyHint(top_window, PR_FALSE);

    gFocusWindow = this;
    DispatchActivateEvent();

    LOGFOCUS(("Events sent from focus in event [%p]\n", (void *)this));
}

void
nsWindow::OnContainerFocusOutEvent(GtkWidget *aWidget, GdkEventFocus *aEvent)
{
    LOGFOCUS(("OnContainerFocusOutEvent [%p]\n", (void *)this));

#ifdef MOZ_X11
    // plugin lose focus
    if (gPluginFocusWindow) {
        nsRefPtr<nsWindow> kungFuDeathGrip = gPluginFocusWindow;
        gPluginFocusWindow->LoseNonXEmbedPluginFocus();
    }
#endif /* MOZ_X11 */

    // Figure out if the focus widget is the child of this window.  If
    // it is, send a deactivate event for it.
    if (!gFocusWindow)
        return;

    GdkWindow *tmpWindow;
    tmpWindow = (GdkWindow *)gFocusWindow->GetNativeData(NS_NATIVE_WINDOW);
    nsWindow *tmpnsWindow = get_window_for_gdk_window(tmpWindow);

    while (tmpWindow && tmpnsWindow) {
        // found it!
        if (tmpnsWindow == this)
            goto foundit;

        tmpWindow = gdk_window_get_parent(tmpWindow);
        if (!tmpWindow)
            break;

        tmpnsWindow = get_owning_window_for_gdk_window(tmpWindow);
    }

    LOGFOCUS(("The focus widget was not a child of this window [%p]\n",
              (void *)this));

    return;

 foundit:

    nsRefPtr<nsWindow> kungFuDeathGrip = gFocusWindow;
#ifdef USE_XIM
    gFocusWindow->IMELoseFocus();
#endif

    // We only dispatch a deactivate event if we are a toplevel
    // window, otherwise the embedding code takes care of it.
    if (NS_LIKELY(!gFocusWindow->mIsDestroyed))
        DispatchDeactivateEvent();

    gFocusWindow = nsnull;

    LOGFOCUS(("Done with container focus out [%p]\n", (void *)this));
}

PRBool
nsWindow::DispatchCommandEvent(nsIAtom* aCommand)
{
    nsEventStatus status;
    nsCommandEvent event(PR_TRUE, nsWidgetAtoms::onAppCommand, aCommand, this);
    DispatchEvent(&event, status);
    return TRUE;
}

static PRUint32
GetCharCodeFor(const GdkEventKey *aEvent, guint aShiftState,
               gint aGroup)
{
    guint keyval;
    if (gdk_keymap_translate_keyboard_state(NULL, aEvent->hardware_keycode,
                                            GdkModifierType(aShiftState),
                                            aGroup,
                                            &keyval, NULL, NULL, NULL)) {
        GdkEventKey tmpEvent = *aEvent;
        tmpEvent.state = guint(aShiftState);
        tmpEvent.keyval = keyval;
        tmpEvent.group = aGroup;
        return nsConvertCharCodeToUnicode(&tmpEvent);
    }
    return 0;
}

static gint
GetKeyLevel(GdkEventKey *aEvent)
{
    gint level;
    if (!gdk_keymap_translate_keyboard_state(NULL,
                                             aEvent->hardware_keycode,
                                             GdkModifierType(aEvent->state),
                                             aEvent->group,
                                             NULL, NULL, &level, NULL))
        return -1;
    return level;
}

static PRBool
IsBasicLatinLetterOrNumeral(PRUint32 aChar)
{
    return (aChar >= 'a' && aChar <= 'z') ||
           (aChar >= 'A' && aChar <= 'Z') ||
           (aChar >= '0' && aChar <= '9');
}

gboolean
nsWindow::OnKeyPressEvent(GtkWidget *aWidget, GdkEventKey *aEvent)
{
    LOGFOCUS(("OnKeyPressEvent [%p]\n", (void *)this));

#ifdef USE_XIM
    // if we are in the middle of composing text, XIM gets to see it
    // before mozilla does.
   LOGIM(("key press [%p]: composing %d val %d\n",
           (void *)this, IMEComposingWindow() != nsnull, aEvent->keyval));
   if (IMEFilterEvent(aEvent))
       return TRUE;
   LOGIM(("sending as regular key press event\n"));
#endif

    nsEventStatus status;

    // work around for annoying things.
    if (aEvent->keyval == GDK_Tab && aEvent->state & GDK_CONTROL_MASK &&
        aEvent->state & GDK_MOD1_MASK) {
        return TRUE;
    }

    nsCOMPtr<nsIWidget> kungFuDeathGrip = this;

    // If the key down flag isn't set then set it so we don't send
    // another key down event on the next key press -- DOM events are
    // key down, key press and key up.  X only has key press and key
    // release.  gtk2 already filters the extra key release events for
    // us.

    PRBool isKeyDownCancelled = PR_FALSE;

    PRUint32 domVirtualKeyCode = GdkKeyCodeToDOMKeyCode(aEvent->keyval);

    if (!IsKeyDown(domVirtualKeyCode)) {
        SetKeyDownFlag(domVirtualKeyCode);

        // send the key down event
        nsKeyEvent downEvent(PR_TRUE, NS_KEY_DOWN, this);
        InitKeyEvent(downEvent, aEvent);
        DispatchEvent(&downEvent, status);
        if (NS_UNLIKELY(mIsDestroyed))
            return PR_TRUE;
        isKeyDownCancelled = (status == nsEventStatus_eConsumeNoDefault);
    }

    // Don't pass modifiers as NS_KEY_PRESS events.
    // TODO: Instead of selectively excluding some keys from NS_KEY_PRESS events,
    //       we should instead selectively include (as per MSDN spec; no official
    //       spec covers KeyPress events).
    if (aEvent->keyval == GDK_Shift_L
        || aEvent->keyval == GDK_Shift_R
        || aEvent->keyval == GDK_Control_L
        || aEvent->keyval == GDK_Control_R
        || aEvent->keyval == GDK_Alt_L
        || aEvent->keyval == GDK_Alt_R
        || aEvent->keyval == GDK_Meta_L
        || aEvent->keyval == GDK_Meta_R) {
        return TRUE;
    }

#ifdef MOZ_X11
    // Look for specialized app-command keys
    switch (aEvent->keyval) {
        case XF86XK_Back:
            return DispatchCommandEvent(nsWidgetAtoms::Back);
        case XF86XK_Forward:
            return DispatchCommandEvent(nsWidgetAtoms::Forward);
        case XF86XK_Refresh:
            return DispatchCommandEvent(nsWidgetAtoms::Reload);
        case XF86XK_Stop:
            return DispatchCommandEvent(nsWidgetAtoms::Stop);
        case XF86XK_Search:
            return DispatchCommandEvent(nsWidgetAtoms::Search);
        case XF86XK_Favorites:
            return DispatchCommandEvent(nsWidgetAtoms::Bookmarks);
        case XF86XK_HomePage:
            return DispatchCommandEvent(nsWidgetAtoms::Home);
    }
#endif /* MOZ_X11 */

    nsKeyEvent event(PR_TRUE, NS_KEY_PRESS, this);
    InitKeyEvent(event, aEvent);
    if (isKeyDownCancelled) {
      // If prevent default set for onkeydown, do the same for onkeypress
      event.flags |= NS_EVENT_FLAG_NO_DEFAULT;
    }
    event.charCode = nsConvertCharCodeToUnicode(aEvent);
    if (event.charCode) {
        event.keyCode = 0;
        gint level = GetKeyLevel(aEvent);
        if ((event.isControl || event.isAlt || event.isMeta) &&
            (level == 0 || level == 1)) {
            guint baseState =
                aEvent->state & ~(GDK_SHIFT_MASK | GDK_CONTROL_MASK |
                                  GDK_MOD1_MASK | GDK_MOD4_MASK);
            // We shold send both shifted char and unshifted char,
            // all keyboard layout users can use all keys.
            // Don't change event.charCode. On some keyboard layouts,
            // ctrl/alt/meta keys are used for inputting some characters.
            nsAlternativeCharCode altCharCodes(0, 0);
            // unshifted charcode of current keyboard layout.
            altCharCodes.mUnshiftedCharCode =
                GetCharCodeFor(aEvent, baseState, aEvent->group);
            PRBool isLatin = (altCharCodes.mUnshiftedCharCode <= 0xFF);
            // shifted charcode of current keyboard layout.
            altCharCodes.mShiftedCharCode =
                GetCharCodeFor(aEvent, baseState | GDK_SHIFT_MASK,
                               aEvent->group);
            isLatin = isLatin && (altCharCodes.mShiftedCharCode <= 0xFF);
            if (altCharCodes.mUnshiftedCharCode ||
                altCharCodes.mShiftedCharCode) {
                event.alternativeCharCodes.AppendElement(altCharCodes);
            }

            if (!isLatin) {
                // Next, find latin inputtable keyboard layout.
                GdkKeymapKey *keys;
                gint count;
                gint minGroup = -1;
                if (gdk_keymap_get_entries_for_keyval(NULL, GDK_a,
                                                      &keys, &count)) {
                    // find the minimum number group for latin inputtable layout
                    for (gint i = 0; i < count && minGroup != 0; ++i) {
                        if (keys[i].level != 0 && keys[i].level != 1)
                            continue;
                        if (minGroup >= 0 && keys[i].group > minGroup)
                            continue;
                        minGroup = keys[i].group;
                    }
                    g_free(keys);
                }
                if (minGroup >= 0) {
                    PRUint32 unmodifiedCh =
                               event.isShift ? altCharCodes.mShiftedCharCode :
                                               altCharCodes.mUnshiftedCharCode;
                    // unshifted charcode of found keyboard layout.
                    PRUint32 ch =
                        GetCharCodeFor(aEvent, baseState, minGroup);
                    altCharCodes.mUnshiftedCharCode =
                        IsBasicLatinLetterOrNumeral(ch) ? ch : 0;
                    // shifted charcode of found keyboard layout.
                    ch = GetCharCodeFor(aEvent, baseState | GDK_SHIFT_MASK,
                                        minGroup);
                    altCharCodes.mShiftedCharCode =
                        IsBasicLatinLetterOrNumeral(ch) ? ch : 0;
                    if (altCharCodes.mUnshiftedCharCode ||
                        altCharCodes.mShiftedCharCode) {
                        event.alternativeCharCodes.AppendElement(altCharCodes);
                    }
                    // If the charCode is not Latin, and the level is 0 or 1,
                    // we should replace the charCode to Latin char if Alt and
                    // Meta keys are not pressed. (Alt should be sent the
                    // localized char for accesskey like handling of Web
                    // Applications.)
                    ch = event.isShift ? altCharCodes.mShiftedCharCode :
                                         altCharCodes.mUnshiftedCharCode;
                    if (ch && !(event.isAlt || event.isMeta) &&
                        event.charCode == unmodifiedCh) {
                        event.charCode = ch;
                    }
                }
            }
        }
    }

    // before we dispatch a key, check if it's the context menu key.
    // If so, send a context menu key event instead.
    if (is_context_menu_key(event)) {
        nsMouseEvent contextMenuEvent(PR_TRUE, NS_CONTEXTMENU, this,
                                      nsMouseEvent::eReal,
                                      nsMouseEvent::eContextMenuKey);
        key_event_to_context_menu_event(contextMenuEvent, aEvent);
        DispatchEvent(&contextMenuEvent, status);
    }
    else {
        // send the key press event
        DispatchEvent(&event, status);
    }

    // If the event was consumed, return.
    LOGIM(("status %d\n", status));
    if (status == nsEventStatus_eConsumeNoDefault) {
        LOGIM(("key press consumed\n"));
        return TRUE;
    }

    return FALSE;
}

gboolean
nsWindow::OnKeyReleaseEvent(GtkWidget *aWidget, GdkEventKey *aEvent)
{
    LOGFOCUS(("OnKeyReleaseEvent [%p]\n", (void *)this));

#ifdef USE_XIM
    if (IMEFilterEvent(aEvent))
        return TRUE;
#endif

    // send the key event as a key up event
    nsKeyEvent event(PR_TRUE, NS_KEY_UP, this);
    InitKeyEvent(event, aEvent);

    // unset the key down flag
    ClearKeyDownFlag(event.keyCode);

    nsEventStatus status;
    DispatchEvent(&event, status);

    // If the event was consumed, return.
    if (status == nsEventStatus_eConsumeNoDefault) {
        LOGIM(("key release consumed\n"));
        return TRUE;
    }

    return FALSE;
}

void
nsWindow::OnScrollEvent(GtkWidget *aWidget, GdkEventScroll *aEvent)
{
    // check to see if we should rollup
    PRBool rolledUp =  check_for_rollup(aEvent->window, aEvent->x_root,
                                        aEvent->y_root, PR_TRUE);
    if (gConsumeRollupEvent && rolledUp)
        return;

    nsMouseScrollEvent event(PR_TRUE, NS_MOUSE_SCROLL, this);
    switch (aEvent->direction) {
    case GDK_SCROLL_UP:
        event.scrollFlags = nsMouseScrollEvent::kIsVertical;
        event.delta = -3;
        break;
    case GDK_SCROLL_DOWN:
        event.scrollFlags = nsMouseScrollEvent::kIsVertical;
        event.delta = 3;
        break;
    case GDK_SCROLL_LEFT:
        event.scrollFlags = nsMouseScrollEvent::kIsHorizontal;
        event.delta = -1;
        break;
    case GDK_SCROLL_RIGHT:
        event.scrollFlags = nsMouseScrollEvent::kIsHorizontal;
        event.delta = 1;
        break;
    }

    if (aEvent->window == mGdkWindow) {
        // we are the window that the event happened on so no need for expensive WidgetToScreenOffset
        event.refPoint.x = nscoord(aEvent->x);
        event.refPoint.y = nscoord(aEvent->y);
    } else {
        // XXX we're never quite sure which GdkWindow the event came from due to our custom bubbling
        // in scroll_event_cb(), so use ScreenToWidget to translate the screen root coordinates into
        // coordinates relative to this widget.
        nsIntPoint point(NSToIntFloor(aEvent->x_root), NSToIntFloor(aEvent->y_root));
        event.refPoint = point - WidgetToScreenOffset();
    }

    event.isShift   = (aEvent->state & GDK_SHIFT_MASK) != 0;
    event.isControl = (aEvent->state & GDK_CONTROL_MASK) != 0;
    event.isAlt     = (aEvent->state & GDK_MOD1_MASK) != 0;
    event.isMeta    = (aEvent->state & GDK_MOD4_MASK) != 0;

    event.time = aEvent->time;

    nsEventStatus status;
    DispatchEvent(&event, status);
}

void
nsWindow::OnVisibilityNotifyEvent(GtkWidget *aWidget,
                                  GdkEventVisibility *aEvent)
{
    LOGDRAW(("Visibility event %i on [%p] %p\n",
             aEvent->state, this, aEvent->window));

    if (!mGdkWindow)
        return;

    switch (aEvent->state) {
    case GDK_VISIBILITY_UNOBSCURED:
    case GDK_VISIBILITY_PARTIAL:
        if (mIsFullyObscured && mHasMappedToplevel) {
            // GDK_EXPOSE events have been ignored, so make sure GDK
            // doesn't think that the window has already been painted.
            gdk_window_invalidate_rect(mGdkWindow, NULL, FALSE);
        }

        mIsFullyObscured = PR_FALSE;

#ifdef MOZ_PLATFORM_MAEMO
#ifdef USE_XIM
        // In Hildon/Maemo, a browser window will get into 'patially visible' state wheneven an
        // autocomplete feature is dropped down (from urlbar or from an entry form completion),
        // and there are no much further ways for that to happen in the plaftorm. In such cases, if hildon
        // virtual keyboard is up, we can not grab focus to any dropdown list. Reason: nsWindow::EnsureGrabs()
        // calls gdk_pointer_grab() which grabs the pointer (usually a mouse) so that all events are passed
        // to this it until the pointer is ungrabbed.
        if(!gIMEVirtualKeyboardOpened)
#endif // USE_XIM
#endif // MOZ_PLATFORM_MAEMO
        // if we have to retry the grab, retry it.
        EnsureGrabs();
        break;
    default: // includes GDK_VISIBILITY_FULLY_OBSCURED
        mIsFullyObscured = PR_TRUE;
        break;
    }
}

void
nsWindow::OnWindowStateEvent(GtkWidget *aWidget, GdkEventWindowState *aEvent)
{
    LOG(("nsWindow::OnWindowStateEvent [%p] changed %d new_window_state %d\n",
         (void *)this, aEvent->changed_mask, aEvent->new_window_state));

    if (IS_MOZ_CONTAINER(aWidget)) {
        // This event is notifying the container widget of changes to the
        // toplevel window.  Just detect changes affecting whether windows are
        // viewable.
        //
        // (A visibility notify event is sent to each window that becomes
        // viewable when the toplevel is mapped, but we can't rely on that for
        // setting mHasMappedToplevel because these toplevel window state
        // events are asynchronous.  The windows in the hierarchy now may not
        // be the same windows as when the toplevel was mapped, so they may
        // not get VisibilityNotify events.)
        PRBool mapped =
            !(aEvent->new_window_state &
              (GDK_WINDOW_STATE_ICONIFIED|GDK_WINDOW_STATE_WITHDRAWN));
        if (mHasMappedToplevel != mapped) {
            SetHasMappedToplevel(mapped);
        }
        return;
    }
    // else the widget is a shell widget.

    nsSizeModeEvent event(PR_TRUE, NS_SIZEMODE, this);

    // We don't care about anything but changes in the maximized/icon
    // states
    if ((aEvent->changed_mask
         & (GDK_WINDOW_STATE_ICONIFIED|GDK_WINDOW_STATE_MAXIMIZED)) == 0) {
        return;
    }

    if (aEvent->new_window_state & GDK_WINDOW_STATE_ICONIFIED) {
        LOG(("\tIconified\n"));
        event.mSizeMode = nsSizeMode_Minimized;
        mSizeState = nsSizeMode_Minimized;
    }
    else if (aEvent->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) {
        LOG(("\tMaximized\n"));
        event.mSizeMode = nsSizeMode_Maximized;
        mSizeState = nsSizeMode_Maximized;
    }
    else if (aEvent->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) {
        LOG(("\tFullscreen\n"));
        event.mSizeMode = nsSizeMode_Fullscreen;
        mSizeState = nsSizeMode_Fullscreen;
    }
    else {
        LOG(("\tNormal\n"));
        event.mSizeMode = nsSizeMode_Normal;
        mSizeState = nsSizeMode_Normal;
    }

    nsEventStatus status;
    DispatchEvent(&event, status);
}

void
nsWindow::ThemeChanged()
{
    nsGUIEvent event(PR_TRUE, NS_THEMECHANGED, this);
    nsEventStatus status = nsEventStatus_eIgnore;
    DispatchEvent(&event, status);

    if (!mGdkWindow || NS_UNLIKELY(mIsDestroyed))
        return;

    // Dispatch NS_THEMECHANGED to all child windows
    GList *children =
        gdk_window_peek_children(mGdkWindow);
    while (children) {
        GdkWindow *gdkWin = GDK_WINDOW(children->data);

        nsWindow *win = (nsWindow*) g_object_get_data(G_OBJECT(gdkWin),
                                                      "nsWindow");

        if (win && win != this) { // guard against infinite recursion
            nsRefPtr<nsWindow> kungFuDeathGrip = win;
            win->ThemeChanged();
        }

        children = children->next;
    }
}

void
nsWindow::CheckNeedDragLeaveEnter(nsWindow* aInnerMostWidget,
                                  nsIDragService* aDragService,
                                  GdkDragContext *aDragContext,
                                  nscoord aX, nscoord aY)
{
    // check to see if there was a drag motion window already in place
    if (mLastDragMotionWindow) {
        // same as the last window so no need for dragenter and dragleave events
        if (mLastDragMotionWindow == aInnerMostWidget) {
            UpdateDragStatus(aDragContext, aDragService);
            return;
        }

        // send a dragleave event to the last window that got a motion event
        nsRefPtr<nsWindow> kungFuDeathGrip = mLastDragMotionWindow;
        mLastDragMotionWindow->OnDragLeave();
    }

    // Make sure that the drag service knows we're now dragging
    aDragService->StartDragSession();

    // update our drag status and send a dragenter event to the window
    UpdateDragStatus(aDragContext, aDragService);
    aInnerMostWidget->OnDragEnter(aX, aY);

    // set the last window to the innerMostWidget
    mLastDragMotionWindow = aInnerMostWidget;
}

gboolean
nsWindow::OnDragMotionEvent(GtkWidget *aWidget,
                            GdkDragContext *aDragContext,
                            gint aX,
                            gint aY,
                            guint aTime,
                            gpointer aData)
{
    LOGDRAG(("nsWindow::OnDragMotionSignal\n"));

    if (mLastButtonReleaseTime) {
      // The drag ended before it was even setup to handle the end of the drag
      // So, we fake the button getting released again to release the drag
      GtkWidget *widget = gtk_grab_get_current();
      GdkEvent event;
      gboolean retval;
      memset(&event, 0, sizeof(event));
      event.type = GDK_BUTTON_RELEASE;
      event.button.time = mLastButtonReleaseTime;
      event.button.button = 1;
      mLastButtonReleaseTime = 0;
      if (widget) {
        g_signal_emit_by_name(widget, "button_release_event", &event, &retval);
        return TRUE;
      }
    }

    sIsDraggingOutOf = PR_FALSE;

    // Reset out drag motion timer
    ResetDragMotionTimer(aWidget, aDragContext, aX, aY, aTime);

    // get our drag context
    nsCOMPtr<nsIDragService> dragService = do_GetService(kCDragServiceCID);
    nsCOMPtr<nsIDragSessionGTK> dragSessionGTK = do_QueryInterface(dragService);

    // first, figure out which internal widget this drag motion actually
    // happened on
    nscoord retx = 0;
    nscoord rety = 0;

    GdkWindow *innerWindow = get_inner_gdk_window(aWidget->window, aX, aY,
                                                  &retx, &rety);
    nsRefPtr<nsWindow> innerMostWidget = get_window_for_gdk_window(innerWindow);

    if (!innerMostWidget)
        innerMostWidget = this;

    // update the drag context
    dragSessionGTK->TargetSetLastContext(aWidget, aDragContext, aTime);

    // clear any drag leave timer that might be pending so that it
    // doesn't get processed when we actually go out to get data.
    if (mDragLeaveTimer) {
        mDragLeaveTimer->Cancel();
        mDragLeaveTimer = nsnull;
    }

    CheckNeedDragLeaveEnter(innerMostWidget, dragService, aDragContext, retx, rety);

    // notify the drag service that we are starting a drag motion.
    dragSessionGTK->TargetStartDragMotion();

    dragService->FireDragEventAtSource(NS_DRAGDROP_DRAG);

    nsDragEvent event(PR_TRUE, NS_DRAGDROP_OVER, innerMostWidget);

    InitDragEvent(event);

    event.refPoint.x = retx;
    event.refPoint.y = rety;
    event.time = aTime;

    nsEventStatus status;
    innerMostWidget->DispatchEvent(&event, status);

    // we're done with the drag motion event.  notify the drag service.
    dragSessionGTK->TargetEndDragMotion(aWidget, aDragContext, aTime);

    // and unset our context
    dragSessionGTK->TargetSetLastContext(0, 0, 0);

    return TRUE;
}

void
nsWindow::OnDragLeaveEvent(GtkWidget *aWidget,
                           GdkDragContext *aDragContext,
                           guint aTime,
                           gpointer aData)
{
    // XXX Do we want to pass this on only if the event's subwindow is null?

    LOGDRAG(("nsWindow::OnDragLeaveSignal(%p)\n", (void*)this));

    sIsDraggingOutOf = PR_TRUE;

    // make sure to unset any drag motion timers here.
    ResetDragMotionTimer(0, 0, 0, 0, 0);

    if (mDragLeaveTimer) {
        return;
    }

    // create a fast timer - we're delaying the drag leave until the
    // next mainloop in hopes that we might be able to get a drag drop
    // signal
    mDragLeaveTimer = do_CreateInstance("@mozilla.org/timer;1");
    NS_ASSERTION(mDragLeaveTimer, "Failed to create drag leave timer!");
    // fire this baby asafp, but not too quickly... see bug 216800 ;-)
    mDragLeaveTimer->InitWithFuncCallback(DragLeaveTimerCallback,
                                          (void *)this,
                                          20, nsITimer::TYPE_ONE_SHOT);
}

gboolean
nsWindow::OnDragDropEvent(GtkWidget *aWidget,
                          GdkDragContext *aDragContext,
                          gint aX,
                          gint aY,
                          guint aTime,
                          gpointer *aData)

{
    LOGDRAG(("nsWindow::OnDragDropSignal\n"));

    // get our drag context
    nsCOMPtr<nsIDragService> dragService = do_GetService(kCDragServiceCID);
    nsCOMPtr<nsIDragSessionGTK> dragSessionGTK = do_QueryInterface(dragService);

    nscoord retx = 0;
    nscoord rety = 0;

    GdkWindow *innerWindow = get_inner_gdk_window(aWidget->window, aX, aY,
                                                  &retx, &rety);
    nsRefPtr<nsWindow> innerMostWidget = get_window_for_gdk_window(innerWindow);

    if (!innerMostWidget)
        innerMostWidget = this;

    // set this now before any of the drag enter or leave events happen
    dragSessionGTK->TargetSetLastContext(aWidget, aDragContext, aTime);

    // clear any drag leave timer that might be pending so that it
    // doesn't get processed when we actually go out to get data.
    if (mDragLeaveTimer) {
        mDragLeaveTimer->Cancel();
        mDragLeaveTimer = nsnull;
    }

    CheckNeedDragLeaveEnter(innerMostWidget, dragService, aDragContext, retx, rety);

    // What we do here is dispatch a new drag motion event to
    // re-validate the drag target and then we do the drop.  The events
    // look the same except for the type.

    nsDragEvent event(PR_TRUE, NS_DRAGDROP_OVER, innerMostWidget);

    InitDragEvent(event);

    event.refPoint.x = retx;
    event.refPoint.y = rety;
    event.time = aTime;

    nsEventStatus status;
    innerMostWidget->DispatchEvent(&event, status);

    // We need to check innerMostWidget->mIsDestroyed here because the nsRefPtr
    // only protects innerMostWidget from being deleted, it does NOT protect
    // against nsView::~nsView() calling Destroy() on it, bug 378670.
    if (!innerMostWidget->mIsDestroyed) {
        nsDragEvent event(PR_TRUE, NS_DRAGDROP_DROP, innerMostWidget);
        event.refPoint.x = retx;
        event.refPoint.y = rety;

        nsEventStatus status = nsEventStatus_eIgnore;
        innerMostWidget->DispatchEvent(&event, status);
    }

    // before we unset the context we need to do a drop_finish

    gdk_drop_finish(aDragContext, TRUE, aTime);

    // after a drop takes place we need to make sure that the drag
    // service doesn't think that it still has a context.  if the other
    // way ( besides the drop ) to end a drag event is during the leave
    // event and and that case is handled in that handler.
    dragSessionGTK->TargetSetLastContext(0, 0, 0);

    // clear the mLastDragMotion window
    mLastDragMotionWindow = 0;

    // Make sure to end the drag session. If this drag started in a
    // different app, we won't get a drag_end signal to end it from.
    gint x, y;
    GdkDisplay* display = gdk_display_get_default();
    if (display) {
      // get the current cursor position
      gdk_display_get_pointer(display, NULL, &x, &y, NULL);
      ((nsDragService *)dragService.get())->SetDragEndPoint(nsIntPoint(x, y));
    }
    dragService->EndDragSession(PR_TRUE);

    return TRUE;
}

void
nsWindow::OnDragDataReceivedEvent(GtkWidget *aWidget,
                                  GdkDragContext *aDragContext,
                                  gint aX,
                                  gint aY,
                                  GtkSelectionData  *aSelectionData,
                                  guint aInfo,
                                  guint aTime,
                                  gpointer aData)
{
    LOGDRAG(("nsWindow::OnDragDataReceived(%p)\n", (void*)this));

    // get our drag context
    nsCOMPtr<nsIDragService> dragService = do_GetService(kCDragServiceCID);
    nsCOMPtr<nsIDragSessionGTK> dragSessionGTK = do_QueryInterface(dragService);

    dragSessionGTK->TargetDataReceived(aWidget, aDragContext, aX, aY,
                                       aSelectionData, aInfo, aTime);
}

void
nsWindow::OnDragLeave(void)
{
    LOGDRAG(("nsWindow::OnDragLeave(%p)\n", (void*)this));

    nsDragEvent event(PR_TRUE, NS_DRAGDROP_EXIT, this);

    nsEventStatus status;
    DispatchEvent(&event, status);

    nsCOMPtr<nsIDragService> dragService = do_GetService(kCDragServiceCID);

    if (dragService) {
        nsCOMPtr<nsIDragSession> currentDragSession;
        dragService->GetCurrentSession(getter_AddRefs(currentDragSession));

        if (currentDragSession) {
            nsCOMPtr<nsIDOMNode> sourceNode;
            currentDragSession->GetSourceNode(getter_AddRefs(sourceNode));

            if (!sourceNode) {
                // We're leaving a window while doing a drag that was
                // initiated in a different app. End the drag session,
                // since we're done with it for now (until the user
                // drags back into mozilla).
                dragService->EndDragSession(PR_FALSE);
            }
        }
    }
}

void
nsWindow::OnDragEnter(nscoord aX, nscoord aY)
{
    // XXX Do we want to pass this on only if the event's subwindow is null?

    LOGDRAG(("nsWindow::OnDragEnter(%p)\n", (void*)this));

    nsDragEvent event(PR_TRUE, NS_DRAGDROP_ENTER, this);

    event.refPoint.x = aX;
    event.refPoint.y = aY;

    nsEventStatus status;
    DispatchEvent(&event, status);
}

static void
GetBrandName(nsXPIDLString& brandName)
{
    nsCOMPtr<nsIStringBundleService> bundleService =
        do_GetService(NS_STRINGBUNDLE_CONTRACTID);

    nsCOMPtr<nsIStringBundle> bundle;
    if (bundleService)
        bundleService->CreateBundle(
            "chrome://branding/locale/brand.properties",
            getter_AddRefs(bundle));

    if (bundle)
        bundle->GetStringFromName(
            NS_LITERAL_STRING("brandShortName").get(),
            getter_Copies(brandName));

    if (brandName.IsEmpty())
        brandName.Assign(NS_LITERAL_STRING("Mozilla"));
}

static GdkWindow *
CreateGdkWindow(GdkWindow *parent, GtkWidget *widget)
{
    GdkWindowAttr attributes;
    gint          attributes_mask = GDK_WA_VISUAL | GDK_WA_COLORMAP;

    attributes.event_mask = (GDK_EXPOSURE_MASK | GDK_STRUCTURE_MASK |
                             GDK_VISIBILITY_NOTIFY_MASK |
                             GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK |
                             GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
#ifdef HAVE_GTK_MOTION_HINTS
                             GDK_POINTER_MOTION_HINT_MASK |
#endif
                             GDK_POINTER_MOTION_MASK);

    attributes.width = 1;
    attributes.height = 1;
    attributes.wclass = GDK_INPUT_OUTPUT;
    attributes.visual = gtk_widget_get_visual(widget);
    attributes.colormap = gtk_widget_get_colormap(widget);
    attributes.window_type = GDK_WINDOW_CHILD;

    GdkWindow *window = gdk_window_new(parent, &attributes, attributes_mask);
    gdk_window_set_user_data(window, widget);

    /* set the default pixmap to None so that you don't end up with the
       gtk default which is BlackPixel. */
    gdk_window_set_back_pixmap(window, NULL, FALSE);

    return window;
}

nsresult
nsWindow::Create(nsIWidget        *aParent,
                 nsNativeWidget    aNativeParent,
                 const nsIntRect  &aRect,
                 EVENT_CALLBACK    aHandleEventFunction,
                 nsIDeviceContext *aContext,
                 nsIAppShell      *aAppShell,
                 nsIToolkit       *aToolkit,
                 nsWidgetInitData *aInitData)
{
    // only set the base parent if we're going to be a dialog or a
    // toplevel
    nsIWidget *baseParent = aInitData &&
        (aInitData->mWindowType == eWindowType_dialog ||
         aInitData->mWindowType == eWindowType_toplevel ||
         aInitData->mWindowType == eWindowType_invisible) ?
        nsnull : aParent;

    NS_ASSERTION(!mWindowGroup, "already have window group (leaking it)");

    // initialize all the common bits of this class
    BaseCreate(baseParent, aRect, aHandleEventFunction, aContext,
               aAppShell, aToolkit, aInitData);

    // Do we need to listen for resizes?
    PRBool listenForResizes = PR_FALSE;;
    if (aNativeParent || (aInitData && aInitData->mListenForResizes))
        listenForResizes = PR_TRUE;

    // and do our common creation
    CommonCreate(aParent, listenForResizes);

    // save our bounds
    mBounds = aRect;
    if (mWindowType != eWindowType_child &&
        mWindowType != eWindowType_plugin) {
        // The window manager might place us. Indicate that if we're
        // shown, we want to go through
        // nsWindow::NativeResize(x,y,w,h) to maybe set our own
        // position.
        mNeedsMove = PR_TRUE;
    }

    // figure out our parent window
    GtkWidget      *parentMozContainer = nsnull;
    GtkContainer   *parentGtkContainer = nsnull;
    GdkWindow      *parentGdkWindow = nsnull;
    GtkWindow      *topLevelParent = nsnull;

    if (aParent)
        parentGdkWindow = GDK_WINDOW(aParent->GetNativeData(NS_NATIVE_WINDOW));
    else if (aNativeParent && GDK_IS_WINDOW(aNativeParent))
        parentGdkWindow = GDK_WINDOW(aNativeParent);
    else if (aNativeParent && GTK_IS_CONTAINER(aNativeParent))
        parentGtkContainer = GTK_CONTAINER(aNativeParent);

    if (parentGdkWindow) {
        // get the widget for the window - it should be a moz container
        parentMozContainer = get_gtk_widget_for_gdk_window(parentGdkWindow);

        if (!IS_MOZ_CONTAINER(parentMozContainer))
            return NS_ERROR_FAILURE;

        // get the toplevel window just in case someone needs to use it
        // for setting transients or whatever.
        topLevelParent =
            GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(parentMozContainer)));
    }

    // ok, create our windows
    switch (mWindowType) {
    case eWindowType_dialog:
    case eWindowType_popup:
    case eWindowType_toplevel:
    case eWindowType_invisible: {
        mIsTopLevel = PR_TRUE;

        nsXPIDLString brandName;
        GetBrandName(brandName);
        NS_ConvertUTF16toUTF8 cBrand(brandName);

        if (mWindowType == eWindowType_dialog) {
            mShell = gtk_window_new(GTK_WINDOW_TOPLEVEL);
            SetDefaultIcon();
            gtk_window_set_wmclass(GTK_WINDOW(mShell), "Dialog", cBrand.get());
            gtk_window_set_type_hint(GTK_WINDOW(mShell),
                                     GDK_WINDOW_TYPE_HINT_DIALOG);
            gtk_window_set_transient_for(GTK_WINDOW(mShell),
                                         topLevelParent);
            mTransientParent = topLevelParent;
            // add ourselves to the parent window's window group
            if (!topLevelParent) {
                gtk_widget_realize(mShell);
                GdkWindow* dialoglead = mShell->window;
                gdk_window_set_group(dialoglead, dialoglead);
            }
            if (parentGdkWindow) {
                nsWindow *parentnsWindow =
                    get_window_for_gdk_window(parentGdkWindow);
                NS_ASSERTION(parentnsWindow,
                             "no nsWindow for parentGdkWindow!");
                if (parentnsWindow && parentnsWindow->mWindowGroup) {
                    gtk_window_group_add_window(parentnsWindow->mWindowGroup,
                                                GTK_WINDOW(mShell));
                    // store this in case any children are created
                    mWindowGroup = parentnsWindow->mWindowGroup;
                    g_object_ref(G_OBJECT(mWindowGroup));
                    LOG(("adding window %p to group %p\n",
                         (void *)mShell, (void *)mWindowGroup));
                }
            }
        }
        else if (mWindowType == eWindowType_popup) {
            // treat popups with a parent as top level windows
            if (mParent) {
                mShell = gtk_window_new(GTK_WINDOW_TOPLEVEL);
                gtk_window_set_wmclass(GTK_WINDOW(mShell), "Toplevel", cBrand.get());
                gtk_window_set_decorated(GTK_WINDOW(mShell), FALSE);
            }
            else {
                mShell = gtk_window_new(GTK_WINDOW_POPUP);
                gtk_window_set_wmclass(GTK_WINDOW(mShell), "Popup", cBrand.get());
            }

            GdkWindowTypeHint gtkTypeHint;
            switch (aInitData->mPopupHint) {
                case ePopupTypeMenu:
                    gtkTypeHint = GDK_WINDOW_TYPE_HINT_POPUP_MENU;
                    break;
                case ePopupTypeTooltip:
                    gtkTypeHint = GDK_WINDOW_TYPE_HINT_TOOLTIP;
                    break;
                default:
                    gtkTypeHint = GDK_WINDOW_TYPE_HINT_UTILITY;
                    break;
            }
            gtk_window_set_type_hint(GTK_WINDOW(mShell), gtkTypeHint);

            if (topLevelParent) {
                gtk_window_set_transient_for(GTK_WINDOW(mShell),
                                            topLevelParent);
                mTransientParent = topLevelParent;

                if (topLevelParent->group) {
                    gtk_window_group_add_window(topLevelParent->group,
                                            GTK_WINDOW(mShell));
                    mWindowGroup = topLevelParent->group;
                    g_object_ref(G_OBJECT(mWindowGroup));
                }
            }
        }
        else { // must be eWindowType_toplevel
            mShell = gtk_window_new(GTK_WINDOW_TOPLEVEL);
            SetDefaultIcon();
            gtk_window_set_wmclass(GTK_WINDOW(mShell), "Toplevel", cBrand.get());

            // each toplevel window gets its own window group
            mWindowGroup = gtk_window_group_new();

            // and add ourselves to the window group
            LOG(("adding window %p to new group %p\n",
                 (void *)mShell, (void *)mWindowGroup));
            gtk_window_group_add_window(mWindowGroup, GTK_WINDOW(mShell));
        }

        // create our container
        GtkWidget *container = moz_container_new();
        mContainer = MOZ_CONTAINER(container);
        gtk_container_add(GTK_CONTAINER(mShell), container);
        gtk_widget_realize(container);

        // Don't let GTK mess with the shapes of our GdkWindows
        GTK_PRIVATE_SET_FLAG(container, GTK_HAS_SHAPE_MASK);

        // make sure this is the focus widget in the container
        gtk_window_set_focus(GTK_WINDOW(mShell), container);

        // the drawing window
        mGdkWindow = container->window;

        if (mWindowType == eWindowType_popup) {
            // gdk does not automatically set the cursor for "temporary"
            // windows, which are what gtk uses for popups.

            mCursor = eCursor_wait; // force SetCursor to actually set the
                                    // cursor, even though our internal state
                                    // indicates that we already have the
                                    // standard cursor.
            SetCursor(eCursor_standard);
        }
    }
        break;
    case eWindowType_plugin:
    case eWindowType_child: {
        if (parentMozContainer) {
            mGdkWindow = CreateGdkWindow(parentGdkWindow, parentMozContainer);
            nsWindow *parentnsWindow =
                get_window_for_gdk_window(parentGdkWindow);
            if (parentnsWindow)
                mHasMappedToplevel = parentnsWindow->mHasMappedToplevel;
        }
        else if (parentGtkContainer) {
            GtkWidget *container = moz_container_new();
            mContainer = MOZ_CONTAINER(container);
            gtk_container_add(parentGtkContainer, container);
            gtk_widget_realize(container);

            // Don't let GTK mess with the shapes of our GdkWindows
            GTK_PRIVATE_SET_FLAG(container, GTK_HAS_SHAPE_MASK);

            mGdkWindow = container->window;
        }
        else {
            NS_WARNING("Warning: tried to create a new child widget with no parent!");
            return NS_ERROR_FAILURE;
        }
    }
        break;
    default:
        break;
    }
    // Disable the double buffer because it will make the caret crazy
    // For bug#153805 (Gtk2 double buffer makes carets misbehave)
    // DirectFB's expose code depends on gtk double buffering
    // XXX - I think this bug is probably dead, we can just use gtk's
    // double-buffering everywhere
#ifdef MOZ_X11
    if (mContainer)
        gtk_widget_set_double_buffered (GTK_WIDGET(mContainer),FALSE);
#endif

    // label the drawing window with this object so we can find our way home
    g_object_set_data(G_OBJECT(mGdkWindow), "nsWindow", this);

    if (mContainer)
        g_object_set_data(G_OBJECT(mContainer), "nsWindow", this);

    if (mShell)
        g_object_set_data(G_OBJECT(mShell), "nsWindow", this);

    // attach listeners for events
    if (mShell) {
        g_signal_connect(G_OBJECT(mShell), "configure_event",
                         G_CALLBACK(configure_event_cb), NULL);
        g_signal_connect(G_OBJECT(mShell), "delete_event",
                         G_CALLBACK(delete_event_cb), NULL);
        g_signal_connect(G_OBJECT(mShell), "window_state_event",
                         G_CALLBACK(window_state_event_cb), NULL);

        GtkSettings* default_settings = gtk_settings_get_default();
        g_signal_connect_after(default_settings,
                               "notify::gtk-theme-name",
                               G_CALLBACK(theme_changed_cb), this);
        g_signal_connect_after(default_settings,
                               "notify::gtk-font-name",
                               G_CALLBACK(theme_changed_cb), this);
    }

    if (mContainer) {
        g_signal_connect(G_OBJECT(mContainer), "unrealize",
                         G_CALLBACK(container_unrealize_cb), NULL);
        g_signal_connect_after(G_OBJECT(mContainer), "size_allocate",
                               G_CALLBACK(size_allocate_cb), NULL);
        g_signal_connect(G_OBJECT(mContainer), "expose_event",
                         G_CALLBACK(expose_event_cb), NULL);
        g_signal_connect(G_OBJECT(mContainer), "enter_notify_event",
                         G_CALLBACK(enter_notify_event_cb), NULL);
        g_signal_connect(G_OBJECT(mContainer), "leave_notify_event",
                         G_CALLBACK(leave_notify_event_cb), NULL);
        g_signal_connect(G_OBJECT(mContainer), "motion_notify_event",
                         G_CALLBACK(motion_notify_event_cb), NULL);
        g_signal_connect(G_OBJECT(mContainer), "button_press_event",
                         G_CALLBACK(button_press_event_cb), NULL);
        g_signal_connect(G_OBJECT(mContainer), "button_release_event",
                         G_CALLBACK(button_release_event_cb), NULL);
        g_signal_connect(G_OBJECT(mContainer), "focus_in_event",
                         G_CALLBACK(focus_in_event_cb), NULL);
        g_signal_connect(G_OBJECT(mContainer), "focus_out_event",
                         G_CALLBACK(focus_out_event_cb), NULL);
        g_signal_connect(G_OBJECT(mContainer), "key_press_event",
                         G_CALLBACK(key_press_event_cb), NULL);
        g_signal_connect(G_OBJECT(mContainer), "key_release_event",
                         G_CALLBACK(key_release_event_cb), NULL);
        g_signal_connect(G_OBJECT(mContainer), "scroll_event",
                         G_CALLBACK(scroll_event_cb), NULL);
        g_signal_connect(G_OBJECT(mContainer), "visibility_notify_event",
                         G_CALLBACK(visibility_notify_event_cb), NULL);
        g_signal_connect(G_OBJECT(mContainer), "hierarchy_changed",
                         G_CALLBACK(hierarchy_changed_cb), NULL);
        // Initialize mHasMappedToplevel.
        hierarchy_changed_cb(GTK_WIDGET(mContainer), NULL);

        gtk_drag_dest_set((GtkWidget *)mContainer,
                          (GtkDestDefaults)0,
                          NULL,
                          0,
                          (GdkDragAction)0);

        g_signal_connect(G_OBJECT(mContainer), "drag_motion",
                         G_CALLBACK(drag_motion_event_cb), NULL);
        g_signal_connect(G_OBJECT(mContainer), "drag_leave",
                         G_CALLBACK(drag_leave_event_cb), NULL);
        g_signal_connect(G_OBJECT(mContainer), "drag_drop",
                         G_CALLBACK(drag_drop_event_cb), NULL);
        g_signal_connect(G_OBJECT(mContainer), "drag_data_received",
                         G_CALLBACK(drag_data_received_event_cb), NULL);

#ifdef USE_XIM
        // We create input contexts for all containers, except for
        // toplevel popup windows
        if (mWindowType != eWindowType_popup)
            IMECreateContext();
#endif
    }

    LOG(("nsWindow [%p]\n", (void *)this));
    if (mShell) {
        LOG(("\tmShell %p %p %lx\n", (void *)mShell, (void *)mShell->window,
             GDK_WINDOW_XWINDOW(mShell->window)));
    }

    if (mContainer) {
        LOG(("\tmContainer %p %p %lx\n", (void *)mContainer,
             (void *)GTK_WIDGET(mContainer)->window,
             GDK_WINDOW_XWINDOW(GTK_WIDGET(mContainer)->window)));
    }
    else if (mGdkWindow) {
        LOG(("\tmGdkWindow %p %lx\n", (void *)mGdkWindow,
             GDK_WINDOW_XWINDOW(mGdkWindow)));
    }

    // resize so that everything is set to the right dimensions
    if (!mIsTopLevel)
        Resize(mBounds.x, mBounds.y, mBounds.width, mBounds.height, PR_FALSE);

#ifdef ACCESSIBILITY
    nsresult rv;
    if (!sAccessibilityChecked) {
        sAccessibilityChecked = PR_TRUE;

        //check if accessibility enabled/disabled by environment variable
        const char *envValue = PR_GetEnv(sAccEnv);
        if (envValue) {
            sAccessibilityEnabled = atoi(envValue) != 0;
            LOG(("Accessibility Env %s=%s\n", sAccEnv, envValue));
        }
        //check gconf-2 setting
        else {
            nsCOMPtr<nsIPrefBranch> sysPrefService =
                do_GetService(sSysPrefService, &rv);
            if (NS_SUCCEEDED(rv) && sysPrefService) {

                // do the work to get gconf setting.
                // will be done soon later.
                sysPrefService->GetBoolPref(sAccessibilityKey,
                                            &sAccessibilityEnabled);
            }

        }
    }
    if (sAccessibilityEnabled) {
        LOG(("nsWindow:: Create Toplevel Accessibility\n"));
        CreateRootAccessible();
    }
#endif

#ifdef MOZ_DFB
    if (!mDFB) {
         DirectFBCreate( &mDFB );

         D_ASSUME( mDFB != NULL );

         if (mDFB)
              mDFB->GetDisplayLayer( mDFB, DLID_PRIMARY, &mDFBLayer );

         D_ASSUME( mDFBLayer != NULL );

         if (mDFBLayer)
              mDFBLayer->GetCursorPosition( mDFBLayer, &mDFBCursorX, &mDFBCursorY );
    }
#endif

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::SetWindowClass(const nsAString &xulWinType)
{
  if (!mShell)
    return NS_ERROR_FAILURE;

#ifdef MOZ_X11
  nsXPIDLString brandName;
  GetBrandName(brandName);

  XClassHint *class_hint = XAllocClassHint();
  if (!class_hint)
    return NS_ERROR_OUT_OF_MEMORY;
  const char *role = NULL;
  class_hint->res_name = ToNewCString(xulWinType);
  if (!class_hint->res_name) {
    XFree(class_hint);
    return NS_ERROR_OUT_OF_MEMORY;
  }
  class_hint->res_class = ToNewCString(brandName);
  if (!class_hint->res_class) {
    nsMemory::Free(class_hint->res_name);
    XFree(class_hint);
    return NS_ERROR_OUT_OF_MEMORY;
  }

  // Parse res_name into a name and role. Characters other than
  // [A-Za-z0-9_-] are converted to '_'. Anything after the first
  // colon is assigned to role; if there's no colon, assign the
  // whole thing to both role and res_name.
  for (char *c = class_hint->res_name; *c; c++) {
    if (':' == *c) {
      *c = 0;
      role = c + 1;
    }
    else if (!isascii(*c) || (!isalnum(*c) && ('_' != *c) && ('-' != *c)))
      *c = '_';
  }
  class_hint->res_name[0] = toupper(class_hint->res_name[0]);
  if (!role) role = class_hint->res_name;

  gdk_window_set_role(GTK_WIDGET(mShell)->window, role);
  // Can't use gtk_window_set_wmclass() for this; it prints
  // a warning & refuses to make the change.
  XSetClassHint(GDK_DISPLAY(),
                GDK_WINDOW_XWINDOW(GTK_WIDGET(mShell)->window),
                class_hint);
  nsMemory::Free(class_hint->res_class);
  nsMemory::Free(class_hint->res_name);
  XFree(class_hint);
#else /* MOZ_X11 */

  char *res_name;

  res_name = ToNewCString(xulWinType);
  if (!res_name)
    return NS_ERROR_OUT_OF_MEMORY;

  printf("WARN: res_name = '%s'\n", res_name);


  const char *role = NULL;

  // Parse res_name into a name and role. Characters other than
  // [A-Za-z0-9_-] are converted to '_'. Anything after the first
  // colon is assigned to role; if there's no colon, assign the
  // whole thing to both role and res_name.
  for (char *c = res_name; *c; c++) {
    if (':' == *c) {
      *c = 0;
      role = c + 1;
    }
    else if (!isascii(*c) || (!isalnum(*c) && ('_' != *c) && ('-' != *c)))
      *c = '_';
  }
  res_name[0] = toupper(res_name[0]);
  if (!role) role = res_name;

  gdk_window_set_role(GTK_WIDGET(mShell)->window, role);

  nsMemory::Free(res_name);

#endif /* MOZ_X11 */
  return NS_OK;
}

void
nsWindow::NativeResize(PRInt32 aWidth, PRInt32 aHeight, PRBool  aRepaint)
{
    LOG(("nsWindow::NativeResize [%p] %d %d\n", (void *)this,
         aWidth, aHeight));

    ResizeTransparencyBitmap(aWidth, aHeight);

    // clear our resize flag
    mNeedsResize = PR_FALSE;

    if (mIsTopLevel) {
        gtk_window_resize(GTK_WINDOW(mShell), aWidth, aHeight);
    }
    else if (mContainer) {
        GtkWidget *widget = GTK_WIDGET(mContainer);
        GtkAllocation allocation;
        allocation.x = widget->allocation.x;
        allocation.y = widget->allocation.y;
        allocation.width = aWidth;
        allocation.height = aHeight;
        gtk_widget_size_allocate(widget, &allocation);
    }
    else if (mGdkWindow) {
        gdk_window_resize(mGdkWindow, aWidth, aHeight);
    }
}

void
nsWindow::NativeResize(PRInt32 aX, PRInt32 aY,
                       PRInt32 aWidth, PRInt32 aHeight,
                       PRBool  aRepaint)
{
    mNeedsResize = PR_FALSE;
    mNeedsMove = PR_FALSE;

    LOG(("nsWindow::NativeResize [%p] %d %d %d %d\n", (void *)this,
         aX, aY, aWidth, aHeight));

    ResizeTransparencyBitmap(aWidth, aHeight);

    if (mIsTopLevel) {
        // We only move the toplevel window if someone has
        // actually placed the window somewhere.  If no placement
        // has taken place, we just let the window manager Do The
        // Right Thing.
        if (mPlaced)
            gtk_window_move(GTK_WINDOW(mShell), aX, aY);

        gtk_window_resize(GTK_WINDOW(mShell), aWidth, aHeight);
    }
    else if (mContainer) {
        GtkAllocation allocation;
        allocation.x = aX;
        allocation.y = aY;
        allocation.width = aWidth;
        allocation.height = aHeight;
        gtk_widget_size_allocate(GTK_WIDGET(mContainer), &allocation);
    }
    else if (mGdkWindow) {
        gdk_window_move_resize(mGdkWindow, aX, aY, aWidth, aHeight);
    }
}

void
nsWindow::NativeShow (PRBool  aAction)
{
    if (aAction) {
        // GTK wants us to set the window mask before we show the window
        // for the first time, or setting the mask later won't work.
        // GTK also wants us to NOT set the window mask if we're not really
        // going to need it, because GTK won't let us unset the mask properly
        // later.
        // So, we delay setting the mask until the last moment: when the window
        // is shown.
        // XXX that may or may not be true for GTK+ 2.x
        if (mTransparencyBitmap) {
            ApplyTransparencyBitmap();
        }

        // unset our flag now that our window has been shown
        mNeedsShow = PR_FALSE;

        if (mIsTopLevel) {
            // Set up usertime/startupID metadata for the created window.
            if (mWindowType != eWindowType_invisible) {
                SetUserTimeAndStartupIDForActivatedWindow(mShell);
            }

            gtk_widget_show(GTK_WIDGET(mContainer));
            gtk_widget_show(mShell);
        }
        else if (mContainer) {
            gtk_widget_show(GTK_WIDGET(mContainer));
        }
        else if (mGdkWindow) {
            gdk_window_show_unraised(mGdkWindow);
        }
    }
    else {
        if (mIsTopLevel) {
            gtk_widget_hide(GTK_WIDGET(mShell));
            gtk_widget_hide(GTK_WIDGET(mContainer));
        }
        else if (mContainer) {
            gtk_widget_hide(GTK_WIDGET(mContainer));
        }
        else if (mGdkWindow) {
            gdk_window_hide(mGdkWindow);
        }
    }
}

void
nsWindow::SetHasMappedToplevel(PRBool aState)
{
    // Even when aState == mHasMappedToplevel (as when this method is called
    // from Show()), child windows need to have their state checked, so don't
    // return early.
    PRBool oldState = mHasMappedToplevel;
    mHasMappedToplevel = aState;

    // mHasMappedToplevel is not updated for children of windows that are
    // hidden; GDK knows not to send expose events for these windows.  The
    // state is recorded on the hidden window itself, but, for child trees of
    // hidden windows, their state essentially becomes disconnected from their
    // hidden parent.  When the hidden parent gets shown, the child trees are
    // reconnected, and the state of the window being shown can be easily
    // propagated.
    if (!mIsShown || !mGdkWindow)
        return;

    if (aState && !oldState && !mIsFullyObscured) {
        // GDK_EXPOSE events have been ignored but the window is now visible,
        // so make sure GDK doesn't think that the window has already been
        // painted.
        gdk_window_invalidate_rect(mGdkWindow, NULL, FALSE);

        // Check that a grab didn't fail due to the window not being
        // viewable.
        EnsureGrabs();
    }

    for (GList *children = gdk_window_peek_children(mGdkWindow);
         children;
         children = children->next) {
        GdkWindow *gdkWin = GDK_WINDOW(children->data);
        nsWindow *child = get_window_for_gdk_window(gdkWin);

        if (child && child->mHasMappedToplevel != aState) {
            child->SetHasMappedToplevel(aState);
        }
    }
}

nsIntSize
nsWindow::GetSafeWindowSize(nsIntSize aSize)
{
    nsIntSize result = aSize;
    const PRInt32 kInt16Max = 32767;
    if (result.width > kInt16Max) {
        NS_WARNING("Clamping huge window width");
        result.width = kInt16Max;
    }
    if (result.height > kInt16Max) {
        NS_WARNING("Clamping huge window height");
        result.height = kInt16Max;
    }
    return result;
}

void
nsWindow::EnsureGrabs(void)
{
    if (mRetryPointerGrab)
        GrabPointer();
    if (mRetryKeyboardGrab)
        GrabKeyboard();
}

void
nsWindow::SetTransparencyMode(nsTransparencyMode aMode)
{
    if (!mShell) {
        // Pass the request to the toplevel window
        GtkWidget *topWidget = nsnull;
        GetToplevelWidget(&topWidget);
        if (!topWidget)
            return;

        nsWindow *topWindow = get_window_for_gtk_widget(topWidget);
        if (!topWindow)
            return;

        topWindow->SetTransparencyMode(aMode);
        return;
    }
    PRBool isTransparent = aMode == eTransparencyTransparent;

    if (mIsTransparent == isTransparent)
        return;

    if (!isTransparent) {
        if (mTransparencyBitmap) {
            delete[] mTransparencyBitmap;
            mTransparencyBitmap = nsnull;
            mTransparencyBitmapWidth = 0;
            mTransparencyBitmapHeight = 0;
            gtk_widget_reset_shapes(mShell);
        }
    } // else the new default alpha values are "all 1", so we don't
    // need to change anything yet

    mIsTransparent = isTransparent;
}

nsTransparencyMode
nsWindow::GetTransparencyMode()
{
    if (!mShell) {
        // Pass the request to the toplevel window
        GtkWidget *topWidget = nsnull;
        GetToplevelWidget(&topWidget);
        if (!topWidget) {
            return eTransparencyOpaque;
        }

        nsWindow *topWindow = get_window_for_gtk_widget(topWidget);
        if (!topWindow) {
            return eTransparencyOpaque;
        }

        return topWindow->GetTransparencyMode();
    }

    return mIsTransparent ? eTransparencyTransparent : eTransparencyOpaque;
}

nsresult
nsWindow::ConfigureChildren(const nsTArray<Configuration>& aConfigurations)
{
    for (PRUint32 i = 0; i < aConfigurations.Length(); ++i) {
        const Configuration& configuration = aConfigurations[i];
        nsWindow* w = static_cast<nsWindow*>(configuration.mChild);
        NS_ASSERTION(w->GetParent() == this,
                     "Configured widget is not a child");
        w->SetWindowClipRegion(configuration.mClipRegion, PR_TRUE);
        if (w->mBounds.Size() != configuration.mBounds.Size()) {
            w->Resize(configuration.mBounds.x, configuration.mBounds.y,
                      configuration.mBounds.width, configuration.mBounds.height,
                      PR_TRUE);
        } else if (w->mBounds.TopLeft() != configuration.mBounds.TopLeft()) {
            w->Move(configuration.mBounds.x, configuration.mBounds.y);
        } 
        w->SetWindowClipRegion(configuration.mClipRegion, PR_FALSE);
    }
    return NS_OK;
}

void
nsWindow::SetWindowClipRegion(const nsTArray<nsIntRect>& aRects,
                              PRBool aIntersectWithExisting)
{
    const nsTArray<nsIntRect>* newRects = &aRects;

    nsAutoTArray<nsIntRect,1> intersectRects;
    if (aIntersectWithExisting) {
        nsAutoTArray<nsIntRect,1> existingRects;
        GetWindowClipRegion(&existingRects);

        nsAutoRef<pixman_region32> existingRegion;
        InitRegion(&existingRegion, existingRects);
        nsAutoRef<pixman_region32> newRegion;
        InitRegion(&newRegion, aRects);
        nsAutoRef<pixman_region32> intersectRegion;
        pixman_region32_intersect(&intersectRegion,
                                  &newRegion, &existingRegion);

        if (pixman_region32_equal(&intersectRegion, &existingRegion))
            return;

        if (!pixman_region32_equal(&intersectRegion, &newRegion)) {
            GetIntRects(intersectRegion, &intersectRects);
            newRects = &intersectRects;
        }
    }

    if (!StoreWindowClipRegion(*newRects))
        return;

    if (!mGdkWindow)
        return;

    GdkRegion *region = gdk_region_new(); // aborts on OOM
    for (PRUint32 i = 0; i < newRects->Length(); ++i) {
        const nsIntRect& r = newRects->ElementAt(i);
        GdkRectangle rect = { r.x, r.y, r.width, r.height };
        gdk_region_union_with_rect(region, &rect);
    }

    gdk_window_shape_combine_region(mGdkWindow, region, 0, 0);
    gdk_region_destroy(region);

    return;
}

void
nsWindow::ResizeTransparencyBitmap(PRInt32 aNewWidth, PRInt32 aNewHeight)
{
    if (!mTransparencyBitmap)
        return;

    if (aNewWidth == mTransparencyBitmapWidth &&
        aNewHeight == mTransparencyBitmapHeight)
        return;

    PRInt32 newSize = ((aNewWidth+7)/8)*aNewHeight;
    gchar* newBits = new gchar[newSize];
    if (!newBits) {
        delete[] mTransparencyBitmap;
        mTransparencyBitmap = nsnull;
        mTransparencyBitmapWidth = 0;
        mTransparencyBitmapHeight = 0;
        return;
    }
    // fill new mask with "opaque", first
    memset(newBits, 255, newSize);

    // Now copy the intersection of the old and new areas into the new mask
    PRInt32 copyWidth = PR_MIN(aNewWidth, mTransparencyBitmapWidth);
    PRInt32 copyHeight = PR_MIN(aNewHeight, mTransparencyBitmapHeight);
    PRInt32 oldRowBytes = (mTransparencyBitmapWidth+7)/8;
    PRInt32 newRowBytes = (aNewWidth+7)/8;
    PRInt32 copyBytes = (copyWidth+7)/8;

    PRInt32 i;
    gchar* fromPtr = mTransparencyBitmap;
    gchar* toPtr = newBits;
    for (i = 0; i < copyHeight; i++) {
        memcpy(toPtr, fromPtr, copyBytes);
        fromPtr += oldRowBytes;
        toPtr += newRowBytes;
    }

    delete[] mTransparencyBitmap;
    mTransparencyBitmap = newBits;
    mTransparencyBitmapWidth = aNewWidth;
    mTransparencyBitmapHeight = aNewHeight;
}

static PRBool
ChangedMaskBits(gchar* aMaskBits, PRInt32 aMaskWidth, PRInt32 aMaskHeight,
        const nsIntRect& aRect, PRUint8* aAlphas, PRInt32 aStride)
{
    PRInt32 x, y, xMax = aRect.XMost(), yMax = aRect.YMost();
    PRInt32 maskBytesPerRow = (aMaskWidth + 7)/8;
    for (y = aRect.y; y < yMax; y++) {
        gchar* maskBytes = aMaskBits + y*maskBytesPerRow;
        PRUint8* alphas = aAlphas;
        for (x = aRect.x; x < xMax; x++) {
            PRBool newBit = *alphas > 0;
            alphas++;

            gchar maskByte = maskBytes[x >> 3];
            PRBool maskBit = (maskByte & (1 << (x & 7))) != 0;

            if (maskBit != newBit) {
                return PR_TRUE;
            }
        }
        aAlphas += aStride;
    }

    return PR_FALSE;
}

static
void UpdateMaskBits(gchar* aMaskBits, PRInt32 aMaskWidth, PRInt32 aMaskHeight,
        const nsIntRect& aRect, PRUint8* aAlphas, PRInt32 aStride)
{
    PRInt32 x, y, xMax = aRect.XMost(), yMax = aRect.YMost();
    PRInt32 maskBytesPerRow = (aMaskWidth + 7)/8;
    for (y = aRect.y; y < yMax; y++) {
        gchar* maskBytes = aMaskBits + y*maskBytesPerRow;
        PRUint8* alphas = aAlphas;
        for (x = aRect.x; x < xMax; x++) {
            PRBool newBit = *alphas > 0;
            alphas++;

            gchar mask = 1 << (x & 7);
            gchar maskByte = maskBytes[x >> 3];
            // Note: '-newBit' turns 0 into 00...00 and 1 into 11...11
            maskBytes[x >> 3] = (maskByte & ~mask) | (-newBit & mask);
        }
        aAlphas += aStride;
    }
}

void
nsWindow::ApplyTransparencyBitmap()
{
    gtk_widget_reset_shapes(mShell);
    GdkBitmap* maskBitmap = gdk_bitmap_create_from_data(mShell->window,
            mTransparencyBitmap,
            mTransparencyBitmapWidth, mTransparencyBitmapHeight);
    if (!maskBitmap)
        return;

    gtk_widget_shape_combine_mask(mShell, maskBitmap, 0, 0);
    g_object_unref(maskBitmap);
}

nsresult
nsWindow::UpdateTranslucentWindowAlphaInternal(const nsIntRect& aRect,
                                               PRUint8* aAlphas, PRInt32 aStride)
{
    if (!mShell) {
        // Pass the request to the toplevel window
        GtkWidget *topWidget = nsnull;
        GetToplevelWidget(&topWidget);
        if (!topWidget)
            return NS_ERROR_FAILURE;

        nsWindow *topWindow = get_window_for_gtk_widget(topWidget);
        if (!topWindow)
            return NS_ERROR_FAILURE;

        return topWindow->UpdateTranslucentWindowAlphaInternal(aRect, aAlphas, aStride);
    }

    NS_ASSERTION(mIsTransparent, "Window is not transparent");

    if (mTransparencyBitmap == nsnull) {
        PRInt32 size = ((mBounds.width+7)/8)*mBounds.height;
        mTransparencyBitmap = new gchar[size];
        if (mTransparencyBitmap == nsnull)
            return NS_ERROR_FAILURE;
        memset(mTransparencyBitmap, 255, size);
        mTransparencyBitmapWidth = mBounds.width;
        mTransparencyBitmapHeight = mBounds.height;
    }

    NS_ASSERTION(aRect.x >= 0 && aRect.y >= 0
            && aRect.XMost() <= mBounds.width && aRect.YMost() <= mBounds.height,
            "Rect is out of window bounds");

    if (!ChangedMaskBits(mTransparencyBitmap, mBounds.width, mBounds.height,
                         aRect, aAlphas, aStride))
        // skip the expensive stuff if the mask bits haven't changed; hopefully
        // this is the common case
        return NS_OK;

    UpdateMaskBits(mTransparencyBitmap, mBounds.width, mBounds.height,
                   aRect, aAlphas, aStride);

    if (!mNeedsShow) {
        ApplyTransparencyBitmap();
    }
    return NS_OK;
}

void
nsWindow::GrabPointer(void)
{
    LOG(("GrabPointer %d\n", mRetryPointerGrab));

    mRetryPointerGrab = PR_FALSE;

    // If the window isn't visible, just set the flag to retry the
    // grab.  When this window becomes visible, the grab will be
    // retried.
    if (!mHasMappedToplevel || mIsFullyObscured) {
        LOG(("GrabPointer: window not visible\n"));
        mRetryPointerGrab = PR_TRUE;
        return;
    }

    if (!mGdkWindow)
        return;

    gint retval;
    retval = gdk_pointer_grab(mGdkWindow, TRUE,
                              (GdkEventMask)(GDK_BUTTON_PRESS_MASK |
                                             GDK_BUTTON_RELEASE_MASK |
                                             GDK_ENTER_NOTIFY_MASK |
                                             GDK_LEAVE_NOTIFY_MASK |
#ifdef HAVE_GTK_MOTION_HINTS
                                             GDK_POINTER_MOTION_HINT_MASK |
#endif
                                             GDK_POINTER_MOTION_MASK),
                              (GdkWindow *)NULL, NULL, GDK_CURRENT_TIME);

    if (retval != GDK_GRAB_SUCCESS) {
        LOG(("GrabPointer: pointer grab failed\n"));
        mRetryPointerGrab = PR_TRUE;
    }
}

void
nsWindow::GrabKeyboard(void)
{
    LOG(("GrabKeyboard %d\n", mRetryKeyboardGrab));

    mRetryKeyboardGrab = PR_FALSE;

    // If the window isn't visible, just set the flag to retry the
    // grab.  When this window becomes visible, the grab will be
    // retried.
    if (!mHasMappedToplevel || mIsFullyObscured) {
        LOG(("GrabKeyboard: window not visible\n"));
        mRetryKeyboardGrab = PR_TRUE;
        return;
    }

    // we need to grab the keyboard on the transient parent so that we
    // don't end up with any focus events that end up on the parent
    // window that will cause the popup to go away
    GdkWindow *grabWindow;

    if (mTransientParent)
        grabWindow = GTK_WIDGET(mTransientParent)->window;
    else if (mGdkWindow)
        grabWindow = mGdkWindow;
    else
        return;

    gint retval;
    retval = gdk_keyboard_grab(grabWindow, TRUE, GDK_CURRENT_TIME);

    if (retval != GDK_GRAB_SUCCESS) {
        LOG(("GrabKeyboard: keyboard grab failed %d\n", retval));
        gdk_pointer_ungrab(GDK_CURRENT_TIME);
        mRetryKeyboardGrab = PR_TRUE;
    }
}

void
nsWindow::ReleaseGrabs(void)
{
    LOG(("ReleaseGrabs\n"));

    mRetryPointerGrab = PR_FALSE;
    mRetryKeyboardGrab = PR_FALSE;

    gdk_pointer_ungrab(GDK_CURRENT_TIME);
    gdk_keyboard_ungrab(GDK_CURRENT_TIME);
}

void
nsWindow::GetToplevelWidget(GtkWidget **aWidget)
{
    *aWidget = nsnull;

    if (mShell) {
        *aWidget = mShell;
        return;
    }

    GtkWidget *widget = GetMozContainerWidget();
    if (!widget)
        return;

    *aWidget = gtk_widget_get_toplevel(widget);
}

GtkWidget *
nsWindow::GetMozContainerWidget()
{
    if (!mGdkWindow)
        return NULL;

    GtkWidget *owningWidget =
        get_gtk_widget_for_gdk_window(mGdkWindow);
    return owningWidget;
}

nsWindow *
nsWindow::GetContainerWindow()
{
    GtkWidget *owningWidget = GetMozContainerWidget();
    if (!owningWidget)
        return nsnull;

    nsWindow *window = get_window_for_gtk_widget(owningWidget);
    NS_ASSERTION(window, "No nsWindow for container widget");
    return window;
}

void
nsWindow::SetUrgencyHint(GtkWidget *top_window, PRBool state)
{
    if (!top_window)
        return;

    // Try to get a pointer to gdk_window_set_urgency_hint
    PRLibrary* lib;
    _gdk_window_set_urgency_hint_fn _gdk_window_set_urgency_hint = nsnull;
    _gdk_window_set_urgency_hint = (_gdk_window_set_urgency_hint_fn)
           PR_FindFunctionSymbolAndLibrary("gdk_window_set_urgency_hint", &lib);

    if (_gdk_window_set_urgency_hint) {
        _gdk_window_set_urgency_hint(top_window->window, state);
        PR_UnloadLibrary(lib);
    }
    else if (state) {
        gdk_window_show_unraised(top_window->window);
    }
}

void *
nsWindow::SetupPluginPort(void)
{
    if (!mGdkWindow)
        return nsnull;

    if (GDK_WINDOW_OBJECT(mGdkWindow)->destroyed == TRUE)
        return nsnull;

    // we have to flush the X queue here so that any plugins that
    // might be running on separate X connections will be able to use
    // this window in case it was just created
#ifdef MOZ_X11
    XWindowAttributes xattrs;
    XGetWindowAttributes(GDK_DISPLAY(), GDK_WINDOW_XWINDOW(mGdkWindow),
                         &xattrs);
    XSelectInput (GDK_DISPLAY (),
                  GDK_WINDOW_XWINDOW(mGdkWindow),
                  xattrs.your_event_mask |
                  SubstructureNotifyMask);

    gdk_window_add_filter(mGdkWindow, plugin_window_filter_func, this);

    XSync(GDK_DISPLAY(), False);
#endif /* MOZ_X11 */

    return (void *)GDK_WINDOW_XWINDOW(mGdkWindow);
}

nsresult
nsWindow::SetWindowIconList(const nsTArray<nsCString> &aIconList)
{
    GList *list = NULL;

    for (PRUint32 i = 0; i < aIconList.Length(); ++i) {
        const char *path = aIconList[i].get();
        LOG(("window [%p] Loading icon from %s\n", (void *)this, path));

        GdkPixbuf *icon = gdk_pixbuf_new_from_file(path, NULL);
        if (!icon)
            continue;

        list = g_list_append(list, icon);
    }

    if (!list)
        return NS_ERROR_FAILURE;

    gtk_window_set_icon_list(GTK_WINDOW(mShell), list);

    g_list_foreach(list, (GFunc) g_object_unref, NULL);
    g_list_free(list);

    return NS_OK;
}

void
nsWindow::SetDefaultIcon(void)
{
    SetIcon(NS_LITERAL_STRING("default"));
}

void
nsWindow::SetPluginType(PluginType aPluginType)
{
    mPluginType = aPluginType;
}

#ifdef MOZ_X11
void
nsWindow::SetNonXEmbedPluginFocus()
{
    if (gPluginFocusWindow == this || mPluginType!=PluginType_NONXEMBED) {
        return;
    }

    if (gPluginFocusWindow) {
        nsRefPtr<nsWindow> kungFuDeathGrip = gPluginFocusWindow;
        gPluginFocusWindow->LoseNonXEmbedPluginFocus();
    }

    LOGFOCUS(("nsWindow::SetNonXEmbedPluginFocus\n"));

    Window curFocusWindow;
    int focusState;

    XGetInputFocus(GDK_WINDOW_XDISPLAY(mGdkWindow),
                   &curFocusWindow,
                   &focusState);

    LOGFOCUS(("\t curFocusWindow=%p\n", curFocusWindow));

    GdkWindow* toplevel = gdk_window_get_toplevel(mGdkWindow);
    GdkWindow *gdkfocuswin = gdk_window_lookup(curFocusWindow);

    // lookup with the focus proxy window is supposed to get the
    // same GdkWindow as toplevel. If the current focused window
    // is not the focus proxy, we return without any change.
    if (gdkfocuswin != toplevel) {
        return;
    }

    // switch the focus from the focus proxy to the plugin window
    mOldFocusWindow = curFocusWindow;
    XRaiseWindow(GDK_WINDOW_XDISPLAY(mGdkWindow),
                 GDK_WINDOW_XWINDOW(mGdkWindow));
    gdk_error_trap_push();
    XSetInputFocus(GDK_WINDOW_XDISPLAY(mGdkWindow),
                   GDK_WINDOW_XWINDOW(mGdkWindow),
                   RevertToNone,
                   CurrentTime);
    gdk_flush();
    gdk_error_trap_pop();
    gPluginFocusWindow = this;
    gdk_window_add_filter(NULL, plugin_client_message_filter, this);

    LOGFOCUS(("nsWindow::SetNonXEmbedPluginFocus oldfocus=%p new=%p\n",
              mOldFocusWindow, GDK_WINDOW_XWINDOW(mGdkWindow)));
}

void
nsWindow::LoseNonXEmbedPluginFocus()
{
    LOGFOCUS(("nsWindow::LoseNonXEmbedPluginFocus\n"));

    // This method is only for the nsWindow which contains a
    // Non-XEmbed plugin, for example, JAVA plugin.
    if (gPluginFocusWindow != this || mPluginType!=PluginType_NONXEMBED) {
        return;
    }

    Window curFocusWindow;
    int focusState;

    XGetInputFocus(GDK_WINDOW_XDISPLAY(mGdkWindow),
                   &curFocusWindow,
                   &focusState);

    // we only switch focus between plugin window and focus proxy. If the
    // current focused window is not the plugin window, just removing the
    // event filter that blocks the WM_TAKE_FOCUS is enough. WM and gtk2
    // will take care of the focus later.
    if (!curFocusWindow ||
        curFocusWindow == GDK_WINDOW_XWINDOW(mGdkWindow)) {

        gdk_error_trap_push();
        XRaiseWindow(GDK_WINDOW_XDISPLAY(mGdkWindow),
                     mOldFocusWindow);
        XSetInputFocus(GDK_WINDOW_XDISPLAY(mGdkWindow),
                       mOldFocusWindow,
                       RevertToParent,
                       CurrentTime);
        gdk_flush();
        gdk_error_trap_pop();
    }
    gPluginFocusWindow = NULL;
    mOldFocusWindow = 0;
    gdk_window_remove_filter(NULL, plugin_client_message_filter, this);

    LOGFOCUS(("nsWindow::LoseNonXEmbedPluginFocus end\n"));
}
#endif /* MOZ_X11 */


gint
nsWindow::ConvertBorderStyles(nsBorderStyle aStyle)
{
    gint w = 0;

    if (aStyle == eBorderStyle_default)
        return -1;

    if (aStyle & eBorderStyle_all)
        w |= GDK_DECOR_ALL;
    if (aStyle & eBorderStyle_border)
        w |= GDK_DECOR_BORDER;
    if (aStyle & eBorderStyle_resizeh)
        w |= GDK_DECOR_RESIZEH;
    if (aStyle & eBorderStyle_title)
        w |= GDK_DECOR_TITLE;
    if (aStyle & eBorderStyle_menu)
        w |= GDK_DECOR_MENU;
    if (aStyle & eBorderStyle_minimize)
        w |= GDK_DECOR_MINIMIZE;
    if (aStyle & eBorderStyle_maximize)
        w |= GDK_DECOR_MAXIMIZE;
    if (aStyle & eBorderStyle_close) {
#ifdef DEBUG
        printf("we don't handle eBorderStyle_close yet... please fix me\n");
#endif /* DEBUG */
    }

    return w;
}

NS_IMETHODIMP
nsWindow::MakeFullScreen(PRBool aFullScreen)
{
    LOG(("nsWindow::MakeFullScreen [%p] aFullScreen %d\n",
         (void *)this, aFullScreen));

    if (aFullScreen) {
        if (mSizeMode != nsSizeMode_Fullscreen)
            mLastSizeMode = mSizeMode;

        mSizeMode = nsSizeMode_Fullscreen;
        gtk_window_fullscreen(GTK_WINDOW(mShell));
    }
    else {
        mSizeMode = mLastSizeMode;
        gtk_window_unfullscreen(GTK_WINDOW(mShell));
    }

    NS_ASSERTION(mLastSizeMode != nsSizeMode_Fullscreen,
                 "mLastSizeMode should never be fullscreen");
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::HideWindowChrome(PRBool aShouldHide)
{
    if (!mShell) {
        // Pass the request to the toplevel window
        GtkWidget *topWidget = nsnull;
        GetToplevelWidget(&topWidget);
        if (!topWidget)
            return NS_ERROR_FAILURE;

        nsWindow *topWindow = get_window_for_gtk_widget(topWidget);
        if (!topWindow)
            return NS_ERROR_FAILURE;

        return topWindow->HideWindowChrome(aShouldHide);
    }

    // Sawfish, metacity, and presumably other window managers get
    // confused if we change the window decorations while the window
    // is visible.
    PRBool wasVisible = PR_FALSE;
    if (gdk_window_is_visible(mShell->window)) {
        gdk_window_hide(mShell->window);
        wasVisible = PR_TRUE;
    }

    gint wmd;
    if (aShouldHide)
        wmd = 0;
    else
        wmd = ConvertBorderStyles(mBorderStyle);

    gdk_window_set_decorations(mShell->window, (GdkWMDecoration) wmd);

    if (wasVisible)
        gdk_window_show(mShell->window);

    // For some window managers, adding or removing window decorations
    // requires unmapping and remapping our toplevel window.  Go ahead
    // and flush the queue here so that we don't end up with a BadWindow
    // error later when this happens (when the persistence timer fires
    // and GetWindowPos is called)
#ifdef MOZ_X11
    XSync(GDK_DISPLAY(), False);
#else
    gdk_flush ();
#endif /* MOZ_X11 */

    return NS_OK;
}

PRBool
check_for_rollup(GdkWindow *aWindow, gdouble aMouseX, gdouble aMouseY,
                 PRBool aIsWheel)
{
    PRBool retVal = PR_FALSE;
    nsCOMPtr<nsIWidget> rollupWidget = do_QueryReferent(gRollupWindow);

    if (rollupWidget && gRollupListener) {
        GdkWindow *currentPopup =
            (GdkWindow *)rollupWidget->GetNativeData(NS_NATIVE_WINDOW);
        if (!is_mouse_in_window(currentPopup, aMouseX, aMouseY)) {
            PRBool rollup = PR_TRUE;
            if (aIsWheel) {
                gRollupListener->ShouldRollupOnMouseWheelEvent(&rollup);
                retVal = PR_TRUE;
            }
            // if we're dealing with menus, we probably have submenus and
            // we don't want to rollup if the clickis in a parent menu of
            // the current submenu
            PRUint32 popupsToRollup = PR_UINT32_MAX;
            if (gMenuRollup) {
                nsAutoTArray<nsIWidget*, 5> widgetChain;
                PRUint32 sameTypeCount = gMenuRollup->GetSubmenuWidgetChain(&widgetChain);
                for (PRUint32 i=0; i<widgetChain.Length(); ++i) {
                    nsIWidget* widget = widgetChain[i];
                    GdkWindow* currWindow =
                        (GdkWindow*) widget->GetNativeData(NS_NATIVE_WINDOW);
                    if (is_mouse_in_window(currWindow, aMouseX, aMouseY)) {
                      // don't roll up if the mouse event occured within a
                      // menu of the same type. If the mouse event occured
                      // in a menu higher than that, roll up, but pass the
                      // number of popups to Rollup so that only those of the
                      // same type close up.
                      if (i < sameTypeCount) {
                        rollup = PR_FALSE;
                      }
                      else {
                        popupsToRollup = sameTypeCount;
                      }
                      break;
                    }
                } // foreach parent menu widget
            } // if rollup listener knows about menus

            // if we've determined that we should still rollup, do it.
            if (rollup) {
                gRollupListener->Rollup(popupsToRollup, nsnull);
                if (popupsToRollup == PR_UINT32_MAX) {
                    retVal = PR_TRUE;
                }
            }
        }
    } else {
        gRollupWindow = nsnull;
        gRollupListener = nsnull;
        NS_IF_RELEASE(gMenuRollup);
    }

    return retVal;
}

/* static */
PRBool
nsWindow::DragInProgress(void)
{
    // mLastDragMotionWindow means the drag arrow is over mozilla
    // sIsDraggingOutOf means the drag arrow is out of mozilla
    // both cases mean the dragging is happenning.
    return (mLastDragMotionWindow || sIsDraggingOutOf);
}

/* static */
PRBool
is_mouse_in_window (GdkWindow* aWindow, gdouble aMouseX, gdouble aMouseY)
{
    gint x = 0;
    gint y = 0;
    gint w, h;

    gint offsetX = 0;
    gint offsetY = 0;

    GdkWindow *window = aWindow;

    while (window) {
        gint tmpX = 0;
        gint tmpY = 0;

        gdk_window_get_position(window, &tmpX, &tmpY);
        GtkWidget *widget = get_gtk_widget_for_gdk_window(window);

        // if this is a window, compute x and y given its origin and our
        // offset
        if (GTK_IS_WINDOW(widget)) {
            x = tmpX + offsetX;
            y = tmpY + offsetY;
            break;
        }

        offsetX += tmpX;
        offsetY += tmpY;
        window = gdk_window_get_parent(window);
    }

    gdk_drawable_get_size(aWindow, &w, &h);

    if (aMouseX > x && aMouseX < x + w &&
        aMouseY > y && aMouseY < y + h)
        return PR_TRUE;

    return PR_FALSE;
}

/* static */
nsWindow *
get_window_for_gtk_widget(GtkWidget *widget)
{
    gpointer user_data = g_object_get_data(G_OBJECT(widget), "nsWindow");

    return static_cast<nsWindow *>(user_data);
}

/* static */
nsWindow *
get_window_for_gdk_window(GdkWindow *window)
{
    gpointer user_data = g_object_get_data(G_OBJECT(window), "nsWindow");

    return static_cast<nsWindow *>(user_data);
}

/* static */
nsWindow *
get_owning_window_for_gdk_window(GdkWindow *window)
{
    GtkWidget *owningWidget = get_gtk_widget_for_gdk_window(window);
    if (!owningWidget)
        return nsnull;

    gpointer user_data = g_object_get_data(G_OBJECT(owningWidget), "nsWindow");

    return static_cast<nsWindow *>(user_data);
}

/* static */
GtkWidget *
get_gtk_widget_for_gdk_window(GdkWindow *window)
{
    gpointer user_data = NULL;
    gdk_window_get_user_data(window, &user_data);

    return GTK_WIDGET(user_data);
}

/* static */
GdkCursor *
get_gtk_cursor(nsCursor aCursor)
{
    GdkPixmap *cursor;
    GdkPixmap *mask;
    GdkColor fg, bg;
    GdkCursor *gdkcursor = nsnull;
    PRUint8 newType = 0xff;

    if ((gdkcursor = gCursorCache[aCursor])) {
        return gdkcursor;
    }

    switch (aCursor) {
    case eCursor_standard:
        gdkcursor = gdk_cursor_new(GDK_LEFT_PTR);
        break;
    case eCursor_wait:
        gdkcursor = gdk_cursor_new(GDK_WATCH);
        break;
    case eCursor_select:
        gdkcursor = gdk_cursor_new(GDK_XTERM);
        break;
    case eCursor_hyperlink:
        gdkcursor = gdk_cursor_new(GDK_HAND2);
        break;
    case eCursor_n_resize:
        gdkcursor = gdk_cursor_new(GDK_TOP_SIDE);
        break;
    case eCursor_s_resize:
        gdkcursor = gdk_cursor_new(GDK_BOTTOM_SIDE);
        break;
    case eCursor_w_resize:
        gdkcursor = gdk_cursor_new(GDK_LEFT_SIDE);
        break;
    case eCursor_e_resize:
        gdkcursor = gdk_cursor_new(GDK_RIGHT_SIDE);
        break;
    case eCursor_nw_resize:
        gdkcursor = gdk_cursor_new(GDK_TOP_LEFT_CORNER);
        break;
    case eCursor_se_resize:
        gdkcursor = gdk_cursor_new(GDK_BOTTOM_RIGHT_CORNER);
        break;
    case eCursor_ne_resize:
        gdkcursor = gdk_cursor_new(GDK_TOP_RIGHT_CORNER);
        break;
    case eCursor_sw_resize:
        gdkcursor = gdk_cursor_new(GDK_BOTTOM_LEFT_CORNER);
        break;
    case eCursor_crosshair:
        gdkcursor = gdk_cursor_new(GDK_CROSSHAIR);
        break;
    case eCursor_move:
        gdkcursor = gdk_cursor_new(GDK_FLEUR);
        break;
    case eCursor_help:
        gdkcursor = gdk_cursor_new(GDK_QUESTION_ARROW);
        break;
    case eCursor_copy: // CSS3
        newType = MOZ_CURSOR_COPY;
        break;
    case eCursor_alias:
        newType = MOZ_CURSOR_ALIAS;
        break;
    case eCursor_context_menu:
        newType = MOZ_CURSOR_CONTEXT_MENU;
        break;
    case eCursor_cell:
        gdkcursor = gdk_cursor_new(GDK_PLUS);
        break;
    case eCursor_grab:
        newType = MOZ_CURSOR_HAND_GRAB;
        break;
    case eCursor_grabbing:
        newType = MOZ_CURSOR_HAND_GRABBING;
        break;
    case eCursor_spinning:
        newType = MOZ_CURSOR_SPINNING;
        break;
    case eCursor_zoom_in:
        newType = MOZ_CURSOR_ZOOM_IN;
        break;
    case eCursor_zoom_out:
        newType = MOZ_CURSOR_ZOOM_OUT;
        break;
    case eCursor_not_allowed:
    case eCursor_no_drop:
        newType = MOZ_CURSOR_NOT_ALLOWED;
        break;
    case eCursor_vertical_text:
        newType = MOZ_CURSOR_VERTICAL_TEXT;
        break;
    case eCursor_all_scroll:
        gdkcursor = gdk_cursor_new(GDK_FLEUR);
        break;
    case eCursor_nesw_resize:
        newType = MOZ_CURSOR_NESW_RESIZE;
        break;
    case eCursor_nwse_resize:
        newType = MOZ_CURSOR_NWSE_RESIZE;
        break;
    case eCursor_ns_resize:
    case eCursor_row_resize:
        gdkcursor = gdk_cursor_new(GDK_SB_V_DOUBLE_ARROW);
        break;
    case eCursor_ew_resize:
    case eCursor_col_resize:
        gdkcursor = gdk_cursor_new(GDK_SB_H_DOUBLE_ARROW);
        break;
    case eCursor_none:
        newType = MOZ_CURSOR_NONE;
        break;
    default:
        NS_ASSERTION(aCursor, "Invalid cursor type");
        gdkcursor = gdk_cursor_new(GDK_LEFT_PTR);
        break;
    }

    // if by now we don't have a xcursor, this means we have to make a
    // custom one
    if (newType != 0xff) {
        gdk_color_parse("#000000", &fg);
        gdk_color_parse("#ffffff", &bg);

        cursor = gdk_bitmap_create_from_data(NULL,
                                             (char *)GtkCursors[newType].bits,
                                             32, 32);
        if (!cursor)
            return NULL;

        mask =
            gdk_bitmap_create_from_data(NULL,
                                        (char *)GtkCursors[newType].mask_bits,
                                        32, 32);
        if (!mask) {
            g_object_unref(cursor);
            return NULL;
        }

        gdkcursor = gdk_cursor_new_from_pixmap(cursor, mask, &fg, &bg,
                                               GtkCursors[newType].hot_x,
                                               GtkCursors[newType].hot_y);

        g_object_unref(mask);
        g_object_unref(cursor);
    }

    gCursorCache[aCursor] = gdkcursor;

    return gdkcursor;
}

// gtk callbacks

/* static */
gboolean
expose_event_cb(GtkWidget *widget, GdkEventExpose *event)
{
    nsRefPtr<nsWindow> window = get_window_for_gdk_window(event->window);
    if (!window)
        return FALSE;

    // XXX We are so getting lucky here.  We are doing all of
    // mozilla's painting and then allowing default processing to occur.
    // This means that Mozilla paints in all of it's stuff and then
    // NO_WINDOW widgets (like scrollbars, for example) are painted by
    // Gtk on top of what we painted.

    // This return window->OnExposeEvent(widget, event); */

    window->OnExposeEvent(widget, event);
    return FALSE;
}

/* static */
gboolean
configure_event_cb(GtkWidget *widget,
                   GdkEventConfigure *event)
{
    nsRefPtr<nsWindow> window = get_window_for_gtk_widget(widget);
    if (!window)
        return FALSE;

    return window->OnConfigureEvent(widget, event);
}

/* static */
void
container_unrealize_cb (GtkWidget *widget)
{
    nsRefPtr<nsWindow> window = get_window_for_gtk_widget(widget);
    if (!window)
        return;

    window->OnContainerUnrealize(widget);
}

/* static */
void
size_allocate_cb (GtkWidget *widget, GtkAllocation *allocation)
{
    nsRefPtr<nsWindow> window = get_window_for_gtk_widget(widget);
    if (!window)
        return;

    window->OnSizeAllocate(widget, allocation);
}

/* static */
gboolean
delete_event_cb(GtkWidget *widget, GdkEventAny *event)
{
    nsRefPtr<nsWindow> window = get_window_for_gtk_widget(widget);
    if (!window)
        return FALSE;

    window->OnDeleteEvent(widget, event);

    return TRUE;
}

/* static */
gboolean
enter_notify_event_cb(GtkWidget *widget,
                      GdkEventCrossing *event)
{
    if (is_parent_ungrab_enter(event)) {
        return TRUE;
    }

    nsRefPtr<nsWindow> window = get_window_for_gdk_window(event->window);
    if (!window)
        return TRUE;

    window->OnEnterNotifyEvent(widget, event);

    return TRUE;
}

/* static */
gboolean
leave_notify_event_cb(GtkWidget *widget,
                      GdkEventCrossing *event)
{
    if (is_parent_grab_leave(event)) {
        return TRUE;
    }

    // bug 369599: Suppress LeaveNotify events caused by pointer grabs to
    // avoid generating spurious mouse exit events.
    gint x = gint(event->x_root);
    gint y = gint(event->y_root);
    GdkDisplay* display = gtk_widget_get_display(widget);
    GdkWindow* winAtPt = gdk_display_get_window_at_pointer(display, &x, &y);
    if (winAtPt == event->window) {
        return TRUE;
    }

    nsRefPtr<nsWindow> window = get_window_for_gdk_window(event->window);
    if (!window)
        return TRUE;

    window->OnLeaveNotifyEvent(widget, event);

    return TRUE;
}

nsWindow*
GetFirstNSWindowForGDKWindow(GdkWindow *aGdkWindow)
{
    nsWindow* window;
    while (!(window = get_window_for_gdk_window(aGdkWindow))) {
        // The event has bubbled to the moz_container widget as passed into each caller's *widget parameter,
        // but its corresponding nsWindow is an ancestor of the window that we need.  Instead, look at
        // event->window and find the first ancestor nsWindow of it because event->window may be in a plugin.
        aGdkWindow = gdk_window_get_parent(aGdkWindow);
        if (!aGdkWindow) {
            window = nsnull;
            break;
        }
    }
    return window;
}

/* static */
gboolean
motion_notify_event_cb(GtkWidget *widget, GdkEventMotion *event)
{
    UpdateLastInputEventTime();

    nsWindow *window = GetFirstNSWindowForGDKWindow(event->window);
    if (!window)
        return FALSE;

    window->OnMotionNotifyEvent(widget, event);

#ifdef HAVE_GTK_MOTION_HINTS
    gdk_event_request_motions(event);
#endif
    return TRUE;
}

/* static */
gboolean
button_press_event_cb(GtkWidget *widget, GdkEventButton *event)
{
    UpdateLastInputEventTime();

    nsWindow *window = GetFirstNSWindowForGDKWindow(event->window);
    if (!window)
        return FALSE;

    window->OnButtonPressEvent(widget, event);

    return TRUE;
}

/* static */
gboolean
button_release_event_cb(GtkWidget *widget, GdkEventButton *event)
{
    UpdateLastInputEventTime();

    nsWindow *window = GetFirstNSWindowForGDKWindow(event->window);
    if (!window)
        return FALSE;

    window->OnButtonReleaseEvent(widget, event);

    return TRUE;
}

/* static */
gboolean
focus_in_event_cb(GtkWidget *widget, GdkEventFocus *event)
{
    nsRefPtr<nsWindow> window = get_window_for_gtk_widget(widget);
    if (!window)
        return FALSE;

    window->OnContainerFocusInEvent(widget, event);

    return FALSE;
}

/* static */
gboolean
focus_out_event_cb(GtkWidget *widget, GdkEventFocus *event)
{
    nsRefPtr<nsWindow> window = get_window_for_gtk_widget(widget);
    if (!window)
        return FALSE;

    window->OnContainerFocusOutEvent(widget, event);

    return FALSE;
}

#ifdef MOZ_X11
/* static */
GdkFilterReturn
plugin_window_filter_func(GdkXEvent *gdk_xevent, GdkEvent *event, gpointer data)
{
    GdkWindow *plugin_window;
    XEvent    *xevent;

    nsRefPtr<nsWindow> nswindow = (nsWindow*)data;
    GdkFilterReturn return_val;

    xevent = (XEvent *)gdk_xevent;
    return_val = GDK_FILTER_CONTINUE;

    switch (xevent->type)
    {
        case CreateNotify:
        case ReparentNotify:
            if (xevent->type==CreateNotify) {
                plugin_window = gdk_window_lookup(xevent->xcreatewindow.window);
            }
            else {
                if (xevent->xreparent.event != xevent->xreparent.parent)
                    break;
                plugin_window = gdk_window_lookup (xevent->xreparent.window);
            }
            if (plugin_window) {
                GtkWidget *widget =
                    get_gtk_widget_for_gdk_window(plugin_window);

                if (GTK_IS_XTBIN(widget)) {
                    nswindow->SetPluginType(nsWindow::PluginType_NONXEMBED);
                    break;
                }
                else if(GTK_IS_SOCKET(widget)) {
                    nswindow->SetPluginType(nsWindow::PluginType_XEMBED);
                    break;
                }
            }
            nswindow->SetPluginType(nsWindow::PluginType_NONXEMBED);
            return_val = GDK_FILTER_REMOVE;
            break;
        case EnterNotify:
            nswindow->SetNonXEmbedPluginFocus();
            break;
        case DestroyNotify:
            gdk_window_remove_filter
                ((GdkWindow*)(nswindow->GetNativeData(NS_NATIVE_WINDOW)),
                 plugin_window_filter_func,
                 nswindow);
            // Currently we consider all plugins are non-xembed and calls
            // LoseNonXEmbedPluginFocus without any checking.
            nswindow->LoseNonXEmbedPluginFocus();
            break;
        default:
            break;
    }
    return return_val;
}

/* static */
GdkFilterReturn
plugin_client_message_filter(GdkXEvent *gdk_xevent,
                             GdkEvent *event,
                             gpointer data)
{
    XEvent    *xevent;
    xevent = (XEvent *)gdk_xevent;

    GdkFilterReturn return_val;
    return_val = GDK_FILTER_CONTINUE;

    if (!gPluginFocusWindow || xevent->type!=ClientMessage) {
        return return_val;
    }

    // When WM sends out WM_TAKE_FOCUS, gtk2 will use XSetInputFocus
    // to set the focus to the focus proxy. To prevent this happen
    // while the focus is on the plugin, we filter the WM_TAKE_FOCUS
    // out.
    if (gdk_x11_get_xatom_by_name("WM_PROTOCOLS")
            != xevent->xclient.message_type) {
        return return_val;
    }

    if ((Atom) xevent->xclient.data.l[0] ==
            gdk_x11_get_xatom_by_name("WM_TAKE_FOCUS")) {
        // block it from gtk2.0 focus proxy
        return_val = GDK_FILTER_REMOVE;
    }

    return return_val;
}
#endif /* MOZ_X11 */

/* static */
gboolean
key_press_event_cb(GtkWidget *widget, GdkEventKey *event)
{
    LOG(("key_press_event_cb\n"));

    UpdateLastInputEventTime();

    // find the window with focus and dispatch this event to that widget
    nsWindow *window = get_window_for_gtk_widget(widget);
    if (!window)
        return FALSE;

    nsRefPtr<nsWindow> focusWindow = gFocusWindow ? gFocusWindow : window;

#ifdef MOZ_X11
    // Keyboard repeat can cause key press events to queue up when there are
    // slow event handlers (bug 301029).  Throttle these events by removing
    // consecutive pending duplicate KeyPress events to the same window.
    // We use the event time of the last one.
    // Note: GDK calls XkbSetDetectableAutorepeat so that KeyRelease events
    // are generated only when the key is physically released.
#define NS_GDKEVENT_MATCH_MASK 0x1FFF /* GDK_SHIFT_MASK .. GDK_BUTTON5_MASK */
    GdkDisplay* gdkDisplay = gtk_widget_get_display(widget);
    Display* dpy = GDK_DISPLAY_XDISPLAY(gdkDisplay);
    while (XPending(dpy)) {
        XEvent next_event;
        XPeekEvent(dpy, &next_event);
        GdkWindow* nextGdkWindow =
            gdk_window_lookup_for_display(gdkDisplay, next_event.xany.window);
        if (nextGdkWindow != event->window ||
            next_event.type != KeyPress ||
            next_event.xkey.keycode != event->hardware_keycode ||
            next_event.xkey.state != (event->state & NS_GDKEVENT_MATCH_MASK)) {
            break;
        }
        XNextEvent(dpy, &next_event);
        event->time = next_event.xkey.time;
    }
#endif

    return focusWindow->OnKeyPressEvent(widget, event);
}

gboolean
key_release_event_cb(GtkWidget *widget, GdkEventKey *event)
{
    LOG(("key_release_event_cb\n"));

    UpdateLastInputEventTime();

    // find the window with focus and dispatch this event to that widget
    nsWindow *window = get_window_for_gtk_widget(widget);
    if (!window)
        return FALSE;

    nsRefPtr<nsWindow> focusWindow = gFocusWindow ? gFocusWindow : window;

    return focusWindow->OnKeyReleaseEvent(widget, event);
}

/* static */
gboolean
scroll_event_cb(GtkWidget *widget, GdkEventScroll *event)
{
    nsWindow *window = GetFirstNSWindowForGDKWindow(event->window);
    if (!window)
        return FALSE;

    window->OnScrollEvent(widget, event);

    return TRUE;
}

/* static */
gboolean
visibility_notify_event_cb (GtkWidget *widget, GdkEventVisibility *event)
{
    nsRefPtr<nsWindow> window = get_window_for_gdk_window(event->window);
    if (!window)
        return FALSE;

    window->OnVisibilityNotifyEvent(widget, event);

    return TRUE;
}

static void
hierarchy_changed_cb (GtkWidget *widget,
                      GtkWidget *previous_toplevel)
{
    GtkWidget *toplevel = gtk_widget_get_toplevel(widget);
    GdkWindowState old_window_state = GDK_WINDOW_STATE_WITHDRAWN;
    GdkEventWindowState event;

    event.new_window_state = GDK_WINDOW_STATE_WITHDRAWN;

    if (GTK_IS_WINDOW(previous_toplevel)) {
        g_signal_handlers_disconnect_by_func(previous_toplevel,
                                             FuncToGpointer(window_state_event_cb),
                                             widget);
        if (previous_toplevel->window) {
            old_window_state = gdk_window_get_state(previous_toplevel->window);
        }
    }

    if (GTK_IS_WINDOW(toplevel)) {
        g_signal_connect_swapped(toplevel, "window-state-event",
                                 G_CALLBACK(window_state_event_cb), widget);
        if (toplevel->window) {
            event.new_window_state = gdk_window_get_state(toplevel->window);
        }
    }

    event.changed_mask = static_cast<GdkWindowState>
        (old_window_state ^ event.new_window_state);

    if (event.changed_mask) {
        event.type = GDK_WINDOW_STATE;
        event.window = NULL;
        event.send_event = TRUE;
        window_state_event_cb(widget, &event);
    }
}

/* static */
gboolean
window_state_event_cb (GtkWidget *widget, GdkEventWindowState *event)
{
    nsRefPtr<nsWindow> window = get_window_for_gtk_widget(widget);
    if (!window)
        return FALSE;

    window->OnWindowStateEvent(widget, event);

    return FALSE;
}

/* static */
void
theme_changed_cb (GtkSettings *settings, GParamSpec *pspec, nsWindow *data)
{
    nsRefPtr<nsWindow> window = data;
    window->ThemeChanged();
}

//////////////////////////////////////////////////////////////////////
// These are all of our drag and drop operations

void
nsWindow::InitDragEvent(nsDragEvent &aEvent)
{
    // set the keyboard modifiers
    gint x, y;
    GdkModifierType state = (GdkModifierType)0;
    gdk_window_get_pointer(NULL, &x, &y, &state);
    aEvent.isShift = (state & GDK_SHIFT_MASK) ? PR_TRUE : PR_FALSE;
    aEvent.isControl = (state & GDK_CONTROL_MASK) ? PR_TRUE : PR_FALSE;
    aEvent.isAlt = (state & GDK_MOD1_MASK) ? PR_TRUE : PR_FALSE;
    aEvent.isMeta = PR_FALSE; // GTK+ doesn't support the meta key
}

// This will update the drag action based on the information in the
// drag context.  Gtk gets this from a combination of the key settings
// and what the source is offering.

void
nsWindow::UpdateDragStatus(GdkDragContext *aDragContext,
                           nsIDragService *aDragService)
{
    // default is to do nothing
    int action = nsIDragService::DRAGDROP_ACTION_NONE;

    // set the default just in case nothing matches below
    if (aDragContext->actions & GDK_ACTION_DEFAULT)
        action = nsIDragService::DRAGDROP_ACTION_MOVE;

    // first check to see if move is set
    if (aDragContext->actions & GDK_ACTION_MOVE)
        action = nsIDragService::DRAGDROP_ACTION_MOVE;

    // then fall to the others
    else if (aDragContext->actions & GDK_ACTION_LINK)
        action = nsIDragService::DRAGDROP_ACTION_LINK;

    // copy is ctrl
    else if (aDragContext->actions & GDK_ACTION_COPY)
        action = nsIDragService::DRAGDROP_ACTION_COPY;

    // update the drag information
    nsCOMPtr<nsIDragSession> session;
    aDragService->GetCurrentSession(getter_AddRefs(session));

    if (session)
        session->SetDragAction(action);
}


/* static */
gboolean
drag_motion_event_cb(GtkWidget *aWidget,
                     GdkDragContext *aDragContext,
                     gint aX,
                     gint aY,
                     guint aTime,
                     gpointer aData)
{
    nsRefPtr<nsWindow> window = get_window_for_gtk_widget(aWidget);
    if (!window)
        return FALSE;

    return window->OnDragMotionEvent(aWidget,
                                     aDragContext,
                                     aX, aY, aTime, aData);
}
/* static */
void
drag_leave_event_cb(GtkWidget *aWidget,
                    GdkDragContext *aDragContext,
                    guint aTime,
                    gpointer aData)
{
    nsRefPtr<nsWindow> window = get_window_for_gtk_widget(aWidget);
    if (!window)
        return;

    window->OnDragLeaveEvent(aWidget, aDragContext, aTime, aData);
}


/* static */
gboolean
drag_drop_event_cb(GtkWidget *aWidget,
                   GdkDragContext *aDragContext,
                   gint aX,
                   gint aY,
                   guint aTime,
                   gpointer *aData)
{
    nsRefPtr<nsWindow> window = get_window_for_gtk_widget(aWidget);
    if (!window)
        return FALSE;

    return window->OnDragDropEvent(aWidget,
                                   aDragContext,
                                   aX, aY, aTime, aData);
}

/* static */
void
drag_data_received_event_cb(GtkWidget *aWidget,
                            GdkDragContext *aDragContext,
                            gint aX,
                            gint aY,
                            GtkSelectionData  *aSelectionData,
                            guint aInfo,
                            guint aTime,
                            gpointer aData)
{
    nsRefPtr<nsWindow> window = get_window_for_gtk_widget(aWidget);
    if (!window)
        return;

    window->OnDragDataReceivedEvent(aWidget,
                                    aDragContext,
                                    aX, aY,
                                    aSelectionData,
                                    aInfo, aTime, aData);
}

/* static */
nsresult
initialize_prefs(void)
{
    nsCOMPtr<nsIPrefBranch> prefs = do_GetService(NS_PREFSERVICE_CONTRACTID);
    if (!prefs)
        return NS_OK;

    PRBool val = PR_TRUE;
    nsresult rv;

    rv = prefs->GetBoolPref("mozilla.widget.raise-on-setfocus", &val);
    if (NS_SUCCEEDED(rv))
        gRaiseWindows = val;

    rv = prefs->GetBoolPref("mozilla.widget.force-24bpp", &val);
    if (NS_SUCCEEDED(rv))
        gForce24bpp = val;

    rv = prefs->GetBoolPref("mozilla.widget.use-buffer-pixmap", &val);
    if (NS_SUCCEEDED(rv))
        gUseBufferPixmap = val;

    rv = prefs->GetBoolPref("mozilla.widget.disable-native-theme", &val);
    if (NS_SUCCEEDED(rv))
        gDisableNativeTheme = val;

    return NS_OK;
}

void
nsWindow::ResetDragMotionTimer(GtkWidget *aWidget,
                               GdkDragContext *aDragContext,
                               gint aX, gint aY, guint aTime)
{

    // We have to be careful about ref ordering here.  if aWidget ==
    // mDraMotionWidget be careful not to let the refcnt drop to zero.
    // Same with the drag context.
    if (aWidget)
        g_object_ref(aWidget);
    if (mDragMotionWidget)
        g_object_unref(mDragMotionWidget);
    mDragMotionWidget = aWidget;

    if (aDragContext)
        g_object_ref(aDragContext);
    if (mDragMotionContext)
        g_object_unref(mDragMotionContext);
    mDragMotionContext = aDragContext;

    mDragMotionX = aX;
    mDragMotionY = aY;
    mDragMotionTime = aTime;

    // always clear the timer
    if (mDragMotionTimerID) {
        g_source_remove(mDragMotionTimerID);
        mDragMotionTimerID = 0;
        LOG(("*** canceled motion timer\n"));
    }

    // if no widget was passed in, just return instead of setting a new
    // timer
    if (!aWidget) {
        return;
    }

    // otherwise we create a new timer
    mDragMotionTimerID = g_timeout_add(100,
                                       (GtkFunction)DragMotionTimerCallback,
                                       this);
}

void
nsWindow::FireDragMotionTimer(void)
{
    LOGDRAG(("nsWindow::FireDragMotionTimer(%p)\n", (void*)this));

    OnDragMotionEvent(mDragMotionWidget, mDragMotionContext,
                      mDragMotionX, mDragMotionY, mDragMotionTime,
                      this);
}

void
nsWindow::FireDragLeaveTimer(void)
{
    LOGDRAG(("nsWindow::FireDragLeaveTimer(%p)\n", (void*)this));

    mDragLeaveTimer = nsnull;

    // clean up any pending drag motion window info
    if (mLastDragMotionWindow) {
        nsRefPtr<nsWindow> kungFuDeathGrip = mLastDragMotionWindow;
        // send our leave signal
        mLastDragMotionWindow->OnDragLeave();
        mLastDragMotionWindow = 0;
    }
}

/* static */
guint
nsWindow::DragMotionTimerCallback(gpointer aClosure)
{
    nsRefPtr<nsWindow> window = static_cast<nsWindow *>(aClosure);
    window->FireDragMotionTimer();
    return FALSE;
}

/* static */
void
nsWindow::DragLeaveTimerCallback(nsITimer *aTimer, void *aClosure)
{
    nsRefPtr<nsWindow> window = static_cast<nsWindow *>(aClosure);
    window->FireDragLeaveTimer();
}

/* static */
GdkWindow *
get_inner_gdk_window (GdkWindow *aWindow,
                      gint x, gint y,
                      gint *retx, gint *rety)
{
    gint cx, cy, cw, ch, cd;
    GList * children = gdk_window_peek_children(aWindow);
    guint num = g_list_length(children);
    for (int i = 0; i < (int)num; i++) {
        GList * child = g_list_nth(children, num - i - 1) ;
        if (child) {
            GdkWindow * childWindow = (GdkWindow *) child->data;
            gdk_window_get_geometry (childWindow, &cx, &cy, &cw, &ch, &cd);
            if ((cx < x) && (x < (cx + cw)) &&
                (cy < y) && (y < (cy + ch)) &&
                gdk_window_is_visible (childWindow)) {
                return get_inner_gdk_window (childWindow,
                                             x - cx, y - cy,
                                             retx, rety);
            }
        }
    }
    *retx = x;
    *rety = y;
    return aWindow;
}

inline PRBool
is_context_menu_key(const nsKeyEvent& aKeyEvent)
{
    return ((aKeyEvent.keyCode == NS_VK_F10 && aKeyEvent.isShift &&
             !aKeyEvent.isControl && !aKeyEvent.isMeta && !aKeyEvent.isAlt) ||
            (aKeyEvent.keyCode == NS_VK_CONTEXT_MENU && !aKeyEvent.isShift &&
             !aKeyEvent.isControl && !aKeyEvent.isMeta && !aKeyEvent.isAlt));
}

void
key_event_to_context_menu_event(nsMouseEvent &aEvent,
                                GdkEventKey *aGdkEvent)
{
    aEvent.refPoint = nsIntPoint(0, 0);
    aEvent.isShift = PR_FALSE;
    aEvent.isControl = PR_FALSE;
    aEvent.isAlt = PR_FALSE;
    aEvent.isMeta = PR_FALSE;
    aEvent.time = aGdkEvent->time;
    aEvent.clickCount = 1;
}

/* static */
int
is_parent_ungrab_enter(GdkEventCrossing *aEvent)
{
    return (GDK_CROSSING_UNGRAB == aEvent->mode) &&
        ((GDK_NOTIFY_ANCESTOR == aEvent->detail) ||
         (GDK_NOTIFY_VIRTUAL == aEvent->detail));

}

/* static */
int
is_parent_grab_leave(GdkEventCrossing *aEvent)
{
    return (GDK_CROSSING_GRAB == aEvent->mode) &&
        ((GDK_NOTIFY_ANCESTOR == aEvent->detail) ||
            (GDK_NOTIFY_VIRTUAL == aEvent->detail));
}

static GdkModifierType
gdk_keyboard_get_modifiers()
{
    GdkModifierType m = (GdkModifierType) 0;

    gdk_window_get_pointer(NULL, NULL, NULL, &m);

    return m;
}

#ifdef MOZ_X11
// Get the modifier masks for GDK_Caps_Lock, GDK_Num_Lock and GDK_Scroll_Lock.
// Return PR_TRUE on success, PR_FALSE on error.
static PRBool
gdk_keyboard_get_modmap_masks(Display*  aDisplay,
                              PRUint32* aCapsLockMask,
                              PRUint32* aNumLockMask,
                              PRUint32* aScrollLockMask)
{
    *aCapsLockMask = 0;
    *aNumLockMask = 0;
    *aScrollLockMask = 0;

    int min_keycode = 0;
    int max_keycode = 0;
    XDisplayKeycodes(aDisplay, &min_keycode, &max_keycode);

    int keysyms_per_keycode = 0;
    KeySym* xkeymap = XGetKeyboardMapping(aDisplay, min_keycode,
                                          max_keycode - min_keycode + 1,
                                          &keysyms_per_keycode);
    if (!xkeymap) {
        return PR_FALSE;
    }

    XModifierKeymap* xmodmap = XGetModifierMapping(aDisplay);
    if (!xmodmap) {
        XFree(xkeymap);
        return PR_FALSE;
    }

    /*
      The modifiermap member of the XModifierKeymap structure contains 8 sets
      of max_keypermod KeyCodes, one for each modifier in the order Shift,
      Lock, Control, Mod1, Mod2, Mod3, Mod4, and Mod5.
      Only nonzero KeyCodes have meaning in each set, and zero KeyCodes are ignored.
    */
    const unsigned int map_size = 8 * xmodmap->max_keypermod;
    for (unsigned int i = 0; i < map_size; i++) {
        KeyCode keycode = xmodmap->modifiermap[i];
        if (!keycode || keycode < min_keycode || keycode > max_keycode)
            continue;

        const KeySym* syms = xkeymap + (keycode - min_keycode) * keysyms_per_keycode;
        const unsigned int mask = 1 << (i / xmodmap->max_keypermod);
        for (int j = 0; j < keysyms_per_keycode; j++) {
            switch (syms[j]) {
                case GDK_Caps_Lock:   *aCapsLockMask |= mask;   break;
                case GDK_Num_Lock:    *aNumLockMask |= mask;    break;
                case GDK_Scroll_Lock: *aScrollLockMask |= mask; break;
            }
        }
    }

    XFreeModifiermap(xmodmap);
    XFree(xkeymap);
    return PR_TRUE;
}
#endif /* MOZ_X11 */

#ifdef ACCESSIBILITY
/**
 * void
 * nsWindow::CreateRootAccessible
 *
 * request to create the nsIAccessible Object for the toplevel window
 **/
void
nsWindow::CreateRootAccessible()
{
    if (mIsTopLevel && !mRootAccessible) {
        nsCOMPtr<nsIAccessible> acc;
        DispatchAccessibleEvent(getter_AddRefs(acc));

        if (acc) {
            mRootAccessible = acc;
        }
    }
}

void
nsWindow::GetRootAccessible(nsIAccessible** aAccessible)
{
    nsCOMPtr<nsIAccessible> accessible, parentAccessible;
    DispatchAccessibleEvent(getter_AddRefs(accessible));
    PRUint32 role;

    if (!accessible) {
        return;
    }
    while (PR_TRUE) {
        accessible->GetParent(getter_AddRefs(parentAccessible));
        if (!parentAccessible) {
            break;
        }
        parentAccessible->GetRole(&role);
        if (role == nsIAccessibleRole::ROLE_APP_ROOT) {
            NS_ADDREF(*aAccessible = accessible);
            break;
        }
        accessible = parentAccessible;
    }
}

/**
 * void
 * nsWindow::DispatchAccessibleEvent
 * @aAccessible: the out var, hold the new accessible object
 *
 * generate the NS_GETACCESSIBLE event, the event handler is
 * reponsible to create an nsIAccessible instant.
 **/
PRBool
nsWindow::DispatchAccessibleEvent(nsIAccessible** aAccessible)
{
    PRBool result = PR_FALSE;
    nsAccessibleEvent event(PR_TRUE, NS_GETACCESSIBLE, this);

    *aAccessible = nsnull;

    nsEventStatus status;
    DispatchEvent(&event, status);
    result = (nsEventStatus_eConsumeNoDefault == status) ? PR_TRUE : PR_FALSE;

    // if the event returned an accesssible get it.
    if (event.accessible)
        *aAccessible = event.accessible;

    return result;
}

void
nsWindow::DispatchActivateEventAccessible(void)
{
    if (sAccessibilityEnabled) {
        nsCOMPtr<nsIAccessible> rootAcc;
        GetRootAccessible(getter_AddRefs(rootAcc));

        nsCOMPtr<nsIAccessibilityService> accService =
            do_GetService("@mozilla.org/accessibilityService;1");

        if (accService) {
            accService->FireAccessibleEvent(
                            nsIAccessibleEvent::EVENT_WINDOW_ACTIVATE,
                            rootAcc);
        }
    }
}

void
nsWindow::DispatchDeactivateEventAccessible(void)
{
    if (sAccessibilityEnabled) {
        nsCOMPtr<nsIAccessible> rootAcc;
        GetRootAccessible(getter_AddRefs(rootAcc));

        nsCOMPtr<nsIAccessibilityService> accService =
            do_GetService("@mozilla.org/accessibilityService;1");

        if (accService) {
          accService->FireAccessibleEvent(
                          nsIAccessibleEvent::EVENT_WINDOW_DEACTIVATE,
                          rootAcc);
        }
    }
}

#endif /* #ifdef ACCESSIBILITY */

// nsChildWindow class

nsChildWindow::nsChildWindow()
{
}

nsChildWindow::~nsChildWindow()
{
}

#ifdef USE_XIM

void
nsWindow::IMEInitData(void)
{
    if (mIMEData)
        return;
    nsWindow *win = GetContainerWindow();
    if (!win)
        return;
    mIMEData = win->mIMEData;
    if (!mIMEData)
        return;
    mIMEData->mRefCount++;
}

void
nsWindow::IMEReleaseData(void)
{
    if (!mIMEData)
        return;

    mIMEData->mRefCount--;
    if (mIMEData->mRefCount != 0)
        return;

    delete mIMEData;
    mIMEData = nsnull;
}

// Work around gtk bug http://bugzilla.gnome.org/show_bug.cgi?id=483223:
// (and the similar issue of GTK+ IIIM)
// The GTK+ XIM and IIIM modules register handlers for the "closed" signal
// on the display, but:
//  * The signal handlers are not disconnected when the module is unloaded.
//
// The GTK+ XIM module has another problem:
//  * When the signal handler is run (with the module loaded) it tries
//    XFree (and fails) on a pointer that did not come from Xmalloc.
//
// To prevent these modules from being unloaded, use static variables to
// hold ref of GtkIMContext class.
// For GTK+ XIM module, to prevent the signal handler from being run,
// find the signal handlers and remove them.
//
// GtkIMContextXIMs share XOpenIM connections and display closed signal
// handlers (where possible).

static void workaround_gtk_im_display_closed(GtkWidget *aGtkWidget,
                                             GtkIMContext *aContext)
{
    GtkIMMulticontext *multicontext = GTK_IM_MULTICONTEXT (aContext);
    GtkIMContext *slave = multicontext->slave;
    if (!slave)
        return;

    GType slaveType = G_TYPE_FROM_INSTANCE(slave);
    const gchar *im_type_name = g_type_name(slaveType);
    if (strcmp(im_type_name, "GtkIMContextXIM") == 0) {
        if (gtk_check_version(2, 12, 1) == NULL) // gtk bug has been fixed
            return;

        struct GtkIMContextXIM
        {
            GtkIMContext parent;
            gpointer private_data;
            // ... other fields
        };
        gpointer signal_data =
            reinterpret_cast<GtkIMContextXIM*>(slave)->private_data;
        if (!signal_data)
            return; // no corresponding handler

        g_signal_handlers_disconnect_matched(gtk_widget_get_display(aGtkWidget),
                                             G_SIGNAL_MATCH_DATA, 0, 0, NULL,
                                             NULL, signal_data);

        // Add a reference to prevent the XIM module from being unloaded
        // and reloaded: each time the module is loaded and used, it
        // opens (and doesn't close) new XOpenIM connections.
        static gpointer gtk_xim_context_class =
            g_type_class_ref(slaveType);
        // Mute unused variable warning:
        gtk_xim_context_class = gtk_xim_context_class;
    }
    else if (strcmp(im_type_name, "GtkIMContextIIIM") == 0) {
        // Add a reference to prevent the IIIM module from being unloaded
        static gpointer gtk_iiim_context_class =
            g_type_class_ref(slaveType);
        // Mute unused variable warning:
        gtk_iiim_context_class = gtk_iiim_context_class;
    }
}

void
nsWindow::IMEDestroyContext(void)
{
    if (!mIMEData || mIMEData->mOwner != this) {
        // This nsWindow is not the owner of the instance of mIMEData.
        if (IMEComposingWindow() == this)
            CancelIMEComposition();
        if (gIMEFocusWindow == this)
            gIMEFocusWindow = nsnull;
        IMEReleaseData();
        return;
    }

    /* NOTE:
     * This nsWindow is the owner of the instance of mIMEData,
     * so, we must release the contexts now. But that might be referred from
     * other nsWindows.(They are children of this. But we don't know why there
     * are the cases.) So, we need to clear the pointers that refers to contexts
     * and this if the other referrers are still alive. See bug 349727.
     */

    // If this is the focus window and we have an IM context we need
    // to unset the focus on this window before we destroy the window.
    GtkIMContext *im = IMEGetContext();
    if (im && gIMEFocusWindow && gIMEFocusWindow->IMEGetContext() == im) {
        gIMEFocusWindow->IMELoseFocus();
        gIMEFocusWindow = nsnull;
    }

    mIMEData->mOwner   = nsnull;
    mIMEData->mEnabled = nsIWidget::IME_STATUS_DISABLED;

    if (mIMEData->mContext) {
        workaround_gtk_im_display_closed(GTK_WIDGET(mContainer),
                                         mIMEData->mContext);
        gtk_im_context_set_client_window(mIMEData->mContext, nsnull);
        g_object_unref(G_OBJECT(mIMEData->mContext));
        mIMEData->mContext = nsnull;
    }

    if (mIMEData->mSimpleContext) {
        gtk_im_context_set_client_window(mIMEData->mSimpleContext, nsnull);
        g_object_unref(G_OBJECT(mIMEData->mSimpleContext));
        mIMEData->mSimpleContext = nsnull;
    }

    if (mIMEData->mDummyContext) {
        // mIMEData->mContext and mIMEData->mDummyContext have the same
        // slaveType and signal_data so no need for another
        // workaround_gtk_im_display_closed.
        gtk_im_context_set_client_window(mIMEData->mDummyContext, nsnull);
        g_object_unref(G_OBJECT(mIMEData->mDummyContext));
        mIMEData->mDummyContext = nsnull;
    }

    IMEReleaseData();
}

void
nsWindow::IMESetFocus(void)
{
    IMEInitData();

    LOGIM(("IMESetFocus %p\n", (void *)this));
    GtkIMContext *im = IMEGetContext();
    if (!im)
        return;

    gtk_im_context_focus_in(im);
    gIMEFocusWindow = this;

    if (!IMEIsEnabledState()) {
        // We should release IME focus for uim and scim.
        // These IMs are using snooper that is released at losing focus.
        IMELoseFocus();
    }
}

void
nsWindow::IMELoseFocus(void)
{
    LOGIM(("IMELoseFocus %p\n", (void *)this));
    GtkIMContext *im = IMEGetContext();
    if (!im)
        return;

    gtk_im_context_focus_out(im);
}

void
nsWindow::IMEComposeStart(void)
{
    LOGIM(("IMEComposeStart [%p]\n", (void *)this));

    if (!mIMEData) {
        NS_ERROR("This widget doesn't support IM");
        return;
    }

    if (IMEComposingWindow()) {
        NS_WARNING("tried to re-start text composition\n");
        return;
    }

    mIMEData->mComposingWindow = this;

    nsCompositionEvent compEvent(PR_TRUE, NS_COMPOSITION_START, this);

    nsEventStatus status;
    DispatchEvent(&compEvent, status);

    if (NS_UNLIKELY(mIsDestroyed))
        return;

    IMESetCursorPosition(compEvent.theReply);
}

void
nsWindow::IMEComposeText(const PRUnichar *aText,
                         const PRInt32 aLen,
                         const gchar *aPreeditString,
                         const gint aCursorPos,
                         const PangoAttrList *aFeedback)
{
    if (!mIMEData) {
        NS_ERROR("This widget doesn't support IM");
        return;
    }

    // Send our start composition event if we need to
    if (!IMEComposingWindow()) {
        IMEComposeStart();
        if (NS_UNLIKELY(mIsDestroyed))
            return;
    }


    LOGIM(("IMEComposeText\n"));
    nsTextEvent textEvent(PR_TRUE, NS_TEXT_TEXT, this);

    if (aLen != 0) {
        textEvent.theText = (PRUnichar*)aText;

        if (aPreeditString && aFeedback && (aLen > 0)) {
            IM_set_text_range(aLen, aPreeditString, aCursorPos, aFeedback,
                              &(textEvent.rangeCount),
                              &(textEvent.rangeArray));
        }
    }

    nsEventStatus status;
    DispatchEvent(&textEvent, status);

    if (textEvent.rangeArray) {
        delete[] textEvent.rangeArray;
    }

    if (NS_UNLIKELY(mIsDestroyed))
        return;

    IMESetCursorPosition(textEvent.theReply);
}

void
nsWindow::IMEComposeEnd(void)
{
    LOGIM(("IMEComposeEnd [%p]\n", (void *)this));
    NS_ASSERTION(mIMEData, "This widget doesn't support IM");

    if (!IMEComposingWindow()) {
        NS_WARNING("tried to end text composition before it was started");
        return;
    }

    mIMEData->mComposingWindow = nsnull;

    nsCompositionEvent compEvent(PR_TRUE, NS_COMPOSITION_END, this);

    nsEventStatus status;
    DispatchEvent(&compEvent, status);
}

GtkIMContext*
nsWindow::IMEGetContext()
{
    return IM_get_input_context(this);
}

static PRBool
IsIMEEnabledState(PRUint32 aState)
{
#ifdef MOZ_PLATFORM_MAEMO
    return aState == nsIWidget::IME_STATUS_ENABLED ||
           aState == nsIWidget::IME_STATUS_PLUGIN  ||
           aState == nsIWidget::IME_STATUS_PASSWORD;
#else
    return aState == nsIWidget::IME_STATUS_ENABLED ||
           aState == nsIWidget::IME_STATUS_PLUGIN;
#endif
}

PRBool
nsWindow::IMEIsEnabledState(void)
{
    return mIMEData ? IsIMEEnabledState(mIMEData->mEnabled) : PR_FALSE;
}

static PRBool
IsIMEEditableState(PRUint32 aState)
{
    return aState == nsIWidget::IME_STATUS_ENABLED ||
           aState == nsIWidget::IME_STATUS_PLUGIN ||
           aState == nsIWidget::IME_STATUS_PASSWORD;
}

PRBool
nsWindow::IMEIsEditableState(void)
{
    return mIMEData ? IsIMEEditableState(mIMEData->mEnabled) : PR_FALSE;
}

nsWindow*
nsWindow::IMEComposingWindow(void)
{
    return mIMEData ? mIMEData->mComposingWindow : nsnull;
}

void
nsWindow::IMECreateContext(void)
{
    mIMEData = new nsIMEData(this);
    if (!mIMEData)
        return;

    mIMEData->mContext = gtk_im_multicontext_new();
    mIMEData->mSimpleContext = gtk_im_context_simple_new();
    mIMEData->mDummyContext = gtk_im_multicontext_new();
    if (!mIMEData->mContext || !mIMEData->mSimpleContext ||
        !mIMEData->mDummyContext) {
        NS_ERROR("failed to create IM context.");
        IMEDestroyContext();
        return;
    }

    gtk_im_context_set_client_window(mIMEData->mContext,
                                     GTK_WIDGET(mContainer)->window);
    gtk_im_context_set_client_window(mIMEData->mSimpleContext,
                                     GTK_WIDGET(mContainer)->window);
    gtk_im_context_set_client_window(mIMEData->mDummyContext,
                                     GTK_WIDGET(mContainer)->window);

    g_signal_connect(G_OBJECT(mIMEData->mContext), "preedit_changed",
                     G_CALLBACK(IM_preedit_changed_cb), this);
    g_signal_connect(G_OBJECT(mIMEData->mContext), "commit",
                     G_CALLBACK(IM_commit_cb), this);
    g_signal_connect(G_OBJECT(mIMEData->mSimpleContext), "preedit_changed",
                     G_CALLBACK(IM_preedit_changed_cb), this);
    g_signal_connect(G_OBJECT(mIMEData->mSimpleContext), "commit",
                     G_CALLBACK(IM_commit_cb), this);
}

PRBool
nsWindow::IMEFilterEvent(GdkEventKey *aEvent)
{
    if (!IMEIsEditableState())
        return FALSE;

    GtkIMContext *im = IMEGetContext();
    if (!im)
        return FALSE;

    gKeyEvent = aEvent;
    gboolean filtered = gtk_im_context_filter_keypress(im, aEvent);
    gKeyEvent = NULL;

    LOGIM(("key filtered: %d committed: %d changed: %d\n",
           filtered, gKeyEventCommitted, gKeyEventChanged));

    // We filter the key event if the event was not committed (because
    // it's probably part of a composition) or if the key event was
    // committed _and_ changed.  This way we still let key press
    // events go through as simple key press events instead of
    // composed characters.

    PRBool retval = PR_FALSE;
    if (filtered &&
        (!gKeyEventCommitted || (gKeyEventCommitted && gKeyEventChanged)))
        retval = PR_TRUE;

    gKeyEventChanged = PR_FALSE;
    gKeyEventCommitted = PR_FALSE;
    gKeyEventChanged = PR_FALSE;

    return retval;
}

void
nsWindow::IMESetCursorPosition(const nsTextEventReply& aReply)
{
    nsIWidget *refWidget = aReply.mReferenceWidget;
    if (!refWidget) {
        NS_WARNING("mReferenceWidget is null");
        refWidget = this;
    }
    nsWindow* refWindow = static_cast<nsWindow*>(refWidget);

    nsWindow* ownerWindow = GetContainerWindow();
    if (!ownerWindow) {
        NS_ERROR("there is no owner");
        return;
    }

    // Get the position of the refWindow in screen.
    gint refX, refY;
    gdk_window_get_origin(refWindow->mGdkWindow, &refX, &refY);

    // Get the position of IM context owner window in screen.
    gint ownerX, ownerY;
    gdk_window_get_origin(ownerWindow->mGdkWindow, &ownerX, &ownerY);

    // Compute the caret position in the IM owner window.
    GdkRectangle area;
    area.x = aReply.mCursorPosition.x + refX - ownerX;
    area.y = aReply.mCursorPosition.y + refY - ownerY;
    area.width  = 0;
    area.height = aReply.mCursorPosition.height;

    gtk_im_context_set_cursor_location(IMEGetContext(), &area);
}

NS_IMETHODIMP
nsWindow::ResetInputState()
{
    IMEInitData();

    nsRefPtr<nsWindow> win = IMEComposingWindow();
    if (win) {
        GtkIMContext *im = IMEGetContext();
        if (!im)
            return NS_OK;

        gchar *preedit_string;
        gint cursor_pos;
        PangoAttrList *feedback_list;
        gtk_im_context_get_preedit_string(im, &preedit_string,
                                          &feedback_list, &cursor_pos);
        if (preedit_string && *preedit_string) {
            IM_commit_cb_internal(preedit_string, win);
            g_free(preedit_string);
        }
        if (feedback_list)
            pango_attr_list_unref(feedback_list);
    }

    CancelIMEComposition();

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::SetIMEOpenState(PRBool aState)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWindow::GetIMEOpenState(PRBool* aState)
{
    return NS_ERROR_NOT_IMPLEMENTED;
}

NS_IMETHODIMP
nsWindow::SetIMEEnabled(PRUint32 aState)
{
    IMEInitData();
    if (!mIMEData)
        return NS_OK;

    if (aState == mIMEData->mEnabled)
        return NS_OK;

    GtkIMContext *focusedIm = nsnull;
    // XXX Don't we need to check gFocusWindow?
    nsRefPtr<nsWindow> focusedWin = gIMEFocusWindow;
    if (focusedWin && focusedWin->mIMEData)
        focusedIm = focusedWin->mIMEData->mContext;

    if (focusedIm && focusedIm == mIMEData->mContext) {
        // Release current IME focus if IME is enabled.
        if (IsIMEEditableState(mIMEData->mEnabled)) {
            focusedWin->ResetInputState();
            focusedWin->IMELoseFocus();
        }

        mIMEData->mEnabled = aState;

        // Even when aState is not PR_TRUE, we need to set IME focus.
        // Because some IMs are updating the status bar of them in this time.
        focusedWin->IMESetFocus();
#ifdef MOZ_PLATFORM_MAEMO
        if (mIMEData->mEnabled) {
            // It is not desired that the hildon's autocomplete mechanism displays
            // user previous entered passwds, so lets make completions invisible
            // in these cases.
            int mode;
            g_object_get (G_OBJECT(IMEGetContext()), "hildon-input-mode", &mode, NULL);

            if (mIMEData->mEnabled == nsIWidget::IME_STATUS_ENABLED ||
                mIMEData->mEnabled == nsIWidget::IME_STATUS_PLUGIN)
                mode &= ~HILDON_GTK_INPUT_MODE_INVISIBLE;
            else if (mIMEData->mEnabled == nsIWidget::IME_STATUS_PASSWORD)
               mode |= HILDON_GTK_INPUT_MODE_INVISIBLE;

            // Turn off auto-capitalization for editboxes
            mode &= ~HILDON_GTK_INPUT_MODE_AUTOCAP;

            g_object_set (G_OBJECT(IMEGetContext()), "hildon-input-mode", (HildonGtkInputMode)mode, NULL);
            gIMEVirtualKeyboardOpened = PR_TRUE;
            hildon_gtk_im_context_show (IMEGetContext());
        } else {
            gIMEVirtualKeyboardOpened = PR_FALSE;
            hildon_gtk_im_context_hide (IMEGetContext());
        }
        nsCOMPtr<nsIObserverService> observerService = do_GetService("@mozilla.org/observer-service;1");
        if (observerService) {
            nsAutoString rectBuf;
            PRInt32 x, y, w, h;
            gdk_window_get_position(mGdkWindow, &x, &y);
            gdk_window_get_size(mGdkWindow, &w, &h);
            rectBuf.Assign(NS_LITERAL_STRING("{\"left\": "));
            rectBuf.AppendInt(x);
            rectBuf.Append(NS_LITERAL_STRING(" \"top\": "));
            rectBuf.AppendInt(y);
            rectBuf.Append(NS_LITERAL_STRING(", \"right\": "));
            rectBuf.AppendInt(w);
            rectBuf.Append(NS_LITERAL_STRING(", \"bottom\": "));
            rectBuf.AppendInt(h);
            rectBuf.Append(NS_LITERAL_STRING("}"));
            observerService->NotifyObservers(nsnull, "softkb-change", rectBuf.get());
        }
#endif
    } else {
        if (IsIMEEditableState(mIMEData->mEnabled))
            ResetInputState();
        mIMEData->mEnabled = aState;
    }

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::GetIMEEnabled(PRUint32* aState)
{
    NS_ENSURE_ARG_POINTER(aState);

    IMEInitData();

    *aState =
      mIMEData ? mIMEData->mEnabled : nsIWidget::IME_STATUS_DISABLED;
    return NS_OK;
}

NS_IMETHODIMP
nsWindow::CancelIMEComposition()
{
    IMEInitData();

    GtkIMContext *im = IMEGetContext();
    if (!im)
        return NS_OK;

    NS_ASSERTION(!gIMESuppressCommit,
                 "CancelIMEComposition is already called!");
    gIMESuppressCommit = PR_TRUE;
    gtk_im_context_reset(im);
    gIMESuppressCommit = PR_FALSE;

    nsRefPtr<nsWindow> win = IMEComposingWindow();
    if (win) {
        win->IMEComposeText(nsnull, 0, nsnull, 0, nsnull);
        win->IMEComposeEnd();
    }

    return NS_OK;
}

NS_IMETHODIMP
nsWindow::GetToggledKeyState(PRUint32 aKeyCode, PRBool* aLEDState)
{
    NS_ENSURE_ARG_POINTER(aLEDState);

#ifdef MOZ_X11

    GdkModifierType modifiers = gdk_keyboard_get_modifiers();
    PRUint32 capsLockMask, numLockMask, scrollLockMask;
    PRBool foundMasks = gdk_keyboard_get_modmap_masks(
                          GDK_WINDOW_XDISPLAY(mGdkWindow),
                          &capsLockMask, &numLockMask, &scrollLockMask);
    if (!foundMasks)
        return NS_ERROR_NOT_IMPLEMENTED;

    PRUint32 mask = 0;
    switch (aKeyCode) {
        case NS_VK_CAPS_LOCK:   mask = capsLockMask;   break;
        case NS_VK_NUM_LOCK:    mask = numLockMask;    break;
        case NS_VK_SCROLL_LOCK: mask = scrollLockMask; break;
    }
    if (mask == 0)
        return NS_ERROR_NOT_IMPLEMENTED;

    *aLEDState = (modifiers & mask) != 0;
    return NS_OK;
#else
    return NS_ERROR_NOT_IMPLEMENTED;
#endif /* MOZ_X11 */
}

/* static */
void
IM_preedit_changed_cb(GtkIMContext *aContext,
                      nsWindow     *aWindow)
{
    gchar *preedit_string;
    gint cursor_pos;
    PangoAttrList *feedback_list;

    // if gFocusWindow is null, use the last focused gIMEFocusWindow
    nsRefPtr<nsWindow> window = gFocusWindow ? gFocusWindow : gIMEFocusWindow;
    if (!window || IM_get_input_context(window) != aContext)
        return;

    // Should use cursor_pos ?
    // Of course!!!
    gtk_im_context_get_preedit_string(aContext, &preedit_string,
                                      &feedback_list, &cursor_pos);

    LOGIM(("preedit string is: %s   length is: %d\n",
           preedit_string, strlen(preedit_string)));

    if (!preedit_string || !*preedit_string) {
        LOGIM(("preedit ended\n"));
        window->IMEComposeText(NULL, 0, NULL, 0, NULL);
        window->IMEComposeEnd();
        return;
    }

    LOGIM(("preedit len %d\n", strlen(preedit_string)));

    gunichar2 * uniStr;
    glong uniStrLen;

    uniStr = NULL;
    uniStrLen = 0;
    uniStr = g_utf8_to_utf16(preedit_string, -1, NULL, &uniStrLen, NULL);

    if (!uniStr) {
        g_free(preedit_string);
        LOG(("utf8-utf16 string tranfer failed!\n"));
        if (feedback_list)
            pango_attr_list_unref(feedback_list);
        return;
    }

    if (uniStrLen) {
        window->IMEComposeText(static_cast<const PRUnichar *>(uniStr),
                               uniStrLen, preedit_string, cursor_pos, feedback_list);
    }

    g_free(preedit_string);
    g_free(uniStr);

    if (feedback_list)
        pango_attr_list_unref(feedback_list);
}

/* static */
void
IM_commit_cb(GtkIMContext *aContext,
             const gchar  *aUtf8_str,
             nsWindow     *aWindow)
{
    if (gIMESuppressCommit)
        return;

    LOGIM(("IM_commit_cb\n"));

    gKeyEventCommitted = PR_TRUE;

    // if gFocusWindow is null, use the last focused gIMEFocusWindow
    nsRefPtr<nsWindow> window = gFocusWindow ? gFocusWindow : gIMEFocusWindow;

    if (!window || IM_get_input_context(window) != aContext)
        return;

    /* If IME doesn't change they keyevent that generated this commit,
       don't send it through XIM - just send it as a normal key press
       event. */

    if (gKeyEvent) {
        char keyval_utf8[8]; /* should have at least 6 bytes of space */
        gint keyval_utf8_len;
        guint32 keyval_unicode;

        keyval_unicode = gdk_keyval_to_unicode(gKeyEvent->keyval);
        keyval_utf8_len = g_unichar_to_utf8(keyval_unicode, keyval_utf8);
        keyval_utf8[keyval_utf8_len] = '\0';

        if (!strcmp(aUtf8_str, keyval_utf8)) {
            gKeyEventChanged = PR_FALSE;
            return;
        }
    }

    gKeyEventChanged = PR_TRUE;
    IM_commit_cb_internal(aUtf8_str, window);
}

/* static */
void
IM_commit_cb_internal(const gchar  *aUtf8_str,
                      nsWindow     *aWindow)
{
    gunichar2 *uniStr = nsnull;
    glong uniStrLen = 0;
    uniStr = g_utf8_to_utf16(aUtf8_str, -1, NULL, &uniStrLen, NULL);

    if (!uniStr) {
        LOGIM(("utf80utf16 string tranfer failed!\n"));
        return;
    }

    if (uniStrLen) {
        aWindow->IMEComposeText((const PRUnichar *)uniStr,
                                (PRInt32)uniStrLen, nsnull, 0, nsnull);
        aWindow->IMEComposeEnd();
    }

    g_free(uniStr);
}

#define	START_OFFSET(I)	\
    (*aTextRangeListResult)[I].mStartOffset

#define	END_OFFSET(I) \
    (*aTextRangeListResult)[I].mEndOffset

#define	SET_FEEDBACKTYPE(I,T) (*aTextRangeListResult)[I].mRangeType = T

/* static */
void
IM_set_text_range(const PRInt32 aLen,
                  const gchar *aPreeditString,
                  const gint aCursorPos,
                  const PangoAttrList *aFeedback,
                  PRUint32 *aTextRangeListLengthResult,
                  nsTextRangeArray *aTextRangeListResult)
{
    if (aLen == 0) {
        aTextRangeListLengthResult = 0;
        aTextRangeListResult = NULL;
        return;
    }

    PangoAttrIterator * aFeedbackIterator;
    aFeedbackIterator = pango_attr_list_get_iterator((PangoAttrList*)aFeedback);
    //(reinterpret_cast<PangoAttrList*>(aFeedback));
    // Since some compilers don't permit this casting -- from const to un-const
    if (aFeedbackIterator == NULL) return;

    /*
     * Don't use pango_attr_iterator_range, it'll mislead you.
     * As for going through the list to get the attribute number,
     * it take much time for performance.
     * Now it's simple and enough, although maybe waste a little space.
    */
    PRInt32 aMaxLenOfTextRange;
    aMaxLenOfTextRange = 2*aLen + 1;
    *aTextRangeListResult = new nsTextRange[aMaxLenOfTextRange];
    NS_ASSERTION(*aTextRangeListResult, "No enough memory.");

    // Set caret's postion
    SET_FEEDBACKTYPE(0, NS_TEXTRANGE_CARETPOSITION);
    START_OFFSET(0) = aCursorPos;
    END_OFFSET(0) = aCursorPos;

    int count = 0;
    PangoAttribute * aPangoAttr;
    PangoAttribute * aPangoAttrReverse, * aPangoAttrUnderline;
    /*
     * Depend on gtk2's implementation on XIM support.
     * In aFeedback got from gtk2, there are only three types of data:
     * PANGO_ATTR_UNDERLINE, PANGO_ATTR_FOREGROUND, PANGO_ATTR_BACKGROUND.
     * Corresponding to XIMUnderline, XIMReverse.
     * Don't take PANGO_ATTR_BACKGROUND into account, since
     * PANGO_ATTR_BACKGROUND and PANGO_ATTR_FOREGROUND are always
     * a couple.
     */
    gint start, end;
    gunichar2 * uniStr;
    glong uniStrLen;
    do {
        aPangoAttrUnderline = pango_attr_iterator_get(aFeedbackIterator,
                                                      PANGO_ATTR_UNDERLINE);
        aPangoAttrReverse = pango_attr_iterator_get(aFeedbackIterator,
                                                    PANGO_ATTR_FOREGROUND);
        if (!aPangoAttrUnderline && !aPangoAttrReverse)
            continue;

        // Get the range of the current attribute(s)
        pango_attr_iterator_range(aFeedbackIterator, &start, &end);

        PRUint32 feedbackType = 0;
        // XIMReverse | XIMUnderline
        if (aPangoAttrUnderline && aPangoAttrReverse) {
            feedbackType = NS_TEXTRANGE_SELECTEDCONVERTEDTEXT;
            // Doesn't matter which attribute we use here since we
            // are using pango_attr_iterator_range to determine the
            // range of the attributes.
            aPangoAttr = aPangoAttrUnderline;
        }
        // XIMUnderline
        else if (aPangoAttrUnderline) {
            feedbackType = NS_TEXTRANGE_CONVERTEDTEXT;
            aPangoAttr = aPangoAttrUnderline;
        }
        // XIMReverse
        else if (aPangoAttrReverse) {
            feedbackType = NS_TEXTRANGE_SELECTEDRAWTEXT;
            aPangoAttr = aPangoAttrReverse;
        }

        count++;
        START_OFFSET(count) = 0;
        END_OFFSET(count) = 0;

        uniStr = NULL;
        if (start > 0) {
            uniStr = g_utf8_to_utf16(aPreeditString, start,
                                     NULL, &uniStrLen, NULL);
        }
        if (uniStr) {
            START_OFFSET(count) = uniStrLen;
            g_free(uniStr);
        }

        uniStr = NULL;
        uniStr = g_utf8_to_utf16(aPreeditString + start, end - start,
                                 NULL, &uniStrLen, NULL);
        if (uniStr) {
            END_OFFSET(count) = START_OFFSET(count) + uniStrLen;
            SET_FEEDBACKTYPE(count, feedbackType);
            g_free(uniStr);
        }

    } while ((count < aMaxLenOfTextRange - 1) &&
             (pango_attr_iterator_next(aFeedbackIterator)));

   *aTextRangeListLengthResult = count + 1;

   pango_attr_iterator_destroy(aFeedbackIterator);
}

/* static */
GtkIMContext *
IM_get_input_context(nsWindow *aWindow)
{
    if (!aWindow)
        return nsnull;
    nsWindow::nsIMEData *data = aWindow->mIMEData;
    if (!data)
        return nsnull;
    if (data->mEnabled == nsIWidget::IME_STATUS_ENABLED ||
        data->mEnabled == nsIWidget::IME_STATUS_PLUGIN)
        return data->mContext;
    if (data->mEnabled == nsIWidget::IME_STATUS_PASSWORD)
#ifdef MOZ_PLATFORM_MAEMO
        return data->mContext;
#else
        return data->mSimpleContext;
#endif
    return data->mDummyContext;
}

#endif

#ifdef MOZ_X11
/* static */ already_AddRefed<gfxASurface>
nsWindow::GetSurfaceForGdkDrawable(GdkDrawable* aDrawable,
                                   const nsIntSize& aSize)
{
    GdkVisual* visual = gdk_drawable_get_visual(aDrawable);
    Display* xDisplay = gdk_x11_drawable_get_xdisplay(aDrawable);
    Drawable xDrawable = gdk_x11_drawable_get_xid(aDrawable);

    gfxASurface* result = nsnull;

    if (visual) {
        Visual* xVisual = gdk_x11_visual_get_xvisual(visual);

        result = new gfxXlibSurface(xDisplay, xDrawable, xVisual,
                                    gfxIntSize(aSize.width, aSize.height));
    } else {
        // no visual? we must be using an xrender format.  Find a format
        // for this depth.
        XRenderPictFormat *pf = NULL;
        switch (gdk_drawable_get_depth(aDrawable)) {
            case 32:
                pf = XRenderFindStandardFormat(xDisplay, PictStandardARGB32);
                break;
            case 24:
                pf = XRenderFindStandardFormat(xDisplay, PictStandardRGB24);
                break;
            default:
                NS_ERROR("Don't know how to handle the given depth!");
                break;
        }

        result = new gfxXlibSurface(xDisplay, xDrawable, pf,
                                    gfxIntSize(aSize.width, aSize.height));
    }

    NS_IF_ADDREF(result);
    return result;
}
#endif

// return the gfxASurface for rendering to this widget
gfxASurface*
nsWindow::GetThebesSurface()
{
    if (!mGdkWindow)
        return nsnull;

    GdkDrawable* d;
    gint x_offset, y_offset;
    gdk_window_get_internal_paint_info(mGdkWindow, &d, &x_offset, &y_offset);

#ifdef MOZ_X11
    gint width, height;
    gdk_drawable_get_size(d, &width, &height);
    // Owen Taylor says this is the right thing to do!
    width = PR_MIN(32767, width);
    height = PR_MIN(32767, height);

    mThebesSurface = new gfxXlibSurface
        (GDK_WINDOW_XDISPLAY(d),
         GDK_WINDOW_XWINDOW(d),
         GDK_VISUAL_XVISUAL(gdk_drawable_get_visual(d)),
         gfxIntSize(width, height));
#endif
#ifdef MOZ_DFB
    mThebesSurface = new gfxDirectFBSurface(gdk_directfb_surface_lookup(d));
#endif

    // if the surface creation is reporting an error, then
    // we don't have a surface to give back
    if (mThebesSurface && mThebesSurface->CairoStatus() != 0) {
        mThebesSurface = nsnull;
    } else {
        mThebesSurface->SetDeviceOffset(gfxPoint(-x_offset, -y_offset));
    }

    return mThebesSurface;
}

NS_IMETHODIMP
nsWindow::BeginResizeDrag(nsGUIEvent* aEvent, PRInt32 aHorizontal, PRInt32 aVertical)
{
    NS_ENSURE_ARG_POINTER(aEvent);

    if (aEvent->eventStructType != NS_MOUSE_EVENT) {
      // you can only begin a resize drag with a mouse event
      return NS_ERROR_INVALID_ARG;
    }

    nsMouseEvent* mouse_event = static_cast<nsMouseEvent*>(aEvent);

    if (mouse_event->button != nsMouseEvent::eLeftButton) {
      // you can only begin a resize drag with the left mouse button
      return NS_ERROR_INVALID_ARG;
    }

    // work out what GdkWindowEdge we're talking about
    GdkWindowEdge window_edge;
    if (aVertical < 0) {
        if (aHorizontal < 0) {
            window_edge = GDK_WINDOW_EDGE_NORTH_WEST;
        } else if (aHorizontal == 0) {
            window_edge = GDK_WINDOW_EDGE_NORTH;
        } else {
            window_edge = GDK_WINDOW_EDGE_NORTH_EAST;
        }
    } else if (aVertical == 0) {
        if (aHorizontal < 0) {
            window_edge = GDK_WINDOW_EDGE_WEST;
        } else if (aHorizontal == 0) {
            return NS_ERROR_INVALID_ARG;
        } else {
            window_edge = GDK_WINDOW_EDGE_EAST;
        }
    } else {
        if (aHorizontal < 0) {
            window_edge = GDK_WINDOW_EDGE_SOUTH_WEST;
        } else if (aHorizontal == 0) {
            window_edge = GDK_WINDOW_EDGE_SOUTH;
        } else {
            window_edge = GDK_WINDOW_EDGE_SOUTH_EAST;
        }
    }

    // get the gdk window for this widget
    GdkWindow* gdk_window = mGdkWindow;
    if (!GDK_IS_WINDOW(gdk_window)) {
      return NS_ERROR_FAILURE;
    }

    // find the top-level window
    gdk_window = gdk_window_get_toplevel(gdk_window);
    if (!GDK_IS_WINDOW(gdk_window)) {
      return NS_ERROR_FAILURE;
    }


    // get the current (default) display
    GdkDisplay* display = gdk_display_get_default();
    if (!GDK_IS_DISPLAY(display)) {
      return NS_ERROR_FAILURE;
    }

    // get the current pointer position and button state
    GdkScreen* screen = NULL;
    gint screenX, screenY;
    GdkModifierType mask;
    gdk_display_get_pointer(display, &screen, &screenX, &screenY, &mask);

    // we only support resizing with button 1
    if (!(mask & GDK_BUTTON1_MASK)) {
        return NS_ERROR_FAILURE;
    }

    // tell the window manager to start the resize
    gdk_window_begin_resize_drag(gdk_window, window_edge, 1,
            screenX, screenY, aEvent->time);

    return NS_OK;
}
