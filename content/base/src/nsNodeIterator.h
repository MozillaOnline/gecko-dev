/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
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
 * The Original Code is this file as it was released on July 19 2008.
 *
 * The Initial Developer of the Original Code is
 * Craig Toppper.
 * Portions created by the Initial Developer are Copyright (C) 2008
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Craig Topper <craig.topper@gmail.com> (Original Author)
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
 * Implementation of DOM Traversal's nsIDOMNodeIterator
 */

#ifndef nsNodeIterator_h___
#define nsNodeIterator_h___

#include "nsIDOMNodeIterator.h"
#include "nsTraversal.h"
#include "nsCycleCollectionParticipant.h"
#include "nsStubMutationObserver.h"

class nsINode;
class nsIDOMNode;
class nsIDOMNodeFilter;

class nsNodeIterator : public nsIDOMNodeIterator,
                       public nsTraversal,
                       public nsStubMutationObserver
{
public:
    NS_DECL_CYCLE_COLLECTING_ISUPPORTS
    NS_DECL_NSIDOMNODEITERATOR

    nsNodeIterator(nsINode *aRoot,
                   PRUint32 aWhatToShow,
                   nsIDOMNodeFilter *aFilter,
                   PRBool aExpandEntityReferences);
    virtual ~nsNodeIterator();

    NS_DECL_NSIMUTATIONOBSERVER_CONTENTINSERTED
    NS_DECL_NSIMUTATIONOBSERVER_CONTENTREMOVED

    NS_DECL_CYCLE_COLLECTION_CLASS_AMBIGUOUS(nsNodeIterator, nsIDOMNodeIterator)

private:
    struct NodePointer {
        NodePointer() : mNode(nsnull) {};
        NodePointer(nsINode *aNode, PRBool aBeforeNode);

        PRBool MoveToNext(nsINode *aRoot);
        PRBool MoveToPrevious(nsINode *aRoot);

        PRBool MoveForward(nsINode *aRoot, nsINode *aParent, PRInt32 aChildNum);
        void MoveBackward(nsINode *aParent, PRInt32 aChildNum);

        void AdjustAfterInsertion(nsINode *aContainer, PRInt32 aIndexInContainer);
        void AdjustAfterRemoval(nsINode *aRoot, nsINode *aContainer, nsIContent *aChild, PRInt32 aIndexInContainer);
        
        void Clear() { mNode = nsnull; }

        nsINode *mNode;
        // pointer to the parent of mNode. Can be dangling if mNode is null or
        // points to the root
        nsINode *mNodeParent;
        PRBool mBeforeNode;
        // mNode's index in mNodeParent. Uninitialized if mNodeParent is null
        // or dangling (per above comment).
        PRInt32 mIndexInParent;
    };

    PRBool mDetached;
    NodePointer mPointer;
    NodePointer mWorkingPointer;
};

#endif
