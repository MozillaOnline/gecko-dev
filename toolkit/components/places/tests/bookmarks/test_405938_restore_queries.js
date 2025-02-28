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
 * The Original Code is Bug 384370 code.
 *
 * The Initial Developer of the Original Code is Mozilla Corp.
 * Portions created by the Initial Developer are Copyright (C) 2008
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Dietrich Ayala <dietrich@mozilla.com> (Original Author)
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

Components.utils.import("resource://gre/modules/utils.js");
var tests = [];

/*

Backup/restore tests example:

var myTest = {
  populate: function () { ... add bookmarks ... },
  validate: function () { ... query for your bookmarks ... }
}

this.push(myTest);

*/

/*

test summary:
- create folders with content
- create a query bookmark for those folders
- backs up bookmarks
- restores bookmarks
- confirms that the query has the new ids for the same folders

scenarios:
- 1 folder  (folder shortcut)
- n folders (single query)
- n folders (multiple queries)

*/

const DEFAULT_INDEX = PlacesUtils.bookmarks.DEFAULT_INDEX;

var test = {
  _testRootId: null,
  _testRootTitle: "test root",
  _folderIds: [],
  _bookmarkURIs: [],
  _count: 3,

  populate: function populate() {
    // folder to hold this test
    PlacesUtils.bookmarks.removeFolderChildren(PlacesUtils.toolbarFolderId);
    this._testRootId =
      PlacesUtils.bookmarks.createFolder(PlacesUtils.toolbarFolderId,
                                         this._testRootTitle, DEFAULT_INDEX);

    // create test folders each with a bookmark
    for (var i = 0; i < this._count; i++) {
      var folderId = 
        PlacesUtils.bookmarks.createFolder(this._testRootId, "folder" + i, DEFAULT_INDEX);
      this._folderIds.push(folderId)
      
      var bookmarkURI = uri("http://" + i);
      PlacesUtils.bookmarks.insertBookmark(folderId, bookmarkURI,
                                           DEFAULT_INDEX, "bookmark" + i);
      this._bookmarkURIs.push(bookmarkURI);
    }

    // create a query URI with 1 folder (ie: folder shortcut)
    this._queryURI1 = uri("place:folder=" + this._folderIds[0] + "&queryType=1");
    this._queryTitle1 = "query1";
    PlacesUtils.bookmarks.insertBookmark(this._testRootId, this._queryURI1,
                                         DEFAULT_INDEX, this._queryTitle1);

    // create a query URI with _count folders
    this._queryURI2 = uri("place:folder=" + this._folderIds.join("&folder=") + "&queryType=1");
    this._queryTitle2 = "query2";
    PlacesUtils.bookmarks.insertBookmark(this._testRootId, this._queryURI2,
                                         DEFAULT_INDEX, this._queryTitle2);

    // create a query URI with _count queries (each with a folder)
    // first get a query object for each folder
    var queries = this._folderIds.map(function(aFolderId) {
      var query = PlacesUtils.history.getNewQuery();
      query.setFolders([aFolderId], 1);
      return query;
    });
    var options = PlacesUtils.history.getNewQueryOptions();
    options.queryType = options.QUERY_TYPE_BOOKMARKS;
    this._queryURI3 =
      uri(PlacesUtils.history.queriesToQueryString(queries, queries.length, options));
    this._queryTitle3 = "query3";
    PlacesUtils.bookmarks.insertBookmark(this._testRootId, this._queryURI3,
                                         DEFAULT_INDEX, this._queryTitle3);
  },

  clean: function () {},

  validate: function validate() {
    // Throw a wrench in the works by inserting some new bookmarks,
    // ensuring folder ids won't be the same, when restoring.
    for (var i = 0; i < 10; i++) {
      PlacesUtils.bookmarks.
                  insertBookmark(PlacesUtils.bookmarksMenuFolderId, uri("http://aaaa"+i), DEFAULT_INDEX, "");
    }

    var toolbar =
      PlacesUtils.getFolderContents(PlacesUtils.toolbarFolderId,
                                    false, true).root;
    do_check_true(toolbar.childCount, 1);

    var folderNode = toolbar.getChild(0);
    do_check_eq(folderNode.type, folderNode.RESULT_TYPE_FOLDER);
    do_check_eq(folderNode.title, this._testRootTitle);
    folderNode.QueryInterface(Ci.nsINavHistoryQueryResultNode);
    folderNode.containerOpen = true;

    // |_count| folders + the query node
    do_check_eq(folderNode.childCount, this._count+3);

    for (var i = 0; i < this._count; i++) {
      var subFolder = folderNode.getChild(i);
      do_check_eq(subFolder.title, "folder"+i);
      subFolder.QueryInterface(Ci.nsINavHistoryContainerResultNode);
      subFolder.containerOpen = true;
      do_check_eq(subFolder.childCount, 1);
      var child = subFolder.getChild(0);
      do_check_eq(child.title, "bookmark"+i);
      do_check_true(uri(child.uri).equals(uri("http://" + i)))
    }

    // validate folder shortcut
    this.validateQueryNode1(folderNode.getChild(this._count));

    // validate folders query
    this.validateQueryNode2(folderNode.getChild(this._count + 1));

    // validate multiple queries query
    this.validateQueryNode3(folderNode.getChild(this._count + 2));

    // clean up
    folderNode.containerOpen = false;
    toolbar.containerOpen = false;
  },

  validateQueryNode1: function validateQueryNode1(aNode) {
    do_check_eq(aNode.title, this._queryTitle1);
    do_check_true(PlacesUtils.nodeIsFolder(aNode));

    aNode.QueryInterface(Ci.nsINavHistoryContainerResultNode);
    aNode.containerOpen = true;
    do_check_eq(aNode.childCount, 1);
    var child = aNode.getChild(0);
    do_check_true(uri(child.uri).equals(uri("http://0")))
    do_check_eq(child.title, "bookmark0")
    aNode.containerOpen = false;
  },

  validateQueryNode2: function validateQueryNode2(aNode) {
    do_check_eq(aNode.title, this._queryTitle2);
    do_check_true(PlacesUtils.nodeIsQuery(aNode));

    aNode.QueryInterface(Ci.nsINavHistoryContainerResultNode);
    aNode.containerOpen = true;
    do_check_eq(aNode.childCount, this._count);
    for (var i = 0; i < aNode.childCount; i++) {
      var child = aNode.getChild(i);
      do_check_true(uri(child.uri).equals(uri("http://" + i)))
      do_check_eq(child.title, "bookmark" + i)
    }
    aNode.containerOpen = false;
  },

  validateQueryNode3: function validateQueryNode3(aNode) {
    do_check_eq(aNode.title, this._queryTitle3);
    do_check_true(PlacesUtils.nodeIsQuery(aNode));

    aNode.QueryInterface(Ci.nsINavHistoryContainerResultNode);
    aNode.containerOpen = true;
    do_check_eq(aNode.childCount, this._count);
    for (var i = 0; i < aNode.childCount; i++) {
      var child = aNode.getChild(i);
      do_check_true(uri(child.uri).equals(uri("http://" + i)))
      do_check_eq(child.title, "bookmark" + i)
    }
    aNode.containerOpen = false;
  }
}
tests.push(test);

function run_test() {
  do_check_eq(typeof PlacesUtils, "object");

  // make json file
  var jsonFile = dirSvc.get("ProfD", Ci.nsILocalFile);
  jsonFile.append("bookmarks.json");
  if (jsonFile.exists())
    jsonFile.remove(false);
  jsonFile.create(Ci.nsILocalFile.NORMAL_FILE_TYPE, 0600);
  if (!jsonFile.exists())
    do_throw("couldn't create file: bookmarks.exported.json");

  // populate db
  tests.forEach(function(aTest) {
    aTest.populate();
    // sanity
    aTest.validate();
  });

  // export json to file
  try {
    PlacesUtils.backups.saveBookmarksToJSONFile(jsonFile);
  } catch(ex) { do_throw("couldn't export to file: " + ex); }

  // clean
  tests.forEach(function(aTest) {
    aTest.clean();
  });

  // restore json file
  try {
    PlacesUtils.restoreBookmarksFromJSONFile(jsonFile);
  } catch(ex) { do_throw("couldn't import the exported file: " + ex); }

  // validate
  tests.forEach(function(aTest) {
    aTest.validate();
  });

  // clean up
  jsonFile.remove(false);
}
