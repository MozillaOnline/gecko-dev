/* cairo - a vector graphics library with display and print output
 *
 * Copyright © 2002 University of Southern California
 * Copyright © 2005 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it either under the terms of the GNU Lesser General Public
 * License version 2.1 as published by the Free Software Foundation
 * (the "LGPL") or, at your option, under the terms of the Mozilla
 * Public License Version 1.1 (the "MPL"). If you do not alter this
 * notice, a recipient may use your version of this file under either
 * the MPL or the LGPL.
 *
 * You should have received a copy of the LGPL along with this library
 * in the file COPYING-LGPL-2.1; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * You should have received a copy of the MPL along with this library
 * in the file COPYING-MPL-1.1
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY
 * OF ANY KIND, either express or implied. See the LGPL or the MPL for
 * the specific language governing rights and limitations.
 *
 * The Original Code is the cairo graphics library.
 *
 * The Initial Developer of the Original Code is University of Southern
 * California.
 *
 * Contributor(s):
 *	Carl D. Worth <cworth@cworth.org>
 */

#ifndef CAIRO_SURFACE_PRIVATE_H
#define CAIRO_SURFACE_PRIVATE_H

#include "cairo.h"

#include "cairo-types-private.h"
#include "cairo-reference-count-private.h"

typedef void (*cairo_surface_func_t) (cairo_surface_t *);

struct _cairo_surface {
    const cairo_surface_backend_t *backend;

    /* We allow surfaces to override the backend->type by shoving something
     * else into surface->type. This is for "wrapper" surfaces that want to
     * hide their internal type from the user-level API. */
    cairo_surface_type_t type;

    cairo_content_t content;

    cairo_reference_count_t ref_count;
    cairo_status_t status;
    cairo_bool_t finished;
    unsigned int unique_id;

    cairo_user_data_array_t user_data;
    cairo_user_data_array_t mime_data;

    cairo_matrix_t device_transform;
    cairo_matrix_t device_transform_inverse;

    /* The actual resolution of the device, in dots per inch. */
    double x_resolution;
    double y_resolution;

    /* The resolution that should be used when generating image-based
     * fallback; generally only used by the analysis/paginated
     * surfaces
     */
    double x_fallback_resolution;
    double y_fallback_resolution;

    cairo_clip_t *clip;

    /*
     * Each time a clip region is modified, it gets the next value in this
     * sequence.  This means that clip regions for this surface are uniquely
     * identified and updates to the clip can be readily identified
     */
    unsigned int next_clip_serial;
    /*
     * The serial number of the current clip.  This is set when
     * the surface clipping is set.  The gstate can then cheaply
     * check whether the surface clipping is already correct before
     * performing a rendering operation.
     *
     * The special value '0' is reserved for the unclipped case.
     */
    unsigned int current_clip_serial;

    /* A "snapshot" surface is immutable. See _cairo_surface_snapshot. */
    cairo_surface_t *snapshot_of;
    cairo_surface_func_t snapshot_detach;
    /* current snapshots of this surface */
    cairo_array_t snapshots;

    /*
     * Surface font options, falling back to backend's default options,
     * and set using _cairo_surface_set_font_options(), and propagated by
     * cairo_surface_create_similar().
     */
    cairo_bool_t has_font_options;
    cairo_font_options_t font_options;
};

#endif /* CAIRO_SURFACE_PRIVATE_H */
