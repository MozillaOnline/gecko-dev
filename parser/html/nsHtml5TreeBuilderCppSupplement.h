/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 sw=2 et tw=78: */
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
 * The Original Code is Mozilla Communicator client code.
 *
 * The Initial Developer of the Original Code is
 * Netscape Communications Corporation.
 * Portions created by the Initial Developer are Copyright (C) 1998
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Pierre Phaneuf <pp@ludusdesign.com>
 *   Henri Sivonen <hsivonen@iki.fi>
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

#include "nsContentErrors.h"
#include "nsIPresShell.h"
#include "nsPresContext.h"
#include "nsEvent.h"
#include "nsGUIEvent.h"
#include "nsEventDispatcher.h"
#include "nsContentUtils.h"
#include "nsNodeUtils.h"
#include "nsHtml5SpeculativeLoader.h"

// this really should be autogenerated...
jArray<PRUnichar,PRInt32> nsHtml5TreeBuilder::ISINDEX_PROMPT = jArray<PRUnichar,PRInt32>();
nsHtml5TreeBuilder::nsHtml5TreeBuilder(nsAHtml5TreeOpSink* aOpSink,
                                       nsHtml5SpeculativeLoader* aSpeculativeLoader)
  : scriptingEnabled(PR_FALSE)
  , fragment(PR_FALSE)
  , contextNode(nsnull)
  , formPointer(nsnull)
  , headPointer(nsnull)
  , mOpSink(aOpSink)
  , mHandles(new nsIContent*[NS_HTML5_TREE_BUILDER_HANDLE_ARRAY_LENGTH])
  , mHandlesUsed(0)
  , mSpeculativeLoader(aSpeculativeLoader)
  , mCurrentHtmlScriptIsAsyncOrDefer(PR_FALSE)
#ifdef DEBUG
  , mActive(PR_FALSE)
#endif
{
  MOZ_COUNT_CTOR(nsHtml5TreeBuilder);
}

nsHtml5TreeBuilder::~nsHtml5TreeBuilder()
{
  MOZ_COUNT_DTOR(nsHtml5TreeBuilder);
  NS_ASSERTION(!mActive, "nsHtml5TreeBuilder deleted without ever calling end() on it!");
  mOpQueue.Clear();
}

class nsHtml5SpeculativeScript : public nsRunnable
{
private:
  nsRefPtr<nsHtml5SpeculativeLoader> mSpeculativeLoader;
  nsString                           mURL;
  nsString                           mCharset;
  nsString                           mType;
public:
  nsHtml5SpeculativeScript(nsHtml5SpeculativeLoader* aSpeculativeLoader,
                           const nsAString& aURL,
                           const nsAString& aCharset,
                           const nsAString& aType)
    : mSpeculativeLoader(aSpeculativeLoader)
    , mURL(aURL)
    , mCharset(aCharset)
    , mType(aType)
  {}
  NS_IMETHODIMP Run()
  {
    mSpeculativeLoader->PreloadScript(mURL, mCharset, mType);
    return NS_OK;
  }
};

class nsHtml5SpeculativeStyle : public nsRunnable
{
private:
  nsRefPtr<nsHtml5SpeculativeLoader> mSpeculativeLoader;
  nsString                           mURL;
  nsString                           mCharset;
public:
  nsHtml5SpeculativeStyle(nsHtml5SpeculativeLoader* aSpeculativeLoader,
                          const nsAString& aURL,
                          const nsAString& aCharset)
    : mSpeculativeLoader(aSpeculativeLoader)
    , mURL(aURL)
    , mCharset(aCharset)
  {}
  NS_IMETHODIMP Run()
  {
    mSpeculativeLoader->PreloadStyle(mURL, mCharset);
    return NS_OK;
  }
};

class nsHtml5SpeculativeImage : public nsRunnable
{
private:
  nsRefPtr<nsHtml5SpeculativeLoader> mSpeculativeLoader;
  nsString                           mURL;
public:
  nsHtml5SpeculativeImage(nsHtml5SpeculativeLoader* aSpeculativeLoader,
                          const nsAString& aURL)
    : mSpeculativeLoader(aSpeculativeLoader)
    , mURL(aURL)
  {}
  NS_IMETHODIMP Run()
  {
    mSpeculativeLoader->PreloadImage(mURL);
    return NS_OK;
  }
};

class nsHtml5SpeculativeManifest : public nsRunnable
{
private:
  nsRefPtr<nsHtml5SpeculativeLoader> mSpeculativeLoader;
  nsString                           mURL;
public:
  nsHtml5SpeculativeManifest(nsHtml5SpeculativeLoader* aSpeculativeLoader,
                          const nsAString& aURL)
    : mSpeculativeLoader(aSpeculativeLoader)
    , mURL(aURL)
  {}
  NS_IMETHODIMP Run()
  {
    mSpeculativeLoader->ProcessManifest(mURL);
    return NS_OK;
  }
};

nsIContent**
nsHtml5TreeBuilder::createElement(PRInt32 aNamespace, nsIAtom* aName, nsHtml5HtmlAttributes* aAttributes)
{
  NS_PRECONDITION(aAttributes, "Got null attributes.");
  NS_PRECONDITION(aName, "Got null name.");
  NS_PRECONDITION(aNamespace == kNameSpaceID_XHTML || 
                  aNamespace == kNameSpaceID_SVG || 
                  aNamespace == kNameSpaceID_MathML,
                  "Bogus namespace.");

  nsIContent** content = AllocateContentHandle();
  nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
  NS_ASSERTION(treeOp, "Tree op allocation failed.");
  treeOp->Init(aNamespace, aName, aAttributes, content);
  
  // Start wall of code for speculative loading and line numbers
  
  if (mSpeculativeLoader) {
    switch (aNamespace) {
      case kNameSpaceID_XHTML:
        if (nsHtml5Atoms::img == aName) {
          nsString* url = aAttributes->getValue(nsHtml5AttributeName::ATTR_SRC);
          if (url) {
            Dispatch(new nsHtml5SpeculativeImage(mSpeculativeLoader, *url));
          }
        } else if (nsHtml5Atoms::script == aName) {
          nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
          NS_ASSERTION(treeOp, "Tree op allocation failed.");
          treeOp->Init(eTreeOpSetScriptLineNumberAndFreeze, content, tokenizer->getLineNumber());

          nsString* url = aAttributes->getValue(nsHtml5AttributeName::ATTR_SRC);
          if (url) {
            nsString* charset = aAttributes->getValue(nsHtml5AttributeName::ATTR_CHARSET);
            nsString* type = aAttributes->getValue(nsHtml5AttributeName::ATTR_TYPE);
            Dispatch(new nsHtml5SpeculativeScript(mSpeculativeLoader, 
                                                  *url,
                                                  (charset) ? *charset : EmptyString(),
                                                  (type) ? *type : EmptyString()));
            mCurrentHtmlScriptIsAsyncOrDefer = 
              aAttributes->contains(nsHtml5AttributeName::ATTR_ASYNC) ||
              aAttributes->contains(nsHtml5AttributeName::ATTR_DEFER);
          }
        } else if (nsHtml5Atoms::link == aName) {
          nsString* rel = aAttributes->getValue(nsHtml5AttributeName::ATTR_REL);
          // Not splitting on space here is bogus but the old parser didn't even
          // do a case-insensitive check.
          if (rel && rel->LowerCaseEqualsASCII("stylesheet")) {
            nsString* url = aAttributes->getValue(nsHtml5AttributeName::ATTR_HREF);
            if (url) {
              nsString* charset = aAttributes->getValue(nsHtml5AttributeName::ATTR_CHARSET);
              Dispatch(new nsHtml5SpeculativeStyle(mSpeculativeLoader, 
                                                   *url,
                                                   (charset) ? *charset : EmptyString()));
            }
          }
        } else if (nsHtml5Atoms::video == aName) {
          nsString* url = aAttributes->getValue(nsHtml5AttributeName::ATTR_POSTER);
          if (url) {
            Dispatch(new nsHtml5SpeculativeImage(mSpeculativeLoader, *url));
          }
        } else if (nsHtml5Atoms::style == aName) {
          nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
          NS_ASSERTION(treeOp, "Tree op allocation failed.");
          treeOp->Init(eTreeOpSetStyleLineNumber, content, tokenizer->getLineNumber());
        } else if (nsHtml5Atoms::html == aName) {
          nsString* url = aAttributes->getValue(nsHtml5AttributeName::ATTR_MANIFEST);
          if (url) {
            Dispatch(new nsHtml5SpeculativeManifest(mSpeculativeLoader, *url));
          }
        }
        break;
      case kNameSpaceID_SVG:
        if (nsHtml5Atoms::image == aName) {
          nsString* url = aAttributes->getValue(nsHtml5AttributeName::ATTR_XLINK_HREF);
          if (url) {
            Dispatch(new nsHtml5SpeculativeImage(mSpeculativeLoader, *url));
          }
        } else if (nsHtml5Atoms::script == aName) {
          nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
          NS_ASSERTION(treeOp, "Tree op allocation failed.");
          treeOp->Init(eTreeOpSetScriptLineNumberAndFreeze, content, tokenizer->getLineNumber());

          nsString* url = aAttributes->getValue(nsHtml5AttributeName::ATTR_XLINK_HREF);
          if (url) {
            nsString* type = aAttributes->getValue(nsHtml5AttributeName::ATTR_TYPE);
            Dispatch(new nsHtml5SpeculativeScript(mSpeculativeLoader, 
                                                  *url,
                                                  EmptyString(),
                                                  (type) ? *type : EmptyString()));
          }
        } else if (nsHtml5Atoms::style == aName) {
          nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
          NS_ASSERTION(treeOp, "Tree op allocation failed.");
          treeOp->Init(eTreeOpSetStyleLineNumber, content, tokenizer->getLineNumber());

          nsString* url = aAttributes->getValue(nsHtml5AttributeName::ATTR_XLINK_HREF);
          if (url) {
            Dispatch(new nsHtml5SpeculativeStyle(mSpeculativeLoader, 
                                                 *url,
                                                 EmptyString()));
          }
        }        
        break;
    }
  } else if (aNamespace != kNameSpaceID_MathML) {
    // No speculative loader--just line numbers and defer/async check
    if (nsHtml5Atoms::style == aName) {
      nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
      NS_ASSERTION(treeOp, "Tree op allocation failed.");
      treeOp->Init(eTreeOpSetStyleLineNumber, content, tokenizer->getLineNumber());
    } else if (nsHtml5Atoms::script == aName) {
      nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
      NS_ASSERTION(treeOp, "Tree op allocation failed.");
      treeOp->Init(eTreeOpSetScriptLineNumberAndFreeze, content, tokenizer->getLineNumber());
      if (aNamespace == kNameSpaceID_XHTML) {
        mCurrentHtmlScriptIsAsyncOrDefer = 
          aAttributes->contains(nsHtml5AttributeName::ATTR_SRC) &&
          (aAttributes->contains(nsHtml5AttributeName::ATTR_ASYNC) ||
           aAttributes->contains(nsHtml5AttributeName::ATTR_DEFER));
      }
    } else if (aNamespace == kNameSpaceID_XHTML && nsHtml5Atoms::html == aName) {
      nsString* url = aAttributes->getValue(nsHtml5AttributeName::ATTR_MANIFEST);
      if (url) {
        nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
        NS_ASSERTION(treeOp, "Tree op allocation failed.");
        treeOp->Init(eTreeOpProcessOfflineManifest, *url);
      }
    }
  }

  // End wall of code for speculative loading
  
  return content;
}

nsIContent**
nsHtml5TreeBuilder::createElement(PRInt32 aNamespace, nsIAtom* aName, nsHtml5HtmlAttributes* aAttributes, nsIContent** aFormElement)
{
  nsIContent** content = createElement(aNamespace, aName, aAttributes);
  if (aFormElement) {
    nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
    NS_ASSERTION(treeOp, "Tree op allocation failed.");
    treeOp->Init(eTreeOpSetFormElement, content, aFormElement);
  }
  return content;
}

nsIContent**
nsHtml5TreeBuilder::createHtmlElementSetAsRoot(nsHtml5HtmlAttributes* aAttributes)
{
  nsIContent** content = createElement(kNameSpaceID_XHTML, nsHtml5Atoms::html, aAttributes);
  nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
  NS_ASSERTION(treeOp, "Tree op allocation failed.");
  treeOp->Init(eTreeOpAppendToDocument, content);
  return content;
}

void
nsHtml5TreeBuilder::detachFromParent(nsIContent** aElement)
{
  NS_PRECONDITION(aElement, "Null element");

  nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
  NS_ASSERTION(treeOp, "Tree op allocation failed.");
  treeOp->Init(eTreeOpDetach, aElement);
}

void
nsHtml5TreeBuilder::appendElement(nsIContent** aChild, nsIContent** aParent)
{
  NS_PRECONDITION(aChild, "Null child");
  NS_PRECONDITION(aParent, "Null parent");

  nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
  NS_ASSERTION(treeOp, "Tree op allocation failed.");
  treeOp->Init(eTreeOpAppend, aChild, aParent);
}

void
nsHtml5TreeBuilder::appendChildrenToNewParent(nsIContent** aOldParent, nsIContent** aNewParent)
{
  NS_PRECONDITION(aOldParent, "Null old parent");
  NS_PRECONDITION(aNewParent, "Null new parent");

  nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
  NS_ASSERTION(treeOp, "Tree op allocation failed.");
  treeOp->Init(eTreeOpAppendChildrenToNewParent, aOldParent, aNewParent);
}

void
nsHtml5TreeBuilder::insertFosterParentedCharacters(PRUnichar* aBuffer, PRInt32 aStart, PRInt32 aLength, nsIContent** aTable, nsIContent** aStackParent)
{
  NS_PRECONDITION(aBuffer, "Null buffer");
  NS_PRECONDITION(aTable, "Null table");
  NS_PRECONDITION(aStackParent, "Null stack parent");

  PRUnichar* bufferCopy = new PRUnichar[aLength];
  memcpy(bufferCopy, aBuffer, aLength * sizeof(PRUnichar));
  
  nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
  NS_ASSERTION(treeOp, "Tree op allocation failed.");
  treeOp->Init(eTreeOpFosterParentText, bufferCopy, aLength, aStackParent, aTable);
}

void
nsHtml5TreeBuilder::insertFosterParentedChild(nsIContent** aChild, nsIContent** aTable, nsIContent** aStackParent)
{
  NS_PRECONDITION(aChild, "Null child");
  NS_PRECONDITION(aTable, "Null table");
  NS_PRECONDITION(aStackParent, "Null stack parent");

  nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
  NS_ASSERTION(treeOp, "Tree op allocation failed.");
  treeOp->Init(eTreeOpFosterParent, aChild, aStackParent, aTable);
}

void
nsHtml5TreeBuilder::appendCharacters(nsIContent** aParent, PRUnichar* aBuffer, PRInt32 aStart, PRInt32 aLength)
{
  NS_PRECONDITION(aBuffer, "Null buffer");
  NS_PRECONDITION(aParent, "Null parent");

  PRUnichar* bufferCopy = new PRUnichar[aLength];
  memcpy(bufferCopy, aBuffer, aLength * sizeof(PRUnichar));
  
  nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
  NS_ASSERTION(treeOp, "Tree op allocation failed.");
  treeOp->Init(eTreeOpAppendText, bufferCopy, aLength, aParent);
}

void
nsHtml5TreeBuilder::appendComment(nsIContent** aParent, PRUnichar* aBuffer, PRInt32 aStart, PRInt32 aLength)
{
  NS_PRECONDITION(aBuffer, "Null buffer");
  NS_PRECONDITION(aParent, "Null parent");

  PRUnichar* bufferCopy = new PRUnichar[aLength];
  memcpy(bufferCopy, aBuffer, aLength * sizeof(PRUnichar));
  
  nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
  NS_ASSERTION(treeOp, "Tree op allocation failed.");
  treeOp->Init(eTreeOpAppendComment, bufferCopy, aLength, aParent);
}

void
nsHtml5TreeBuilder::appendCommentToDocument(PRUnichar* aBuffer, PRInt32 aStart, PRInt32 aLength)
{
  NS_PRECONDITION(aBuffer, "Null buffer");

  PRUnichar* bufferCopy = new PRUnichar[aLength];
  memcpy(bufferCopy, aBuffer, aLength * sizeof(PRUnichar));
  
  nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
  NS_ASSERTION(treeOp, "Tree op allocation failed.");
  treeOp->Init(eTreeOpAppendCommentToDocument, bufferCopy, aLength);
}

void
nsHtml5TreeBuilder::addAttributesToElement(nsIContent** aElement, nsHtml5HtmlAttributes* aAttributes)
{
  NS_PRECONDITION(aElement, "Null element");
  NS_PRECONDITION(aAttributes, "Null attributes");

  if (aAttributes == nsHtml5HtmlAttributes::EMPTY_ATTRIBUTES) {
    return;
  }
  nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
  NS_ASSERTION(treeOp, "Tree op allocation failed.");
  treeOp->Init(aElement, aAttributes);
}

void
nsHtml5TreeBuilder::markMalformedIfScript(nsIContent** aElement)
{
  NS_PRECONDITION(aElement, "Null element");

  nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
  NS_ASSERTION(treeOp, "Tree op allocation failed.");
  treeOp->Init(eTreeOpMarkMalformedIfScript, aElement);
}

void
nsHtml5TreeBuilder::start(PRBool fragment)
{
  mCurrentHtmlScriptIsAsyncOrDefer = PR_FALSE;
#ifdef DEBUG
  mActive = PR_TRUE;
#endif
}

void
nsHtml5TreeBuilder::end()
{
  mOpQueue.Clear();
#ifdef DEBUG
  mActive = PR_FALSE;
#endif
}

void
nsHtml5TreeBuilder::appendDoctypeToDocument(nsIAtom* aName, nsString* aPublicId, nsString* aSystemId)
{
  NS_PRECONDITION(aName, "Null name");

  nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
  NS_ASSERTION(treeOp, "Tree op allocation failed.");
  treeOp->Init(aName, *aPublicId, *aSystemId);
  // nsXMLContentSink can flush here, but what's the point?
  // It can also interrupt here, but we can't.
}

void
nsHtml5TreeBuilder::elementPushed(PRInt32 aNamespace, nsIAtom* aName, nsIContent** aElement)
{
}

void
nsHtml5TreeBuilder::elementPopped(PRInt32 aNamespace, nsIAtom* aName, nsIContent** aElement)
{
  NS_ASSERTION(aNamespace == kNameSpaceID_XHTML || aNamespace == kNameSpaceID_SVG || aNamespace == kNameSpaceID_MathML, "Element isn't HTML, SVG or MathML!");
  NS_ASSERTION(aName, "Element doesn't have local name!");
  NS_ASSERTION(aElement, "No element!");
  if (aNamespace == kNameSpaceID_MathML) {
    return;
  }
  // we now have only SVG and HTML
  if (aName == nsHtml5Atoms::script) {
    if (mCurrentHtmlScriptIsAsyncOrDefer) {
      NS_ASSERTION(aNamespace == kNameSpaceID_XHTML, 
                   "Only HTML scripts may be async/defer.");
      nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
      NS_ASSERTION(treeOp, "Tree op allocation failed.");
      treeOp->Init(eTreeOpRunScriptAsyncDefer, aElement);      
      mCurrentHtmlScriptIsAsyncOrDefer = PR_FALSE;
      return;
    }
    requestSuspension();
    nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
    NS_ASSERTION(treeOp, "Tree op allocation failed.");
    treeOp->InitScript(aElement);
    return;
  }
  if (aName == nsHtml5Atoms::title) {
    nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
    NS_ASSERTION(treeOp, "Tree op allocation failed.");
    treeOp->Init(eTreeOpDoneAddingChildren, aElement);
    return;
  }
  if (aName == nsHtml5Atoms::style || (aNamespace == kNameSpaceID_XHTML && aName == nsHtml5Atoms::link)) {
    nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
    NS_ASSERTION(treeOp, "Tree op allocation failed.");
    treeOp->Init(eTreeOpUpdateStyleSheet, aElement);
    return;
  }
  if (aNamespace == kNameSpaceID_SVG) {
#if 0
    if (aElement->HasAttr(kNameSpaceID_None, nsHtml5Atoms::onload)) {
      nsEvent event(PR_TRUE, NS_SVG_LOAD);
      event.eventStructType = NS_SVG_EVENT;
      event.flags |= NS_EVENT_FLAG_CANT_BUBBLE;
      // Do we care about forcing presshell creation if it hasn't happened yet?
      // That is, should this code flush or something?  Does it really matter?
      // For that matter, do we really want to try getting the prescontext?  Does
      // this event ever want one?
      nsRefPtr<nsPresContext> ctx;
      nsCOMPtr<nsIPresShell> shell = parser->GetDocument()->GetPrimaryShell();
      if (shell) {
        ctx = shell->GetPresContext();
      }
      nsEventDispatcher::Dispatch(aElement, ctx, &event);
    }
#endif
    // TODO soft flush the op queue every now and then
    return;
  }
  // we now have only HTML
  // Some HTML nodes need DoneAddingChildren() called to initialize
  // properly (e.g. form state restoration).
  // XXX expose ElementName group here and do switch
  if (aName == nsHtml5Atoms::video ||
      aName == nsHtml5Atoms::audio ||
      aName == nsHtml5Atoms::object ||
      aName == nsHtml5Atoms::applet) {
    nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
    NS_ASSERTION(treeOp, "Tree op allocation failed.");
    treeOp->Init(eTreeOpDoneAddingChildren, aElement);
    return;
  }
  if (aName == nsHtml5Atoms::select || 
      aName == nsHtml5Atoms::textarea) {
    if (!formPointer) {
      // If form inputs don't belong to a form, their state preservation
      // won't work right without an append notification flush at this 
      // point. See bug 497861 and bug 539895.
      nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
      NS_ASSERTION(treeOp, "Tree op allocation failed.");
      treeOp->Init(eTreeOpFlushPendingAppendNotifications);
    }
    nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
    NS_ASSERTION(treeOp, "Tree op allocation failed.");
    treeOp->Init(eTreeOpDoneAddingChildren, aElement);
    return;
  }
  if (aName == nsHtml5Atoms::input ||
      aName == nsHtml5Atoms::button) {
    if (!formPointer) {
      // If form inputs don't belong to a form, their state preservation
      // won't work right without an append notification flush at this 
      // point. See bug 497861.
      nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
      NS_ASSERTION(treeOp, "Tree op allocation failed.");
      treeOp->Init(eTreeOpFlushPendingAppendNotifications);
    }
    nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
    NS_ASSERTION(treeOp, "Tree op allocation failed.");
    treeOp->Init(eTreeOpDoneCreatingElement, aElement);
    return;
  }
  if (aName == nsHtml5Atoms::base) {
    nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
    NS_ASSERTION(treeOp, "Tree op allocation failed.");
    treeOp->Init(eTreeOpProcessBase, aElement);
    return;
  }
  if (aName == nsHtml5Atoms::meta) {
    nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
    NS_ASSERTION(treeOp, "Tree op allocation failed.");
    treeOp->Init(eTreeOpProcessMeta, aElement);
    return;
  }
  if (aName == nsHtml5Atoms::head) {
    nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
    NS_ASSERTION(treeOp, "Tree op allocation failed.");
    treeOp->Init(eTreeOpStartLayout);
    return;
  }
  return;
}

void
nsHtml5TreeBuilder::accumulateCharacters(PRUnichar* aBuf, PRInt32 aStart, PRInt32 aLength)
{
  PRInt32 newFillLen = charBufferLen + aLength;
  if (newFillLen > charBuffer.length) {
    PRInt32 newAllocLength = newFillLen + (newFillLen >> 1);
    jArray<PRUnichar,PRInt32> newBuf(newAllocLength);
    memcpy(newBuf, charBuffer, sizeof(PRUnichar) * charBufferLen);
    charBuffer.release();
    charBuffer = newBuf;
  }
  memcpy(charBuffer + charBufferLen, aBuf + aStart, sizeof(PRUnichar) * aLength);
  charBufferLen = newFillLen;
}

nsIContent**
nsHtml5TreeBuilder::AllocateContentHandle()
{
  if (mHandlesUsed == NS_HTML5_TREE_BUILDER_HANDLE_ARRAY_LENGTH) {
    mOldHandles.AppendElement(mHandles.forget());
    mHandles = new nsIContent*[NS_HTML5_TREE_BUILDER_HANDLE_ARRAY_LENGTH];
    mHandlesUsed = 0;
  }
#ifdef DEBUG
  mHandles[mHandlesUsed] = (nsIContent*)0xC0DEDBAD;
#endif
  return &mHandles[mHandlesUsed++];
}

PRBool
nsHtml5TreeBuilder::HasScript()
{
  PRUint32 len = mOpQueue.Length();
  if (!len) {
    return PR_FALSE;
  }
  return mOpQueue.ElementAt(len - 1).IsRunScript();
}

PRBool
nsHtml5TreeBuilder::Flush()
{
  PRBool hasOps = !mOpQueue.IsEmpty();
  if (hasOps) {
    mOpSink->MoveOpsFrom(mOpQueue);
  }
  return hasOps;
}

void
nsHtml5TreeBuilder::SetDocumentCharset(nsACString& aCharset)
{
  nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
  NS_ASSERTION(treeOp, "Tree op allocation failed.");
  treeOp->Init(eTreeOpSetDocumentCharset, aCharset);  
}

void
nsHtml5TreeBuilder::StreamEnded()
{
  // The fragment mode calls DidBuildModel from nsHtml5Parser. 
  // Letting DidBuildModel be called from the executor in the fragment case
  // confuses the EndLoad logic of nsHTMLDocument, since nsHTMLDocument
  // thinks it is dealing with document.written content as opposed to 
  // innerHTML content.
  if (!fragment) {
    nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
    NS_ASSERTION(treeOp, "Tree op allocation failed.");
    treeOp->Init(eTreeOpStreamEnded);
  }
}

void
nsHtml5TreeBuilder::NeedsCharsetSwitchTo(const nsACString& aCharset)
{
  nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
  NS_ASSERTION(treeOp, "Tree op allocation failed.");
  treeOp->Init(eTreeOpNeedsCharsetSwitchTo, aCharset);  
}

void
nsHtml5TreeBuilder::AddSnapshotToScript(nsAHtml5TreeBuilderState* aSnapshot, PRInt32 aLine)
{
  NS_PRECONDITION(HasScript(), "No script to add a snapshot to!");
  NS_PRECONDITION(aSnapshot, "Got null snapshot.");
  mOpQueue.ElementAt(mOpQueue.Length() - 1).SetSnapshot(aSnapshot, aLine);
}

void
nsHtml5TreeBuilder::DropSpeculativeLoader() {
  mSpeculativeLoader = nsnull;
}

PRBool 
nsHtml5TreeBuilder::IsDiscretionaryFlushSafe()
{
  return !(charBufferLen && 
           currentPtr >= 0 && 
           stack[currentPtr]->fosterParenting);
}

// DocumentModeHandler
void
nsHtml5TreeBuilder::documentMode(nsHtml5DocumentMode m)
{
  nsHtml5TreeOperation* treeOp = mOpQueue.AppendElement();
  NS_ASSERTION(treeOp, "Tree op allocation failed.");
  treeOp->Init(m);
}
