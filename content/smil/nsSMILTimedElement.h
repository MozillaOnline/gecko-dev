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
 * The Original Code is the Mozilla SMIL module.
 *
 * The Initial Developer of the Original Code is Brian Birtles.
 * Portions created by the Initial Developer are Copyright (C) 2005
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Brian Birtles <birtles@gmail.com>
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

#ifndef NS_SMILTIMEDELEMENT_H_
#define NS_SMILTIMEDELEMENT_H_

#include "nsSMILInterval.h"
#include "nsSMILInstanceTime.h"
#include "nsSMILMilestone.h"
#include "nsSMILTimeValueSpec.h"
#include "nsSMILRepeatCount.h"
#include "nsSMILTypes.h"
#include "nsTArray.h"
#include "nsTHashtable.h"
#include "nsHashKeys.h"
#include "nsAutoPtr.h"
#include "nsAttrValue.h"

class nsISMILAnimationElement;
class nsSMILAnimationFunction;
class nsSMILTimeContainer;
class nsSMILTimeValue;
class nsIAtom;

//----------------------------------------------------------------------
// nsSMILTimedElement

class nsSMILTimedElement
{
public:
  nsSMILTimedElement();

  /*
   * Sets the owning animation element which this class uses to convert between
   * container times and to register timebase elements.
   */
  void SetAnimationElement(nsISMILAnimationElement* aElement);

  /*
   * Returns the time container with which this timed element is associated or
   * nsnull if it is not associated with a time container.
   */
  nsSMILTimeContainer* GetTimeContainer();

  /**
   * Methods for supporting the nsIDOMElementTimeControl interface.
   */

  /*
   * Adds a new begin instance time at the current container time plus or minus
   * the specified offset.
   *
   * @param aOffsetSeconds A real number specifying the number of seconds to add
   *                       to the current container time.
   * @return NS_OK if the operation succeeeded, or an error code otherwise.
   */
  nsresult BeginElementAt(double aOffsetSeconds);

  /*
   * Adds a new end instance time at the current container time plus or minus
   * the specified offset.
   *
   * @param aOffsetSeconds A real number specifying the number of seconds to add
   *                       to the current container time.
   * @return NS_OK if the operation succeeeded, or an error code otherwise.
   */
  nsresult EndElementAt(double aOffsetSeconds);

  /**
   * Methods for supporting the nsSVGAnimationElement interface.
   */

  /**
   * According to SVG 1.1 SE this returns
   *
   *   the begin time, in seconds, for this animation element's current
   *   interval, if it exists, regardless of whether the interval has begun yet.
   *
   * @return the start time as defined above in milliseconds or an unresolved
   * time if there is no current interval.
   */
  nsSMILTimeValue GetStartTime() const;

  /**
   * Returns the simple duration of this element.
   *
   * @return the simple duration in milliseconds or INDEFINITE.
   */
  nsSMILTimeValue GetSimpleDuration() const
  {
    return mSimpleDur;
  }

  /**
   * Internal SMIL methods
   */

  /**
   * Adds an instance time object this element's list of instance times.
   * These instance times are used when creating intervals.
   *
   * This method is typically called by an nsSMILTimeValueSpec.
   *
   * @param aInstanceTime   The time to add, expressed in container time.
   * @param aIsBegin        PR_TRUE if the time to be added represents a begin
   *                        time or PR_FALSE if it represents an end time.
   */
  void AddInstanceTime(nsSMILInstanceTime* aInstanceTime, PRBool aIsBegin);

  /**
   * Requests this element update the given instance time.
   *
   * This method is typically called by a child nsSMILTimeValueSpec.
   *
   * @param aInstanceTime   The instance time to update.
   * @param aUpdatedTime    The time to update aInstanceTime with.
   * @param aDependentTime  The instance time upon which aInstanceTime should be
   *                        based.
   * @param aIsBegin        PR_TRUE if the time to be updated represents a begin
   *                        instance time or PR_FALSE if it represents an end
   *                        instance time.
   */
  void UpdateInstanceTime(nsSMILInstanceTime* aInstanceTime,
                          nsSMILTimeValue& aUpdatedTime,
                          const nsSMILInstanceTime* aDependentTime,
                          PRBool aIsBegin);

  /**
   * Removes an instance time object this element's list of instance times.
   *
   * This method is typically called by a child nsSMILTimeValueSpec.
   *
   * @param aInstanceTime   The instance time to remove.
   * @param aIsBegin        PR_TRUE if the time to be removed represents a begin
   *                        time or PR_FALSE if it represents an end time.
   */
  void RemoveInstanceTime(nsSMILInstanceTime* aInstanceTime, PRBool aIsBegin);

  /**
   * Sets the object that will be called by this timed element each time it is
   * sampled.
   *
   * In Schmitz's model it is possible to associate several time clients with
   * a timed element but for now we only allow one.
   *
   * @param aClient   The time client to associate. Any previous time client
   *                  will be disassociated and no longer sampled. Setting this
   *                  to nsnull will simply disassociate the previous client, if
   *                  any.
   */
  void SetTimeClient(nsSMILAnimationFunction* aClient);

  /**
   * Samples the object at the given container time. Timing intervals are
   * updated and if this element is active at the given time the associated time
   * client will be sampled with the appropriate simple time.
   *
   * @param aContainerTime The container time at which to sample.
   */
  void SampleAt(nsSMILTime aContainerTime);

  /**
   * Performs a special sample for the end of an interval. Such a sample should
   * only advance the timed element (and any dependent elements) to the waiting
   * or postactive state. It should not cause a transition to the active state.
   * Transition to the active state is only performed on a regular SampleAt.
   *
   * This allows all interval ends at a given time to be processed first and
   * hence the new interval can be established based on full information of the
   * available instance times.
   *
   * @param aContainerTime The container time at which to sample.
   */
  void SampleEndAt(nsSMILTime aContainerTime);

  /**
   * Informs the timed element that its time container has changed time
   * relative to document time. The timed element therefore needs to update its
   * dependent elements (which may belong to a different time container) so they
   * can re-resolve their times.
   */
  void HandleContainerTimeChange();

  /**
   * Reset the element's internal state. As described in SMILANIM 3.3.7, all
   * instance times associated with DOM calls, events, etc. are cleared.
   */
  void Reset();

  /**
   * Attempts to set an attribute on this timed element.
   *
   * @param aAttribute  The name of the attribute to set. The namespace of this
   *                    attribute is not specified as it is checked by the host
   *                    element. Only attributes in the namespace defined for
   *                    SMIL attributes in the host language are passed to the
   *                    timed element.
   * @param aValue      The attribute value.
   * @param aResult     The nsAttrValue object that may be used for storing the
   *                    parsed result.
   * @param aContextNode The node to use for context when resolving references
   *                     to other elements.
   * @param[out] aParseResult The result of parsing the attribute. Will be set
   *                          to NS_OK if parsing is successful.
   *
   * @return PR_TRUE if the given attribute is a timing attribute, PR_FALSE
   * otherwise.
   */
  PRBool SetAttr(nsIAtom* aAttribute, const nsAString& aValue,
                 nsAttrValue& aResult, nsIContent* aContextNode,
                 nsresult* aParseResult = nsnull);

  /**
   * Attempts to unset an attribute on this timed element.
   *
   * @param aAttribute  The name of the attribute to set. As with SetAttr the
   *                    namespace of the attribute is not specified (see
   *                    SetAttr).
   *
   * @return PR_TRUE if the given attribute is a timing attribute, PR_FALSE
   * otherwise.
   */
  PRBool UnsetAttr(nsIAtom* aAttribute);

  /**
   * Adds a syncbase dependency to the list of dependents that will be notified
   * when this timed element creates, deletes, or updates its current interval.
   *
   * @param aDependent  The nsSMILTimeValueSpec object to notify. A raw pointer
   *                    to this object will be stored. Therefore it is necessary
   *                    for the object to be explicitly unregistered (with
   *                    RemoveDependent) when it is destroyed.
   */
  void AddDependent(nsSMILTimeValueSpec& aDependent);

  /**
   * Removes a syncbase dependency from the list of dependents that are notified
   * when the current interval is modified.
   *
   * @param aDependent  The nsSMILTimeValueSpec object to unregister.
   */
  void RemoveDependent(nsSMILTimeValueSpec& aDependent);

  /**
   * Determines if this timed element is dependent on the given timed element's
   * begin time for the interval currently in effect. Whilst the element is in
   * the active state this is the current interval and in the postactive or
   * waiting state this is the previous interval if one exists. In all other
   * cases the element is not considered a time dependent of any other element.
   *
   * @param aOther    The potential syncbase element.
   * @return PR_TRUE if this timed element's begin time for the currently
   * effective interval is directly or indirectly derived from aOther, PR_FALSE
   * otherwise.
   */
  PRBool IsTimeDependent(const nsSMILTimedElement& aOther) const;

  /**
   * Called when the timed element has been bound to the document so that
   * references from this timed element to other elements can be resolved.
   *
   * @param aContextNode  The node which provides the necessary context for
   *                      resolving references. This is typically the element in
   *                      the host language that owns this timed element. Should
   *                      not be null.
   */
  void BindToTree(nsIContent* aContextNode);

  /**
   * Called when the timed element has been removed from a document so that
   * references to other elements can be broken.
   */
  void DissolveReferences() { Unlink(); }

  // Cycle collection
  void Traverse(nsCycleCollectionTraversalCallback* aCallback);
  void Unlink();

protected:
  // Typedefs
  typedef nsTArray<nsAutoPtr<nsSMILTimeValueSpec> > TimeValueSpecList;
  typedef nsTArray<nsRefPtr<nsSMILInstanceTime> >   InstanceTimeList;
  typedef nsPtrHashKey<nsSMILTimeValueSpec> TimeValueSpecPtrKey;
  typedef nsTHashtable<TimeValueSpecPtrKey> TimeValueSpecHashSet;

  // Helper classes
  class InstanceTimeComparator {
    public:
      PRBool Equals(const nsSMILInstanceTime* aElem1,
                    const nsSMILInstanceTime* aElem2) const;
      PRBool LessThan(const nsSMILInstanceTime* aElem1,
                      const nsSMILInstanceTime* aElem2) const;
  };

  struct NotifyTimeDependentsParams {
    nsSMILInterval*      mCurrentInterval;
    nsSMILTimeContainer* mTimeContainer;
  };

  //
  // Implementation helpers
  //

  nsresult          SetBeginSpec(const nsAString& aBeginSpec,
                                 nsIContent* aContextNode);
  nsresult          SetEndSpec(const nsAString& aEndSpec,
                               nsIContent* aContextNode);
  nsresult          SetSimpleDuration(const nsAString& aDurSpec);
  nsresult          SetMin(const nsAString& aMinSpec);
  nsresult          SetMax(const nsAString& aMaxSpec);
  nsresult          SetRestart(const nsAString& aRestartSpec);
  nsresult          SetRepeatCount(const nsAString& aRepeatCountSpec);
  nsresult          SetRepeatDur(const nsAString& aRepeatDurSpec);
  nsresult          SetFillMode(const nsAString& aFillModeSpec);

  void              UnsetBeginSpec();
  void              UnsetEndSpec();
  void              UnsetSimpleDuration();
  void              UnsetMin();
  void              UnsetMax();
  void              UnsetRestart();
  void              UnsetRepeatCount();
  void              UnsetRepeatDur();
  void              UnsetFillMode();

  nsresult          SetBeginOrEndSpec(const nsAString& aSpec,
                                      nsIContent* aContextNode,
                                      PRBool aIsBegin);
  void              ClearBeginOrEndSpecs(PRBool aIsBegin);
  void              DoSampleAt(nsSMILTime aContainerTime, PRBool aEndOnly);

  /**
   * Calculates the next acceptable interval for this element after the
   * specified interval, or, if no previous interval is specified, it will be
   * the first interval with an end time after t=0.
   *
   * @see SMILANIM 3.6.8
   *
   * @param aPrevInterval   The previous interval used. If supplied, the first
   *                        interval that begins after aPrevInterval will be
   *                        returned. May be nsnull.
   * @param aFixedBeginTime The time to use for the start of the interval. This
   *                        is used when only the endpoint of the interval
   *                        should be updated such as when the animation is in
   *                        the ACTIVE state. May be nsnull.
   * @param[out] aResult    The next interval. Will be unchanged if no suitable
   *                        interval was found (in which case NS_ERROR_FAILURE
   *                        will be returned).
   * @return  NS_OK if a suitable interval was found, NS_ERROR_FAILURE
   * otherwise.
   */
  nsresult          GetNextInterval(const nsSMILInterval* aPrevInterval,
                                    const nsSMILInstanceTime* aFixedBeginTime,
                                    nsSMILInterval& aResult);
  nsSMILInstanceTime* GetNextGreater(const InstanceTimeList& aList,
                                     const nsSMILTimeValue& aBase,
                                     PRInt32& aPosition) const;
  nsSMILInstanceTime* GetNextGreaterOrEqual(const InstanceTimeList& aList,
                                            const nsSMILTimeValue& aBase,
                                            PRInt32& aPosition) const;
  nsSMILTimeValue   CalcActiveEnd(const nsSMILTimeValue& aBegin,
                                  const nsSMILTimeValue& aEnd) const;
  nsSMILTimeValue   GetRepeatDuration() const;
  nsSMILTimeValue   ApplyMinAndMax(const nsSMILTimeValue& aDuration) const;
  nsSMILTime        ActiveTimeToSimpleTime(nsSMILTime aActiveTime,
                                           PRUint32& aRepeatIteration);
  nsSMILInstanceTime* CheckForEarlyEnd(
                        const nsSMILTimeValue& aContainerTime) const;
  void              UpdateCurrentInterval(PRBool aForceChangeNotice = PR_FALSE);
  void              SampleSimpleTime(nsSMILTime aActiveTime);
  void              SampleFillValue();
  void              AddInstanceTimeFromCurrentTime(nsSMILTime aCurrentTime,
                        double aOffsetSeconds, PRBool aIsBegin);
  void              RegisterMilestone();
  PRBool            GetNextMilestone(nsSMILMilestone& aNextMilestone) const;

  void              NotifyNewInterval();
  void              NotifyChangedInterval();
  void              NotifyDeletedInterval();
  const nsSMILInstanceTime* GetEffectiveBeginInstance() const;

  // Hashtable callback methods
  PR_STATIC_CALLBACK(PLDHashOperator) NotifyNewIntervalCallback(
      TimeValueSpecPtrKey* aKey, void* aData);
  PR_STATIC_CALLBACK(PLDHashOperator) NotifyChangedIntervalCallback(
      TimeValueSpecPtrKey* aKey, void* aData);
  PR_STATIC_CALLBACK(PLDHashOperator) NotifyDeletedIntervalCallback(
      TimeValueSpecPtrKey* aKey, void* /* unused */);
  static inline void SanityCheckTimeDependentCallbackArgs(
      TimeValueSpecPtrKey* aKey, NotifyTimeDependentsParams* aParams,
      PRBool aExpectingParams);

  //
  // Members
  //
  nsISMILAnimationElement*        mAnimationElement; // [weak] won't outlive
                                                     // owner
  TimeValueSpecList               mBeginSpecs; // [strong]
  TimeValueSpecList               mEndSpecs; // [strong]

  nsSMILTimeValue                 mSimpleDur;

  nsSMILRepeatCount               mRepeatCount;
  nsSMILTimeValue                 mRepeatDur;

  nsSMILTimeValue                 mMin;
  nsSMILTimeValue                 mMax;

  enum nsSMILFillMode
  {
    FILL_REMOVE,
    FILL_FREEZE
  };
  nsSMILFillMode                  mFillMode;
  static nsAttrValue::EnumTable   sFillModeTable[];

  enum nsSMILRestartMode
  {
    RESTART_ALWAYS,
    RESTART_WHENNOTACTIVE,
    RESTART_NEVER
  };
  nsSMILRestartMode               mRestartMode;
  static nsAttrValue::EnumTable   sRestartModeTable[];

  //
  // We need to distinguish between attempting to set the begin spec and failing
  // (in which case the mBeginSpecs array will be empty) and not attempting to
  // set the begin spec at all. In the first case, we should act as if the begin
  // was indefinite, and in the second, we should act as if begin was 0s.
  //
  PRPackedBool                    mBeginSpecSet;
  PRPackedBool                    mEndHasEventConditions;

  InstanceTimeList                mBeginInstances;
  InstanceTimeList                mEndInstances;
  PRUint32                        mInstanceSerialIndex;

  nsSMILAnimationFunction*        mClient;
  nsSMILInterval                  mCurrentInterval;
  nsSMILInterval                  mPrevInterval;
  nsSMILMilestone                 mPrevRegisteredMilestone;
  static const nsSMILMilestone    sMaxMilestone;

  // Set of dependent time value specs to be notified when creating, updating,
  // or deleting the current interval.
  //
  // [weak] The nsSMILTimeValueSpec objects register themselves and unregister
  // on destruction. Likewise, we notify them when we are destroyed.
  TimeValueSpecHashSet mTimeDependents;

  /**
   * The state of the element in its life-cycle. These states are based on the
   * element life-cycle described in SMILANIM 3.6.8
   */
  enum nsSMILElementState
  {
    STATE_STARTUP,
    STATE_WAITING,
    STATE_ACTIVE,
    STATE_POSTACTIVE
  };
  nsSMILElementState              mElementState;
};

#endif // NS_SMILTIMEDELEMENT_H_
