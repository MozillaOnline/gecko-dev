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
 *   Ben Turner <bent.mozilla@gmail.com>.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Chris Jones <jones.chris.g@gmail.com>
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

#include "mozilla/plugins/PluginThreadChild.h"

#include "prlink.h"

#include "base/command_line.h"
#include "base/string_util.h"
#include "chrome/common/child_process.h"
#include "chrome/common/chrome_switches.h"

using mozilla::ipc::MozillaChildThread;

namespace mozilla {
namespace plugins {

PluginThreadChild::PluginThreadChild(ProcessHandle aParentHandle) :
    MozillaChildThread(aParentHandle, MessageLoop::TYPE_UI)
{
    NS_ASSERTION(!gInstance, "Two PluginThreadChild?");
    gInstance = this;
}

PluginThreadChild::~PluginThreadChild()
{
    gInstance = NULL;
}

PluginThreadChild* PluginThreadChild::gInstance;

void
PluginThreadChild::Init()
{
    MozillaChildThread::Init();

    std::string pluginFilename;

#if defined(OS_POSIX)
    // NB: need to be very careful in ensuring that the first arg
    // (after the binary name) here is indeed the plugin module path.
    // Keep in sync with dom/plugins/PluginModuleParent.
    std::vector<std::string> values = CommandLine::ForCurrentProcess()->argv();
    NS_ABORT_IF_FALSE(values.size() >= 2, "not enough args");

    pluginFilename = values[1];

#elif defined(OS_WIN)
    std::vector<std::wstring> values =
        CommandLine::ForCurrentProcess()->GetLooseValues();
    NS_ABORT_IF_FALSE(values.size() >= 1, "not enough loose args");

    pluginFilename = WideToUTF8(values[0]);

#else
#  error Sorry
#endif

    // FIXME owner_loop() is bad here
    mPlugin.Init(pluginFilename,
                 GetParentProcessHandle(), owner_loop(), channel());
}

void
PluginThreadChild::CleanUp()
{
    mPlugin.CleanUp();
    MozillaChildThread::CleanUp();
}

/* static */
void
PluginThreadChild::AppendNotesToCrashReport(const nsCString& aNotes)
{
    AssertPluginThread();

    if (gInstance) {
        gInstance->mPlugin.SendAppendNotesToCrashReport(aNotes);
    }
}

} // namespace plugins
} // namespace mozilla
