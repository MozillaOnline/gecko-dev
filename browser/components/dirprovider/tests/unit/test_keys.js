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
 *   Ryan Flint <rflint@mozilla.com>
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

function test_usr_micsum() {
  let mdir = gProfD.clone();
  mdir.append("microsummary-generators");

  let tmdir = gDirSvc.get("UsrMicsumGens", Ci.nsIFile);
  do_check_true(tmdir.equals(mdir));

  if (!tmdir.exists())
    tmdir.create(Ci.nsIFile.DIRECTORY_TYPE, 0777);

  do_check_true(tmdir.isWritable());

  let tfile = writeTestFile(tmdir, "usrmicsum");
  do_check_true(tfile.exists());

  mdir.append(tfile.leafName);
  do_check_true(mdir.exists());
}

function test_bookmarkhtml() {
  let bmarks = gProfD.clone();
  bmarks.append("bookmarks.html");

  let tbmarks = gDirSvc.get("BMarks", Ci.nsIFile);
  do_check_true(bmarks.equals(tbmarks));
}

function test_prefoverride() {
  let dir = gDirSvc.get("DefRt", Ci.nsIFile);
  dir.append("existing-profile-defaults.js");

  let tdir = gDirSvc.get("ExistingPrefOverride", Ci.nsIFile);
  do_check_true(dir.equals(tdir));
}

function run_test() {
  [test_usr_micsum,
   test_bookmarkhtml,
   test_prefoverride
  ].forEach(function(f) {
    do_test_pending();
    print("Running test: " + f.name);
    f();
    do_test_finished();
  });
}
