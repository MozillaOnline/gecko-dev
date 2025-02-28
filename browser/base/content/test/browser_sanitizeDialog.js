/* -*- Mode: Java; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:set ts=2 sw=2 sts=2 et: */
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
 * The Original Code is sanitize dialog test code.
 *
 * The Initial Developer of the Original Code is Mozilla Corp.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Drew Willcoxon <adw@mozilla.com> (Original Author)
 *   Ehsan Akhgari <ehsan.akhgari@gmail.com>
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
 * Tests the sanitize dialog (a.k.a. the clear recent history dialog).
 * See bug 480169.
 *
 * The purpose of this test is not to fully flex the sanitize timespan code;
 * browser/base/content/test/browser_sanitize-timespans.js does that.  This
 * test checks the UI of the dialog and makes sure it's correctly connected to
 * the sanitize timespan code.
 *
 * Some of this code, especially the history creation parts, was taken from
 * browser/base/content/test/browser_sanitize-timespans.js.
 */

Cc["@mozilla.org/moz/jssubscript-loader;1"].
  getService(Components.interfaces.mozIJSSubScriptLoader).
  loadSubScript("chrome://mochikit/content/MochiKit/packed.js");

Cc["@mozilla.org/moz/jssubscript-loader;1"].
  getService(Components.interfaces.mozIJSSubScriptLoader).
  loadSubScript("chrome://browser/content/sanitize.js");

const winWatch = Cc["@mozilla.org/embedcomp/window-watcher;1"].
                 getService(Ci.nsIWindowWatcher);
const dm = Cc["@mozilla.org/download-manager;1"].
           getService(Ci.nsIDownloadManager);
const bhist = Cc["@mozilla.org/browser/global-history;2"].
              getService(Ci.nsIBrowserHistory);
const formhist = Cc["@mozilla.org/satchel/form-history;1"].
                 getService(Ci.nsIFormHistory2);

// Add tests here.  Each is a function that's called by doNextTest().
var gAllTests = [

  /**
   * Initializes the dialog to its default state.
   */
  function () {
    let wh = new WindowHelper();
    wh.onload = function () {
      // Select "Last Hour"
      this.selectDuration(Sanitizer.TIMESPAN_HOUR);
      // Hide details
      if (!this.getItemList().collapsed)
        this.toggleDetails();
      this.acceptDialog();
    };
    wh.open();
  },

  /**
   * Cancels the dialog, makes sure history not cleared.
   */
  function () {
    // Add history (within the past hour)
    let uris = [];
    for (let i = 0; i < 30; i++) {
      uris.push(addHistoryWithMinutesAgo(i));
    }

    let wh = new WindowHelper();
    wh.onload = function () {
      this.selectDuration(Sanitizer.TIMESPAN_HOUR);
      this.checkPrefCheckbox("history", false);
      this.checkDetails(false);

      // Show details
      this.toggleDetails();
      this.checkDetails(true);

      // Hide details
      this.toggleDetails();
      this.checkDetails(false);
      this.cancelDialog();

      ensureHistoryClearedState(uris, false);
      blankSlate();
      ensureHistoryClearedState(uris, true);
    };
    wh.open();
  },

  /**
   * Ensures that the combined history-downloads checkbox clears both history
   * visits and downloads when checked; the dialog respects simple timespan.
   */
  function () {
    // Add history and downloads (within the past hour).
    let uris = [];
    for (let i = 0; i < 30; i++) {
      uris.push(addHistoryWithMinutesAgo(i));
    }
    let downloadIDs = [];
    for (let i = 0; i < 5; i++) {
      downloadIDs.push(addDownloadWithMinutesAgo(i));
    }
    // Add history and downloads (over an hour ago).
    let olderURIs = [];
    for (let i = 0; i < 5; i++) {
      olderURIs.push(addHistoryWithMinutesAgo(61 + i));
    }
    let olderDownloadIDs = [];
    for (let i = 0; i < 5; i++) {
      olderDownloadIDs.push(addDownloadWithMinutesAgo(61 + i));
    }
    let totalHistoryVisits = uris.length + olderURIs.length;

    let wh = new WindowHelper();
    wh.onload = function () {
      this.selectDuration(Sanitizer.TIMESPAN_HOUR);
      this.checkPrefCheckbox("history", true);
      this.acceptDialog();

      intPrefIs("sanitize.timeSpan", Sanitizer.TIMESPAN_HOUR,
                "timeSpan pref should be hour after accepting dialog with " +
                "hour selected");
      boolPrefIs("cpd.history", true,
                 "history pref should be true after accepting dialog with " +
                 "history checkbox checked");
      boolPrefIs("cpd.downloads", true,
                 "downloads pref should be true after accepting dialog with " +
                 "history checkbox checked");

      // History visits and downloads within one hour should be cleared.
      ensureHistoryClearedState(uris, true);
      ensureDownloadsClearedState(downloadIDs, true);

      // Visits and downloads > 1 hour should still exist.
      ensureHistoryClearedState(olderURIs, false);
      ensureDownloadsClearedState(olderDownloadIDs, false);

      // OK, done, cleanup after ourselves.
      blankSlate();
      ensureHistoryClearedState(olderURIs, true);
      ensureDownloadsClearedState(olderDownloadIDs, true);
    };
    wh.open();
  },

  /**
   * Ensures that the combined history-downloads checkbox removes neither
   * history visits nor downloads when not checked.
   */
  function () {
    // Add history, downloads, form entries (within the past hour).
    let uris = [];
    for (let i = 0; i < 5; i++) {
      uris.push(addHistoryWithMinutesAgo(i));
    }
    let downloadIDs = [];
    for (let i = 0; i < 5; i++) {
      downloadIDs.push(addDownloadWithMinutesAgo(i));
    }
    let formEntries = [];
    for (let i = 0; i < 5; i++) {
      formEntries.push(addFormEntryWithMinutesAgo(i));
    }

    let wh = new WindowHelper();
    wh.onload = function () {
      is(this.isWarningPanelVisible(), false,
         "Warning panel should be hidden after previously accepting dialog " +
         "with a predefined timespan");
      this.selectDuration(Sanitizer.TIMESPAN_HOUR);

      // Remove only form entries, leave history (including downloads).
      this.checkPrefCheckbox("history", false);
      this.checkPrefCheckbox("formdata", true);
      this.acceptDialog();

      intPrefIs("sanitize.timeSpan", Sanitizer.TIMESPAN_HOUR,
                "timeSpan pref should be hour after accepting dialog with " +
                "hour selected");
      boolPrefIs("cpd.history", false,
                 "history pref should be false after accepting dialog with " +
                 "history checkbox unchecked");
      boolPrefIs("cpd.downloads", false,
                 "downloads pref should be false after accepting dialog with " +
                 "history checkbox unchecked");

      // Of the three only form entries should be cleared.
      ensureHistoryClearedState(uris, false);
      ensureDownloadsClearedState(downloadIDs, false);
      ensureFormEntriesClearedState(formEntries, true);

      // OK, done, cleanup after ourselves.
      blankSlate();
      ensureHistoryClearedState(uris, true);
      ensureDownloadsClearedState(downloadIDs, true);
    };
    wh.open();
  },

  /**
   * Ensures that the "Everything" duration option works.
   */
  function () {
    // Add history.
    let uris = [];
    uris.push(addHistoryWithMinutesAgo(10));  // within past hour
    uris.push(addHistoryWithMinutesAgo(70));  // within past two hours
    uris.push(addHistoryWithMinutesAgo(130)); // within past four hours
    uris.push(addHistoryWithMinutesAgo(250)); // outside past four hours

    let wh = new WindowHelper();
    wh.onload = function () {
      is(this.isWarningPanelVisible(), false,
         "Warning panel should be hidden after previously accepting dialog " +
         "with a predefined timespan");
      this.selectDuration(Sanitizer.TIMESPAN_EVERYTHING);
      this.checkPrefCheckbox("history", true);
      this.checkDetails(true);

      // Hide details
      this.toggleDetails();
      this.checkDetails(false);

      // Show details
      this.toggleDetails();
      this.checkDetails(true);

      this.acceptDialog();

      intPrefIs("sanitize.timeSpan", Sanitizer.TIMESPAN_EVERYTHING,
                "timeSpan pref should be everything after accepting dialog " +
                "with everything selected");
      ensureHistoryClearedState(uris, true);
    };
    wh.open();
  },

  /**
   * Ensures that the "Everything" warning is visible on dialog open after
   * the previous test.
   */
  function () {
    // Add history.
    let uris = [];
    uris.push(addHistoryWithMinutesAgo(10));  // within past hour
    uris.push(addHistoryWithMinutesAgo(70));  // within past two hours
    uris.push(addHistoryWithMinutesAgo(130)); // within past four hours
    uris.push(addHistoryWithMinutesAgo(250)); // outside past four hours

    let wh = new WindowHelper();
    wh.onload = function () {
      is(this.isWarningPanelVisible(), true,
         "Warning panel should be visible after previously accepting dialog " +
         "with clearing everything");
      this.selectDuration(Sanitizer.TIMESPAN_EVERYTHING);
      this.checkPrefCheckbox("history", true);
      this.acceptDialog();

      intPrefIs("sanitize.timeSpan", Sanitizer.TIMESPAN_EVERYTHING,
                "timeSpan pref should be everything after accepting dialog " +
                "with everything selected");
      ensureHistoryClearedState(uris, true);
    };
    wh.open();
  },

  /**
   * These next six tests together ensure that toggling details persists
   * across dialog openings.
   */
  function () {
    let wh = new WindowHelper();
    wh.onload = function () {
      // Check all items and select "Everything"
      this.checkAllCheckboxes();
      this.selectDuration(Sanitizer.TIMESPAN_EVERYTHING);

      // Hide details
      this.toggleDetails();
      this.checkDetails(false);
      this.acceptDialog();
    };
    wh.open();
  },
  function () {
    let wh = new WindowHelper();
    wh.onload = function () {
      // Details should remain closed because all items are checked.
      this.checkDetails(false);

      // Uncheck history.
      this.checkPrefCheckbox("history", false);
      this.acceptDialog();
    };
    wh.open();
  },
  function () {
    let wh = new WindowHelper();
    wh.onload = function () {
      // Details should be open because not all items are checked.
      this.checkDetails(true);

      // Modify the Site Preferences item state (bug 527820)
      this.checkAllCheckboxes();
      this.checkPrefCheckbox("siteSettings", false);
      this.acceptDialog();
    };
    wh.open();
  },
  function () {
    let wh = new WindowHelper();
    wh.onload = function () {
      // Details should be open because not all items are checked.
      this.checkDetails(true);

      // Hide details
      this.toggleDetails();
      this.checkDetails(false);
      this.cancelDialog();
    };
    wh.open();
  },
  function () {
    let wh = new WindowHelper();
    wh.onload = function () {
      // Details should be open because not all items are checked.
      this.checkDetails(true);

      // Select another duration
      this.selectDuration(Sanitizer.TIMESPAN_HOUR);
      // Hide details
      this.toggleDetails();
      this.checkDetails(false);
      this.acceptDialog();
    };
    wh.open();
  },
  function () {
    let wh = new WindowHelper();
    wh.onload = function () {
      // Details should not be open because "Last Hour" is selected
      this.checkDetails(false);

      this.cancelDialog();
    };
    wh.open();
  },
  function () {
    let wh = new WindowHelper();
    wh.onload = function () {
      // Details should have remained closed
      this.checkDetails(false);

      // Show details
      this.toggleDetails();
      this.checkDetails(true);
      this.cancelDialog();
    };
    wh.open();
  }
];

// Used as the download database ID for a new download.  Incremented for each
// new download.  See addDownloadWithMinutesAgo().
var gDownloadId = 5555551;

// Index in gAllTests of the test currently being run.  Incremented for each
// test run.  See doNextTest().
var gCurrTest = 0;

var now_uSec = Date.now() * 1000;

///////////////////////////////////////////////////////////////////////////////

/**
 * This wraps the dialog and provides some convenience methods for interacting
 * with it.
 *
 * @param aWin
 *        The dialog's nsIDOMWindow
 */
function WindowHelper(aWin) {
  this.win = aWin;
}

WindowHelper.prototype = {
  /**
   * "Presses" the dialog's OK button.
   */
  acceptDialog: function () {
    is(this.win.document.documentElement.getButton("accept").disabled, false,
       "Dialog's OK button should not be disabled");
    this.win.document.documentElement.acceptDialog();
  },

  /**
   * "Presses" the dialog's Cancel button.
   */
  cancelDialog: function () {
    this.win.document.documentElement.cancelDialog();
  },

  /**
   * Ensures that the details progressive disclosure button and the item list
   * hidden by it match up.  Also makes sure the height of the dialog is
   * sufficient for the item list and warning panel.
   *
   * @param aShouldBeShown
   *        True if you expect the details to be shown and false if hidden
   */
  checkDetails: function (aShouldBeShown) {
    let button = this.getDetailsButton();
    let list = this.getItemList();
    let hidden = list.hidden || list.collapsed;
    is(hidden, !aShouldBeShown,
       "Details should be " + (aShouldBeShown ? "shown" : "hidden") +
       " but were actually " + (hidden ? "hidden" : "shown"));
    let dir = hidden ? "down" : "up";
    is(button.className, "expander-" + dir,
       "Details button should be " + dir + " because item list is " +
       (hidden ? "" : "not ") + "hidden");
    let height = 0;
    if (!hidden)
      height += list.boxObject.height;
    if (this.isWarningPanelVisible())
      height += this.getWarningPanel().boxObject.height;
    ok(height < this.win.innerHeight,
       "Window should be tall enough to fit warning panel and item list");
  },

  /**
   * (Un)checks a history scope checkbox (browser & download history,
   * form history, etc.).
   *
   * @param aPrefName
   *        The final portion of the checkbox's privacy.cpd.* preference name
   * @param aCheckState
   *        True if the checkbox should be checked, false otherwise
   */
  checkPrefCheckbox: function (aPrefName, aCheckState) {
    var pref = "privacy.cpd." + aPrefName;
    var cb = this.win.document.querySelectorAll(
               "#itemList > [preference='" + pref + "']");
    is(cb.length, 1, "found checkbox for " + pref + " preference");
    if (cb[0].checked != aCheckState)
      cb[0].click();
  },

  /**
   * Makes sure all the checkboxes are checked.
   */
  checkAllCheckboxes: function () {
    var cb = this.win.document.querySelectorAll("#itemList > [preference]");
    ok(cb.length > 1, "found checkboxes for preferences");
    for (var i = 0; i < cb.length; ++i) {
      var pref = this.win.document.getElementById(cb[i].getAttribute("preference"));
      if (!pref.value)
        cb[i].click();
    }
  },

  /**
   * @return The details progressive disclosure button
   */
  getDetailsButton: function () {
    return this.win.document.getElementById("detailsExpander");
  },

  /**
   * @return The dialog's duration dropdown
   */
  getDurationDropdown: function () {
    return this.win.document.getElementById("sanitizeDurationChoice");
  },

  /**
   * @return The item list hidden by the details progressive disclosure button
   */
  getItemList: function () {
    return this.win.document.getElementById("itemList");
  },

  /**
   * @return The clear-everything warning box
   */
  getWarningPanel: function () {
    return this.win.document.getElementById("sanitizeEverythingWarningBox");
  },

  /**
   * @return True if the "Everything" warning panel is visible (as opposed to
   *         the tree)
   */
  isWarningPanelVisible: function () {
    return !this.getWarningPanel().hidden;
  },

  /**
   * Opens the clear recent history dialog.  Before calling this, set
   * this.onload to a function to execute onload.  It should close the dialog
   * when done so that the tests may continue.  Set this.onunload to a function
   * to execute onunload.  this.onunload is optional.
   */
  open: function () {
    let wh = this;

    function windowObserver(aSubject, aTopic, aData) {
      if (aTopic != "domwindowopened")
        return;

      winWatch.unregisterNotification(windowObserver);

      var loaded = false;
      let win = aSubject.QueryInterface(Ci.nsIDOMWindow);

      win.addEventListener("load", function onload(event) {
        win.removeEventListener("load", onload, false);

        if (win.name !== "SanitizeDialog")
          return;

        wh.win = win;
        loaded = true;

        executeSoon(function () {
          // Some exceptions that reach here don't reach the test harness, but
          // ok()/is() do...
          try {
            wh.onload();
          }
          catch (exc) {
            win.close();
            ok(false, "Unexpected exception: " + exc + "\n" + exc.stack);
            finish();
          }
        });
      }, false);

      win.addEventListener("unload", function onunload(event) {
        if (win.name !== "SanitizeDialog") {
          win.removeEventListener("unload", onunload, false);
          return;
        }

        // Why is unload fired before load?
        if (!loaded)
          return;

        win.removeEventListener("unload", onunload, false);
        wh.win = win;

        executeSoon(function () {
          // Some exceptions that reach here don't reach the test harness, but
          // ok()/is() do...
          try {
            if (wh.onunload)
              wh.onunload();
            doNextTest();
          }
          catch (exc) {
            win.close();
            ok(false, "Unexpected exception: " + exc + "\n" + exc.stack);
            finish();
          }
        });
      }, false);
    }
    winWatch.registerNotification(windowObserver);
    winWatch.openWindow(null,
                        "chrome://browser/content/sanitize.xul",
                        "SanitizeDialog",
                        "chrome,titlebar,dialog,centerscreen,modal",
                        null);
  },

  /**
   * Selects a duration in the duration dropdown.
   *
   * @param aDurVal
   *        One of the Sanitizer.TIMESPAN_* values
   */
  selectDuration: function (aDurVal) {
    this.getDurationDropdown().value = aDurVal;
    if (aDurVal === Sanitizer.TIMESPAN_EVERYTHING) {
      is(this.isWarningPanelVisible(), true,
         "Warning panel should be visible for TIMESPAN_EVERYTHING");
    }
    else {
      is(this.isWarningPanelVisible(), false,
         "Warning panel should not be visible for non-TIMESPAN_EVERYTHING");
    }
  },

  /**
   * Toggles the details progressive disclosure button.
   */
  toggleDetails: function () {
    this.getDetailsButton().click();
  }
};

/**
 * Adds a download to history.
 *
 * @param aMinutesAgo
 *        The download will be downloaded this many minutes ago
 */
function addDownloadWithMinutesAgo(aMinutesAgo) {
  let name = "fakefile-" + aMinutesAgo + "-minutes-ago";
  let data = {
    id:        gDownloadId,
    name:      name,
    source:   "https://bugzilla.mozilla.org/show_bug.cgi?id=480169",
    target:    name,
    startTime: now_uSec - (aMinutesAgo * 60 * 1000000),
    endTime:   now_uSec - ((aMinutesAgo + 1) *60 * 1000000),
    state:     Ci.nsIDownloadManager.DOWNLOAD_FINISHED,
    currBytes: 0, maxBytes: -1, preferredAction: 0, autoResume: 0
  };

  let db = dm.DBConnection;
  let stmt = db.createStatement(
    "INSERT INTO moz_downloads (id, name, source, target, startTime, endTime, " +
      "state, currBytes, maxBytes, preferredAction, autoResume) " +
    "VALUES (:id, :name, :source, :target, :startTime, :endTime, :state, " +
      ":currBytes, :maxBytes, :preferredAction, :autoResume)");
  try {
    for (let prop in data) {
      stmt.params[prop] = data[prop];
    }
    stmt.execute();
  }
  finally {
    stmt.reset();
  }

  is(downloadExists(gDownloadId), true,
     "Sanity check: download " + gDownloadId +
     " should exist after creating it");

  return gDownloadId++;
}

/**
 * Adds a form entry to history.
 *
 * @param aMinutesAgo
 *        The entry will be added this many minutes ago
 */
function addFormEntryWithMinutesAgo(aMinutesAgo) {
  let name = aMinutesAgo + "-minutes-ago";
  formhist.addEntry(name, "dummy");

  // Artifically age the entry to the proper vintage.
  let db = formhist.DBConnection;
  let timestamp = now_uSec - (aMinutesAgo * 60 * 1000000);
  db.executeSimpleSQL("UPDATE moz_formhistory SET firstUsed = " +
                      timestamp +  " WHERE fieldname = '" + name + "'");

  is(formhist.nameExists(name), true,
     "Sanity check: form entry " + name + " should exist after creating it");
  return name;
}

/**
 * Adds a history visit to history.
 *
 * @param aMinutesAgo
 *        The visit will be visited this many minutes ago
 */
function addHistoryWithMinutesAgo(aMinutesAgo) {
  let pURI = makeURI("http://" + aMinutesAgo + "-minutes-ago.com/");
  bhist.addPageWithDetails(pURI,
                           aMinutesAgo + " minutes ago",
                           now_uSec - (aMinutesAgo * 60 * 1000 * 1000));
  is(bhist.isVisited(pURI), true,
     "Sanity check: history visit " + pURI.spec +
     " should exist after creating it");
  return pURI;
}

/**
 * Removes all history visits, downloads, and form entries.
 */
function blankSlate() {
  bhist.removeAllPages();
  dm.cleanUp();
  formhist.removeAllEntries();
}

/**
 * Ensures that the given pref is the expected value.
 *
 * @param aPrefName
 *        The pref's sub-branch under the privacy branch
 * @param aExpectedVal
 *        The pref's expected value
 * @param aMsg
 *        Passed to is()
 */
function boolPrefIs(aPrefName, aExpectedVal, aMsg) {
  is(gPrefService.getBoolPref("privacy." + aPrefName), aExpectedVal, aMsg);
}

/**
 * Checks to see if the download with the specified ID exists.
 *
 * @param  aID
 *         The ID of the download to check
 * @return True if the download exists, false otherwise
 */
function downloadExists(aID)
{
  let db = dm.DBConnection;
  let stmt = db.createStatement(
    "SELECT * " +
    "FROM moz_downloads " +
    "WHERE id = :id"
  );
  stmt.params.id = aID;
  let rows = stmt.executeStep();
  stmt.finalize();
  return !!rows;
}

/**
 * Runs the next test in the gAllTests array.  If all tests have been run,
 * finishes the entire suite.
 */
function doNextTest() {
  if (gAllTests.length <= gCurrTest) {
    blankSlate();
    finish();
  }
  else {
    let ct = gCurrTest;
    gCurrTest++;
    gAllTests[ct]();
  }
}

/**
 * Ensures that the specified downloads are either cleared or not.
 *
 * @param aDownloadIDs
 *        Array of download database IDs
 * @param aShouldBeCleared
 *        True if each download should be cleared, false otherwise
 */
function ensureDownloadsClearedState(aDownloadIDs, aShouldBeCleared) {
  let niceStr = aShouldBeCleared ? "no longer" : "still";
  aDownloadIDs.forEach(function (id) {
    is(downloadExists(id), !aShouldBeCleared,
       "download " + id + " should " + niceStr + " exist");
  });
}

/**
 * Ensures that the specified form entries are either cleared or not.
 *
 * @param aFormEntries
 *        Array of form entry names
 * @param aShouldBeCleared
 *        True if each form entry should be cleared, false otherwise
 */
function ensureFormEntriesClearedState(aFormEntries, aShouldBeCleared) {
  let niceStr = aShouldBeCleared ? "no longer" : "still";
  aFormEntries.forEach(function (entry) {
    is(formhist.nameExists(entry), !aShouldBeCleared,
       "form entry " + entry + " should " + niceStr + " exist");
  });
}

/**
 * Ensures that the specified URIs are either cleared or not.
 *
 * @param aURIs
 *        Array of page URIs
 * @param aShouldBeCleared
 *        True if each visit to the URI should be cleared, false otherwise
 */
function ensureHistoryClearedState(aURIs, aShouldBeCleared) {
  let niceStr = aShouldBeCleared ? "no longer" : "still";
  aURIs.forEach(function (aURI) {
    is(bhist.isVisited(aURI), !aShouldBeCleared,
       "history visit " + aURI.spec + " should " + niceStr + " exist");
  });
}

/**
 * Ensures that the given pref is the expected value.
 *
 * @param aPrefName
 *        The pref's sub-branch under the privacy branch
 * @param aExpectedVal
 *        The pref's expected value
 * @param aMsg
 *        Passed to is()
 */
function intPrefIs(aPrefName, aExpectedVal, aMsg) {
  is(gPrefService.getIntPref("privacy." + aPrefName), aExpectedVal, aMsg);
}

///////////////////////////////////////////////////////////////////////////////

function test() {
  blankSlate();
  waitForExplicitFinish();
  // Kick off all the tests in the gAllTests array.
  doNextTest();
}
