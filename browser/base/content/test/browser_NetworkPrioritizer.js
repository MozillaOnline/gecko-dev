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
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Paul O’Shannessy <paul@oshannessy.com> (original author)
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

function test() {
  /** Tests for NetworkPrioritizer.jsm (Bug 514490) **/

  waitForExplicitFinish();

  const PRIORITY_DELTA = -10; // same as in NetworkPrioritizer

  // Test helper functions.
  // getPriority and setPriority can take a Tab or a Browser
  function getPriority(aBrowser) {
    // Assume we were passed a tab if it's not a browser
    if (!aBrowser.webNavigation)
      aBrowser = aBrowser.linkedBrowser;
    return aBrowser.webNavigation.QueryInterface(Ci.nsIDocumentLoader)
                   .loadGroup.QueryInterface(Ci.nsISupportsPriority).priority;
  }
  function setPriority(aBrowser, aPriority) {
    if (!aBrowser.webNavigation)
      aBrowser = aBrowser.linkedBrowser;
    aBrowser.webNavigation.QueryInterface(Ci.nsIDocumentLoader)
            .loadGroup.QueryInterface(Ci.nsISupportsPriority).priority = aPriority;
  }

  function isWindowState(aWindow, aTabPriorities) {
    let browsers = aWindow.gBrowser.browsers;
    // Make sure we have the right number of tabs & priorities
    is(browsers.length, aTabPriorities.length,
       "Window has expected number of tabs");
    // aState should be in format [ priority, priority, priority ]
    for (let i = 0; i < browsers.length; i++) {
      is(getPriority(browsers[i]), aTabPriorities[i],
         "Tab had expected priority");
    }
  }


  // This is the real test. It creates multiple tabs & windows, changes focus,
  // closes windows/tabs to make sure we behave correctly.
  // This test assumes that no priorities have been adjusted and the loadgroup
  // priority starts at 0.
  function test_behavior() {

    // Call window "window_A" to make the test easier to follow
    let window_A = window;

    // Test 1 window, 1 tab case.
    isWindowState(window_A, [-10]);

    // Exising tab is tab_A1
    let tab_A2 = window_A.gBrowser.addTab("http://example.com");
    let tab_A3 = window_A.gBrowser.addTab("about:config");
    tab_A3.linkedBrowser.addEventListener("load", function(aEvent) {
      tab_A3.removeEventListener("load", arguments.callee, true);

      // tab_A2 isn't focused yet
      isWindowState(window_A, [-10, 0, 0]);

      // focus tab_A2 & make sure priority got updated
      window_A.gBrowser.selectedTab = tab_A2;
      isWindowState(window_A, [0, -10, 0]);

      window_A.gBrowser.removeTab(tab_A2);
      // Next tab is auto selected
      isWindowState(window_A, [0, -10]);

      // Open another window then play with focus
      let window_B = openDialog(location, "_blank", "chrome,all,dialog=no", "http://example.com");
      window_B.addEventListener("load", function(aEvent) {
        window_B.removeEventListener("load", arguments.callee, false);
        window_B.gBrowser.addEventListener("load", function(aEvent) {
          // waitForFocus can attach to the wrong "window" with about:blank loading first
          // So just ensure that we're getting the load event for the right URI
          if (window_B.gBrowser.currentURI.spec == "about:blank")
            return;
          window_B.gBrowser.removeEventListener("load", arguments.callee, true);

          waitForFocus(function() {
            isWindowState(window_A, [10, 0]);
            isWindowState(window_B, [-10]);

            waitForFocus(function() {
              isWindowState(window_A, [0, -10]);
              isWindowState(window_B, [0]);

              waitForFocus(function() {
                isWindowState(window_A, [10, 0]);
                isWindowState(window_B, [-10]);

                // And we're done. Cleanup & run the next test
                window_B.close();
                window_A.gBrowser.removeTab(tab_A3);
                executeSoon(runNextTest);
              }, window_B);
            }, window_A);
          }, window_B);

        }, true);
      }, false);
    }, true);

  }


  // This is more a test of nsLoadGroup and how it handles priorities. But since
  // we depend on its behavior, it's good to test it. This is testing that there
  // are no errors if we adjust beyond nsISupportsPriority's bounds.
  function test_extremePriorities() {
    let tab_A1 = gBrowser.tabContainer.getItemAtIndex(0);
    let oldPriority = getPriority(tab_A1);

    // Set the priority of tab_A1 to the lowest possible. Selecting the other tab
    // will try to lower it
    setPriority(tab_A1, Ci.nsISupportsPriority.PRIORITY_LOWEST);

    let tab_A2 = gBrowser.addTab("http://example.com");
    tab_A2.linkedBrowser.addEventListener("load", function(aEvent) {
      tab_A2.removeEventListener("load", arguments.callee, true);
      gBrowser.selectedTab = tab_A2;
      is(getPriority(tab_A1), Ci.nsISupportsPriority.PRIORITY_LOWEST - PRIORITY_DELTA,
         "Can adjust priority beyond 'lowest'");

      // Now set priority to "highest" and make sure that no errors occur.
      setPriority(tab_A1, Ci.nsISupportsPriority.PRIORITY_HIGHEST);
      gBrowser.selectedTab = tab_A1;

      is(getPriority(tab_A1), Ci.nsISupportsPriority.PRIORITY_HIGHEST + PRIORITY_DELTA,
         "Can adjust priority beyond 'highest'");

      // Cleanup, run next test
      gBrowser.removeTab(tab_A2);
      executeSoon(function() {
        setPriority(tab_A1, oldPriority);
        runNextTest();
      });

    }, true);
  }


  let tests = [test_behavior, test_extremePriorities];
  function runNextTest() {
    if (tests.length) {
      // Linux has problems if window isn't focused. Should help prevent [orange].
      waitForFocus(tests.shift());
    } else {
      finish();
    }
  }

  runNextTest();
}
