/* ***** BEGIN LICENSE BLOCK *****
 *   Version: MPL 1.1/GPL 2.0/LGPL 2.1
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
 * The Initial Developer of the Original Code is
 * Ehsan Akhgari.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Ehsan Akhgari <ehsan.akhgari@gmail.com> (Original Author)
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

const Cc = Components.classes;
const Ci = Components.interfaces;

function test() {
    waitForExplicitFinish();

    let pwmgr = Cc["@mozilla.org/login-manager;1"].
                getService(Ci.nsILoginManager);
    pwmgr.removeAllLogins();

    // Add some initial logins
    let urls = [
        "http://example.com/",
        "http://example.org/",
        "http://mozilla.com/",
        "http://mozilla.org/",
        "http://spreadfirefox.com/",
        "http://planet.mozilla.org/",
        "https://developer.mozilla.org/",
        "http://hg.mozilla.org/",
        "http://mxr.mozilla.org/",
        "http://feeds.mozilla.org/",
    ];
    let nsLoginInfo = new Components.Constructor("@mozilla.org/login-manager/loginInfo;1",
                                                 Ci.nsILoginInfo, "init");
    let logins = [
        new nsLoginInfo(urls[0], urls[0], null, "user", "password", "u1", "p1"),
        new nsLoginInfo(urls[1], urls[1], null, "username", "password", "u2", "p2"),
        new nsLoginInfo(urls[2], urls[2], null, "ehsan", "mypass", "u3", "p3"),
        new nsLoginInfo(urls[3], urls[3], null, "ehsan", "mypass", "u4", "p4"),
        new nsLoginInfo(urls[4], urls[4], null, "john", "smith", "u5", "p5"),
        new nsLoginInfo(urls[5], urls[5], null, "what?", "very secret", "u6", "p6"),
        new nsLoginInfo(urls[6], urls[6], null, "really?", "super secret", "u7", "p7"),
        new nsLoginInfo(urls[7], urls[7], null, "you sure?", "absolutely", "u8", "p8"),
        new nsLoginInfo(urls[8], urls[8], null, "my user name", "mozilla", "u9", "p9"),
        new nsLoginInfo(urls[9], urls[9], null, "my username", "mozilla.com", "u10", "p10"),
    ];
    logins.forEach(function (login) pwmgr.addLogin(login));

    // Detect when the password manager window is opened
    let ww = Cc["@mozilla.org/embedcomp/window-watcher;1"].
             getService(Ci.nsIWindowWatcher);

    // Open the password manager dialog
    const PWMGR_DLG = "chrome://passwordmgr/content/passwordManager.xul";
    let pwmgrdlg = window.openDialog(PWMGR_DLG, "Toolkit:PasswordManager", "");
    SimpleTest.waitForFocus(doTest, pwmgrdlg);

    // the meat of the test
    function doTest() {
        let doc = pwmgrdlg.document;
        let win = doc.defaultView;
        let filter = doc.getElementById("filter");
        let tree = doc.getElementById("signonsTree");
        let view = tree.treeBoxObject.view;

        is(filter.value, "", "Filter box should initially be empty");
        is(view.rowCount, 10, "There should be 10 passwords initially");

        // Prepare a set of tests
        //   filter: the text entered in the filter search box
        //   count: the number of logins which should match the respective filter
        //   count2: the number of logins which should match the respective filter
        //           if the passwords are being shown as well
        //   Note: if a test doesn't have count2 set, count is used instead.
        let tests = [
            {filter: "pass", count: 0, count2: 4},
            {filter: "", count: 10}, // test clearing the filter
            {filter: "moz", count: 7},
            {filter: "mozi", count: 7},
            {filter: "mozil", count: 7},
            {filter: "mozill", count: 7},
            {filter: "mozilla", count: 7},
            {filter: "mozilla.com", count: 1, count2: 2},
            {filter: "user", count: 4},
            {filter: "user ", count: 1},
            {filter: " user", count: 2},
            {filter: "http", count: 10},
            {filter: "https", count: 1},
            {filter: "secret", count: 0, count2: 2},
            {filter: "secret!", count: 0},
        ];

        let toggleCalls = 0;
        function toggleShowPasswords(func) {
            let toggleButton = doc.getElementById("togglePasswords");
            let showMode = (toggleCalls++ % 2) == 0;

            // only watch for a confirmation dialog every other time being called
            if (showMode) {
                ww.registerNotification(function (aSubject, aTopic, aData) {
                    if (aTopic == "domwindowclosed")
                        ww.unregisterNotification(arguments.callee);
                    else if (aTopic == "domwindowopened") {
                        let win = aSubject.QueryInterface(Ci.nsIDOMEventTarget);
                        SimpleTest.waitForFocus(function() {
                            EventUtils.synthesizeKey("VK_RETURN", {}, win)
                        }, win);
                    }
                });
            }

            let obsSvc = Cc["@mozilla.org/observer-service;1"].
                         getService(Ci.nsIObserverService);
            obsSvc.addObserver(function (aSubject, aTopic, aData) {
                if (aTopic == "passwordmgr-password-toggle-complete") {
                    obsSvc.removeObserver(arguments.callee, aTopic, false);
                    func();
                }
            }, "passwordmgr-password-toggle-complete", false);

            EventUtils.synthesizeMouse(toggleButton, 1, 1, {}, win);
        }

        function runTests(mode, endFunction) {
            let testCounter = 0;

            function setFilter(string) {
                filter.value = string;
                filter.doCommand();
            }

            function runOneTest(test) {
                function tester() {
                    is(view.rowCount, expected, expected + " logins should match '" + test.filter + "'");
                }

                let expected;
                switch (mode) {
                case 1: // without showing passwords
                    expected = test.count;
                    break;
                case 2: // showing passwords
                    expected = ("count2" in test) ? test.count2 : test.count;
                    break;
                case 3: // toggle
                    expected = test.count;
                    tester();
                    toggleShowPasswords(function () {
                        expected = ("count2" in test) ? test.count2 : test.count;
                        tester();
                        toggleShowPasswords(proceed);
                    });
                    return;
                }
                tester();
                proceed();
            }

            function proceed() {
                // run the next test if necessary or proceed with the tests
                if (testCounter != tests.length)
                    runNextTest();
                else
                    endFunction();
            }

            function runNextTest() {
                let test = tests[testCounter++];
                setFilter(test.filter);
                setTimeout(runOneTest, 0, test);
            }

            runNextTest();
        }

        function step1() {
            runTests(1, step2);
        }

        function step2() {
            toggleShowPasswords(function() {
                runTests(2, step3);
            });
        }

        function step3() {
            toggleShowPasswords(function() {
                runTests(3, lastStep);
            });
        }

        function lastStep() {
            // cleanup
            ww.registerNotification(function (aSubject, aTopic, aData) {
                // unregister ourself
                ww.unregisterNotification(arguments.callee);

                pwmgr.removeAllLogins();
                finish();
            });
            pwmgrdlg.close();
        }

        step1();
    }
}
