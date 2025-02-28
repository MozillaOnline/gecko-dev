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
 * The Original Code is the Update Timer Manager.
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

/* General Update Timer Manager Tests */

const Cc = Components.classes;
const Ci = Components.interfaces;
const Cm = Components.manager;
const Cr = Components.results;

Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");

const CATEGORY_UPDATE_TIMER = "update-timer";

const PREF_APP_UPDATE_TIMER = "app.update.timer";
const PREF_APP_UPDATE_LOG_ALL = "app.update.log.all";
const PREF_BRANCH_LAST_UPDATE_TIME = "app.update.lastUpdateTime.";

const MAIN_TIMER_INTERVAL = 500;    // milliseconds
const CONSUMER_TIMER_INTERVAL = 1; // seconds

const TESTS = [ {
  desc            : "Test Timer Callback 1",
  timerID         : "test1-update-timer",
  defaultInterval : "bogus",
  prefInterval    : "test1.timer.interval",
  contractID      : "@mozilla.org/test1/timercallback;1",
  method          : "createInstance",
  classID         : Components.ID("9c7ce81f-98bb-4729-adb4-4d0deb0f59e5"),
  notified        : false
}, {
  desc            : "Test Timer Callback 2",
  timerID         : "test2-update-timer",
  defaultInterval : CONSUMER_TIMER_INTERVAL,
  prefInterval    : "test2.timer.interval",
  contractID      : "@mozilla.org/test2/timercallback;1",
  method          : "createInstance",
  classID         : Components.ID("512834f3-05bb-46be-84e0-81d881a140b7"),
  notified        : false
}, {
  desc            : "Test Timer Callback 3",
  timerID         : "test3-update-timer",
  defaultInterval : CONSUMER_TIMER_INTERVAL,
  prefInterval    : "test3.timer.interval",
  contractID      : "@mozilla.org/test3/timercallback;1",
  method          : "createInstance",
  classID         : Components.ID("c8ac5027-8d11-4471-9d7c-fd692501b437"),
  notified        : false
}, {
  desc            : "Test Timer Callback 4",
  timerID         : "test4-update-timer",
  defaultInterval : CONSUMER_TIMER_INTERVAL,
  prefInterval    : "test4.timer.interval",
  contractID      : "@mozilla.org/test4/timercallback;1",
  method          : "createInstance",
  classID         : Components.ID("6b0e79f3-4ab8-414c-8f14-dde10e185727"),
  notified        : false
}, {
  desc            : "Test Timer Callback 5",
  timerID         : "test5-update-timer",
  defaultInterval : CONSUMER_TIMER_INTERVAL,
  prefInterval    : "test5.timer.interval",
  contractID      : "@mozilla.org/test5/timercallback;1",
  method          : "createInstance",
  classID         : Components.ID("2f6b7b92-e40f-4874-bfbb-eeb2412c959d"),
  notified        : false
}, {
  desc            : "Test Timer Callback 6",
  timerID         : "test6-update-timer",
  defaultInterval : 86400,
  prefInterval    : "test6.timer.interval",
  contractID      : "@mozilla.org/test6/timercallback;1",
  method          : "createInstance",
  classID         : Components.ID("8a95f611-b2ac-4c7e-8b73-9748c4839731"),
  notified        : false
}, {
  desc            : "Test Timer Callback 7",
  timerID         : "test7-update-timer",
  defaultInterval : CONSUMER_TIMER_INTERVAL,
  contractID      : "@mozilla.org/test7/timercallback;1",
  classID         : Components.ID("af878d4b-1d12-41f6-9a90-4e687367ecc1"),
  notified        : false,
  lastUpdateTime  : 0
} ];

var gUTM;
var gNextFunc;

XPCOMUtils.defineLazyServiceGetter(this, "gPref",
                                   "@mozilla.org/preferences-service;1",
                                   "nsIPrefBranch2");

XPCOMUtils.defineLazyServiceGetter(this, "gCatMan",
                                   "@mozilla.org/categorymanager;1",
                                   "nsICategoryManager");

XPCOMUtils.defineLazyGetter(this, "gCompReg", function() {
  return Cm.QueryInterface(Ci.nsIComponentRegistrar);
});

function run_test() {
  do_test_pending();

  // Set the timer to fire every second
  gPref.setIntPref(PREF_APP_UPDATE_TIMER, MAIN_TIMER_INTERVAL);
  gPref.setBoolPref(PREF_APP_UPDATE_LOG_ALL, true);

  // Remove existing update timers to prevent them from being notified
  var entries = gCatMan.enumerateCategory(CATEGORY_UPDATE_TIMER);
  while (entries.hasMoreElements()) {
    let entry = entries.getNext().QueryInterface(Ci.nsISupportsCString).data;
    gCatMan.deleteCategoryEntry(CATEGORY_UPDATE_TIMER, entry, false);
  }

  gUTM = Cc["@mozilla.org/updates/timer-manager;1"].
         getService(Ci.nsIUpdateTimerManager).
         QueryInterface(Ci.nsIObserver);
  gUTM.observe(null, "utm-test-init", "");

  do_execute_soon(run_test1thru6);
}

function end_test() {
  gUTM.observe(null, "xpcom-shutdown", "");
  do_test_finished();
}

function run_test1thru6() {
  gNextFunc = check_test1thru6;
  // bogus default interval
  gCompReg.registerFactory(TESTS[0].classID, TESTS[0].desc,
                           TESTS[0].contractID, gTest1Factory);
  gCatMan.addCategoryEntry(CATEGORY_UPDATE_TIMER, TESTS[0].desc,
                           [TESTS[0].contractID, TESTS[0].method,
                            TESTS[0].timerID, TESTS[0].prefInterval,
                            TESTS[0].defaultInterval].join(","), false, true);

  // doesn't implement nsITimerCallback
  gCompReg.registerFactory(TESTS[1].classID, TESTS[1].desc,
                           TESTS[1].contractID, gTest2Factory);
  gCatMan.addCategoryEntry(CATEGORY_UPDATE_TIMER, TESTS[1].desc,
                           [TESTS[1].contractID, TESTS[1].method,
                            TESTS[1].timerID, TESTS[1].prefInterval,
                            TESTS[1].defaultInterval].join(","), false, true);

  // has a next update time 24 hours from now
  var nextUpdateTime = Math.round(Date.now() / 1000) + 86400;
  gPref.setIntPref(PREF_BRANCH_LAST_UPDATE_TIME + TESTS[2].timerID, nextUpdateTime);
  gCompReg.registerFactory(TESTS[2].classID, TESTS[2].desc,
                           TESTS[2].contractID, gTest3Factory);
  gCatMan.addCategoryEntry(CATEGORY_UPDATE_TIMER, TESTS[2].desc,
                           [TESTS[2].contractID, TESTS[2].method,
                            TESTS[2].timerID, TESTS[2].prefInterval,
                            TESTS[2].defaultInterval].join(","), false, true);

  // doesn't have a notify method
  gCompReg.registerFactory(TESTS[3].classID, TESTS[3].desc,
                           TESTS[3].contractID, gTest4Factory);
  gCatMan.addCategoryEntry(CATEGORY_UPDATE_TIMER, TESTS[3].desc,
                           [TESTS[3].contractID, TESTS[3].method,
                            TESTS[3].timerID, TESTS[3].prefInterval,
                            TESTS[3].defaultInterval].join(","), false, true);

  // already has a last update time
  gPref.setIntPref(PREF_BRANCH_LAST_UPDATE_TIME + TESTS[4].timerID, 1);
  gCompReg.registerFactory(TESTS[4].classID, TESTS[4].desc,
                           TESTS[4].contractID, gTest5Factory);
  gCatMan.addCategoryEntry(CATEGORY_UPDATE_TIMER, TESTS[4].desc,
                           [TESTS[4].contractID, TESTS[4].method,
                            TESTS[4].timerID, TESTS[4].prefInterval,
                            TESTS[4].defaultInterval].join(","), false, true);

  // has an interval preference that overrides the default
  gPref.setIntPref(TESTS[5].prefInterval, CONSUMER_TIMER_INTERVAL);
  gCompReg.registerFactory(TESTS[5].classID, TESTS[5].desc,
                           TESTS[5].contractID, gTest6Factory);
  gCatMan.addCategoryEntry(CATEGORY_UPDATE_TIMER, TESTS[5].desc,
                           [TESTS[5].contractID, TESTS[5].method,
                            TESTS[5].timerID, TESTS[5].prefInterval,
                            TESTS[5].defaultInterval].join(","), false, true);
}

function finished_test1thru6() {
  if (TESTS[4].notified && TESTS[5].notified)
    do_timeout(0, gNextFunc);
}

function check_test1thru6() {
  dump("Testing: a category registered timer didn't fire due to an invalid " +
       "default interval\n");
  do_check_false(TESTS[0].notified);

  dump("Testing: a category registered timer didn't fire due to not " +
       "implementing nsITimerCallback\n");
  do_check_false(TESTS[1].notified);

  dump("Testing: a category registered timer didn't fire due to the next " +
       "update time being 24 hours from now\n");
  do_check_false(TESTS[2].notified);

  dump("Testing: a category registered timer didn't fire due to not " +
       "having a notify method\n");
  do_check_false(TESTS[3].notified);

  dump("Testing: a category registered timer has fired\n");
  do_check_true(TESTS[4].notified);

  dump("Testing: a category registered timer fired that has an interval " +
       "preference that overrides a default that wouldn't have fired yet\n");
  do_check_true(TESTS[5].notified);

  dump("Testing: two category registered timers last update time has " +
       "user values\n");
  do_check_true(gPref.prefHasUserValue(PREF_BRANCH_LAST_UPDATE_TIME +
                TESTS[4].timerID));
  do_check_true(gPref.prefHasUserValue(PREF_BRANCH_LAST_UPDATE_TIME +
                TESTS[5].timerID));

  dump("Testing: one registerTimer registered timer last update time has " +
       "been updated\n");
  do_check_neq(gPref.getIntPref(PREF_BRANCH_LAST_UPDATE_TIME + TESTS[4].timerID), 1);

  // Remove the category timers that should have failed
  gCatMan.deleteCategoryEntry(CATEGORY_UPDATE_TIMER, TESTS[0].desc, true);
  gCatMan.deleteCategoryEntry(CATEGORY_UPDATE_TIMER, TESTS[1].desc, true);
  gCatMan.deleteCategoryEntry(CATEGORY_UPDATE_TIMER, TESTS[2].desc, true);
  gCatMan.deleteCategoryEntry(CATEGORY_UPDATE_TIMER, TESTS[3].desc, true);
  var count = 0;
  var entries = gCatMan.enumerateCategory(CATEGORY_UPDATE_TIMER);
  while (entries.hasMoreElements()) {
    let entry = entries.getNext().QueryInterface(Ci.nsISupportsCString).data;
    gCatMan.deleteCategoryEntry(CATEGORY_UPDATE_TIMER, entry, false);
    count++;
  }
  dump("Testing: no " + CATEGORY_UPDATE_TIMER + " categories are still " +
       "registered\n");
  do_check_eq(count, 0);

  do_timeout(0, run_test7());
}

function run_test7() {
  gNextFunc = check_test7;
  gPref.setIntPref(PREF_BRANCH_LAST_UPDATE_TIME + TESTS[6].timerID, 1);
  gCompReg.registerFactory(TESTS[6].classID, TESTS[6].desc,
                           TESTS[6].contractID, gTest7Factory);
  gUTM.registerTimer(TESTS[6].timerID, gTest7TimerCallback,
                     TESTS[6].defaultInterval);
}

function check_test7() {
  dump("Testing: one registerTimer registered timer has fired\n");
  do_check_true(TESTS[6].notified);
  dump("Testing: one registerTimer registered timer last update time has " +
       "been updated\n");
  do_check_neq(gPref.getIntPref(PREF_BRANCH_LAST_UPDATE_TIME + TESTS[6].timerID), 1);
  end_test();
}

var gTest1TimerCallback = {
  notify: function T1CB_notify(aTimer) {
    do_throw("gTest1TimerCallback notify method should not have been called");
  },
  QueryInterface: XPCOMUtils.generateQI([Ci.nsITimerCallback])
};

var gTest1Factory = {
  createInstance: function (outer, iid) {
    if (outer == null)
      return gTest1TimerCallback.QueryInterface(iid);
    throw Cr.NS_ERROR_NO_AGGREGATION;
  }
};

var gTest2TimerCallback = {
  notify: function T2CB_notify(aTimer) {
    do_throw("gTest2TimerCallback notify method should not have been called");
  },
  QueryInterface: XPCOMUtils.generateQI([Ci.nsITimer])
};

var gTest2Factory = {
  createInstance: function (outer, iid) {
    if (outer == null)
      return gTest2TimerCallback.QueryInterface(iid);
    throw Cr.NS_ERROR_NO_AGGREGATION;
  }
};

var gTest3TimerCallback = {
  notify: function T3CB_notify(aTimer) {
    do_throw("gTest3TimerCallback notify method should not have been called");
  },
  QueryInterface: XPCOMUtils.generateQI([Ci.nsITimerCallback])
};

var gTest3Factory = {
  createInstance: function (outer, iid) {
    if (outer == null)
      return gTest3TimerCallback.QueryInterface(iid);
    throw Cr.NS_ERROR_NO_AGGREGATION;
  }
};

var gTest4TimerCallback = {
  QueryInterface: XPCOMUtils.generateQI([Ci.nsITimerCallback])
};

var gTest4Factory = {
  createInstance: function (outer, iid) {
    if (outer == null)
      return gTest4TimerCallback.QueryInterface(iid);
    throw Cr.NS_ERROR_NO_AGGREGATION;
  }
};

var gTest5TimerCallback = {
  notify: function T5CB_notify(aTimer) {
    gCatMan.deleteCategoryEntry(CATEGORY_UPDATE_TIMER, TESTS[4].desc, true);
    TESTS[4].notified = true;
    finished_test1thru6();
  },
  QueryInterface: XPCOMUtils.generateQI([Ci.nsITimerCallback])
};

var gTest5Factory = {
  createInstance: function (outer, iid) {
    if (outer == null)
      return gTest5TimerCallback.QueryInterface(iid);
    throw Cr.NS_ERROR_NO_AGGREGATION;
  }
};

var gTest6TimerCallback = {
  notify: function T6CB_notify(aTimer) {
    gCatMan.deleteCategoryEntry(CATEGORY_UPDATE_TIMER, TESTS[5].desc, true);
    TESTS[5].notified = true;
    finished_test1thru6();
  },
  QueryInterface: XPCOMUtils.generateQI([Ci.nsITimerCallback])
};

var gTest6Factory = {
  createInstance: function (outer, iid) {
    if (outer == null)
      return gTest6TimerCallback.QueryInterface(iid);
    throw Cr.NS_ERROR_NO_AGGREGATION;
  }
};

var gTest7TimerCallback = {
  notify: function T7CB_notify(aTimer) {
    TESTS[6].notified = true;
    do_timeout(0, check_test7);
  },
  QueryInterface: XPCOMUtils.generateQI([Ci.nsITimerCallback])
};

var gTest7Factory = {
  createInstance: function (outer, iid) {
    if (outer == null)
      return gTest7TimerCallback.QueryInterface(iid);
    throw Cr.NS_ERROR_NO_AGGREGATION;
  }
};
