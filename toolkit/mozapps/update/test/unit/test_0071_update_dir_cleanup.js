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
 * The Original Code is the Application Update Service.
 *
 * The Initial Developer of the Original Code is
 * Robert Strong <robert.bugzilla@gmail.com>.
 *
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Mozilla Foundation <http://www.mozilla.org/>. All Rights Reserved.
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
 * ***** END LICENSE BLOCK *****
 */

/* General Update Directory Cleanup Tests */

function run_test() {
  removeUpdateDirsAndFiles();
  var defaults = getPrefBranch().QueryInterface(AUS_Ci.nsIPrefService).
                 getDefaultBranch(null);
  defaults.setCharPref("app.update.channel", "bogus_channel");

  writeUpdatesToXMLFile(getLocalUpdatesXMLString(""), false);
  var patches = getLocalPatchString(null, null, null, null, null, null,
                                    STATE_PENDING);
  var updates = getLocalUpdateString(patches);
  writeUpdatesToXMLFile(getLocalUpdatesXMLString(updates), true);
  writeStatusFile(STATE_SUCCEEDED);

  var dir = getUpdatesDir();
  var log = dir.clone();
  log.append(FILE_LAST_LOG);
  writeFile(log, "Backup Update Log");

  log = dir.clone();
  log.append(FILE_BACKUP_LOG);
  writeFile(log, "To Be Deleted Backup Update Log");

  log = dir.clone();
  log.append("0");
  log.append(FILE_UPDATE_LOG);
  writeFile(log, "Last Update Log");

  startAUS();

  dump("Testing: " + FILE_UPDATE_LOG + " doesn't exist\n");
  do_check_false(log.exists());

  dump("Testing: " + FILE_LAST_LOG + " exists\n");
  log = dir.clone();
  log.append(FILE_LAST_LOG);
  do_check_true(log.exists());

  dump("Testing: " + FILE_LAST_LOG + " contents\n");
  do_check_eq(readFile(log), "Last Update Log");

  dump("Testing: " + FILE_BACKUP_LOG + " exists\n");
  log = dir.clone();
  log.append(FILE_BACKUP_LOG);
  do_check_true(log.exists());

  dump("Testing: " + FILE_BACKUP_LOG + " contents (bug 470979)\n");
  do_check_eq(readFile(log), "Backup Update Log");

  dump("Testing: " + dir.path + " exists (bug 512994)\n");
  dir.append("0");
  do_check_true(dir.exists());

  cleanUp();
}
