/*
 * Run an xpcshell subprocess and crash it.
 *
 * @param setup
 *        A string of JavaScript code to execute in the subprocess
 *        before crashing. If this is a function and not a string,
 *        it will have .toSource() called on it, and turned into
 *        a call to itself. (for programmer convenience)
 *        This code will be evaluted between crasher_subprocess_head.js
 *        and crasher_subprocess_tail.js, so it will have access
 *        to everything defined in crasher_subprocess_head.js,
 *        which includes "crashReporter", a variable holding
 *        the crash reporter service.
 *
 * @param callback
 *        A JavaScript function to be called after the subprocess
 *        crashes. It will be passed (minidump, extra), where
 *         minidump is an nsILocalFile of the minidump file produced,
 *         and extra is an object containing the key,value pairs from
 *         the .extra file.
 */
function do_crash(setup, callback)
{
  // get current process filename (xpcshell)
  let ds = Components.classes["@mozilla.org/file/directory_service;1"]
    .getService(Components.interfaces.nsIProperties);
  let bin = ds.get("CurProcD", Components.interfaces.nsILocalFile);
  bin.append("xpcshell");
  if (!bin.exists()) {
    bin.leafName = "xpcshell.exe";
    do_check_true(bin.exists());
    if (!bin.exists())
      // weird, can't find xpcshell binary?
      do_throw("Can't find xpcshell binary!");
  }
  // get Gre dir (GreD)
  let greD = ds.get("GreD", Components.interfaces.nsILocalFile);
  let headfile = do_get_file("crasher_subprocess_head.js");
  let tailfile = do_get_file("crasher_subprocess_tail.js");
  // run xpcshell -g GreD -f head -e "some setup code" -f tail
  let process = Components.classes["@mozilla.org/process/util;1"]
                  .createInstance(Components.interfaces.nsIProcess);
  process.init(bin);
  let args = ['-g', greD.path,
              '-f', headfile.path];
  if (setup) {
    if (typeof(setup) == "function")
      // funky, but convenient
      setup = "("+setup.toSource()+")();";
    args.push('-e', setup);
  }
  args.push('-f', tailfile.path);
  try {
      process.run(true, args, args.length);
  }
  catch(ex) {} // on Windows we exit with a -1 status when crashing.

  // should exit with an error (should have crashed)
  do_check_neq(process.exitValue, 0);
  // find minidump
  let minidump = null;
  let en = do_get_cwd().directoryEntries;
  while (en.hasMoreElements()) {
    let f = en.getNext().QueryInterface(Components.interfaces.nsILocalFile);
    if (f.leafName.substr(-4) == ".dmp") {
      minidump = f;
      break;
    }
  }

  if (minidump == null)
    do_throw("No minidump found!");

  let extrafile = minidump.clone();
  extrafile.leafName = extrafile.leafName.slice(0, -4) + ".extra";
  do_check_true(extrafile.exists());
  let extra = parseKeyValuePairsFromFile(extrafile);

  if (callback)
    callback(minidump, extra);

  if (minidump.exists())
    minidump.remove(false);
  if (extrafile.exists())
    extrafile.remove(false);
}

// Utility functions for parsing .extra files
function parseKeyValuePairs(text) {
  var lines = text.split('\n');
  var data = {};
  for (let i = 0; i < lines.length; i++) {
    if (lines[i] == '')
      continue;

    // can't just .split() because the value might contain = characters
    let eq = lines[i].indexOf('=');
    if (eq != -1) {
      let [key, value] = [lines[i].substring(0, eq),
                          lines[i].substring(eq + 1)];
      if (key && value)
        data[key] = value.replace("\\n", "\n", "g").replace("\\\\", "\\", "g");
    }
  }
  return data;
}

function parseKeyValuePairsFromFile(file) {
  var fstream = Components.classes["@mozilla.org/network/file-input-stream;1"].
                createInstance(Components.interfaces.nsIFileInputStream);
  fstream.init(file, -1, 0, 0);
  var is = Components.classes["@mozilla.org/intl/converter-input-stream;1"].
           createInstance(Components.interfaces.nsIConverterInputStream);
  is.init(fstream, "UTF-8", 1024, Components.interfaces.nsIConverterInputStream.DEFAULT_REPLACEMENT_CHARACTER);
  var str = {};
  var contents = '';
  while (is.readString(4096, str) != 0) {
    contents += str.value;
  }
  is.close();
  fstream.close();
  return parseKeyValuePairs(contents);
}
