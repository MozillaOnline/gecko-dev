const Ci = Components.interfaces;
const Cc = Components.classes;
const CC = Components.Constructor;

function CreateScriptableConverter()
{
  var ScriptableUnicodeConverter = 
    CC("@mozilla.org/intl/scriptableunicodeconverter",
       "nsIScriptableUnicodeConverter");

  return new ScriptableUnicodeConverter();
}

function checkDecode(converter, charset, inText, expectedText)
{
  converter.charset = charset;

  dump("testing decoding from " + charset + " to Unicode.\n");
  try {
    var outText = converter.ConvertToUnicode(inText) + converter.Finish();
  } catch(e) {
    outText = "\ufffd";
  }
  do_check_eq(outText, expectedText);
}

function checkEncode(converter, charset, inText, expectedText)
{
  converter.charset = charset;

  dump("testing encoding from Unicode to " + charset + "\n");
  var outText = converter.ConvertFromUnicode(inText) + converter.Finish();
  do_check_eq(outText, expectedText);
}

function testDecodeAliases()
{
  var converter = CreateScriptableConverter();
  for (var i = 0; i < aliases.length; ++i) {
    checkDecode(converter, aliases[i], inString, expectedString);
  }
}

function testEncodeAliases()
{
  var converter = CreateScriptableConverter();
  for (var i = 0; i < aliases.length; ++i) {
    checkEncode(converter, aliases[i], inString, expectedString);
  }
}
