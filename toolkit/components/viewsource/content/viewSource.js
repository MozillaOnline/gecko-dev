# -*- Mode: Java; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
# ***** BEGIN LICENSE BLOCK *****
# Version: MPL 1.1/GPL 2.0/LGPL 2.1
#
# The contents of this file are subject to the Mozilla Public License Version
# 1.1 (the "License"); you may not use this file except in compliance with
# the License. You may obtain a copy of the License at
# http://www.mozilla.org/MPL/
#
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
#
# The Original Code is mozilla.org code.
#
# The Initial Developer of the Original Code is
# Netscape Communications Corporation.
# Portions created by the Initial Developer are Copyright (C) 2001
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#   Doron Rosenberg (doronr@naboonline.com)
#   Neil Rashbrook (neil@parkwaycc.co.uk)
#   Dão Gottwald (dao@design-noir.de)
#
# Alternatively, the contents of this file may be used under the terms of
# either the GNU General Public License Version 2 or later (the "GPL"), or
# the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
# in which case the provisions of the GPL or the LGPL are applicable instead
# of those above. If you wish to allow use of your version of this file only
# under the terms of either the GPL or the LGPL, and not to allow others to
# use your version of this file under the terms of the MPL, indicate your
# decision by deleting the provisions above and replace them with the notice
# and other provisions required by the GPL or the LGPL. If you do not delete
# the provisions above, a recipient may use your version of this file under
# the terms of any one of the MPL, the GPL or the LGPL.
#
# ***** END LICENSE BLOCK *****

const Cc = Components.classes;
const Ci = Components.interfaces;
var gPrefs = null;

var gLastLineFound = '';
var gGoToLine = 0;

[
  ["gBrowser",          "content"],
  ["gViewSourceBundle", "viewSourceBundle"]
].forEach(function ([name, id]) {
  window.__defineGetter__(name, function () {
    var element = document.getElementById(id);
    if (!element)
      return null;
    delete window[name];
    return window[name] = element;
  });
});

// viewZoomOverlay.js uses this
function getBrowser() {
  return gBrowser;
}

__defineGetter__("gPageLoader", function () {
  var webnav = getWebNavigation();
  if (!webnav)
    return null;
  delete this.gPageLoader;
  return this.gPageLoader = webnav.QueryInterface(Ci.nsIWebPageDescriptor);
});

try {
  var prefService = Components.classes["@mozilla.org/preferences-service;1"]
                              .getService(Components.interfaces.nsIPrefService);
  gPrefs = prefService.getBranch(null);
} catch (ex) {
}

var gSelectionListener = {
  timeout: 0,
  attached: false,
  notifySelectionChanged: function(doc, sel, reason)
  {
    // Coalesce notifications within 100ms intervals.
    if (!this.timeout)
      this.timeout = setTimeout(updateStatusBar, 100);
  }
}

function onLoadViewSource() 
{
  viewSource(window.arguments[0]);
  document.commandDispatcher.focusedWindow = content;
      
  if (!isHistoryEnabled()) {
    // Disable the BACK and FORWARD commands and hide the related menu items.
    var viewSourceNavigation = document.getElementById("viewSourceNavigation");
    viewSourceNavigation.setAttribute("disabled", "true");
    viewSourceNavigation.setAttribute("hidden", "true");
  }
}

function isHistoryEnabled() {
  return !gBrowser.hasAttribute("disablehistory");
}

function getSelectionController() {
  return gBrowser.docShell
                 .QueryInterface(Ci.nsIInterfaceRequestor)
                 .getInterface(Ci.nsISelectionDisplay)
                 .QueryInterface(Ci.nsISelectionController);
}

function viewSource(url)
{
  if (!url)
    return; // throw Components.results.NS_ERROR_FAILURE;
    
  var viewSrcUrl = "view-source:" + url;

  gBrowser.addEventListener("pagehide", onUnloadContent, true);
  gBrowser.addEventListener("pageshow", onLoadContent, true);
  gBrowser.addEventListener("command", onCommandContent, false);

  var loadFromURL = true;

  // Parse the 'arguments' supplied with the dialog.
  //    arg[0] - URL string.
  //    arg[1] - Charset value in the form 'charset=xxx'.
  //    arg[2] - Page descriptor used to load content from the cache.
  //    arg[3] - Line number to go to.
  //    arg[4] - Whether charset was forced by the user

  if ("arguments" in window) {
    var arg;

    // Set the charset of the viewsource window...
    var charset;
    if (window.arguments.length >= 2) {
      arg = window.arguments[1];

      try {
        if (typeof(arg) == "string" && arg.indexOf('charset=') != -1) {
          var arrayArgComponents = arg.split('=');
          if (arrayArgComponents) {
            //we should "inherit" the charset menu setting in a new window
            charset = arrayArgComponents[1];
            gBrowser.markupDocumentViewer.defaultCharacterSet = charset;
          }
        }
      } catch (ex) {
        // Ignore the failure and keep processing arguments...
      }
    }
    // If the document had a forced charset, set it here also
    if (window.arguments.length >= 5) {
      arg = window.arguments[4];

      try {
        if (arg === true) {
          var docCharset = gBrowser.docShell.QueryInterface(Ci.nsIDocCharset);
          docCharset.charset = charset;
        }
      } catch (ex) {
        // Ignore the failure and keep processing arguments...
      }
    }

    // Get any specified line to jump to.
    if (window.arguments.length >= 4) {
      arg = window.arguments[3];
      gGoToLine = parseInt(arg);
    }

    // Use the page descriptor to load the content from the cache (if
    // available).
    if (window.arguments.length >= 3) {
      arg = window.arguments[2];

      try {
        if (typeof(arg) == "object" && arg != null) {
          // Load the page using the page descriptor rather than the URL.
          // This allows the content to be fetched from the cache (if
          // possible) rather than the network...
          gPageLoader.loadPage(arg, gPageLoader.DISPLAY_AS_SOURCE);

          // The content was successfully loaded.
          loadFromURL = false;

          // Record the page load in the session history so <back> will work.
          var shEntrySource = arg.QueryInterface(Ci.nsISHEntry);
          var shEntry = Cc["@mozilla.org/browser/session-history-entry;1"].createInstance(Ci.nsISHEntry);
          shEntry.setURI(makeURI(viewSrcUrl, null, null));
          shEntry.setTitle(viewSrcUrl);
          shEntry.loadType = Ci.nsIDocShellLoadInfo.loadHistory;
          shEntry.cacheKey = shEntrySource.cacheKey;
          gBrowser.sessionHistory
                  .QueryInterface(Ci.nsISHistoryInternal)
                  .addEntry(shEntry, true);
        }
      } catch(ex) {
        // Ignore the failure.  The content will be loaded via the URL
        // that was supplied in arg[0].
      }
    }
  }

  if (loadFromURL) {
    // Currently, an exception is thrown if the URL load fails...
    var loadFlags = Ci.nsIWebNavigation.LOAD_FLAGS_NONE;
    getWebNavigation().loadURI(viewSrcUrl, loadFlags, null, null, null);
  }

  // Check the view_source.wrap_long_lines pref and set the menuitem's checked
  // attribute accordingly.
  if (gPrefs) {
    try {
      var wraplonglinesPrefValue = gPrefs.getBoolPref("view_source.wrap_long_lines");

      if (wraplonglinesPrefValue)
        document.getElementById("menu_wrapLongLines").setAttribute("checked", "true");
    } catch (ex) {
    }
    try {
      document.getElementById("menu_highlightSyntax").setAttribute("checked",
        gPrefs.getBoolPref("view_source.syntax_highlight"));
    } catch (ex) {
    }
  } else {
    document.getElementById("menu_highlightSyntax").setAttribute("hidden", "true");
  }

  window.addEventListener("AppCommand", HandleAppCommandEvent, true);
  window.addEventListener("MozSwipeGesture", HandleSwipeGesture, true);
  window.content.focus();
}

function onLoadContent()
{
  // If the view source was opened with a "go to line" argument.
  if (gGoToLine > 0) {
    goToLine(gGoToLine);
    gGoToLine = 0;
  }
  document.getElementById('cmd_goToLine').removeAttribute('disabled');

  // Register a listener so that we can show the caret position on the status bar.
  window.content.getSelection()
   .QueryInterface(Ci.nsISelectionPrivate)
   .addSelectionListener(gSelectionListener);
  gSelectionListener.attached = true;

  if (isHistoryEnabled())
    UpdateBackForwardCommands();
}

function onUnloadContent()
{
  // Disable "go to line" while reloading due to e.g. change of charset
  // or toggling of syntax highlighting.
  document.getElementById('cmd_goToLine').setAttribute('disabled', 'true');

  // If we're not just unloading the initial "about:blank" which doesn't have
  // a selection listener, get rid of it so it doesn't try to fire after the
  // window has gone away.
  if (gSelectionListener.attached) {
    window.content.getSelection().QueryInterface(Ci.nsISelectionPrivate)
          .removeSelectionListener(gSelectionListener);
    gSelectionListener.attached = false;
  }
}

/**
 * Handle command events bubbling up from error page content
 */
function onCommandContent(event) {
  // Don't trust synthetic events
  if (!event.isTrusted)
    return;

  var target = event.originalTarget;
  var errorDoc = target.ownerDocument;
  
  var formatter = Cc["@mozilla.org/toolkit/URLFormatterService;1"]
                    .getService(Ci.nsIURLFormatter);

  if (/^about:blocked/.test(errorDoc.documentURI)) {
    // The event came from a button on a malware/phishing block page
    // First check whether it's malware or phishing, so that we can
    // use the right strings/links
    var isMalware = /e=malwareBlocked/.test(errorDoc.documentURI);
    
    if (target == errorDoc.getElementById('getMeOutButton')) {
      // Instead of loading some safe page, just close the window
      window.close();
    } else if (target == errorDoc.getElementById('reportButton')) {
      // This is the "Why is this site blocked" button.  For malware,
      // we can fetch a site-specific report, for phishing, we redirect
      // to the generic page describing phishing protection.

      if (isMalware) {
        // Get the stop badware "why is this blocked" report url,
        // append the current url, and go there.
        try {
          let reportURL = formatter.formatURLPref("browser.safebrowsing.malware.reportURL", true);
          reportURL += errorDoc.location.href.slice(12);
          openURL(reportURL);
        } catch (e) {
          Components.utils.reportError("Couldn't get malware report URL: " + e);
        }
      } else { // It's a phishing site, not malware
        try {
          var infoURL = formatter.formatURLPref("browser.safebrowsing.warning.infoURL", true);
          openURL(infoURL);
        } catch (e) {
          Components.utils.reportError("Couldn't get phishing info URL: " + e);
        }
      }
    } else if (target == errorDoc.getElementById('ignoreWarningButton')) {
      // Allow users to override and continue through to the site,
      // but add a notify bar as a reminder, so that they don't lose
      // track after, e.g., tab switching.
      gBrowser.loadURIWithFlags(content.location.href,
                                Ci.nsIWebNavigation.LOAD_FLAGS_BYPASS_CLASSIFIER,
                                null, null, null);
    }
  }
}

function HandleAppCommandEvent(evt)
{
  evt.stopPropagation();
  switch (evt.command) {
    case "Back":
      BrowserBack();
      break;
    case "Forward":
      BrowserForward();
      break;
  }
}

function HandleSwipeGesture(evt) {
  evt.stopPropagation();
  switch (evt.direction) {
    case SimpleGestureEvent.DIRECTION_LEFT:
      BrowserBack();
      break;
    case SimpleGestureEvent.DIRECTION_RIGHT:
      BrowserForward();
      break;
    case SimpleGestureEvent.DIRECTION_UP:
      goDoCommand("cmd_scrollTop");
      break;
    case SimpleGestureEvent.DIRECTION_DOWN:
      goDoCommand("cmd_scrollBottom");
      break;
  }
}

function ViewSourceClose()
{
  window.close();
}

function ViewSourceReload()
{
  gBrowser.reloadWithFlags(Ci.nsIWebNavigation.LOAD_FLAGS_BYPASS_PROXY |
                           Ci.nsIWebNavigation.LOAD_FLAGS_BYPASS_CACHE);
}

// Strips the |view-source:| for editPage()
function ViewSourceEditPage()
{
  editPage(window.content.location.href.substring(12), window, false);
}

// Strips the |view-source:| for saveURL()
function ViewSourceSavePage()
{
  saveURL(window.content.location.href.substring(12), null, "SaveLinkTitle");
}

var PrintPreviewListener = {
  getPrintPreviewBrowser: function () {
    var browser = document.getElementById("ppBrowser");
    if (!browser) {
      browser = document.createElement("browser");
      browser.setAttribute("id", "ppBrowser");
      browser.setAttribute("flex", "1");
      document.getElementById("appcontent").
        insertBefore(browser, document.getElementById("FindToolbar"));
    }
    return browser;
  },
  getSourceBrowser: function () {
    return gBrowser;
  },
  getNavToolbox: function () {
    return document.getElementById("appcontent");
  },
  onEnter: function () {
    var toolbox = document.getElementById("viewSource-toolbox");
    toolbox.hidden = true;
    gBrowser.collapsed = true;
  },
  onExit: function () {
    document.getElementById("ppBrowser").collapsed = true;
    gBrowser.collapsed = false;
    document.getElementById("viewSource-toolbox").hidden = false;
  }
}

function getWebNavigation()
{
  try {
    return gBrowser.webNavigation;
  } catch (e) {
    return null;
  }
}

function ViewSourceGoToLine()
{
  var promptService = Cc["@mozilla.org/embedcomp/prompt-service;1"]
                        .getService(Ci.nsIPromptService);

  var input = {value:gLastLineFound};
  for (;;) {
    var ok = promptService.prompt(
        window,
        gViewSourceBundle.getString("goToLineTitle"),
        gViewSourceBundle.getString("goToLineText"),
        input,
        null,
        {value:0});

    if (!ok)
      return;

    var line = parseInt(input.value, 10);

    if (!(line > 0)) {
      promptService.alert(window,
                          gViewSourceBundle.getString("invalidInputTitle"),
                          gViewSourceBundle.getString("invalidInputText"));

      continue;
    }

    var found = goToLine(line);

    if (found)
      break;

    promptService.alert(window,
                        gViewSourceBundle.getString("outOfRangeTitle"),
                        gViewSourceBundle.getString("outOfRangeText"));
  }
}

function goToLine(line)
{
  var viewsource = window.content.document.body;

  // The source document is made up of a number of pre elements with
  // id attributes in the format <pre id="line123">, meaning that
  // the first line in the pre element is number 123.
  // Do binary search to find the pre element containing the line.

  var pre;
  for (var lbound = 0, ubound = viewsource.childNodes.length; ; ) {
    var middle = (lbound + ubound) >> 1;
    pre = viewsource.childNodes[middle];

    var firstLine = parseInt(pre.id.substring(4));

    if (lbound == ubound - 1) {
      break;
    }

    if (line >= firstLine) {
      lbound = middle;
    } else {
      ubound = middle;
    }
  }

  var result = {};
  var found = findLocation(pre, line, null, -1, false, result);

  if (!found) {
    return false;
  }

  var selection = window.content.getSelection();
  selection.removeAllRanges();

  // In our case, the range's startOffset is after "\n" on the previous line.
  // Tune the selection at the beginning of the next line and do some tweaking
  // to position the focusNode and the caret at the beginning of the line.

  selection.QueryInterface(Ci.nsISelectionPrivate)
    .interlinePosition = true;

  selection.addRange(result.range);

  if (!selection.isCollapsed) {
    selection.collapseToEnd();

    var offset = result.range.startOffset;
    var node = result.range.startContainer;
    if (offset < node.data.length) {
      // The same text node spans across the "\n", just focus where we were.
      selection.extend(node, offset);
    }
    else {
      // There is another tag just after the "\n", hook there. We need
      // to focus a safe point because there are edgy cases such as
      // <span>...\n</span><span>...</span> vs.
      // <span>...\n<span>...</span></span><span>...</span>
      node = node.nextSibling ? node.nextSibling : node.parentNode.nextSibling;
      selection.extend(node, 0);
    }
  }

  var selCon = getSelectionController();
  selCon.setDisplaySelection(Ci.nsISelectionController.SELECTION_ON);
  selCon.setCaretVisibilityDuringSelection(true);

  // Scroll the beginning of the line into view.
  selCon.scrollSelectionIntoView(
    Ci.nsISelectionController.SELECTION_NORMAL,
    Ci.nsISelectionController.SELECTION_FOCUS_REGION,
    true);

  gLastLineFound = line;

  document.getElementById("statusbar-line-col").label =
    gViewSourceBundle.getFormattedString("statusBarLineCol", [line, 1]);

  return true;
}

function updateStatusBar()
{
  // Reset the coalesce flag.
  gSelectionListener.timeout = 0;

  var statusBarField = document.getElementById("statusbar-line-col");

  var selection = window.content.getSelection();
  if (!selection.focusNode) {
    statusBarField.label = '';
    return;
  }
  if (selection.focusNode.nodeType != Node.TEXT_NODE) {
    return;
  }

  var selCon = getSelectionController();
  selCon.setDisplaySelection(Ci.nsISelectionController.SELECTION_ON);
  selCon.setCaretVisibilityDuringSelection(true);

  var interlinePosition = selection.QueryInterface(Ci.nsISelectionPrivate)
                                   .interlinePosition;

  var result = {};
  findLocation(null, -1, 
      selection.focusNode, selection.focusOffset, interlinePosition, result);

  statusBarField.label = gViewSourceBundle.getFormattedString(
                           "statusBarLineCol", [result.line, result.col]);
}

// Loops through the text lines in the pre element. The arguments are either
// (pre, line) or (node, offset, interlinePosition). result is an out
// argument. If (pre, line) are specified (and node == null), result.range is
// a range spanning the specified line. If the (node, offset,
// interlinePosition) are specified, result.line and result.col are the line
// and column number of the specified offset in the specified node relative to
// the whole file.
function findLocation(pre, line, node, offset, interlinePosition, result)
{
  if (node && !pre) {
    // Look upwards to find the current pre element.
    for (pre = node;
         pre.nodeName != "PRE";
         pre = pre.parentNode);
  }

  // The source document is made up of a number of pre elements with
  // id attributes in the format <pre id="line123">, meaning that
  // the first line in the pre element is number 123.
  var curLine = parseInt(pre.id.substring(4));

  // Walk through each of the text nodes and count newlines.
  var treewalker = window.content.document
      .createTreeWalker(pre, NodeFilter.SHOW_TEXT, null, false);

  // The column number of the first character in the current text node.
  var firstCol = 1;

  var found = false;
  for (var textNode = treewalker.firstChild();
       textNode && !found;
       textNode = treewalker.nextNode()) {

    // \r is not a valid character in the DOM, so we only check for \n.
    var lineArray = textNode.data.split(/\n/);
    var lastLineInNode = curLine + lineArray.length - 1;

    // Check if we can skip the text node without further inspection.
    if (node ? (textNode != node) : (lastLineInNode < line)) {
      if (lineArray.length > 1) {
        firstCol = 1;
      }
      firstCol += lineArray[lineArray.length - 1].length;
      curLine = lastLineInNode;
      continue;
    }

    // curPos is the offset within the current text node of the first
    // character in the current line.
    for (var i = 0, curPos = 0;
         i < lineArray.length;
         curPos += lineArray[i++].length + 1) {

      if (i > 0) {
        curLine++;
      }

      if (node) {
        if (offset >= curPos && offset <= curPos + lineArray[i].length) {
          // If we are right after the \n of a line and interlinePosition is
          // false, the caret looks as if it were at the end of the previous
          // line, so we display that line and column instead.

          if (i > 0 && offset == curPos && !interlinePosition) {
            result.line = curLine - 1;
            var prevPos = curPos - lineArray[i - 1].length;
            result.col = (i == 1 ? firstCol : 1) + offset - prevPos;
          } else {
            result.line = curLine;
            result.col = (i == 0 ? firstCol : 1) + offset - curPos;
          }
          found = true;

          break;
        }

      } else {
        if (curLine == line && !("range" in result)) {
          result.range = document.createRange();
          result.range.setStart(textNode, curPos);

          // This will always be overridden later, except when we look for
          // the very last line in the file (this is the only line that does
          // not end with \n).
          result.range.setEndAfter(pre.lastChild);

        } else if (curLine == line + 1) {
          result.range.setEnd(textNode, curPos - 1);
          found = true;
          break;
        }
      }
    }
  }

  return found || ("range" in result);
}

// Toggle long-line wrapping and sets the view_source.wrap_long_lines
// pref to persist the last state.
function wrapLongLines()
{
  var myWrap = window.content.document.body;

  if (myWrap.className == '')
    myWrap.className = 'wrap';
  else myWrap.className = '';

  // Since multiple viewsource windows are possible, another window could have
  // affected the pref, so instead of determining the new pref value via the current
  // pref value, we use myWrap.className.
  if (gPrefs) {
    try {
      if (myWrap.className == '') {
        gPrefs.setBoolPref("view_source.wrap_long_lines", false);
      }
      else {
        gPrefs.setBoolPref("view_source.wrap_long_lines", true);
      }
    } catch (ex) {
    }
  }
}

// Toggles syntax highlighting and sets the view_source.syntax_highlight
// pref to persist the last state.
function highlightSyntax()
{
  var highlightSyntaxMenu = document.getElementById("menu_highlightSyntax");
  var highlightSyntax = (highlightSyntaxMenu.getAttribute("checked") == "true");
  gPrefs.setBoolPref("view_source.syntax_highlight", highlightSyntax);

  gPageLoader.loadPage(gPageLoader.currentDescriptor, gPageLoader.DISPLAY_NORMAL);
}

// Fix for bug 136322: this function overrides the function in
// browser.js to call PageLoader.loadPage() instead of BrowserReloadWithFlags()
function BrowserSetForcedCharacterSet(aCharset)
{
  var docCharset = gBrowser.docShell.QueryInterface(Ci.nsIDocCharset);
  docCharset.charset = aCharset;
  if (isHistoryEnabled()) {
    gPageLoader.loadPage(gPageLoader.currentDescriptor,
                         gPageLoader.DISPLAY_NORMAL);
  } else {
    gBrowser.reloadWithFlags(Ci.nsIWebNavigation.LOAD_FLAGS_CHARSET_CHANGE);
  }
}

// fix for bug #229503
// we need to define BrowserSetForcedDetector() so that we can
// change auto-detect options in the "View | Character Encoding" menu.
// As with BrowserSetForcedCharacterSet(), call PageLoader.loadPage() 
// instead of BrowserReloadWithFlags()
function BrowserSetForcedDetector(doReload)
{
  gBrowser.documentCharsetInfo.forcedDetector = true; 
  if (doReload)
  {
    if (isHistoryEnabled()) {
      gPageLoader.loadPage(gPageLoader.currentDescriptor,
                           gPageLoader.DISPLAY_NORMAL);
    } else {
      gBrowser.reloadWithFlags(Ci.nsIWebNavigation.LOAD_FLAGS_CHARSET_CHANGE);
    }
  }
}

function BrowserForward(aEvent) {
  try {
    gBrowser.goForward();
  }
  catch(ex) {
  }
}

function BrowserBack(aEvent) {
  try {
    gBrowser.goBack();
  }
  catch(ex) {
  }
}

function UpdateBackForwardCommands() {
  var backBroadcaster = document.getElementById("Browser:Back");
  var forwardBroadcaster = document.getElementById("Browser:Forward");

  if (getWebNavigation().canGoBack)
    backBroadcaster.removeAttribute("disabled");
  else
    backBroadcaster.setAttribute("disabled", "true");

  if (getWebNavigation().canGoForward)
    forwardBroadcaster.removeAttribute("disabled");
  else
    forwardBroadcaster.setAttribute("disabled", "true");
}
