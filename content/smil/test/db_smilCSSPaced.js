/* -*- Mode: Java; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/* vim: set shiftwidth=4 tabstop=4 autoindent cindent noexpandtab: */
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
 * The Original Code is Mozilla SMIL Test Code.
 *
 * The Initial Developer of the Original Code is the Mozilla Corporation.
 * Portions created by the Initial Developer are Copyright (C) 2009
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *   Daniel Holbert <dholbert@mozilla.com> (original author)
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

/* testcase data for paced-mode animations of CSS properties */

// Lists of testcases for re-use across multiple properties of the same type
var _pacedTestLists =
{
  color: [
    new AnimTestcasePaced("rgb(2,  4,  6); " +
                          "rgb(4,  8, 12); " +
                          "rgb(8, 16, 24)",
                          { comp0: "rgb(2, 4, 6)",
                            comp1_6: "rgb(3, 6, 9)",
                            comp1_3: "rgb(4, 8, 12)",
                            comp2_3: "rgb(6, 12, 18)",
                            comp1:   "rgb(8, 16, 24)"
                          }),
    new AnimTestcasePaced("rgb(10, 10, 10); " +
                          "rgb(20, 10, 8); " +
                          "rgb(20, 30, 4)",
                          { comp0:   "rgb(10, 10, 10)",
                            comp1_6: "rgb(15, 10, 9)",
                            comp1_3: "rgb(20, 10, 8)",
                            comp2_3: "rgb(20, 20, 6)",
                            comp1:   "rgb(20, 30, 4)"
                          }),
    new AnimTestcasePaced("olive; " +        // rgb(128, 128, 0)
                          "currentColor; " + // rgb(50, 50, 50)
                          "rgb(206, 150, 206)",
                          { comp0:   "rgb(128, 128, 0)",
                            comp1_6: "rgb(89, 89, 25)",
                            comp1_3: "rgb(50, 50, 50)",
                            comp2_3: "rgb(128, 100, 128)",
                            comp1:   "rgb(206, 150, 206)"
                          }),
  ],
  paintServer : [
    // Sanity check: These aren't interpolatable -- they should end up
    // ignoring the calcMode="paced" and falling into discrete-mode.
    new AnimTestcasePaced("url(#gradA); url(#gradB)",
          {
            comp0:   "url(\"" + document.URL + "#gradA\") rgb(0, 0, 0)",
            comp1_6: "url(\"" + document.URL + "#gradA\") rgb(0, 0, 0)",
            comp1_3: "url(\"" + document.URL + "#gradA\") rgb(0, 0, 0)",
            comp2_3: "url(\"" + document.URL + "#gradB\") rgb(0, 0, 0)",
            comp1:   "url(\"" + document.URL + "#gradB\") rgb(0, 0, 0)"
          },
          "need support for URI-based paints"),
    new AnimTestcasePaced("url(#gradA); url(#gradB); url(#gradC)",
          {
            comp0:   "url(\"" + document.URL + "#gradA\") rgb(0, 0, 0)",
            comp1_6: "url(\"" + document.URL + "#gradA\") rgb(0, 0, 0)",
            comp1_3: "url(\"" + document.URL + "#gradB\") rgb(0, 0, 0)",
            comp2_3: "url(\"" + document.URL + "#gradC\") rgb(0, 0, 0)",
            comp1:   "url(\"" + document.URL + "#gradC\") rgb(0, 0, 0)"
          },
          "need support for URI-based paints"),
  ],
  lengthPx : [
    new AnimTestcasePaced("0px; 2px; 6px",
                          { comp0:   "0px",
                            comp1_6: "1px",
                            comp1_3: "2px",
                            comp2_3: "4px",
                            comp1:   "6px"
                          }),
    new AnimTestcasePaced("10px; 12px; 8px",
                          { comp0:   "10px",
                            comp1_6: "11px",
                            comp1_3: "12px",
                            comp2_3: "10px",
                            comp1:   "8px"
                          }),
  ],
  lengthPctSVG : [
    new AnimTestcasePaced("5%; 6%; 4%",
                          { comp0:   "5%",
                            comp1_6: "5.5%",
                            comp1_3: "6%",
                            comp2_3: "5%",
                            comp1:   "4%"
                          }),
  ],
  lengthPxPctSVG : [
    new AnimTestcasePaced("0px; 1%; 6px",
                          { comp0:   "0px",
                            comp1_6: "1px",
                            comp1_3: "1%",
                            comp2_3: "4px",
                            comp1:   "6px"
                          },
                          "need support for interpolating between " +
                          "px and percent values"),
  ],
  opacity : [
    new AnimTestcasePaced("0; 0.2; 0.6",
                          { comp0:   "0",
                            comp1_6: "0.1",
                            comp1_3: "0.2",
                            comp2_3: "0.4",
                            comp1:   "0.6"
                          }),
    new AnimTestcasePaced("0.7; 1.0; 0.4",
                          { comp0:   "0.7",
                            comp1_6: "0.85",
                            comp1_3: "1",
                            comp2_3: "0.7",
                            comp1:   "0.4"
                          }),
  ],
  rect : [
    new AnimTestcasePaced("rect(2px, 4px, 6px, 8px); " +
                          "rect(4px, 8px, 12px, 16px); " +
                          "rect(8px, 16px, 24px, 32px)",
                          { comp0:   "rect(2px, 4px, 6px, 8px)",
                            comp1_6: "rect(3px, 6px, 9px, 12px)",
                            comp1_3: "rect(4px, 8px, 12px, 16px)",
                            comp2_3: "rect(6px, 12px, 18px, 24px)",
                            comp1:   "rect(8px, 16px, 24px, 32px)"
                          }),
    new AnimTestcasePaced("rect(10px, 10px, 10px, 10px); " +
                          "rect(20px, 10px, 50px, 8px); " +
                          "rect(20px, 30px, 130px, 4px)",
                          { comp0:   "rect(10px, 10px, 10px, 10px)",
                            comp1_6: "rect(15px, 10px, 30px, 9px)",
                            comp1_3: "rect(20px, 10px, 50px, 8px)",
                            comp2_3: "rect(20px, 20px, 90px, 6px)",
                            comp1:   "rect(20px, 30px, 130px, 4px)"
                          }),
    new AnimTestcasePaced("rect(10px, auto, 10px, 10px); " +
                          "rect(20px, auto, 50px, 8px); " +
                          "rect(40px, auto, 130px, 4px)",
                          { comp0:   "rect(10px, auto, 10px, 10px)",
                            comp1_6: "rect(15px, auto, 30px, 9px)",
                            comp1_3: "rect(20px, auto, 50px, 8px)",
                            comp2_3: "rect(30px, auto, 90px, 6px)",
                            comp1:   "rect(40px, auto, 130px, 4px)"
                          }),
    // Paced-mode animation is not supported in these next few cases
    // (Can't compute subcomponent distance between 'auto' & px-values)
    new AnimTestcasePaced("rect(10px, 10px, 10px, auto); " +
                          "rect(20px, 10px, 50px, 8px); " +
                          "rect(20px, 30px, 130px, 4px)",
                          { comp0:   "rect(10px, 10px, 10px, auto)",
                            comp1_6: "rect(10px, 10px, 10px, auto)",
                            comp1_3: "rect(20px, 10px, 50px, 8px)",
                            comp2_3: "rect(20px, 30px, 130px, 4px)",
                            comp1:   "rect(20px, 30px, 130px, 4px)"
                          }),
    new AnimTestcasePaced("rect(10px, 10px, 10px, 10px); " +
                          "rect(20px, 10px, 50px, 8px); " +
                          "auto",
                          { comp0:   "rect(10px, 10px, 10px, 10px)",
                            comp1_6: "rect(10px, 10px, 10px, 10px)",
                            comp1_3: "rect(20px, 10px, 50px, 8px)",
                            comp2_3: "auto",
                            comp1:   "auto"
                          }),
    new AnimTestcasePaced("auto; " +
                          "auto; " +
                          "rect(20px, 30px, 130px, 4px)",
                          { comp0:   "auto",
                            comp1_6: "auto",
                            comp1_3: "auto",
                            comp2_3: "rect(20px, 30px, 130px, 4px)",
                            comp1:   "rect(20px, 30px, 130px, 4px)"
                          }),
    new AnimTestcasePaced("auto; auto; auto",
                          { comp0:   "auto",
                            comp1_6: "auto",
                            comp1_3: "auto",
                            comp2_3: "auto",
                            comp1:   "auto"
                          }),
  ],
};

// TODO: test more properties here.
var gPacedBundles =
[
  new TestcaseBundle(gPropList.clip,  _pacedTestLists.rect),
  new TestcaseBundle(gPropList.color, _pacedTestLists.color),
  new TestcaseBundle(gPropList.direction, [
    new AnimTestcasePaced("rtl; ltr; rtl")
  ]),
  new TestcaseBundle(gPropList.fill,
                     [].concat(_pacedTestLists.color,
                               _pacedTestLists.paintServer)),
  new TestcaseBundle(gPropList.font_size,
                     [].concat(_pacedTestLists.lengthPx, [
    new AnimTestcasePaced("20%; 24%; 16%",
                          { comp0:   "10px",
                            comp1_6: "11px",
                            comp1_3: "12px",
                            comp2_3: "10px",
                            comp1:   "8px"
                          }),
    new AnimTestcasePaced("0px; 4%; 6px",
                          { comp0:   "0px",
                            comp1_6: "1px",
                            comp1_3: "2px",
                            comp2_3: "4px",
                            comp1:   "6px"
                          }),
    ])
  ),
  new TestcaseBundle(gPropList.font_size_adjust, [
    new AnimTestcasePaced("0.2; 0.6; 0.8",
                          { comp0:   "0.2",
                            comp1_6: "0.3",
                            comp1_3: "0.4",
                            comp2_3: "0.6",
                            comp1:   "0.8"
                          }),
    new AnimTestcasePaced("none; none; 0.5",
                          { comp0:   "none",
                            comp1_6: "none",
                            comp1_3: "none",
                            comp2_3: "0.5",
                            comp1:   "0.5"
                          }),
  ]),
  new TestcaseBundle(gPropList.font_family, [
    // Sanity check: 'font-family' isn't interpolatable.  It should end up
    // ignoring the calcMode="paced" and falling into discrete-mode.
    new AnimTestcasePaced("serif; sans-serif; monospace",
                          { comp0:   "serif",
                            comp1_6: "serif",
                            comp1_3: "sans-serif",
                            comp2_3: "monospace",
                            comp1:   "monospace"
                          },
                          "need support for more font properties"),
  ]),
  new TestcaseBundle(gPropList.opacity, _pacedTestLists.opacity),
  new TestcaseBundle(gPropList.stroke_dasharray,
                     [].concat(_pacedTestLists.lengthPctSVG, [
    new AnimTestcasePaced("7, 7, 7; 7, 10, 3; 1, 2, 3",
                          { comp0:   "7, 7, 7",
                            comp1_6: "7, 8.5, 5",
                            comp1_3: "7, 10, 3",
                            comp2_3: "4, 6, 3",
                            comp1:   "1, 2, 3"
                          }),
  ])),
  new TestcaseBundle(gPropList.stroke_dashoffset,
                     [].concat(_pacedTestLists.lengthPx,
                               _pacedTestLists.lengthPctSVG,
                               _pacedTestLists.lengthPxPctSVG)),
  new TestcaseBundle(gPropList.stroke_width,
                     [].concat(_pacedTestLists.lengthPx,
                               _pacedTestLists.lengthPctSVG,
                               _pacedTestLists.lengthPxPctSVG)),
  // XXXdholbert TODO: test 'stroke-dasharray' once we support animating it
];
