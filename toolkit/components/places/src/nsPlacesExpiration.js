/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=2 sts=2 expandtab
 * ***** BEGIN LICENSE BLOCK *****
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
 * The Initial Developer of the Original Code is
 * Mozilla Foundation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Marco Bonardo <mak77@bonardo.net> (Original Author)
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
 * This component handles history and orphans expiration through asynchronous
 * Storage statements.
 * Expiration runs:
 * - At idle, but just once, we stop any other kind of expiration during idle
 *   to preserve batteries in portable devices.
 * - At shutdown, only if the database is dirty, we should still avoid to
 *   expire too heavily on shutdown.
 * - On ClearHistory we run a full expiration for privacy reasons.
 * - On a repeating timer we expire in small chunks.
 *
 * Expiration algorithm will adapt itself based on:
 * - Memory size of the device.
 * - Status of the database (clean or dirty).
 */

const Cc = Components.classes;
const Ci = Components.interfaces;

Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");

////////////////////////////////////////////////////////////////////////////////
//// nsIFactory

const nsPlacesExpirationFactory = {
  _instance: null,
  createInstance: function(aOuter, aIID) {
    if (aOuter != null)
      throw Components.results.NS_ERROR_NO_AGGREGATION;
    return this._instance === null ? this._instance = new nsPlacesExpiration() :
                                     this._instance;
  },
  lockFactory: function (aDoLock) {},
  QueryInterface: XPCOMUtils.generateQI([
    Ci.nsIFactory
  ]),
};

////////////////////////////////////////////////////////////////////////////////
//// Constants

const MAX_INT64 = 9223372036854775807;

const TOPIC_XPCOM_SHUTDOWN = "xpcom-shutdown";
const TOPIC_PREF_CHANGED = "nsPref:changed";
const TOPIC_DEBUG_START_EXPIRATION = "places-debug-start-expiration";
const TOPIC_EXPIRATION_FINISHED = "places-expiration-finished";
const TOPIC_IDLE_BEGIN = "idle";
const TOPIC_IDLE_END = "back";

// Branch for all expiration preferences.
const PREF_BRANCH = "places.history.expiration.";

// Max number of unique URIs to retain in history.
// Notice this is a lazy limit.  This means we will start to expire if we will
// go over it, but we won't ensure that we will stop exactly when we reach it,
// instead we will stop after the next expiration step that will bring us
// below it.
// If this preference does not exist or has a negative value, we will calculate
// a limit based on current hardware.
const PREF_MAX_URIS = "max_pages";
const PREF_MAX_URIS_NOTSET = -1; // Use our internally calculated limit.

// We save the current unique URIs limit to this pref, to make it available to
// other components without having to duplicate the full logic.
const PREF_READONLY_CALCULATED_MAX_URIS = "transient_current_max_pages";

// Seconds between each expiration step.
const PREF_INTERVAL_SECONDS = "interval_seconds";
const PREF_INTERVAL_SECONDS_NOTSET = 3 * 60;

// The percentage of system memory we will use for the database's cache.
// Use the same value set in nsNavHistory.cpp.  We use the size of the cache to
// evaluate how many pages we can store before going over it.
const PREF_DATABASE_CACHE_PER_MEMORY_PERCENTAGE =
  "places.history.cache_per_memory_percentage";
const PREF_DATABASE_CACHE_PER_MEMORY_PERCENTAGE_NOTSET = 6;

// Minimum number of unique URIs to retain.  This is used when system-info
// returns bogus values.
const MIN_URIS = 1000;

// Max number of entries to expire at each expiration step.
// This value is globally used for different kind of data we expire, can be
// tweaked based on data type.  See below in getBoundStatement.
const EXPIRE_LIMIT_PER_STEP = 6;
// When we run a large expiration step, the above limit is multiplied by this.
const EXPIRE_LIMIT_PER_LARGE_STEP_MULTIPLIER = 10;

// When history is clean or dirty enough we will adapt the expiration algorithm
// to be more lazy or more aggressive.
// This is done acting on the interval between expiration steps and the number
// of expirable items.
// 1. Clean history:
//   We expire at (default interval * EXPIRE_AGGRESSIVITY_MULTIPLIER) the
//   default number of entries.
// 2. Dirty history:
//   We expire at the default interval, but a greater number of entries
//   (default number of entries * EXPIRE_AGGRESSIVITY_MULTIPLIER).
const EXPIRE_AGGRESSIVITY_MULTIPLIER = 3;

// This is the average size in bytes of an URI entry in the database.
// Magic numbers are determined through analysis of the distribution of a ratio
// between number of unique URIs and database size among our users.  We use a
// more pessimistic ratio on single cores, since we handle some stuff in a
// separate thread.
// Based on these values we evaluate how many unique URIs we can handle before
// going over the database maximum cache size.  If we are over the maximum
// number of entries, we will expire.
const URIENTRY_AVG_SIZE_MIN = 2000;
const URIENTRY_AVG_SIZE_MAX = 4000;

// Seconds of idle time before starting a larger expiration step.
// Notice during idle we stop the expiration timer since we don't want to hurt
// stand-by or mobile devices batteries.
const IDLE_TIMEOUT_SECONDS = 5 * 60;

// If a clear history ran just before we shutdown, we will skip most of the
// expiration at shutdown.  This is maximum number of seconds from last
// clearHistory to decide to skip expiration at shutdown.
const SHUTDOWN_WITH_RECENT_CLEARHISTORY_TIMEOUT_SECONDS = 10;

const USECS_PER_DAY = 86400000000;
const ANNOS_EXPIRE_POLICIES = [
  { bind: "expire_days",
    type: Ci.nsIAnnotationService.EXPIRE_DAYS,
    time: 7 * USECS_PER_DAY },
  { bind: "expire_weeks",
    type: Ci.nsIAnnotationService.EXPIRE_WEEKS,
    time: 30 * USECS_PER_DAY },
  { bind: "expire_months",
    type: Ci.nsIAnnotationService.EXPIRE_MONTHS,
    time: 180 * USECS_PER_DAY },
];

// When we expire we can use these limits:
// - SMALL for usual partial expirations, will expire a small chunk.
// - LARGE for idle or shutdown expirations, will expire a large chunk.
// - UNLIMITED for clearHistory, will expire everything.
// - DEBUG will use a known limit, passed along with the debug notification.
const LIMIT = {
  SMALL: 0,
  LARGE: 1,
  UNLIMITED: 2,
  DEBUG: 3,
};

// Represents the status of history database.
const STATUS = {
  CLEAN: 0,
  DIRTY: 1,
  UNKNOWN: 2,
};

// Represents actions on which a query will run.
const ACTION = {
  TIMED: 1 << 0,
  CLEAR_HISTORY: 1 << 1,
  SHUTDOWN: 1 << 2,
  CLEAN_SHUTDOWN: 1 << 3,
  IDLE: 1 << 4,
  DEBUG: 1 << 5,
};

// The queries we use to expire.
const EXPIRATION_QUERIES = {

  // Finds visits to be expired.  Will return nothing if we are not over the
  // unique URIs limit.
  QUERY_FIND_VISITS_TO_EXPIRE: {
    sql: "SELECT IFNULL(h_t.url, h.url) AS url, v.visit_date AS visit_date, " +
                "0 AS whole_entry, :limit_visits AS expected_results " +
         "FROM moz_historyvisits_temp v " +
         "LEFT JOIN moz_places_temp AS h_t ON h_t.id = v.place_id " +
         "LEFT JOIN moz_places AS h ON h.id = v.place_id " +
         "WHERE ((SELECT count(*) FROM moz_places_temp) + " +
                "(SELECT count(*) FROM moz_places)) > :max_uris " +
         "UNION ALL " +
         "SELECT IFNULL(h_t.url, h.url) AS url, v.visit_date AS visit_date, " +
                "0 AS whole_entry, :limit_uris AS expected_results " +
         "FROM moz_historyvisits v " +
         "LEFT JOIN moz_places_temp AS h_t ON h_t.id = v.place_id " +
         "LEFT JOIN moz_places AS h ON h.id = v.place_id " +
         "WHERE ((SELECT count(*) FROM moz_places_temp) + " +
                "(SELECT count(*) FROM moz_places)) > :max_uris " +
         "ORDER BY v.visit_date ASC " +
         "LIMIT :limit_visits",
    actions: ACTION.TIMED | ACTION.SHUTDOWN | ACTION.IDLE | ACTION.DEBUG
  },

  // Removes the previously found visits.
  QUERY_EXPIRE_VISITS: {
    sql: "DELETE FROM moz_historyvisits_view WHERE id IN ( " +
           "SELECT id FROM ( " +
             "SELECT v.id, v.visit_date " +
             "FROM moz_historyvisits_temp v " +
             "UNION ALL " +
             "SELECT v.id, v.visit_date " +
             "FROM moz_historyvisits v " +
             "ORDER BY v.visit_date ASC " +
             "LIMIT :limit_visits " +
            " ) " +
          ") " +
        " AND ((SELECT count(*) FROM moz_places_temp) + " +
              "(SELECT count(*) FROM moz_places))  > :max_uris",
    actions: ACTION.TIMED | ACTION.SHUTDOWN | ACTION.IDLE | ACTION.DEBUG
  },

  // Finds orphan URIs in the database.
  // Notice we won't notify single removed URIs on removeAllPages, so we don't
  // run this query in such a case, but just delete URIs.
  QUERY_FIND_URIS_TO_EXPIRE: {
    sql: "SELECT h.url, h.last_visit_date AS visit_date, 1 AS whole_entry, " +
                ":limit_uris AS expected_results " +
         "FROM moz_places_temp h " +
         "LEFT JOIN moz_historyvisits v ON h.id = v.place_id " +
         "LEFT JOIN moz_historyvisits_temp v_t ON h.id = v_t.place_id " +
         "LEFT JOIN moz_bookmarks b ON h.id = b.fk " +
         "WHERE v.id IS NULL " +
           "AND v_t.id IS NULL " +
           "AND b.id IS NULL " +
           "AND h.id <= :last_place_id " +
           "AND SUBSTR(h.url, 1, 6) <> 'place:' " +
         "UNION ALL " +
         "SELECT h.url, h.last_visit_date AS visit_date, 1 AS whole_entry, " +
               ":limit_uris AS expected_results " +
         "FROM moz_places h " +
         "LEFT JOIN moz_historyvisits v ON h.id = v.place_id " +
         "LEFT JOIN moz_historyvisits_temp v_t ON h.id = v_t.place_id " +
         "LEFT JOIN moz_bookmarks b ON h.id = b.fk " +
         "WHERE v.id IS NULL " +
           "AND v_t.id IS NULL " +
           "AND b.id IS NULL " +
           "AND h.id <= :last_place_id " +
           "AND SUBSTR(h.url, 1, 6) <> 'place:' " +
         "LIMIT :limit_uris",
    actions: ACTION.TIMED | ACTION.SHUTDOWN | ACTION.IDLE | ACTION.DEBUG
  },

  // Expire orphan URIs from the database.
  QUERY_EXPIRE_URIS: {
    sql: "DELETE FROM moz_places_view WHERE id IN ( " +
           "SELECT h.id " +
           "FROM moz_places_temp h " +
           "LEFT JOIN moz_historyvisits v ON h.id = v.place_id " +
           "LEFT JOIN moz_historyvisits_temp v_t ON h.id = v_t.place_id " +
           "LEFT JOIN moz_bookmarks b ON h.id = b.fk " +
           "WHERE v.id IS NULL " +
             "AND v_t.id IS NULL " +
             "AND b.id IS NULL " +
             "AND h.id <= :last_place_id " +
             "AND SUBSTR(h.url, 1, 6) <> 'place:' " +
           "UNION ALL " +
           "SELECT h.id " +
           "FROM moz_places h " +
           "LEFT JOIN moz_historyvisits v ON h.id = v.place_id " +
           "LEFT JOIN moz_historyvisits_temp v_t ON h.id = v_t.place_id " +
           "LEFT JOIN moz_bookmarks b ON h.id = b.fk " +
           "WHERE v.id IS NULL " +
             "AND v_t.id IS NULL " +
             "AND b.id IS NULL " +
             "AND h.id <= :last_place_id " +
             "AND SUBSTR(h.url, 1, 6) <> 'place:' " +
           "LIMIT :limit_uris " +
         ")",
    actions: ACTION.TIMED | ACTION.CLEAR_HISTORY | ACTION.SHUTDOWN |
             ACTION.IDLE | ACTION.DEBUG
  },

  // Expire orphan icons from the database.
  QUERY_EXPIRE_FAVICONS: {
    sql: "DELETE FROM moz_favicons WHERE id IN ( " +
           "SELECT f.id FROM moz_favicons f " +
           "LEFT JOIN moz_places h ON f.id = h.favicon_id " +
           "LEFT JOIN moz_places_temp h_t ON f.id = h_t.favicon_id " +
           "WHERE h.favicon_id IS NULL " +
             "AND h_t.favicon_id IS NULL " +
           "LIMIT :limit_favicons " +
         ")",
    actions: ACTION.TIMED | ACTION.CLEAR_HISTORY | ACTION.SHUTDOWN |
             ACTION.IDLE | ACTION.DEBUG
  },

  // Expire orphan page annotations from the database.
  QUERY_EXPIRE_ANNOS: {
    sql: "DELETE FROM moz_annos WHERE id in ( " +
           "SELECT a.id FROM moz_annos a " +
           "LEFT JOIN moz_places h ON a.place_id = h.id " +
           "LEFT JOIN moz_places_temp h_t ON a.place_id = h_t.id " +
           "LEFT JOIN moz_historyvisits v ON a.place_id = v.place_id " +
           "LEFT JOIN moz_historyvisits_temp v_t ON a.place_id = v_t.place_id " +
           "WHERE (h.id IS NULL AND h_t.id IS NULL) " +
              "OR (v.id IS NULL AND v_t.id IS NULL AND a.expiration <> :expire_never) " +
           "LIMIT :limit_annos " +
         ")",
    actions: ACTION.TIMED | ACTION.CLEAR_HISTORY | ACTION.SHUTDOWN |
             ACTION.IDLE | ACTION.DEBUG
  },

  // Expire page annotations based on expiration policy.
  QUERY_EXPIRE_ANNOS_WITH_POLICY: {
    sql: "DELETE FROM moz_annos " +
         "WHERE (expiration = :expire_days " +
           "AND :expire_days_time > MAX(lastModified, dateAdded)) " +
            "OR (expiration = :expire_weeks " +
           "AND :expire_weeks_time > MAX(lastModified, dateAdded)) " +
            "OR (expiration = :expire_months " +
           "AND :expire_months_time > MAX(lastModified, dateAdded))",
    actions: ACTION.TIMED | ACTION.CLEAR_HISTORY | ACTION.SHUTDOWN |
             ACTION.IDLE | ACTION.DEBUG
  },

  // Expire items annotations based on expiration policy.
  QUERY_EXPIRE_ITEMS_ANNOS_WITH_POLICY: {
    sql: "DELETE FROM moz_items_annos " +
         "WHERE (expiration = :expire_days " +
           "AND :expire_days_time > MAX(lastModified, dateAdded)) " +
            "OR (expiration = :expire_weeks " +
           "AND :expire_weeks_time > MAX(lastModified, dateAdded)) " +
            "OR (expiration = :expire_months " +
           "AND :expire_months_time > MAX(lastModified, dateAdded))",
    actions: ACTION.TIMED | ACTION.CLEAR_HISTORY | ACTION.SHUTDOWN |
             ACTION.IDLE | ACTION.DEBUG
  },

  // Expire page annotations based on expiration policy.
  QUERY_EXPIRE_ANNOS_WITH_HISTORY: {
    sql: "DELETE FROM moz_annos " +
         "WHERE expiration = :expire_with_history " +
           "AND NOT EXISTS (SELECT id FROM moz_historyvisits_temp " +
                           "WHERE place_id = moz_annos.place_id LIMIT 1) " +
           "AND NOT EXISTS (SELECT id FROM moz_historyvisits " +
                           "WHERE place_id = moz_annos.place_id LIMIT 1)",
    actions: ACTION.TIMED | ACTION.CLEAR_HISTORY | ACTION.SHUTDOWN |
             ACTION.IDLE | ACTION.DEBUG
  },

  // Expire item annos without a corresponding item id.
  QUERY_EXPIRE_ITEMS_ANNOS: {
    sql: "DELETE FROM moz_items_annos WHERE id IN ( " +
           "SELECT a.id FROM moz_items_annos a " +
           "LEFT JOIN moz_bookmarks b ON a.item_id = b.id " +
           "WHERE b.id IS NULL " +
           "LIMIT :limit_annos " +
         ")",
    actions: ACTION.TIMED | ACTION.CLEAR_HISTORY | ACTION.SHUTDOWN |
             ACTION.IDLE | ACTION.DEBUG
  },

  // Expire all annotation names without a corresponding annotation.
  QUERY_EXPIRE_ANNO_ATTRIBUTES: {
    sql: "DELETE FROM moz_anno_attributes WHERE id IN ( " +
           "SELECT n.id FROM moz_anno_attributes n " +
           "LEFT JOIN moz_annos a ON n.id = a.anno_attribute_id " +
           "LEFT JOIN moz_items_annos t ON n.id = t.anno_attribute_id " +
           "WHERE a.anno_attribute_id IS NULL " +
             "AND t.anno_attribute_id IS NULL " +
           "LIMIT :limit_annos" +
         ")",
    actions: ACTION.TIMED | ACTION.CLEAR_HISTORY | ACTION.SHUTDOWN |
             ACTION.IDLE | ACTION.DEBUG
  },

  // Expire orphan inputhistory.
  QUERY_EXPIRE_INPUTHISTORY: {
    sql: "DELETE FROM moz_inputhistory WHERE place_id IN ( " +
           "SELECT i.place_id FROM moz_inputhistory i " +
           "LEFT JOIN moz_places h ON h.id = i.place_id " +
           "LEFT JOIN moz_places_temp h_t ON h_t.id = i.place_id " +
           "WHERE h.id IS NULL " +
             "AND h_t.id IS NULL " +
           "LIMIT :limit_inputhistory " +
         ")",
    actions: ACTION.TIMED | ACTION.CLEAR_HISTORY | ACTION.SHUTDOWN |
             ACTION.IDLE | ACTION.DEBUG
  },

  // Expire all session annotations.  Should only be called at shutdown.
  QUERY_EXPIRE_ANNOS_SESSION: {
    sql: "DELETE FROM moz_annos WHERE expiration = :expire_session",
    actions: ACTION.CLEAR_HISTORY | ACTION.SHUTDOWN | ACTION.CLEAN_SHUTDOWN |
             ACTION.DEBUG
  },

  // Expire all session item annotations.  Should only be called at shutdown.
  QUERY_EXPIRE_ITEMS_ANNOS_SESSION: {
    sql: "DELETE FROM moz_items_annos WHERE expiration = :expire_session",
    actions: ACTION.CLEAR_HISTORY | ACTION.SHUTDOWN | ACTION.CLEAN_SHUTDOWN |
             ACTION.DEBUG
  },

};

////////////////////////////////////////////////////////////////////////////////
//// nsPlacesExpiration definition

function nsPlacesExpiration()
{
  //////////////////////////////////////////////////////////////////////////////
  //// Smart Getters

  XPCOMUtils.defineLazyGetter(this, "_db", function() {
    return Cc["@mozilla.org/browser/nav-history-service;1"].
           getService(Ci.nsPIPlacesDatabase).
           DBConnection;
  });
  XPCOMUtils.defineLazyServiceGetter(this, "_ios",
                                     "@mozilla.org/network/io-service;1",
                                     "nsIIOService");
  XPCOMUtils.defineLazyServiceGetter(this, "_hsn",
                                     "@mozilla.org/browser/nav-history-service;1",
                                     "nsPIPlacesHistoryListenersNotifier");
  XPCOMUtils.defineLazyServiceGetter(this, "_sys",
                                     "@mozilla.org/system-info;1",
                                     "nsIPropertyBag2");
  XPCOMUtils.defineLazyServiceGetter(this, "_idle",
                                     "@mozilla.org/widget/idleservice;1",
                                     "nsIIdleService");

  this._prefBranch = Cc["@mozilla.org/preferences-service;1"].
                     getService(Ci.nsIPrefService).
                     getBranch(PREF_BRANCH).
                     QueryInterface(Ci.nsIPrefBranch2);
  this._loadPrefs();

  // Observe our preferences branch for changes.
  this._prefBranch.addObserver("", this, false);

  // Register topic observers.
  this._os = Cc["@mozilla.org/observer-service;1"].
             getService(Ci.nsIObserverService);
  this._os.addObserver(this, TOPIC_XPCOM_SHUTDOWN, false);
  this._os.addObserver(this, TOPIC_DEBUG_START_EXPIRATION, false);

  // Create our expiration timer.
  this._newTimer();
}

nsPlacesExpiration.prototype = {

  //////////////////////////////////////////////////////////////////////////////
  //// nsIObserver

  observe: function PEX_observe(aSubject, aTopic, aData)
  {
    if (aTopic == TOPIC_XPCOM_SHUTDOWN) {
      this._shuttingDown = true;
      this._os.removeObserver(this, TOPIC_XPCOM_SHUTDOWN);
      this._os.removeObserver(this, TOPIC_DEBUG_START_EXPIRATION);

      this._prefBranch.removeObserver("", this);

      if (this._isIdleObserver)
        this._idle.removeIdleObserver(this, IDLE_TIMEOUT_SECONDS);

      if (this._timer) {
        this._timer.cancel();
        this._timer = null;
      }

      // If we ran a clearHistory recently, we don't want to spend time
      // expiring on shutdown, we can just expire session annotations.
      let hasRecentClearHistory =
        Date.now() - this._lastClearHistoryTime <
          SHUTDOWN_WITH_RECENT_CLEARHISTORY_TIMEOUT_SECONDS * 1000;
      let action = hasRecentClearHistory ? ACTION.CLEAN_SHUTDOWN
                                         : ACTION.SHUTDOWN;
      this._expireWithActionAndLimit(action, LIMIT.LARGE);

      this._finalizeInternalStatements();
    }
    else if (aTopic == TOPIC_PREF_CHANGED) {
      this._loadPrefs();

      if (aData == PREF_INTERVAL_SECONDS) {
        // Renew the timer with the new interval value.
        this._newTimer();
      }
    }
    else if (aTopic == TOPIC_DEBUG_START_EXPIRATION) {
      this._debugLimit = aData;
      this._expireWithActionAndLimit(ACTION.DEBUG, LIMIT.DEBUG);
    }
    else if (aTopic == TOPIC_IDLE_BEGIN) {
      // Stop the expiration timer.
      if (this._timer) {
        this._timer.cancel();
        this._timer = null;
      }
      this._expireWithActionAndLimit(ACTION.IDLE, LIMIT.LARGE);
    }
    else if (aTopic == TOPIC_IDLE_END) {
      // Restart the expiration timer.
      if (!this._timer)
        this._newTimer();
    }
  },

  //////////////////////////////////////////////////////////////////////////////
  //// nsINavHistoryObserver

  _inBatchMode: false,
  onBeginUpdateBatch: function PEX_onBeginUpdateBatch()
  {
    this._inBatchMode = true;

    // We do not want to expire while we are doing batch work.
    if (this._timer) {
      this._timer.cancel();
      this._timer = null;
    }
  },

  onEndUpdateBatch: function PEX_onEndUpdateBatch()
  {
    this._inBatchMode = false;

    // Restore timer.
    if (!this._timer)
      this._newTimer();
  },

  _lastClearHistoryTime: 0,
  onClearHistory: function PEX_onClearHistory() {
    this._lastClearHistoryTime = Date.now();
    // Expire orphans.
    this._expireWithActionAndLimit(ACTION.CLEAR_HISTORY, LIMIT.UNLIMITED);
  },

  onVisit: function() {},
  onTitleChanged: function() {},
  onBeforeDeleteURI: function() {},
  onDeleteURI: function() {},
  onPageChanged: function() {},
  onDeleteVisits: function() {},

  //////////////////////////////////////////////////////////////////////////////
  //// nsITimerCallback

  notify: function PEX_timerCallback()
  {
    // We don't start observing idle in the constructor because unit tests
    // will run with a large idle, and notify it immediately, instead we start
    // observing idle at the first timer notification.
    if (!this._isIdleObserver) {
      this._idle.addIdleObserver(this, IDLE_TIMEOUT_SECONDS);
      this._isIdleObserver = true;
    }

    this._expireWithActionAndLimit(ACTION.TIMED, LIMIT.SMALL);
  },

  //////////////////////////////////////////////////////////////////////////////
  //// mozIStorageStatementCallback

  handleResult: function PEX_handleResult(aResultSet)
  {
    if (!("_expiredResults" in this))
      this._expiredResults = {};

    let row;
    while (row = aResultSet.getNextRow()) {

      this._expectedResultsCount = row.getResultByName("expected_results");

      let url = row.getResultByName("url");
      let wholeEntry = row.getResultByName("whole_entry");
      let visitDate = row.getResultByName("visit_date");

      if (url in this._expiredResults) {
        this._expiredResults[url].wholeEntry |= wholeEntry;
        this._expiredResults[url].visitDate =
          Math.max(this._expiredResults[url].visitDate, visitDate);
      }
      else {
        this._expiredResults[url] = {
          visitDate: visitDate,
          wholeEntry: wholeEntry,
        };
      }
    }
  },

  handleError: function PEX_handleError(aError)
  {
    Components.utils.reportError("Async statement execution returned with '" +
                                 aError.result + "', '" + aError.message + "'");
  },

  handleCompletion: function PEX_handleCompletion(aReason)
  {
    if (aReason == Ci.mozIStorageStatementCallback.REASON_FINISHED) {
      // Adapt the aggressivity of partial steps based on the status of history.
      // A dirty history will return all the entries we are expecting, while a
      // clean one will return less results or no results at all.
      this._status = STATUS.UNKNOWN;
      if ("_expectedResultsCount" in this && "_expiredResults" in this) {
        let isClean = this._expiredResults.length < this._expectedResultsCount;
        let status = isClean ? STATUS.CLEAN : STATUS.DIRTY;
        // If status changes we should restart the timer.
        if (this._status != status) {
          this._newTimer();
          this._status = status;
        }
      }
      delete this._expectedResultsCount;

      // Dispatch to history that we've finished expiring if we have results.
      // We will notify just once per url
      if ("_expiredResults" in this) {
        if (!this._shuttingDown) {
          // We don't want to notify after shutdown.
          for (let expiredUrl in this._expiredResults) {
            let entry = this._expiredResults[expiredUrl];
            this._hsn.notifyOnPageExpired(
              this._ios.newURI(expiredUrl, null, null),
              entry.visitDate,
              entry.wholeEntry);
          }
        }
        delete this._expiredResults;
      }

      // Dispatch a notification that expiration has finished.
      this._os.notifyObservers(null, TOPIC_EXPIRATION_FINISHED, null);
    }
  },

  //////////////////////////////////////////////////////////////////////////////
  //// nsPlacesExpiration

  _urisLimit: PREF_MAX_URIS_NOTSET,
  _interval: PREF_INTERVAL_SECONDS_NOTSET,
  _status: STATUS.UNKNOWN,
  _isIdleObserver: false,
  _shuttingDown: false,

  _loadPrefs: function PEX__loadPrefs() {
    // Get the user's limit, if it was set.
    try {
      // We want to silently fail since getIntPref throws if it does not exist,
      // and use a default to fallback to.
      this._urisLimit = this._prefBranch.getIntPref(PREF_MAX_URIS);
    }
    catch(e) {}

    if (this._urisLimit < 0) {
      // The preference did not exist or has a negative value, so we calculate a
      // limit based on hardware.
      let memsize = this._sys.getProperty("memsize"); // Memory size in bytes.
      let cpucount = this._sys.getProperty("cpucount"); // CPU count.
      const AVG_SIZE_PER_URIENTRY = cpucount > 1 ? URIENTRY_AVG_SIZE_MIN
                                                 : URIENTRY_AVG_SIZE_MAX;
      // We will try to live inside the database cache size, since working out
      // of it can be really slow.
      let cache_percentage = PREF_DATABASE_CACHE_PER_MEMORY_PERCENTAGE_NOTSET;
      try {
        let prefs = Cc["@mozilla.org/preferences-service;1"].
                    getService(Ci.nsIPrefBranch);
        cache_percentage =
          prefs.getIntPref(PREF_DATABASE_CACHE_PER_MEMORY_PERCENTAGE);
        if (cache_percentage < 0) {
          cache_percentage = 0;
        }
        else if (cache_percentage > 50) {
          cache_percentage = 50;
        }
      }
      catch(e) {}
      let cachesize = memsize * cache_percentage / 100;
      this._urisLimit = Math.max(MIN_URIS,
                                 parseInt(cachesize / AVG_SIZE_PER_URIENTRY));
    }
    // Expose the calculated limit to other components.
    this._prefBranch.setIntPref(PREF_READONLY_CALCULATED_MAX_URIS,
                                this._urisLimit);

    // Get the expiration interval value.
    try {
      // We want to silently fail since getIntPref throws if it does not exist,
      // and use a default to fallback to.
      this._interval = this._prefBranch.getIntPref(PREF_INTERVAL_SECONDS);
    }
    catch (e) {}
    if (this._interval <= 0)
      this._interval = PREF_INTERVAL_SECONDS_NOTSET;
  },

  /**
   * Execute async statements to expire with the specified queries.
   *
   * @param aQueryNames
   *        The names of the queries to use for this expiration step.
   */
  _expireWithActionAndLimit:
  function PEX__expireWithActionAndLimit(aAction, aLimit)
  {
    // Skip expiration during batch mode.
    if (this._inBatchMode)
      return;

    let boundStatements = [];
    for (let queryType in EXPIRATION_QUERIES) {
      if (EXPIRATION_QUERIES[queryType].actions & aAction)
        boundStatements.push(this._getBoundStatement(queryType, aLimit));
    }

    // Execute statements asynchronously in a transaction.
    this._db.executeAsync(boundStatements, boundStatements.length, this);
  },

  /**
   * Finalizes all of our mozIStorageStatements so we can properly close the
   * database.
   */
  _finalizeInternalStatements: function PEX__finalizeInternalStatements()
  {
    for each (let stmt in this._cachedStatements) {
      stmt.finalize();
    }
  },

  /**
   * Generate the statement used for expiration.
   *
   * @param aQueryType
   *        Type of the query to build statement for.
   * @param aLimit
   *        Whether to use small, large or no limits when expiring.  See the
   *        LIMIT const for values.
   */
  _cachedStatements: {},
  _getBoundStatement: function PEX__getBoundStatement(aQueryType, aLimit)
  {
    // Statements creation can be expensive, so we want to cache them.
    let stmt = this._cachedStatements[aQueryType];
    if (stmt === undefined) {
      stmt = this._cachedStatements[aQueryType] =
        this._db.createStatement(EXPIRATION_QUERIES[aQueryType].sql);
    }

    let baseLimit;
    switch (aLimit) {
      case LIMIT.UNLIMITED:
        baseLimit = -1;
        break;
      case LIMIT.SMALL:
        baseLimit = EXPIRE_LIMIT_PER_STEP;
        break;
      case LIMIT.LARGE:
        baseLimit = EXPIRE_LIMIT_PER_STEP * EXPIRE_LIMIT_PER_LARGE_STEP_MULTIPLIER;
        break;
      case LIMIT.DEBUG:
        baseLimit = this._debugLimit;
        break;
    }
    if (this._status == STATUS.DIRTY && aLimit != LIMIT.DEBUG)
      baseLimit *= EXPIRE_AGGRESSIVITY_MULTIPLIER;

    // Bind the appropriate parameters.
    let params = stmt.params;
    switch (aQueryType) {
      case "QUERY_FIND_VISITS_TO_EXPIRE":
      case "QUERY_EXPIRE_VISITS":
        params.max_uris = this._urisLimit;
        params.limit_visits = baseLimit;
        break;
      case "QUERY_FIND_URIS_TO_EXPIRE":
      case "QUERY_EXPIRE_URIS":
        // We could run in the middle of an addVisit for a new page.  In such a
        // case since we are async and addVisit is sync, we could end up
        // expiring the page before it actually has any visit.
        // This is a temporary workaround till addVisit will be async, we don't
        // want to expire new pages added after this runs.
        let max_place_id = MAX_INT64;
        let maxPlaceIdStmt = this._db.createStatement(
          "SELECT MAX(IFNULL((SELECT MAX(id) FROM moz_places_temp), 0), " +
                     "IFNULL((SELECT MAX(id) FROM moz_places), 0) " +
                    ") AS max_place_id");
        try {
          maxPlaceIdStmt.executeStep();
          max_place_id = maxPlaceIdStmt.getInt64(0);
        }
        catch(e) {}
        finally {
          maxPlaceIdStmt.finalize();
        }

        params.limit_uris = baseLimit;
        params.last_place_id = max_place_id;
        break;
      case "QUERY_EXPIRE_FAVICONS":
        params.limit_favicons = baseLimit;
        break;
      case "QUERY_EXPIRE_ANNOS":
        params.expire_never = Ci.nsIAnnotationService.EXPIRE_NEVER;
        params.limit_annos = baseLimit;
        break;
      case "QUERY_EXPIRE_ANNOS_WITH_POLICY":
      case "QUERY_EXPIRE_ITEMS_ANNOS_WITH_POLICY":
        let microNow = Date.now() * 1000;
        ANNOS_EXPIRE_POLICIES.forEach(function(policy) {
          params[policy.bind] = policy.type;
          params[policy.bind + "_time"] = microNow - policy.time;
        });
        break;
      case "QUERY_EXPIRE_ANNOS_WITH_HISTORY":
        params.expire_with_history = Ci.nsIAnnotationService.EXPIRE_WITH_HISTORY;
        break;
      case "QUERY_EXPIRE_ITEMS_ANNOS":
        params.limit_annos = baseLimit;
        break;
      case "QUERY_EXPIRE_ANNO_ATTRIBUTES":
        params.limit_annos = baseLimit;
        break;
      case "QUERY_EXPIRE_INPUTHISTORY":
        params.limit_inputhistory = baseLimit;
        break;
      case "QUERY_EXPIRE_ANNOS_SESSION":
      case "QUERY_EXPIRE_ITEMS_ANNOS_SESSION":
        params.expire_session = Ci.nsIAnnotationService.EXPIRE_SESSION;
        break;
    }

    return stmt;
  },

  /**
   * Creates a new timer based on this._syncInterval.
   *
   * @return a REPEATING_SLACK nsITimer that runs every this._syncInterval.
   */
  _newTimer: function PEX__newTimer()
  {
    if (this._timer)
      this._timer.cancel();
    let interval = (this._status == STATUS.CLEAN) ?
      this._interval * EXPIRE_AGGRESSIVITY_MULTIPLIER : this._interval;

    let timer = Cc["@mozilla.org/timer;1"].createInstance(Ci.nsITimer);
    timer.initWithCallback(this, interval * 1000,
                           Ci.nsITimer.TYPE_REPEATING_SLACK);
    return this._timer = timer;
  },

  //////////////////////////////////////////////////////////////////////////////
  //// nsISupports

  classDescription: "Used to expire obsolete data from Places",
  classID: Components.ID("705a423f-2f69-42f3-b9fe-1517e0dee56f"),
  contractID: "@mozilla.org/places/expiration;1",

  // Registering in these categories makes us get initialized when either of
  // those listeners would be notified.
  _xpcom_categories: [
    { category: "history-observers" },
  ],

  _xpcom_factory: nsPlacesExpirationFactory,

  QueryInterface: XPCOMUtils.generateQI([
    Ci.nsIObserver,
    Ci.nsINavHistoryObserver,
    Ci.nsITimerCallback,
    Ci.mozIStorageStatementCallback,
  ])
};

////////////////////////////////////////////////////////////////////////////////
//// Module Registration

let components = [nsPlacesExpiration];
function NSGetModule(compMgr, fileSpec)
{
  return XPCOMUtils.generateModule(components);
}
