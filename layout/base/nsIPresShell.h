/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
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
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Steve Clark <buster@netscape.com>
 *   Dan Rosen <dr@netscape.com>
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
 * ***** END LICENSE BLOCK *****
 *
 * This Original Code has been modified by IBM Corporation.
 * Modifications made by IBM described herein are
 * Copyright (c) International Business Machines
 * Corporation, 2000
 *
 * Modifications to Mozilla code or documentation
 * identified per MPL Section 3.3
 *
 * Date         Modified by     Description of modification
 * 05/03/2000   IBM Corp.       Observer related defines for reflow
 */

/* a presentation of a document, part 2 */

#ifndef nsIPresShell_h___
#define nsIPresShell_h___

#include "nsISupports.h"
#include "nsQueryFrame.h"
#include "nsCoord.h"
#include "nsRect.h"
#include "nsColor.h"
#include "nsEvent.h"
#include "nsCompatibility.h"
#include "nsFrameManagerBase.h"
#include "mozFlushType.h"
#include "nsWeakReference.h"
#include <stdio.h> // for FILE definition

class nsIAtom;
class nsIContent;
class nsIContentIterator;
class nsIDocument;
class nsIDocumentObserver;
class nsIFrame;
class nsPresContext;
class nsStyleSet;
class nsIViewManager;
class nsIDeviceContext;
class nsIRenderingContext;
class nsIPageSequenceFrame;
class nsString;
class nsAString;
class nsCaret;
class nsStyleContext;
class nsFrameSelection;
class nsFrameManager;
class nsILayoutHistoryState;
class nsIReflowCallback;
class nsIDOMNode;
class nsIRegion;
class nsIStyleFrameConstruction;
class nsIStyleSheet;
class nsCSSFrameConstructor;
class nsISelection;
template<class E> class nsCOMArray;
class nsWeakFrame;
class nsIScrollableFrame;
class gfxASurface;
class gfxContext;
class nsPIDOMEventTarget;
class nsIDOMEvent;
class nsDisplayList;
class nsDisplayListBuilder;

typedef short SelectionType;
typedef PRUint32 nsFrameState;

// Flags to pass to SetCapturingContent
//
// when assigning capture, ignore whether capture is allowed or not
#define CAPTURE_IGNOREALLOWED 1
// true if events should be targeted at the capturing content or its children
#define CAPTURE_RETARGETTOELEMENT 2
// true if the current capture wants drags to be prevented
#define CAPTURE_PREVENTDRAG 4

typedef struct CapturingContentInfo {
  // capture should only be allowed during a mousedown event
  PRPackedBool mAllowed;
  PRPackedBool mRetargetToElement;
  PRPackedBool mPreventDrag;
  nsIContent* mContent;

  CapturingContentInfo() :
    mAllowed(PR_FALSE), mRetargetToElement(PR_FALSE), mPreventDrag(PR_FALSE),
    mContent(nsnull) { }
} CapturingContentInfo;

#define NS_IPRESSHELL_IID     \
  { 0x20b82adf, 0x1f5c, 0x44f7, \
    { 0x9b, 0x74, 0xc0, 0xa3, 0x14, 0xd8, 0xcf, 0x91 } }

// Constants for ScrollContentIntoView() function
#define NS_PRESSHELL_SCROLL_TOP      0
#define NS_PRESSHELL_SCROLL_BOTTOM   100
#define NS_PRESSHELL_SCROLL_LEFT     0
#define NS_PRESSHELL_SCROLL_RIGHT    100
#define NS_PRESSHELL_SCROLL_CENTER   50
#define NS_PRESSHELL_SCROLL_ANYWHERE -1
#define NS_PRESSHELL_SCROLL_IF_NOT_VISIBLE -2

// debug VerifyReflow flags
#define VERIFY_REFLOW_ON              0x01
#define VERIFY_REFLOW_NOISY           0x02
#define VERIFY_REFLOW_ALL             0x04
#define VERIFY_REFLOW_DUMP_COMMANDS   0x08
#define VERIFY_REFLOW_NOISY_RC        0x10
#define VERIFY_REFLOW_REALLY_NOISY_RC 0x20
#define VERIFY_REFLOW_DURING_RESIZE_REFLOW  0x40

#undef NOISY_INTERRUPTIBLE_REFLOW

enum nsRectVisibility { 
  nsRectVisibility_kVisible, 
  nsRectVisibility_kAboveViewport, 
  nsRectVisibility_kBelowViewport, 
  nsRectVisibility_kLeftOfViewport, 
  nsRectVisibility_kRightOfViewport
}; 

/**
 * Presentation shell interface. Presentation shells are the
 * controlling point for managing the presentation of a document. The
 * presentation shell holds a live reference to the document, the
 * presentation context, the style manager, the style set and the root
 * frame. <p>
 *
 * When this object is Release'd, it will release the document, the
 * presentation context, the style manager, the style set and the root
 * frame.
 */

// hack to make egcs / gcc 2.95.2 happy
class nsIPresShell_base : public nsISupports
{
public:
  NS_DECLARE_STATIC_IID_ACCESSOR(NS_IPRESSHELL_IID)
};

NS_DEFINE_STATIC_IID_ACCESSOR(nsIPresShell_base, NS_IPRESSHELL_IID)

class nsIPresShell : public nsIPresShell_base
{
public:
  NS_IMETHOD Init(nsIDocument* aDocument,
                  nsPresContext* aPresContext,
                  nsIViewManager* aViewManager,
                  nsStyleSet* aStyleSet,
                  nsCompatibility aCompatMode) = 0;

  /**
   * All callers are responsible for calling |Destroy| after calling
   * |EndObservingDocument|.  It needs to be separate only because form
   * controls incorrectly store their data in the frames rather than the
   * content model and printing calls |EndObservingDocument| multiple
   * times to make form controls behave nicely when printed.
   */
  NS_IMETHOD Destroy() = 0;
  
  PRBool IsDestroying() { return mIsDestroying; }

  // All frames owned by the shell are allocated from an arena.  They
  // are also recycled using free lists.  Separate free lists are
  // maintained for each frame type (aCode), which must always
  // correspond to the same aSize value. AllocateFrame clears the
  // memory that it returns.
  virtual void* AllocateFrame(nsQueryFrame::FrameIID aCode, size_t aSize) = 0;
  virtual void  FreeFrame(nsQueryFrame::FrameIID aCode, void* aChunk) = 0;

  // Objects closely related to the frame tree, but that are not
  // actual frames (subclasses of nsFrame) are also allocated from the
  // arena, and recycled via a separate set of per-size free lists.
  // AllocateMisc does *not* clear the memory that it returns.
  virtual void* AllocateMisc(size_t aSize) = 0;
  virtual void  FreeMisc(size_t aSize, void* aChunk) = 0;

  /**
   * Stack memory allocation:
   *
   * Callers who wish to allocate memory whose lifetime corresponds to
   * the lifetime of a stack-allocated object can use this API.  The
   * caller must use a pair of calls to PushStackMemory and
   * PopStackMemory, such that all stack object lifetimes are either
   * entirely between the calls or containing both calls.
   *
   * Then, between the calls, the caller can call AllocateStackMemory to
   * allocate memory from an arena pool that will be freed by the call
   * to PopStackMemory.
   *
   * The allocations cannot be for more than 4044 bytes.
   */
  virtual void PushStackMemory() = 0;
  virtual void PopStackMemory() = 0;
  virtual void* AllocateStackMemory(size_t aSize) = 0;
  
  nsIDocument* GetDocument() const { return mDocument; }

  nsPresContext* GetPresContext() { return mPresContext; }

  nsIViewManager* GetViewManager() { return mViewManager; }

#ifdef _IMPL_NS_LAYOUT
  nsStyleSet*  StyleSet() { return mStyleSet; }

  nsCSSFrameConstructor* FrameConstructor()
  {
    return mFrameConstructor;
  }

  nsFrameManager* FrameManager() const {
    return reinterpret_cast<nsFrameManager*>
                           (&const_cast<nsIPresShell*>(this)->mFrameManager);
  }

#endif

  /* Enable/disable author style level. Disabling author style disables the entire
   * author level of the cascade, including the HTML preshint level.
   */
  // XXX these could easily be inlined, but there is a circular #include
  // problem with nsStyleSet.
  NS_HIDDEN_(void) SetAuthorStyleDisabled(PRBool aDisabled);
  NS_HIDDEN_(PRBool) GetAuthorStyleDisabled();

  /*
   * Called when stylesheets are added/removed/enabled/disabled to rebuild
   * all style data for a given pres shell without necessarily reconstructing
   * all of the frames.  This will not reconstruct style synchronously; if
   * you need to do that, call FlushPendingNotifications to flush out style
   * reresolves.
   * // XXXbz why do we have this on the interface anyway?  The only consumer
   * is calling AddOverrideStyleSheet/RemoveOverrideStyleSheet, and I think
   * those should just handle reconstructing style data...
   */
  virtual NS_HIDDEN_(void) ReconstructStyleDataExternal();
  NS_HIDDEN_(void) ReconstructStyleDataInternal();
#ifdef _IMPL_NS_LAYOUT
  void ReconstructStyleData() { ReconstructStyleDataInternal(); }
#else
  void ReconstructStyleData() { ReconstructStyleDataExternal(); }
#endif

  /** Setup all style rules required to implement preferences
   * - used for background/text/link colors and link underlining
   *    may be extended for any prefs that are implemented via style rules
   * - aForceReflow argument is used to force a full reframe to make the rules show
   *   (only used when the current page needs to reflect changed pref rules)
   *
   * - initially created for bugs 31816, 20760, 22963
   */
  NS_IMETHOD SetPreferenceStyleRules(PRBool aForceReflow) = 0;

  /**
   * FrameSelection will return the Frame based selection API.
   * You cannot go back and forth anymore with QI between nsIDOM sel and
   * nsIFrame sel.
   */
  already_AddRefed<nsFrameSelection> FrameSelection();

  /**
   * ConstFrameSelection returns an object which methods are safe to use for
   * example in nsIFrame code.
   */
  const nsFrameSelection* ConstFrameSelection() { return mSelection; }

  // Make shell be a document observer.  If called after Destroy() has
  // been called on the shell, this will be ignored.
  NS_IMETHOD BeginObservingDocument() = 0;

  // Make shell stop being a document observer
  NS_IMETHOD EndObservingDocument() = 0;

  /**
   * Return whether InitialReflow() was previously called.
   */
  PRBool DidInitialReflow() const { return mDidInitialReflow; }

  /**
   * Perform the initial reflow. Constructs the frame for the root content
   * object and then reflows the frame model into the specified width and
   * height.
   *
   * The coordinates for aWidth and aHeight must be in standard nscoords.
   *
   * Callers of this method must hold a reference to this shell that
   * is guaranteed to survive through arbitrary script execution.
   * Calling InitialReflow can execute arbitrary script.
   */
  NS_IMETHOD InitialReflow(nscoord aWidth, nscoord aHeight) = 0;

  /**
   * Reflow the frame model into a new width and height.  The
   * coordinates for aWidth and aHeight must be in standard nscoord's.
   */
  NS_IMETHOD ResizeReflow(nscoord aWidth, nscoord aHeight) = 0;

  /**
   * Reflow the frame model with a reflow reason of eReflowReason_StyleChange
   */
  NS_IMETHOD StyleChangeReflow() = 0;

  /**
   * This calls through to the frame manager to get the root frame.
   * Callers inside of gklayout should use FrameManager()->GetRootFrame()
   * instead, as it's more efficient.
   */
  virtual NS_HIDDEN_(nsIFrame*) GetRootFrame() const;

  /*
   * Get root scroll frame from FrameManager()->GetRootFrame().
   */
  nsIFrame* GetRootScrollFrame() const;

  /*
   * The same as GetRootScrollFrame, but returns an nsIScrollableFrame
   */
  nsIScrollableFrame* GetRootScrollFrameAsScrollable() const;

  /*
   * The same as GetRootScrollFrame, but returns an nsIScrollableFrame.
   * Can be called by code not linked into gklayout.
   */
  virtual nsIScrollableFrame* GetRootScrollFrameAsScrollableExternal() const;

  /**
   * Returns the page sequence frame associated with the frame hierarchy.
   * Returns NULL if not a paginated view.
   */
  NS_IMETHOD GetPageSequenceFrame(nsIPageSequenceFrame** aResult) const = 0;

  /**
   * Gets the real primary frame associated with the content object.
   *
   * In the case of absolutely positioned elements and floated elements,
   * the real primary frame is the frame that is out of the flow and not the
   * placeholder frame.
   */
  virtual NS_HIDDEN_(nsIFrame*) GetRealPrimaryFrameFor(nsIContent* aContent) const = 0;

  /**
   * Gets the placeholder frame associated with the specified frame. This is
   * a helper frame that forwards the request to the frame manager.
   */
  NS_IMETHOD GetPlaceholderFrameFor(nsIFrame*  aFrame,
                                    nsIFrame** aPlaceholderFrame) const = 0;

  /**
   * Tell the pres shell that a frame needs to be marked dirty and needs
   * Reflow.  It's OK if this is an ancestor of the frame needing reflow as
   * long as the ancestor chain between them doesn't cross a reflow root.  The
   * bit to add should be either NS_FRAME_IS_DIRTY or
   * NS_FRAME_HAS_DIRTY_CHILDREN (but not both!).
   */
  enum IntrinsicDirty {
    // XXXldb eResize should be renamed
    eResize,     // don't mark any intrinsic widths dirty
    eTreeChange, // mark intrinsic widths dirty on aFrame and its ancestors
    eStyleChange // Do eTreeChange, plus all of aFrame's descendants
  };
  NS_IMETHOD FrameNeedsReflow(nsIFrame *aFrame,
                              IntrinsicDirty aIntrinsicDirty,
                              nsFrameState aBitToAdd) = 0;

  /**
   * Tell the presshell that the given frame's reflow was interrupted.  This
   * will mark as having dirty children a path from the given frame (inclusive)
   * to the nearest ancestor with a dirty subtree, or to the reflow root
   * currently being reflowed if no such ancestor exists (inclusive).  This is
   * to be done immediately after reflow of the current reflow root completes.
   * This method must only be called during reflow, and the frame it's being
   * called on must be in the process of being reflowed when it's called.  This
   * method doesn't mark any intrinsic widths dirty and doesn't add any bits
   * other than NS_FRAME_HAS_DIRTY_CHILDREN.
   */
  NS_IMETHOD_(void) FrameNeedsToContinueReflow(nsIFrame *aFrame) = 0;

  NS_IMETHOD CancelAllPendingReflows() = 0;

  /**
   * Recreates the frames for a node
   */
  NS_IMETHOD RecreateFramesFor(nsIContent* aContent) = 0;

  void PostRecreateFramesFor(nsIContent* aContent);
  void RestyleForAnimation(nsIContent* aContent);
  
  /**
   * Determine if it is safe to flush all pending notifications
   * @param aIsSafeToFlush PR_TRUE if it is safe, PR_FALSE otherwise.
   * 
   */
  NS_IMETHOD IsSafeToFlush(PRBool& aIsSafeToFlush) = 0;

  /**
   * Flush pending notifications of the type specified.  This method
   * will not affect the content model; it'll just affect style and
   * frames. Callers that actually want up-to-date presentation (other
   * than the document itself) should probably be calling
   * nsIDocument::FlushPendingNotifications.
   *
   * @param aType the type of notifications to flush
   */
  NS_IMETHOD FlushPendingNotifications(mozFlushType aType) = 0;

  /**
   * Callbacks will be called even if reflow itself fails for
   * some reason.
   */
  NS_IMETHOD PostReflowCallback(nsIReflowCallback* aCallback) = 0;
  NS_IMETHOD CancelReflowCallback(nsIReflowCallback* aCallback) = 0;

  NS_IMETHOD ClearFrameRefs(nsIFrame* aFrame) = 0;

  /**
   * Given a frame, create a rendering context suitable for use with
   * the frame.
   */
  NS_IMETHOD CreateRenderingContext(nsIFrame *aFrame,
                                    nsIRenderingContext** aContext) = 0;

  /**
   * Informs the pres shell that the document is now at the anchor with
   * the given name.  If |aScroll| is true, scrolls the view of the
   * document so that the anchor with the specified name is displayed at
   * the top of the window.  If |aAnchorName| is empty, then this informs
   * the pres shell that there is no current target, and |aScroll| must
   * be false.
   */
  NS_IMETHOD GoToAnchor(const nsAString& aAnchorName, PRBool aScroll) = 0;

  /**
   * Tells the presshell to scroll again to the last anchor scrolled to by
   * GoToAnchor, if any. This scroll only happens if the scroll
   * position has not changed since the last GoToAnchor. This is called
   * by nsDocumentViewer::LoadComplete. This clears the last anchor
   * scrolled to by GoToAnchor (we don't want to keep it alive if it's
   * removed from the DOM), so don't call this more than once.
   */
  NS_IMETHOD ScrollToAnchor() = 0;

  /**
   * Scrolls the view of the document so that the primary frame of the content
   * is displayed in the window. Layout is flushed before scrolling.
   *
   * @param aContent  The content object of which primary frame should be
   *                  scrolled into view.
   * @param aVPercent How to align the frame vertically. A value of 0
   *                  (NS_PRESSHELL_SCROLL_TOP) means the frame's upper edge is
   *                  aligned with the top edge of the visible area. A value of
   *                  100 (NS_PRESSHELL_SCROLL_BOTTOM) means the frame's bottom
   *                  edge is aligned with the bottom edge of the visible area.
   *                  For values in between, the point "aVPercent" down the frame
   *                  is placed at the point "aVPercent" down the visible area. A
   *                  value of 50 (NS_PRESSHELL_SCROLL_CENTER) centers the frame
   *                  vertically. A value of NS_PRESSHELL_SCROLL_ANYWHERE means move
   *                  the frame the minimum amount necessary in order for the entire
   *                  frame to be visible vertically (if possible)
   * @param aHPercent How to align the frame horizontally. A value of 0
   *                  (NS_PRESSHELL_SCROLL_LEFT) means the frame's left edge is
   *                  aligned with the left edge of the visible area. A value of
   *                  100 (NS_PRESSHELL_SCROLL_RIGHT) means the frame's right
   *                  edge is aligned with the right edge of the visible area.
   *                  For values in between, the point "aVPercent" across the frame
   *                  is placed at the point "aVPercent" across the visible area.
   *                  A value of 50 (NS_PRESSHELL_SCROLL_CENTER) centers the frame
   *                  horizontally . A value of NS_PRESSHELL_SCROLL_ANYWHERE means move
   *                  the frame the minimum amount necessary in order for the entire
   *                  frame to be visible horizontally (if possible)
   */
  NS_IMETHOD ScrollContentIntoView(nsIContent* aContent,
                                   PRIntn      aVPercent,
                                   PRIntn      aHPercent) = 0;

  enum {
    SCROLL_FIRST_ANCESTOR_ONLY = 0x01,
    SCROLL_OVERFLOW_HIDDEN = 0x02
  };
  /**
   * Scrolls the view of the document so that the given area of a frame
   * is visible, if possible. Layout is not flushed before scrolling.
   * 
   * @param aRect relative to aFrame
   * @param aVPercent see ScrollContentIntoView
   * @param aHPercent see ScrollContentIntoView
   * @param aFlags if SCROLL_FIRST_ANCESTOR_ONLY is set, only the
   * nearest scrollable ancestor is scrolled, otherwise all
   * scrollable ancestors may be scrolled if necessary
   * if SCROLL_OVERFLOW_HIDDEN is set then we may scroll in a direction
   * even if overflow:hidden is specified in that direction; otherwise
   * we will not scroll in that direction when overflow:hidden is
   * set for that direction
   * @return true if any scrolling happened, false if no scrolling happened
   */
  virtual PRBool ScrollFrameRectIntoView(nsIFrame*     aFrame,
                                         const nsRect& aRect,
                                         PRIntn        aVPercent,
                                         PRIntn        aHPercent,
                                         PRUint32      aFlags) = 0;

  /**
   * Determine if a rectangle specified in the frame's coordinate system 
   * intersects the viewport "enough" to be considered visible.
   * @param aFrame frame that aRect coordinates are specified relative to
   * @param aRect rectangle in twips to test for visibility 
   * @param aMinTwips is the minimum distance in from the edge of the viewport
   *                  that an object must be to be counted visible
   * @return nsRectVisibility_kVisible if the rect is visible
   *         nsRectVisibility_kAboveViewport
   *         nsRectVisibility_kBelowViewport 
   *         nsRectVisibility_kLeftOfViewport 
   *         nsRectVisibility_kRightOfViewport rectangle is outside the viewport
   *         in the specified direction 
   */
  virtual nsRectVisibility GetRectVisibility(nsIFrame *aFrame,
                                             const nsRect &aRect, 
                                             nscoord aMinTwips) = 0;

  /**
   * Suppress notification of the frame manager that frames are
   * being destroyed.
   */
  NS_IMETHOD SetIgnoreFrameDestruction(PRBool aIgnore) = 0;

  /**
   * Notification sent by a frame informing the pres shell that it is about to
   * be destroyed.
   * This allows any outstanding references to the frame to be cleaned up
   */
  NS_IMETHOD NotifyDestroyingFrame(nsIFrame* aFrame) = 0;

  /**
   * Notify the Clipboard that we have something to copy.
   */
  NS_IMETHOD DoCopy() = 0;

  /**
   * Get the selection of the focussed element (either the page selection,
   * or the selection for a text field).
   */
  NS_IMETHOD GetSelectionForCopy(nsISelection** outSelection) = 0;

  /**
   * Get link location.
   */
  NS_IMETHOD GetLinkLocation(nsIDOMNode* aNode, nsAString& aLocation) = 0;

  /**
   * Get the doc or the selection as text or html.
   */
  NS_IMETHOD DoGetContents(const nsACString& aMimeType, PRUint32 aFlags, PRBool aSelectionOnly, nsAString& outValue) = 0;

  /**
   * Get the caret, if it exists. AddRefs it.
   */
  NS_IMETHOD GetCaret(nsCaret **aOutCaret) = 0;

  /**
   * Invalidate the caret's current position if it's outside of its frame's
   * boundaries. This function is useful if you're batching selection
   * notifications and might remove the caret's frame out from under it.
   */
  NS_IMETHOD_(void) MaybeInvalidateCaretPosition() = 0;

  /**
   * Set the current caret to a new caret. To undo this, call RestoreCaret.
   */
  virtual void SetCaret(nsCaret *aNewCaret) = 0;

  /**
   * Restore the caret to the original caret that this pres shell was created
   * with.
   */
  virtual void RestoreCaret() = 0;

  /**
   * Should the images have borders etc.  Actual visual effects are determined
   * by the frames.  Visual effects may not effect layout, only display.
   * Takes effect on next repaint, does not force a repaint itself.
   *
   * @param aEnabled  if PR_TRUE, visual selection effects are enabled
   *                  if PR_FALSE visual selection effects are disabled
   * @return  always NS_OK
   */
  NS_IMETHOD SetSelectionFlags(PRInt16 aInEnable) = 0;

  /** 
    * Gets the current state of non text selection effects
    * @param aEnabled  [OUT] set to the current state of non text selection,
    *                  as set by SetDisplayNonTextSelection
    * @return   if aOutEnabled==null, returns NS_ERROR_INVALID_ARG
    *           else NS_OK
    */
  NS_IMETHOD GetSelectionFlags(PRInt16 *aOutEnabled) = 0;
  
  virtual nsISelection* GetCurrentSelection(SelectionType aType) = 0;

  /**
    * Interface to dispatch events via the presshell
    * @note The caller must have a strong reference to the PresShell.
    */
  NS_IMETHOD HandleEventWithTarget(nsEvent* aEvent,
                                   nsIFrame* aFrame,
                                   nsIContent* aContent,
                                   nsEventStatus* aStatus) = 0;

  /**
   * Dispatch event to content only (NOT full processing)
   * @note The caller must have a strong reference to the PresShell.
   */
  NS_IMETHOD HandleDOMEventWithTarget(nsIContent* aTargetContent,
                                      nsEvent* aEvent,
                                      nsEventStatus* aStatus) = 0;

  /**
   * Dispatch event to content only (NOT full processing)
   * @note The caller must have a strong reference to the PresShell.
   */
  NS_IMETHOD HandleDOMEventWithTarget(nsIContent* aTargetContent,
                                      nsIDOMEvent* aEvent,
                                      nsEventStatus* aStatus) = 0;

  /**
    * Gets the current target event frame from the PresShell
    */
  NS_IMETHOD GetEventTargetFrame(nsIFrame** aFrame) = 0;

  /**
    * Gets the current target event frame from the PresShell
    */
  NS_IMETHOD GetEventTargetContent(nsEvent* aEvent, nsIContent** aContent) = 0;

  /**
   * Get and set the history state for the current document 
   */

  NS_IMETHOD CaptureHistoryState(nsILayoutHistoryState** aLayoutHistoryState, PRBool aLeavingPage = PR_FALSE) = 0;

  /**
   * Determine if reflow is currently locked
   * @param aIsReflowLocked returns PR_TRUE if reflow is locked, PR_FALSE otherwise
   */
  NS_IMETHOD IsReflowLocked(PRBool* aIsLocked) = 0;  

  /**
   * Called to find out if painting is suppressed for this presshell.  If it is suppressd,
   * we don't allow the painting of any layer but the background, and we don't
   * recur into our children.
   */
  NS_IMETHOD IsPaintingSuppressed(PRBool* aResult)=0;

  /**
   * Unsuppress painting.
   */
  NS_IMETHOD UnsuppressPainting() = 0;

  /**
   * Called to disable nsITheme support in a specific presshell.
   */
  NS_IMETHOD DisableThemeSupport() = 0;

  /**
   * Indicates whether theme support is enabled.
   */
  virtual PRBool IsThemeSupportEnabled() = 0;

  /**
   * Get the set of agent style sheets for this presentation
   */
  virtual nsresult GetAgentStyleSheets(nsCOMArray<nsIStyleSheet>& aSheets) = 0;

  /**
   * Replace the set of agent style sheets
   */
  virtual nsresult SetAgentStyleSheets(const nsCOMArray<nsIStyleSheet>& aSheets) = 0;

  /**
   * Add an override style sheet for this presentation
   */
  virtual nsresult AddOverrideStyleSheet(nsIStyleSheet *aSheet) = 0;

  /**
   * Remove an override style sheet
   */
  virtual nsresult RemoveOverrideStyleSheet(nsIStyleSheet *aSheet) = 0;

  /**
   * Reconstruct frames for all elements in the document
   */
  virtual nsresult ReconstructFrames() = 0;

  /**
   * Given aFrame, the root frame of a stacking context, find its descendant
   * frame under the point aPt that receives a mouse event at that location,
   * or nsnull if there is no such frame.
   * @param aPt the point, relative to the frame origin
   */
  virtual nsIFrame* GetFrameForPoint(nsIFrame* aFrame, nsPoint aPt) = 0;

  /**
   * See if reflow verification is enabled. To enable reflow verification add
   * "verifyreflow:1" to your NSPR_LOG_MODULES environment variable
   * (any non-zero debug level will work). Or, call SetVerifyReflowEnable
   * with PR_TRUE.
   */
  static PRBool GetVerifyReflowEnable();

  /**
   * Set the verify-reflow enable flag.
   */
  static void SetVerifyReflowEnable(PRBool aEnabled);

  /**
   * Get the flags associated with the VerifyReflow debug tool
   */
  static PRInt32 GetVerifyReflowFlags();

  virtual nsIFrame* GetAbsoluteContainingBlock(nsIFrame* aFrame);

#ifdef MOZ_REFLOW_PERF
  NS_IMETHOD DumpReflows() = 0;
  NS_IMETHOD CountReflows(const char * aName, nsIFrame * aFrame) = 0;
  NS_IMETHOD PaintCount(const char * aName, 
                        nsIRenderingContext* aRenderingContext, 
                        nsPresContext * aPresContext, 
                        nsIFrame * aFrame,
                        PRUint32 aColor) = 0;
  NS_IMETHOD SetPaintFrameCount(PRBool aOn) = 0;
  virtual PRBool IsPaintingFrameCounts() = 0;
#endif

#ifdef DEBUG
  // Debugging hooks
  virtual void ListStyleContexts(nsIFrame *aRootFrame, FILE *out,
                                 PRInt32 aIndent = 0) = 0;

  virtual void ListStyleSheets(FILE *out, PRInt32 aIndent = 0) = 0;
  virtual void VerifyStyleTree() = 0;
#endif

  static PRBool gIsAccessibilityActive;
  static PRBool IsAccessibilityActive() { return gIsAccessibilityActive; }

  /**
   * Stop all active elements (plugins and the caret) in this presentation and
   * in the presentations of subdocuments.  Resets painting to a suppressed state.
   * XXX this should include image animations
   */
  virtual void Freeze() = 0;

  /**
   * Restarts active elements (plugins) in this presentation and in the
   * presentations of subdocuments, then do a full invalidate of the content area.
   */
  virtual void Thaw() = 0;

  virtual void FireOrClearDelayedEvents(PRBool aFireEvents) = 0;

  /**
   * When this shell is disconnected from its containing docshell, we
   * lose our container pointer.  However, we'd still like to be able to target
   * user events at the docshell's parent.  This pointer allows us to do that.
   * It should not be used for any other purpose.
   */
  void SetForwardingContainer(nsWeakPtr aContainer)
  {
    mForwardingContainer = aContainer;
  }
  
  /**
   * Render the document into an arbitrary gfxContext
   * Designed for getting a picture of a document or a piece of a document
   * Note that callers will generally want to call FlushPendingNotifications
   * to get an up-to-date view of the document
   * @param aRect is the region to capture into the offscreen buffer, in the
   * root frame's coordinate system (if aIgnoreViewportScrolling is false)
   * or in the root scrolled frame's coordinate system
   * (if aIgnoreViewportScrolling is true). The coordinates are in appunits.
   * @param aFlags see below;
   *   set RENDER_IS_UNTRUSTED if the contents may be passed to malicious
   * agents. E.g. we might choose not to paint the contents of sensitive widgets
   * such as the file name in a file upload widget, and we might choose not
   * to paint themes.
   *   set RENDER_IGNORE_VIEWPORT_SCROLLING to ignore
   * clipping/scrolling/scrollbar painting due to scrolling in the viewport
   *   set RENDER_CARET to draw the caret if one would be visible
   * (by default the caret is never drawn)
   * @param aBackgroundColor a background color to render onto
   * @param aRenderedContext the gfxContext to render to. We render so that
   * one CSS pixel in the source document is rendered to one unit in the current
   * transform.
   */
  enum {
    RENDER_IS_UNTRUSTED = 0x01,
    RENDER_IGNORE_VIEWPORT_SCROLLING = 0x02,
    RENDER_CARET = 0x04
  };
  NS_IMETHOD RenderDocument(const nsRect& aRect, PRUint32 aFlags,
                            nscolor aBackgroundColor,
                            gfxContext* aRenderedContext) = 0;

  /**
   * Renders a node aNode to a surface and returns it. The aRegion may be used
   * to clip the rendering. This region is measured in device pixels from the
   * edge of the presshell area. The aPoint, aScreenRect and aSurface
   * arguments function in a similar manner as RenderSelection.
   */
  virtual already_AddRefed<gfxASurface> RenderNode(nsIDOMNode* aNode,
                                                   nsIRegion* aRegion,
                                                   nsIntPoint& aPoint,
                                                   nsIntRect* aScreenRect) = 0;

  /**
   * Renders a selection to a surface and returns it. This method is primarily
   * intended to create the drag feedback when dragging a selection.
   *
   * aScreenRect will be filled in with the bounding rectangle of the
   * selection area on screen.
   *
   * If the area of the selection is large, the image will be scaled down.
   * The argument aPoint is used in this case as a reference point when
   * determining the new screen rectangle after scaling. Typically, this
   * will be the mouse position, so that the screen rectangle is positioned
   * such that the mouse is over the same point in the scaled image as in
   * the original. When scaling does not occur, the mouse point isn't used
   * as the position can be determined from the displayed frames.
   */
  virtual already_AddRefed<gfxASurface> RenderSelection(nsISelection* aSelection,
                                                        nsIntPoint& aPoint,
                                                        nsIntRect* aScreenRect) = 0;

  void AddWeakFrameInternal(nsWeakFrame* aWeakFrame);
  virtual void AddWeakFrameExternal(nsWeakFrame* aWeakFrame);

  void AddWeakFrame(nsWeakFrame* aWeakFrame)
  {
#ifdef _IMPL_NS_LAYOUT
    AddWeakFrameInternal(aWeakFrame);
#else
    AddWeakFrameExternal(aWeakFrame);
#endif
  }

  void RemoveWeakFrameInternal(nsWeakFrame* aWeakFrame);
  virtual void RemoveWeakFrameExternal(nsWeakFrame* aWeakFrame);

  void RemoveWeakFrame(nsWeakFrame* aWeakFrame)
  {
#ifdef _IMPL_NS_LAYOUT
    RemoveWeakFrameInternal(aWeakFrame);
#else
    RemoveWeakFrameExternal(aWeakFrame);
#endif
  }

#ifdef NS_DEBUG
  nsIFrame* GetDrawEventTargetFrame() { return mDrawEventTargetFrame; }
#endif

  /**
   * Stop or restart non synthetic test mouse event handling on *all*
   * presShells.
   *
   * @param aDisable If true, disable all non synthetic test mouse
   * events on all presShells.  Otherwise, enable them.
   */
  NS_IMETHOD DisableNonTestMouseEvents(PRBool aDisable) = 0;

  /**
   * Record the background color of the most recently drawn canvas. This color
   * is composited on top of the user's default background color and then used
   * to draw the background color of the canvas. See PresShell::Paint,
   * PresShell::PaintDefaultBackground, and nsDocShell::SetupNewViewer;
   * bug 488242, bug 476557 and other bugs mentioned there.
   */
  void SetCanvasBackground(nscolor aColor) { mCanvasBackgroundColor = aColor; }
  nscolor GetCanvasBackground() { return mCanvasBackgroundColor; }

  /**
   * Use the current frame tree (if it exists) to update the background
   * color of the most recently drawn canvas.
   */
  virtual void UpdateCanvasBackground() = 0;

  /**
   * Add a solid color item to the bottom of aList with frame aFrame and
   * bounds aBounds. Checks first if this needs to be done by checking if
   * aFrame is a canvas frame (if aForceDraw is true then this check is
   * skipped). If aBounds is null (the default) then the bounds will be derived
   * from the frame. aBackstopColor is composed behind the background color of
   * the canvas, it is transparent by default.
   */
  virtual nsresult AddCanvasBackgroundColorItem(nsDisplayListBuilder& aBuilder,
                                                nsDisplayList& aList,
                                                nsIFrame* aFrame,
                                                nsRect* aBounds = nsnull,
                                                nscolor aBackstopColor = NS_RGBA(0,0,0,0),
                                                PRBool aForceDraw = PR_FALSE) = 0;

  void ObserveNativeAnonMutationsForPrint(PRBool aObserve)
  {
    mObservesMutationsForPrint = aObserve;
  }
  PRBool ObservesNativeAnonMutationsForPrint()
  {
    return mObservesMutationsForPrint;
  }

  // mouse capturing

  static CapturingContentInfo gCaptureInfo;

  /**
   * When capturing content is set, it traps all mouse events and retargets
   * them at this content node. If capturing is not allowed
   * (gCaptureInfo.mAllowed is false), then capturing is not set. However, if
   * the CAPTURE_IGNOREALLOWED flag is set, the allowed state is ignored and
   * capturing is set regardless. To disable capture, pass null for the value
   * of aContent.
   *
   * If CAPTURE_RETARGETTOELEMENT is set, all mouse events are targeted at
   * aContent only. Otherwise, mouse events are targeted at aContent or its
   * descendants. That is, descendants of aContent receive mouse events as
   * they normally would, but mouse events outside of aContent are retargeted
   * to aContent.
   *
   * If CAPTURE_PREVENTDRAG is set then drags are prevented from starting while
   * this capture is active.
   */
  static void SetCapturingContent(nsIContent* aContent, PRUint8 aFlags);

  /**
   * Return the active content currently capturing the mouse if any.
   */
  static nsIContent* GetCapturingContent()
  {
    return gCaptureInfo.mContent;
  }

  /**
   * Allow or disallow mouse capturing.
   */
  static void AllowMouseCapture(PRBool aAllowed)
  {
    gCaptureInfo.mAllowed = aAllowed;
  }

  /**
   * Returns true if there is an active mouse capture that wants to prevent
   * drags.
   */
  static PRBool IsMouseCapturePreventingDrag()
  {
    return gCaptureInfo.mPreventDrag && gCaptureInfo.mContent;
  }

protected:
  // IMPORTANT: The ownership implicit in the following member variables
  // has been explicitly checked.  If you add any members to this class,
  // please make the ownership explicit (pinkerton, scc).

  // these are the same Document and PresContext owned by the DocViewer.
  // we must share ownership.
  nsIDocument*              mDocument;      // [STRONG]
  nsPresContext*            mPresContext;   // [STRONG]
  nsStyleSet*               mStyleSet;      // [OWNS]
  nsCSSFrameConstructor*    mFrameConstructor; // [STRONG]
  nsIViewManager*           mViewManager;   // [WEAK] docViewer owns it so I don't have to
  nsFrameSelection*         mSelection;
  nsFrameManagerBase        mFrameManager;  // [OWNS]
  nsWeakPtr                 mForwardingContainer;

#ifdef NS_DEBUG
  nsIFrame*                 mDrawEventTargetFrame;
#endif

  PRPackedBool              mStylesHaveChanged;
  PRPackedBool              mDidInitialReflow;
  PRPackedBool              mIsDestroying;

#ifdef ACCESSIBILITY
  /**
   * Call this when there have been significant changes in the rendering for
   * a content subtree, so the matching accessibility subtree can be invalidated
   */
  void InvalidateAccessibleSubtree(nsIContent *aContent);
#endif

  // Set to true when the accessibility service is being used to mirror
  // the dom/layout trees
  PRPackedBool              mIsAccessibilityActive;

  PRPackedBool              mObservesMutationsForPrint;

  // A list of weak frames. This is a pointer to the last item in the list.
  nsWeakFrame*              mWeakFrames;

  // Most recent canvas background color.
  nscolor                   mCanvasBackgroundColor;
};

/**
 * Create a new empty presentation shell. Upon success, call Init
 * before attempting to use the shell.
 */
nsresult
NS_NewPresShell(nsIPresShell** aInstancePtrResult);

#endif /* nsIPresShell_h___ */
