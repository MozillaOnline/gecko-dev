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
 * The Original Code is Mozilla XUL Toolkit Testing Code.
 *
 * The Initial Developer of the Original Code is
 * Paolo Amadini <http://www.amadzone.org/>.
 * Portions created by the Initial Developer are Copyright (C) 2009
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

/**
 * Test for bug 471962 <https://bugzilla.mozilla.org/show_bug.cgi?id=471962>:
 * When saving an inner frame as file only, the POST data of the outer page is
 * sent to the address of the inner page.
 */
function test() {

  // --- Testing support library ---

  // Import the toolkit test support library in the scope of the current test.
  // This operation also defines the common constants Cc, Ci, Cu, Cr and Cm.
  Components.classes["@mozilla.org/moz/jssubscript-loader;1"].
   getService(Components.interfaces.mozIJSSubScriptLoader).loadSubScript(
   "chrome://mochikit/content/browser/toolkit/content/tests/browser/common/_loadAll.js",
   this);

  // --- Test implementation ---

  const kBaseUrl =
        "http://localhost:8888/browser/toolkit/content/tests/browser/data/";

  function FramePostData_TestGenerator() {
    // Display the outer page, and wait for it to be loaded. Loading the URI
    // doesn't generally raise any exception, but if an error page is
    // displayed, an exception will occur later during the test.
    gBrowser.addEventListener("pageshow", testRunner.continueTest, false);
    gBrowser.loadURI(kBaseUrl + "post_form_outer.sjs");
    yield;
    gBrowser.removeEventListener("pageshow", testRunner.continueTest, false);

    try {
      // Submit the form in the outer page, then wait for both the outer
      // document and the inner frame to be loaded again.
      gBrowser.addEventListener("DOMContentLoaded",
                                testRunner.continueAfterTwoEvents, false);
      try {
        gBrowser.contentDocument.getElementById("postForm").submit();
        yield;
      }
      finally {
        // Remove the event listener, even if an exception occurred for any
        // reason (for example, the requested element does not exist). 
        gBrowser.removeEventListener("DOMContentLoaded",
                                     testRunner.continueAfterTwoEvents, false);
      }

      // Save a reference to the inner frame in the reloaded page for later.
      var innerFrame = gBrowser.contentDocument.getElementById("innerFrame");

      // Submit the form in the inner page.
      gBrowser.addEventListener("DOMContentLoaded",
                                testRunner.continueTest, false);
      try {
        innerFrame.contentDocument.getElementById("postForm").submit();
        yield;
      }
      finally {
        // Remove the event listener, even if an exception occurred for any
        // reason (for example, the requested element does not exist). 
        gBrowser.removeEventListener("DOMContentLoaded",
                                     testRunner.continueTest, false);
      }

      // Create the folder the page will be saved into.
      var destDir = createTemporarySaveDirectory();
      try {
        // Call the appropriate save function defined in contentAreaUtils.js.
        mockFilePickerSettings.destDir = destDir;
        mockFilePickerSettings.filterIndex = 1; // kSaveAsType_URL
        callSaveWithMockObjects(function() {
          var docToSave = innerFrame.contentDocument;
          // We call internalSave instead of saveDocument to bypass the history
          // cache.
          internalSave(docToSave.location.href, docToSave, null, null,
                       docToSave.contentType, false, null, null,
                       docToSave.referrer ? makeURI(docToSave.referrer) : null,
                       false, null);
        });

        // Wait for the download to finish, and exit if it wasn't successful.
        var downloadSuccess = yield;
        if (!downloadSuccess)
          throw "Unexpected failure, the inner frame couldn't be saved!";

        // Read the entire saved file.
        var fileContents = readShortFile(mockFilePickerResults.selectedFile);

        // Check if outer POST data is found.
        const searchPattern = "inputfield=outer";
        ok(fileContents.indexOf(searchPattern) === -1,
           "The saved inner frame does not contain outer POST data");
      }
      finally {
        // Clean up the saved file.
        destDir.remove(true);
      }
    }
    finally {
      // Replace the current tab with a clean one.
      gBrowser.addTab().linkedBrowser.stop();
      gBrowser.removeCurrentTab();
    }
  }

  // --- Run the test ---

  testRunner.runTest(FramePostData_TestGenerator);
}
