////////////////////////////////////////////////////////////////////////////////
// Interfaces

const nsIAccessibleRetrieval = Components.interfaces.nsIAccessibleRetrieval;

const nsIAccessibleEvent = Components.interfaces.nsIAccessibleEvent;
const nsIAccessibleStateChangeEvent =
  Components.interfaces.nsIAccessibleStateChangeEvent;
const nsIAccessibleCaretMoveEvent =
  Components.interfaces.nsIAccessibleCaretMoveEvent;

const nsIAccessibleStates = Components.interfaces.nsIAccessibleStates;
const nsIAccessibleRole = Components.interfaces.nsIAccessibleRole;
const nsIAccessibleTypes = Components.interfaces.nsIAccessibleTypes;

const nsIAccessibleRelation = Components.interfaces.nsIAccessibleRelation;

const nsIAccessNode = Components.interfaces.nsIAccessNode;
const nsIAccessible = Components.interfaces.nsIAccessible;

const nsIAccessibleCoordinateType =
      Components.interfaces.nsIAccessibleCoordinateType;

const nsIAccessibleDocument = Components.interfaces.nsIAccessibleDocument;

const nsIAccessibleText = Components.interfaces.nsIAccessibleText;
const nsIAccessibleEditableText = Components.interfaces.nsIAccessibleEditableText;

const nsIAccessibleHyperLink = Components.interfaces.nsIAccessibleHyperLink;
const nsIAccessibleHyperText = Components.interfaces.nsIAccessibleHyperText;

const nsIAccessibleImage = Components.interfaces.nsIAccessibleImage;
const nsIAccessibleSelectable = Components.interfaces.nsIAccessibleSelectable;
const nsIAccessibleTable = Components.interfaces.nsIAccessibleTable;
const nsIAccessibleTableCell = Components.interfaces.nsIAccessibleTableCell;
const nsIAccessibleValue = Components.interfaces.nsIAccessibleValue;

const nsIObserverService = Components.interfaces.nsIObserverService;

const nsIDOMDocument = Components.interfaces.nsIDOMDocument;
const nsIDOMEvent = Components.interfaces.nsIDOMEvent;
const nsIDOMHTMLDocument = Components.interfaces.nsIDOMHTMLDocument;
const nsIDOMNode = Components.interfaces.nsIDOMNode;
const nsIDOMNSHTMLElement = Components.interfaces.nsIDOMNSHTMLElement;
const nsIDOMWindow = Components.interfaces.nsIDOMWindow;
const nsIDOMXULElement = Components.interfaces.nsIDOMXULElement;

const nsIPropertyElement = Components.interfaces.nsIPropertyElement;

////////////////////////////////////////////////////////////////////////////////
// States

const STATE_BUSY = nsIAccessibleStates.STATE_BUSY;
const STATE_CHECKED = nsIAccessibleStates.STATE_CHECKED;
const STATE_CHECKABLE = nsIAccessibleStates.STATE_CHECKABLE;
const STATE_COLLAPSED = nsIAccessibleStates.STATE_COLLAPSED;
const STATE_EXPANDED = nsIAccessibleStates.STATE_EXPANDED;
const STATE_EXTSELECTABLE = nsIAccessibleStates.STATE_EXTSELECTABLE;
const STATE_FOCUSABLE = nsIAccessibleStates.STATE_FOCUSABLE;
const STATE_FOCUSED = nsIAccessibleStates.STATE_FOCUSED;
const STATE_HASPOPUP = nsIAccessibleStates.STATE_HASPOPUP;
const STATE_INVALID = nsIAccessibleStates.STATE_INVALID;
const STATE_LINKED = nsIAccessibleStates.STATE_LINKED;
const STATE_MIXED = nsIAccessibleStates.STATE_MIXED;
const STATE_MULTISELECTABLE = nsIAccessibleStates.STATE_MULTISELECTABLE;
const STATE_OFFSCREEN = nsIAccessibleStates.STATE_OFFSCREEN;
const STATE_PRESSED = nsIAccessibleStates.STATE_PRESSED;
const STATE_READONLY = nsIAccessibleStates.STATE_READONLY;
const STATE_REQUIRED = nsIAccessibleStates.STATE_REQUIRED;
const STATE_SELECTABLE = nsIAccessibleStates.STATE_SELECTABLE;
const STATE_SELECTED = nsIAccessibleStates.STATE_SELECTED;
const STATE_TRAVERSED = nsIAccessibleStates.STATE_TRAVERSED;
const STATE_UNAVAILABLE = nsIAccessibleStates.STATE_UNAVAILABLE;

const EXT_STATE_EDITABLE = nsIAccessibleStates.EXT_STATE_EDITABLE;
const EXT_STATE_EXPANDABLE = nsIAccessibleStates.EXT_STATE_EXPANDABLE;
const EXT_STATE_HORIZONTAL = nsIAccessibleStates.EXT_STATE_HORIZONTAL;
const EXT_STATE_MULTI_LINE = nsIAccessibleStates.EXT_STATE_MULTI_LINE;
const EXT_STATE_SINGLE_LINE = nsIAccessibleStates.EXT_STATE_SINGLE_LINE;
const EXT_STATE_SUPPORTS_AUTOCOMPLETION = 
      nsIAccessibleStates.EXT_STATE_SUPPORTS_AUTOCOMPLETION;
const EXT_STATE_VERTICAL = nsIAccessibleStates.EXT_STATE_VERTICAL;

////////////////////////////////////////////////////////////////////////////////
// OS detect
const MAC = (navigator.platform.indexOf("Mac") != -1)? true : false;
const LINUX = (navigator.platform.indexOf("Linux") != -1)? true : false;
const SOLARIS = (navigator.platform.indexOf("SunOS") != -1)? true : false;
const WIN = (navigator.platform.indexOf("Win") != -1)? true : false;

////////////////////////////////////////////////////////////////////////////////
// Accessible general

/**
 * nsIAccessibleRetrieval, initialized when test is loaded.
 */
var gAccRetrieval = null;

/**
 * Invokes the given function when document is loaded and focused. Preferable
 * to mochitests 'addLoadEvent' function -- additionally ensures state of the
 * document accessible is not busy.
 *
 * @param aFunc  the function to invoke
 */
function addA11yLoadEvent(aFunc)
{
  function waitForDocLoad()
  {
    window.setTimeout(
      function()
      {
        var accDoc = getAccessible(document);
        var state = {};
        accDoc.getState(state, {});
        if (state.value & STATE_BUSY)
          return waitForDocLoad();

        window.setTimeout(aFunc, 150);
      },
      0
    );
  }

  SimpleTest.waitForFocus(waitForDocLoad);
}

////////////////////////////////////////////////////////////////////////////////
// Helpers for getting DOM node/accessible

/**
 * Return the DOM node by identifier (may be accessible, DOM node or ID).
 */
function getNode(aAccOrNodeOrID)
{
  if (!aAccOrNodeOrID)
    return null;

  if (aAccOrNodeOrID instanceof nsIDOMNode)
    return aAccOrNodeOrID;

  if (aAccOrNodeOrID instanceof nsIAccessible) {
    aAccOrNodeOrID.QueryInterface(nsIAccessNode);
    return aAccOrNodeOrID.DOMNode;
  }

  node = document.getElementById(aAccOrNodeOrID);
  if (!node) {
    ok(false, "Can't get DOM element for " + aAccOrNodeOrID);
    return null;
  }

  return node;
}

/**
 * Constants indicates getAccessible doesn't fail if there is no accessible.
 */
const DONOTFAIL_IF_NO_ACC = 1;

/**
 * Constants indicates getAccessible won't fail if accessible doesn't implement
 * the requested interfaces.
 */
const DONOTFAIL_IF_NO_INTERFACE = 2;

/**
 * Return accessible for the given identifier (may be ID attribute or DOM
 * element or accessible object) or null.
 *
 * @param aAccOrElmOrID      [in] identifier to get an accessible implementing
 *                           the given interfaces
 * @param aInterfaces        [in, optional] the interface or an array interfaces
 *                           to query it/them from obtained accessible
 * @param aElmObj            [out, optional] object to store DOM element which
 *                           accessible is obtained for
 * @param aDoNotFailIf       [in, optional] no error for special cases (see
 *                            constants above)
 */
function getAccessible(aAccOrElmOrID, aInterfaces, aElmObj, aDoNotFailIf)
{
  if (!aAccOrElmOrID)
    return null;

  var elm = null;

  if (aAccOrElmOrID instanceof nsIAccessible) {
    aAccOrElmOrID.QueryInterface(nsIAccessNode);
    elm = aAccOrElmOrID.DOMNode;

  } else if (aAccOrElmOrID instanceof nsIDOMNode) {
    elm = aAccOrElmOrID;

  } else {
    var elm = document.getElementById(aAccOrElmOrID);
    if (!elm) {
      ok(false, "Can't get DOM element for " + aAccOrElmOrID);
      return null;
    }
  }

  if (aElmObj && (typeof aElmObj == "object"))
    aElmObj.value = elm;

  var acc = (aAccOrElmOrID instanceof nsIAccessible) ? aAccOrElmOrID : null;
  if (!acc) {
    try {
      acc = gAccRetrieval.getAccessibleFor(elm);
    } catch (e) {
    }

    if (!acc) {
      if (!(aDoNotFailIf & DONOTFAIL_IF_NO_ACC))
        ok(false, "Can't get accessible for " + aAccOrElmOrID);

      return null;
    }
  }

  if (!aInterfaces)
    return acc;

  if (aInterfaces instanceof Array) {
    for (var index = 0; index < aInterfaces.length; index++) {
      try {
        acc.QueryInterface(aInterfaces[index]);
      } catch (e) {
        if (!(aDoNotFailIf & DONOTFAIL_IF_NO_INTERFACE))
          ok(false, "Can't query " + aInterfaces[index] + " for " + aAccOrElmOrID);

        return null;
      }
    }
    return acc;
  }
  
  try {
    acc.QueryInterface(aInterfaces);
  } catch (e) {
    ok(false, "Can't query " + aInterfaces + " for " + aAccOrElmOrID);
    return null;
  }
  
  return acc;
}

/**
 * Return true if the given identifier has an accessible, or exposes the wanted
 * interfaces.
 */
function isAccessible(aAccOrElmOrID, aInterfaces)
{
  return getAccessible(aAccOrElmOrID, aInterfaces, null,
                       DONOTFAIL_IF_NO_ACC | DONOTFAIL_IF_NO_INTERFACE) ?
    true : false;
}

/**
 * Return root accessible for the given identifier.
 */
function getRootAccessible(aAccOrElmOrID)
{
  var acc = getAccessible(aAccOrElmOrID ? aAccOrElmOrID : document);
  while (acc) {
    var parent = acc.parent;
    if (parent && !parent.parent)
      return acc;

    acc = parent;
  }

  return null;
}

/**
 * Return application accessible.
 */
function getApplicationAccessible()
{
  var acc = getAccessible(document), parent = null;
  while (acc) {

    try {
      parent = acc.parent;
    } catch (e) {
      ok(false, "Can't get a parent for " + prettyName(acc));
      return null;
    }

    if (!parent) {
      if (acc.role == ROLE_APP_ROOT)
        return acc;

      ok(false, "No application accessible!");
      return null;
    }

    acc = parent;
  }

  return null;
}

/**
 * Run through accessible tree of the given identifier so that we ensure
 * accessible tree is created.
 */
function ensureAccessibleTree(aAccOrElmOrID)
{
  var acc = getAccessible(aAccOrElmOrID);
  if (!acc)
    return;

  var child = acc.firstChild;
  while (child) {
    ensureAccessibleTree(child);
    try {
      child = child.nextSibling;
    } catch (e) {
      child = null;
    }
  }
}

/**
 * Compare expected and actual accessibles trees.
 *
 * @param  aAccOrElmOrID  [in] accessible identifier
 * @param  aAccTree       [in] JS object, each field corresponds to property of
 *                         accessible object. Additionally special properties
 *                         are presented:
 *                          children - an array of JS objects representing
 *                                      children of accessible
 *                          states   - an object having states and extraStates
 *                                      fields
 */
function testAccessibleTree(aAccOrElmOrID, aAccTree)
{
  var acc = getAccessible(aAccOrElmOrID);
  if (!acc)
    return;

  for (var prop in aAccTree) {
    var msg = "Wrong value of property '" + prop + "' for " + prettyName(acc) + ".";
    if (prop == "role") {
      is(roleToString(acc[prop]), roleToString(aAccTree[prop]), msg);

    } else if (prop == "states") {
      var statesObj = aAccTree[prop];
      testStates(acc, statesObj.states, statesObj.extraStates,
                 statesObj.absentStates, statesObj.absentExtraStates);

    } else if (prop != "children") {
      is(acc[prop], aAccTree[prop], msg);
    }
  }

  if ("children" in aAccTree && aAccTree["children"] instanceof Array) {
    var children = acc.children;
    is(children.length, aAccTree.children.length,
       "Different amount of expected children of " + prettyName(acc) + ".");

    if (aAccTree.children.length == children.length) {
      var childCount = children.length;

      // nsIAccessible::firstChild
      var expectedFirstChild = childCount > 0 ?
        children.queryElementAt(0, nsIAccessible) : null;
      var firstChild = null;
      try { firstChild = acc.firstChild; } catch (e) {}
      is(firstChild, expectedFirstChild,
         "Wrong first child of " + prettyName(acc));

      // nsIAccessible::lastChild
      var expectedLastChild = childCount > 0 ?
        children.queryElementAt(childCount - 1, nsIAccessible) : null;
      var lastChild = null;
      try { lastChild = acc.lastChild; } catch (e) {}
      is(lastChild, expectedLastChild,
         "Wrong last child of " + prettyName(acc));

      for (var i = 0; i < children.length; i++) {
        var child = children.queryElementAt(i, nsIAccessible);

        // nsIAccessible::parent
        var parent = null;
        try { parent = child.parent; } catch (e) {}
        is(parent, acc, "Wrong parent of " + prettyName(child));

        // nsIAccessible::indexInParent
        var indexInParent = -1;
        try { indexInParent = child.indexInParent; } catch(e) {}
        is(indexInParent, i,
           "Wrong index in parent of " + prettyName(child));

        // nsIAccessible::nextSibling
        var expectedNextSibling = (i < childCount - 1) ?
          children.queryElementAt(i + 1, nsIAccessible) : null;
        var nextSibling = null;
        try { nextSibling = child.nextSibling; } catch (e) {}
        is(nextSibling, expectedNextSibling,
           "Wrong next sibling of " + prettyName(child));

        // nsIAccessible::previousSibling
        var expectedPrevSibling = (i > 0) ?
          children.queryElementAt(i - 1, nsIAccessible) : null;
        var prevSibling = null;
        try { prevSibling = child.previousSibling; } catch (e) {}
        is(prevSibling, expectedPrevSibling,
           "Wrong previous sibling of " + prettyName(child));

        // Go down through subtree
        testAccessibleTree(child, aAccTree.children[i]);
      }
    }
  }
}

/**
 * Test accessible tree for defunct accessible.
 *
 * @param  aAcc       [in] the defunct accessible
 * @param  aNodeOrId  [in] the DOM node identifier for the defunct accessible
 */
function testDefunctAccessible(aAcc, aNodeOrId)
{
  if (aNodeOrId)
    ok(!isAccessible(aNodeOrId),
       "Accessible for " + aNodeOrId + " wasn't properly shut down!");

  var msg = " doesn't fail for shut down accessible " + prettyName(aNodeOrId) + "!";

  // firstChild
  var success = false;
  try {
    aAcc.firstChild;
  } catch (e) {
    success = (e.result == Components.results.NS_ERROR_FAILURE)
  }
  ok(success, "firstChild" + msg);

  // lastChild
  success = false;
  try {
    aAcc.lastChild;
  } catch (e) {
    success = (e.result == Components.results.NS_ERROR_FAILURE)
  }
  ok(success, "lastChild" + msg);

  // childCount
  success = false;
  try {
    aAcc.childCount;
  } catch (e) {
    success = (e.result == Components.results.NS_ERROR_FAILURE)
  }
  ok(success, "childCount" + msg);

  // children
  success = false;
  try {
    aAcc.children;
  } catch (e) {
    success = (e.result == Components.results.NS_ERROR_FAILURE)
  }
  ok(success, "children" + msg);

  // nextSibling
  success = false;
  try {
    aAcc.nextSibling;
  } catch (e) {
    success = (e.result == Components.results.NS_ERROR_FAILURE);
  }
  ok(success, "nextSibling" + msg);

  // previousSibling
  success = false;
  try {
    aAcc.previousSibling;
  } catch (e) {
    success = (e.result == Components.results.NS_ERROR_FAILURE);
  }
  ok(success, "previousSibling" + msg);

  // parent
  success = false;
  try {
    aAcc.parent;
  } catch (e) {
    success = (e.result == Components.results.NS_ERROR_FAILURE);
  }
  ok(success, "parent" + msg);
}


/**
 * Convert role to human readable string.
 */
function roleToString(aRole)
{
  return gAccRetrieval.getStringRole(aRole);
}

/**
 * Convert states to human readable string.
 */
function statesToString(aStates, aExtraStates)
{
  var list = gAccRetrieval.getStringStates(aStates, aExtraStates);

  var str = "";
  for (var index = 0; index < list.length - 1; index++)
    str += list.item(index) + ", ";

  if (list.length != 0)
    str += list.item(index)

  return str;
}

/**
 * Convert event type to human readable string.
 */
function eventTypeToString(aEventType)
{
  return gAccRetrieval.getStringEventType(aEventType);
}

/**
 * Convert relation type to human readable string.
 */
function relationTypeToString(aRelationType)
{
  return gAccRetrieval.getStringRelationType(aRelationType);
}

/**
 * Return pretty name for identifier, it may be ID, DOM node or accessible.
 */
function prettyName(aIdentifier)
{
  if (aIdentifier instanceof nsIAccessible) {
    var acc = getAccessible(aIdentifier, [nsIAccessNode]);
    var msg = "[" + getNodePrettyName(acc.DOMNode) +
      ", role: " + roleToString(acc.role);

    if (acc.name)
      msg += ", name: '" + acc.name + "'"
    msg += "]";

    return msg;
  }

  if (aIdentifier instanceof nsIDOMNode)
    return getNodePrettyName(aIdentifier);

  return " '" + aIdentifier + "' ";
}

////////////////////////////////////////////////////////////////////////////////
// Private
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Accessible general

function initialize()
{
  gAccRetrieval = Components.classes["@mozilla.org/accessibleRetrieval;1"].
    getService(nsIAccessibleRetrieval);
}

addLoadEvent(initialize);

function getNodePrettyName(aNode)
{
  try {
    if (aNode.nodeType == nsIDOMNode.ELEMENT_NODE && aNode.hasAttribute("id"))
      return " '" + aNode.getAttribute("id") + "' ";

    if (aNode.nodeType == nsIDOMNode.DOCUMENT_NODE)
      return " 'document node' ";

    return " '" + aNode.localName + " node' ";
  } catch (e) {
    return "no node info";
  }
}
