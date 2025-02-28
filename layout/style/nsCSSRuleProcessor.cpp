/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
// vim:cindent:tabstop=2:expandtab:shiftwidth=2:
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
 *   L. David Baron <dbaron@dbaron.org>
 *   Daniel Glazman <glazman@netscape.com>
 *   Ehsan Akhgari <ehsan.akhgari@gmail.com>
 *   Rob Arnold <robarnold@mozilla.com>
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
 * style rule processor for CSS style sheets, responsible for selector
 * matching and cascading
 */

#include "nsCSSRuleProcessor.h"
#include "nsRuleProcessorData.h"

#define PL_ARENA_CONST_ALIGN_MASK 7
#define NS_RULEHASH_ARENA_BLOCK_SIZE (256)
#include "plarena.h"

#include "nsCRT.h"
#include "nsIAtom.h"
#include "pldhash.h"
#include "nsHashtable.h"
#include "nsICSSPseudoComparator.h"
#include "nsCSSRuleProcessor.h"
#include "nsICSSStyleRule.h"
#include "nsICSSGroupRule.h"
#include "nsIDocument.h"
#include "nsPresContext.h"
#include "nsIEventStateManager.h"
#include "nsGkAtoms.h"
#include "nsString.h"
#include "nsUnicharUtils.h"
#include "nsDOMError.h"
#include "nsRuleWalker.h"
#include "nsCSSPseudoClasses.h"
#include "nsCSSPseudoElements.h"
#include "nsIContent.h"
#include "nsCOMPtr.h"
#include "nsHashKeys.h"
#include "nsStyleUtil.h"
#include "nsQuickSort.h"
#include "nsAttrValue.h"
#include "nsAttrName.h"
#include "nsILookAndFeel.h"
#include "nsWidgetsCID.h"
#include "nsServiceManagerUtils.h"
#include "nsTArray.h"
#include "nsContentUtils.h"
#include "nsIMediaList.h"
#include "nsCSSRules.h"
#include "nsIPrincipal.h"
#include "nsStyleSet.h"
#include "prlog.h"
#include "nsIObserverService.h"
#include "nsIPrivateBrowsingService.h"
#include "nsNetCID.h"

#define VISITED_PSEUDO_PREF "layout.css.visited_links_enabled"

static PRBool gSupportVisitedPseudo = PR_TRUE;

static NS_DEFINE_CID(kLookAndFeelCID, NS_LOOKANDFEEL_CID);
static nsTArray< nsCOMPtr<nsIAtom> >* sSystemMetrics = 0;

struct RuleValue {
  /**
   * |RuleValue|s are constructed before they become part of the
   * |RuleHash|, to act as rule/selector pairs.  |Add| is called when
   * they are added to the |RuleHash|, and can be considered the second
   * half of the constructor.
   *
   * |RuleValue|s are added to the rule hash from highest weight/order
   * to lowest (since this is the fast way to build a singly linked
   * list), so the index used to remember the order is backwards.
   */
  RuleValue(nsICSSStyleRule* aRule, nsCSSSelector* aSelector)
    : mRule(aRule), mSelector(aSelector) {}

  RuleValue* Add(PRInt32 aBackwardIndex, RuleValue *aNext)
  {
    mBackwardIndex = aBackwardIndex;
    mNext = aNext;
    return this;
  }
    
  // CAUTION: ~RuleValue will never get called as RuleValues are arena
  // allocated and arena cleanup will take care of deleting memory.
  // Add code to RuleHash::~RuleHash to get it to call the destructor
  // if any more cleanup needs to happen.
  ~RuleValue()
  {
    // Rule values are arena allocated. No need for any deletion.
  }

  // Placement new to arena allocate the RuleValues
  void *operator new(size_t aSize, PLArenaPool &aArena) CPP_THROW_NEW {
    void *mem;
    PL_ARENA_ALLOCATE(mem, &aArena, aSize);
    return mem;
  }

  nsICSSStyleRule*  mRule;
  nsCSSSelector*    mSelector; // which of |mRule|'s selectors
  PRInt32           mBackwardIndex; // High index means low weight/order.
  RuleValue*        mNext;
};

// ------------------------------
// Rule hash table
//

// Uses any of the sets of ops below.
struct RuleHashTableEntry : public PLDHashEntryHdr {
  RuleValue *mRules; // linked list of |RuleValue|, null-terminated
};

struct RuleHashTagTableEntry : public RuleHashTableEntry {
  nsCOMPtr<nsIAtom> mTag;
};

static PLDHashNumber
RuleHash_CIHashKey(PLDHashTable *table, const void *key)
{
  nsIAtom *atom = const_cast<nsIAtom*>(static_cast<const nsIAtom*>(key));

  nsAutoString str;
  atom->ToString(str);
  ToUpperCase(str);
  return HashString(str);
}

typedef nsIAtom*
(* RuleHashGetKey) (PLDHashTable *table, const PLDHashEntryHdr *entry);

struct RuleHashTableOps {
  PLDHashTableOps ops;
  // Extra callback to avoid duplicating the matchEntry callback for
  // each table.  (There used to be a getKey callback in
  // PLDHashTableOps.)
  RuleHashGetKey getKey;
};

inline const RuleHashTableOps*
ToLocalOps(const PLDHashTableOps *aOps)
{
  return (const RuleHashTableOps*)
           (((const char*) aOps) - offsetof(RuleHashTableOps, ops));
}

static PRBool
RuleHash_CIMatchEntry(PLDHashTable *table, const PLDHashEntryHdr *hdr,
                      const void *key)
{
  nsIAtom *match_atom = const_cast<nsIAtom*>(static_cast<const nsIAtom*>
                                              (key));
  // Use our extra |getKey| callback to avoid code duplication.
  nsIAtom *entry_atom = ToLocalOps(table->ops)->getKey(table, hdr);

  // Check for case-sensitive match first.
  if (match_atom == entry_atom)
    return PR_TRUE;

  const char *match_str, *entry_str;
  match_atom->GetUTF8String(&match_str);
  entry_atom->GetUTF8String(&entry_str);

  return (nsCRT::strcasecmp(entry_str, match_str) == 0);
}

static PRBool
RuleHash_CSMatchEntry(PLDHashTable *table, const PLDHashEntryHdr *hdr,
                      const void *key)
{
  nsIAtom *match_atom = const_cast<nsIAtom*>(static_cast<const nsIAtom*>
                                              (key));
  // Use our extra |getKey| callback to avoid code duplication.
  nsIAtom *entry_atom = ToLocalOps(table->ops)->getKey(table, hdr);

  return match_atom == entry_atom;
}

static PRBool
RuleHash_TagTable_MatchEntry(PLDHashTable *table, const PLDHashEntryHdr *hdr,
                      const void *key)
{
  nsIAtom *match_atom = const_cast<nsIAtom*>(static_cast<const nsIAtom*>
                                              (key));
  nsIAtom *entry_atom = static_cast<const RuleHashTagTableEntry*>(hdr)->mTag;

  return match_atom == entry_atom;
}

static void RuleHash_TagTable_ClearEntry(PLDHashTable *table, PLDHashEntryHdr *entry)
{
  (static_cast<RuleHashTagTableEntry*>(entry))->~RuleHashTagTableEntry();
}

static nsIAtom*
RuleHash_ClassTable_GetKey(PLDHashTable *table, const PLDHashEntryHdr *hdr)
{
  const RuleHashTableEntry *entry =
    static_cast<const RuleHashTableEntry*>(hdr);
  return entry->mRules->mSelector->mClassList->mAtom;
}

static nsIAtom*
RuleHash_IdTable_GetKey(PLDHashTable *table, const PLDHashEntryHdr *hdr)
{
  const RuleHashTableEntry *entry =
    static_cast<const RuleHashTableEntry*>(hdr);
  return entry->mRules->mSelector->mIDList->mAtom;
}

static PLDHashNumber
RuleHash_NameSpaceTable_HashKey(PLDHashTable *table, const void *key)
{
  return NS_PTR_TO_INT32(key);
}

static PRBool
RuleHash_NameSpaceTable_MatchEntry(PLDHashTable *table,
                                   const PLDHashEntryHdr *hdr,
                                   const void *key)
{
  const RuleHashTableEntry *entry =
    static_cast<const RuleHashTableEntry*>(hdr);

  return NS_PTR_TO_INT32(key) ==
         entry->mRules->mSelector->mNameSpace;
}

static const PLDHashTableOps RuleHash_TagTable_Ops = {
  PL_DHashAllocTable,
  PL_DHashFreeTable,
  PL_DHashVoidPtrKeyStub,
  RuleHash_TagTable_MatchEntry,
  PL_DHashMoveEntryStub,
  RuleHash_TagTable_ClearEntry,
  PL_DHashFinalizeStub,
  NULL
};

// Case-sensitive ops.
static const RuleHashTableOps RuleHash_ClassTable_CSOps = {
  {
  PL_DHashAllocTable,
  PL_DHashFreeTable,
  PL_DHashVoidPtrKeyStub,
  RuleHash_CSMatchEntry,
  PL_DHashMoveEntryStub,
  PL_DHashClearEntryStub,
  PL_DHashFinalizeStub,
  NULL
  },
  RuleHash_ClassTable_GetKey
};

// Case-insensitive ops.
static const RuleHashTableOps RuleHash_ClassTable_CIOps = {
  {
  PL_DHashAllocTable,
  PL_DHashFreeTable,
  RuleHash_CIHashKey,
  RuleHash_CIMatchEntry,
  PL_DHashMoveEntryStub,
  PL_DHashClearEntryStub,
  PL_DHashFinalizeStub,
  NULL
  },
  RuleHash_ClassTable_GetKey
};

// Case-sensitive ops.
static const RuleHashTableOps RuleHash_IdTable_CSOps = {
  {
  PL_DHashAllocTable,
  PL_DHashFreeTable,
  PL_DHashVoidPtrKeyStub,
  RuleHash_CSMatchEntry,
  PL_DHashMoveEntryStub,
  PL_DHashClearEntryStub,
  PL_DHashFinalizeStub,
  NULL
  },
  RuleHash_IdTable_GetKey
};

// Case-insensitive ops.
static const RuleHashTableOps RuleHash_IdTable_CIOps = {
  {
  PL_DHashAllocTable,
  PL_DHashFreeTable,
  RuleHash_CIHashKey,
  RuleHash_CIMatchEntry,
  PL_DHashMoveEntryStub,
  PL_DHashClearEntryStub,
  PL_DHashFinalizeStub,
  NULL
  },
  RuleHash_IdTable_GetKey
};

static const PLDHashTableOps RuleHash_NameSpaceTable_Ops = {
  PL_DHashAllocTable,
  PL_DHashFreeTable,
  RuleHash_NameSpaceTable_HashKey,
  RuleHash_NameSpaceTable_MatchEntry,
  PL_DHashMoveEntryStub,
  PL_DHashClearEntryStub,
  PL_DHashFinalizeStub,
  NULL,
};

#undef RULE_HASH_STATS
#undef PRINT_UNIVERSAL_RULES

#ifdef RULE_HASH_STATS
#define RULE_HASH_STAT_INCREMENT(var_) PR_BEGIN_MACRO ++(var_); PR_END_MACRO
#else
#define RULE_HASH_STAT_INCREMENT(var_) PR_BEGIN_MACRO PR_END_MACRO
#endif

// Enumerator callback function.
typedef void (*RuleEnumFunc)(nsICSSStyleRule* aRule,
                             nsCSSSelector* aSelector,
                             void *aData);

class RuleHash {
public:
  RuleHash(PRBool aQuirksMode);
  ~RuleHash();
  void PrependRule(RuleValue *aRuleInfo);
  void EnumerateAllRules(PRInt32 aNameSpace, nsIAtom* aTag, nsIAtom* aID,
                         const nsAttrValue* aClassList,
                         RuleEnumFunc aFunc, RuleProcessorData* aData);
  PLArenaPool& Arena() { return mArena; }

protected:
  void PrependRuleToTable(PLDHashTable* aTable, const void* aKey,
                          RuleValue* aRuleInfo);
  void PrependRuleToTagTable(nsIAtom* aKey, RuleValue* aRuleInfo);
  void PrependUniversalRule(RuleValue* aRuleInfo);

  // All rule values in these hashtables are arena allocated
  PRInt32     mRuleCount;
  PLDHashTable mIdTable;
  PLDHashTable mClassTable;
  PLDHashTable mTagTable;
  PLDHashTable mNameSpaceTable;
  RuleValue *mUniversalRules;

  RuleValue** mEnumList;
  PRInt32     mEnumListSize;

  PLArenaPool mArena;

#ifdef RULE_HASH_STATS
  PRUint32    mUniversalSelectors;
  PRUint32    mNameSpaceSelectors;
  PRUint32    mTagSelectors;
  PRUint32    mClassSelectors;
  PRUint32    mIdSelectors;

  PRUint32    mElementsMatched;

  PRUint32    mElementUniversalCalls;
  PRUint32    mElementNameSpaceCalls;
  PRUint32    mElementTagCalls;
  PRUint32    mElementClassCalls;
  PRUint32    mElementIdCalls;
#endif // RULE_HASH_STATS
};

RuleHash::RuleHash(PRBool aQuirksMode)
  : mRuleCount(0),
    mUniversalRules(nsnull),
    mEnumList(nsnull), mEnumListSize(0)
#ifdef RULE_HASH_STATS
    ,
    mUniversalSelectors(0),
    mNameSpaceSelectors(0),
    mTagSelectors(0),
    mClassSelectors(0),
    mIdSelectors(0),
    mElementsMatched(0),
    mElementUniversalCalls(0),
    mElementNameSpaceCalls(0),
    mElementTagCalls(0),
    mElementClassCalls(0),
    mElementIdCalls(0)
#endif
{
  MOZ_COUNT_CTOR(RuleHash);
  // Initialize our arena
  PL_INIT_ARENA_POOL(&mArena, "RuleHashArena", NS_RULEHASH_ARENA_BLOCK_SIZE);

  PL_DHashTableInit(&mTagTable, &RuleHash_TagTable_Ops, nsnull,
                    sizeof(RuleHashTagTableEntry), 64);
  PL_DHashTableInit(&mIdTable,
                    aQuirksMode ? &RuleHash_IdTable_CIOps.ops
                                : &RuleHash_IdTable_CSOps.ops,
                    nsnull, sizeof(RuleHashTableEntry), 16);
  PL_DHashTableInit(&mClassTable,
                    aQuirksMode ? &RuleHash_ClassTable_CIOps.ops
                                : &RuleHash_ClassTable_CSOps.ops,
                    nsnull, sizeof(RuleHashTableEntry), 16);
  PL_DHashTableInit(&mNameSpaceTable, &RuleHash_NameSpaceTable_Ops, nsnull,
                    sizeof(RuleHashTableEntry), 16);
}

RuleHash::~RuleHash()
{
  MOZ_COUNT_DTOR(RuleHash);
#ifdef RULE_HASH_STATS
  printf(
"RuleHash(%p):\n"
"  Selectors: Universal (%u) NameSpace(%u) Tag(%u) Class(%u) Id(%u)\n"
"  Content Nodes: Elements(%u)\n"
"  Element Calls: Universal(%u) NameSpace(%u) Tag(%u) Class(%u) Id(%u)\n"
         static_cast<void*>(this),
         mUniversalSelectors, mNameSpaceSelectors, mTagSelectors,
           mClassSelectors, mIdSelectors,
         mElementsMatched,
         mElementUniversalCalls, mElementNameSpaceCalls, mElementTagCalls,
           mElementClassCalls, mElementIdCalls);
#ifdef PRINT_UNIVERSAL_RULES
  {
    RuleValue* value = mUniversalRules;
    if (value) {
      printf("  Universal rules:\n");
      do {
        nsAutoString selectorText;
        PRUint32 lineNumber = value->mRule->GetLineNumber();
        nsCOMPtr<nsIStyleSheet> sheet;
        value->mRule->GetStyleSheet(*getter_AddRefs(sheet));
        nsCOMPtr<nsICSSStyleSheet> cssSheet = do_QueryInterface(sheet);
        value->mSelector->ToString(selectorText, cssSheet);

        printf("    line %d, %s\n",
               lineNumber, NS_ConvertUTF16toUTF8(selectorText).get());
        value = value->mNext;
      } while (value);
    }
  }
#endif // PRINT_UNIVERSAL_RULES
#endif // RULE_HASH_STATS
  // Rule Values are arena allocated no need to delete them. Their destructor
  // isn't doing any cleanup. So we dont even bother to enumerate through
  // the hash tables and call their destructors.
  if (nsnull != mEnumList) {
    delete [] mEnumList;
  }
  // delete arena for strings and small objects
  PL_DHashTableFinish(&mIdTable);
  PL_DHashTableFinish(&mClassTable);
  PL_DHashTableFinish(&mTagTable);
  PL_DHashTableFinish(&mNameSpaceTable);
  PL_FinishArenaPool(&mArena);
}

void RuleHash::PrependRuleToTable(PLDHashTable* aTable, const void* aKey,
                                  RuleValue* aRuleInfo)
{
  // Get a new or existing entry.
  RuleHashTableEntry *entry = static_cast<RuleHashTableEntry*>
                                         (PL_DHashTableOperate(aTable, aKey, PL_DHASH_ADD));
  if (!entry)
    return;
  entry->mRules = aRuleInfo->Add(mRuleCount++, entry->mRules);
}

static void
DoPrependRuleToTagTable(PLDHashTable* aTable, nsIAtom* aKey,
                        RuleValue* aRuleInfo, PRInt32 aBackwardsIndex)
{
  // Get a new or exisiting entry
  RuleHashTagTableEntry *entry = static_cast<RuleHashTagTableEntry*>
    (PL_DHashTableOperate(aTable, aKey, PL_DHASH_ADD));
  if (!entry)
    return;

  entry->mTag = aKey;

  // This may give the same rule two different rule counts, but that is OK
  // because we never combine two different entries in a tag table.
  entry->mRules = aRuleInfo->Add(aBackwardsIndex, entry->mRules);
}

void RuleHash::PrependRuleToTagTable(nsIAtom* aKey, RuleValue* aRuleInfo)
{
  DoPrependRuleToTagTable(&mTagTable, aKey, aRuleInfo, mRuleCount++);
}

void RuleHash::PrependUniversalRule(RuleValue *aRuleInfo)
{
  mUniversalRules = aRuleInfo->Add(mRuleCount++, mUniversalRules);
}

void RuleHash::PrependRule(RuleValue *aRuleInfo)
{
  nsCSSSelector *selector = aRuleInfo->mSelector;
  if (nsnull != selector->mIDList) {
    PrependRuleToTable(&mIdTable, selector->mIDList->mAtom, aRuleInfo);
    RULE_HASH_STAT_INCREMENT(mIdSelectors);
  }
  else if (nsnull != selector->mClassList) {
    PrependRuleToTable(&mClassTable, selector->mClassList->mAtom, aRuleInfo);
    RULE_HASH_STAT_INCREMENT(mClassSelectors);
  }
  else if (selector->mLowercaseTag) {
    PrependRuleToTagTable(selector->mLowercaseTag, aRuleInfo);
    RULE_HASH_STAT_INCREMENT(mTagSelectors);
    if (selector->mCasedTag && 
        selector->mCasedTag != selector->mLowercaseTag) {
      PrependRuleToTagTable(selector->mCasedTag, 
                            new (mArena) RuleValue(aRuleInfo->mRule, 
                                                   aRuleInfo->mSelector));
      RULE_HASH_STAT_INCREMENT(mTagSelectors);
    }
  }
  else if (kNameSpaceID_Unknown != selector->mNameSpace) {
    PrependRuleToTable(&mNameSpaceTable,
                       NS_INT32_TO_PTR(selector->mNameSpace), aRuleInfo);
    RULE_HASH_STAT_INCREMENT(mNameSpaceSelectors);
  }
  else {  // universal tag selector
    PrependUniversalRule(aRuleInfo);
    RULE_HASH_STAT_INCREMENT(mUniversalSelectors);
  }
}

// this should cover practically all cases so we don't need to reallocate
#define MIN_ENUM_LIST_SIZE 8

#ifdef RULE_HASH_STATS
#define RULE_HASH_STAT_INCREMENT_LIST_COUNT(list_, var_) \
  do { ++(var_); (list_) = (list_)->mNext; } while (list_)
#else
#define RULE_HASH_STAT_INCREMENT_LIST_COUNT(list_, var_) \
  PR_BEGIN_MACRO PR_END_MACRO
#endif

void RuleHash::EnumerateAllRules(PRInt32 aNameSpace, nsIAtom* aTag,
                                 nsIAtom* aID, const nsAttrValue* aClassList,
                                 RuleEnumFunc aFunc, RuleProcessorData* aData)
{
  PRInt32 classCount = aClassList ? aClassList->GetAtomCount() : 0;

  // assume 1 universal, tag, id, and namespace, rather than wasting
  // time counting
  PRInt32 testCount = classCount + 4;

  if (mEnumListSize < testCount) {
    delete [] mEnumList;
    mEnumListSize = NS_MAX(testCount, MIN_ENUM_LIST_SIZE);
    mEnumList = new RuleValue*[mEnumListSize];
  }

  PRInt32 valueCount = 0;
  RULE_HASH_STAT_INCREMENT(mElementsMatched);

  { // universal rules
    RuleValue* value = mUniversalRules;
    if (nsnull != value) {
      mEnumList[valueCount++] = value;
      RULE_HASH_STAT_INCREMENT_LIST_COUNT(value, mElementUniversalCalls);
    }
  }
  // universal rules within the namespace
  if (kNameSpaceID_Unknown != aNameSpace && mNameSpaceTable.entryCount) {
    RuleHashTableEntry *entry = static_cast<RuleHashTableEntry*>
                                           (PL_DHashTableOperate(&mNameSpaceTable, NS_INT32_TO_PTR(aNameSpace),
                             PL_DHASH_LOOKUP));
    if (PL_DHASH_ENTRY_IS_BUSY(entry)) {
      RuleValue *value = entry->mRules;
      mEnumList[valueCount++] = value;
      RULE_HASH_STAT_INCREMENT_LIST_COUNT(value, mElementNameSpaceCalls);
    }
  }
  if (aTag && mTagTable.entryCount) {
    RuleHashTableEntry *entry = static_cast<RuleHashTableEntry*>
                                           (PL_DHashTableOperate(&mTagTable, aTag, PL_DHASH_LOOKUP));
    if (PL_DHASH_ENTRY_IS_BUSY(entry)) {
      RuleValue *value = entry->mRules;
      mEnumList[valueCount++] = value;
      RULE_HASH_STAT_INCREMENT_LIST_COUNT(value, mElementTagCalls);
    }
  }
  if (aID && mIdTable.entryCount) {
    RuleHashTableEntry *entry = static_cast<RuleHashTableEntry*>
                                           (PL_DHashTableOperate(&mIdTable, aID, PL_DHASH_LOOKUP));
    if (PL_DHASH_ENTRY_IS_BUSY(entry)) {
      RuleValue *value = entry->mRules;
      mEnumList[valueCount++] = value;
      RULE_HASH_STAT_INCREMENT_LIST_COUNT(value, mElementIdCalls);
    }
  }
  if (mClassTable.entryCount) {
    for (PRInt32 index = 0; index < classCount; ++index) {
      RuleHashTableEntry *entry = static_cast<RuleHashTableEntry*>
                                             (PL_DHashTableOperate(&mClassTable, aClassList->AtomAt(index),
                             PL_DHASH_LOOKUP));
      if (PL_DHASH_ENTRY_IS_BUSY(entry)) {
        RuleValue *value = entry->mRules;
        mEnumList[valueCount++] = value;
        RULE_HASH_STAT_INCREMENT_LIST_COUNT(value, mElementClassCalls);
      }
    }
  }
  NS_ASSERTION(valueCount <= testCount, "values exceeded list size");

  if (valueCount > 0) {
    // Merge the lists while there are still multiple lists to merge.
    while (valueCount > 1) {
      PRInt32 valueIndex = 0;
      PRInt32 highestRuleIndex = mEnumList[valueIndex]->mBackwardIndex;
      for (PRInt32 index = 1; index < valueCount; ++index) {
        PRInt32 ruleIndex = mEnumList[index]->mBackwardIndex;
        if (ruleIndex > highestRuleIndex) {
          valueIndex = index;
          highestRuleIndex = ruleIndex;
        }
      }
      RuleValue *cur = mEnumList[valueIndex];
      (*aFunc)(cur->mRule, cur->mSelector, aData);
      RuleValue *next = cur->mNext;
      mEnumList[valueIndex] = next ? next : mEnumList[--valueCount];
    }

    // Fast loop over single value.
    RuleValue* value = mEnumList[0];
    do {
      (*aFunc)(value->mRule, value->mSelector, aData);
      value = value->mNext;
    } while (value);
  }
}

//--------------------------------

// Attribute selectors hash table.
struct AttributeSelectorEntry : public PLDHashEntryHdr {
  nsIAtom *mAttribute;
  nsTArray<nsCSSSelector*> *mSelectors;
};

static void
AttributeSelectorClearEntry(PLDHashTable *table, PLDHashEntryHdr *hdr)
{
  AttributeSelectorEntry *entry = static_cast<AttributeSelectorEntry*>(hdr);
  delete entry->mSelectors;
  memset(entry, 0, table->entrySize);
}

static const PLDHashTableOps AttributeSelectorOps = {
  PL_DHashAllocTable,
  PL_DHashFreeTable,
  PL_DHashVoidPtrKeyStub,
  PL_DHashMatchEntryStub,
  PL_DHashMoveEntryStub,
  AttributeSelectorClearEntry,
  PL_DHashFinalizeStub,
  NULL
};


//--------------------------------

struct RuleCascadeData {
  RuleCascadeData(nsIAtom *aMedium, PRBool aQuirksMode)
    : mRuleHash(aQuirksMode),
      mStateSelectors(),
      mCacheKey(aMedium),
      mNext(nsnull),
      mQuirksMode(aQuirksMode)
  {
    PL_DHashTableInit(&mAttributeSelectors, &AttributeSelectorOps, nsnull,
                      sizeof(AttributeSelectorEntry), 16);
    PL_DHashTableInit(&mAnonBoxRules, &RuleHash_TagTable_Ops, nsnull,
                      sizeof(RuleHashTagTableEntry), 16);
    memset(mPseudoElementRuleHashes, 0, sizeof(mPseudoElementRuleHashes));
#ifdef MOZ_XUL
    PL_DHashTableInit(&mXULTreeRules, &RuleHash_TagTable_Ops, nsnull,
                      sizeof(RuleHashTagTableEntry), 16);
#endif
  }

  ~RuleCascadeData()
  {
    PL_DHashTableFinish(&mAttributeSelectors);
    PL_DHashTableFinish(&mAnonBoxRules);
    PL_DHashTableFinish(&mXULTreeRules);
    for (PRUint32 i = 0; i < NS_ARRAY_LENGTH(mPseudoElementRuleHashes); ++i) {
      delete mPseudoElementRuleHashes[i];
    }
  }
  RuleHash                 mRuleHash;
  RuleHash*
    mPseudoElementRuleHashes[nsCSSPseudoElements::ePseudo_PseudoElementCount];
  nsTArray<nsCSSSelector*> mStateSelectors;
  nsTArray<nsCSSSelector*> mClassSelectors;
  nsTArray<nsCSSSelector*> mIDSelectors;
  PLDHashTable             mAttributeSelectors;
  PLDHashTable             mAnonBoxRules;
#ifdef MOZ_XUL
  PLDHashTable             mXULTreeRules;
#endif

  nsTArray<nsFontFaceRuleContainer> mFontFaceRules;

  // Looks up or creates the appropriate list in |mAttributeSelectors|.
  // Returns null only on allocation failure.
  nsTArray<nsCSSSelector*>* AttributeListFor(nsIAtom* aAttribute);

  nsMediaQueryResultCacheKey mCacheKey;
  RuleCascadeData*  mNext; // for a different medium

  const PRBool mQuirksMode;
};

nsTArray<nsCSSSelector*>*
RuleCascadeData::AttributeListFor(nsIAtom* aAttribute)
{
  AttributeSelectorEntry *entry = static_cast<AttributeSelectorEntry*>
                                             (PL_DHashTableOperate(&mAttributeSelectors, aAttribute, PL_DHASH_ADD));
  if (!entry)
    return nsnull;
  if (!entry->mSelectors) {
    if (!(entry->mSelectors = new nsTArray<nsCSSSelector*>)) {
      PL_DHashTableRawRemove(&mAttributeSelectors, entry);
      return nsnull;
    }
    entry->mAttribute = aAttribute;
  }
  return entry->mSelectors;
}

class nsPrivateBrowsingObserver : nsIObserver,
                                  nsSupportsWeakReference
{
public:
  nsPrivateBrowsingObserver();

  NS_DECL_ISUPPORTS
  NS_DECL_NSIOBSERVER

  void Init();
  PRBool InPrivateBrowsing() const { return mInPrivateBrowsing; }

private:
  PRBool mInPrivateBrowsing;
};

NS_IMPL_ISUPPORTS2(nsPrivateBrowsingObserver, nsIObserver, nsISupportsWeakReference)

nsPrivateBrowsingObserver::nsPrivateBrowsingObserver()
  : mInPrivateBrowsing(PR_FALSE)
{
}

void
nsPrivateBrowsingObserver::Init()
{
  nsCOMPtr<nsIPrivateBrowsingService> pbService =
    do_GetService(NS_PRIVATE_BROWSING_SERVICE_CONTRACTID);
  if (pbService) {
    pbService->GetPrivateBrowsingEnabled(&mInPrivateBrowsing);

    nsCOMPtr<nsIObserverService> observerService =
      do_GetService("@mozilla.org/observer-service;1");
    if (observerService) {
      observerService->AddObserver(this, NS_PRIVATE_BROWSING_SWITCH_TOPIC, PR_TRUE);
    }
  }
}

nsresult
nsPrivateBrowsingObserver::Observe(nsISupports *aSubject,
                                   const char *aTopic,
                                   const PRUnichar *aData)
{
  if (!strcmp(aTopic, NS_PRIVATE_BROWSING_SWITCH_TOPIC)) {
    if (!nsCRT::strcmp(aData, NS_LITERAL_STRING(NS_PRIVATE_BROWSING_ENTER).get())) {
      mInPrivateBrowsing = PR_TRUE;
    } else {
      mInPrivateBrowsing = PR_FALSE;
    }
  }
  return NS_OK;
}

static nsPrivateBrowsingObserver *gPrivateBrowsingObserver = nsnull;

// -------------------------------
// CSS Style rule processor implementation
//

nsCSSRuleProcessor::nsCSSRuleProcessor(const nsCOMArray<nsICSSStyleSheet>& aSheets,
                                       PRUint8 aSheetType)
  : mSheets(aSheets)
  , mRuleCascades(nsnull)
  , mLastPresContext(nsnull)
  , mSheetType(aSheetType)
{
  for (PRInt32 i = mSheets.Count() - 1; i >= 0; --i)
    mSheets[i]->AddRuleProcessor(this);
}

nsCSSRuleProcessor::~nsCSSRuleProcessor()
{
  for (PRInt32 i = mSheets.Count() - 1; i >= 0; --i)
    mSheets[i]->DropRuleProcessor(this);
  mSheets.Clear();
  ClearRuleCascades();
}

NS_IMPL_ISUPPORTS1(nsCSSRuleProcessor, nsIStyleRuleProcessor)

/* static */ nsresult
nsCSSRuleProcessor::Startup()
{
  nsContentUtils::AddBoolPrefVarCache(VISITED_PSEUDO_PREF,
                                      &gSupportVisitedPseudo);
  // We want to default to true, not false as AddBoolPrefVarCache does.
  gSupportVisitedPseudo =
    nsContentUtils::GetBoolPref(VISITED_PSEUDO_PREF, PR_TRUE);

  gPrivateBrowsingObserver = new nsPrivateBrowsingObserver();
  NS_ENSURE_TRUE(gPrivateBrowsingObserver, NS_ERROR_OUT_OF_MEMORY);
  NS_ADDREF(gPrivateBrowsingObserver);
  gPrivateBrowsingObserver->Init();

  return NS_OK;
}

static PRBool
InitSystemMetrics()
{
  NS_ASSERTION(!sSystemMetrics, "already initialized");

  sSystemMetrics = new nsTArray< nsCOMPtr<nsIAtom> >;
  NS_ENSURE_TRUE(sSystemMetrics, PR_FALSE);

  nsresult rv;
  nsCOMPtr<nsILookAndFeel> lookAndFeel(do_GetService(kLookAndFeelCID, &rv));
  NS_ENSURE_SUCCESS(rv, PR_FALSE);

  /***************************************************************************
   * ANY METRICS ADDED HERE SHOULD ALSO BE ADDED AS MEDIA QUERIES IN         *
   * nsMediaFeatures.cpp                                                     *
   ***************************************************************************/

  PRInt32 metricResult;
  lookAndFeel->GetMetric(nsILookAndFeel::eMetric_ScrollArrowStyle, metricResult);
  if (metricResult & nsILookAndFeel::eMetric_ScrollArrowStartBackward) {
    sSystemMetrics->AppendElement(nsGkAtoms::scrollbar_start_backward);
  }
  if (metricResult & nsILookAndFeel::eMetric_ScrollArrowStartForward) {
    sSystemMetrics->AppendElement(nsGkAtoms::scrollbar_start_forward);
  }
  if (metricResult & nsILookAndFeel::eMetric_ScrollArrowEndBackward) {
    sSystemMetrics->AppendElement(nsGkAtoms::scrollbar_end_backward);
  }
  if (metricResult & nsILookAndFeel::eMetric_ScrollArrowEndForward) {
    sSystemMetrics->AppendElement(nsGkAtoms::scrollbar_end_forward);
  }

  lookAndFeel->GetMetric(nsILookAndFeel::eMetric_ScrollSliderStyle, metricResult);
  if (metricResult != nsILookAndFeel::eMetric_ScrollThumbStyleNormal) {
    sSystemMetrics->AppendElement(nsGkAtoms::scrollbar_thumb_proportional);
  }

  lookAndFeel->GetMetric(nsILookAndFeel::eMetric_ImagesInMenus, metricResult);
  if (metricResult) {
    sSystemMetrics->AppendElement(nsGkAtoms::images_in_menus);
  }

  lookAndFeel->GetMetric(nsILookAndFeel::eMetric_ImagesInButtons, metricResult);
  if (metricResult) {
    sSystemMetrics->AppendElement(nsGkAtoms::images_in_buttons);
  }

  rv = lookAndFeel->GetMetric(nsILookAndFeel::eMetric_WindowsDefaultTheme, metricResult);
  if (NS_SUCCEEDED(rv) && metricResult) {
    sSystemMetrics->AppendElement(nsGkAtoms::windows_default_theme);
  }

  rv = lookAndFeel->GetMetric(nsILookAndFeel::eMetric_MacGraphiteTheme, metricResult);
  if (NS_SUCCEEDED(rv) && metricResult) {
    sSystemMetrics->AppendElement(nsGkAtoms::mac_graphite_theme);
  }

  rv = lookAndFeel->GetMetric(nsILookAndFeel::eMetric_DWMCompositor, metricResult);
  if (NS_SUCCEEDED(rv) && metricResult) {
    sSystemMetrics->AppendElement(nsGkAtoms::windows_compositor);
  }

  rv = lookAndFeel->GetMetric(nsILookAndFeel::eMetric_WindowsClassic, metricResult);
  if (NS_SUCCEEDED(rv) && metricResult) {
    sSystemMetrics->AppendElement(nsGkAtoms::windows_classic);
  }

  rv = lookAndFeel->GetMetric(nsILookAndFeel::eMetric_TouchEnabled, metricResult);
  if (NS_SUCCEEDED(rv) && metricResult) {
    sSystemMetrics->AppendElement(nsGkAtoms::touch_enabled);
  }
 
  rv = lookAndFeel->GetMetric(nsILookAndFeel::eMetric_MaemoClassic, metricResult);
  if (NS_SUCCEEDED(rv) && metricResult) {
    sSystemMetrics->AppendElement(nsGkAtoms::maemo_classic);
  }

  return PR_TRUE;
}

/* static */ void
nsCSSRuleProcessor::FreeSystemMetrics()
{
  delete sSystemMetrics;
  sSystemMetrics = nsnull;
}

/* static */ void
nsCSSRuleProcessor::Shutdown()
{
  FreeSystemMetrics();
  NS_RELEASE(gPrivateBrowsingObserver);
}

/* static */ PRBool
nsCSSRuleProcessor::HasSystemMetric(nsIAtom* aMetric)
{
  if (!sSystemMetrics && !InitSystemMetrics()) {
    return PR_FALSE;
  }
  return sSystemMetrics->IndexOf(aMetric) != sSystemMetrics->NoIndex;
}

RuleProcessorData::RuleProcessorData(nsPresContext* aPresContext,
                                     nsIContent* aContent, 
                                     nsRuleWalker* aRuleWalker,
                                     nsCompatibility* aCompat /*= nsnull*/)
  : mPresContext(aPresContext),
    mContent(aContent),
    mRuleWalker(aRuleWalker),
    mScopedRoot(nsnull),
    mPreviousSiblingData(nsnull),
    mParentData(nsnull),
    mLanguage(nsnull),
    mGotContentState(PR_FALSE)
{
  MOZ_COUNT_CTOR(RuleProcessorData);

  NS_ASSERTION(aContent && aContent->IsNodeOfType(nsINode::eELEMENT),
               "non-element leaked into SelectorMatches");

  mNthIndices[0][0] = -2;
  mNthIndices[0][1] = -2;
  mNthIndices[1][0] = -2;
  mNthIndices[1][1] = -2;

  // get the compat. mode (unless it is provided)
  // XXXbz is passing in the compat mode really that much of an optimization?
  if (aCompat) {
    mCompatMode = *aCompat;
  } else if (NS_LIKELY(mPresContext)) {
    mCompatMode = mPresContext->CompatibilityMode();
  } else {
    NS_ASSERTION(aContent, "Must have content");
    NS_ASSERTION(aContent->GetOwnerDoc(), "Must have document");
    mCompatMode = aContent->GetOwnerDoc()->GetCompatibilityMode();
  }

  NS_ASSERTION(aContent->GetOwnerDoc(), "Document-less node here?");
    
  // get the tag and parent
  mContentTag = aContent->Tag();
  mParentContent = aContent->GetParent();

  // see if there are attributes for the content
  mHasAttributes = aContent->GetAttrCount() > 0;
  if (mHasAttributes) {
    // get the ID and classes for the content
    mContentID = aContent->GetID();
    mClasses = aContent->GetClasses();
  } else {
    mContentID = nsnull;
    mClasses = nsnull;
  }

  // get the namespace
  mNameSpaceID = aContent->GetNameSpaceID();

  // check for HTMLContent status
  mIsHTMLContent = (mNameSpaceID == kNameSpaceID_XHTML);
  mIsHTML = mIsHTMLContent && aContent->IsInHTMLDocument();

  // No need to initialize mContentState; the ContentState() accessor will handle
  // that.
}

RuleProcessorData::~RuleProcessorData()
{
  MOZ_COUNT_DTOR(RuleProcessorData);

  // Destroy potentially long chains of previous sibling and parent data
  // without more than one level of recursion.
  if (mPreviousSiblingData || mParentData) {
    nsAutoVoidArray destroyQueue;
    destroyQueue.AppendElement(this);

    do {
      RuleProcessorData *d = static_cast<RuleProcessorData*>
                                        (destroyQueue.FastElementAt(destroyQueue.Count() - 1));
      destroyQueue.RemoveElementAt(destroyQueue.Count() - 1);

      if (d->mPreviousSiblingData) {
        destroyQueue.AppendElement(d->mPreviousSiblingData);
        d->mPreviousSiblingData = nsnull;
      }
      if (d->mParentData) {
        destroyQueue.AppendElement(d->mParentData);
        d->mParentData = nsnull;
      }

      if (d != this)
        d->Destroy();
    } while (destroyQueue.Count());
  }

  delete mLanguage;
}

const nsString* RuleProcessorData::GetLang()
{
  if (!mLanguage) {
    mLanguage = new nsString();
    if (!mLanguage)
      return nsnull;
    for (nsIContent* content = mContent; content;
         content = content->GetParent()) {
      if (content->GetAttrCount() > 0) {
        // xml:lang has precedence over lang on HTML elements (see
        // XHTML1 section C.7).
        PRBool hasAttr = content->GetAttr(kNameSpaceID_XML, nsGkAtoms::lang,
                                          *mLanguage);
        if (!hasAttr && content->IsHTML()) {
          hasAttr = content->GetAttr(kNameSpaceID_None, nsGkAtoms::lang,
                                     *mLanguage);
        }
        NS_ASSERTION(hasAttr || mLanguage->IsEmpty(),
                     "GetAttr that returns false should not make string non-empty");
        if (hasAttr) {
          break;
        }
      }
    }
  }
  return mLanguage;
}

PRUint32
RuleProcessorData::ContentState()
{
  if (!mGotContentState) {
    mGotContentState = PR_TRUE;
    mContentState = 0;
    if (mPresContext) {
      mPresContext->EventStateManager()->GetContentState(mContent,
                                                         mContentState);
    } else {
      mContentState = mContent->IntrinsicState();
    }

    // If we are not supposed to mark visited links as such, be sure to flip the
    // bits appropriately.
    if ((!gSupportVisitedPseudo ||
        gPrivateBrowsingObserver->InPrivateBrowsing()) &&
        (mContentState & NS_EVENT_STATE_VISITED)) {
      mContentState = (mContentState & ~PRUint32(NS_EVENT_STATE_VISITED)) |
                      NS_EVENT_STATE_UNVISITED;
    }
  }
  return mContentState;
}

PRBool
RuleProcessorData::IsLink()
{
  PRUint32 state = ContentState();
  return (state & (NS_EVENT_STATE_VISITED | NS_EVENT_STATE_UNVISITED)) != 0;
}

PRInt32
RuleProcessorData::GetNthIndex(PRBool aIsOfType, PRBool aIsFromEnd,
                               PRBool aCheckEdgeOnly)
{
  NS_ASSERTION(mParentContent, "caller should check mParentContent");
  NS_ASSERTION(!mPreviousSiblingData ||
               mPreviousSiblingData->mContent->IsNodeOfType(nsINode::eELEMENT),
               "Unexpected previous sibling data");

  PRInt32 &slot = mNthIndices[aIsOfType][aIsFromEnd];
  if (slot != -2 && (slot != -1 || aCheckEdgeOnly))
    return slot;

  if (mPreviousSiblingData &&
      (!aIsOfType ||
       (mPreviousSiblingData->mNameSpaceID == mNameSpaceID &&
        mPreviousSiblingData->mContentTag == mContentTag))) {
    slot = mPreviousSiblingData->mNthIndices[aIsOfType][aIsFromEnd];
    if (slot > 0) {
      slot += (aIsFromEnd ? -1 : 1);
      NS_ASSERTION(slot > 0, "How did that happen?");
      return slot;
    }
  }

  PRInt32 result = 1;
  nsIContent* parent = mParentContent;

  PRUint32 childCount;
  nsIContent * const * curChildPtr = parent->GetChildArray(&childCount);

#ifdef DEBUG
  nsMutationGuard debugMutationGuard;
#endif  
  
  PRInt32 increment;
  nsIContent * const * stopPtr;
  if (aIsFromEnd) {
    stopPtr = curChildPtr - 1;
    curChildPtr = stopPtr + childCount;
    increment = -1;
  } else {
    increment = 1;
    stopPtr = curChildPtr + childCount;
  }

  for ( ; ; curChildPtr += increment) {
    if (curChildPtr == stopPtr) {
      // mContent is the root of an anonymous content subtree.
      result = 0; // special value to indicate that it is not at any index
      break;
    }
    nsIContent* child = *curChildPtr;
    if (child == mContent)
      break;
    if (child->IsNodeOfType(nsINode::eELEMENT) &&
        (!aIsOfType ||
         (child->Tag() == mContentTag &&
          child->GetNameSpaceID() == mNameSpaceID))) {
      if (aCheckEdgeOnly) {
        // The caller only cares whether or not the result is 1, and we
        // now know it's not.
        result = -1;
        break;
      }
      ++result;
    }
  }

#ifdef DEBUG
  NS_ASSERTION(!debugMutationGuard.Mutated(0), "Unexpected mutations happened");
#endif  

  slot = result;
  return result;
}

static PRBool ValueIncludes(const nsSubstring& aValueList,
                            const nsSubstring& aValue,
                            const nsStringComparator& aComparator)
{
  const PRUnichar *p = aValueList.BeginReading(),
              *p_end = aValueList.EndReading();

  while (p < p_end) {
    // skip leading space
    while (p != p_end && nsContentUtils::IsHTMLWhitespace(*p))
      ++p;

    const PRUnichar *val_start = p;

    // look for space or end
    while (p != p_end && !nsContentUtils::IsHTMLWhitespace(*p))
      ++p;

    const PRUnichar *val_end = p;

    if (val_start < val_end &&
        aValue.Equals(Substring(val_start, val_end), aComparator))
      return PR_TRUE;

    ++p; // we know the next character is not whitespace
  }
  return PR_FALSE;
}

// Return whether we should apply a "global" (i.e., universal-tag)
// selector for event states in quirks mode.  Note that
// |data.IsLink()| is checked separately by the caller, so we return
// false for |nsGkAtoms::a|, which here means a named anchor.
inline PRBool IsQuirkEventSensitive(nsIAtom *aContentTag)
{
  return PRBool ((nsGkAtoms::button == aContentTag) ||
                 (nsGkAtoms::img == aContentTag)    ||
                 (nsGkAtoms::input == aContentTag)  ||
                 (nsGkAtoms::label == aContentTag)  ||
                 (nsGkAtoms::select == aContentTag) ||
                 (nsGkAtoms::textarea == aContentTag));
}


static inline PRBool
IsSignificantChild(nsIContent* aChild, PRBool aTextIsSignificant,
                   PRBool aWhitespaceIsSignificant)
{
  return nsStyleUtil::IsSignificantChild(aChild, aTextIsSignificant,
                                         aWhitespaceIsSignificant);
}

// This function is to be called once we have fetched a value for an attribute
// whose namespace and name match those of aAttrSelector.  This function
// performs comparisons on the value only, based on aAttrSelector->mFunction.
static PRBool AttrMatchesValue(const nsAttrSelector* aAttrSelector,
                               const nsString& aValue, PRBool isHTML)
{
  NS_PRECONDITION(aAttrSelector, "Must have an attribute selector");

  // http://lists.w3.org/Archives/Public/www-style/2008Apr/0038.html
  // *= (CONTAINSMATCH) ~= (INCLUDES) ^= (BEGINSMATCH) $= (ENDSMATCH)
  // all accept the empty string, but match nothing.
  if (aAttrSelector->mValue.IsEmpty() &&
      (aAttrSelector->mFunction == NS_ATTR_FUNC_INCLUDES ||
       aAttrSelector->mFunction == NS_ATTR_FUNC_ENDSMATCH ||
       aAttrSelector->mFunction == NS_ATTR_FUNC_BEGINSMATCH ||
       aAttrSelector->mFunction == NS_ATTR_FUNC_CONTAINSMATCH))
    return PR_FALSE;

  const nsDefaultStringComparator defaultComparator;
  const nsCaseInsensitiveStringComparator ciComparator;
  const nsStringComparator& comparator =
      (aAttrSelector->mCaseSensitive || !isHTML)
                ? static_cast<const nsStringComparator&>(defaultComparator)
                : static_cast<const nsStringComparator&>(ciComparator);

  switch (aAttrSelector->mFunction) {
    case NS_ATTR_FUNC_EQUALS: 
      return aValue.Equals(aAttrSelector->mValue, comparator);
    case NS_ATTR_FUNC_INCLUDES: 
      return ValueIncludes(aValue, aAttrSelector->mValue, comparator);
    case NS_ATTR_FUNC_DASHMATCH: 
      return nsStyleUtil::DashMatchCompare(aValue, aAttrSelector->mValue, comparator);
    case NS_ATTR_FUNC_ENDSMATCH:
      return StringEndsWith(aValue, aAttrSelector->mValue, comparator);
    case NS_ATTR_FUNC_BEGINSMATCH:
      return StringBeginsWith(aValue, aAttrSelector->mValue, comparator);
    case NS_ATTR_FUNC_CONTAINSMATCH:
      return FindInReadable(aAttrSelector->mValue, aValue, comparator);
    default:
      NS_NOTREACHED("Shouldn't be ending up here");
      return PR_FALSE;
  }
}

static PRBool NS_FASTCALL
firstNodeMatches(RuleProcessorData& data, PRBool setNodeFlags,
                 nsPseudoClassList* pseudoClass)
{
  NS_PRECONDITION(pseudoClass->mAtom == nsCSSPseudoClasses::firstNode,
                  "Unexpected atom");
  nsIContent *firstNode = nsnull;
  nsIContent *parent = data.mParentContent;
  if (parent) {
    if (setNodeFlags)
      parent->SetFlags(NODE_HAS_EDGE_CHILD_SELECTOR);

    PRInt32 index = -1;
    do {
      firstNode = parent->GetChildAt(++index);
      // stop at first non-comment and non-whitespace node
    } while (firstNode &&
             !IsSignificantChild(firstNode, PR_TRUE, PR_FALSE));
  }
  return (data.mContent == firstNode);
}

static PRBool NS_FASTCALL
lastNodeMatches(RuleProcessorData& data, PRBool setNodeFlags,
                nsPseudoClassList* pseudoClass)
{
  NS_PRECONDITION(pseudoClass->mAtom == nsCSSPseudoClasses::lastNode,
                  "Unexpected atom");
  nsIContent *lastNode = nsnull;
  nsIContent *parent = data.mParentContent;
  if (parent) {
    if (setNodeFlags)
      parent->SetFlags(NODE_HAS_EDGE_CHILD_SELECTOR);

    PRUint32 index = parent->GetChildCount();
    do {
      lastNode = parent->GetChildAt(--index);
      // stop at first non-comment and non-whitespace node
    } while (lastNode &&
             !IsSignificantChild(lastNode, PR_TRUE, PR_FALSE));
  }
  return (data.mContent == lastNode);
}

static inline PRBool
edgeChildMatches(RuleProcessorData& data, PRBool setNodeFlags,
                 PRBool checkFirst, PRBool checkLast)
{
  nsIContent *parent = data.mParentContent;
  if (!parent) {
    return PR_FALSE;
  }

  if (setNodeFlags)
    parent->SetFlags(NODE_HAS_EDGE_CHILD_SELECTOR);

  return (!checkFirst ||
          data.GetNthIndex(PR_FALSE, PR_FALSE, PR_TRUE) == 1) &&
         (!checkLast ||
          data.GetNthIndex(PR_FALSE, PR_TRUE, PR_TRUE) == 1);
}

static PRBool NS_FASTCALL
firstChildMatches(RuleProcessorData& data, PRBool setNodeFlags,
                  nsPseudoClassList* pseudoClass)
{
  NS_PRECONDITION(pseudoClass->mAtom == nsCSSPseudoClasses::firstChild,
                  "Unexpected atom");
  return edgeChildMatches(data, setNodeFlags, PR_TRUE, PR_FALSE);
}

static PRBool NS_FASTCALL
lastChildMatches(RuleProcessorData& data, PRBool setNodeFlags,
                 nsPseudoClassList* pseudoClass)
{
  NS_PRECONDITION(pseudoClass->mAtom == nsCSSPseudoClasses::lastChild,
                  "Unexpected atom");
  return edgeChildMatches(data, setNodeFlags, PR_FALSE, PR_TRUE);
}

static PRBool NS_FASTCALL
onlyChildMatches(RuleProcessorData& data, PRBool setNodeFlags,
                 nsPseudoClassList* pseudoClass)
{
  NS_PRECONDITION(pseudoClass->mAtom == nsCSSPseudoClasses::onlyChild,
                  "Unexpected atom");
  return edgeChildMatches(data, setNodeFlags, PR_TRUE, PR_TRUE);
}

static inline PRBool
nthChildGenericMatches(RuleProcessorData& data, PRBool setNodeFlags,
                       nsPseudoClassList* pseudoClass,
                       PRBool isOfType, PRBool isFromEnd)
{
  nsIContent *parent = data.mParentContent;
  if (!parent) {
    return PR_FALSE;
  }

  if (setNodeFlags) {
    if (isFromEnd)
      parent->SetFlags(NODE_HAS_SLOW_SELECTOR);
    else
      parent->SetFlags(NODE_HAS_SLOW_SELECTOR_NOAPPEND);
  }

  const PRInt32 index = data.GetNthIndex(isOfType, isFromEnd, PR_FALSE);
  if (index <= 0) {
    // Node is anonymous content (not really a child of its parent).
    return PR_FALSE;
  }

  const PRInt32 a = pseudoClass->u.mNumbers[0];
  const PRInt32 b = pseudoClass->u.mNumbers[1];
  // result should be true if there exists n >= 0 such that
  // a * n + b == index.
  if (a == 0) {
    return b == index;
  }

  // Integer division in C does truncation (towards 0).  So
  // check that the result is nonnegative, and that there was no
  // truncation.
  const PRInt32 n = (index - b) / a;
  return n >= 0 && (a * n == index - b);
}

static PRBool NS_FASTCALL
nthChildMatches(RuleProcessorData& data, PRBool setNodeFlags,
                nsPseudoClassList* pseudoClass)
{
  NS_PRECONDITION(pseudoClass->mAtom == nsCSSPseudoClasses::nthChild,
                  "Unexpected atom");
  return nthChildGenericMatches(data, setNodeFlags, pseudoClass,
                                PR_FALSE, PR_FALSE);
}

static PRBool NS_FASTCALL
nthLastChildMatches(RuleProcessorData& data, PRBool setNodeFlags,
                    nsPseudoClassList* pseudoClass)
{
  NS_PRECONDITION(pseudoClass->mAtom == nsCSSPseudoClasses::nthLastChild,
                  "Unexpected atom");
  return nthChildGenericMatches(data, setNodeFlags, pseudoClass,
                                PR_FALSE, PR_TRUE);
}

static PRBool NS_FASTCALL
nthOfTypeMatches(RuleProcessorData& data, PRBool setNodeFlags,
                 nsPseudoClassList* pseudoClass)
{
  NS_PRECONDITION(pseudoClass->mAtom == nsCSSPseudoClasses::nthOfType,
                  "Unexpected atom");
  return nthChildGenericMatches(data, setNodeFlags, pseudoClass,
                                PR_TRUE, PR_FALSE);
}

static PRBool NS_FASTCALL
nthLastOfTypeMatches(RuleProcessorData& data, PRBool setNodeFlags,
                     nsPseudoClassList* pseudoClass)
{
  NS_PRECONDITION(pseudoClass->mAtom == nsCSSPseudoClasses::nthLastOfType,
                  "Unexpected atom");
  return nthChildGenericMatches(data, setNodeFlags, pseudoClass,
                                PR_TRUE, PR_TRUE);
}

static inline PRBool
edgeOfTypeMatches(RuleProcessorData& data, PRBool setNodeFlags,
                  PRBool checkFirst, PRBool checkLast)
{
  nsIContent *parent = data.mParentContent;
  if (!parent) {
    return PR_FALSE;
  }

  if (setNodeFlags) {
    if (checkLast)
      parent->SetFlags(NODE_HAS_SLOW_SELECTOR);
    else
      parent->SetFlags(NODE_HAS_SLOW_SELECTOR_NOAPPEND);
  }

  return (!checkFirst ||
          data.GetNthIndex(PR_TRUE, PR_FALSE, PR_TRUE) == 1) &&
         (!checkLast ||
          data.GetNthIndex(PR_TRUE, PR_TRUE, PR_TRUE) == 1);
}

static PRBool NS_FASTCALL
firstOfTypeMatches(RuleProcessorData& data, PRBool setNodeFlags,
                   nsPseudoClassList* pseudoClass)
{ 
  NS_PRECONDITION(pseudoClass->mAtom == nsCSSPseudoClasses::firstOfType,
                  "Unexpected atom");
  return edgeOfTypeMatches(data, setNodeFlags, PR_TRUE, PR_FALSE);
}

static PRBool NS_FASTCALL
lastOfTypeMatches(RuleProcessorData& data, PRBool setNodeFlags,
                  nsPseudoClassList* pseudoClass)
{
  NS_PRECONDITION(pseudoClass->mAtom == nsCSSPseudoClasses::lastOfType,
                  "Unexpected atom");
  return edgeOfTypeMatches(data, setNodeFlags, PR_FALSE, PR_TRUE);
}

static PRBool NS_FASTCALL
onlyOfTypeMatches(RuleProcessorData& data, PRBool setNodeFlags,
                  nsPseudoClassList* pseudoClass)
{
  NS_PRECONDITION(pseudoClass->mAtom == nsCSSPseudoClasses::onlyOfType,
                  "Unexpected atom");
  return edgeOfTypeMatches(data, setNodeFlags, PR_TRUE, PR_TRUE);
}

static inline PRBool
checkGenericEmptyMatches(RuleProcessorData& data, PRBool setNodeFlags,
                         PRBool isWhitespaceSignificant)
{
  nsIContent *child = nsnull;
  nsIContent *element = data.mContent;
  PRInt32 index = -1;

  if (setNodeFlags)
    element->SetFlags(NODE_HAS_EMPTY_SELECTOR);

  do {
    child = element->GetChildAt(++index);
    // stop at first non-comment (and non-whitespace for
    // :-moz-only-whitespace) node        
  } while (child && !IsSignificantChild(child, PR_TRUE, isWhitespaceSignificant));
  return (child == nsnull);
}

static PRBool NS_FASTCALL
emptyMatches(RuleProcessorData& data, PRBool setNodeFlags,
             nsPseudoClassList* pseudoClass)
{
  NS_PRECONDITION(pseudoClass->mAtom == nsCSSPseudoClasses::empty,
                  "Unexpected atom");
  return checkGenericEmptyMatches(data, setNodeFlags, PR_TRUE);
}

static PRBool NS_FASTCALL
mozOnlyWhitespaceMatches(RuleProcessorData& data, PRBool setNodeFlags,
                         nsPseudoClassList* pseudoClass)
{
  NS_PRECONDITION(pseudoClass->mAtom == nsCSSPseudoClasses::mozOnlyWhitespace,
                  "Unexpected atom");
  return checkGenericEmptyMatches(data, setNodeFlags, PR_FALSE);
}

static PRBool NS_FASTCALL
mozEmptyExceptChildrenWithLocalnameMatches(RuleProcessorData& data,
                                           PRBool setNodeFlags,
                                           nsPseudoClassList* pseudoClass)
{
  NS_PRECONDITION(pseudoClass->mAtom ==
                    nsCSSPseudoClasses::mozEmptyExceptChildrenWithLocalname,
                  "Unexpected atom");
  NS_ASSERTION(pseudoClass->u.mString, "Must have string!");
  nsIContent *child = nsnull;
  nsIContent *element = data.mContent;
  PRInt32 index = -1;

  if (setNodeFlags)
    element->SetFlags(NODE_HAS_SLOW_SELECTOR);

  do {
    child = element->GetChildAt(++index);
  } while (child &&
           (!IsSignificantChild(child, PR_TRUE, PR_FALSE) ||
            (child->GetNameSpaceID() == element->GetNameSpaceID() &&
             child->Tag()->Equals(nsDependentString(pseudoClass->u.mString)))));
  return (child == nsnull);
}

static PRBool NS_FASTCALL
mozSystemMetricMatches(RuleProcessorData& data, PRBool setNodeFlags,
                       nsPseudoClassList* pseudoClass)
{
  NS_PRECONDITION(pseudoClass->mAtom == nsCSSPseudoClasses::mozSystemMetric,
                  "Unexpected atom");
  NS_ASSERTION(pseudoClass->u.mString, "Must have string!");
  nsCOMPtr<nsIAtom> metric = do_GetAtom(pseudoClass->u.mString);
  return nsCSSRuleProcessor::HasSystemMetric(metric);
}

static PRBool NS_FASTCALL
mozHasHandlerRefMatches(RuleProcessorData& data, PRBool setNodeFlags,
                        nsPseudoClassList* pseudoClass)
{
  NS_PRECONDITION(pseudoClass->mAtom == nsCSSPseudoClasses::mozHasHandlerRef,
                  "Unexpected atom");
  nsIContent *child = nsnull;
  nsIContent *element = data.mContent;
  PRInt32 index = -1;

  do {
    child = element->GetChildAt(++index);
    if (child && child->IsHTML() &&
        child->Tag() == nsGkAtoms::param &&
        child->AttrValueIs(kNameSpaceID_None, nsGkAtoms::name,
                           NS_LITERAL_STRING("pluginurl"), eIgnoreCase)) {
      return PR_TRUE;
    }
  } while (child);
  return PR_FALSE;
}

static PRBool NS_FASTCALL
rootMatches(RuleProcessorData& data, PRBool setNodeFlags,
            nsPseudoClassList* pseudoClass)
{
  NS_PRECONDITION(pseudoClass->mAtom == nsCSSPseudoClasses::root,
                  "Unexpected atom");
  return (data.mParentContent == nsnull &&
          data.mContent == data.mContent->GetOwnerDoc()->GetRootContent());
}

static PRBool NS_FASTCALL
mozBoundElementMatches(RuleProcessorData& data, PRBool setNodeFlags,
                       nsPseudoClassList* pseudoClass)
{
  NS_PRECONDITION(pseudoClass->mAtom == nsCSSPseudoClasses::mozBoundElement,
                  "Unexpected atom");
  // XXXldb How do we know where the selector came from?  And what
  // if there are multiple bindings, and we should be matching the
  // outer one?
  return (data.mScopedRoot == data.mContent);
}

static PRBool NS_FASTCALL
langMatches(RuleProcessorData& data, PRBool setNodeFlags,
            nsPseudoClassList* pseudoClass)
{
  NS_PRECONDITION(pseudoClass->mAtom == nsCSSPseudoClasses::lang,
                  "Unexpected atom");
  NS_ASSERTION(nsnull != pseudoClass->u.mString, "null lang parameter");
  if (!pseudoClass->u.mString || !*pseudoClass->u.mString) {
    return PR_FALSE;
  }

  // We have to determine the language of the current element.  Since
  // this is currently no property and since the language is inherited
  // from the parent we have to be prepared to look at all parent
  // nodes.  The language itself is encoded in the LANG attribute.
  const nsString* lang = data.GetLang();
  if (lang && !lang->IsEmpty()) { // null check for out-of-memory
    return
      nsStyleUtil::DashMatchCompare(*lang,
                                    nsDependentString(pseudoClass->u.mString), 
                                    nsCaseInsensitiveStringComparator());
  }

  nsIDocument* doc = data.mContent->GetDocument();
  if (doc) {
    // Try to get the language from the HTTP header or if this
    // is missing as well from the preferences.
    // The content language can be a comma-separated list of
    // language codes.
    nsAutoString language;
    doc->GetContentLanguage(language);

    nsDependentString langString(pseudoClass->u.mString);
    language.StripWhitespace();
    PRInt32 begin = 0;
    PRInt32 len = language.Length();
    while (begin < len) {
      PRInt32 end = language.FindChar(PRUnichar(','), begin);
      if (end == kNotFound) {
        end = len;
      }
      if (nsStyleUtil::DashMatchCompare(Substring(language, begin, end-begin),
                                        langString,
                                        nsCaseInsensitiveStringComparator())) {
        return PR_TRUE;
      }
      begin = end + 1;
    }
  }

  return PR_FALSE;
}

static PRBool NS_FASTCALL
mozIsHTMLMatches(RuleProcessorData& data, PRBool setNodeFlags,
                 nsPseudoClassList* pseudoClass)
{
  NS_PRECONDITION(pseudoClass->mAtom == nsCSSPseudoClasses::mozIsHTML,
                  "Unexpected atom");
  return data.mIsHTML;
}

static PRBool NS_FASTCALL
mozLocaleDirMatches(RuleProcessorData& data, PRBool setNodeFlags,
                    nsPseudoClassList* pseudoClass)
{
  NS_PRECONDITION(pseudoClass->mAtom == nsCSSPseudoClasses::mozLocaleDir,
                  "Unexpected atom");
  nsIDocument* doc = data.mContent->GetDocument();

  if (!doc) {
    return PR_FALSE;
  }

  PRBool docIsRTL = doc && doc->IsDocumentRightToLeft();

  nsDependentString dirString(pseudoClass->u.mString);
  NS_ASSERTION(dirString.EqualsLiteral("ltr") || dirString.EqualsLiteral("rtl"),
               "invalid value for -moz-locale-dir");

  return dirString.EqualsLiteral("rtl") == docIsRTL;
}

static PRBool NS_FASTCALL
mozLWThemeMatches(RuleProcessorData& data, PRBool setNodeFlags,
                  nsPseudoClassList* pseudoClass)
{
  NS_PRECONDITION(pseudoClass->mAtom == nsCSSPseudoClasses::mozLWTheme,
                  "Unexpected atom");
  nsIDocument* doc = data.mContent->GetOwnerDoc();
  return doc && doc->GetDocumentLWTheme() > nsIDocument::Doc_Theme_None;
}

static PRBool NS_FASTCALL
mozLWThemeBrightTextMatches(RuleProcessorData& data, PRBool setNodeFlags,
                            nsPseudoClassList* pseudoClass)
{
  NS_PRECONDITION(pseudoClass->mAtom ==
                    nsCSSPseudoClasses::mozLWThemeBrightText,
                  "Unexpected atom");
  nsIDocument* doc = data.mContent->GetOwnerDoc();
  return doc && doc->GetDocumentLWTheme() == nsIDocument::Doc_Theme_Bright;
}

static PRBool NS_FASTCALL
mozLWThemeDarkTextMatches(RuleProcessorData& data, PRBool setNodeFlags,
                          nsPseudoClassList* pseudoClass)
{
  NS_PRECONDITION(pseudoClass->mAtom ==
                    nsCSSPseudoClasses::mozLWThemeDarkText,
                  "Unexpected atom");
  nsIDocument* doc = data.mContent->GetOwnerDoc();
  return doc && doc->GetDocumentLWTheme() == nsIDocument::Doc_Theme_Dark;
}

static PRBool NS_FASTCALL
notPseudoMatches(RuleProcessorData& data, PRBool setNodeFlags,
                 nsPseudoClassList* pseudoClass)
{
  NS_NOTREACHED("Why did this get called?");
  return PR_FALSE;
}

typedef PRBool
  (NS_FASTCALL * PseudoClassMatcher)(RuleProcessorData&, PRBool setNodeFlags,
                                     nsPseudoClassList* pseudoClass);
// Only one of mFunc or mBits will be set; the other will be null or 0
// respectively.  We could use a union, but then we'd still need to
// differentiate somehow, eiher with another member in the struct or
// with a boolean coming from _sowewhere_.
struct PseudoClassInfo {
  PseudoClassMatcher mFunc;
  PRInt32 mBits;
};

static const PseudoClassInfo sPseudoClassInfo[] = {
#define CSS_PSEUDO_CLASS(_name, _value)         \
  { &_name##Matches, 0 },
#define CSS_STATE_PSEUDO_CLASS(_name, _value, _bit) \
  { nsnull, _bit },
#include "nsCSSPseudoClassList.h"
#undef CSS_STATE_PSEUDO_CLASS
#undef CSS_PSEUDO_CLASS
  // Add more entries for our fake values to make sure we can't
  // index out of bounds into this array no matter what.
  { nsnull, 0 },
  { nsnull, 0 }
};
PR_STATIC_ASSERT(NS_ARRAY_LENGTH(sPseudoClassInfo) >
                   nsCSSPseudoClasses::ePseudoClass_NotPseudoClass);

// NOTE: For |aStateMask| to work correctly, it's important that any change
// that changes multiple state bits include all those state bits in the
// notification.  Otherwise, if multiple states change but we do separate
// notifications then we might determine the style is not state-dependent when
// it really is (e.g., determining that a :hover:active rule no longer matches
// when both states are unset).

// If |aForStyling| is false, we shouldn't mark slow-selector bits on nodes.

// |aDependence| has two functions:
//  * when non-null, it indicates that we're processing a negation,
//    which is done only when SelectorMatches calls itself recursively
//  * what it points to should be set to true whenever a test is skipped
//    because of aStateMask
static PRBool SelectorMatches(RuleProcessorData &data,
                              nsCSSSelector* aSelector,
                              PRInt32 aStateMask, // states NOT to test
                              PRBool aForStyling,
                              PRBool* const aDependence = nsnull) 

{
  NS_PRECONDITION(!aSelector->IsPseudoElement(),
                  "Pseudo-element snuck into SelectorMatches?");
  // namespace/tag match
  // optimization : bail out early if we can
  if ((kNameSpaceID_Unknown != aSelector->mNameSpace &&
       data.mNameSpaceID != aSelector->mNameSpace))
    return PR_FALSE;

  if (aSelector->mLowercaseTag && 
      (data.mIsHTML ? aSelector->mLowercaseTag : aSelector->mCasedTag) !=
        data.mContentTag) {
    return PR_FALSE;
  }

  nsAtomList* IDList = aSelector->mIDList;
  if (IDList) {
    if (data.mContentID) {
      // case sensitivity: bug 93371
      const PRBool isCaseSensitive =
        data.mCompatMode != eCompatibility_NavQuirks;

      if (isCaseSensitive) {
        do {
          if (IDList->mAtom != data.mContentID) {
            return PR_FALSE;
          }
          IDList = IDList->mNext;
        } while (IDList);
      } else {
        const char* id1Str;
        data.mContentID->GetUTF8String(&id1Str);
        nsDependentCString id1(id1Str);
        do {
          const char* id2Str;
          IDList->mAtom->GetUTF8String(&id2Str);
          if (!id1.Equals(id2Str, nsCaseInsensitiveCStringComparator())) {
            return PR_FALSE;
          }
          IDList = IDList->mNext;
        } while (IDList);
      }
    } else {
      // Element has no id but we have an id selector
      return PR_FALSE;
    }
  }

  nsAtomList* classList = aSelector->mClassList;
  if (classList) {
    // test for class match
    const nsAttrValue *elementClasses = data.mClasses;
    if (!elementClasses) {
      // Element has no classes but we have a class selector
      return PR_FALSE;
    }

    // case sensitivity: bug 93371
    const PRBool isCaseSensitive =
      data.mCompatMode != eCompatibility_NavQuirks;

    while (classList) {
      if (!elementClasses->Contains(classList->mAtom,
                                    isCaseSensitive ?
                                      eCaseMatters : eIgnoreCase)) {
        return PR_FALSE;
      }
      classList = classList->mNext;
    }
  }

  const PRBool isNegated = (aDependence != nsnull);
  // The selectors for which we set node bits are, unfortunately, early
  // in this function (because they're pseudo-classes, which are
  // generally quick to test, and thus earlier).  If they were later,
  // we'd probably avoid setting those bits in more cases where setting
  // them is unnecessary.
  NS_ASSERTION(aStateMask == 0 || !aForStyling,
               "aForStyling must be false if we're just testing for "
               "state-dependence");
  const PRBool setNodeFlags = aForStyling;

  // test for pseudo class match
  for (nsPseudoClassList* pseudoClass = aSelector->mPseudoClassList;
       pseudoClass; pseudoClass = pseudoClass->mNext) {
    const PseudoClassInfo& info = sPseudoClassInfo[pseudoClass->mType];
    if (info.mFunc) {
      if (!(*info.mFunc)(data, setNodeFlags, pseudoClass)) {
        return PR_FALSE;
      }
    } else {
      PRInt32 statesToCheck = info.mBits;
      NS_ABORT_IF_FALSE(statesToCheck != 0, "How did that happen?");
      if ((statesToCheck & (NS_EVENT_STATE_HOVER | NS_EVENT_STATE_ACTIVE)) &&
          data.mCompatMode == eCompatibility_NavQuirks &&
          // global selector (but don't check .class):
          !aSelector->HasTagSelector() && !aSelector->mIDList && 
          !aSelector->mAttrList &&
          // This (or the other way around) both make :not() asymmetric
          // in quirks mode (and it's hard to work around since we're
          // testing the current mNegations, not the first
          // (unnegated)). This at least makes it closer to the spec.
          !isNegated &&
          // important for |IsQuirkEventSensitive|:
          data.mIsHTMLContent && !data.IsLink() &&
          !IsQuirkEventSensitive(data.mContentTag)) {
        // In quirks mode, only make certain elements sensitive to
        // selectors ":hover" and ":active".
        return PR_FALSE;
      } else {
        if (aStateMask & statesToCheck) {
          if (aDependence)
            *aDependence = PR_TRUE;
        } else {
          if (!(data.ContentState() & statesToCheck)) {
            return PR_FALSE;
          }
        }
      }
    }
  }

  PRBool result = PR_TRUE;
  if (aSelector->mAttrList) {
    // test for attribute match
    if (!data.mHasAttributes) {
      // if no attributes on the content, no match
      return PR_FALSE;
    } else {
      result = PR_TRUE;
      nsAttrSelector* attr = aSelector->mAttrList;
      nsIAtom* matchAttribute;

      do {
        matchAttribute = data.mIsHTML ? attr->mLowercaseAttr : attr->mCasedAttr;
        if (attr->mNameSpace == kNameSpaceID_Unknown) {
          // Attr selector with a wildcard namespace.  We have to examine all
          // the attributes on our content node....  This sort of selector is
          // essentially a boolean OR, over all namespaces, of equivalent attr
          // selectors with those namespaces.  So to evaluate whether it
          // matches, evaluate for each namespace (the only namespaces that
          // have a chance at matching, of course, are ones that the element
          // actually has attributes in), short-circuiting if we ever match.
          PRUint32 attrCount = data.mContent->GetAttrCount();
          result = PR_FALSE;
          for (PRUint32 i = 0; i < attrCount; ++i) {
            const nsAttrName* attrName =
              data.mContent->GetAttrNameAt(i);
            NS_ASSERTION(attrName, "GetAttrCount lied or GetAttrNameAt failed");
            if (attrName->LocalName() != matchAttribute) {
              continue;
            }
            if (attr->mFunction == NS_ATTR_FUNC_SET) {
              result = PR_TRUE;
            } else {
              nsAutoString value;
#ifdef DEBUG
              PRBool hasAttr =
#endif
                data.mContent->GetAttr(attrName->NamespaceID(),
                                       attrName->LocalName(), value);
              NS_ASSERTION(hasAttr, "GetAttrNameAt lied");
              result = AttrMatchesValue(attr, value, data.mIsHTML);
            }

            // At this point |result| has been set by us
            // explicitly in this loop.  If it's PR_FALSE, we may still match
            // -- the content may have another attribute with the same name but
            // in a different namespace.  But if it's PR_TRUE, we are done (we
            // can short-circuit the boolean OR described above).
            if (result) {
              break;
            }
          }
        }
        else if (attr->mFunction == NS_ATTR_FUNC_EQUALS) {
          result =
            data.mContent->
              AttrValueIs(attr->mNameSpace, matchAttribute, attr->mValue,
                          (!data.mIsHTML || attr->mCaseSensitive) ? eCaseMatters
                                                                  : eIgnoreCase);
        }
        else if (!data.mContent->HasAttr(attr->mNameSpace, matchAttribute)) {
          result = PR_FALSE;
        }
        else if (attr->mFunction != NS_ATTR_FUNC_SET) {
          nsAutoString value;
#ifdef DEBUG
          PRBool hasAttr =
#endif
              data.mContent->GetAttr(attr->mNameSpace, matchAttribute, value);
          NS_ASSERTION(hasAttr, "HasAttr lied");
          result = AttrMatchesValue(attr, value, data.mIsHTML);
        }
        
        attr = attr->mNext;
      } while (attr && result);
    }
  }

  // apply SelectorMatches to the negated selectors in the chain
  if (!isNegated) {
    for (nsCSSSelector *negation = aSelector->mNegations;
         result && negation; negation = negation->mNegations) {
      PRBool dependence = PR_FALSE;
      result = !SelectorMatches(data, negation, aStateMask,
                                aForStyling, &dependence);
      // If the selector does match due to the dependence on aStateMask,
      // then we want to keep result true so that the final result of
      // SelectorMatches is true.  Doing so tells StateEnumFunc that
      // there is a dependence on the state.
      result = result || dependence;
    }
  }
  return result;
}

#undef STATE_CHECK

// Right now, there are four operators:
//   PRUnichar(0), the descendant combinator, is greedy
//   '~', the indirect adjacent sibling combinator, is greedy
//   '+' and '>', the direct adjacent sibling and child combinators, are not
#define NS_IS_GREEDY_OPERATOR(ch) (ch == PRUnichar(0) || ch == PRUnichar('~'))

static PRBool SelectorMatchesTree(RuleProcessorData& aPrevData,
                                  nsCSSSelector* aSelector,
                                  PRBool aForStyling) 
{
  nsCSSSelector* selector = aSelector;
  RuleProcessorData* prevdata = &aPrevData;
  while (selector) { // check compound selectors
    // If we don't already have a RuleProcessorData for the next
    // appropriate content (whether parent or previous sibling), create
    // one.

    // for adjacent sibling combinators, the content to test against the
    // selector is the previous sibling *element*
    RuleProcessorData* data;
    if (PRUnichar('+') == selector->mOperator ||
        PRUnichar('~') == selector->mOperator) {
      data = prevdata->mPreviousSiblingData;
      if (!data) {
        nsIContent* content = prevdata->mContent;
        nsIContent* parent = prevdata->mParentContent;
        if (parent) {
          if (aForStyling)
            parent->SetFlags(NODE_HAS_SLOW_SELECTOR_NOAPPEND);

          PRInt32 index = parent->IndexOf(content);
          while (0 <= --index) {
            content = parent->GetChildAt(index);
            if (content->IsNodeOfType(nsINode::eELEMENT)) {
              data = RuleProcessorData::Create(prevdata->mPresContext, content,
                                               prevdata->mRuleWalker,
                                               prevdata->mCompatMode);
              prevdata->mPreviousSiblingData = data;    
              break;
            }
          }
        }
      }
    }
    // for descendant combinators and child combinators, the content
    // to test against is the parent
    else {
      data = prevdata->mParentData;
      if (!data) {
        nsIContent *content = prevdata->mParentContent;
        // GetParent could return a document fragment; we only want
        // element parents.
        if (content && content->IsNodeOfType(nsINode::eELEMENT)) {
          data = RuleProcessorData::Create(prevdata->mPresContext, content,
                                           prevdata->mRuleWalker,
                                           prevdata->mCompatMode);
          prevdata->mParentData = data;
        }
      }
    }
    if (! data) {
      return PR_FALSE;
    }
    if (SelectorMatches(*data, selector, 0, aForStyling)) {
      // to avoid greedy matching, we need to recur if this is a
      // descendant or general sibling combinator and the next
      // combinator is different, but we can make an exception for
      // sibling, then parent, since a sibling's parent is always the
      // same.
      if (NS_IS_GREEDY_OPERATOR(selector->mOperator) &&
          selector->mNext &&
          selector->mNext->mOperator != selector->mOperator &&
          !(selector->mOperator == '~' &&
            (selector->mNext->mOperator == PRUnichar(0) ||
             selector->mNext->mOperator == PRUnichar('>')))) {

        // pretend the selector didn't match, and step through content
        // while testing the same selector

        // This approach is slightly strange in that when it recurs
        // it tests from the top of the content tree, down.  This
        // doesn't matter much for performance since most selectors
        // don't match.  (If most did, it might be faster...)
        if (SelectorMatchesTree(*data, selector, aForStyling)) {
          return PR_TRUE;
        }
      }
      selector = selector->mNext;
    }
    else {
      // for adjacent sibling and child combinators, if we didn't find
      // a match, we're done
      if (!NS_IS_GREEDY_OPERATOR(selector->mOperator)) {
        return PR_FALSE;  // parent was required to match
      }
    }
    prevdata = data;
  }
  return PR_TRUE; // all the selectors matched.
}

static void ContentEnumFunc(nsICSSStyleRule* aRule, nsCSSSelector* aSelector,
                            void* aData)
{
  RuleProcessorData* data = (RuleProcessorData*)aData;

  if (SelectorMatches(*data, aSelector, 0, PR_TRUE)) {
    nsCSSSelector *next = aSelector->mNext;
    if (!next || SelectorMatchesTree(*data, next, PR_TRUE)) {
      // for performance, require that every implementation of
      // nsICSSStyleRule return the same pointer for nsIStyleRule (why
      // would anything multiply inherit nsIStyleRule anyway?)
#ifdef DEBUG
      nsCOMPtr<nsIStyleRule> iRule = do_QueryInterface(aRule);
      NS_ASSERTION(static_cast<nsIStyleRule*>(aRule) == iRule.get(),
                   "Please fix QI so this performance optimization is valid");
#endif
      aRule->RuleMatched();
      data->mRuleWalker->Forward(static_cast<nsIStyleRule*>(aRule));
      // nsStyleSet will deal with the !important rule
    }
  }
}

NS_IMETHODIMP
nsCSSRuleProcessor::RulesMatching(ElementRuleProcessorData *aData)
{
  NS_PRECONDITION(aData->mContent->IsNodeOfType(nsINode::eELEMENT),
                  "content must be element");

  RuleCascadeData* cascade = GetRuleCascade(aData->mPresContext);

  if (cascade) {
    cascade->mRuleHash.EnumerateAllRules(aData->mNameSpaceID,
                                         aData->mContentTag,
                                         aData->mContentID,
                                         aData->mClasses,
                                         ContentEnumFunc,
                                         aData);
  }
  return NS_OK;
}

NS_IMETHODIMP
nsCSSRuleProcessor::RulesMatching(PseudoElementRuleProcessorData* aData)
{
  NS_PRECONDITION(aData->mContent->IsNodeOfType(nsINode::eELEMENT),
                  "content must be element");

  RuleCascadeData* cascade = GetRuleCascade(aData->mPresContext);

  if (cascade) {
    RuleHash* ruleHash = cascade->mPseudoElementRuleHashes[aData->mPseudoType];
    if (ruleHash) {
      ruleHash->EnumerateAllRules(aData->mNameSpaceID,
                                  aData->mContentTag,
                                  aData->mContentID,
                                  aData->mClasses,
                                  ContentEnumFunc,
                                  aData);
    }
  }
  return NS_OK;
}

NS_IMETHODIMP
nsCSSRuleProcessor::RulesMatching(AnonBoxRuleProcessorData* aData)
{
  RuleCascadeData* cascade = GetRuleCascade(aData->mPresContext);

  if (cascade && cascade->mAnonBoxRules.entryCount) {
    RuleHashTagTableEntry* entry = static_cast<RuleHashTagTableEntry*>
      (PL_DHashTableOperate(&cascade->mAnonBoxRules, aData->mPseudoTag,
                            PL_DHASH_LOOKUP));
    if (PL_DHASH_ENTRY_IS_BUSY(entry)) {
      for (RuleValue *value = entry->mRules; value; value = value->mNext) {
        // for performance, require that every implementation of
        // nsICSSStyleRule return the same pointer for nsIStyleRule (why
        // would anything multiply inherit nsIStyleRule anyway?)
#ifdef DEBUG
        nsCOMPtr<nsIStyleRule> iRule = do_QueryInterface(value->mRule);
        NS_ASSERTION(static_cast<nsIStyleRule*>(value->mRule) == iRule.get(),
                     "Please fix QI so this performance optimization is valid");
#endif
        value->mRule->RuleMatched();
        aData->mRuleWalker->Forward(static_cast<nsIStyleRule*>(value->mRule));
      }
    }
  }
  return NS_OK;
}

#ifdef MOZ_XUL
NS_IMETHODIMP
nsCSSRuleProcessor::RulesMatching(XULTreeRuleProcessorData* aData)
{
  NS_PRECONDITION(aData->mContent->IsNodeOfType(nsINode::eELEMENT),
                  "content must be element");

  RuleCascadeData* cascade = GetRuleCascade(aData->mPresContext);

  if (cascade && cascade->mXULTreeRules.entryCount) {
    RuleHashTagTableEntry* entry = static_cast<RuleHashTagTableEntry*>
      (PL_DHashTableOperate(&cascade->mXULTreeRules, aData->mPseudoTag,
                            PL_DHASH_LOOKUP));
    if (PL_DHASH_ENTRY_IS_BUSY(entry)) {
      for (RuleValue *value = entry->mRules; value; value = value->mNext) {
        PRBool matches = PR_TRUE;
        aData->mComparator->PseudoMatches(aData->mPseudoTag, value->mSelector,
                                          &matches);
        if (matches) {
          ContentEnumFunc(value->mRule, value->mSelector->mNext,
                          static_cast<RuleProcessorData*>(aData));
        }
      }
    }
  }
  return NS_OK;
}
#endif

inline PRBool
IsSiblingOperator(PRUnichar oper)
{
  return oper == PRUnichar('+') || oper == PRUnichar('~');
}

nsReStyleHint
nsCSSRuleProcessor::HasStateDependentStyle(StateRuleProcessorData* aData)
{
  NS_PRECONDITION(aData->mContent->IsNodeOfType(nsINode::eELEMENT),
                  "content must be element");

  RuleCascadeData* cascade = GetRuleCascade(aData->mPresContext);

  // Look up the content node in the state rule list, which points to
  // any (CSS2 definition) simple selector (whether or not it is the
  // subject) that has a state pseudo-class on it.  This means that this
  // code will be matching selectors that aren't real selectors in any
  // stylesheet (e.g., if there is a selector "body > p:hover > a", then
  // "body > p:hover" will be in |cascade->mStateSelectors|).  Note that
  // |IsStateSelector| below determines which selectors are in
  // |cascade->mStateSelectors|.
  nsReStyleHint hint = nsReStyleHint(0);
  if (cascade) {
    nsCSSSelector **iter = cascade->mStateSelectors.Elements(),
                  **end = iter + cascade->mStateSelectors.Length();
    for(; iter != end; ++iter) {
      nsCSSSelector* selector = *iter;

      nsReStyleHint possibleChange = IsSiblingOperator(selector->mOperator) ?
        eReStyle_LaterSiblings : eReStyle_Self;

      // If hint already includes all the bits of possibleChange,
      // don't bother calling SelectorMatches, since even if it returns false
      // hint won't change.
      if ((possibleChange & ~hint) &&
          SelectorMatches(*aData, selector, aData->mStateMask, PR_FALSE) &&
          SelectorMatchesTree(*aData, selector->mNext, PR_FALSE)) {
        hint = nsReStyleHint(hint | possibleChange);
      }
    }
  }
  return hint;
}

struct AttributeEnumData {
  AttributeEnumData(AttributeRuleProcessorData *aData)
    : data(aData), change(nsReStyleHint(0)) {}

  AttributeRuleProcessorData *data;
  nsReStyleHint change;
};


static void
AttributeEnumFunc(nsCSSSelector* aSelector, AttributeEnumData* aData)
{
  AttributeRuleProcessorData *data = aData->data;

  nsReStyleHint possibleChange = IsSiblingOperator(aSelector->mOperator) ?
    eReStyle_LaterSiblings : eReStyle_Self;

  // If enumData->change already includes all the bits of possibleChange, don't
  // bother calling SelectorMatches, since even if it returns false
  // enumData->change won't change.
  if ((possibleChange & ~(aData->change)) &&
      SelectorMatches(*data, aSelector, 0, PR_FALSE) &&
      SelectorMatchesTree(*data, aSelector->mNext, PR_FALSE)) {
    aData->change = nsReStyleHint(aData->change | possibleChange);
  }
}

nsReStyleHint
nsCSSRuleProcessor::HasAttributeDependentStyle(AttributeRuleProcessorData* aData)
{
  NS_PRECONDITION(aData->mContent->IsNodeOfType(nsINode::eELEMENT),
                  "content must be element");

  //  We could try making use of aData->mModType, but :not rules make it a bit
  //  of a pain to do so...  So just ignore it for now.

  AttributeEnumData data(aData);

  // Don't do our special handling of certain attributes if the attr
  // hasn't changed yet.
  if (aData->mAttrHasChanged) {
    // check for the localedir, lwtheme and lwthemetextcolor attribute on root XUL elements
    if ((aData->mAttribute == nsGkAtoms::localedir ||
         aData->mAttribute == nsGkAtoms::lwtheme ||
         aData->mAttribute == nsGkAtoms::lwthemetextcolor) &&
        aData->mNameSpaceID == kNameSpaceID_XUL &&
        aData->mContent == aData->mContent->GetOwnerDoc()->GetRootContent())
      {
        data.change = nsReStyleHint(data.change | eReStyle_Self);
      }
  }

  RuleCascadeData* cascade = GetRuleCascade(aData->mPresContext);

  // Since we get both before and after notifications for attributes, we
  // don't have to ignore aData->mAttribute while matching.  Just check
  // whether we have selectors relevant to aData->mAttribute that we
  // match.  If this is the before change notification, that will catch
  // rules we might stop matching; if the after change notification, the
  // ones we might have started matching.
  if (cascade) {
    if (aData->mAttribute == aData->mContent->GetIDAttributeName()) {
      nsCSSSelector **iter = cascade->mIDSelectors.Elements(),
                    **end = iter + cascade->mIDSelectors.Length();
      for(; iter != end; ++iter) {
        AttributeEnumFunc(*iter, &data);
      }
    }
    
    if (aData->mAttribute == aData->mContent->GetClassAttributeName()) {
      nsCSSSelector **iter = cascade->mClassSelectors.Elements(),
                    **end = iter + cascade->mClassSelectors.Length();
      for(; iter != end; ++iter) {
        AttributeEnumFunc(*iter, &data);
      }
    }

    AttributeSelectorEntry *entry = static_cast<AttributeSelectorEntry*>
                                               (PL_DHashTableOperate(&cascade->mAttributeSelectors, aData->mAttribute,
                             PL_DHASH_LOOKUP));
    if (PL_DHASH_ENTRY_IS_BUSY(entry)) {
      nsCSSSelector **iter = entry->mSelectors->Elements(),
                    **end = iter + entry->mSelectors->Length();
      for(; iter != end; ++iter) {
        AttributeEnumFunc(*iter, &data);
      }
    }
  }

  return data.change;
}

NS_IMETHODIMP
nsCSSRuleProcessor::MediumFeaturesChanged(nsPresContext* aPresContext,
                                          PRBool* aRulesChanged)
{
  RuleCascadeData *old = mRuleCascades;
  // We don't want to do anything if there aren't any sets of rules
  // cached yet (or somebody cleared them and is thus responsible for
  // rebuilding things), since we should not build the rule cascade too
  // early (e.g., before we know whether the quirk style sheet should be
  // enabled).  And if there's nothing cached, it doesn't matter if
  // anything changed.  See bug 448281.
  if (old) {
    RefreshRuleCascade(aPresContext);
  }
  *aRulesChanged = (old != mRuleCascades);
  return NS_OK;
}

// Append all the currently-active font face rules to aArray.  Return
// true for success and false for failure.
PRBool
nsCSSRuleProcessor::AppendFontFaceRules(
                              nsPresContext *aPresContext,
                              nsTArray<nsFontFaceRuleContainer>& aArray)
{
  RuleCascadeData* cascade = GetRuleCascade(aPresContext);

  if (cascade) {
    if (!aArray.AppendElements(cascade->mFontFaceRules))
      return PR_FALSE;
  }
  
  return PR_TRUE;
}

nsresult
nsCSSRuleProcessor::ClearRuleCascades()
{
  // We rely on our caller (perhaps indirectly) to do something that
  // will rebuild style data and the user font set (either
  // nsIPresShell::ReconstructStyleData or
  // nsPresContext::RebuildAllStyleData).
  RuleCascadeData *data = mRuleCascades;
  mRuleCascades = nsnull;
  while (data) {
    RuleCascadeData *next = data->mNext;
    delete data;
    data = next;
  }
  return NS_OK;
}


// This function should return true only for selectors that need to be
// checked by |HasStateDependentStyle|.
inline
PRBool IsStateSelector(nsCSSSelector& aSelector)
{
  for (nsPseudoClassList* pseudoClass = aSelector.mPseudoClassList;
       pseudoClass; pseudoClass = pseudoClass->mNext) {
    // Tree pseudo-elements overload mPseudoClassList for things that
    // aren't pseudo-classes.
    if (pseudoClass->mType >= nsCSSPseudoClasses::ePseudoClass_Count) {
      continue;
    }
    if (sPseudoClassInfo[pseudoClass->mType].mBits) {
      return PR_TRUE;
    }
  }
  return PR_FALSE;
}

static PRBool
AddRule(RuleValue* aRuleInfo, RuleCascadeData* aCascade)
{
  RuleCascadeData * const cascade = aCascade;

  // Build the rule hash.
  nsCSSPseudoElements::Type pseudoType = aRuleInfo->mSelector->PseudoType();
  if (NS_LIKELY(pseudoType == nsCSSPseudoElements::ePseudo_NotPseudoElement)) {
    cascade->mRuleHash.PrependRule(aRuleInfo);
  } else if (pseudoType < nsCSSPseudoElements::ePseudo_PseudoElementCount) {
    RuleHash*& ruleHash = cascade->mPseudoElementRuleHashes[pseudoType];
    if (!ruleHash) {
      ruleHash = new RuleHash(cascade->mQuirksMode);
      if (!ruleHash) {
        // Out of memory; give up
        return PR_FALSE;
      }
    }
    NS_ASSERTION(aRuleInfo->mSelector->mNext,
                 "Must have mNext; parser screwed up");
    NS_ASSERTION(aRuleInfo->mSelector->mNext->mOperator == '>',
                 "Unexpected mNext combinator");
    aRuleInfo->mSelector = aRuleInfo->mSelector->mNext;
    ruleHash->PrependRule(aRuleInfo);
  } else if (pseudoType == nsCSSPseudoElements::ePseudo_AnonBox) {
    NS_ASSERTION(!aRuleInfo->mSelector->mCasedTag &&
                 !aRuleInfo->mSelector->mIDList &&
                 !aRuleInfo->mSelector->mClassList &&
                 !aRuleInfo->mSelector->mPseudoClassList &&
                 !aRuleInfo->mSelector->mAttrList &&
                 !aRuleInfo->mSelector->mNegations &&
                 !aRuleInfo->mSelector->mNext &&
                 aRuleInfo->mSelector->mNameSpace == kNameSpaceID_Unknown,
                 "Parser messed up with anon box selector");

    // Index doesn't matter here, since we'll just be walking these
    // rules in order; just pass 0.
    DoPrependRuleToTagTable(&cascade->mAnonBoxRules,
                            aRuleInfo->mSelector->mLowercaseTag,
                            aRuleInfo, 0);
  } else {
#ifdef MOZ_XUL
    NS_ASSERTION(pseudoType == nsCSSPseudoElements::ePseudo_XULTree,
                 "Unexpected pseudo type");
    // Index doesn't matter here, since we'll just be walking these
    // rules in order; just pass 0.
    DoPrependRuleToTagTable(&cascade->mXULTreeRules,
                            aRuleInfo->mSelector->mLowercaseTag,
                            aRuleInfo, 0);
#else
    NS_NOTREACHED("Unexpected pseudo type");
#endif
  }

  nsTArray<nsCSSSelector*>* stateArray = &cascade->mStateSelectors;
  nsTArray<nsCSSSelector*>* classArray = &cascade->mClassSelectors;
  nsTArray<nsCSSSelector*>* idArray = &cascade->mIDSelectors;
  
  for (nsCSSSelector* selector = aRuleInfo->mSelector;
           selector; selector = selector->mNext) {
    if (selector->IsPseudoElement()) {
      NS_ASSERTION(!selector->mNegations, "Shouldn't have negations");
      // Make sure these selectors don't end up in the hashtables we use to
      // match against actual elements, no matter what.  Normally they wouldn't
      // anyway, but trees overload mPseudoClassList with weird stuff.
      continue;
    }
    // It's worth noting that this loop over negations isn't quite
    // optimal for two reasons.  One, we could add something to one of
    // these lists twice, which means we'll check it twice, but I don't
    // think that's worth worrying about.   (We do the same for multiple
    // attribute selectors on the same attribute.)  Two, we don't really
    // need to check negations past the first in the current
    // implementation (and they're rare as well), but that might change
    // in the future if :not() is extended. 
    for (nsCSSSelector* negation = selector; negation;
         negation = negation->mNegations) {
      // Build mStateSelectors.
      if (IsStateSelector(*negation))
        stateArray->AppendElement(selector);

      // Build mIDSelectors
      if (negation->mIDList) {
        idArray->AppendElement(selector);
      }
      
      // Build mClassSelectors
      if (negation->mClassList) {
        classArray->AppendElement(selector);
      }

      // Build mAttributeSelectors.
      for (nsAttrSelector *attr = negation->mAttrList; attr;
           attr = attr->mNext) {
        nsTArray<nsCSSSelector*> *array =
          cascade->AttributeListFor(attr->mCasedAttr);
        if (!array)
          return PR_FALSE;
        array->AppendElement(selector);
        if (attr->mLowercaseAttr != attr->mCasedAttr) {
          nsTArray<nsCSSSelector*> *array =
            cascade->AttributeListFor(attr->mLowercaseAttr);
          if (!array)
            return PR_FALSE;
          array->AppendElement(selector);
        }          
      }
    }
  }

  return PR_TRUE;
}

struct PerWeightData {
  PRInt32 mWeight;
  RuleValue* mRules; // linked list (reverse order)
};

struct RuleByWeightEntry : public PLDHashEntryHdr {
  PerWeightData data; // mWeight is key, mRules are value
};

static PLDHashNumber
HashIntKey(PLDHashTable *table, const void *key)
{
  return PLDHashNumber(NS_PTR_TO_INT32(key));
}

static PRBool
MatchWeightEntry(PLDHashTable *table, const PLDHashEntryHdr *hdr,
                 const void *key)
{
  const RuleByWeightEntry *entry = (const RuleByWeightEntry *)hdr;
  return entry->data.mWeight == NS_PTR_TO_INT32(key);
}

static PLDHashTableOps gRulesByWeightOps = {
    PL_DHashAllocTable,
    PL_DHashFreeTable,
    HashIntKey,
    MatchWeightEntry,
    PL_DHashMoveEntryStub,
    PL_DHashClearEntryStub,
    PL_DHashFinalizeStub,
    NULL
};

struct CascadeEnumData {
  CascadeEnumData(nsPresContext* aPresContext,
                  nsTArray<nsFontFaceRuleContainer>& aFontFaceRules,
                  nsMediaQueryResultCacheKey& aKey,
                  PLArenaPool& aArena,
                  PRUint8 aSheetType)
    : mPresContext(aPresContext),
      mFontFaceRules(aFontFaceRules),
      mCacheKey(aKey),
      mArena(aArena),
      mSheetType(aSheetType)
  {
    if (!PL_DHashTableInit(&mRulesByWeight, &gRulesByWeightOps, nsnull,
                          sizeof(RuleByWeightEntry), 64))
      mRulesByWeight.ops = nsnull;
  }

  ~CascadeEnumData()
  {
    if (mRulesByWeight.ops)
      PL_DHashTableFinish(&mRulesByWeight);
  }

  nsPresContext* mPresContext;
  nsTArray<nsFontFaceRuleContainer>& mFontFaceRules;
  nsMediaQueryResultCacheKey& mCacheKey;
  PLArenaPool& mArena;
  // Hooray, a manual PLDHashTable since nsClassHashtable doesn't
  // provide a getter that gives me a *reference* to the value.
  PLDHashTable mRulesByWeight; // of RuleValue* linked lists (?)
  PRUint8 mSheetType;
};

/*
 * This enumerates style rules in a sheet (and recursively into any
 * grouping rules) in order to:
 *  (1) add any style rules, in order, into data->mRulesByWeight (for
 *      the primary CSS cascade), where they are separated by weight
 *      but kept in order per-weight, and
 *  (2) add any @font-face rules, in order, into data->mFontFaceRules.
 */
static PRBool
CascadeRuleEnumFunc(nsICSSRule* aRule, void* aData)
{
  CascadeEnumData* data = (CascadeEnumData*)aData;
  PRInt32 type = nsICSSRule::UNKNOWN_RULE;
  aRule->GetType(type);

  if (nsICSSRule::STYLE_RULE == type) {
    nsICSSStyleRule* styleRule = (nsICSSStyleRule*)aRule;

    for (nsCSSSelectorList *sel = styleRule->Selector();
         sel; sel = sel->mNext) {
      PRInt32 weight = sel->mWeight;
      RuleByWeightEntry *entry = static_cast<RuleByWeightEntry*>(
        PL_DHashTableOperate(&data->mRulesByWeight, NS_INT32_TO_PTR(weight),
                             PL_DHASH_ADD));
      if (!entry)
        return PR_FALSE;
      entry->data.mWeight = weight;
      RuleValue *info =
        new (data->mArena) RuleValue(styleRule, sel->mSelectors);
      // entry->data.mRules must be in backwards order.
      info->mNext = entry->data.mRules;
      entry->data.mRules = info;
    }
  }
  else if (nsICSSRule::MEDIA_RULE == type ||
           nsICSSRule::DOCUMENT_RULE == type) {
    nsICSSGroupRule* groupRule = (nsICSSGroupRule*)aRule;
    if (groupRule->UseForPresentation(data->mPresContext, data->mCacheKey))
      if (!groupRule->EnumerateRulesForwards(CascadeRuleEnumFunc, aData))
        return PR_FALSE;
  }
  else if (nsICSSRule::FONT_FACE_RULE == type) {
    nsCSSFontFaceRule *fontFaceRule = static_cast<nsCSSFontFaceRule*>(aRule);
    nsFontFaceRuleContainer *ptr = data->mFontFaceRules.AppendElement();
    if (!ptr)
      return PR_FALSE;
    ptr->mRule = fontFaceRule;
    ptr->mSheetType = data->mSheetType;
  }

  return PR_TRUE;
}

/* static */ PRBool
nsCSSRuleProcessor::CascadeSheetEnumFunc(nsICSSStyleSheet* aSheet, void* aData)
{
  nsCSSStyleSheet*  sheet = static_cast<nsCSSStyleSheet*>(aSheet);
  CascadeEnumData* data = static_cast<CascadeEnumData*>(aData);
  PRBool bSheetApplicable = PR_TRUE;
  sheet->GetApplicable(bSheetApplicable);

  if (bSheetApplicable &&
      sheet->UseForPresentation(data->mPresContext, data->mCacheKey) &&
      sheet->mInner) {
    nsCSSStyleSheet* child = sheet->mInner->mFirstChild;
    while (child) {
      CascadeSheetEnumFunc(child, data);
      child = child->mNext;
    }

    if (!sheet->mInner->mOrderedRules.EnumerateForwards(CascadeRuleEnumFunc,
                                                        data))
      return PR_FALSE;
  }
  return PR_TRUE;
}

static int CompareWeightData(const void* aArg1, const void* aArg2,
                             void* closure)
{
  const PerWeightData* arg1 = static_cast<const PerWeightData*>(aArg1);
  const PerWeightData* arg2 = static_cast<const PerWeightData*>(aArg2);
  return arg1->mWeight - arg2->mWeight; // put lower weight first
}


struct FillWeightArrayData {
  FillWeightArrayData(PerWeightData* aArrayData) :
    mIndex(0),
    mWeightArray(aArrayData)
  {
  }
  PRInt32 mIndex;
  PerWeightData* mWeightArray;
};


static PLDHashOperator
FillWeightArray(PLDHashTable *table, PLDHashEntryHdr *hdr,
                PRUint32 number, void *arg)
{
  FillWeightArrayData* data = static_cast<FillWeightArrayData*>(arg);
  const RuleByWeightEntry *entry = (const RuleByWeightEntry *)hdr;

  data->mWeightArray[data->mIndex++] = entry->data;

  return PL_DHASH_NEXT;
}

RuleCascadeData*
nsCSSRuleProcessor::GetRuleCascade(nsPresContext* aPresContext)
{
  // If anything changes about the presentation context, we will be
  // notified.  Otherwise, our cache is valid if mLastPresContext
  // matches aPresContext.  (The only rule processors used for multiple
  // pres contexts are for XBL.  These rule processors are probably less
  // likely to have @media rules, and thus the cache is pretty likely to
  // hit instantly even when we're switching between pres contexts.)

  if (!mRuleCascades || aPresContext != mLastPresContext) {
    RefreshRuleCascade(aPresContext);
  }
  mLastPresContext = aPresContext;

  return mRuleCascades;
}

void
nsCSSRuleProcessor::RefreshRuleCascade(nsPresContext* aPresContext)
{
  // Having RuleCascadeData objects be per-medium (over all variation
  // caused by media queries, handled through mCacheKey) works for now
  // since nsCSSRuleProcessor objects are per-document.  (For a given
  // set of stylesheets they can vary based on medium (@media) or
  // document (@-moz-document).)

  for (RuleCascadeData **cascadep = &mRuleCascades, *cascade;
       (cascade = *cascadep); cascadep = &cascade->mNext) {
    if (cascade->mCacheKey.Matches(aPresContext)) {
      // Ensure that the current one is always mRuleCascades.
      *cascadep = cascade->mNext;
      cascade->mNext = mRuleCascades;
      mRuleCascades = cascade;

      return;
    }
  }

  if (mSheets.Count() != 0) {
    nsAutoPtr<RuleCascadeData> newCascade(
      new RuleCascadeData(aPresContext->Medium(),
                          eCompatibility_NavQuirks == aPresContext->CompatibilityMode()));
    if (newCascade) {
      CascadeEnumData data(aPresContext, newCascade->mFontFaceRules,
                           newCascade->mCacheKey,
                           newCascade->mRuleHash.Arena(),
                           mSheetType);
      if (!data.mRulesByWeight.ops)
        return; /* out of memory */
      if (!mSheets.EnumerateForwards(CascadeSheetEnumFunc, &data))
        return; /* out of memory */

      // Sort the hash table of per-weight linked lists by weight.
      PRUint32 weightCount = data.mRulesByWeight.entryCount;
      nsAutoArrayPtr<PerWeightData> weightArray(new PerWeightData[weightCount]);
      FillWeightArrayData fwData(weightArray);
      PL_DHashTableEnumerate(&data.mRulesByWeight, FillWeightArray, &fwData);
      NS_QuickSort(weightArray, weightCount, sizeof(PerWeightData),
                   CompareWeightData, nsnull);

      // Put things into the rule hash backwards because it's easier to
      // build a singly linked list lowest-first that way.
      // The primary sort is by weight...
      PRUint32 i = weightCount;
      while (i > 0) {
        --i;
        // and the secondary sort is by order.  mRules are already backwards.
        RuleValue *ruleValue = weightArray[i].mRules;
        do {
          // Calling |AddRule| reuses mNext!
          RuleValue *next = ruleValue->mNext;
          if (!AddRule(ruleValue, newCascade))
            return; /* out of memory */
          ruleValue = next;
        } while (ruleValue);
      }

      // Ensure that the current one is always mRuleCascades.
      newCascade->mNext = mRuleCascades;
      mRuleCascades = newCascade.forget();
    }
  }
  return;
}

/* static */ PRBool
nsCSSRuleProcessor::SelectorListMatches(RuleProcessorData& aData,
                                        nsCSSSelectorList* aSelectorList)
{
  while (aSelectorList) {
    nsCSSSelector* sel = aSelectorList->mSelectors;
    NS_ASSERTION(sel, "Should have *some* selectors");
    NS_ASSERTION(!sel->IsPseudoElement(), "Shouldn't have been called");
    if (SelectorMatches(aData, sel, 0, PR_FALSE)) {
      nsCSSSelector* next = sel->mNext;
      if (!next || SelectorMatchesTree(aData, next, PR_FALSE)) {
        return PR_TRUE;
      }
    }

    aSelectorList = aSelectorList->mNext;
  }

  return PR_FALSE;
}
