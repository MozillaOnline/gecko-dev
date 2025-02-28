/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * vim: sw=2 ts=2 et :
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
 *   Ben Turner <bent.mozilla@gmail.com>
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

#include "PluginScriptableObjectChild.h"
#include "PluginScriptableObjectUtils.h"

using namespace mozilla::plugins;
using mozilla::ipc::NPRemoteIdentifier;

// static
NPObject*
PluginScriptableObjectChild::ScriptableAllocate(NPP aInstance,
                                                NPClass* aClass)
{
  AssertPluginThread();

  if (aClass != GetClass()) {
    NS_RUNTIMEABORT("Huh?! Wrong class!");
  }

  return new ChildNPObject();
}

// static
void
PluginScriptableObjectChild::ScriptableInvalidate(NPObject* aObject)
{
  AssertPluginThread();

  if (aObject->_class != GetClass()) {
    NS_RUNTIMEABORT("Don't know what kind of object this is!");
  }

  ChildNPObject* object = reinterpret_cast<ChildNPObject*>(aObject);
  if (object->invalidated) {
    // This can happen more than once, and is just fine.
    return;
  }

  object->invalidated = true;
}

// static
void
PluginScriptableObjectChild::ScriptableDeallocate(NPObject* aObject)
{
  AssertPluginThread();

  if (aObject->_class != GetClass()) {
    NS_RUNTIMEABORT("Don't know what kind of object this is!");
  }

  ChildNPObject* object = reinterpret_cast<ChildNPObject*>(aObject);
  PluginScriptableObjectChild* actor = object->parent;
  if (actor) {
    NS_ASSERTION(actor->Type() == Proxy, "Bad type!");
    actor->DropNPObject();
  }

  delete object;
}

// static
bool
PluginScriptableObjectChild::ScriptableHasMethod(NPObject* aObject,
                                                 NPIdentifier aName)
{
  AssertPluginThread();

  if (aObject->_class != GetClass()) {
    NS_RUNTIMEABORT("Don't know what kind of object this is!");
  }

  ChildNPObject* object = reinterpret_cast<ChildNPObject*>(aObject);
  if (object->invalidated) {
    NS_WARNING("Calling method on an invalidated object!");
    return false;
  }

  ProtectedActor<PluginScriptableObjectChild> actor(object->parent);
  NS_ASSERTION(actor, "This shouldn't ever be null!");
  NS_ASSERTION(actor->Type() == Proxy, "Bad type!");

  bool result;
  actor->CallHasMethod((NPRemoteIdentifier)aName, &result);

  return result;
}

// static
bool
PluginScriptableObjectChild::ScriptableInvoke(NPObject* aObject,
                                              NPIdentifier aName,
                                              const NPVariant* aArgs,
                                              uint32_t aArgCount,
                                              NPVariant* aResult)
{
  AssertPluginThread();

  if (aObject->_class != GetClass()) {
    NS_RUNTIMEABORT("Don't know what kind of object this is!");
  }

  ChildNPObject* object = reinterpret_cast<ChildNPObject*>(aObject);
  if (object->invalidated) {
    NS_WARNING("Calling method on an invalidated object!");
    return false;
  }

  ProtectedActor<PluginScriptableObjectChild> actor(object->parent);
  NS_ASSERTION(actor, "This shouldn't ever be null!");
  NS_ASSERTION(actor->Type() == Proxy, "Bad type!");

  ProtectedVariantArray args(aArgs, aArgCount, actor->GetInstance());
  if (!args.IsOk()) {
    NS_ERROR("Failed to convert arguments!");
    return false;
  }

  Variant remoteResult;
  bool success;
  actor->CallInvoke((NPRemoteIdentifier)aName, args, &remoteResult, &success);

  if (!success) {
    return false;
  }

  ConvertToVariant(remoteResult, *aResult);
  return true;
}

// static
bool
PluginScriptableObjectChild::ScriptableInvokeDefault(NPObject* aObject,
                                                     const NPVariant* aArgs,
                                                     uint32_t aArgCount,
                                                     NPVariant* aResult)
{
  AssertPluginThread();

  if (aObject->_class != GetClass()) {
    NS_RUNTIMEABORT("Don't know what kind of object this is!");
  }

  ChildNPObject* object = reinterpret_cast<ChildNPObject*>(aObject);
  if (object->invalidated) {
    NS_WARNING("Calling method on an invalidated object!");
    return false;
  }

  ProtectedActor<PluginScriptableObjectChild> actor(object->parent);
  NS_ASSERTION(actor, "This shouldn't ever be null!");
  NS_ASSERTION(actor->Type() == Proxy, "Bad type!");

  ProtectedVariantArray args(aArgs, aArgCount, actor->GetInstance());
  if (!args.IsOk()) {
    NS_ERROR("Failed to convert arguments!");
    return false;
  }

  Variant remoteResult;
  bool success;
  actor->CallInvokeDefault(args, &remoteResult, &success);

  if (!success) {
    return false;
  }

  ConvertToVariant(remoteResult, *aResult);
  return true;
}

// static
bool
PluginScriptableObjectChild::ScriptableHasProperty(NPObject* aObject,
                                                   NPIdentifier aName)
{
  AssertPluginThread();

  if (aObject->_class != GetClass()) {
    NS_RUNTIMEABORT("Don't know what kind of object this is!");
  }

  ChildNPObject* object = reinterpret_cast<ChildNPObject*>(aObject);
  if (object->invalidated) {
    NS_WARNING("Calling method on an invalidated object!");
    return false;
  }

  ProtectedActor<PluginScriptableObjectChild> actor(object->parent);
  NS_ASSERTION(actor, "This shouldn't ever be null!");
  NS_ASSERTION(actor->Type() == Proxy, "Bad type!");

  bool result;
  actor->CallHasProperty((NPRemoteIdentifier)aName, &result);

  return result;
}

// static
bool
PluginScriptableObjectChild::ScriptableGetProperty(NPObject* aObject,
                                                   NPIdentifier aName,
                                                   NPVariant* aResult)
{
  AssertPluginThread();

  if (aObject->_class != GetClass()) {
    NS_RUNTIMEABORT("Don't know what kind of object this is!");
  }

  ChildNPObject* object = reinterpret_cast<ChildNPObject*>(aObject);
  if (object->invalidated) {
    NS_WARNING("Calling method on an invalidated object!");
    return false;
  }

  ProtectedActor<PluginScriptableObjectChild> actor(object->parent);
  NS_ASSERTION(actor, "This shouldn't ever be null!");
  NS_ASSERTION(actor->Type() == Proxy, "Bad type!");

  Variant result;
  bool success;
  actor->CallGetProperty((NPRemoteIdentifier)aName, &result, &success);

  if (!success) {
    return false;
  }

  ConvertToVariant(result, *aResult);
  return true;
}

// static
bool
PluginScriptableObjectChild::ScriptableSetProperty(NPObject* aObject,
                                                   NPIdentifier aName,
                                                   const NPVariant* aValue)
{
  AssertPluginThread();

  if (aObject->_class != GetClass()) {
    NS_RUNTIMEABORT("Don't know what kind of object this is!");
  }

  ChildNPObject* object = reinterpret_cast<ChildNPObject*>(aObject);
  if (object->invalidated) {
    NS_WARNING("Calling method on an invalidated object!");
    return false;
  }

  ProtectedActor<PluginScriptableObjectChild> actor(object->parent);
  NS_ASSERTION(actor, "This shouldn't ever be null!");
  NS_ASSERTION(actor->Type() == Proxy, "Bad type!");

  ProtectedVariant value(*aValue, actor->GetInstance());
  if (!value.IsOk()) {
    NS_WARNING("Failed to convert variant!");
    return false;
  }

  bool success;
  actor->CallSetProperty((NPRemoteIdentifier)aName, value, &success);

  return success;
}

// static
bool
PluginScriptableObjectChild::ScriptableRemoveProperty(NPObject* aObject,
                                                      NPIdentifier aName)
{
  AssertPluginThread();

  if (aObject->_class != GetClass()) {
    NS_RUNTIMEABORT("Don't know what kind of object this is!");
  }

  ChildNPObject* object = reinterpret_cast<ChildNPObject*>(aObject);
  if (object->invalidated) {
    NS_WARNING("Calling method on an invalidated object!");
    return false;
  }

  ProtectedActor<PluginScriptableObjectChild> actor(object->parent);
  NS_ASSERTION(actor, "This shouldn't ever be null!");
  NS_ASSERTION(actor->Type() == Proxy, "Bad type!");

  bool success;
  actor->CallRemoveProperty((NPRemoteIdentifier)aName, &success);

  return success;
}

// static
bool
PluginScriptableObjectChild::ScriptableEnumerate(NPObject* aObject,
                                                 NPIdentifier** aIdentifiers,
                                                 uint32_t* aCount)
{
  AssertPluginThread();

  if (aObject->_class != GetClass()) {
    NS_RUNTIMEABORT("Don't know what kind of object this is!");
  }

  ChildNPObject* object = reinterpret_cast<ChildNPObject*>(aObject);
  if (object->invalidated) {
    NS_WARNING("Calling method on an invalidated object!");
    return false;
  }

  ProtectedActor<PluginScriptableObjectChild> actor(object->parent);
  NS_ASSERTION(actor, "This shouldn't ever be null!");
  NS_ASSERTION(actor->Type() == Proxy, "Bad type!");

  nsAutoTArray<NPRemoteIdentifier, 10> identifiers;
  bool success;
  actor->CallEnumerate(&identifiers, &success);

  if (!success) {
    return false;
  }

  *aCount = identifiers.Length();
  if (!*aCount) {
    *aIdentifiers = nsnull;
    return true;
  }

  *aIdentifiers = reinterpret_cast<NPIdentifier*>(
      PluginModuleChild::sBrowserFuncs.memalloc(*aCount * sizeof(NPIdentifier)));
  if (!*aIdentifiers) {
    NS_ERROR("Out of memory!");
    return false;
  }

  for (PRUint32 index = 0; index < *aCount; index++) {
    (*aIdentifiers)[index] = (NPIdentifier)identifiers[index];
  }
  return true;
}

// static
bool
PluginScriptableObjectChild::ScriptableConstruct(NPObject* aObject,
                                                 const NPVariant* aArgs,
                                                 uint32_t aArgCount,
                                                 NPVariant* aResult)
{
  AssertPluginThread();

  if (aObject->_class != GetClass()) {
    NS_RUNTIMEABORT("Don't know what kind of object this is!");
  }

  ChildNPObject* object = reinterpret_cast<ChildNPObject*>(aObject);
  if (object->invalidated) {
    NS_WARNING("Calling method on an invalidated object!");
    return false;
  }

  ProtectedActor<PluginScriptableObjectChild> actor(object->parent);
  NS_ASSERTION(actor, "This shouldn't ever be null!");
  NS_ASSERTION(actor->Type() == Proxy, "Bad type!");

  ProtectedVariantArray args(aArgs, aArgCount, actor->GetInstance());
  if (!args.IsOk()) {
    NS_ERROR("Failed to convert arguments!");
    return false;
  }

  Variant remoteResult;
  bool success;
  actor->CallConstruct(args, &remoteResult, &success);

  if (!success) {
    return false;
  }

  ConvertToVariant(remoteResult, *aResult);
  return true;
}

const NPClass PluginScriptableObjectChild::sNPClass = {
  NP_CLASS_STRUCT_VERSION,
  PluginScriptableObjectChild::ScriptableAllocate,
  PluginScriptableObjectChild::ScriptableDeallocate,
  PluginScriptableObjectChild::ScriptableInvalidate,
  PluginScriptableObjectChild::ScriptableHasMethod,
  PluginScriptableObjectChild::ScriptableInvoke,
  PluginScriptableObjectChild::ScriptableInvokeDefault,
  PluginScriptableObjectChild::ScriptableHasProperty,
  PluginScriptableObjectChild::ScriptableGetProperty,
  PluginScriptableObjectChild::ScriptableSetProperty,
  PluginScriptableObjectChild::ScriptableRemoveProperty,
  PluginScriptableObjectChild::ScriptableEnumerate,
  PluginScriptableObjectChild::ScriptableConstruct
};

PluginScriptableObjectChild::PluginScriptableObjectChild(
                                                     ScriptableObjectType aType)
: mInstance(nsnull),
  mObject(nsnull),
  mInvalidated(false),
  mProtectCount(0),
  mType(aType)
{
  AssertPluginThread();
}

PluginScriptableObjectChild::~PluginScriptableObjectChild()
{
  AssertPluginThread();

  if (mObject) {
    PluginModuleChild::current()->UnregisterActorForNPObject(mObject);

    if (mObject->_class == GetClass()) {
      NS_ASSERTION(mType == Proxy, "Wrong type!");
      static_cast<ChildNPObject*>(mObject)->parent = nsnull;
    }
    else {
      NS_ASSERTION(mType == LocalObject, "Wrong type!");
      PluginModuleChild::sBrowserFuncs.releaseobject(mObject);
    }
  }
}

void
PluginScriptableObjectChild::InitializeProxy()
{
  AssertPluginThread();
  NS_ASSERTION(mType == Proxy, "Bad type!");
  NS_ASSERTION(!mObject, "Calling Initialize more than once!");
  NS_ASSERTION(!mInvalidated, "Already invalidated?!");

  mInstance = static_cast<PluginInstanceChild*>(Manager());
  NS_ASSERTION(mInstance, "Null manager?!");

  NPObject* object = CreateProxyObject();
  NS_ASSERTION(object, "Failed to create object!");

  if (!PluginModuleChild::current()->RegisterActorForNPObject(object, this)) {
    NS_ERROR("Out of memory?");
  }

  mObject = object;
}

void
PluginScriptableObjectChild::InitializeLocal(NPObject* aObject)
{
  AssertPluginThread();
  NS_ASSERTION(mType == LocalObject, "Bad type!");
  NS_ASSERTION(!mObject, "Calling Initialize more than once!");
  NS_ASSERTION(!mInvalidated, "Already invalidated?!");

  mInstance = static_cast<PluginInstanceChild*>(Manager());
  NS_ASSERTION(mInstance, "Null manager?!");

  PluginModuleChild::sBrowserFuncs.retainobject(aObject);

  NS_ASSERTION(!mProtectCount, "Should be zero!");
  mProtectCount++;

  if (!PluginModuleChild::current()->RegisterActorForNPObject(aObject, this)) {
      NS_ERROR("Out of memory?");
  }

  mObject = aObject;
}

NPObject*
PluginScriptableObjectChild::CreateProxyObject()
{
  NS_ASSERTION(mInstance, "Must have an instance!");
  NS_ASSERTION(mType == Proxy, "Shouldn't call this for non-proxy object!");

  NPClass* proxyClass = const_cast<NPClass*>(GetClass());
  NPObject* npobject =
    PluginModuleChild::sBrowserFuncs.createobject(mInstance->GetNPP(),
                                                  proxyClass);
  NS_ASSERTION(npobject, "Failed to create object?!");
  NS_ASSERTION(npobject->_class == GetClass(), "Wrong kind of object!");
  NS_ASSERTION(npobject->referenceCount == 1, "Some kind of live object!");

  ChildNPObject* object = static_cast<ChildNPObject*>(npobject);
  NS_ASSERTION(!object->invalidated, "Bad object!");
  NS_ASSERTION(!object->parent, "Bad object!");

  // We don't want to have the actor own this object but rather let the object
  // own this actor. Set the reference count to 0 here so that when the object
  // dies we will send the destructor message to the child.
  object->referenceCount = 0;
  NS_LOG_RELEASE(object, 0, "NPObject");

  object->parent = const_cast<PluginScriptableObjectChild*>(this);
  return object;
}

bool
PluginScriptableObjectChild::ResurrectProxyObject()
{
  NS_ASSERTION(mInstance, "Must have an instance already!");
  NS_ASSERTION(!mObject, "Should not have an object already!");
  NS_ASSERTION(mType == Proxy, "Shouldn't call this for non-proxy object!");

  NPObject* object = CreateProxyObject();
  if (!object) {
    NS_WARNING("Failed to create object!");
    return false;
  }

  InitializeProxy();
  NS_ASSERTION(mObject, "Initialize failed!");

  CallProtect();
  return true;
}

NPObject*
PluginScriptableObjectChild::GetObject(bool aCanResurrect)
{
  if (!mObject && aCanResurrect && !ResurrectProxyObject()) {
    NS_ERROR("Null object!");
    return nsnull;
  }
  return mObject;
}

void
PluginScriptableObjectChild::Protect()
{
  NS_ASSERTION(mObject, "No object!");
  NS_ASSERTION(mProtectCount >= 0, "Negative retain count?!");

  if (mType == LocalObject) {
    ++mProtectCount;
  }
}

void
PluginScriptableObjectChild::Unprotect()
{
  NS_ASSERTION(mObject, "Bad state!");
  NS_ASSERTION(mProtectCount >= 0, "Negative retain count?!");

  if (mType == LocalObject) {
    if (--mProtectCount == 0) {
      PluginScriptableObjectChild::Call__delete__(this);
    }
  }
}

void
PluginScriptableObjectChild::DropNPObject()
{
  NS_ASSERTION(mObject, "Invalidated object!");
  NS_ASSERTION(mObject->_class == GetClass(), "Wrong type of object!");
  NS_ASSERTION(mType == Proxy, "Shouldn't call this for non-proxy object!");

  // We think we're about to be deleted, but we could be racing with the other
  // process.
  PluginModuleChild::current()->UnregisterActorForNPObject(mObject);
  mObject = nsnull;

  CallUnprotect();
}

void
PluginScriptableObjectChild::NPObjectDestroyed()
{
  NS_ASSERTION(LocalObject == mType,
               "ScriptableDeallocate should have handled this for proxies");
  mInvalidated = true;
  mObject = NULL;
}

bool
PluginScriptableObjectChild::AnswerInvalidate()
{
  AssertPluginThread();

  if (mInvalidated) {
    return true;
  }

  mInvalidated = true;

  NS_ASSERTION(mObject->_class != GetClass(), "Bad object type!");
  NS_ASSERTION(mType == LocalObject, "Bad type!");

  if (mObject->_class && mObject->_class->invalidate) {
    mObject->_class->invalidate(mObject);
  }

  Unprotect();

  return true;
}

bool
PluginScriptableObjectChild::AnswerHasMethod(const NPRemoteIdentifier& aId,
                                             bool* aHasMethod)
{
  AssertPluginThread();

  if (mInvalidated) {
    NS_WARNING("Calling AnswerHasMethod with an invalidated object!");
    *aHasMethod = false;
    return true;
  }

  NS_ASSERTION(mObject->_class != GetClass(), "Bad object type!");
  NS_ASSERTION(mType == LocalObject, "Bad type!");

  if (!(mObject->_class && mObject->_class->hasMethod)) {
    *aHasMethod = false;
    return true;
  }

  *aHasMethod = mObject->_class->hasMethod(mObject, (NPIdentifier)aId);
  return true;
}

bool
PluginScriptableObjectChild::AnswerInvoke(const NPRemoteIdentifier& aId,
                                          const nsTArray<Variant>& aArgs,
                                          Variant* aResult,
                                          bool* aSuccess)
{
  AssertPluginThread();

  if (mInvalidated) {
    NS_WARNING("Calling AnswerInvoke with an invalidated object!");
    *aResult = void_t();
    *aSuccess = false;
    return true;
  }

  NS_ASSERTION(mObject->_class != GetClass(), "Bad object type!");
  NS_ASSERTION(mType == LocalObject, "Bad type!");

  if (!(mObject->_class && mObject->_class->invoke)) {
    *aResult = void_t();
    *aSuccess = false;
    return true;
  }

  nsAutoTArray<NPVariant, 10> convertedArgs;
  PRUint32 argCount = aArgs.Length();

  if (!convertedArgs.SetLength(argCount)) {
    *aResult = void_t();
    *aSuccess = false;
    return true;
  }

  for (PRUint32 index = 0; index < argCount; index++) {
    ConvertToVariant(aArgs[index], convertedArgs[index]);
  }

  NPVariant result;
  VOID_TO_NPVARIANT(result);
  bool success = mObject->_class->invoke(mObject, (NPIdentifier)aId,
                                         convertedArgs.Elements(), argCount,
                                         &result);

  for (PRUint32 index = 0; index < argCount; index++) {
    PluginModuleChild::sBrowserFuncs.releasevariantvalue(&convertedArgs[index]);
  }

  if (!success) {
    *aResult = void_t();
    *aSuccess = false;
    return true;
  }

  Variant convertedResult;
  success = ConvertToRemoteVariant(result, convertedResult, GetInstance(),
                                   false);

  DeferNPVariantLastRelease(&PluginModuleChild::sBrowserFuncs, &result);

  if (!success) {
    *aResult = void_t();
    *aSuccess = false;
    return true;
  }

  *aSuccess = true;
  *aResult = convertedResult;
  return true;
}

bool
PluginScriptableObjectChild::AnswerInvokeDefault(const nsTArray<Variant>& aArgs,
                                                 Variant* aResult,
                                                 bool* aSuccess)
{
  AssertPluginThread();

  if (mInvalidated) {
    NS_WARNING("Calling AnswerInvokeDefault with an invalidated object!");
    *aResult = void_t();
    *aSuccess = false;
    return true;
  }

  NS_ASSERTION(mObject->_class != GetClass(), "Bad object type!");
  NS_ASSERTION(mType == LocalObject, "Bad type!");

  if (!(mObject->_class && mObject->_class->invokeDefault)) {
    *aResult = void_t();
    *aSuccess = false;
    return true;
  }

  nsAutoTArray<NPVariant, 10> convertedArgs;
  PRUint32 argCount = aArgs.Length();

  if (!convertedArgs.SetLength(argCount)) {
    *aResult = void_t();
    *aSuccess = false;
    return true;
  }

  for (PRUint32 index = 0; index < argCount; index++) {
    ConvertToVariant(aArgs[index], convertedArgs[index]);
  }

  NPVariant result;
  VOID_TO_NPVARIANT(result);
  bool success = mObject->_class->invokeDefault(mObject,
                                                convertedArgs.Elements(),
                                                argCount, &result);

  for (PRUint32 index = 0; index < argCount; index++) {
    PluginModuleChild::sBrowserFuncs.releasevariantvalue(&convertedArgs[index]);
  }

  if (!success) {
    *aResult = void_t();
    *aSuccess = false;
    return true;
  }

  Variant convertedResult;
  success = ConvertToRemoteVariant(result, convertedResult, GetInstance(),
                                   false);

  DeferNPVariantLastRelease(&PluginModuleChild::sBrowserFuncs, &result);

  if (!success) {
    *aResult = void_t();
    *aSuccess = false;
    return true;
  }

  *aResult = convertedResult;
  *aSuccess = true;
  return true;
}

bool
PluginScriptableObjectChild::AnswerHasProperty(const NPRemoteIdentifier& aId,
                                               bool* aHasProperty)
{
  AssertPluginThread();

  if (mInvalidated) {
    NS_WARNING("Calling AnswerHasProperty with an invalidated object!");
    *aHasProperty = false;
    return true;
  }

  NS_ASSERTION(mObject->_class != GetClass(), "Bad object type!");
  NS_ASSERTION(mType == LocalObject, "Bad type!");

  if (!(mObject->_class && mObject->_class->hasProperty)) {
    *aHasProperty = false;
    return true;
  }

  *aHasProperty = mObject->_class->hasProperty(mObject, (NPIdentifier)aId);
  return true;
}

bool
PluginScriptableObjectChild::AnswerGetProperty(const NPRemoteIdentifier& aId,
                                               Variant* aResult,
                                               bool* aSuccess)
{
  AssertPluginThread();

  if (mInvalidated) {
    NS_WARNING("Calling AnswerGetProperty with an invalidated object!");
    *aResult = void_t();
    *aSuccess = false;
    return true;
  }

  NS_ASSERTION(mObject->_class != GetClass(), "Bad object type!");
  NS_ASSERTION(mType == LocalObject, "Bad type!");

  if (!(mObject->_class && mObject->_class->getProperty)) {
    *aResult = void_t();
    *aSuccess = false;
    return true;
  }

  NPVariant result;
  VOID_TO_NPVARIANT(result);
  if (!mObject->_class->getProperty(mObject, (NPIdentifier)aId, &result)) {
    *aResult = void_t();
    *aSuccess = false;
    return true;
  }

  Variant converted;
  if ((*aSuccess = ConvertToRemoteVariant(result, converted, GetInstance(),
                                          false))) {
    DeferNPVariantLastRelease(&PluginModuleChild::sBrowserFuncs, &result);
    *aResult = converted;
  }
  else {
    *aResult = void_t();
  }

  return true;
}

bool
PluginScriptableObjectChild::AnswerSetProperty(const NPRemoteIdentifier& aId,
                                               const Variant& aValue,
                                               bool* aSuccess)
{
  AssertPluginThread();

  if (mInvalidated) {
    NS_WARNING("Calling AnswerSetProperty with an invalidated object!");
    *aSuccess = false;
    return true;
  }

  NS_ASSERTION(mObject->_class != GetClass(), "Bad object type!");
  NS_ASSERTION(mType == LocalObject, "Bad type!");

  if (!(mObject->_class && mObject->_class->setProperty)) {
    *aSuccess = false;
    return true;
  }

  NPVariant converted;
  ConvertToVariant(aValue, converted);

  if ((*aSuccess = mObject->_class->setProperty(mObject, (NPIdentifier)aId,
                                                &converted))) {
    PluginModuleChild::sBrowserFuncs.releasevariantvalue(&converted);
  }
  return true;
}

bool
PluginScriptableObjectChild::AnswerRemoveProperty(const NPRemoteIdentifier& aId,
                                                  bool* aSuccess)
{
  AssertPluginThread();

  if (mInvalidated) {
    NS_WARNING("Calling AnswerRemoveProperty with an invalidated object!");
    *aSuccess = false;
    return true;
  }

  NS_ASSERTION(mObject->_class != GetClass(), "Bad object type!");
  NS_ASSERTION(mType == LocalObject, "Bad type!");

  if (!(mObject->_class && mObject->_class->removeProperty)) {
    *aSuccess = false;
    return true;
  }

  *aSuccess = mObject->_class->removeProperty(mObject, (NPIdentifier)aId);
  return true;
}

bool
PluginScriptableObjectChild::AnswerEnumerate(nsTArray<NPRemoteIdentifier>* aProperties,
                                             bool* aSuccess)
{
  AssertPluginThread();

  if (mInvalidated) {
    NS_WARNING("Calling AnswerEnumerate with an invalidated object!");
    *aSuccess = false;
    return true;
  }

  NS_ASSERTION(mObject->_class != GetClass(), "Bad object type!");
  NS_ASSERTION(mType == LocalObject, "Bad type!");

  if (!(mObject->_class && mObject->_class->enumerate)) {
    *aSuccess = false;
    return true;
  }

  NPIdentifier* ids;
  uint32_t idCount;
  if (!mObject->_class->enumerate(mObject, &ids, &idCount)) {
    *aSuccess = false;
    return true;
  }

  if (!aProperties->SetCapacity(idCount)) {
    PluginModuleChild::sBrowserFuncs.memfree(ids);
    *aSuccess = false;
    return true;
  }

  for (uint32_t index = 0; index < idCount; index++) {
#ifdef DEBUG
    NPRemoteIdentifier* remoteId =
#endif
    aProperties->AppendElement((NPRemoteIdentifier)ids[index]);
    NS_ASSERTION(remoteId, "Shouldn't fail if SetCapacity above succeeded!");
  }

  PluginModuleChild::sBrowserFuncs.memfree(ids);
  *aSuccess = true;
  return true;
}

bool
PluginScriptableObjectChild::AnswerConstruct(const nsTArray<Variant>& aArgs,
                                             Variant* aResult,
                                             bool* aSuccess)
{
  AssertPluginThread();

  if (mInvalidated) {
    NS_WARNING("Calling AnswerConstruct with an invalidated object!");
    *aResult = void_t();
    *aSuccess = false;
    return true;
  }

  NS_ASSERTION(mObject->_class != GetClass(), "Bad object type!");
  NS_ASSERTION(mType == LocalObject, "Bad type!");

  if (!(mObject->_class && mObject->_class->construct)) {
    *aResult = void_t();
    *aSuccess = false;
    return true;
  }

  nsAutoTArray<NPVariant, 10> convertedArgs;
  PRUint32 argCount = aArgs.Length();

  if (!convertedArgs.SetLength(argCount)) {
    *aResult = void_t();
    *aSuccess = false;
    return true;
  }

  for (PRUint32 index = 0; index < argCount; index++) {
    ConvertToVariant(aArgs[index], convertedArgs[index]);
  }

  NPVariant result;
  VOID_TO_NPVARIANT(result);
  bool success = mObject->_class->construct(mObject, convertedArgs.Elements(),
                                            argCount, &result);

  for (PRUint32 index = 0; index < argCount; index++) {
    PluginModuleChild::sBrowserFuncs.releasevariantvalue(&convertedArgs[index]);
  }

  if (!success) {
    *aResult = void_t();
    *aSuccess = false;
    return true;
  }

  Variant convertedResult;
  success = ConvertToRemoteVariant(result, convertedResult, GetInstance(),
                                   false);

  DeferNPVariantLastRelease(&PluginModuleChild::sBrowserFuncs, &result);

  if (!success) {
    *aResult = void_t();
    *aSuccess = false;
    return true;
  }

  *aResult = convertedResult;
  *aSuccess = true;
  return true;
}

bool
PluginScriptableObjectChild::AnswerProtect()
{
  NS_ASSERTION(mObject->_class != GetClass(), "Bad object type!");
  NS_ASSERTION(mType == LocalObject, "Bad type!");

  Protect();
  return true;
}

bool
PluginScriptableObjectChild::AnswerUnprotect()
{
  NS_ASSERTION(mObject->_class != GetClass(), "Bad object type!");
  NS_ASSERTION(mType == LocalObject, "Bad type!");

  Unprotect();
  return true;
}

bool
PluginScriptableObjectChild::Evaluate(NPString* aScript,
                                      NPVariant* aResult)
{
  nsDependentCString script("");
  if (aScript->UTF8Characters && aScript->UTF8Length) {
    script.Rebind(aScript->UTF8Characters, aScript->UTF8Length);
  }

  bool success;
  Variant result;
  CallNPN_Evaluate(script, &result, &success);

  if (!success) {
    return false;
  }

  ConvertToVariant(result, *aResult);
  return true;
}
