/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * vim: sw=4 ts=4 et :
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
 * The Original Code is Mozilla Plugin App.
 *
 * The Initial Developer of the Original Code is
 *   Josh Aas <josh@mozilla.com>
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
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

#ifndef PluginPRLibrary_h
#define PluginPRLibrary_h 1

#include "mozilla/PluginLibrary.h"
#include "nsNPAPIPlugin.h"

namespace mozilla {

class PluginPRLibrary : public PluginLibrary
{
#if defined(XP_UNIX) && !defined(XP_MACOSX)
    typedef NPError (*NP_InitializeFunc)(NPNetscapeFuncs*, NPPluginFuncs*);
#else
    typedef NPError (OSCALL *NP_InitializeFunc)(NPNetscapeFuncs*);
#endif
    typedef NPError (OSCALL *NP_ShutdownFunc)();
    typedef char* (*NP_GetMIMEDescriptionFunc)();
    typedef NPError (*NP_GetValueFunc)(void *, NPPVariable, void*);
#if defined(XP_WIN) || defined(XP_MACOSX) || defined(XP_OS2)
    typedef NPError (OSCALL *NP_GetEntryPointsFunc)(NPPluginFuncs*);
#endif

public:
    PluginPRLibrary(const char* aFilePath, PRLibrary* aLibrary) :
#if defined(XP_UNIX) && !defined(XP_MACOSX)
        mNP_Initialize(nsnull),
#else
        mNP_Initialize(nsnull),
#endif
        mNP_Shutdown(nsnull),
        mNP_GetMIMEDescription(nsnull),
        mNP_GetValue(nsnull),
#if defined(XP_WIN) || defined(XP_MACOSX) || defined(XP_OS2)
        mNP_GetEntryPoints(nsnull),
#endif
        mNPP_New(nsnull),
        mLibrary(aLibrary)
    {
        NS_ASSERTION(mLibrary, "need non-null lib");
        // addref here??
    }

    virtual ~PluginPRLibrary()
    {
        // unref here??
    }

    virtual void SetPlugin(nsNPAPIPlugin*) { }

    virtual bool HasRequiredFunctions() {
        mNP_Initialize = (NP_InitializeFunc)
            PR_FindFunctionSymbol(mLibrary, "NP_Initialize");
        if (!mNP_Initialize)
            return false;

        mNP_Shutdown = (NP_ShutdownFunc)
            PR_FindFunctionSymbol(mLibrary, "NP_Shutdown");
        if (!mNP_Shutdown)
            return false;

        mNP_GetMIMEDescription = (NP_GetMIMEDescriptionFunc)
            PR_FindFunctionSymbol(mLibrary, "NP_GetMIMEDescription");
#ifndef XP_MACOSX
        if (!mNP_GetMIMEDescription)
            return false;
#endif

        mNP_GetValue = (NP_GetValueFunc)
            PR_FindFunctionSymbol(mLibrary, "NP_GetValue");
#ifndef XP_MACOSX
        if (!mNP_GetValue)
            return false;
#endif

#if defined(XP_WIN) || defined(XP_MACOSX) || defined(XP_OS2)
        mNP_GetEntryPoints = (NP_GetEntryPointsFunc)
            PR_FindFunctionSymbol(mLibrary, "NP_GetEntryPoints");
        if (!mNP_GetEntryPoints)
            return false;
#endif
        return true;
    }

#if defined(XP_UNIX) && !defined(XP_MACOSX)
    virtual nsresult NP_Initialize(NPNetscapeFuncs* bFuncs, NPPluginFuncs* pFuncs, NPError* error) {
        if (mNP_Initialize) {
            *error = mNP_Initialize(bFuncs, pFuncs);
        } else {
            NP_InitializeFunc pfNP_Initialize = (NP_InitializeFunc)
                PR_FindFunctionSymbol(mLibrary, "NP_Initialize");
            if (!pfNP_Initialize)
                return NS_ERROR_FAILURE;
            *error = pfNP_Initialize(bFuncs, pFuncs);
        }

        // save NPP_New
        mNPP_New = pFuncs->newp;
        return NS_OK;
    }
#else
    virtual nsresult NP_Initialize(NPNetscapeFuncs* bFuncs, NPError* error) {
        if (mNP_Initialize) {
            *error = mNP_Initialize(bFuncs);
        } else {
            NP_InitializeFunc pfNP_Initialize = (NP_InitializeFunc)
                PR_FindFunctionSymbol(mLibrary, "NP_Initialize");
            if (!pfNP_Initialize)
                return NS_ERROR_FAILURE;
            *error = pfNP_Initialize(bFuncs);
        }

        return NS_OK;
    }
#endif

    virtual nsresult NP_Shutdown(NPError* error) {
        if (mNP_Shutdown) {
            *error = mNP_Shutdown();
        } else {
            NP_ShutdownFunc pfNP_Shutdown = (NP_ShutdownFunc)
                PR_FindFunctionSymbol(mLibrary, "NP_Shutdown");
            if (!pfNP_Shutdown)
                return NS_ERROR_FAILURE;
            *error = pfNP_Shutdown();
        }

        return NS_OK;
    }

    virtual nsresult NP_GetMIMEDescription(const char** mimeDesc) {
        if (mNP_GetMIMEDescription) {
            *mimeDesc = mNP_GetMIMEDescription();
        }
        else {
            NP_GetMIMEDescriptionFunc pfNP_GetMIMEDescription =
                (NP_GetMIMEDescriptionFunc)
                PR_FindFunctionSymbol(mLibrary, "NP_GetMIMEDescription");
            if (!pfNP_GetMIMEDescription) {
                *mimeDesc = "";
                return NS_ERROR_FAILURE;
            }
            *mimeDesc = pfNP_GetMIMEDescription();
        }

        return NS_OK;
    }

    virtual nsresult NP_GetValue(void *future, NPPVariable aVariable,
                                 void *aValue, NPError* error) {
        if (mNP_GetValue) {
            *error = mNP_GetValue(future, aVariable, aValue);
        } else {
            NP_GetValueFunc pfNP_GetValue = (NP_GetValueFunc)
                PR_FindFunctionSymbol(mLibrary, "NP_GetValue");
            if (!pfNP_GetValue)
                return NS_ERROR_FAILURE;
            *error = pfNP_GetValue(future, aVariable, aValue);
        }

        return NS_OK;
    }

#if defined(XP_WIN) || defined(XP_MACOSX) || defined(XP_OS2)
    virtual nsresult NP_GetEntryPoints(NPPluginFuncs* pFuncs, NPError* error) {
        if (mNP_GetEntryPoints) {
            *error = mNP_GetEntryPoints(pFuncs);
        } else {
            NP_GetEntryPointsFunc pfNP_GetEntryPoints = (NP_GetEntryPointsFunc)
                PR_FindFunctionSymbol(mLibrary, "NP_GetEntryPoints");
            if (!pfNP_GetEntryPoints)
                return NS_ERROR_FAILURE;
            *error = pfNP_GetEntryPoints(pFuncs);
        }

        // save NPP_New
        mNPP_New = pFuncs->newp;
        return NS_OK;
    }
#endif

    virtual nsresult NPP_New(NPMIMEType pluginType, NPP instance,
                             uint16_t mode, int16_t argc, char* argn[],
                             char* argv[], NPSavedData* saved,
                             NPError* error) {
        if (!mNPP_New)
            return NS_ERROR_FAILURE;
        *error = mNPP_New(pluginType, instance, mode, argc, argn, argv, saved);
        return NS_OK;
    }

private:
    NP_InitializeFunc mNP_Initialize;
    NP_ShutdownFunc mNP_Shutdown;
    NP_GetMIMEDescriptionFunc mNP_GetMIMEDescription;
    NP_GetValueFunc mNP_GetValue;
#if defined(XP_WIN) || defined(XP_MACOSX) || defined(XP_OS2)
    NP_GetEntryPointsFunc mNP_GetEntryPoints;
#endif
    NPP_NewProcPtr mNPP_New;
    PRLibrary* mLibrary;
};


} // namespace mozilla

#endif  // ifndef PluginPRLibrary_h
