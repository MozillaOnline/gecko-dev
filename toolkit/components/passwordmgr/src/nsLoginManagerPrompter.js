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
 * Portions created by the Initial Developer are Copyright (C) 2007
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *  Justin Dolske <dolske@mozilla.com> (original author)
 *  Ehsan Akhgari <ehsan.akhgari@gmail.com>
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
const Cr = Components.results;

Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");

/*
 * LoginManagerPromptFactory
 *
 * Implements nsIPromptFactory
 *
 * Invoked by NS_NewAuthPrompter2()
 * [embedding/components/windowwatcher/src/nsPrompt.cpp]
 */
function LoginManagerPromptFactory() {
    var observerService = Cc["@mozilla.org/observer-service;1"].
                          getService(Ci.nsIObserverService);
    observerService.addObserver(this, "quit-application-granted", true);
}

LoginManagerPromptFactory.prototype = {

    classDescription : "LoginManagerPromptFactory",
    contractID : "@mozilla.org/passwordmanager/authpromptfactory;1",
    classID : Components.ID("{749e62f4-60ae-4569-a8a2-de78b649660e}"),
    QueryInterface : XPCOMUtils.generateQI([Ci.nsIPromptFactory, Ci.nsIObserver, Ci.nsISupportsWeakReference]),

    _debug : false,
    _asyncPrompts : {},
    _asyncPromptInProgress : false,

    __logService : null, // Console logging service, used for debugging.
    get _logService() {
        if (!this.__logService)
            this.__logService = Cc["@mozilla.org/consoleservice;1"].
                                getService(Ci.nsIConsoleService);
        return this.__logService;
    },

    __threadManager: null,
    get _threadManager() {
        if (!this.__threadManager)
            this.__threadManager = Cc["@mozilla.org/thread-manager;1"].
                                   getService(Ci.nsIThreadManager);
        return this.__threadManager;
    },

    observe : function (subject, topic, data) {
        if (topic == "quit-application-granted") {
            var asyncPrompts = this._asyncPrompts;
            this.__proto__._asyncPrompts = {};
            for each (var asyncPrompt in asyncPrompts) {
                for each (var consumer in asyncPrompt.consumers) {
                    if (consumer.callback) {
                        this.log("Canceling async auth prompt callback " + consumer.callback);
                        try {
                          consumer.callback.onAuthCancelled(consumer.context, true);
                        } catch (e) { /* Just ignore exceptions from the callback */ }
                    }
                }
            }
        }
    },

    getPrompt : function (aWindow, aIID) {
        var prefBranch = Cc["@mozilla.org/preferences-service;1"].
                         getService(Ci.nsIPrefService).getBranch("signon.");
        this._debug = prefBranch.getBoolPref("debug");

        var prompt = new LoginManagerPrompter().QueryInterface(aIID);
        prompt.init(aWindow, this);
        return prompt;
    },

    _doAsyncPrompt : function() {
        if (this._asyncPromptInProgress) {
            this.log("_doAsyncPrompt bypassed, already in progress");
            return;
        }

        // Find the first prompt key we have in the queue
        var hashKey = null;
        for (hashKey in this._asyncPrompts)
            break;

        if (!hashKey) {
            this.log("_doAsyncPrompt:run bypassed, no prompts in the queue");
            return;
        }

        this._asyncPromptInProgress = true;
        var self = this;

        var runnable = {
            run : function() {
                var ok = false;
                var prompt = self._asyncPrompts[hashKey];
                try {
                    self.log("_doAsyncPrompt:run - performing the prompt for '" + hashKey + "'");
                    ok = prompt.prompter.promptAuth(prompt.channel,
                                                    prompt.level,
                                                    prompt.authInfo);
                } catch (e) {
                    Components.utils.reportError("LoginManagerPrompter: " +
                        "_doAsyncPrompt:run: " + e + "\n");
                }

                delete self._asyncPrompts[hashKey];
                self._asyncPromptInProgress = false;

                for each (var consumer in prompt.consumers) {
                    if (!consumer.callback)
                        // Not having a callback means that consumer didn't provide it
                        // or canceled the notification
                        continue;

                    self.log("Calling back to " + consumer.callback + " ok=" + ok);
                    try {
                        if (ok)
                            consumer.callback.onAuthAvailable(consumer.context, prompt.authInfo);
                        else
                            consumer.callback.onAuthCancelled(consumer.context, true);
                    } catch (e) { /* Throw away exceptions caused by callback */ }
                }
                self._doAsyncPrompt();
            }
        }

        this._threadManager.mainThread.dispatch(runnable, Ci.nsIThread.DISPATCH_NORMAL);
        this.log("_doAsyncPrompt:run dispatched");
    },

    log : function (message) {
        if (!this._debug)
            return;

        dump("Pwmgr PromptFactory: " + message + "\n");
        this._logService.logStringMessage("Pwmgr PrompFactory: " + message);
    }
}; // end of LoginManagerPromptFactory implementation




/* ==================== LoginManagerPrompter ==================== */




/*
 * LoginManagerPrompter
 *
 * Implements interfaces for prompting the user to enter/save/change auth info.
 *
 * nsIAuthPrompt: Used by SeaMonkey, Thunderbird, but not Firefox.
 *
 * nsIAuthPrompt2: Is invoked by a channel for protocol-based authentication
 * (eg HTTP Authenticate, FTP login).
 *
 * nsILoginManagerPrompter: Used by Login Manager for saving/changing logins
 * found in HTML forms.
 */
function LoginManagerPrompter() {}

LoginManagerPrompter.prototype = {

    classDescription : "LoginManagerPrompter",
    contractID : "@mozilla.org/login-manager/prompter;1",
    classID : Components.ID("{8aa66d77-1bbb-45a6-991e-b8f47751c291}"),
    QueryInterface : XPCOMUtils.generateQI([Ci.nsIAuthPrompt,
                                            Ci.nsIAuthPrompt2,
                                            Ci.nsILoginManagerPrompter]),

    _factory       : null,
    _window        : null,
    _debug         : false, // mirrors signon.debug

    __pwmgr : null, // Password Manager service
    get _pwmgr() {
        if (!this.__pwmgr)
            this.__pwmgr = Cc["@mozilla.org/login-manager;1"].
                           getService(Ci.nsILoginManager);
        return this.__pwmgr;
    },

    __logService : null, // Console logging service, used for debugging.
    get _logService() {
        if (!this.__logService)
            this.__logService = Cc["@mozilla.org/consoleservice;1"].
                                getService(Ci.nsIConsoleService);
        return this.__logService;
    },

    __promptService : null, // Prompt service for user interaction
    get _promptService() {
        if (!this.__promptService)
            this.__promptService =
                Cc["@mozilla.org/embedcomp/prompt-service;1"].
                getService(Ci.nsIPromptService2);
        return this.__promptService;
    },


    __strBundle : null, // String bundle for L10N
    get _strBundle() {
        if (!this.__strBundle) {
            var bunService = Cc["@mozilla.org/intl/stringbundle;1"].
                             getService(Ci.nsIStringBundleService);
            this.__strBundle = bunService.createBundle(
                        "chrome://passwordmgr/locale/passwordmgr.properties");
            if (!this.__strBundle)
                throw "String bundle for Login Manager not present!";
        }

        return this.__strBundle;
    },


    __brandBundle : null, // String bundle for L10N
    get _brandBundle() {
        if (!this.__brandBundle) {
            var bunService = Cc["@mozilla.org/intl/stringbundle;1"].
                             getService(Ci.nsIStringBundleService);
            this.__brandBundle = bunService.createBundle(
                        "chrome://branding/locale/brand.properties");
            if (!this.__brandBundle)
                throw "Branding string bundle not present!";
        }

        return this.__brandBundle;
    },


    __ioService: null, // IO service for string -> nsIURI conversion
    get _ioService() {
        if (!this.__ioService)
            this.__ioService = Cc["@mozilla.org/network/io-service;1"].
                               getService(Ci.nsIIOService);
        return this.__ioService;
    },


    __ellipsis : null,
    get _ellipsis() {
        if (!this.__ellipsis) {
            this.__ellipsis = "\u2026";
            try {
                var prefSvc = Cc["@mozilla.org/preferences-service;1"].
                              getService(Ci.nsIPrefBranch);
                this.__ellipsis = prefSvc.getComplexValue("intl.ellipsis",
                                      Ci.nsIPrefLocalizedString).data;
            } catch (e) { }
        }
        return this.__ellipsis;
    },


    // Whether we are in private browsing mode
    get _inPrivateBrowsing() {
      // The Private Browsing service might not be available.
      try {
        var pbs = Cc["@mozilla.org/privatebrowsing;1"].
                  getService(Ci.nsIPrivateBrowsingService);
        return pbs.privateBrowsingEnabled;
      } catch (e) {
        return false;
      }
    },


    /*
     * log
     *
     * Internal function for logging debug messages to the Error Console window.
     */
    log : function (message) {
        if (!this._debug)
            return;

        dump("Pwmgr Prompter: " + message + "\n");
        this._logService.logStringMessage("Pwmgr Prompter: " + message);
    },




    /* ---------- nsIAuthPrompt prompts ---------- */


    /*
     * prompt
     *
     * Wrapper around the prompt service prompt. Saving random fields here
     * doesn't really make sense and therefore isn't implemented.
     */
    prompt : function (aDialogTitle, aText, aPasswordRealm,
                       aSavePassword, aDefaultText, aResult) {
        if (aSavePassword != Ci.nsIAuthPrompt.SAVE_PASSWORD_NEVER)
            throw Components.results.NS_ERROR_NOT_IMPLEMENTED;

        this.log("===== prompt() called =====");

        if (aDefaultText) {
            aResult.value = aDefaultText;
        }

        return this._promptService.prompt(this._window,
               aDialogTitle, aText, aResult, null, {});
    },


    /*
     * promptUsernameAndPassword
     *
     * Looks up a username and password in the database. Will prompt the user
     * with a dialog, even if a username and password are found.
     */
    promptUsernameAndPassword : function (aDialogTitle, aText, aPasswordRealm,
                                         aSavePassword, aUsername, aPassword) {
        this.log("===== promptUsernameAndPassword() called =====");

        if (aSavePassword == Ci.nsIAuthPrompt.SAVE_PASSWORD_FOR_SESSION)
            throw Components.results.NS_ERROR_NOT_IMPLEMENTED;

        var selectedLogin = null;
        var checkBox = { value : false };
        var checkBoxLabel = null;
        var [hostname, realm, unused] = this._getRealmInfo(aPasswordRealm);

        // If hostname is null, we can't save this login.
        if (hostname) {
            var canRememberLogin;
            if (this._inPrivateBrowsing)
                canRememberLogin = false;
            else
                canRememberLogin = (aSavePassword ==
                                    Ci.nsIAuthPrompt.SAVE_PASSWORD_PERMANENTLY) &&
                                   this._pwmgr.getLoginSavingEnabled(hostname);

            // if checkBoxLabel is null, the checkbox won't be shown at all.
            if (canRememberLogin)
                checkBoxLabel = this._getLocalizedString("rememberPassword");

            // Look for existing logins.
            var foundLogins = this._pwmgr.findLogins({}, hostname, null,
                                                     realm);

            // XXX Like the original code, we can't deal with multiple
            // account selection. (bug 227632)
            if (foundLogins.length > 0) {
                selectedLogin = foundLogins[0];

                // If the caller provided a username, try to use it. If they
                // provided only a password, this will try to find a password-only
                // login (or return null if none exists).
                if (aUsername.value)
                    selectedLogin = this._repickSelectedLogin(foundLogins,
                                                              aUsername.value);

                if (selectedLogin) {
                    checkBox.value = true;
                    aUsername.value = selectedLogin.username;
                    // If the caller provided a password, prefer it.
                    if (!aPassword.value)
                        aPassword.value = selectedLogin.password;
                }
            }
        }

        var ok = this._promptService.promptUsernameAndPassword(this._window,
                    aDialogTitle, aText, aUsername, aPassword,
                    checkBoxLabel, checkBox);

        if (!ok || !checkBox.value || !hostname)
            return ok;

        if (!aPassword.value) {
            this.log("No password entered, so won't offer to save.");
            return ok;
        }

        var newLogin = Cc["@mozilla.org/login-manager/loginInfo;1"].
                       createInstance(Ci.nsILoginInfo);
        newLogin.init(hostname, null, realm, aUsername.value, aPassword.value,
                      "", "");

        // XXX We can't prompt with multiple logins yet (bug 227632), so
        // the entered login might correspond to an existing login
        // other than the one we originally selected.
        selectedLogin = this._repickSelectedLogin(foundLogins, aUsername.value);

        // If we didn't find an existing login, or if the username
        // changed, save as a new login.
        if (!selectedLogin) {
            // add as new
            this.log("New login seen for " + realm);
            this._pwmgr.addLogin(newLogin);
        } else if (aPassword.value != selectedLogin.password) {
            // update password
            this.log("Updating password for  " + realm);
            this._pwmgr.modifyLogin(selectedLogin, newLogin);
        } else {
            this.log("Login unchanged, no further action needed.");
        }

        return ok;
    },


    /*
     * promptPassword
     *
     * If a password is found in the database for the password realm, it is
     * returned straight away without displaying a dialog.
     *
     * If a password is not found in the database, the user will be prompted
     * with a dialog with a text field and ok/cancel buttons. If the user
     * allows it, then the password will be saved in the database.
     */
    promptPassword : function (aDialogTitle, aText, aPasswordRealm,
                               aSavePassword, aPassword) {
        this.log("===== promptPassword called() =====");

        if (aSavePassword == Ci.nsIAuthPrompt.SAVE_PASSWORD_FOR_SESSION)
            throw Components.results.NS_ERROR_NOT_IMPLEMENTED;

        var checkBox = { value : false };
        var checkBoxLabel = null;
        var [hostname, realm, username] = this._getRealmInfo(aPasswordRealm);

        username = decodeURIComponent(username);

        // If hostname is null, we can't save this login.
        if (hostname && !this._inPrivateBrowsing) {
          var canRememberLogin = (aSavePassword ==
                                  Ci.nsIAuthPrompt.SAVE_PASSWORD_PERMANENTLY) &&
                                 this._pwmgr.getLoginSavingEnabled(hostname);
  
          // if checkBoxLabel is null, the checkbox won't be shown at all.
          if (canRememberLogin)
              checkBoxLabel = this._getLocalizedString("rememberPassword");
  
          if (!aPassword.value) {
              // Look for existing logins.
              var foundLogins = this._pwmgr.findLogins({}, hostname, null,
                                                       realm);
  
              // XXX Like the original code, we can't deal with multiple
              // account selection (bug 227632). We can deal with finding the
              // account based on the supplied username - but in this case we'll
              // just return the first match.
              for (var i = 0; i < foundLogins.length; ++i) {
                  if (foundLogins[i].username == username) {
                      aPassword.value = foundLogins[i].password;
                      // wallet returned straight away, so this mimics that code
                      return true;
                  }
              }
          }
        }

        var ok = this._promptService.promptPassword(this._window, aDialogTitle,
                                                    aText, aPassword,
                                                    checkBoxLabel, checkBox);

        if (ok && checkBox.value && hostname && aPassword.value) {
            var newLogin = Cc["@mozilla.org/login-manager/loginInfo;1"].
                           createInstance(Ci.nsILoginInfo);
            newLogin.init(hostname, null, realm, username,
                          aPassword.value, "", "");

            this.log("New login seen for " + realm);

            this._pwmgr.addLogin(newLogin);
        }

        return ok;
    },

    /* ---------- nsIAuthPrompt helpers ---------- */


    /**
     * Given aRealmString, such as "http://user@example.com/foo", returns an
     * array of:
     *   - the formatted hostname
     *   - the realm (hostname + path)
     *   - the username, if present
     *
     * If aRealmString is in the format produced by NS_GetAuthKey for HTTP[S]
     * channels, e.g. "example.com:80 (httprealm)", null is returned for all
     * arguments to let callers know the login can't be saved because we don't
     * know whether it's http or https.
     */
    _getRealmInfo : function (aRealmString) {
        var httpRealm = /^.+ \(.+\)$/;
        if (httpRealm.test(aRealmString))
            return [null, null, null];

        var uri = this._ioService.newURI(aRealmString, null, null);
        var pathname = "";

        if (uri.path != "/")
            pathname = uri.path;

        var formattedHostname = this._getFormattedHostname(uri);

        return [formattedHostname, formattedHostname + pathname, uri.username];
    },

    /* ---------- nsIAuthPrompt2 prompts ---------- */




    /*
     * promptAuth
     *
     * Implementation of nsIAuthPrompt2.
     *
     * nsIChannel aChannel
     * int        aLevel
     * nsIAuthInformation aAuthInfo
     */
    promptAuth : function (aChannel, aLevel, aAuthInfo) {
        var selectedLogin = null;
        var checkbox = { value : false };
        var checkboxLabel = null;
        var epicfail = false;

        try {

            this.log("===== promptAuth called =====");

            // If the user submits a login but it fails, we need to remove the
            // notification bar that was displayed. Conveniently, the user will
            // be prompted for authentication again, which brings us here.
            var notifyBox = this._getNotifyBox();
            if (notifyBox)
                this._removeLoginNotifications(notifyBox);

            var [hostname, httpRealm] = this._getAuthTarget(aChannel, aAuthInfo);


            // Looks for existing logins to prefill the prompt with.
            var foundLogins = this._pwmgr.findLogins({},
                                        hostname, null, httpRealm);
            this.log("found " + foundLogins.length + " matching logins.");

            // XXX Can't select from multiple accounts yet. (bug 227632)
            if (foundLogins.length > 0) {
                selectedLogin = foundLogins[0];
                this._SetAuthInfo(aAuthInfo, selectedLogin.username,
                                             selectedLogin.password);
                checkbox.value = true;
            }

            var canRememberLogin = this._pwmgr.getLoginSavingEnabled(hostname);
            if (this._inPrivateBrowsing)
              canRememberLogin = false;
        
            // if checkboxLabel is null, the checkbox won't be shown at all.
            if (canRememberLogin && !notifyBox)
                checkboxLabel = this._getLocalizedString("rememberPassword");
        } catch (e) {
            // Ignore any errors and display the prompt anyway.
            epicfail = true;
            Components.utils.reportError("LoginManagerPrompter: " +
                "Epic fail in promptAuth: " + e + "\n");
        }

        var ok = this._promptService.promptAuth(this._window, aChannel,
                                aLevel, aAuthInfo, checkboxLabel, checkbox);

        // If there's a notification box, use it to allow the user to
        // determine if the login should be saved. If there isn't a
        // notification box, only save the login if the user set the
        // checkbox to do so.
        var rememberLogin = notifyBox ? canRememberLogin : checkbox.value;
        if (!ok || !rememberLogin || epicfail)
            return ok;

        try {
            var [username, password] = this._GetAuthInfo(aAuthInfo);

            if (!password) {
                this.log("No password entered, so won't offer to save.");
                return ok;
            }

            var newLogin = Cc["@mozilla.org/login-manager/loginInfo;1"].
                           createInstance(Ci.nsILoginInfo);
            newLogin.init(hostname, null, httpRealm,
                          username, password, "", "");

            // XXX We can't prompt with multiple logins yet (bug 227632), so
            // the entered login might correspond to an existing login
            // other than the one we originally selected.
            selectedLogin = this._repickSelectedLogin(foundLogins, username);

            // If we didn't find an existing login, or if the username
            // changed, save as a new login.
            if (!selectedLogin) {
                // add as new
                this.log("New login seen for " + username +
                         " @ " + hostname + " (" + httpRealm + ")");
                if (notifyBox)
                    this._showSaveLoginNotification(notifyBox, newLogin);
                else
                    this._pwmgr.addLogin(newLogin);

            } else if (password != selectedLogin.password) {

                this.log("Updating password for " + username +
                         " @ " + hostname + " (" + httpRealm + ")");
                if (notifyBox)
                    this._showChangeLoginNotification(notifyBox,
                                                      selectedLogin, newLogin);
                else
                    this._pwmgr.modifyLogin(selectedLogin, newLogin);

            } else {
                this.log("Login unchanged, no further action needed.");
            }
        } catch (e) {
            Components.utils.reportError("LoginManagerPrompter: " +
                "Fail2 in promptAuth: " + e + "\n");
        }

        return ok;
    },

    asyncPromptAuth : function (aChannel, aCallback, aContext, aLevel, aAuthInfo) {
        var cancelable = null;

        try {
            this.log("===== asyncPromptAuth called =====");

            // If the user submits a login but it fails, we need to remove the
            // notification bar that was displayed. Conveniently, the user will
            // be prompted for authentication again, which brings us here.
            var notifyBox = this._getNotifyBox();
            if (notifyBox)
                this._removeLoginNotifications(notifyBox);

            cancelable = this._newAsyncPromptConsumer(aCallback, aContext);

            var [hostname, httpRealm] = this._getAuthTarget(aChannel, aAuthInfo);

            var hashKey = aLevel + "|" + hostname + "|" + httpRealm;
            this.log("Async prompt key = " + hashKey);
            var asyncPrompt = this._factory._asyncPrompts[hashKey];
            if (asyncPrompt) {
                this.log("Prompt bound to an existing one in the queue, callback = " + aCallback);
                asyncPrompt.consumers.push(cancelable);
                return cancelable;
            }

            this.log("Adding new prompt to the queue, callback = " + aCallback);
            asyncPrompt = {
                consumers: [cancelable],
                channel: aChannel,
                authInfo: aAuthInfo,
                level: aLevel,
                prompter: this
            }

            this._factory._asyncPrompts[hashKey] = asyncPrompt;
            this._factory._doAsyncPrompt();
        }
        catch (e) {
            Components.utils.reportError("LoginManagerPrompter: " +
                "asyncPromptAuth: " + e + "\nFalling back to promptAuth\n");
            // Fail the prompt operation to let the consumer fall back
            // to synchronous promptAuth method
            throw e;
        }

        return cancelable;
    },




    /* ---------- nsILoginManagerPrompter prompts ---------- */




    /*
     * init
     *
     */
    init : function (aWindow, aFactory) {
        this._window = aWindow;
        this._factory = aFactory || null;

        var prefBranch = Cc["@mozilla.org/preferences-service;1"].
                         getService(Ci.nsIPrefService).getBranch("signon.");
        this._debug = prefBranch.getBoolPref("debug");
        this.log("===== initialized =====");
    },


    /*
     * promptToSavePassword
     *
     */
    promptToSavePassword : function (aLogin) {
        var notifyBox = this._getNotifyBox();

        if (notifyBox)
            this._showSaveLoginNotification(notifyBox, aLogin);
        else
            this._showSaveLoginDialog(aLogin);
    },


    /*
     * _showLoginNotification
     *
     * Displays a notification bar.
     *
     */
    _showLoginNotification : function (aNotifyBox, aName, aText, aButtons) {
        var oldBar = aNotifyBox.getNotificationWithValue(aName);
        const priority = aNotifyBox.PRIORITY_INFO_MEDIUM;

        this.log("Adding new " + aName + " notification bar");
        var newBar = aNotifyBox.appendNotification(
                                aText, aName,
                                "chrome://mozapps/skin/passwordmgr/key.png",
                                priority, aButtons);

        // The page we're going to hasn't loaded yet, so we want to persist
        // across the first location change.
        newBar.persistence++;

        // Sites like Gmail perform a funky redirect dance before you end up
        // at the post-authentication page. I don't see a good way to
        // heuristically determine when to ignore such location changes, so
        // we'll try ignoring location changes based on a time interval.
        newBar.timeout = Date.now() + 20000; // 20 seconds

        if (oldBar) {
            this.log("(...and removing old " + aName + " notification bar)");
            aNotifyBox.removeNotification(oldBar);
        }
    },


    /*
     * _showSaveLoginNotification
     *
     * Displays a notification bar (rather than a popup), to allow the user to
     * save the specified login. This allows the user to see the results of
     * their login, and only save a login which they know worked.
     *
     */
    _showSaveLoginNotification : function (aNotifyBox, aLogin) {

        // Ugh. We can't use the strings from the popup window, because they
        // have the access key marked in the string (eg "Mo&zilla"), along
        // with some weird rules for handling access keys that do not occur
        // in the string, for L10N. See commonDialog.js's setLabelForNode().
        var neverButtonText =
              this._getLocalizedString("notifyBarNeverForSiteButtonText");
        var neverButtonAccessKey =
              this._getLocalizedString("notifyBarNeverForSiteButtonAccessKey");
        var rememberButtonText =
              this._getLocalizedString("notifyBarRememberButtonText");
        var rememberButtonAccessKey =
              this._getLocalizedString("notifyBarRememberButtonAccessKey");
        var notNowButtonText =
              this._getLocalizedString("notifyBarNotNowButtonText");
        var notNowButtonAccessKey =
              this._getLocalizedString("notifyBarNotNowButtonAccessKey");

        var brandShortName =
              this._brandBundle.GetStringFromName("brandShortName");
        var displayHost = this._getShortDisplayHost(aLogin.hostname);
        var notificationText;
        if (aLogin.username) {
            var displayUser = this._sanitizeUsername(aLogin.username);
            notificationText  = this._getLocalizedString(
                                        "saveLoginText",
                                        [brandShortName, displayUser, displayHost]);
        } else {
            notificationText  = this._getLocalizedString(
                                        "saveLoginTextNoUsername",
                                        [brandShortName, displayHost]);
        }

        // The callbacks in |buttons| have a closure to access the variables
        // in scope here; set one to |this._pwmgr| so we can get back to pwmgr
        // without a getService() call.
        var pwmgr = this._pwmgr;


        var buttons = [
            // "Remember" button
            {
                label:     rememberButtonText,
                accessKey: rememberButtonAccessKey,
                popup:     null,
                callback: function(aNotificationBar, aButton) {
                    pwmgr.addLogin(aLogin);
                }
            },

            // "Never for this site" button
            {
                label:     neverButtonText,
                accessKey: neverButtonAccessKey,
                popup:     null,
                callback: function(aNotificationBar, aButton) {
                    pwmgr.setLoginSavingEnabled(aLogin.hostname, false);
                }
            },

            // "Not now" button
            {
                label:     notNowButtonText,
                accessKey: notNowButtonAccessKey,
                popup:     null,
                callback:  function() { /* NOP */ } 
            }
        ];

        this._showLoginNotification(aNotifyBox, "password-save",
             notificationText, buttons);
    },


    /*
     * _removeLoginNotifications
     *
     */
    _removeLoginNotifications : function (aNotifyBox) {
        var oldBar = aNotifyBox.getNotificationWithValue("password-save");
        if (oldBar) {
            this.log("Removing save-password notification bar.");
            aNotifyBox.removeNotification(oldBar);
        }

        oldBar = aNotifyBox.getNotificationWithValue("password-change");
        if (oldBar) {
            this.log("Removing change-password notification bar.");
            aNotifyBox.removeNotification(oldBar);
        }
    },


    /*
     * _showSaveLoginDialog
     *
     * Called when we detect a new login in a form submission,
     * asks the user what to do.
     *
     */
    _showSaveLoginDialog : function (aLogin) {
        const buttonFlags = Ci.nsIPrompt.BUTTON_POS_1_DEFAULT +
            (Ci.nsIPrompt.BUTTON_TITLE_IS_STRING * Ci.nsIPrompt.BUTTON_POS_0) +
            (Ci.nsIPrompt.BUTTON_TITLE_IS_STRING * Ci.nsIPrompt.BUTTON_POS_1) +
            (Ci.nsIPrompt.BUTTON_TITLE_IS_STRING * Ci.nsIPrompt.BUTTON_POS_2);

        var brandShortName =
                this._brandBundle.GetStringFromName("brandShortName");
        var displayHost = this._getShortDisplayHost(aLogin.hostname);

        var dialogText;
        if (aLogin.username) {
            var displayUser = this._sanitizeUsername(aLogin.username);
            dialogText = this._getLocalizedString(
                                 "saveLoginText",
                                 [brandShortName, displayUser, displayHost]);
        } else {
            dialogText = this._getLocalizedString(
                                 "saveLoginTextNoUsername",
                                 [brandShortName, displayHost]);
        }
        var dialogTitle        = this._getLocalizedString(
                                        "savePasswordTitle");
        var neverButtonText    = this._getLocalizedString(
                                        "neverForSiteButtonText");
        var rememberButtonText = this._getLocalizedString(
                                        "rememberButtonText");
        var notNowButtonText   = this._getLocalizedString(
                                        "notNowButtonText");

        this.log("Prompting user to save/ignore login");
        var userChoice = this._promptService.confirmEx(this._window,
                                            dialogTitle, dialogText,
                                            buttonFlags, rememberButtonText,
                                            notNowButtonText, neverButtonText,
                                            null, {});
        //  Returns:
        //   0 - Save the login
        //   1 - Ignore the login this time
        //   2 - Never save logins for this site
        if (userChoice == 2) {
            this.log("Disabling " + aLogin.hostname + " logins by request.");
            this._pwmgr.setLoginSavingEnabled(aLogin.hostname, false);
        } else if (userChoice == 0) {
            this.log("Saving login for " + aLogin.hostname);
            this._pwmgr.addLogin(aLogin);
        } else {
            // userChoice == 1 --> just ignore the login.
            this.log("Ignoring login.");
        }
    },


    /*
     * promptToChangePassword
     *
     * Called when we think we detect a password change for an existing
     * login, when the form being submitted contains multiple password
     * fields.
     *
     */
    promptToChangePassword : function (aOldLogin, aNewLogin) {
        var notifyBox = this._getNotifyBox();

        if (notifyBox)
            this._showChangeLoginNotification(notifyBox, aOldLogin, aNewLogin);
        else
            this._showChangeLoginDialog(aOldLogin, aNewLogin);
    },


    /*
     * _showChangeLoginNotification
     *
     * Shows the Change Password notification bar.
     *
     */
    _showChangeLoginNotification : function (aNotifyBox, aOldLogin, aNewLogin) {
        var notificationText;
        if (aOldLogin.username)
            notificationText  = this._getLocalizedString(
                                          "passwordChangeText",
                                          [aOldLogin.username]);
        else
            notificationText  = this._getLocalizedString(
                                          "passwordChangeTextNoUser");

        var changeButtonText =
              this._getLocalizedString("notifyBarChangeButtonText");
        var changeButtonAccessKey =
              this._getLocalizedString("notifyBarChangeButtonAccessKey");
        var dontChangeButtonText =
              this._getLocalizedString("notifyBarDontChangeButtonText");
        var dontChangeButtonAccessKey =
              this._getLocalizedString("notifyBarDontChangeButtonAccessKey");

        // The callbacks in |buttons| have a closure to access the variables
        // in scope here; set one to |this._pwmgr| so we can get back to pwmgr
        // without a getService() call.
        var pwmgr = this._pwmgr;

        var buttons = [
            // "Yes" button
            {
                label:     changeButtonText,
                accessKey: changeButtonAccessKey,
                popup:     null,
                callback:  function(aNotificationBar, aButton) {
                    pwmgr.modifyLogin(aOldLogin, aNewLogin);
                }
            },

            // "No" button
            {
                label:     dontChangeButtonText,
                accessKey: dontChangeButtonAccessKey,
                popup:     null,
                callback:  function(aNotificationBar, aButton) {
                    // do nothing
                }
            }
        ];

        this._showLoginNotification(aNotifyBox, "password-change",
             notificationText, buttons);
    },


    /*
     * _showChangeLoginDialog
     *
     * Shows the Change Password dialog.
     *
     */
    _showChangeLoginDialog : function (aOldLogin, aNewLogin) {
        const buttonFlags = Ci.nsIPrompt.STD_YES_NO_BUTTONS;

        var dialogText;
        if (aOldLogin.username)
            dialogText  = this._getLocalizedString(
                                    "passwordChangeText",
                                    [aOldLogin.username]);
        else
            dialogText  = this._getLocalizedString(
                                    "passwordChangeTextNoUser");

        var dialogTitle = this._getLocalizedString(
                                    "passwordChangeTitle");

        // returns 0 for yes, 1 for no.
        var ok = !this._promptService.confirmEx(this._window,
                                dialogTitle, dialogText, buttonFlags,
                                null, null, null,
                                null, {});
        if (ok) {
            this.log("Updating password for user " + aOldLogin.username);
            this._pwmgr.modifyLogin(aOldLogin, aNewLogin);
        }
    },


    /*
     * promptToChangePasswordWithUsernames
     *
     * Called when we detect a password change in a form submission, but we
     * don't know which existing login (username) it's for. Asks the user
     * to select a username and confirm the password change.
     *
     * Note: The caller doesn't know the username for aNewLogin, so this
     *       function fills in .username and .usernameField with the values
     *       from the login selected by the user.
     * 
     * Note; XPCOM stupidity: |count| is just |logins.length|.
     */
    promptToChangePasswordWithUsernames : function (logins, count, aNewLogin) {
        const buttonFlags = Ci.nsIPrompt.STD_YES_NO_BUTTONS;

        var usernames = logins.map(function (l) l.username);
        var dialogText  = this._getLocalizedString("userSelectText");
        var dialogTitle = this._getLocalizedString("passwordChangeTitle");
        var selectedIndex = { value: null };

        // If user selects ok, outparam.value is set to the index
        // of the selected username.
        var ok = this._promptService.select(this._window,
                                dialogTitle, dialogText,
                                usernames.length, usernames,
                                selectedIndex);
        if (ok) {
            // Now that we know which login to change the password for,
            // update the missing username info in the aNewLogin.

            var selectedLogin = logins[selectedIndex.value];

            this.log("Updating password for user " + selectedLogin.username);

            aNewLogin.username      = selectedLogin.username;
            aNewLogin.usernameField = selectedLogin.usernameField;

            this._pwmgr.modifyLogin(selectedLogin, aNewLogin);
        }
    },




    /* ---------- Internal Methods ---------- */




    /*
     * _getNotifyBox
     *
     * Returns the notification box to this prompter, or null if there isn't
     * a notification box available.
     */
    _getNotifyBox : function () {
        var notifyBox = null;

        // Given a content DOM window, returns the chrome window it's in.
        function getChromeWindow(aWindow) {
            var chromeWin = aWindow 
                                .QueryInterface(Ci.nsIInterfaceRequestor)
                                .getInterface(Ci.nsIWebNavigation)
                                .QueryInterface(Ci.nsIDocShellTreeItem)
                                .rootTreeItem
                                .QueryInterface(Ci.nsIInterfaceRequestor)
                                .getInterface(Ci.nsIDOMWindow)
                                .QueryInterface(Ci.nsIDOMChromeWindow);
            return chromeWin;
        }

        try {
            // Get topmost window, in case we're in a frame.
            var notifyWindow = this._window.top

            // Some sites pop up a temporary login window, when disappears
            // upon submission of credentials. We want to put the notification
            // bar in the opener window if this seems to be happening.
            if (notifyWindow.opener) {
                var chromeDoc = getChromeWindow(notifyWindow)
                                    .document.documentElement;
                var webnav = notifyWindow
                                    .QueryInterface(Ci.nsIInterfaceRequestor)
                                    .getInterface(Ci.nsIWebNavigation);

                // Check to see if the current window was opened with chrome
                // disabled, and if so use the opener window. But if the window
                // has been used to visit other pages (ie, has a history),
                // assume it'll stick around and *don't* use the opener.
                if (chromeDoc.getAttribute("chromehidden") &&
                    webnav.sessionHistory.count == 1) {
                    this.log("Using opener window for notification bar.");
                    notifyWindow = notifyWindow.opener;
                }
            }


            // Get the chrome window for the content window we're using.
            // .wrappedJSObject needed here -- see bug 422974 comment 5.
            var chromeWin = getChromeWindow(notifyWindow).wrappedJSObject;

            if (chromeWin.getNotificationBox)
                notifyBox = chromeWin.getNotificationBox(notifyWindow);
            else
                this.log("getNotificationBox() not available on window");

        } catch (e) {
            // If any errors happen, just assume no notification box.
            this.log("No notification box available: " + e)
        }

        return notifyBox;
    },


    /*
     * _repickSelectedLogin
     *
     * The user might enter a login that isn't the one we prefilled, but
     * is the same as some other existing login. So, pick a login with a
     * matching username, or return null.
     */
    _repickSelectedLogin : function (foundLogins, username) {
        for (var i = 0; i < foundLogins.length; i++)
            if (foundLogins[i].username == username)
                return foundLogins[i];
        return null;
    },

    
    /*
     * _getLocalizedString
     *
     * Can be called as:
     *   _getLocalizedString("key1");
     *   _getLocalizedString("key2", ["arg1"]);
     *   _getLocalizedString("key3", ["arg1", "arg2"]);
     *   (etc)
     *
     * Returns the localized string for the specified key,
     * formatted if required.
     *
     */ 
    _getLocalizedString : function (key, formatArgs) {
        if (formatArgs)
            return this._strBundle.formatStringFromName(
                                        key, formatArgs, formatArgs.length);
        else
            return this._strBundle.GetStringFromName(key);
    },


    /*
     * _sanitizeUsername
     *
     * Sanitizes the specified username, by stripping quotes and truncating if
     * it's too long. This helps prevent an evil site from messing with the
     * "save password?" prompt too much.
     */
    _sanitizeUsername : function (username) {
        if (username.length > 30) {
            username = username.substring(0, 30);
            username += this._ellipsis;
        }
        return username.replace(/['"]/g, "");
    },


    /*
     * _getFormattedHostname
     *
     * The aURI parameter may either be a string uri, or an nsIURI instance.
     *
     * Returns the hostname to use in a nsILoginInfo object (for example,
     * "http://example.com").
     */
    _getFormattedHostname : function (aURI) {
        var uri;
        if (aURI instanceof Ci.nsIURI) {
            uri = aURI;
        } else {
            uri = this._ioService.newURI(aURI, null, null);
        }
        var scheme = uri.scheme;

        var hostname = scheme + "://" + uri.host;

        // If the URI explicitly specified a port, only include it when
        // it's not the default. (We never want "http://foo.com:80")
        port = uri.port;
        if (port != -1) {
            var handler = this._ioService.getProtocolHandler(scheme);
            if (port != handler.defaultPort)
                hostname += ":" + port;
        }

        return hostname;
    },


    /*
     * _getShortDisplayHost
     *
     * Converts a login's hostname field (a URL) to a short string for
     * prompting purposes. Eg, "http://foo.com" --> "foo.com", or
     * "ftp://www.site.co.uk" --> "site.co.uk".
     */
    _getShortDisplayHost: function (aURIString) {
        var displayHost;

        var eTLDService = Cc["@mozilla.org/network/effective-tld-service;1"].
                          getService(Ci.nsIEffectiveTLDService);
        var idnService = Cc["@mozilla.org/network/idn-service;1"].
                         getService(Ci.nsIIDNService);
        try {
            var uri = this._ioService.newURI(aURIString, null, null);
            var baseDomain = eTLDService.getBaseDomain(uri);
            displayHost = idnService.convertToDisplayIDN(baseDomain, {});
        } catch (e) {
            this.log("_getShortDisplayHost couldn't process " + aURIString);
        }

        if (!displayHost)
            displayHost = aURIString;

        return displayHost;
    },


    /*
     * _getAuthTarget
     *
     * Returns the hostname and realm for which authentication is being
     * requested, in the format expected to be used with nsILoginInfo.
     */
    _getAuthTarget : function (aChannel, aAuthInfo) {
        var hostname, realm;

        // If our proxy is demanding authentication, don't use the
        // channel's actual destination.
        if (aAuthInfo.flags & Ci.nsIAuthInformation.AUTH_PROXY) {
            this.log("getAuthTarget is for proxy auth");
            if (!(aChannel instanceof Ci.nsIProxiedChannel))
                throw "proxy auth needs nsIProxiedChannel";

            var info = aChannel.proxyInfo;
            if (!info)
                throw "proxy auth needs nsIProxyInfo";

            // Proxies don't have a scheme, but we'll use "moz-proxy://"
            // so that it's more obvious what the login is for.
            var idnService = Cc["@mozilla.org/network/idn-service;1"].
                             getService(Ci.nsIIDNService);
            hostname = "moz-proxy://" +
                        idnService.convertUTF8toACE(info.host) +
                        ":" + info.port;
            realm = aAuthInfo.realm;
            if (!realm)
                realm = hostname;

            return [hostname, realm];
        }

        hostname = this._getFormattedHostname(aChannel.URI);

        // If a HTTP WWW-Authenticate header specified a realm, that value
        // will be available here. If it wasn't set or wasn't HTTP, we'll use
        // the formatted hostname instead.
        realm = aAuthInfo.realm;
        if (!realm)
            realm = hostname;

        return [hostname, realm];
    },


    /**
     * Returns [username, password] as extracted from aAuthInfo (which
     * holds this info after having prompted the user).
     *
     * If the authentication was for a Windows domain, we'll prepend the
     * return username with the domain. (eg, "domain\user")
     */
    _GetAuthInfo : function (aAuthInfo) {
        var username, password;

        var flags = aAuthInfo.flags;
        if (flags & Ci.nsIAuthInformation.NEED_DOMAIN && aAuthInfo.domain)
            username = aAuthInfo.domain + "\\" + aAuthInfo.username;
        else
            username = aAuthInfo.username;

        password = aAuthInfo.password;

        return [username, password];
    },


    /**
     * Given a username (possibly in DOMAIN\user form) and password, parses the
     * domain out of the username if necessary and sets domain, username and
     * password on the auth information object.
     */
    _SetAuthInfo : function (aAuthInfo, username, password) {
        var flags = aAuthInfo.flags;
        if (flags & Ci.nsIAuthInformation.NEED_DOMAIN) {
            // Domain is separated from username by a backslash
            var idx = username.indexOf("\\");
            if (idx == -1) {
                aAuthInfo.username = username;
            } else {
                aAuthInfo.domain   =  username.substring(0, idx);
                aAuthInfo.username =  username.substring(idx+1);
            }
        } else {
            aAuthInfo.username = username;
        }
        aAuthInfo.password = password;
    },

    _newAsyncPromptConsumer : function(aCallback, aContext) {
        return {
            QueryInterface: XPCOMUtils.generateQI([Ci.nsICancelable]),
            callback: aCallback,
            context: aContext,
            cancel: function() {
                this.callback.onAuthCancelled(this.context, false);
                this.callback = null;
                this.context = null;
            }
        }
    }

}; // end of LoginManagerPrompter implementation


var component = [LoginManagerPromptFactory, LoginManagerPrompter];
function NSGetModule(compMgr, fileSpec) {
    return XPCOMUtils.generateModule(component);
}
