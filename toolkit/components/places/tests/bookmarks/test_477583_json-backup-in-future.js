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
 * The Original Code is Bug 466303 code.
 *
 * The Initial Developer of the Original Code is
 * Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2008
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

Components.utils.import("resource://gre/modules/utils.js");

function run_test() {
  let bookmarksBackupDir = PlacesUtils.backups.folder;
  // Remove all files from backups folder.
  let files = bookmarksBackupDir.directoryEntries;
  while (files.hasMoreElements()) {
    let entry = files.getNext().QueryInterface(Ci.nsIFile);
    entry.remove(false);
  }

  // Create a json dummy backup in the future.
  let dateObj = new Date();
  dateObj.setYear(dateObj.getFullYear() + 1);
  let name = PlacesUtils.backups.getFilenameForDate(dateObj);
  do_check_eq(name, "bookmarks-" + dateObj.toLocaleFormat("%Y-%m-%d") + ".json");
  let futureBackupFile = bookmarksBackupDir.clone();
  futureBackupFile.append(name);
  if (futureBackupFile.exists())
    futureBackupFile.remove(false);
  do_check_false(futureBackupFile.exists());
  futureBackupFile.create(Ci.nsILocalFile.NORMAL_FILE_TYPE, 0600);
  do_check_true(futureBackupFile.exists());

  do_check_eq(PlacesUtils.backups.entries.length, 0);

  PlacesUtils.backups.create();

  // Check that a backup for today has been created.
  do_check_eq(PlacesUtils.backups.entries.length, 1);
  let mostRecentBackupFile = PlacesUtils.backups.getMostRecent();
  do_check_neq(mostRecentBackupFile, null);
  let todayName = PlacesUtils.backups.getFilenameForDate();
  do_check_eq(mostRecentBackupFile.leafName, todayName);

  // Check that future backup has been removed.
  do_check_false(futureBackupFile.exists());

  // Cleanup.
  mostRecentBackupFile.remove(false);
  do_check_false(mostRecentBackupFile.exists());
}
