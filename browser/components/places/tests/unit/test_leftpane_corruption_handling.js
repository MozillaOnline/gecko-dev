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
 * The Original Code is Places Unit Test code.
 *
 * The Initial Developer of the Original Code is Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Marco Bonardo <mak77@bonardo.net>
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
 * Tests that we build a working leftpane in various corruption situations.
 */

// Used to store the original leftPaneFolderId getter.
let gLeftPaneFolderIdGetter;
let gAllBookmarksFolderIdGetter;
// Used to store the original left Pane status as a JSON string.
let gReferenceJSON;
let gLeftPaneFolderId;
// Third party annotated folder.
let gFolderId;

// Corruption cases.
let gTests = [

  function test1() {
    print("1. Do nothing, checks test calibration.");
  },

  function test2() {
    print("2. Delete the left pane folder.");
    PlacesUtils.bookmarks.removeItem(gLeftPaneFolderId);
  },

  function test3() {
    print("3. Delete a child of the left pane folder.");
    let id = PlacesUtils.bookmarks.getIdForItemAt(gLeftPaneFolderId, 0);
    PlacesUtils.bookmarks.removeItem(id);
  },

  function test4() {
    print("4. Delete AllBookmarks.");
    PlacesUtils.bookmarks.removeItem(PlacesUIUtils.allBookmarksFolderId);
  },

  function test5() {
    print("5. Create a duplicated left pane folder.");
    let id = PlacesUtils.bookmarks.createFolder(PlacesUtils.unfiledBookmarksFolderId,
                                                "PlacesRoot",
                                                PlacesUtils.bookmarks.DEFAULT_INDEX);
    PlacesUtils.annotations.setItemAnnotation(id, ORGANIZER_FOLDER_ANNO,
                                              "PlacesRoot", 0,
                                              PlacesUtils.annotations.EXPIRE_NEVER);
  },

  function test6() {
    print("6. Create a duplicated left pane query.");
    let id = PlacesUtils.bookmarks.createFolder(PlacesUtils.unfiledBookmarksFolderId,
                                                "AllBookmarks",
                                                PlacesUtils.bookmarks.DEFAULT_INDEX);
    PlacesUtils.annotations.setItemAnnotation(id, ORGANIZER_QUERY_ANNO,
                                              "AllBookmarks", 0,
                                              PlacesUtils.annotations.EXPIRE_NEVER);
  },

  function test7() {
    print("7. Remove the left pane folder annotation.");
    PlacesUtils.annotations.removeItemAnnotation(gLeftPaneFolderId,
                                                 ORGANIZER_FOLDER_ANNO);
  },

  function test8() {
    print("8. Remove a left pane query annotation.");
    PlacesUtils.annotations.removeItemAnnotation(PlacesUIUtils.allBookmarksFolderId,
                                                 ORGANIZER_QUERY_ANNO);
  },

  function test9() {
    print("9. Remove a child of AllBookmarks.");
    let id = PlacesUtils.bookmarks.getIdForItemAt(PlacesUIUtils.allBookmarksFolderId, 0);
    PlacesUtils.bookmarks.removeItem(id);
  },

];

function run_test() {
  // We want empty roots.
  remove_all_bookmarks();

  // Import PlacesUIUtils.
  let scriptLoader = Cc["@mozilla.org/moz/jssubscript-loader;1"].
                     getService(Ci.mozIJSSubScriptLoader);
  scriptLoader.loadSubScript("chrome://browser/content/places/utils.js", this);
  do_check_true(!!PlacesUIUtils);

  // Check getters.
  gLeftPaneFolderIdGetter = PlacesUIUtils.__lookupGetter__("leftPaneFolderId");
  do_check_eq(typeof(gLeftPaneFolderIdGetter), "function");
  gAllBookmarksFolderIdGetter = PlacesUIUtils.__lookupGetter__("allBookmarksFolderId");
  do_check_eq(typeof(gAllBookmarksFolderIdGetter), "function");

  // Add a third party bogus annotated item.  Should not be removed.
  gFolderId = PlacesUtils.bookmarks.createFolder(PlacesUtils.unfiledBookmarksFolderId,
                                                 "test",
                                                 PlacesUtils.bookmarks.DEFAULT_INDEX);
  PlacesUtils.annotations.setItemAnnotation(gFolderId, ORGANIZER_QUERY_ANNO,
                                            "test", 0,
                                            PlacesUtils.annotations.EXPIRE_NEVER);

  // Create the left pane, and store its current status, it will be used
  // as reference value.
  gLeftPaneFolderId = PlacesUIUtils.leftPaneFolderId;
  gReferenceJSON = folderToJSON(gLeftPaneFolderId);

  // Kick-off tests.
  do_test_pending();
  do_timeout(0, run_next_test);
}

function run_next_test() {
  if (gTests.length) {
    // Create corruption.
    let test = gTests.shift();
    test();
    // Regenerate getters.
    PlacesUIUtils.__defineGetter__("leftPaneFolderId", gLeftPaneFolderIdGetter);
    gLeftPaneFolderId = PlacesUIUtils.leftPaneFolderId;
    PlacesUIUtils.__defineGetter__("allBookmarksFolderId", gAllBookmarksFolderIdGetter);
    // Check the new left pane folder.
    let leftPaneJSON = folderToJSON(gLeftPaneFolderId);
    do_check_true(compareJSON(gReferenceJSON, leftPaneJSON));
    do_check_eq(PlacesUtils.bookmarks.getItemTitle(gFolderId), "test");
    // Go to next test.
    do_timeout(0, run_next_test);
  }
  else {
    // All tests finished.
    remove_all_bookmarks();
    do_test_finished();
  }
}

/**
 * Convert a folder item id to a JSON representation of it and its contents.
 */
function folderToJSON(aItemId) {
  let query = PlacesUtils.history.getNewQuery();
  query.setFolders([aItemId], 1);
  let options = PlacesUtils.history.getNewQueryOptions();
  options.queryType = Ci.nsINavHistoryQueryOptions.QUERY_TYPE_BOOKMARKS;
  let root = PlacesUtils.history.executeQuery(query, options).root;
  let writer = {
    value: "",
    write: function PU_wrapNode__write(aStr, aLen) {
      this.value += aStr;
    }
  };
  PlacesUtils.serializeNodeAsJSONToOutputStream(root, writer, false, false);
  do_check_true(writer.value.length > 0);
  return writer.value;
}

/**
 * Compare the JSON representation of 2 nodes, skipping everchanging properties
 * like dates.
 */
function compareJSON(aNodeJSON_1, aNodeJSON_2) {
  let JSON = Cc["@mozilla.org/dom/json;1"].createInstance(Ci.nsIJSON);
  node1 = JSON.decode(aNodeJSON_1);
  node2 = JSON.decode(aNodeJSON_2);

  // List of properties we should not compare (expected to be different).
  const SKIP_PROPS = ["dateAdded", "lastModified", "id"];

  function compareObjects(obj1, obj2) {
    do_check_eq(obj1.__count__, obj2.__count__);
    for (let prop in obj1) {
      // Skip everchanging values.
      if (SKIP_PROPS.indexOf(prop) != -1)
        continue;
      // Skip undefined objects, otherwise we hang on them.
      if (!obj1[prop])
        continue;
      if (typeof(obj1[prop]) == "object")
        return compareObjects(obj1[prop], obj2[prop]);
      if (obj1[prop] !== obj2[prop]) {
        print(prop + ": " + obj1[prop] + "!=" + obj2[prop]);
        return false;
      }
    }
    return true;
  }

  return compareObjects(node1, node2);
}
