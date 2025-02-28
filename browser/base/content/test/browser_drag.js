function test()
{
  // ---- Test dragging the proxy icon ---
  var value = content.location.href;
  var urlString = value + "\n" + content.document.title;
  var htmlString = "<a href=\"" + value + "\">" + value + "</a>";
  var expected = [ [
    { type  : "text/x-moz-url",
      data  : urlString },
    { type  : "text/uri-list",
      data  : value },
    { type  : "text/plain",
      data  : value },
    { type  : "text/html",
      data  : htmlString }
  ] ];
  // set the valid attribute so dropping is allowed
  var proxyicon = document.getElementById("page-proxy-favicon")
  var oldstate = proxyicon.getAttribute("pageproxystate");
  proxyicon.setAttribute("pageproxystate", "valid");
  var dt = EventUtils.synthesizeDragStart(proxyicon, expected);
  is(dt, null, "drag on proxy icon");
  proxyicon.setAttribute("pageproxystate", oldstate);
  // Now, the identity information panel is opened by the proxy icon click.
  // We need to close it for next tests.
  EventUtils.synthesizeKey("VK_ESCAPE", {}, window);
}
