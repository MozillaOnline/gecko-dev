/* -*- Mode: Java; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- /
/* vim: set shiftwidth=4 tabstop=8 autoindent cindent expandtab: */
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
 * The Original Code is Mozilla's layout acceptance tests.
 *
 * The Initial Developer of the Original Code is the Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2006
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   L. David Baron <dbaron@dbaron.org>, Mozilla Corporation (original author)
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

const CC = Components.classes;
const CI = Components.interfaces;
const CR = Components.results;

const XHTML_NS = "http://www.w3.org/1999/xhtml";

const NS_LOCAL_FILE_CONTRACTID = "@mozilla.org/file/local;1";
const IO_SERVICE_CONTRACTID = "@mozilla.org/network/io-service;1";
const DEBUG_CONTRACTID = "@mozilla.org/xpcom/debug;1";
const NS_LOCALFILEINPUTSTREAM_CONTRACTID =
          "@mozilla.org/network/file-input-stream;1";
const NS_SCRIPTSECURITYMANAGER_CONTRACTID =
          "@mozilla.org/scriptsecuritymanager;1";
const NS_REFTESTHELPER_CONTRACTID =
          "@mozilla.org/reftest-helper;1";
const NS_NETWORK_PROTOCOL_CONTRACTID_PREFIX =
          "@mozilla.org/network/protocol;1?name=";
const NS_XREAPPINFO_CONTRACTID =
          "@mozilla.org/xre/app-info;1";


var gLoadTimeout = 0;

// "<!--CLEAR-->"
const BLANK_URL_FOR_CLEARING = "data:text/html,%3C%21%2D%2DCLEAR%2D%2D%3E";

var gBrowser;
var gCanvas1, gCanvas2;
// gCurrentCanvas is non-null between InitCurrentCanvasWithSnapshot and the next
// DocumentLoaded.
var gCurrentCanvas = null;
var gURLs;
// Map from URI spec to the number of times it remains to be used
var gURIUseCounts;
// Map from URI spec to the canvas rendered for that URI
var gURICanvases;
var gTestResults = {
  // Successful...
  Pass: 0,
  LoadOnly: 0,
  // Unexpected...
  Exception: 0,
  FailedLoad: 0,
  UnexpectedFail: 0,
  UnexpectedPass: 0,
  AssertionUnexpected: 0,
  AssertionUnexpectedFixed: 0,
  // Known problems...
  KnownFail : 0,
  AssertionKnown: 0,
  Random : 0,
  Skip: 0,
};
var gTotalTests = 0;
var gState;
var gCurrentURL;
var gFailureTimeout = null;
var gFailureReason;
var gServer;
var gCount = 0;
var gAssertionCount = 0;

var gIOService;
var gDebug;
var gWindowUtils;

var gCurrentTestStartTime;
var gSlowestTestTime = 0;
var gSlowestTestURL;
var gClearingForAssertionCheck = false;

const TYPE_REFTEST_EQUAL = '==';
const TYPE_REFTEST_NOTEQUAL = '!=';
const TYPE_LOAD = 'load';     // test without a reference (just test that it does
                              // not assert, crash, hang, or leak)
const TYPE_SCRIPT = 'script'; // test contains individual test results

const EXPECTED_PASS = 0;
const EXPECTED_FAIL = 1;
const EXPECTED_RANDOM = 2;
const EXPECTED_DEATH = 3;  // test must be skipped to avoid e.g. crash/hang

const gProtocolRE = /^\w+:/;

var HTTP_SERVER_PORT = 4444;
const HTTP_SERVER_PORTS_TO_TRY = 50;

// whether we should skip caching canvases
var gNoCanvasCache = false;

var gRecycledCanvases = new Array();

function AllocateCanvas()
{
    var windowElem = document.documentElement;

    if (gRecycledCanvases.length > 0)
        return gRecycledCanvases.shift();

    var canvas = document.createElementNS(XHTML_NS, "canvas");
    canvas.setAttribute("width", windowElem.getAttribute("width"));
    canvas.setAttribute("height", windowElem.getAttribute("height"));

    return canvas;
}

function ReleaseCanvas(canvas)
{
    // store a maximum of 2 canvases, if we're not caching
    if (!gNoCanvasCache || gRecycledCanvases.length < 2)
        gRecycledCanvases.push(canvas);
}

function OnRefTestLoad()
{
    gBrowser = document.getElementById("browser");

    /* set the gLoadTimeout */
    try {
      var prefs = Components.classes["@mozilla.org/preferences-service;1"].
                  getService(Components.interfaces.nsIPrefBranch2);
      gLoadTimeout = prefs.getIntPref("reftest.timeout");
    }
    catch(e) {
      gLoadTimeout = 5 * 60 * 1000; //5 minutes as per bug 479518
    }

    gBrowser.addEventListener("load", OnDocumentLoad, true);

    try {
        gWindowUtils = window.QueryInterface(CI.nsIInterfaceRequestor).getInterface(CI.nsIDOMWindowUtils);
        if (gWindowUtils && !gWindowUtils.compareCanvases)
            gWindowUtils = null;
    } catch (e) {
        gWindowUtils = null;
    }

    var windowElem = document.documentElement;

    gIOService = CC[IO_SERVICE_CONTRACTID].getService(CI.nsIIOService);
    gDebug = CC[DEBUG_CONTRACTID].getService(CI.nsIDebug2);
    gServer = CC["@mozilla.org/server/jshttp;1"].
                  createInstance(CI.nsIHttpServer);

    try {
        if (gServer)
            StartHTTPServer();
    } catch (ex) {
        //gBrowser.loadURI('data:text/plain,' + ex);
        ++gTestResults.Exception;
        dump("REFTEST TEST-UNEXPECTED-FAIL | | EXCEPTION: " + ex + "\n");
        DoneTests();
    }

    StartTests();
}

function StartHTTPServer()
{
    gServer.registerContentType("sjs", "sjs");
    // We want to try different ports in case the port we want
    // is being used.
    var tries = HTTP_SERVER_PORTS_TO_TRY;
    do {
        try {
            gServer.start(HTTP_SERVER_PORT);
            return;
        } catch (ex) {
            ++HTTP_SERVER_PORT;
            if (--tries == 0)
                throw ex;
        }
    } while (true);
}

function StartTests()
{
    try {
        // Need to read the manifest once we have the final HTTP_SERVER_PORT.
        var args = window.arguments[0].wrappedJSObject;

        if ("nocache" in args && args["nocache"])
            gNoCanvasCache = true;

        ReadTopManifest(args.uri);
        BuildUseCounts();
        gTotalTests = gURLs.length;

        if (!gTotalTests)
            throw "No tests to run";

        gURICanvases = {};
        StartCurrentTest();
    } catch (ex) {
        //gBrowser.loadURI('data:text/plain,' + ex);
        ++gTestResults.Exception;
        dump("REFTEST TEST-UNEXPECTED-FAIL | | EXCEPTION: " + ex + "\n");
        DoneTests();
    }
}

function OnRefTestUnload()
{
    /* Clear the sRGB forcing pref to leave the profile as we found it. */
    var prefs = Components.classes["@mozilla.org/preferences-service;1"].
                getService(Components.interfaces.nsIPrefBranch2);
    prefs.clearUserPref("gfx.color_management.force_srgb");

    gBrowser.removeEventListener("load", OnDocumentLoad, true);
}

function ReadTopManifest(aFileURL)
{
    gURLs = new Array();
    var url = gIOService.newURI(aFileURL, null, null);
    if (!url || !url.schemeIs("file"))
        throw "Expected a file URL for the manifest.";
    ReadManifest(url);
}

// Note: If you materially change the reftest manifest parsing,
// please keep the parser in print-manifest-dirs.py in sync.
function ReadManifest(aURL)
{
    var listURL = aURL.QueryInterface(CI.nsIFileURL);

    var secMan = CC[NS_SCRIPTSECURITYMANAGER_CONTRACTID]
                     .getService(CI.nsIScriptSecurityManager);

    var fis = CC[NS_LOCALFILEINPUTSTREAM_CONTRACTID].
                  createInstance(CI.nsIFileInputStream);
    fis.init(listURL.file, -1, -1, false);
    var lis = fis.QueryInterface(CI.nsILineInputStream);

    // Build the sandbox for fails-if(), etc., condition evaluation.
    var sandbox = new Components.utils.Sandbox(aURL.spec);
    var xr = CC[NS_XREAPPINFO_CONTRACTID].getService(CI.nsIXULRuntime);
    sandbox.MOZ_WIDGET_TOOLKIT = xr.widgetToolkit;
    sandbox.isDebugBuild = gDebug.isDebugBuild;
    sandbox.xulRuntime = {widgetToolkit: xr.widgetToolkit, OS: xr.OS};

    // xr.XPCOMABI throws exception for configurations without full ABI
    // support (mobile builds on ARM)
    try {
      sandbox.xulRuntime.XPCOMABI = xr.XPCOMABI;
    } catch(e) {
      sandbox.xulRuntime.XPCOMABI = "";
    }

    var hh = CC[NS_NETWORK_PROTOCOL_CONTRACTID_PREFIX + "http"].
                 getService(CI.nsIHttpProtocolHandler);
    sandbox.http = {};
    for each (var prop in [ "userAgent", "appName", "appVersion",
                            "vendor", "vendorSub", "vendorComment",
                            "product", "productSub", "productComment",
                            "platform", "oscpu", "language", "misc" ])
        sandbox.http[prop] = hh[prop];
    // see if we have the test plugin available,
    // and set a sandox prop accordingly
    sandbox.haveTestPlugin = false;
    for (var i = 0; i < navigator.mimeTypes.length; i++) {
        if (navigator.mimeTypes[i].type == "application/x-test" &&
            navigator.mimeTypes[i].enabledPlugin != null &&
            navigator.mimeTypes[i].enabledPlugin.name == "Test Plug-in") {
            sandbox.haveTestPlugin = true;
            break;
        }
    }

    // Set a flag on sandbox if the windows default theme is active
    var box = document.createElement("box");
    box.setAttribute("id", "_box_windowsDefaultTheme");
    document.documentElement.appendChild(box);
    sandbox.windowsDefaultTheme = (getComputedStyle(box, null).display == "none");
    document.documentElement.removeChild(box);

    var line = {value:null};
    var lineNo = 0;
    var urlprefix = "";
    do {
        var more = lis.readLine(line);
        ++lineNo;
        var str = line.value;
        if (str.charAt(0) == "#")
            continue; // entire line was a comment
        var i = str.search(/\s+#/);
        if (i >= 0)
            str = str.substring(0, i);
        // strip leading and trailing whitespace
        str = str.replace(/^\s*/, '').replace(/\s*$/, '');
        if (!str || str == "")
            continue;
        var items = str.split(/\s+/); // split on whitespace

        if (items[0] == "url-prefix") {
            if (items.length != 2)
                throw "url-prefix requires one url in manifest file " + aURL.spec + " line " + lineNo;
            urlprefix = items[1];
            continue;
        }

        var expected_status = EXPECTED_PASS;
        var minAsserts = 0;
        var maxAsserts = 0;
        while (items[0].match(/^(fails|random|skip|asserts)/)) {
            var item = items.shift();
            var stat;
            var cond;
            var m = item.match(/^(fails|random|skip)-if(\(.*\))$/);
            if (m) {
                stat = m[1];
                // Note: m[2] contains the parentheses, and we want them.
                cond = Components.utils.evalInSandbox(m[2], sandbox);
            } else if (item.match(/^(fails|random|skip)$/)) {
                stat = item;
                cond = true;
            } else if ((m = item.match(/^asserts\((\d+)(-\d+)?\)$/))) {
                cond = false;
                minAsserts = Number(m[1]);
                maxAsserts = (m[2] == undefined) ? minAsserts
                                                 : Number(m[2].substring(1));
            } else if ((m = item.match(/^asserts-if\((.*?),(\d+)(-\d+)?\)$/))) {
                cond = false;
                if (Components.utils.evalInSandbox("(" + m[1] + ")", sandbox)) {
                    minAsserts = Number(m[2]);
                    maxAsserts =
                      (m[3] == undefined) ? minAsserts
                                          : Number(m[3].substring(1));
                }
            } else {
                throw "Error 1 in manifest file " + aURL.spec + " line " + lineNo;
            }

            if (cond) {
                if (stat == "fails") {
                    expected_status = EXPECTED_FAIL;
                } else if (stat == "random") {
                    expected_status = EXPECTED_RANDOM;
                } else if (stat == "skip") {
                    expected_status = EXPECTED_DEATH;
                }
            }
        }

        if (minAsserts > maxAsserts) {
            throw "Bad range in manifest file " + aURL.spec + " line " + lineNo;
        }

        var runHttp = false;
        var httpDepth;
        if (items[0] == "HTTP") {
            runHttp = true;
            httpDepth = 0;
            items.shift();
        } else if (items[0].match(/HTTP\(\.\.(\/\.\.)*\)/)) {
            // Accept HTTP(..), HTTP(../..), HTTP(../../..), etc.
            runHttp = true;
            httpDepth = (items[0].length - 5) / 3;
            items.shift();
        }

        // do not prefix the url for include commands or urls specifying
        // a protocol
        if (urlprefix && items[0] != "include") {
            if (items.length > 1 && !items[1].match(gProtocolRE)) {
                items[1] = urlprefix + items[1];
            }
            if (items.length > 2 && !items[2].match(gProtocolRE)) {
                items[2] = urlprefix + items[2];
            }
        }

        if (items[0] == "include") {
            if (items.length != 2 || runHttp)
                throw "Error 2 in manifest file " + aURL.spec + " line " + lineNo;
            var incURI = gIOService.newURI(items[1], null, listURL);
            secMan.checkLoadURI(aURL, incURI,
                                CI.nsIScriptSecurityManager.DISALLOW_SCRIPT);
            ReadManifest(incURI);
        } else if (items[0] == TYPE_LOAD) {
            if (items.length != 2 ||
                (expected_status != EXPECTED_PASS &&
                 expected_status != EXPECTED_DEATH))
                throw "Error 3 in manifest file " + aURL.spec + " line " + lineNo;
            var [testURI] = runHttp
                            ? ServeFiles(aURL, httpDepth,
                                         listURL.file.parent, [items[1]])
                            : [gIOService.newURI(items[1], null, listURL)];
            var prettyPath = runHttp
                           ? gIOService.newURI(items[1], null, listURL).spec
                           : testURI.spec;
            secMan.checkLoadURI(aURL, testURI,
                                CI.nsIScriptSecurityManager.DISALLOW_SCRIPT);
            gURLs.push( { type: TYPE_LOAD,
                          expected: expected_status,
                          prettyPath: prettyPath,
                          minAsserts: minAsserts,
                          maxAsserts: maxAsserts,
                          url1: testURI,
                          url2: null } );
        } else if (items[0] == TYPE_SCRIPT) {
            if (items.length != 2)
                throw "Error 4 in manifest file " + aURL.spec + " line " + lineNo;
            var [testURI] = runHttp
                            ? ServeFiles(aURL, httpDepth,
                                         listURL.file.parent, [items[1]])
                            : [gIOService.newURI(items[1], null, listURL)];
            var prettyPath = runHttp
                           ? gIOService.newURI(items[1], null, listURL).spec
                           : testURI.spec;
            secMan.checkLoadURI(aURL, testURI,
                                CI.nsIScriptSecurityManager.DISALLOW_SCRIPT);
            gURLs.push( { type: TYPE_SCRIPT,
                          expected: expected_status,
                          prettyPath: prettyPath,
                          minAsserts: minAsserts,
                          maxAsserts: maxAsserts,
                          url1: testURI,
                          url2: null } );
        } else if (items[0] == TYPE_REFTEST_EQUAL || items[0] == TYPE_REFTEST_NOTEQUAL) {
            if (items.length != 3)
                throw "Error 5 in manifest file " + aURL.spec + " line " + lineNo;
            var [testURI, refURI] = runHttp
                                  ? ServeFiles(aURL, httpDepth,
                                               listURL.file.parent, [items[1], items[2]])
                                  : [gIOService.newURI(items[1], null, listURL),
                                     gIOService.newURI(items[2], null, listURL)];
            var prettyPath = runHttp
                           ? gIOService.newURI(items[1], null, listURL).spec
                           : testURI.spec;
            secMan.checkLoadURI(aURL, testURI,
                                CI.nsIScriptSecurityManager.DISALLOW_SCRIPT);
            secMan.checkLoadURI(aURL, refURI,
                                CI.nsIScriptSecurityManager.DISALLOW_SCRIPT);
            gURLs.push( { type: items[0],
                          expected: expected_status,
                          prettyPath: prettyPath,
                          minAsserts: minAsserts,
                          maxAsserts: maxAsserts,
                          url1: testURI,
                          url2: refURI } );
        } else {
            throw "Error 6 in manifest file " + aURL.spec + " line " + lineNo;
        }
    } while (more);
}

function AddURIUseCount(uri)
{
    if (uri == null)
        return;

    var spec = uri.spec;
    if (spec in gURIUseCounts) {
        gURIUseCounts[spec]++;
    } else {
        gURIUseCounts[spec] = 1;
    }
}

function BuildUseCounts()
{
    gURIUseCounts = {};
    for (var i = 0; i < gURLs.length; ++i) {
        var url = gURLs[i];
        if (url.expected != EXPECTED_DEATH &&
            (url.type == TYPE_REFTEST_EQUAL ||
             url.type == TYPE_REFTEST_NOTEQUAL)) {
            AddURIUseCount(gURLs[i].url1);
            AddURIUseCount(gURLs[i].url2);
        }
    }
}

function ServeFiles(manifestURL, depth, directory, files)
{
    // Allow serving a tree that's an ancestor of the directory containing
    // the files so that they can use resources in ../ (etc.).
    var dirPath = "/";
    while (depth > 0) {
        dirPath = "/" + directory.leafName + dirPath;
        directory = directory.parent;
        --depth;
    }

    gCount++;
    var path = "/" + Date.now() + "/" + gCount;
    gServer.registerDirectory(path + "/", directory);

    var secMan = CC[NS_SCRIPTSECURITYMANAGER_CONTRACTID]
                     .getService(CI.nsIScriptSecurityManager);

    var testbase = gIOService.newURI("http://localhost:" + HTTP_SERVER_PORT +
                                         path + dirPath,
                                     null, null);

    function FileToURI(file)
    {
        // Only serve relative URIs via the HTTP server, not absolute
        // ones like about:blank.
        var testURI = gIOService.newURI(file, null, testbase);

        // XXX necessary?  manifestURL guaranteed to be file, others always HTTP
        secMan.checkLoadURI(manifestURL, testURI,
                            CI.nsIScriptSecurityManager.DISALLOW_SCRIPT);

        return testURI;
    }

    return files.map(FileToURI);
}

function StartCurrentTest()
{
    // make sure we don't run tests that are expected to kill the browser
    while (gURLs.length > 0 && gURLs[0].expected == EXPECTED_DEATH) {
        ++gTestResults.Skip;
        dump("REFTEST TEST-KNOWN-FAIL | " + gURLs[0].url1.spec + " | (SKIP)\n");
        gURLs.shift();
    }

    if (gURLs.length == 0) {
        DoneTests();
    }
    else {
        var currentTest = gTotalTests - gURLs.length;
        document.title = "reftest: " + currentTest + " / " + gTotalTests +
            " (" + Math.floor(100 * (currentTest / gTotalTests)) + "%)";
        StartCurrentURI(1);
    }
}

function StartCurrentURI(aState)
{
    gCurrentTestStartTime = Date.now();
    if (gFailureTimeout != null) {
        dump("REFTEST TEST-UNEXPECTED-FAIL | " +
             "| program error managing timeouts\n");
        ++gTestResults.Exception;
    }
    gFailureTimeout = setTimeout(LoadFailed, gLoadTimeout);
    gFailureReason = "timed out waiting for onload to fire";

    gState = aState;
    gCurrentURL = gURLs[0]["url" + aState].spec;

    if (gURICanvases[gCurrentURL] &&
        (gURLs[0].type == TYPE_REFTEST_EQUAL ||
         gURLs[0].type == TYPE_REFTEST_NOTEQUAL) &&
        gURLs[0].maxAsserts == 0) {
        // Pretend the document loaded --- DocumentLoaded will notice
        // there's already a canvas for this URL
        setTimeout(DocumentLoaded, 0);
    } else {
        dump("REFTEST INFO | Loading " + gCurrentURL + "\n");
        gBrowser.loadURI(gCurrentURL);
    }
}

function DoneTests()
{
    // TEMPORARILY DISABLE REPORTING OF ASSERTION FAILURES.
    gTestResults.AssertionUnexpected = 0;
    gTestResults.AssertionUnexpectedFixed = 0;

    dump("REFTEST FINISHED: Slowest test took " + gSlowestTestTime +
         "ms (" + gSlowestTestURL + ")\n");

    dump("REFTEST INFO | Result summary:\n");
    var count = gTestResults.Pass + gTestResults.LoadOnly;
    dump("REFTEST INFO | Successful: " + count + " (" +
         gTestResults.Pass + " pass, " +
         gTestResults.LoadOnly + " load only)\n");
    count = gTestResults.Exception + gTestResults.FailedLoad +
            gTestResults.UnexpectedFail + gTestResults.UnexpectedPass +
            gTestResults.AssertionUnexpected +
            gTestResults.AssertionUnexpectedFixed;
    dump("REFTEST INFO | Unexpected: " + count + " (" +
         gTestResults.UnexpectedFail + " unexpected fail, " +
         gTestResults.UnexpectedPass + " unexpected pass, " +
         gTestResults.AssertionUnexpected + " unexpected asserts, " +
         gTestResults.AssertionUnexpectedFixed + " unexpected fixed asserts, " +
         gTestResults.FailedLoad + " failed load, " +
         gTestResults.Exception + " exception)\n");
    count = gTestResults.KnownFail + gTestResults.AssertionKnown +
            gTestResults.Random + gTestResults.Skip;
    dump("REFTEST INFO | Known problems: " + count + " (" +
         gTestResults.KnownFail + " known fail, " +
         gTestResults.AssertionKnown + " known asserts, " +
         gTestResults.Random + " random, " +
         gTestResults.Skip + " skipped)\n");

    dump("REFTEST INFO | Total canvas count = " + gRecycledCanvases.length + "\n");

    function onStopped() {
        dump("REFTEST INFO | Quitting...\n");
        goQuitApplication();
    }
    if (gServer)
        gServer.stop(onStopped);
    else
        onStopped();
}

function setupZoom(contentRootElement) {
    if (!contentRootElement || !contentRootElement.hasAttribute('reftest-zoom'))
        return;
    gBrowser.markupDocumentViewer.fullZoom =
        contentRootElement.getAttribute('reftest-zoom');
}

function resetZoom() {
    gBrowser.markupDocumentViewer.fullZoom = 1.0;
}

function OnDocumentLoad(event)
{
    if (event.target != gBrowser.contentDocument)
        // Ignore load events for subframes.
        return;

    if (gClearingForAssertionCheck &&
        gBrowser.contentDocument.location.href == BLANK_URL_FOR_CLEARING) {
        DoAssertionCheck();
        return;
    }

    if (gBrowser.contentDocument.location.href != gCurrentURL)
        // Ignore load events for previous documents.
        return;

    var contentRootElement = gBrowser.contentDocument.documentElement;

    function shouldWait() {
        // use getAttribute because className works differently in HTML and SVG
        return contentRootElement &&
               contentRootElement.hasAttribute('class') &&
               contentRootElement.getAttribute('class').split(/\s+/)
                                 .indexOf("reftest-wait") != -1;
    }

    function doPrintMode() {
        // use getAttribute because className works differently in HTML and SVG
        return contentRootElement &&
               contentRootElement.hasAttribute('class') &&
               contentRootElement.getAttribute('class').split(/\s+/)
                                 .indexOf("reftest-print") != -1;
    }

    function setupPrintMode() {
       var PSSVC = Components.classes["@mozilla.org/gfx/printsettings-service;1"]
                  .getService(Components.interfaces.nsIPrintSettingsService);
       var ps = PSSVC.newPrintSettings;
       ps.paperWidth = 5;
       ps.paperHeight = 3;

       // Override any os-specific unwriteable margins
       ps.unwriteableMarginTop = 0;
       ps.unwriteableMarginLeft = 0;
       ps.unwriteableMarginBottom = 0;
       ps.unwriteableMarginRight = 0;

       ps.headerStrLeft = "";
       ps.headerStrCenter = "";
       ps.headerStrRight = "";
       ps.footerStrLeft = "";
       ps.footerStrCenter = "";
       ps.footerStrRight = "";
       gBrowser.docShell.contentViewer.setPageMode(true, ps);

       // WORKAROUND FOR ASSERTIONS IN BUG 534478:  Calling setPageMode
       // above causes 2 assertions.  So that we don't have to annotate
       // the manifests for every reftest-print reftest, bump the
       // assertion count by two right here.
       // And on Mac, it causes *three* assertions.
       var xr = CC[NS_XREAPPINFO_CONTRACTID].getService(CI.nsIXULRuntime);
       var count = (xr.widgetToolkit == "cocoa") ? 3 : 2;
       gURLs[0].minAsserts += count;
       gURLs[0].maxAsserts += count;
    }

    setupZoom(contentRootElement);

    if (shouldWait()) {
        // The testcase will let us know when the test snapshot should be made.
        // Register a mutation listener to know when the 'reftest-wait' class
        // gets removed.
        gFailureReason = "timed out waiting for reftest-wait to be removed (after onload fired)"

        var stopAfterPaintReceived = false;
        var currentDoc = gBrowser.contentDocument;
        var utils = gBrowser.contentWindow.QueryInterface(CI.nsIInterfaceRequestor)
            .getInterface(CI.nsIDOMWindowUtils);

        function FlushRendering() {
            // Flush pending restyles and reflows
            contentRootElement.getBoundingClientRect();
            // Flush out invalidation
            utils.processUpdates();
        }

        function WhenMozAfterPaintFlushed(continuation) {
            if (utils.isMozAfterPaintPending) {
                function handler() {
                    gBrowser.removeEventListener("MozAfterPaint", handler, false);
                    continuation();
                }
                gBrowser.addEventListener("MozAfterPaint", handler, false);
            } else {
                continuation();
            }
        }

        function AfterPaintListener(event) {
            if (event.target.document != currentDoc) {
                // ignore paint events for subframes or old documents in the window.
                // Invalidation in subframes will cause invalidation in the main document anyway.
                return;
            }

            FlushRendering();
            UpdateCurrentCanvasForEvent(event);
            // When stopAfteraintReceived is set, we can stop --- but we should keep going as long
            // as there are paint events coming (there probably shouldn't be any, but it doesn't
            // hurt to process them)
            if (stopAfterPaintReceived && !utils.isMozAfterPaintPending) {
                FinishWaitingForTestEnd();
            }
        }

        function FinishWaitingForTestEnd() {
            gBrowser.removeEventListener("MozAfterPaint", AfterPaintListener, false);
            setTimeout(DocumentLoaded, 0);
        }

        function AttrModifiedListener() {
            if (shouldWait())
                return;

            // We don't want to be notified again
            contentRootElement.removeEventListener("DOMAttrModified", AttrModifiedListener, false);
            // Wait for the next return-to-event-loop before continuing to flush rendering and
            // check isMozAfterPaintPending --- for example, the attribute may have been modified
            // in an subdocument's load event handler, in which case we need load event processing
            // to complete and unsuppress painting before we check isMozAfterPaintPending.
            setTimeout(AttrModifiedListenerContinuation, 0);
        }

        function AttrModifiedListenerContinuation() {
            if (doPrintMode())
                setupPrintMode();
            FlushRendering();

            if (utils.isMozAfterPaintPending) {
                // Wait for the last invalidation to have happened and been snapshotted before
                // we stop the test
                stopAfterPaintReceived = true;
            } else {
                // Nothing to wait for, so stop now
                FinishWaitingForTestEnd();
            }
        }

        function StartWaitingForTestEnd() {
            FlushRendering();

            function continuation() {
                gBrowser.addEventListener("MozAfterPaint", AfterPaintListener, false);
                contentRootElement.addEventListener("DOMAttrModified", AttrModifiedListener, false);

                // Take a snapshot of the window in its current state
                InitCurrentCanvasWithSnapshot();

                if (!shouldWait()) {
                    // reftest-wait was already removed (during the interval between OnDocumentLoaded
                    // calling setTimeout(StartWaitingForTestEnd,0) below, and this function
                    // actually running), so let's fake a direct notification of the attribute
                    // change.
                    AttrModifiedListener();
                    return;
                }

                // Notify the test document that now is a good time to test some invalidation
                var notification = document.createEvent("Events");
                notification.initEvent("MozReftestInvalidate", true, false);
                contentRootElement.dispatchEvent(notification);
            }
            WhenMozAfterPaintFlushed(continuation);
        }

        // After this load event has finished being dispatched, painting is normally
        // unsuppressed, which invalidates the entire window. So ensure
        // StartWaitingForTestEnd runs after that invalidation has been requested.
        setTimeout(StartWaitingForTestEnd, 0);
    } else {
        if (doPrintMode())
            setupPrintMode();

        // Since we can't use a bubbling-phase load listener from chrome,
        // this is a capturing phase listener.  So do setTimeout twice, the
        // first to get us after the onload has fired in the content, and
        // the second to get us after any setTimeout(foo, 0) in the content.
        setTimeout(setTimeout, 0, DocumentLoaded, 0);
    }
}

function UpdateCanvasCache(url, canvas)
{
    var spec = url.spec;

    --gURIUseCounts[spec];

    if (gNoCanvasCache || gURIUseCounts[spec] == 0) {
        ReleaseCanvas(canvas);
        delete gURICanvases[spec];
    } else if (gURIUseCounts[spec] > 0) {
        gURICanvases[spec] = canvas;
    } else {
        throw "Use counts were computed incorrectly";
    }
}

function InitCurrentCanvasWithSnapshot()
{
    gCurrentCanvas = AllocateCanvas();

    /* XXX This needs to be rgb(255,255,255) because otherwise we get
     * black bars at the bottom of every test that are different size
     * for the first test and the rest (scrollbar-related??) */
    var win = gBrowser.contentWindow;
    var ctx = gCurrentCanvas.getContext("2d");
    var scale = gBrowser.markupDocumentViewer.fullZoom;
    ctx.save();
    // drawWindow always draws one canvas pixel for each CSS pixel in the source
    // window, so scale the drawing to show the zoom (making each canvas pixel be one
    // device pixel instead)
    ctx.scale(scale, scale);
    ctx.drawWindow(win, win.scrollX, win.scrollY,
                   Math.ceil(gCurrentCanvas.width / scale),
                   Math.ceil(gCurrentCanvas.height / scale),
                   "rgb(255,255,255)",
                   ctx.DRAWWINDOW_DRAW_CARET);
    ctx.restore();
}

function roundTo(x, fraction)
{
    return Math.round(x/fraction)*fraction;
}

function UpdateCurrentCanvasForEvent(event)
{
    var win = gBrowser.contentWindow;
    var ctx = gCurrentCanvas.getContext("2d");
    var scale = gBrowser.markupDocumentViewer.fullZoom;

    var rectList = event.clientRects;
    for (var i = 0; i < rectList.length; ++i) {
        var r = rectList[i];
        // Set left/top/right/bottom to "device pixel" boundaries
        var left = Math.floor(roundTo(r.left*scale, 0.001))/scale;
        var top = Math.floor(roundTo(r.top*scale, 0.001))/scale;
        var right = Math.ceil(roundTo(r.right*scale, 0.001))/scale;
        var bottom = Math.ceil(roundTo(r.bottom*scale, 0.001))/scale;

        ctx.save();
        ctx.scale(scale, scale);
        ctx.translate(left, top);
        ctx.drawWindow(win, left + win.scrollX, top + win.scrollY,
                       right - left, bottom - top,
                       "rgb(255,255,255)",
                       ctx.DRAWWINDOW_DRAW_CARET);
        ctx.restore();
    }
}

function DocumentLoaded()
{
    // Keep track of which test was slowest, and how long it took.
    var currentTestRunTime = Date.now() - gCurrentTestStartTime;
    if (currentTestRunTime > gSlowestTestTime) {
        gSlowestTestTime = currentTestRunTime;
        gSlowestTestURL  = gCurrentURL;
    }

    clearTimeout(gFailureTimeout);
    gFailureReason = null;
    gFailureTimeout = null;

    // Not 'const ...' because of 'EXPECTED_*' value dependency.
    var outputs = {};
    const randomMsg = "(EXPECTED RANDOM)";
    outputs[EXPECTED_PASS] = {
        true:  {s: "TEST-PASS"                  , n: "Pass"},
        false: {s: "TEST-UNEXPECTED-FAIL"       , n: "UnexpectedFail"}
    };
    outputs[EXPECTED_FAIL] = {
        true:  {s: "TEST-UNEXPECTED-PASS"       , n: "UnexpectedPass"},
        false: {s: "TEST-KNOWN-FAIL"            , n: "KnownFail"}
    };
    outputs[EXPECTED_RANDOM] = {
        true:  {s: "TEST-PASS" + randomMsg      , n: "Random"},
        false: {s: "TEST-KNOWN-FAIL" + randomMsg, n: "Random"}
    };
    var output;

    if (gURLs[0].type == TYPE_LOAD) {
        ++gTestResults.LoadOnly;
        dump("REFTEST TEST-PASS | " + gURLs[0].prettyPath + " | (LOAD ONLY)\n");
        FinishTestItem();
        return;
    }
    if (gURLs[0].type == TYPE_SCRIPT) {
        var missing_msg = false;
        var testwindow = gBrowser.contentWindow;
        expected = gURLs[0].expected;

        if (testwindow.wrappedJSObject)
            testwindow = testwindow.wrappedJSObject;

        var testcases;

        if (!testwindow.getTestCases || typeof testwindow.getTestCases != "function") {
            // Force an unexpected failure to alert the test author to fix the test.
            expected = EXPECTED_PASS;
            missing_msg = "test must provide a function getTestCases(). (SCRIPT)\n";
        }
        else if (!(testcases = testwindow.getTestCases())) {
            // Force an unexpected failure to alert the test author to fix the test.
            expected = EXPECTED_PASS;
            missing_msg = "test's getTestCases() must return an Array-like Object. (SCRIPT)\n";
        }
        else if (testcases.length == 0) {
            // This failure may be due to a JavaScript Engine bug causing
            // early termination of the test.
            missing_msg = "No test results reported. (SCRIPT)\n";
        }

        if (missing_msg) {
            output = outputs[expected][false];
            ++gTestResults[output.n];
            var result = "REFTEST " + output.s + " | " +
                gURLs[0].prettyPath + " | " + // the URL being tested
                missing_msg;

            dump(result);
            FinishTestItem();
            return;
        }

        var results = testcases.map(function(test) {
                return { passed: test.testPassed(), description: test.testDescription()};
            });
        var anyFailed = results.some(function(result) { return !result.passed; });
        var outputPair;
        if (anyFailed && expected == EXPECTED_FAIL) {
            // If we're marked as expected to fail, and some (but not all) tests
            // passed, treat those tests as though they were marked random
            // (since we can't tell whether they were really intended to be
            // marked failing or not).
            outputPair = { true: outputs[EXPECTED_RANDOM][true],
                           false: outputs[expected][false] };
        } else {
            outputPair = outputs[expected];
        }
        var index = 0;
        results.forEach(function(result) {
                var output = outputPair[result.passed];

                ++gTestResults[output.n];
                result = "REFTEST " + output.s + " | " +
                    gURLs[0].prettyPath + " | " + // the URL being tested
                    result.description + " item " + (++index) + "\n";
                dump(result);
            });

        FinishTestItem();
        return;
    }

    if (gURICanvases[gCurrentURL]) {
        gCurrentCanvas = gURICanvases[gCurrentURL];
    } else if (gCurrentCanvas == null) {
        InitCurrentCanvasWithSnapshot();
    }
    if (gState == 1) {
        gCanvas1 = gCurrentCanvas;
    } else {
        gCanvas2 = gCurrentCanvas;
    }
    gCurrentCanvas = null;

    resetZoom();

    switch (gState) {
        case 1:
            // First document has been loaded.
            // Proceed to load the second document.

            StartCurrentURI(2);
            break;
        case 2:
            // Both documents have been loaded. Compare the renderings and see
            // if the comparison result matches the expected result specified
            // in the manifest.

            // number of different pixels
            var differences;
            // whether the two renderings match:
            var equal;

            if (gWindowUtils) {
                differences = gWindowUtils.compareCanvases(gCanvas1, gCanvas2, {});
                equal = (differences == 0);
            } else {
                differences = -1;
                var k1 = gCanvas1.toDataURL();
                var k2 = gCanvas2.toDataURL();
                equal = (k1 == k2);
            }

            // whether the comparison result matches what is in the manifest
            var test_passed = (equal == (gURLs[0].type == TYPE_REFTEST_EQUAL));
            // what is expected on this platform (PASS, FAIL, or RANDOM)
            var expected = gURLs[0].expected;
            output = outputs[expected][test_passed];

            ++gTestResults[output.n];

            var result = "REFTEST " + output.s + " | " +
                         gURLs[0].prettyPath + " | "; // the URL being tested
            if (gURLs[0].type == TYPE_REFTEST_NOTEQUAL) {
                result += "(!=) ";
            }
            dump(result + "\n");

            if (!test_passed && expected == EXPECTED_PASS ||
                test_passed && expected == EXPECTED_FAIL) {
                if (!equal) {
                    dump("REFTEST   IMAGE 1 (TEST): " + gCanvas1.toDataURL() + "\n");
                    dump("REFTEST   IMAGE 2 (REFERENCE): " + gCanvas2.toDataURL() + "\n");
                    dump("REFTEST number of differing pixels: " + differences + "\n");
                } else {
                    dump("REFTEST   IMAGE: " + gCanvas1.toDataURL() + "\n");
                }
            }

            UpdateCanvasCache(gURLs[0].url1, gCanvas1);
            UpdateCanvasCache(gURLs[0].url2, gCanvas2);

            FinishTestItem();
            break;
        default:
            throw "Unexpected state.";
    }
}

function LoadFailed()
{
    gFailureTimeout = null;
    ++gTestResults.FailedLoad;
    dump("REFTEST TEST-UNEXPECTED-FAIL | " +
         gURLs[0]["url" + gState].spec + " | " + gFailureReason + "\n");
    FinishTestItem();
}

function FinishTestItem()
{
    // Replace document with BLANK_URL_FOR_CLEARING in case there are
    // assertions when unloading.
    dump("REFTEST INFO | Loading a blank page\n");
    gClearingForAssertionCheck = true;
    gBrowser.loadURI(BLANK_URL_FOR_CLEARING);
}

function DoAssertionCheck()
{
    gClearingForAssertionCheck = false;

    if (gDebug.isDebugBuild) {
        var newAssertionCount = gDebug.assertionCount;
        var numAsserts = newAssertionCount - gAssertionCount;
        gAssertionCount = newAssertionCount;

        var minAsserts = gURLs[0].minAsserts;
        var maxAsserts = gURLs[0].maxAsserts;

        var expectedAssertions = "expected " + minAsserts;
        if (minAsserts != maxAsserts) {
            expectedAssertions += " to " + maxAsserts;
        }
        expectedAssertions += " assertions";

        if (numAsserts < minAsserts) {
            ++gTestResults.AssertionUnexpectedFixed;
            // TEMPORARILY DISABLING REPORTING ON TINDERBOX BY REVERSING
            // THE WORD "UNEXPECTED".
            dump("REFTEST TEST-DETCEPXENU-PASS | " + gURLs[0].prettyPath +
                 " | assertion count " + numAsserts + " is less than " +
                 expectedAssertions + "\n");
        } else if (numAsserts > maxAsserts) {
            ++gTestResults.AssertionUnexpected;
            // TEMPORARILY DISABLING REPORTING ON TINDERBOX BY REVERSING
            // THE WORD "UNEXPECTED".
            dump("REFTEST TEST-DETCEPXENU-FAIL | " + gURLs[0].prettyPath +
                 " | assertion count " + numAsserts + " is more than " +
                 expectedAssertions + "\n");
        } else if (numAsserts != 0) {
            ++gTestResults.AssertionKnown;
            dump("REFTEST TEST-KNOWN-FAIL | " + gURLs[0].prettyPath +
                 " | assertion count " + numAsserts + " matches " +
                 expectedAssertions + "\n");
        }
    }

    // And start the next test.
    gURLs.shift();
    StartCurrentTest();
}
