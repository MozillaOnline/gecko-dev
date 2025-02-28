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
 * Kyle Huey <me@kylehuey.com>
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

#include <shlobj.h>

#include "nsDataObjCollection.h"
#include "nsClipboard.h"
#include "IEnumFE.h"

#include <ole2.h>

/*
 * Class nsDataObjCollection
 */

nsDataObjCollection::nsDataObjCollection()
  : m_cRef(0), mIsAsyncMode(FALSE), mIsInOperation(FALSE)
{
  m_enumFE = new CEnumFormatEtc();
  m_enumFE->AddRef();
}

nsDataObjCollection::~nsDataObjCollection()
{
  mDataFlavors.Clear();
  mDataObjects.Clear();

  m_enumFE->Release();
}


// IUnknown interface methods - see iunknown.h for documentation
STDMETHODIMP nsDataObjCollection::QueryInterface(REFIID riid, void** ppv)
{
  *ppv=NULL;

  if ( (IID_IUnknown == riid) || (IID_IDataObject  == riid) ) {
    *ppv = static_cast<IDataObject*>(this); 
    AddRef();
    return NOERROR;
  }

  if ( IID_IDataObjCollection  == riid ) {
    *ppv = static_cast<nsIDataObjCollection*>(this); 
    AddRef();
    return NOERROR;
  }

  return E_NOINTERFACE;
}

STDMETHODIMP_(ULONG) nsDataObjCollection::AddRef()
{
  return ++m_cRef;
}

STDMETHODIMP_(ULONG) nsDataObjCollection::Release()
{
  if (0 != --m_cRef)
    return m_cRef;

  delete this;

  return 0;
}

BOOL nsDataObjCollection::FormatsMatch(const FORMATETC& source,
                                       const FORMATETC& target) const
{
  if ((source.cfFormat == target.cfFormat) &&
      (source.dwAspect & target.dwAspect)  &&
      (source.tymed    & target.tymed)) {
    return TRUE;
  } else {
    return FALSE;
  }
}

// IDataObject methods
STDMETHODIMP nsDataObjCollection::GetData(LPFORMATETC pFE, LPSTGMEDIUM pSTM)
{
  static CLIPFORMAT fileDescriptorFlavorA =
                               ::RegisterClipboardFormat(CFSTR_FILEDESCRIPTORA);
  static CLIPFORMAT fileDescriptorFlavorW =
                               ::RegisterClipboardFormat(CFSTR_FILEDESCRIPTORW);
  static CLIPFORMAT fileFlavor = ::RegisterClipboardFormat(CFSTR_FILECONTENTS);

  switch (pFE->cfFormat) {
  case CF_TEXT:
  case CF_UNICODETEXT:
    return GetText(pFE, pSTM);
  case CF_HDROP:
    return GetFile(pFE, pSTM);
  default:
    if (pFE->cfFormat == fileDescriptorFlavorA ||
        pFE->cfFormat == fileDescriptorFlavorW) {
      return GetFileDescriptors(pFE, pSTM);
    }
    if (pFE->cfFormat == fileFlavor) {
      return GetFileContents(pFE, pSTM);
    }
  }
  return GetFirstSupporting(pFE, pSTM);
}

STDMETHODIMP nsDataObjCollection::GetDataHere(LPFORMATETC pFE, LPSTGMEDIUM pSTM)
{
  return E_FAIL;
}

// Other objects querying to see if we support a particular format
STDMETHODIMP nsDataObjCollection::QueryGetData(LPFORMATETC pFE)
{
  UINT format = nsClipboard::GetFormat(MULTI_MIME);

  if (format == pFE->cfFormat) {
    return S_OK;
  }

  for (PRUint32 i = 0; i < mDataObjects.Length(); ++i) {
    IDataObject * dataObj = mDataObjects.ElementAt(i);
    if (S_OK == dataObj->QueryGetData(pFE)) {
      return S_OK;
    }
  }

  return DV_E_FORMATETC;
}

STDMETHODIMP nsDataObjCollection::GetCanonicalFormatEtc(LPFORMATETC pFEIn,
                                                        LPFORMATETC pFEOut)
{
  return E_NOTIMPL;
}

STDMETHODIMP nsDataObjCollection::SetData(LPFORMATETC pFE,
                                          LPSTGMEDIUM pSTM,
                                          BOOL fRelease)
{
  // Set arbitrary data formats on the first object in the collection and let
  // it handle the heavy lifting
  if (mDataObjects.Length() == 0)
    return E_FAIL;
  return mDataObjects.ElementAt(0)->SetData(pFE, pSTM, fRelease);
}

STDMETHODIMP nsDataObjCollection::EnumFormatEtc(DWORD dwDir,
                                                LPENUMFORMATETC *ppEnum)
{
  if (dwDir == DATADIR_GET) {
    // Clone addref's the new enumerator.
    m_enumFE->Clone(ppEnum);
    if (!(*ppEnum))
      return E_FAIL;
    (*ppEnum)->Reset();
    return S_OK;
  }

  return E_NOTIMPL;
}

STDMETHODIMP nsDataObjCollection::DAdvise(LPFORMATETC pFE,
                                          DWORD dwFlags,
                                          LPADVISESINK pIAdviseSink,
                                          DWORD* pdwConn)
{
  return OLE_E_ADVISENOTSUPPORTED;
}

STDMETHODIMP nsDataObjCollection::DUnadvise(DWORD dwConn)
{
  return OLE_E_ADVISENOTSUPPORTED;
}

STDMETHODIMP nsDataObjCollection::EnumDAdvise(LPENUMSTATDATA *ppEnum)
{
  return OLE_E_ADVISENOTSUPPORTED;
}

// GetData and SetData helper functions
HRESULT nsDataObjCollection::AddSetFormat(FORMATETC& aFE)
{
  return S_OK;
}

HRESULT nsDataObjCollection::AddGetFormat(FORMATETC& aFE)
{
  return S_OK;
}

// Registers a DataFlavor/FE pair
void nsDataObjCollection::AddDataFlavor(const char * aDataFlavor,
                                        LPFORMATETC aFE)
{
  // Add the FormatEtc to our list if it's not already there.  We don't care
  // about the internal aDataFlavor because nsDataObj handles that.
  IEnumFORMATETC * ifEtc;
  FORMATETC fEtc;
  ULONG num;
  if (S_OK != this->EnumFormatEtc(DATADIR_GET, &ifEtc))
    return;
  while (S_OK == ifEtc->Next(1, &fEtc, &num)) {
    NS_ASSERTION(1 == num,
         "Bit off more than we can chew in nsDataObjCollection::AddDataFlavor");
    if (FormatsMatch(fEtc, *aFE)) {
      ifEtc->Release();
      return;
    }
  } // If we didn't find a matching format, add this one
  ifEtc->Release();
  m_enumFE->AddFormatEtc(aFE);
}

// We accept ownership of the nsDataObj which we free on destruction
void nsDataObjCollection::AddDataObject(IDataObject * aDataObj)
{
  nsDataObj* dataObj = reinterpret_cast<nsDataObj*>(aDataObj);
  mDataObjects.AppendElement(dataObj);
}

// IAsyncOperation methods
STDMETHODIMP nsDataObjCollection::EndOperation(HRESULT hResult,
                                               IBindCtx *pbcReserved,
                                               DWORD dwEffects)
{
  mIsInOperation = FALSE;
  Release();
  return S_OK;
}

STDMETHODIMP nsDataObjCollection::GetAsyncMode(BOOL *pfIsOpAsync)
{
  if (!pfIsOpAsync)
    return E_FAIL;

  *pfIsOpAsync = mIsAsyncMode;

  return S_OK;
}

STDMETHODIMP nsDataObjCollection::InOperation(BOOL *pfInAsyncOp)
{
  if (!pfInAsyncOp)
    return E_FAIL;

  *pfInAsyncOp = mIsInOperation;

  return S_OK;
}

STDMETHODIMP nsDataObjCollection::SetAsyncMode(BOOL fDoOpAsync)
{
  mIsAsyncMode = fDoOpAsync;
  return S_OK;
}

STDMETHODIMP nsDataObjCollection::StartOperation(IBindCtx *pbcReserved)
{
  mIsInOperation = TRUE;
  return S_OK;
}

// Methods for getting data
HRESULT nsDataObjCollection::GetFile(LPFORMATETC pFE, LPSTGMEDIUM pSTM)
{
  STGMEDIUM workingmedium;
  FORMATETC fe = *pFE;
  HGLOBAL hGlobalMemory;
  HRESULT hr;
  // Make enough space for the header and the trailing null
  PRUint32 buffersize = sizeof(DROPFILES) + sizeof(PRUnichar);
  PRUint32 alloclen = 0;
  PRUnichar* realbuffer;
  nsAutoString filename;
  
  hGlobalMemory = GlobalAlloc(GHND, buffersize);

  for (PRUint32 i = 0; i < mDataObjects.Length(); ++i) {
    nsDataObj* dataObj = mDataObjects.ElementAt(i);
    hr = dataObj->GetData(&fe, &workingmedium);
    if (hr != S_OK) {
      switch (hr) {
      case DV_E_FORMATETC:
        continue;
      default:
        return hr;
      }
    }
    // Now we need to pull out the filename
    PRUnichar* buffer = (PRUnichar*)GlobalLock(workingmedium.hGlobal);
    if (buffer == NULL)
      return E_FAIL;
    buffer += sizeof(DROPFILES)/sizeof(PRUnichar);
    filename = buffer;
    GlobalUnlock(workingmedium.hGlobal);
    ReleaseStgMedium(&workingmedium);
    // Now put the filename into our buffer
    alloclen = (filename.Length() + 1) * sizeof(PRUnichar);
    hGlobalMemory = ::GlobalReAlloc(hGlobalMemory, buffersize + alloclen, GHND);
    if (hGlobalMemory == NULL)
      return E_FAIL;
    realbuffer = (PRUnichar*)((char*)GlobalLock(hGlobalMemory) + buffersize);
    if (!realbuffer)
      return E_FAIL;
    realbuffer--; // Overwrite the preceding null
    memcpy(realbuffer, filename.get(), alloclen);
    GlobalUnlock(hGlobalMemory);
    buffersize += alloclen;
  }
  // We get the last null (on the double null terminator) for free since we used
  // the zero memory flag when we allocated.  All we need to do is fill the
  // DROPFILES structure
  DROPFILES* df = (DROPFILES*)GlobalLock(hGlobalMemory);
  if (!df)
    return E_FAIL;
  df->pFiles = sizeof(DROPFILES); //Offset to start of file name string
  df->fNC    = 0;
  df->pt.x   = 0;
  df->pt.y   = 0;
  df->fWide  = TRUE; // utf-16 chars
  GlobalUnlock(hGlobalMemory);
  // Finally fill out the STGMEDIUM struct
  pSTM->tymed = TYMED_HGLOBAL;
  pSTM->pUnkForRelease = NULL; // Caller gets to free the data
  pSTM->hGlobal = hGlobalMemory;
  return S_OK;
}

HRESULT nsDataObjCollection::GetText(LPFORMATETC pFE, LPSTGMEDIUM pSTM)
{
  STGMEDIUM workingmedium;
  FORMATETC fe = *pFE;
  HGLOBAL hGlobalMemory;
  HRESULT hr;
  PRUint32 buffersize = 1;
  PRUint32 alloclen = 0;

  hGlobalMemory = GlobalAlloc(GHND, buffersize);

  if (pFE->cfFormat == CF_TEXT) {
    nsCAutoString text;
    for (PRUint32 i = 0; i < mDataObjects.Length(); ++i) {
      nsDataObj* dataObj = mDataObjects.ElementAt(i);
      hr = dataObj->GetData(&fe, &workingmedium);
      if (hr != S_OK) {
        switch (hr) {
        case DV_E_FORMATETC:
          continue;
        default:
          return hr;
        }
      }
      // Now we need to pull out the text
      char* buffer = (char*)GlobalLock(workingmedium.hGlobal);
      if (buffer == NULL)
        return E_FAIL;
      text = buffer;
      GlobalUnlock(workingmedium.hGlobal);
      ReleaseStgMedium(&workingmedium);
      // Now put the text into our buffer
      alloclen = text.Length();
      hGlobalMemory = ::GlobalReAlloc(hGlobalMemory, buffersize + alloclen,
                                      GHND);
      if (hGlobalMemory == NULL)
        return E_FAIL;
      buffer = ((char*)GlobalLock(hGlobalMemory) + buffersize);
      if (!buffer)
        return E_FAIL;
      buffer--; // Overwrite the preceding null
      memcpy(buffer, text.get(), alloclen);
      GlobalUnlock(hGlobalMemory);
      buffersize += alloclen;
    }
    pSTM->tymed = TYMED_HGLOBAL;
    pSTM->pUnkForRelease = NULL; // Caller gets to free the data
    pSTM->hGlobal = hGlobalMemory;
    return S_OK;
  }
  if (pFE->cfFormat == CF_UNICODETEXT) {
    buffersize = sizeof(PRUnichar);
    nsAutoString text;
    for (PRUint32 i = 0; i < mDataObjects.Length(); ++i) {
      nsDataObj* dataObj = mDataObjects.ElementAt(i);
      hr = dataObj->GetData(&fe, &workingmedium);
      if (hr != S_OK) {
        switch (hr) {
        case DV_E_FORMATETC:
          continue;
        default:
          return hr;
        }
      }
      // Now we need to pull out the text
      PRUnichar* buffer = (PRUnichar*)GlobalLock(workingmedium.hGlobal);
      if (buffer == NULL)
        return E_FAIL;
      text = buffer;
      GlobalUnlock(workingmedium.hGlobal);
      ReleaseStgMedium(&workingmedium);
      // Now put the text into our buffer
      alloclen = text.Length() * sizeof(PRUnichar);
      hGlobalMemory = ::GlobalReAlloc(hGlobalMemory, buffersize + alloclen,
                                      GHND);
      if (hGlobalMemory == NULL)
        return E_FAIL;
      buffer = (PRUnichar*)((char*)GlobalLock(hGlobalMemory) + buffersize);
      if (!buffer)
        return E_FAIL;
      buffer--; // Overwrite the preceding null
      memcpy(buffer, text.get(), alloclen);
      GlobalUnlock(hGlobalMemory);
      buffersize += alloclen;
    }
    pSTM->tymed = TYMED_HGLOBAL;
    pSTM->pUnkForRelease = NULL; // Caller gets to free the data
    pSTM->hGlobal = hGlobalMemory;
    return S_OK;
  }

  return E_FAIL;
}

HRESULT nsDataObjCollection::GetFileDescriptors(LPFORMATETC pFE,
                                                LPSTGMEDIUM pSTM)
{
  STGMEDIUM workingmedium;
  FORMATETC fe = *pFE;
  HGLOBAL hGlobalMemory;
  HRESULT hr;
  PRUint32 buffersize = sizeof(FILEGROUPDESCRIPTOR);
  PRUint32 alloclen = sizeof(FILEDESCRIPTOR);

  hGlobalMemory = GlobalAlloc(GHND, buffersize);

  for (PRUint32 i = 0; i < mDataObjects.Length(); ++i) {
    nsDataObj* dataObj = mDataObjects.ElementAt(i);
    hr = dataObj->GetData(&fe, &workingmedium);
    if (hr != S_OK) {
      switch (hr) {
      case DV_E_FORMATETC:
        continue;
      default:
        return hr;
      }
    }
    // Now we need to pull out the filedescriptor
    FILEDESCRIPTOR* buffer =
     (FILEDESCRIPTOR*)((char*)GlobalLock(workingmedium.hGlobal) + sizeof(UINT));
    if (buffer == NULL)
      return E_FAIL;
    hGlobalMemory = ::GlobalReAlloc(hGlobalMemory, buffersize + alloclen, GHND);
    if (hGlobalMemory == NULL)
      return E_FAIL;
    FILEGROUPDESCRIPTOR* realbuffer =
                                (FILEGROUPDESCRIPTOR*)GlobalLock(hGlobalMemory);
    if (!realbuffer)
      return E_FAIL;
    FILEDESCRIPTOR* copyloc = (FILEDESCRIPTOR*)((char*)realbuffer + buffersize);
    memcpy(copyloc, buffer, sizeof(FILEDESCRIPTOR));
    realbuffer->cItems++;
    GlobalUnlock(hGlobalMemory);
    GlobalUnlock(workingmedium.hGlobal);
    ReleaseStgMedium(&workingmedium);
    buffersize += alloclen;
  }
  pSTM->tymed = TYMED_HGLOBAL;
  pSTM->pUnkForRelease = NULL; // Caller gets to free the data
  pSTM->hGlobal = hGlobalMemory;
  return S_OK;
}

HRESULT nsDataObjCollection::GetFileContents(LPFORMATETC pFE, LPSTGMEDIUM pSTM)
{
  ULONG num = 0;
  ULONG numwanted = (pFE->lindex == -1) ? 0 : pFE->lindex;
  FORMATETC fEtc = *pFE;
  fEtc.lindex = -1;  // We're lying to the data object so it thinks it's alone

  // The key for this data type is to figure out which data object the index
  // corresponds to and then just pass it along
  for (PRUint32 i = 0; i < mDataObjects.Length(); ++i) {
    nsDataObj* dataObj = mDataObjects.ElementAt(i);
    if (dataObj->QueryGetData(&fEtc) != S_OK)
      continue;
    if (num == numwanted)
      return dataObj->GetData(pFE, pSTM);
    numwanted++;
  }
  return DV_E_LINDEX;
}

HRESULT nsDataObjCollection::GetFirstSupporting(LPFORMATETC pFE,
                                                LPSTGMEDIUM pSTM)
{
  // There is no way to pass more than one of this, so just find the first data
  // object that supports it and pass it along
  for (PRUint32 i = 0; i < mDataObjects.Length(); ++i) {
    if (mDataObjects.ElementAt(i)->QueryGetData(pFE) == S_OK)
      return mDataObjects.ElementAt(i)->GetData(pFE, pSTM);
  }
  return DV_E_FORMATETC;
}