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
 * The Original Code is sessionstore test code.
 *
 * The Initial Developer of the Original Code is
 * Nils Maier <maierman@web.de>
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 * Simon Bünzli <zeniko@gmail.com>
 * Paul O’Shannessy <paul@oshannessy.com>
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

/**
 * Checks that restoring the last browser window in session is actually
 * working:
 *  1.1) Open a new browser window
 *  1.2) Add some tabs
 *  1.3) Close that window
 *  1.4) Opening another window
 *  --> State is restored
 *
 *  2.1) Open a new browser window
 *  2.2) Add some tabs
 *  2.3) Enter private browsing mode
 *  2.4) Close the window while still in private browsing mode
 *  2.5) Opening a new window
 *  --> State is not restored, because private browsing mode is still active
 *  2.6) Leaving private browsing mode
 *  2.7) Open another window
 *  --> State (that was before entering PBM) is restored
 *
 *  3.1) Open a new browser window
 *  3.2) Add some tabs
 *  3.4) Open some popups
 *  3.5) Add another tab to one popup (so that it gets stored) and close it again
 *  3.5) Close the browser window
 *  3.6) Open another browser window
 *  --> State of the closed browser window, but not of the popup, is restored
 *
 *  4.1) Open a popup
 *  4.2) Add another tab to the popup (so that it gets stored) and close it again
 *  4.3) Open a window
 *  --> Nothing at all should be restored
 *
 *  5.1) Open two browser windows and close them again
 *  5.2) undoCloseWindow() one
 *  5.3) Open another browser window
 *  --> Nothing at all should be restored
 *
 * Checks the new notifications are correctly posted and processed, that is
 * for each successful -requested a -granted is received, but omitted if
 *  -requested was cnceled
 * Said notifications are:
 *  - browser-lastwindow-close-requested
 *  - browser-lastwindow-close-granted
 * Tests are:
 *  6) Cancel closing when first observe a -requested
 *  --> Window is kept open
 *  7) Count the number of notifications
 *  --> count(-requested) == count(-granted) + 1
 *  --> (The first -requested was canceled, so off-by-one)
 *  8) (Mac only) Mac version of Test 5 additionally preparing Test 6
 *
 * @see https://bugzilla.mozilla.org/show_bug.cgi?id=354894
 * @note It is implicitly tested that restoring the last window works when
 * non-browser windows are around. The "Run Tests" window as well as the main
 * browser window (wherein the test code gets executed) won't be considered
 * browser windows. To achiveve this said main browser window has it's windowtype
 * attribute modified so that it's not considered a browser window any longer.
 * This is crucial, because otherwise there would be two browser windows around,
 * said main test window and the one opened by the tests, and hence the new
 * logic wouldn't be executed at all.
 * @note Mac only tests the new notifications, as restoring the last window is
 * not enabled on that platform (platform shim; the application is kept running
 * although there are no windows left)
 * @note There is a difference when closing a browser window with
 * BrowserTryToCloseWindow() as opposed to close(). The former will make
 * nsSessionStore restore a window next time it gets a chance and will post
 * notifications. The latter won't.
 */

function browserWindowsCount(expected, msg) {
  if (typeof expected == "number")
    expected = [expected, expected];
  let count = 0;
  let e = Cc["@mozilla.org/appshell/window-mediator;1"]
            .getService(Ci.nsIWindowMediator)
            .getEnumerator("navigator:browser");
  while (e.hasMoreElements()) {
    if (!e.getNext().closed)
      ++count;
  }
  is(count, expected[0], msg + " (nsIWindowMediator)");
  let state = Cc["@mozilla.org/browser/sessionstore;1"]
                .getService(Ci.nsISessionStore)
                .getBrowserState();
  info(state);
  is(JSON.parse(state).windows.length, expected[1], msg + " (getBrowserState)");
}

function test() {
  browserWindowsCount(1, "Only one browser window should be open initially");

  waitForExplicitFinish();
  // This test takes some time to run, and it could timeout randomly.
  // So we require a longer timeout. See bug 528219.
  requestLongerTimeout(2);

  // Some urls that might be opened in tabs and/or popups
  // Do not use about:blank:
  // That one is reserved for special purposes in the tests
  const TEST_URLS = ["about:mozilla", "about:buildconfig"];

  // Number of -request notifications to except
  // remember to adjust when adding new tests
  const NOTIFICATIONS_EXPECTED = 6;

  // Window features of popup windows
  const POPUP_FEATURES = "toolbar=no,resizable=no,status=no";

  // Window features of browser windows
  const CHROME_FEATURES = "chrome,all,dialog=no";

  // Store the old window type for cleanup
  let oldWinType = "";
  // Store the old tabs.warnOnClose pref so that we may reset it during
  // cleanup
  let oldWarnTabsOnClose = gPrefService.getBoolPref("browser.tabs.warnOnClose");

  // Observe these, and also use to count the number of hits
  let observing = {
    "browser-lastwindow-close-requested": 0,
    "browser-lastwindow-close-granted": 0
  };

  /**
   * Helper: Will observe and handle the notifications for us
   */
  let hitCount = 0;
  function observer(aCancel, aTopic, aData) {
    // count so that we later may compare
    observing[aTopic]++;

    // handle some tests
    if (++hitCount == 1) {
      // Test 6
      aCancel.QueryInterface(Ci.nsISupportsPRBool).data = true;
    }
  }
  let observerService = Cc["@mozilla.org/observer-service;1"].
                        getService(Ci.nsIObserverService);

  /**
   * Helper: Sets prefs as the testsuite requires
   * @note Will be reset in cleanTestSuite just before finishing the tests
   */
  function setPrefs() {
    gPrefService.setIntPref("browser.startup.page", 3);
    gPrefService.setBoolPref(
      "browser.privatebrowsing.keep_current_session",
      true
    );
    gPrefService.setBoolPref("browser.tabs.warnOnClose", false);
  }

  /**
   * Helper: Sets up this testsuite
   */
  function setupTestsuite(testFn) {
    // Register our observers
    for (let o in observing)
      observerService.addObserver(observer, o, false);

    // Make the main test window not count as a browser window any longer
    oldWinType = document.documentElement.getAttribute("windowtype");
    document.documentElement.setAttribute("windowtype", "navigator:testrunner");
  }

  /**
   * Helper: Cleans up behind the testsuite
   */
  function cleanupTestsuite(callback) {
    // Finally remove observers again
    for (let o in observing)
      observerService.removeObserver(observer, o, false);

    // Reset the prefs we touched
    [
      "browser.startup.page",
      "browser.privatebrowsing.keep_current_session"
    ].forEach(function (pref) {
      if (gPrefService.prefHasUserValue(pref))
        gPrefService.clearUserPref(pref);
    });
    gPrefService.setBoolPref("browser.tabs.warnOnClose", oldWarnTabsOnClose);

    // Reset the window type
    document.documentElement.setAttribute("windowtype", oldWinType);
  }

  /**
   * Helper: sets the prefs and a new window with our test tabs
   */
  function setupTestAndRun(testFn) {
    // Prepare the prefs
    setPrefs();

    // Prepare a window; open it and add more tabs
    let newWin = openDialog(location, "_blank", CHROME_FEATURES, "about:config");
    newWin.addEventListener("load", function(aEvent) {
      newWin.removeEventListener("load", arguments.callee, false);
      newWin.gBrowser.addEventListener("load", function(aEvent) {
        newWin.gBrowser.removeEventListener("load", arguments.callee, true);
        TEST_URLS.forEach(function (url) {
          newWin.gBrowser.addTab(url);
        });

        executeSoon(function() testFn(newWin));
      }, true);
    }, false);
  }

  /**
   * Test 1: Normal in-session restore
   * @note: Non-Mac only
   */
  function testOpenCloseNormal(nextFn) {
    setupTestAndRun(function(newWin) {
      // Close the window
      // window.close doesn't push any close events,
      // so use BrowserTryToCloseWindow
      newWin.BrowserTryToCloseWindow();

      // The first request to close is denied by our observer (Test 6)
      ok(!newWin.closed, "First close request was denied");
      if (!newWin.closed) {
        newWin.BrowserTryToCloseWindow();
        ok(newWin.closed, "Second close request was granted");
      }

      // Open a new window
      // The previously closed window should be restored
      newWin = openDialog(location, "_blank", CHROME_FEATURES);
      newWin.addEventListener("load", function() {
        this.removeEventListener("load", arguments.callee, true);
        executeSoon(function() {
          is(newWin.gBrowser.browsers.length, TEST_URLS.length + 1,
             "Restored window in-session with otherpopup windows around");

          // Cleanup
          newWin.close();

          // Next please
          executeSoon(nextFn);
        });
      }, true);
    });
  }

  /**
   * Test 2: PrivateBrowsing in-session restore
   * @note: Non-Mac only
   */
  function testOpenClosePrivateBrowsing(nextFn) {
    setupTestAndRun(function(newWin) {
      let pb = Cc["@mozilla.org/privatebrowsing;1"].
               getService(Ci.nsIPrivateBrowsingService);

      // Close the window
      newWin.BrowserTryToCloseWindow();

      // Enter private browsing mode
      pb.privateBrowsingEnabled = true;

      // Open a new window.
      // The previously closed window should NOT be restored
      newWin = openDialog(location, "_blank", CHROME_FEATURES);
      newWin.addEventListener("load", function() {
        this.removeEventListener("load", arguments.callee, true);
        executeSoon(function() {
          is(newWin.gBrowser.browsers.length, 1,
             "Did not restore in private browing mode");

          // Cleanup
          newWin.BrowserTryToCloseWindow();

          // Exit private browsing mode again
          pb.privateBrowsingEnabled = false;

          newWin = openDialog(location, "_blank", CHROME_FEATURES);
          newWin.addEventListener("load", function() {
            this.removeEventListener("load", arguments.callee, true);
            executeSoon(function() {
              is(newWin.gBrowser.browsers.length, TEST_URLS.length + 1,
                 "Restored after leaving private browsing again");

              newWin.close();

              // Next please
              executeSoon(nextFn);
            });
          }, true);
        });
      }, true);
    });
  }

  /**
   * Test 3: Open some popup windows to check those aren't restored, but
   *         the browser window is
   * @note: Non-Mac only
   */
  function testOpenCloseWindowAndPopup(nextFn) {
    setupTestAndRun(function(newWin) {
      // open some popups
      let popup = openDialog(location, "popup", POPUP_FEATURES, TEST_URLS[0]);
      let popup2 = openDialog(location, "popup2", POPUP_FEATURES, TEST_URLS[1]);
      popup2.addEventListener("load", function() {
        popup2.removeEventListener("load", arguments.callee, false);
        popup2.gBrowser.addEventListener("load", function() {
          popup2.gBrowser.removeEventListener("load", arguments.callee, true);
          popup2.gBrowser.addTab(TEST_URLS[0]);
          // close the window
          newWin.BrowserTryToCloseWindow();

          // Close the popup window
          // The test is successful when not this popup window is restored
          // but instead newWin
          popup2.close();

          // open a new window the previously closed window should be restored to
          newWin = openDialog(location, "_blank", CHROME_FEATURES);
          newWin.addEventListener("load", function() {
            this.removeEventListener("load", arguments.callee, true);
            executeSoon(function() {
              is(newWin.gBrowser.browsers.length, TEST_URLS.length + 1,
                 "Restored window and associated tabs in session");

              // Cleanup
              newWin.close();
              popup.close();

              // Next please
              executeSoon(nextFn);
            });
          }, true);
        }, true);
      }, false);
    });
  }

  /**
   * Test 4: Open some popup window to check it isn't restored.
   *         Instead nothing at all should be restored
   * @note: Non-Mac only
   */
  function testOpenCloseOnlyPopup(nextFn) {
    // prepare the prefs
    setPrefs();

    // This will cause nsSessionStore to restore a window the next time it
    // gets a chance.
    let popup = openDialog(location, "popup", POPUP_FEATURES, TEST_URLS[1]);
    popup.addEventListener("load", function() {
      this.removeEventListener("load", arguments.callee, true);
      is(popup.gBrowser.browsers.length, 1,
         "Did not restore the popup window (1)");
      popup.BrowserTryToCloseWindow();

      // Real tests
      popup = openDialog(location, "popup", POPUP_FEATURES, TEST_URLS[1]);
      popup.addEventListener("load", function() {
        popup.removeEventListener("load", arguments.callee, false);
        popup.gBrowser.addEventListener("load", function() {
          popup.gBrowser.removeEventListener("load", arguments.callee, true);
          popup.gBrowser.addTab(TEST_URLS[0]);

          is(popup.gBrowser.browsers.length, 2,
             "Did not restore to the popup window (2)");

          // Close the popup window
          // The test is successful when not this popup window is restored
          // but instead a new window is opened without restoring anything
          popup.close();

          let newWin = openDialog(location, "_blank", CHROME_FEATURES, "about:blank");
          newWin.addEventListener("load", function() {
            newWin.removeEventListener("load", arguments.callee, true);
            executeSoon(function() {
              isnot(newWin.gBrowser.browsers.length, 2,
                    "Did not restore the popup window");
              is(TEST_URLS.indexOf(newWin.gBrowser.browsers[0].currentURI.spec), -1,
                 "Did not restore the popup window (2)");

              // Cleanup
              newWin.close();

              // Next please
              executeSoon(nextFn);
            });
          }, true);
        }, true);
      }, false);
    }, true);
  }

    /**
   * Test 5: Open some windows and do undoCloseWindow. This should prevent any
   *         restoring later in the test
   * @note: Non-Mac only
   */
  function testOpenCloseRestoreFromPopup(nextFn) {
    setupTestAndRun(function(newWin) {
      setupTestAndRun(function(newWin2) {
        newWin.BrowserTryToCloseWindow();
        newWin2.BrowserTryToCloseWindow();

        browserWindowsCount([0, 1], "browser windows while running testOpenCloseRestoreFromPopup");

        newWin = undoCloseWindow(0);
        newWin.addEventListener("load", function () {
          info(["testOpenCloseRestoreFromPopup: newWin loaded", newWin.closed, newWin.document]);
          var ds = newWin.delayedStartup;
          newWin.delayedStartup = function () {
            info(["testOpenCloseRestoreFromPopup: newWin delayedStartup", newWin.closed, newWin.document]);
            ds.apply(newWin, arguments);
          };
        }, false);
        newWin.addEventListener("unload", function () {
          info("testOpenCloseRestoreFromPopup: newWin unloaded");
        }, false);

        newWin2 = openDialog(location, "_blank", CHROME_FEATURES);
        newWin2.addEventListener("load", function() {
          newWin2.removeEventListener("load", arguments.callee, true);
          executeSoon(function() {
            is(newWin2.gBrowser.browsers.length, 1,
               "Did not restore, as undoCloseWindow() was last called");
            is(TEST_URLS.indexOf(newWin2.gBrowser.browsers[0].currentURI.spec), -1,
               "Did not restore, as undoCloseWindow() was last called (2)");

            browserWindowsCount([2, 3], "browser windows while running testOpenCloseRestoreFromPopup");

            info([newWin.closed, newWin.__SSi, newWin.__SS_restoreID, newWin.__SS_dyingCache]);
            info(newWin2.__SSi);

            // Cleanup
            newWin.close();
            newWin2.close();

            info([newWin.closed, newWin.__SSi, newWin.__SS_restoreID, newWin.__SS_dyingCache]);

            browserWindowsCount([0, 1], "browser windows while running testOpenCloseRestoreFromPopup");

            info([newWin.closed, newWin.__SSi, newWin.__SS_restoreID, newWin.__SS_dyingCache]);

            // Next please
            executeSoon(nextFn);
          });
        }, true);
      });
    });
  }

  /**
   * Test 7: Check whether the right number of notifications was received during
   *         the tests
   */
  function testNotificationCount(nextFn) {
    is(observing["browser-lastwindow-close-requested"], NOTIFICATIONS_EXPECTED,
       "browser-lastwindow-close-requested notifications observed");

    // -request must be one more as we cancel the first one we hit,
    // and hence won't produce a corresponding -grant
    // @see observer.observe
    is(observing["browser-lastwindow-close-requested"],
       observing["browser-lastwindow-close-granted"] + 1,
       "Notification count for -request and -grant matches");

    executeSoon(nextFn);
  }

  /**
   * Test 8: Test if closing can be denied on Mac
   *         Futhermore prepares the testNotificationCount test (Test 6)
   * @note: Mac only
   */
  function testMacNotifications(nextFn, iteration) {
    iteration = iteration || 1;
    setupTestAndRun(function(newWin) {
      // close the window
      // window.close doesn't push any close events,
      // so use BrowserTryToCloseWindow
      newWin.BrowserTryToCloseWindow();
      if (iteration == 1) {
        ok(!newWin.closed, "First close attempt denied");
        if (!newWin.closed) {
          newWin.BrowserTryToCloseWindow();
          ok(newWin.closed, "Second close attempt granted");
        }
      }

      if (iteration < NOTIFICATIONS_EXPECTED - 1) {
        executeSoon(function() testMacNotifications(nextFn, ++iteration));
      }
      else {
        executeSoon(nextFn);
      }
    });
  }

  // Execution starts here

  setupTestsuite();
  if (navigator.platform.match(/Mac/)) {
    // Mac tests
    testMacNotifications(function () {
      testNotificationCount(function () {
        cleanupTestsuite();
        browserWindowsCount(1, "Only one browser window should be open eventually");
        finish();
      });
    });
  }
  else {
    // Non-Mac Tests
    testOpenCloseNormal(function () {
      browserWindowsCount([0, 1], "browser windows after testOpenCloseNormal");
      testOpenClosePrivateBrowsing(function () {
        browserWindowsCount([0, 1], "browser windows after testOpenClosePrivateBrowsing");
        testOpenCloseWindowAndPopup(function () {
          browserWindowsCount([0, 1], "browser windows after testOpenCloseWindowAndPopup");
          testOpenCloseOnlyPopup(function () {
            browserWindowsCount([0, 1], "browser windows after testOpenCloseOnlyPopup");
            testOpenCloseRestoreFromPopup(function () {
              browserWindowsCount([0, 1], "browser windows after testOpenCloseRestoreFromPopup");
              testNotificationCount(function () {
                cleanupTestsuite();
                browserWindowsCount(1, "browser windows after testNotificationCount");
                finish();
              });
            });
          });
        });
      });
    });
  }
}
