/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Laurent Jouanneau <laurent.jouanneau@disruptive-innovations.com>
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either of the GNU General Public License Version 2 or later (the "GPL"),
 * or the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
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

/*
 * nsIContentSerializer implementation that can be used with an
 * nsIDocumentEncoder to convert an XML DOM to an XML string that
 * could be parsed into more or less the original DOM.
 */

#include "nsXMLContentSerializer.h"

#include "nsGkAtoms.h"
#include "nsIDOMText.h"
#include "nsIDOMCDATASection.h"
#include "nsIDOMProcessingInstruction.h"
#include "nsIDOMComment.h"
#include "nsIDOMDocument.h"
#include "nsIDOMDocumentType.h"
#include "nsIDOMElement.h"
#include "nsIContent.h"
#include "nsIDocument.h"
#include "nsIDocumentEncoder.h"
#include "nsINameSpaceManager.h"
#include "nsTextFragment.h"
#include "nsString.h"
#include "prprf.h"
#include "nsUnicharUtils.h"
#include "nsCRT.h"
#include "nsContentUtils.h"
#include "nsAttrName.h"
#include "nsILineBreaker.h"

#define kXMLNS "xmlns"

// to be readable, we assume that an indented line contains
// at least this number of characters (arbitrary value here).
// This is a limit for the indentation.
#define MIN_INDENTED_LINE_LENGTH 15

// the string used to indent.
#define INDENT_STRING "  "
#define INDENT_STRING_LENGTH 2

nsresult NS_NewXMLContentSerializer(nsIContentSerializer** aSerializer)
{
  nsXMLContentSerializer* it = new nsXMLContentSerializer();
  if (!it) {
    return NS_ERROR_OUT_OF_MEMORY;
  }

  return CallQueryInterface(it, aSerializer);
}

nsXMLContentSerializer::nsXMLContentSerializer()
  : mPrefixIndex(0),
    mColPos(0),
    mIndentOverflow(0),
    mIsIndentationAddedOnCurrentLine(PR_FALSE),
    mInAttribute(PR_FALSE),
    mAddNewlineForRootNode(PR_FALSE),
    mAddSpace(PR_FALSE),
    mMayIgnoreLineBreakSequence(PR_FALSE)
{
}

nsXMLContentSerializer::~nsXMLContentSerializer()
{
}

NS_IMPL_ISUPPORTS1(nsXMLContentSerializer, nsIContentSerializer)

NS_IMETHODIMP 
nsXMLContentSerializer::Init(PRUint32 aFlags, PRUint32 aWrapColumn,
                             const char* aCharSet, PRBool aIsCopying,
                             PRBool aRewriteEncodingDeclaration)
{
  mCharset = aCharSet;
  mFlags = aFlags;

  // Set the line break character:
  if ((mFlags & nsIDocumentEncoder::OutputCRLineBreak)
      && (mFlags & nsIDocumentEncoder::OutputLFLineBreak)) { // Windows
    mLineBreak.AssignLiteral("\r\n");
  }
  else if (mFlags & nsIDocumentEncoder::OutputCRLineBreak) { // Mac
    mLineBreak.AssignLiteral("\r");
  }
  else if (mFlags & nsIDocumentEncoder::OutputLFLineBreak) { // Unix/DOM
    mLineBreak.AssignLiteral("\n");
  }
  else {
    mLineBreak.AssignLiteral(NS_LINEBREAK);         // Platform/default
  }

  mDoRaw = !!(mFlags & nsIDocumentEncoder::OutputRaw);

  mDoFormat = (mFlags & nsIDocumentEncoder::OutputFormatted && !mDoRaw);

  mDoWrap = (mFlags & nsIDocumentEncoder::OutputWrap && !mDoRaw);

  if (!aWrapColumn) {
    mMaxColumn = 72;
  }
  else {
    mMaxColumn = aWrapColumn;
  }

  mPreLevel = 0;
  mIsIndentationAddedOnCurrentLine = PR_FALSE;
  return NS_OK;
}

nsresult
nsXMLContentSerializer::AppendTextData(nsIDOMNode* aNode,
                                       PRInt32 aStartOffset,
                                       PRInt32 aEndOffset,
                                       nsAString& aStr,
                                       PRBool aTranslateEntities)
{
  nsCOMPtr<nsIContent> content = do_QueryInterface(aNode);
  const nsTextFragment* frag;
  if (!content || !(frag = content->GetText())) {
    return NS_ERROR_FAILURE;
  }

  PRInt32 endoffset = (aEndOffset == -1) ? frag->GetLength() : aEndOffset;
  PRInt32 length = endoffset - aStartOffset;

  NS_ASSERTION(aStartOffset >= 0, "Negative start offset for text fragment!");
  NS_ASSERTION(aStartOffset <= endoffset, "A start offset is beyond the end of the text fragment!");

  if (length <= 0) {
    // XXX Zero is a legal value, maybe non-zero values should be an
    // error.
    return NS_OK;
  }
    
  if (frag->Is2b()) {
    const PRUnichar *strStart = frag->Get2b() + aStartOffset;
    if (aTranslateEntities) {
      AppendAndTranslateEntities(Substring(strStart, strStart + length), aStr);
    }
    else {
      aStr.Append(Substring(strStart, strStart + length));
    }
  }
  else {
    if (aTranslateEntities) {
      AppendAndTranslateEntities(NS_ConvertASCIItoUTF16(frag->Get1b()+aStartOffset, length), aStr);
    }
    else {
      aStr.Append(NS_ConvertASCIItoUTF16(frag->Get1b()+aStartOffset, length));
    }
  }

  return NS_OK;
}

NS_IMETHODIMP 
nsXMLContentSerializer::AppendText(nsIDOMText* aText,
                                   PRInt32 aStartOffset,
                                   PRInt32 aEndOffset,
                                   nsAString& aStr)
{
  NS_ENSURE_ARG(aText);

  nsAutoString data;
  nsresult rv;

  rv = AppendTextData(aText, aStartOffset, aEndOffset, data, PR_TRUE);
  if (NS_FAILED(rv))
    return NS_ERROR_FAILURE;

  if (mPreLevel > 0 || mDoRaw) {
    AppendToStringConvertLF(data, aStr);
  }
  else if (mDoFormat) {
    AppendToStringFormatedWrapped(data, aStr);
  }
  else if (mDoWrap) {
    AppendToStringWrapped(data, aStr);
  }
  else {
    AppendToStringConvertLF(data, aStr);
  }

  return NS_OK;
}

NS_IMETHODIMP 
nsXMLContentSerializer::AppendCDATASection(nsIDOMCDATASection* aCDATASection,
                                           PRInt32 aStartOffset,
                                           PRInt32 aEndOffset,
                                           nsAString& aStr)
{
  NS_ENSURE_ARG(aCDATASection);
  nsresult rv;

  NS_NAMED_LITERAL_STRING(cdata , "<![CDATA[");

  if (mPreLevel > 0 || mDoRaw) {
    AppendToString(cdata, aStr);
  }
  else if (mDoFormat) {
    AppendToStringFormatedWrapped(cdata, aStr);
  }
  else if (mDoWrap) {
    AppendToStringWrapped(cdata, aStr);
  }
  else {
    AppendToString(cdata, aStr);
  }

  nsAutoString data;
  rv = AppendTextData(aCDATASection, aStartOffset, aEndOffset, data, PR_FALSE);
  if (NS_FAILED(rv)) return NS_ERROR_FAILURE;

  AppendToStringConvertLF(data, aStr);

  AppendToString(NS_LITERAL_STRING("]]>"), aStr);

  return NS_OK;
}

NS_IMETHODIMP 
nsXMLContentSerializer::AppendProcessingInstruction(nsIDOMProcessingInstruction* aPI,
                                                    PRInt32 aStartOffset,
                                                    PRInt32 aEndOffset,
                                                    nsAString& aStr)
{
  NS_ENSURE_ARG(aPI);
  nsresult rv;
  nsAutoString target, data, start;

  MaybeAddNewlineForRootNode(aStr);

  rv = aPI->GetTarget(target);
  if (NS_FAILED(rv)) return NS_ERROR_FAILURE;

  rv = aPI->GetData(data);
  if (NS_FAILED(rv)) return NS_ERROR_FAILURE;

  start.AppendLiteral("<?");
  start.Append(target);

  if (mPreLevel > 0 || mDoRaw) {
    AppendToString(start, aStr);
  }
  else if (mDoFormat) {
    if (mAddSpace) {
      AppendNewLineToString(aStr);
    }
    AppendToStringFormatedWrapped(start, aStr);
  }
  else if (mDoWrap) {
    AppendToStringWrapped(start, aStr);
  }
  else {
    AppendToString(start, aStr);
  }

  if (!data.IsEmpty()) {
    AppendToString(PRUnichar(' '), aStr);
    AppendToStringConvertLF(data, aStr);
  }
  AppendToString(NS_LITERAL_STRING("?>"), aStr);

  MaybeFlagNewlineForRootNode(aPI);

  return NS_OK;
}

NS_IMETHODIMP 
nsXMLContentSerializer::AppendComment(nsIDOMComment* aComment,
                                      PRInt32 aStartOffset,
                                      PRInt32 aEndOffset,
                                      nsAString& aStr)
{
  NS_ENSURE_ARG(aComment);
  nsresult rv;
  nsAutoString data;

  rv = aComment->GetData(data);
  if (NS_FAILED(rv)) return NS_ERROR_FAILURE;

  if (aStartOffset || (aEndOffset != -1)) {
    PRInt32 length = (aEndOffset == -1) ? data.Length() : aEndOffset;
    length -= aStartOffset;

    nsAutoString frag;
    data.Mid(frag, aStartOffset, length);
    data.Assign(frag);
  }

  MaybeAddNewlineForRootNode(aStr);

  NS_NAMED_LITERAL_STRING(startComment, "<!--");

  if (mPreLevel > 0 || mDoRaw) {
    AppendToString(startComment, aStr);
  }
  else if (mDoFormat) {
    if (mAddSpace) {
      AppendNewLineToString(aStr);
    }
    AppendToStringFormatedWrapped(startComment, aStr);
  }
  else if (mDoWrap) {
    AppendToStringWrapped(startComment, aStr);
  }
  else {
    AppendToString(startComment, aStr);
  }

  // Even if mDoformat, we don't format the content because it
  // could have been preformated by the author
  AppendToStringConvertLF(data, aStr);
  AppendToString(NS_LITERAL_STRING("-->"), aStr);

  MaybeFlagNewlineForRootNode(aComment);

  return NS_OK;
}

NS_IMETHODIMP 
nsXMLContentSerializer::AppendDoctype(nsIDOMDocumentType *aDoctype,
                                      nsAString& aStr)
{
  NS_ENSURE_ARG(aDoctype);
  nsresult rv;
  nsAutoString name, publicId, systemId, internalSubset;

  rv = aDoctype->GetName(name);
  if (NS_FAILED(rv)) return NS_ERROR_FAILURE;
  rv = aDoctype->GetPublicId(publicId);
  if (NS_FAILED(rv)) return NS_ERROR_FAILURE;
  rv = aDoctype->GetSystemId(systemId);
  if (NS_FAILED(rv)) return NS_ERROR_FAILURE;
  rv = aDoctype->GetInternalSubset(internalSubset);
  if (NS_FAILED(rv)) return NS_ERROR_FAILURE;

  MaybeAddNewlineForRootNode(aStr);

  AppendToString(NS_LITERAL_STRING("<!DOCTYPE "), aStr);
  AppendToString(name, aStr);

  PRUnichar quote;
  if (!publicId.IsEmpty()) {
    AppendToString(NS_LITERAL_STRING(" PUBLIC "), aStr);
    if (publicId.FindChar(PRUnichar('"')) == -1) {
      quote = PRUnichar('"');
    }
    else {
      quote = PRUnichar('\'');
    }
    AppendToString(quote, aStr);
    AppendToString(publicId, aStr);
    AppendToString(quote, aStr);

    if (!systemId.IsEmpty()) {
      AppendToString(PRUnichar(' '), aStr);
      if (systemId.FindChar(PRUnichar('"')) == -1) {
        quote = PRUnichar('"');
      }
      else {
        quote = PRUnichar('\'');
      }
      AppendToString(quote, aStr);
      AppendToString(systemId, aStr);
      AppendToString(quote, aStr);
    }
  }
  else if (!systemId.IsEmpty()) {
    if (systemId.FindChar(PRUnichar('"')) == -1) {
      quote = PRUnichar('"');
    }
    else {
      quote = PRUnichar('\'');
    }
    AppendToString(NS_LITERAL_STRING(" SYSTEM "), aStr);
    AppendToString(quote, aStr);
    AppendToString(systemId, aStr);
    AppendToString(quote, aStr);
  }
  
  if (!internalSubset.IsEmpty()) {
    AppendToString(NS_LITERAL_STRING(" ["), aStr);
    AppendToString(internalSubset, aStr);
    AppendToString(PRUnichar(']'), aStr);
  }
    
  AppendToString(kGreaterThan, aStr);
  MaybeFlagNewlineForRootNode(aDoctype);

  return NS_OK;
}

nsresult
nsXMLContentSerializer::PushNameSpaceDecl(const nsAString& aPrefix,
                                          const nsAString& aURI,
                                          nsIDOMElement* aOwner)
{
  NameSpaceDecl* decl = mNameSpaceStack.AppendElement();
  if (!decl) return NS_ERROR_OUT_OF_MEMORY;

  decl->mPrefix.Assign(aPrefix);
  decl->mURI.Assign(aURI);
  // Don't addref - this weak reference will be removed when
  // we pop the stack
  decl->mOwner = aOwner;
  return NS_OK;
}

void
nsXMLContentSerializer::PopNameSpaceDeclsFor(nsIDOMElement* aOwner)
{
  PRInt32 index, count;

  count = mNameSpaceStack.Length();
  for (index = count - 1; index >= 0; index--) {
    if (mNameSpaceStack[index].mOwner != aOwner) {
      break;
    }
    mNameSpaceStack.RemoveElementAt(index);
  }
}

PRBool
nsXMLContentSerializer::ConfirmPrefix(nsAString& aPrefix,
                                      const nsAString& aURI,
                                      nsIDOMElement* aElement,
                                      PRBool aIsAttribute)
{
  if (aPrefix.EqualsLiteral(kXMLNS)) {
    return PR_FALSE;
  }

  if (aURI.EqualsLiteral("http://www.w3.org/XML/1998/namespace")) {
    // The prefix must be xml for this namespace. We don't need to declare it,
    // so always just set the prefix to xml.
    aPrefix.AssignLiteral("xml");

    return PR_FALSE;
  }

  PRBool mustHavePrefix;
  if (aIsAttribute) {
    if (aURI.IsEmpty()) {
      // Attribute in the null namespace.  This just shouldn't have a prefix.
      // And there's no need to push any namespace decls
      aPrefix.Truncate();
      return PR_FALSE;
    }

    // Attribute not in the null namespace -- must have a prefix
    mustHavePrefix = PR_TRUE;
  } else {
    // Not an attribute, so doesn't _have_ to have a prefix
    mustHavePrefix = PR_FALSE;
  }

  // Keep track of the closest prefix that's bound to aURI and whether we've
  // found such a thing.  closestURIMatch holds the prefix, and uriMatch
  // indicates whether we actually have one.
  nsAutoString closestURIMatch;
  PRBool uriMatch = PR_FALSE;

  // Also keep track of whether we've seen aPrefix already.  If we have, that
  // means that it's already bound to a URI different from aURI, so even if we
  // later (so in a more outer scope) see it bound to aURI we can't reuse it.
  PRBool haveSeenOurPrefix = PR_FALSE;

  PRInt32 count = mNameSpaceStack.Length();
  PRInt32 index = count - 1;
  while (index >= 0) {
    NameSpaceDecl& decl = mNameSpaceStack.ElementAt(index);
    // Check if we've found a prefix match
    if (aPrefix.Equals(decl.mPrefix)) {

      // If the URIs match and aPrefix is not bound to any other URI, we can
      // use aPrefix
      if (!haveSeenOurPrefix && aURI.Equals(decl.mURI)) {
        // Just use our uriMatch stuff.  That will deal with an empty aPrefix
        // the right way.  We can break out of the loop now, though.
        uriMatch = PR_TRUE;
        closestURIMatch = aPrefix;
        break;
      }

      haveSeenOurPrefix = PR_TRUE;      

      // If they don't, and either:
      // 1) We have a prefix (so we'd be redeclaring this prefix to point to a
      //    different namespace) or
      // 2) We're looking at an existing default namespace decl on aElement (so
      //    we can't create a new default namespace decl for this URI)
      // then generate a new prefix.  Note that we do NOT generate new prefixes
      // if we happen to have aPrefix == decl->mPrefix == "" and mismatching
      // URIs when |decl| doesn't have aElement as its owner.  In that case we
      // can simply push the new namespace URI as the default namespace for
      // aElement.
      if (!aPrefix.IsEmpty() || decl.mOwner == aElement) {
        NS_ASSERTION(!aURI.IsEmpty(),
                     "Not allowed to add a xmlns attribute with an empty "
                     "namespace name unless it declares the default "
                     "namespace.");

        GenerateNewPrefix(aPrefix);
        // Now we need to validate our new prefix/uri combination; check it
        // against the full namespace stack again.  Note that just restarting
        // the while loop is ok, since we haven't changed aURI, so the
        // closestURIMatch and uriMatch state is not affected.
        index = count - 1;
        haveSeenOurPrefix = PR_FALSE;
        continue;
      }
    }
    
    // If we've found a URI match, then record the first one
    if (!uriMatch && aURI.Equals(decl.mURI)) {
      // Need to check that decl->mPrefix is not declared anywhere closer to
      // us.  If it is, we can't use it.
      PRBool prefixOK = PR_TRUE;
      PRInt32 index2;
      for (index2 = count-1; index2 > index && prefixOK; --index2) {
        prefixOK = (mNameSpaceStack[index2].mPrefix != decl.mPrefix);
      }
      
      if (prefixOK) {
        uriMatch = PR_TRUE;
        closestURIMatch.Assign(decl.mPrefix);
      }
    }
    
    --index;
  }

  // At this point the following invariants hold:
  // 1) The prefix in closestURIMatch is mapped to aURI in our scope if
  //    uriMatch is set.
  // 2) There is nothing on the namespace stack that has aPrefix as the prefix
  //    and a _different_ URI, except for the case aPrefix.IsEmpty (and
  //    possible default namespaces on ancestors)
  
  // So if uriMatch is set it's OK to use the closestURIMatch prefix.  The one
  // exception is when closestURIMatch is actually empty (default namespace
  // decl) and we must have a prefix.
  if (uriMatch && (!mustHavePrefix || !closestURIMatch.IsEmpty())) {
    aPrefix.Assign(closestURIMatch);
    return PR_FALSE;
  }
  
  if (aPrefix.IsEmpty()) {
    // At this point, aPrefix is empty (which means we never had a prefix to
    // start with).  If we must have a prefix, just generate a new prefix and
    // then send it back through the namespace stack checks to make sure it's
    // OK.
    if (mustHavePrefix) {
      GenerateNewPrefix(aPrefix);
      return ConfirmPrefix(aPrefix, aURI, aElement, aIsAttribute);
    }

    // One final special case.  If aPrefix is empty and we never saw an empty
    // prefix (default namespace decl) on the namespace stack and we're in the
    // null namespace there is no reason to output an |xmlns=""| here.  It just
    // makes the output less readable.
    if (!haveSeenOurPrefix && aURI.IsEmpty()) {
      return PR_FALSE;
    }
  }

  // Now just set aURI as the new default namespace URI.  Indicate that we need
  // to create a namespace decl for the final prefix
  return PR_TRUE;
}

void
nsXMLContentSerializer::GenerateNewPrefix(nsAString& aPrefix)
{
  aPrefix.AssignLiteral("a");
  char buf[128];
  PR_snprintf(buf, sizeof(buf), "%d", mPrefixIndex++);
  AppendASCIItoUTF16(buf, aPrefix);
}

void
nsXMLContentSerializer::SerializeAttr(const nsAString& aPrefix,
                                      const nsAString& aName,
                                      const nsAString& aValue,
                                      nsAString& aStr,
                                      PRBool aDoEscapeEntities)
{
  nsAutoString attrString;

  attrString.Append(PRUnichar(' '));
  if (!aPrefix.IsEmpty()) {
    attrString.Append(aPrefix);
    attrString.Append(PRUnichar(':'));
  }
  attrString.Append(aName);

  if (aDoEscapeEntities) {
    // if problem characters are turned into character entity references
    // then there will be no problem with the value delimiter characters
    attrString.AppendLiteral("=\"");

    mInAttribute = PR_TRUE;
    AppendAndTranslateEntities(aValue, attrString);
    mInAttribute = PR_FALSE;

    attrString.Append(PRUnichar('"'));
  }
  else {
    // Depending on whether the attribute value contains quotes or apostrophes we
    // need to select the delimiter character and escape characters using
    // character entity references, ignoring the value of aDoEscapeEntities.
    // See http://www.w3.org/TR/REC-html40/appendix/notes.html#h-B.3.2.2 for
    // the standard on character entity references in values.  We also have to
    // make sure to escape any '&' characters.
    
    PRBool bIncludesSingle = PR_FALSE;
    PRBool bIncludesDouble = PR_FALSE;
    nsAString::const_iterator iCurr, iEnd;
    PRUint32 uiSize, i;
    aValue.BeginReading(iCurr);
    aValue.EndReading(iEnd);
    for ( ; iCurr != iEnd; iCurr.advance(uiSize) ) {
      const PRUnichar * buf = iCurr.get();
      uiSize = iCurr.size_forward();
      for ( i = 0; i < uiSize; i++, buf++ ) {
        if ( *buf == PRUnichar('\'') )
        {
          bIncludesSingle = PR_TRUE;
          if ( bIncludesDouble ) break;
        }
        else if ( *buf == PRUnichar('"') )
        {
          bIncludesDouble = PR_TRUE;
          if ( bIncludesSingle ) break;
        }
      }
      // if both have been found we don't need to search further
      if ( bIncludesDouble && bIncludesSingle ) break;
    }

    // Delimiter and escaping is according to the following table
    //    bIncludesDouble     bIncludesSingle     Delimiter       Escape Double Quote
    //    FALSE               FALSE               "               FALSE
    //    FALSE               TRUE                "               FALSE
    //    TRUE                FALSE               '               FALSE
    //    TRUE                TRUE                "               TRUE
    PRUnichar cDelimiter = 
        (bIncludesDouble && !bIncludesSingle) ? PRUnichar('\'') : PRUnichar('"');
    attrString.Append(PRUnichar('='));
    attrString.Append(cDelimiter);
    nsAutoString sValue(aValue);
    sValue.ReplaceSubstring(NS_LITERAL_STRING("&"),
                            NS_LITERAL_STRING("&amp;"));
    if (bIncludesDouble && bIncludesSingle) {
      sValue.ReplaceSubstring(NS_LITERAL_STRING("\""),
                              NS_LITERAL_STRING("&quot;"));
    }
    attrString.Append(sValue);
    attrString.Append(cDelimiter);
  }
  if (mPreLevel > 0 || mDoRaw) {
    AppendToStringConvertLF(attrString, aStr);
  }
  else if (mDoFormat) {
    AppendToStringFormatedWrapped(attrString, aStr);
  }
  else if (mDoWrap) {
    AppendToStringWrapped(attrString, aStr);
  }
  else {
    AppendToStringConvertLF(attrString, aStr);
  }
}

PRUint32 
nsXMLContentSerializer::ScanNamespaceDeclarations(nsIContent* aContent,
                                                  nsIDOMElement *aOriginalElement,
                                                  const nsAString& aTagNamespaceURI)
{
  PRUint32 index, count;
  nsAutoString nameStr, prefixStr, uriStr, valueStr;

  count = aContent->GetAttrCount();

  // First scan for namespace declarations, pushing each on the stack
  PRUint32 skipAttr = count;
  for (index = 0; index < count; index++) {
    
    const nsAttrName* name = aContent->GetAttrNameAt(index);
    PRInt32 namespaceID = name->NamespaceID();
    nsIAtom *attrName = name->LocalName();
    
    if (namespaceID == kNameSpaceID_XMLNS ||
        // Also push on the stack attrs named "xmlns" in the null
        // namespace... because once we serialize those out they'll look like
        // namespace decls.  :(
        // XXXbz what if we have both "xmlns" in the null namespace and "xmlns"
        // in the xmlns namespace?
        (namespaceID == kNameSpaceID_None &&
         attrName == nsGkAtoms::xmlns)) {
      aContent->GetAttr(namespaceID, attrName, uriStr);

      if (!name->GetPrefix()) {
        if (aTagNamespaceURI.IsEmpty() && !uriStr.IsEmpty()) {
          // If the element is in no namespace we need to add a xmlns
          // attribute to declare that. That xmlns attribute must not have a
          // prefix (see http://www.w3.org/TR/REC-xml-names/#dt-prefix), ie it
          // must declare the default namespace. We just found an xmlns
          // attribute that declares the default namespace to something
          // non-empty. We're going to ignore this attribute, for children we
          // will detect that we need to add it again and attributes aren't
          // affected by the default namespace.
          skipAttr = index;
        }
        else {
          // Default NS attribute does not have prefix (and the name is "xmlns")
          PushNameSpaceDecl(EmptyString(), uriStr, aOriginalElement);
        }
      }
      else {
        attrName->ToString(nameStr);
        PushNameSpaceDecl(nameStr, uriStr, aOriginalElement);
      }
    }
  }
  return skipAttr;
}


PRBool
nsXMLContentSerializer::IsJavaScript(nsIContent * aContent, nsIAtom* aAttrNameAtom,
                                     PRInt32 aAttrNamespaceID, const nsAString& aValueString)
{
  PRInt32 namespaceID = aContent->GetNameSpaceID();
  PRBool isHtml = aContent->IsHTML();

  if (aAttrNamespaceID == kNameSpaceID_None &&
      (isHtml ||
       namespaceID == kNameSpaceID_XUL ||
       namespaceID == kNameSpaceID_SVG) &&
      (aAttrNameAtom == nsGkAtoms::href ||
       aAttrNameAtom == nsGkAtoms::src)) {

    static const char kJavaScript[] = "javascript";
    PRInt32 pos = aValueString.FindChar(':');
    if (pos < (PRInt32)(sizeof kJavaScript - 1))
        return PR_FALSE;
    nsAutoString scheme(Substring(aValueString, 0, pos));
    scheme.StripWhitespace();
    if ((scheme.Length() == (sizeof kJavaScript - 1)) &&
        scheme.EqualsIgnoreCase(kJavaScript))
      return PR_TRUE;
    else
      return PR_FALSE;
  }

  if (isHtml) {
    return nsContentUtils::IsEventAttributeName(aAttrNameAtom, EventNameType_HTML);
  }
  else if (namespaceID == kNameSpaceID_XUL) {
    return nsContentUtils::IsEventAttributeName(aAttrNameAtom, EventNameType_XUL);
  }
  else if (namespaceID == kNameSpaceID_SVG) {
    return nsContentUtils::IsEventAttributeName(aAttrNameAtom,
                                                EventNameType_SVGGraphic | EventNameType_SVGSVG);
  }
  return PR_FALSE;
}


void 
nsXMLContentSerializer::SerializeAttributes(nsIContent* aContent,
                                            nsIDOMElement *aOriginalElement,
                                            nsAString& aTagPrefix,
                                            const nsAString& aTagNamespaceURI,
                                            nsIAtom* aTagName,
                                            nsAString& aStr,
                                            PRUint32 aSkipAttr,
                                            PRBool aAddNSAttr)
{

  nsAutoString nameStr, prefixStr, uriStr, valueStr;
  nsAutoString xmlnsStr;
  xmlnsStr.AssignLiteral(kXMLNS);
  PRUint32 index, count;

  // If we had to add a new namespace declaration, serialize
  // and push it on the namespace stack
  if (aAddNSAttr) {
    if (aTagPrefix.IsEmpty()) {
      // Serialize default namespace decl
      SerializeAttr(EmptyString(), xmlnsStr, aTagNamespaceURI, aStr, PR_TRUE);
    }
    else {
      // Serialize namespace decl
      SerializeAttr(xmlnsStr, aTagPrefix, aTagNamespaceURI, aStr, PR_TRUE);
    }
    PushNameSpaceDecl(aTagPrefix, aTagNamespaceURI, aOriginalElement);
  }

  count = aContent->GetAttrCount();

  // Now serialize each of the attributes
  // XXX Unfortunately we need a namespace manager to get
  // attribute URIs.
  for (index = 0; index < count; index++) {
    if (aSkipAttr == index) {
        continue;
    }

    const nsAttrName* name = aContent->GetAttrNameAt(index);
    PRInt32 namespaceID = name->NamespaceID();
    nsIAtom* attrName = name->LocalName();
    nsIAtom* attrPrefix = name->GetPrefix();

    if (attrPrefix) {
      attrPrefix->ToString(prefixStr);
    }
    else {
      prefixStr.Truncate();
    }

    PRBool addNSAttr = PR_FALSE;
    if (kNameSpaceID_XMLNS != namespaceID) {
      nsContentUtils::NameSpaceManager()->GetNameSpaceURI(namespaceID, uriStr);
      addNSAttr = ConfirmPrefix(prefixStr, uriStr, aOriginalElement, PR_TRUE);
    }
    
    aContent->GetAttr(namespaceID, attrName, valueStr);
    attrName->ToString(nameStr);

    // XXX Hack to get around the fact that MathML can add
    //     attributes starting with '-', which makes them
    //     invalid XML. see Bug 475518
    if (!nameStr.IsEmpty() && nameStr.First() == '-')
      continue;

    PRBool isJS = IsJavaScript(aContent, attrName, namespaceID, valueStr);

    SerializeAttr(prefixStr, nameStr, valueStr, aStr, !isJS);
    
    if (addNSAttr) {
      NS_ASSERTION(!prefixStr.IsEmpty(),
                   "Namespaced attributes must have a prefix");
      SerializeAttr(xmlnsStr, prefixStr, uriStr, aStr, PR_TRUE);
      PushNameSpaceDecl(prefixStr, uriStr, aOriginalElement);
    }
  }
}

NS_IMETHODIMP 
nsXMLContentSerializer::AppendElementStart(nsIDOMElement *aElement,
                                           nsIDOMElement *aOriginalElement,
                                           nsAString& aStr)
{
  NS_ENSURE_ARG(aElement);

  nsCOMPtr<nsIContent> content(do_QueryInterface(aElement));
  if (!content) return NS_ERROR_FAILURE;

  PRBool forceFormat = PR_FALSE;
  if (!CheckElementStart(content, forceFormat, aStr)) {
    return NS_OK;
  }

  nsAutoString tagPrefix, tagLocalName, tagNamespaceURI;
  aElement->GetPrefix(tagPrefix);
  aElement->GetLocalName(tagLocalName);
  aElement->GetNamespaceURI(tagNamespaceURI);

  PRUint32 skipAttr = ScanNamespaceDeclarations(content,
                          aOriginalElement, tagNamespaceURI);

  nsIAtom *name = content->Tag();
  PRBool lineBreakBeforeOpen = LineBreakBeforeOpen(content->GetNameSpaceID(), name);

  if ((mDoFormat || forceFormat) && !mPreLevel && !mDoRaw) {
    if (mColPos && lineBreakBeforeOpen) {
      AppendNewLineToString(aStr);
    }
    else {
      MaybeAddNewlineForRootNode(aStr);
    }
    if (!mColPos) {
      AppendIndentation(aStr);
    }
    else if (mAddSpace) {
      AppendToString(PRUnichar(' '), aStr);
      mAddSpace = PR_FALSE;
    }
  }
  else if (mAddSpace) {
    AppendToString(PRUnichar(' '), aStr);
    mAddSpace = PR_FALSE;
  }
  else {
    MaybeAddNewlineForRootNode(aStr);
  }

  // Always reset to avoid false newlines in case MaybeAddNewlineForRootNode wasn't
  // called
  mAddNewlineForRootNode = PR_FALSE;

  PRBool addNSAttr;
  addNSAttr = ConfirmPrefix(tagPrefix, tagNamespaceURI, aOriginalElement,
                            PR_FALSE);

  // Serialize the qualified name of the element
  AppendToString(kLessThan, aStr);
  if (!tagPrefix.IsEmpty()) {
    AppendToString(tagPrefix, aStr);
    AppendToString(NS_LITERAL_STRING(":"), aStr);
  }
  AppendToString(tagLocalName, aStr);

  MaybeEnterInPreContent(content);

  if ((mDoFormat || forceFormat) && !mPreLevel && !mDoRaw) {
    IncrIndentation(name);
  }

  SerializeAttributes(content, aOriginalElement, tagPrefix, tagNamespaceURI,
                      name, aStr, skipAttr, addNSAttr);

  AppendEndOfElementStart(aOriginalElement, name, content->GetNameSpaceID(),
                          aStr);

  if ((mDoFormat || forceFormat) && !mPreLevel 
    && !mDoRaw && LineBreakAfterOpen(content->GetNameSpaceID(), name)) {
    AppendNewLineToString(aStr);
  }

  AfterElementStart(content, aOriginalElement, aStr);

  return NS_OK;
}

void 
nsXMLContentSerializer::AppendEndOfElementStart(nsIDOMElement *aOriginalElement,
                                                nsIAtom * aName,
                                                PRInt32 aNamespaceID,
                                                nsAString& aStr)
{
  // We don't output a separate end tag for empty elements
  PRBool hasChildren = PR_FALSE;
  if (NS_FAILED(aOriginalElement->HasChildNodes(&hasChildren)) ||
      !hasChildren) {
    AppendToString(NS_LITERAL_STRING("/>"), aStr);
  }
  else {
    AppendToString(kGreaterThan, aStr);
  }
}

NS_IMETHODIMP 
nsXMLContentSerializer::AppendElementEnd(nsIDOMElement *aElement,
                                         nsAString& aStr)
{
  NS_ENSURE_ARG(aElement);

  nsCOMPtr<nsIContent> content(do_QueryInterface(aElement));
  if (!content) return NS_ERROR_FAILURE;

  PRBool forceFormat = PR_FALSE, outputElementEnd;
  outputElementEnd = CheckElementEnd(content, forceFormat, aStr);

  nsIAtom *name = content->Tag();

  if ((mDoFormat || forceFormat) && !mPreLevel && !mDoRaw) {
    DecrIndentation(name);
  }

  if (!outputElementEnd) {
    PopNameSpaceDeclsFor(aElement);
    MaybeFlagNewlineForRootNode(aElement);
    return NS_OK;
  }

  nsAutoString tagPrefix, tagLocalName, tagNamespaceURI;
  
  aElement->GetPrefix(tagPrefix);
  aElement->GetLocalName(tagLocalName);
  aElement->GetNamespaceURI(tagNamespaceURI);

#ifdef DEBUG
  PRBool debugNeedToPushNamespace =
#endif
  ConfirmPrefix(tagPrefix, tagNamespaceURI, aElement, PR_FALSE);
  NS_ASSERTION(!debugNeedToPushNamespace, "Can't push namespaces in closing tag!");

  if ((mDoFormat || forceFormat) && !mPreLevel && !mDoRaw) {

    PRBool lineBreakBeforeClose = LineBreakBeforeClose(content->GetNameSpaceID(), name);

    if (mColPos && lineBreakBeforeClose) {
      AppendNewLineToString(aStr);
    }
    if (!mColPos) {
      AppendIndentation(aStr);
    }
    else if (mAddSpace) {
      AppendToString(PRUnichar(' '), aStr);
      mAddSpace = PR_FALSE;
    }
  }
  else if (mAddSpace) {
    AppendToString(PRUnichar(' '), aStr);
    mAddSpace = PR_FALSE;
  }

  AppendToString(kEndTag, aStr);
  if (!tagPrefix.IsEmpty()) {
    AppendToString(tagPrefix, aStr);
    AppendToString(NS_LITERAL_STRING(":"), aStr);
  }
  AppendToString(tagLocalName, aStr);
  AppendToString(kGreaterThan, aStr);

  PopNameSpaceDeclsFor(aElement);

  MaybeLeaveFromPreContent(content);

  if ((mDoFormat || forceFormat) && !mPreLevel
      && !mDoRaw && LineBreakAfterClose(content->GetNameSpaceID(), name)) {
    AppendNewLineToString(aStr);
  }
  else {
    MaybeFlagNewlineForRootNode(aElement);
  }

  AfterElementEnd(content, aStr);

  return NS_OK;
}

NS_IMETHODIMP
nsXMLContentSerializer::AppendDocumentStart(nsIDOMDocument *aDocument,
                                            nsAString& aStr)
{
  NS_ENSURE_ARG_POINTER(aDocument);

  nsCOMPtr<nsIDocument> doc(do_QueryInterface(aDocument));
  if (!doc) {
    return NS_OK;
  }

  nsAutoString version, encoding, standalone;
  doc->GetXMLDeclaration(version, encoding, standalone);

  if (version.IsEmpty())
    return NS_OK; // A declaration must have version, or there is no decl

  NS_NAMED_LITERAL_STRING(endQuote, "\"");

  aStr += NS_LITERAL_STRING("<?xml version=\"") + version + endQuote;
  
  if (!mCharset.IsEmpty()) {
    aStr += NS_LITERAL_STRING(" encoding=\"") +
      NS_ConvertASCIItoUTF16(mCharset) + endQuote;
  }
  // Otherwise just don't output an encoding attr.  Not that we expect
  // mCharset to ever be empty.
#ifdef DEBUG
  else {
    NS_WARNING("Empty mCharset?  How come?");
  }
#endif

  if (!standalone.IsEmpty()) {
    aStr += NS_LITERAL_STRING(" standalone=\"") + standalone + endQuote;
  }

  aStr.AppendLiteral("?>");
  mAddNewlineForRootNode = PR_TRUE;

  return NS_OK;
}

PRBool
nsXMLContentSerializer::CheckElementStart(nsIContent * aContent,
                                          PRBool & aForceFormat,
                                          nsAString& aStr)
{
  aForceFormat = PR_FALSE;
  return PR_TRUE;
}

PRBool
nsXMLContentSerializer::CheckElementEnd(nsIContent * aContent,
                                        PRBool & aForceFormat,
                                        nsAString& aStr)
{
  // We don't output a separate end tag for empty element
  nsCOMPtr<nsIDOMNode> node(do_QueryInterface(aContent));
  PRBool hasChildren;
  aForceFormat = PR_FALSE;

  if (NS_SUCCEEDED(node->HasChildNodes(&hasChildren)) && !hasChildren) {
    return PR_FALSE;
  }
  return PR_TRUE;
}


void
nsXMLContentSerializer::AppendToString(const PRUnichar* aStr,
                                       PRInt32 aLength,
                                       nsAString& aOutputStr)
{
  PRInt32 length = (aLength == -1) ? nsCRT::strlen(aStr) : aLength;

  mColPos += length;

  aOutputStr.Append(aStr, length);
}

void 
nsXMLContentSerializer::AppendToString(const PRUnichar aChar,
                                       nsAString& aOutputStr)
{
  mColPos += 1;
  aOutputStr.Append(aChar);
}

void
nsXMLContentSerializer::AppendToString(const nsAString& aStr,
                                       nsAString& aOutputStr)
{
  mColPos += aStr.Length();
  aOutputStr.Append(aStr);
}


static const PRUint16 kGTVal = 62;
static const char* kEntities[] = {
  "", "", "", "", "", "", "", "", "", "",
  "", "", "", "", "", "", "", "", "", "",
  "", "", "", "", "", "", "", "", "", "",
  "", "", "", "", "", "", "", "", "&amp;", "",
  "", "", "", "", "", "", "", "", "", "",
  "", "", "", "", "", "", "", "", "", "",
  "&lt;", "", "&gt;"
};

static const char* kAttrEntities[] = {
  "", "", "", "", "", "", "", "", "", "",
  "", "", "", "", "", "", "", "", "", "",
  "", "", "", "", "", "", "", "", "", "",
  "", "", "", "", "&quot;", "", "", "", "&amp;", "",
  "", "", "", "", "", "", "", "", "", "",
  "", "", "", "", "", "", "", "", "", "",
  "&lt;", "", "&gt;"
};

void
nsXMLContentSerializer::AppendAndTranslateEntities(const nsAString& aStr,
                                                   nsAString& aOutputStr)
{
  nsReadingIterator<PRUnichar> done_reading;
  aStr.EndReading(done_reading);

  // for each chunk of |aString|...
  PRUint32 advanceLength = 0;
  nsReadingIterator<PRUnichar> iter;

  const char **entityTable = mInAttribute ? kAttrEntities : kEntities;

  for (aStr.BeginReading(iter);
       iter != done_reading;
       iter.advance(PRInt32(advanceLength))) {
    PRUint32 fragmentLength = iter.size_forward();
    const PRUnichar* c = iter.get();
    const PRUnichar* fragmentStart = c;
    const PRUnichar* fragmentEnd = c + fragmentLength;
    const char* entityText = nsnull;

    advanceLength = 0;
    // for each character in this chunk, check if it
    // needs to be replaced
    for (; c < fragmentEnd; c++, advanceLength++) {
      PRUnichar val = *c;
      if ((val <= kGTVal) && (entityTable[val][0] != 0)) {
        entityText = entityTable[val];
        break;
      }
    }

    aOutputStr.Append(fragmentStart, advanceLength);
    if (entityText) {
      AppendASCIItoUTF16(entityText, aOutputStr);
      advanceLength++;
    }
  }
}

void
nsXMLContentSerializer::MaybeAddNewlineForRootNode(nsAString& aStr)
{
  if (mAddNewlineForRootNode) {
    AppendNewLineToString(aStr);
  }
}

void
nsXMLContentSerializer::MaybeFlagNewlineForRootNode(nsIDOMNode* aNode)
{
  nsCOMPtr<nsIDOMNode> parent;
  aNode->GetParentNode(getter_AddRefs(parent));
  if (parent) {
    PRUint16 type;
    parent->GetNodeType(&type);
    mAddNewlineForRootNode = type == nsIDOMNode::DOCUMENT_NODE;
  }
}

void
nsXMLContentSerializer::MaybeEnterInPreContent(nsIContent* aNode)
{
  // support of the xml:space attribute
  if (aNode->HasAttr(kNameSpaceID_XML, nsGkAtoms::space)) {
    nsAutoString space;
    aNode->GetAttr(kNameSpaceID_XML, nsGkAtoms::space, space);
    if (space.EqualsLiteral("preserve"))
      ++mPreLevel;
  }
}

void
nsXMLContentSerializer::MaybeLeaveFromPreContent(nsIContent* aNode)
{
  // support of the xml:space attribute
  if (aNode->HasAttr(kNameSpaceID_XML, nsGkAtoms::space)) {
    nsAutoString space;
    aNode->GetAttr(kNameSpaceID_XML, nsGkAtoms::space, space);
    if (space.EqualsLiteral("preserve"))
      --mPreLevel;
  }
}

void
nsXMLContentSerializer::AppendNewLineToString(nsAString& aStr)
{
  AppendToString(mLineBreak, aStr);
  mMayIgnoreLineBreakSequence = PR_TRUE;
  mColPos = 0;
  mAddSpace = PR_FALSE;
  mIsIndentationAddedOnCurrentLine = PR_FALSE;
}

void
nsXMLContentSerializer::AppendIndentation(nsAString& aStr)
{
  mIsIndentationAddedOnCurrentLine = PR_TRUE;
  AppendToString(mIndent, aStr);
  mAddSpace = PR_FALSE;
  mMayIgnoreLineBreakSequence = PR_FALSE;
}

void
nsXMLContentSerializer::IncrIndentation(nsIAtom* aName)
{
  // we want to keep the source readable
  if(mDoWrap && mIndent.Length() >= mMaxColumn - MIN_INDENTED_LINE_LENGTH) {
    ++mIndentOverflow;
  }
  else {
    mIndent.AppendLiteral(INDENT_STRING);
  }
}

void
nsXMLContentSerializer::DecrIndentation(nsIAtom* aName)
{
  if(mIndentOverflow)
    --mIndentOverflow;
  else
    mIndent.Cut(0, INDENT_STRING_LENGTH);
}

PRBool
nsXMLContentSerializer::LineBreakBeforeOpen(PRInt32 aNamespaceID, nsIAtom* aName)
{
  return mAddSpace;
}

PRBool 
nsXMLContentSerializer::LineBreakAfterOpen(PRInt32 aNamespaceID, nsIAtom* aName)
{
  return PR_FALSE;
}

PRBool 
nsXMLContentSerializer::LineBreakBeforeClose(PRInt32 aNamespaceID, nsIAtom* aName)
{
  return mAddSpace;
}

PRBool 
nsXMLContentSerializer::LineBreakAfterClose(PRInt32 aNamespaceID, nsIAtom* aName)
{
  return PR_FALSE;
}

void
nsXMLContentSerializer::AppendToStringConvertLF(const nsAString& aStr,
                                                nsAString& aOutputStr)
{
  if (mDoRaw) {
    nsAutoString str (aStr);
    PRInt32 lastNewlineOffset = str.RFindChar('\n');
    AppendToString(aStr, aOutputStr);

    if (lastNewlineOffset != kNotFound) {
      // the string contains at least a line break,
      // so we should update the mColPos property with
      // the number of characters between the last line
      // break and the end of the string
      mColPos = aStr.Length() - lastNewlineOffset;
    }

    mIsIndentationAddedOnCurrentLine = (mColPos != 0);
  }
  else {
    // Convert line-endings to mLineBreak
    PRUint32 start = 0;
    PRUint32 theLen = aStr.Length();
    while (start < theLen) {
      PRInt32 eol = aStr.FindChar('\n', start);
      if (eol == kNotFound) {
        nsDependentSubstring dataSubstring(aStr, start, theLen - start);
        AppendToString(dataSubstring, aOutputStr);
        start = theLen;
        // if there was a line break before this substring
        // AppendNewLineToString was called, so we should reverse
        // this flag
        mMayIgnoreLineBreakSequence = PR_FALSE;
      }
      else {
        nsDependentSubstring dataSubstring(aStr, start, eol - start);
        AppendToString(dataSubstring, aOutputStr);
        AppendNewLineToString(aOutputStr);
        start = eol + 1;
      }
    }
  }
}

void
nsXMLContentSerializer::AppendFormatedWrapped_WhitespaceSequence(
                        nsASingleFragmentString::const_char_iterator &aPos,
                        const nsASingleFragmentString::const_char_iterator aEnd,
                        const nsASingleFragmentString::const_char_iterator aSequenceStart,
                        PRBool &aMayIgnoreStartOfLineWhitespaceSequence,
                        nsAString &aOutputStr)
{
  // Handle the complete sequence of whitespace.
  // Continue to iterate until we find the first non-whitespace char.
  // Updates "aPos" to point to the first unhandled char.
  // Also updates the aMayIgnoreStartOfLineWhitespaceSequence flag,
  // as well as the other "global" state flags.

  PRBool sawBlankOrTab = PR_FALSE;
  PRBool leaveLoop = PR_FALSE;

  do {
    switch (*aPos) {
      case ' ':
      case '\t':
        sawBlankOrTab = PR_TRUE;
        // no break
      case '\n':
        ++aPos;
        // do not increase mColPos,
        // because we will reduce the whitespace to a single char
        break;
      default:
        leaveLoop = PR_TRUE;
        break;
    }
  } while (!leaveLoop && aPos < aEnd);

  if (mAddSpace) {
    // if we had previously been asked to add space,
    // our situation has not changed
  }
  else if (!sawBlankOrTab && mMayIgnoreLineBreakSequence) {
    // nothing to do in the case where line breaks have already been added
    // before the call of AppendToStringWrapped
    // and only if we found line break in the sequence
    mMayIgnoreLineBreakSequence = PR_FALSE;
  }
  else if (aMayIgnoreStartOfLineWhitespaceSequence) {
    // nothing to do
    aMayIgnoreStartOfLineWhitespaceSequence = PR_FALSE;
  }
  else {
    if (sawBlankOrTab) {
      if (mDoWrap && mColPos + 1 >= mMaxColumn) {
        // no much sense in delaying, we only have one slot left,
        // let's write a break now
        aOutputStr.Append(mLineBreak);
        mColPos = 0;
        mIsIndentationAddedOnCurrentLine = PR_FALSE;
        mMayIgnoreLineBreakSequence = PR_TRUE;
      }
      else {
        // do not write out yet, we may write out either a space or a linebreak
        // let's delay writing it out until we know more
        mAddSpace = PR_TRUE;
        ++mColPos; // eat a slot of available space
      }
    }
    else {
      // Asian text usually does not contain spaces, therefore we should not
      // transform a linebreak into a space.
      // Since we only saw linebreaks, but no spaces or tabs,
      // let's write a linebreak now.
      AppendNewLineToString(aOutputStr);
    }
  }
}

void
nsXMLContentSerializer::AppendWrapped_NonWhitespaceSequence(
                        nsASingleFragmentString::const_char_iterator &aPos,
                        const nsASingleFragmentString::const_char_iterator aEnd,
                        const nsASingleFragmentString::const_char_iterator aSequenceStart,
                        PRBool &aMayIgnoreStartOfLineWhitespaceSequence,
                        PRBool &aSequenceStartAfterAWhiteSpace,
                        nsAString& aOutputStr)
{
  mMayIgnoreLineBreakSequence = PR_FALSE;
  aMayIgnoreStartOfLineWhitespaceSequence = PR_FALSE;

  // Handle the complete sequence of non-whitespace in this block
  // Iterate until we find the first whitespace char or an aEnd condition
  // Updates "aPos" to point to the first unhandled char.
  // Also updates the aMayIgnoreStartOfLineWhitespaceSequence flag,
  // as well as the other "global" state flags.

  PRBool thisSequenceStartsAtBeginningOfLine = !mColPos;
  PRBool onceAgainBecauseWeAddedBreakInFront = PR_FALSE;
  PRBool foundWhitespaceInLoop;
  PRInt32 length, colPos;

  do {

    if (mColPos) {
      colPos = mColPos;
    }
    else {
      if (mDoFormat && !mPreLevel && !onceAgainBecauseWeAddedBreakInFront) {
        colPos = mIndent.Length();
      }
      else
        colPos = 0;
    }
    foundWhitespaceInLoop = PR_FALSE;
    length = 0;
    // we iterate until the next whitespace character
    // or until we reach the maximum of character per line
    // or until the end of the string to add.
    do {
      if (*aPos == ' ' || *aPos == '\t' || *aPos == '\n') {
        foundWhitespaceInLoop = PR_TRUE;
        break;
      }

      ++aPos;
      ++length;
    } while ( (!mDoWrap || colPos + length < mMaxColumn) && aPos < aEnd);

    // in the case we don't reached the end of the string, but we reached the maxcolumn,
    // we see if there is a whitespace after the maxcolumn
    // if yes, then we can append directly the string instead of
    // appending a new line etc.
    if (*aPos == ' ' || *aPos == '\t' || *aPos == '\n') {
      foundWhitespaceInLoop = PR_TRUE;
    }

    if (aPos == aEnd || foundWhitespaceInLoop) {
      // there is enough room for the complete block we found
      if (mDoFormat && !mColPos) {
        AppendIndentation(aOutputStr);
      }
      else if (mAddSpace) {
        aOutputStr.Append(PRUnichar(' '));
        mAddSpace = PR_FALSE;
      }

      mColPos += length;
      aOutputStr.Append(aSequenceStart, aPos - aSequenceStart);

      // We have not yet reached the max column, we will continue to
      // fill the current line in the next outer loop iteration
      // (this one in AppendToStringWrapped)
      // make sure we return in this outer loop
      onceAgainBecauseWeAddedBreakInFront = PR_FALSE;
    }
    else { // we reach the max column
      if (!thisSequenceStartsAtBeginningOfLine &&
          (mAddSpace || (!mDoFormat && aSequenceStartAfterAWhiteSpace))) { 
          // when !mDoFormat, mAddSpace is not used, mAddSpace is always false
          // so, in the case where mDoWrap && !mDoFormat, if we want to enter in this condition...

        // We can avoid to wrap. We try to add the whole block 
        // in an empty new line

        AppendNewLineToString(aOutputStr);
        aPos = aSequenceStart;
        thisSequenceStartsAtBeginningOfLine = PR_TRUE;
        onceAgainBecauseWeAddedBreakInFront = PR_TRUE;
      }
      else {
        // we must wrap
        onceAgainBecauseWeAddedBreakInFront = PR_FALSE;
        PRBool foundWrapPosition = PR_FALSE;
        PRInt32 wrapPosition;

        nsILineBreaker *lineBreaker = nsContentUtils::LineBreaker();

        wrapPosition = lineBreaker->Prev(aSequenceStart,
                                         (aEnd - aSequenceStart),
                                         (aPos - aSequenceStart) + 1);
        if (wrapPosition != NS_LINEBREAKER_NEED_MORE_TEXT) {
          foundWrapPosition = PR_TRUE;
        }
        else {
          wrapPosition = lineBreaker->Next(aSequenceStart,
                                           (aEnd - aSequenceStart),
                                           (aPos - aSequenceStart));
          if (wrapPosition != NS_LINEBREAKER_NEED_MORE_TEXT) {
            foundWrapPosition = PR_TRUE;
          }
        }

        if (foundWrapPosition) {
          if (!mColPos && mDoFormat) {
            AppendIndentation(aOutputStr);
          }
          else if (mAddSpace) {
            aOutputStr.Append(PRUnichar(' '));
            mAddSpace = PR_FALSE;
          }
          aOutputStr.Append(aSequenceStart, wrapPosition);

          AppendNewLineToString(aOutputStr);
          aPos = aSequenceStart + wrapPosition;
          aMayIgnoreStartOfLineWhitespaceSequence = PR_TRUE;
        }
        else {
          // try some simple fallback logic
          // go forward up to the next whitespace position,
          // in the worst case this will be all the rest of the data

          // we update the mColPos variable with the length of
          // the part already parsed.
          mColPos += length;

          // now try to find the next whitespace
          do {
            if (*aPos == ' ' || *aPos == '\t' || *aPos == '\n') {
              break;
            }

            ++aPos;
            ++mColPos;
          } while (aPos < aEnd);

          if (mAddSpace) {
            aOutputStr.Append(PRUnichar(' '));
            mAddSpace = PR_FALSE;
          }
          aOutputStr.Append(aSequenceStart, aPos - aSequenceStart);
        }
      }
      aSequenceStartAfterAWhiteSpace = PR_FALSE;
    }
  } while (onceAgainBecauseWeAddedBreakInFront);
}

void 
nsXMLContentSerializer::AppendToStringFormatedWrapped(const nsASingleFragmentString& aStr,
                                               nsAString& aOutputStr)
{
  nsASingleFragmentString::const_char_iterator pos, end, sequenceStart;

  aStr.BeginReading(pos);
  aStr.EndReading(end);

  PRBool sequenceStartAfterAWhitespace = PR_FALSE;
  if (pos < end) {
    nsAString::const_char_iterator end2;
    aOutputStr.EndReading(end2);
    --end2;
    if (*end2 == ' ' || *end2 == '\n' || *end2 == '\t') {
      sequenceStartAfterAWhitespace = PR_TRUE;
    }
  }

  // if the current line already has text on it, such as a tag,
  // leading whitespace is significant
  PRBool mayIgnoreStartOfLineWhitespaceSequence =
    (!mColPos || (mIsIndentationAddedOnCurrentLine &&
                  sequenceStartAfterAWhitespace &&
                  mColPos == mIndent.Length()));

  while (pos < end) {
    sequenceStart = pos;

    // if beginning of a whitespace sequence
    if (*pos == ' ' || *pos == '\n' || *pos == '\t') {
      AppendFormatedWrapped_WhitespaceSequence(pos, end, sequenceStart,
        mayIgnoreStartOfLineWhitespaceSequence, aOutputStr);
    }
    else { // any other non-whitespace char
      AppendWrapped_NonWhitespaceSequence(pos, end, sequenceStart,
        mayIgnoreStartOfLineWhitespaceSequence, sequenceStartAfterAWhitespace, aOutputStr);
    }
  }
}

void
nsXMLContentSerializer::AppendWrapped_WhitespaceSequence(
                        nsASingleFragmentString::const_char_iterator &aPos,
                        const nsASingleFragmentString::const_char_iterator aEnd,
                        const nsASingleFragmentString::const_char_iterator aSequenceStart,
                        nsAString &aOutputStr)
{
  // Handle the complete sequence of whitespace.
  // Continue to iterate until we find the first non-whitespace char.
  // Updates "aPos" to point to the first unhandled char.
  mAddSpace = PR_FALSE;
  mIsIndentationAddedOnCurrentLine = PR_FALSE;

  PRBool leaveLoop = PR_FALSE;
  nsASingleFragmentString::const_char_iterator lastPos = aPos;

  do {
    switch (*aPos) {
      case ' ':
      case '\t':
        // if there are too many spaces on a line, we wrap
        if (mColPos >= mMaxColumn) {
          if (lastPos != aPos) {
            aOutputStr.Append(lastPos, aPos - lastPos);
          }
          AppendToString(mLineBreak, aOutputStr);
          mColPos = 0;
          lastPos = aPos;
        }

        ++mColPos;
        ++aPos;
        break;
      case '\n':
        if (lastPos != aPos) {
          aOutputStr.Append(lastPos, aPos - lastPos);
        }
        AppendToString(mLineBreak, aOutputStr);
        mColPos = 0;
        ++aPos;
        lastPos = aPos;
        break;
      default:
        leaveLoop = PR_TRUE;
        break;
    }
  } while (!leaveLoop && aPos < aEnd);

  if (lastPos != aPos) {
    aOutputStr.Append(lastPos, aPos - lastPos);
  }
}

void 
nsXMLContentSerializer::AppendToStringWrapped(const nsASingleFragmentString& aStr,
                                               nsAString& aOutputStr)
{
  nsASingleFragmentString::const_char_iterator pos, end, sequenceStart;

  aStr.BeginReading(pos);
  aStr.EndReading(end);

  // not used in this case, but needed by AppendWrapped_NonWhitespaceSequence
  PRBool mayIgnoreStartOfLineWhitespaceSequence = PR_FALSE;
  mMayIgnoreLineBreakSequence = PR_FALSE;

  PRBool sequenceStartAfterAWhitespace = PR_FALSE;
  if (pos < end) {
    nsAString::const_char_iterator end2;
    aOutputStr.EndReading(end2);
    --end2;
    if (*end2 == ' ' || *end2 == '\n' || *end2 == '\t') {
      sequenceStartAfterAWhitespace = PR_TRUE;
    }
  }

  while (pos < end) {
    sequenceStart = pos;

    // if beginning of a whitespace sequence
    if (*pos == ' ' || *pos == '\n' || *pos == '\t') {
      sequenceStartAfterAWhitespace = PR_TRUE;
      AppendWrapped_WhitespaceSequence(pos, end, sequenceStart, aOutputStr);
    }
    else { // any other non-whitespace char
      AppendWrapped_NonWhitespaceSequence(pos, end, sequenceStart,
        mayIgnoreStartOfLineWhitespaceSequence, sequenceStartAfterAWhitespace, aOutputStr);
    }
  }
}
