var data = '<!DOCTYPE HTML>\n\
<html>\n\
<head>\n\
<script>\n\
window.addEventListener("message", function(e) {\n\
\n\
  sendData = null;\n\
\n\
  req = eval(e.data);\n\
  var res = {\n\
    didFail: false,\n\
    events: [],\n\
    progressEvents: 0\n\
  };\n\
  \n\
  var xhr = new XMLHttpRequest();\n\
  for each(type in ["load", "abort", "error", "loadstart"]) {\n\
    xhr.addEventListener(type, function(e) {\n\
      res.events.push(e.type);\n\
    }, false);\n\
  }\n\
  xhr.addEventListener("readystatechange", function(e) {\n\
    res.events.push("rs" + xhr.readyState);\n\
  }, false);\n\
  xhr.addEventListener("progress", function(e) {\n\
    res.progressEvents++;\n\
  }, false);\n\
  if (req.uploadProgress) {\n\
    if (req.uploadProgress == "uploadProgress") {\n\
      xhr.addEventListener("uploadProgress", function(e) {\n\
        res.progressEvents++;\n\
      }, false);\n\
    }\n\
    else {\n\
      xhr.upload.addEventListener(req.uploadProgress, function(e) {\n\
        res.progressEvents++;\n\
      }, false);\n\
    }\n\
  }\n\
  xhr.onload = function () {\n\
    res.status = xhr.status;\n\
    res.responseXML = xhr.responseXML ?\n\
      (new XMLSerializer()).serializeToString(xhr.responseXML) :\n\
      null;\n\
    res.responseText = xhr.responseText;\n\
    post(e, res);\n\
  };\n\
  xhr.onerror = function () {\n\
    res.didFail = true;\n\
    res.status = xhr.status;\n\
    res.responseXML = xhr.responseXML ?\n\
      (new XMLSerializer()).serializeToString(xhr.responseXML) :\n\
      null;\n\
    res.responseText = xhr.responseText;\n\
    post(e, res);\n\
  }\n\
\n\
  if (req.withCred)\n\
    xhr.withCredentials = true;\n\
  if (req.body)\n\
    sendData = req.body;\n\
\n\
  res.events.push("opening");\n\
  xhr.open(req.method, req.url, true);\n\
\n\
  for (header in req.headers) {\n\
    xhr.setRequestHeader(header, req.headers[header]);\n\
  }\n\
\n\
  res.events.push("sending");\n\
  xhr.send(sendData);\n\
\n\
}, false);\n\
\n\
function post(e, res) {\n\
  e.source.postMessage(res.toSource(), "*");\n\
}\n\
\n\
</script>\n\
</head>\n\
<body>\n\
Inner page\n\
</body>\n\
</html>'

function handleRequest(request, response)
{
  response.setStatusLine(null, 302, "Follow me");
  response.setHeader("Location", "data:text/html," + escape(data));
  response.setHeader("Content-Type", "text/plain");
  response.write("Follow that guy!");
}
