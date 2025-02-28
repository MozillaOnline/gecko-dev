/* -*- Mode: c; tab-width: 8; c-basic-offset: 4; indent-tabs-mode: t; -*- */
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

#include "cairoint.h"

#include "cairo-surface-fallback-private.h"
#include "cairo-clip-private.h"

typedef struct {
    cairo_surface_t *dst;
    cairo_rectangle_int_t extents;
    cairo_image_surface_t *image;
    cairo_rectangle_int_t image_rect;
    void *image_extra;
} fallback_state_t;

/**
 * _fallback_init:
 *
 * Acquire destination image surface needed for an image-based
 * fallback.
 *
 * Return value: %CAIRO_INT_STATUS_NOTHING_TO_DO if the extents are not
 * visible, %CAIRO_STATUS_SUCCESS if some portion is visible and all
 * went well, or some error status otherwise.
 **/
static cairo_int_status_t
_fallback_init (fallback_state_t *state,
		cairo_surface_t  *dst,
		int               x,
		int               y,
		int               width,
		int               height)
{
    cairo_status_t status;

    state->extents.x = x;
    state->extents.y = y;
    state->extents.width = width;
    state->extents.height = height;

    state->dst = dst;

    status = _cairo_surface_acquire_dest_image (dst, &state->extents,
						&state->image, &state->image_rect,
						&state->image_extra);
    if (unlikely (status))
	return status;

    /* XXX: This NULL value tucked away in state->image is a rather
     * ugly interface. Cleaner would be to push the
     * CAIRO_INT_STATUS_NOTHING_TO_DO value down into
     * _cairo_surface_acquire_dest_image and its backend
     * counterparts. */
    if (state->image == NULL)
	return CAIRO_INT_STATUS_NOTHING_TO_DO;

    return CAIRO_STATUS_SUCCESS;
}

static void
_fallback_fini (fallback_state_t *state)
{
    _cairo_surface_release_dest_image (state->dst, &state->extents,
				       state->image, &state->image_rect,
				       state->image_extra);
}

typedef cairo_status_t (*cairo_draw_func_t) (void                          *closure,
					     cairo_operator_t               op,
					     const cairo_pattern_t         *src,
					     cairo_surface_t               *dst,
					     int                            dst_x,
					     int                            dst_y,
					     const cairo_rectangle_int_t   *extents);

static cairo_status_t
_create_composite_mask_pattern (cairo_surface_pattern_t       *mask_pattern,
				cairo_clip_t                  *clip,
				cairo_draw_func_t             draw_func,
				void                          *draw_closure,
				cairo_surface_t               *dst,
				const cairo_rectangle_int_t   *extents)
{
    cairo_surface_t *mask;
    cairo_status_t status;

    mask = cairo_surface_create_similar (dst,
					 CAIRO_CONTENT_ALPHA,
					 extents->width,
					 extents->height);
    if (mask->status)
	return mask->status;

    status = (*draw_func) (draw_closure, CAIRO_OPERATOR_ADD,
			   NULL, mask,
			   extents->x, extents->y,
			   extents);
    if (unlikely (status))
	goto CLEANUP_SURFACE;

    if (clip && clip->surface)
	status = _cairo_clip_combine_to_surface (clip, CAIRO_OPERATOR_IN,
						 mask,
						 extents->x, extents->y,
						 extents);
    if (unlikely (status))
	goto CLEANUP_SURFACE;

    _cairo_pattern_init_for_surface (mask_pattern, mask);

 CLEANUP_SURFACE:
    cairo_surface_destroy (mask);

    return status;
}

/* Handles compositing with a clip surface when the operator allows
 * us to combine the clip with the mask
 */
static cairo_status_t
_clip_and_composite_with_mask (cairo_clip_t                  *clip,
			       cairo_operator_t               op,
			       const cairo_pattern_t         *src,
			       cairo_draw_func_t              draw_func,
			       void                          *draw_closure,
			       cairo_surface_t               *dst,
			       const cairo_rectangle_int_t   *extents)
{
    cairo_surface_pattern_t mask_pattern;
    cairo_status_t status;

    status = _create_composite_mask_pattern (&mask_pattern,
					     clip,
					     draw_func, draw_closure,
					     dst, extents);
    if (unlikely (status))
	return status;

    status = _cairo_surface_composite (op,
				       src, &mask_pattern.base, dst,
				       extents->x,     extents->y,
				       0,              0,
				       extents->x,     extents->y,
				       extents->width, extents->height);

    _cairo_pattern_fini (&mask_pattern.base);

    return status;
}

/* Handles compositing with a clip surface when we have to do the operation
 * in two pieces and combine them together.
 */
static cairo_status_t
_clip_and_composite_combine (cairo_clip_t                  *clip,
			     cairo_operator_t               op,
			     const cairo_pattern_t         *src,
			     cairo_draw_func_t              draw_func,
			     void                          *draw_closure,
			     cairo_surface_t               *dst,
			     const cairo_rectangle_int_t   *extents)
{
    cairo_surface_t *intermediate;
    cairo_surface_pattern_t dst_pattern;
    cairo_surface_pattern_t intermediate_pattern;
    cairo_status_t status;

    /* We'd be better off here creating a surface identical in format
     * to dst, but we have no way of getting that information.
     * A CAIRO_CONTENT_CLONE or something might be useful.
     * cairo_surface_create_similar() also unnecessarily clears the surface.
     */
    intermediate = cairo_surface_create_similar (dst,
						 CAIRO_CONTENT_COLOR_ALPHA,
						 extents->width,
						 extents->height);
    if (intermediate->status)
	return intermediate->status;

    /* Initialize the intermediate surface from the destination surface
     */
    _cairo_pattern_init_for_surface (&dst_pattern, dst);

    status = _cairo_surface_composite (CAIRO_OPERATOR_SOURCE,
				       &dst_pattern.base, NULL, intermediate,
				       extents->x,     extents->y,
				       0,              0,
				       0,              0,
				       extents->width, extents->height);

    _cairo_pattern_fini (&dst_pattern.base);

    if (unlikely (status))
	goto CLEANUP_SURFACE;

    status = (*draw_func) (draw_closure, op,
			   src, intermediate,
			   extents->x, extents->y,
			   extents);
    if (unlikely (status))
	goto CLEANUP_SURFACE;

    /* Combine that with the clip
     */
    status = _cairo_clip_combine_to_surface (clip, CAIRO_OPERATOR_DEST_IN,
					     intermediate,
					     extents->x, extents->y,
					     extents);
    if (unlikely (status))
	goto CLEANUP_SURFACE;

    /* Punch the clip out of the destination
     */
    status = _cairo_clip_combine_to_surface (clip, CAIRO_OPERATOR_DEST_OUT,
					     dst,
					     0, 0,
					     extents);
    if (unlikely (status))
	goto CLEANUP_SURFACE;

    /* Now add the two results together
     */
    _cairo_pattern_init_for_surface (&intermediate_pattern, intermediate);

    status = _cairo_surface_composite (CAIRO_OPERATOR_ADD,
				       &intermediate_pattern.base, NULL, dst,
				       0,              0,
				       0,              0,
				       extents->x,     extents->y,
				       extents->width, extents->height);

    _cairo_pattern_fini (&intermediate_pattern.base);

 CLEANUP_SURFACE:
    cairo_surface_destroy (intermediate);

    return status;
}

/* Handles compositing for %CAIRO_OPERATOR_SOURCE, which is special; it's
 * defined as (src IN mask IN clip) ADD (dst OUT (mask IN clip))
 */
static cairo_status_t
_clip_and_composite_source (cairo_clip_t                  *clip,
			    const cairo_pattern_t         *src,
			    cairo_draw_func_t              draw_func,
			    void                          *draw_closure,
			    cairo_surface_t               *dst,
			    const cairo_rectangle_int_t   *extents)
{
    cairo_surface_pattern_t mask_pattern;
    cairo_status_t status;

    /* Create a surface that is mask IN clip
     */
    status = _create_composite_mask_pattern (&mask_pattern,
					     clip,
					     draw_func, draw_closure,
					     dst, extents);
    if (unlikely (status))
	return status;

    /* Compute dest' = dest OUT (mask IN clip)
     */
    status = _cairo_surface_composite (CAIRO_OPERATOR_DEST_OUT,
				       &mask_pattern.base, NULL, dst,
				       0,              0,
				       0,              0,
				       extents->x,     extents->y,
				       extents->width, extents->height);

    if (unlikely (status))
	goto CLEANUP_MASK_PATTERN;

    /* Now compute (src IN (mask IN clip)) ADD dest'
     */
    status = _cairo_surface_composite (CAIRO_OPERATOR_ADD,
				       src, &mask_pattern.base, dst,
				       extents->x,     extents->y,
				       0,              0,
				       extents->x,     extents->y,
				       extents->width, extents->height);

 CLEANUP_MASK_PATTERN:
    _cairo_pattern_fini (&mask_pattern.base);
    return status;
}

static int
_cairo_rectangle_empty (const cairo_rectangle_int_t *rect)
{
    return rect->width == 0 || rect->height == 0;
}

/**
 * _clip_and_composite:
 * @clip: a #cairo_clip_t
 * @op: the operator to draw with
 * @src: source pattern
 * @draw_func: function that can be called to draw with the mask onto a surface.
 * @draw_closure: data to pass to @draw_func.
 * @dst: destination surface
 * @extents: rectangle holding a bounding box for the operation; this
 *           rectangle will be used as the size for the temporary
 *           surface.
 *
 * When there is a surface clip, we typically need to create an intermediate
 * surface. This function handles the logic of creating a temporary surface
 * drawing to it, then compositing the result onto the target surface.
 *
 * @draw_func is to called to draw the mask; it will be called no more
 * than once.
 *
 * Return value: %CAIRO_STATUS_SUCCESS if the drawing succeeded.
 **/
static cairo_status_t
_clip_and_composite (cairo_clip_t                  *clip,
		     cairo_operator_t               op,
		     const cairo_pattern_t         *src,
		     cairo_draw_func_t              draw_func,
		     void                          *draw_closure,
		     cairo_surface_t               *dst,
		     const cairo_rectangle_int_t   *extents)
{
    cairo_solid_pattern_t solid_pattern;
    cairo_status_t status;

    if (_cairo_rectangle_empty (extents))
	/* Nothing to do */
	return CAIRO_STATUS_SUCCESS;

    if (op == CAIRO_OPERATOR_CLEAR) {
	_cairo_pattern_init_solid (&solid_pattern, CAIRO_COLOR_WHITE,
				   CAIRO_CONTENT_COLOR);
	src = &solid_pattern.base;
	op = CAIRO_OPERATOR_DEST_OUT;
    }

    if ((clip && clip->surface) || op == CAIRO_OPERATOR_SOURCE)
    {
	if (op == CAIRO_OPERATOR_SOURCE)
	    status = _clip_and_composite_source (clip,
						 src,
						 draw_func, draw_closure,
						 dst, extents);
	else if (_cairo_operator_bounded_by_mask (op))
	    status = _clip_and_composite_with_mask (clip, op,
						    src,
						    draw_func, draw_closure,
						    dst, extents);
	else
	    status = _clip_and_composite_combine (clip, op,
						  src,
						  draw_func, draw_closure,
						  dst, extents);
    }
    else
    {
	status = (*draw_func) (draw_closure, op,
			       src, dst,
			       0, 0,
			       extents);
    }

    return status;
}

/* Composites a region representing a set of trapezoids.
 */
static cairo_status_t
_composite_trap_region (cairo_clip_t            *clip,
			const cairo_pattern_t	*src,
			cairo_operator_t         op,
			cairo_surface_t         *dst,
			cairo_region_t          *trap_region,
			cairo_rectangle_int_t   *extents)
{
    cairo_status_t status;
    cairo_solid_pattern_t solid_pattern;
    cairo_surface_pattern_t mask;
    int num_rects = cairo_region_num_rectangles (trap_region);
    unsigned int clip_serial;
    cairo_surface_t *clip_surface = clip ? clip->surface : NULL;

    if (num_rects == 0)
	return CAIRO_STATUS_SUCCESS;

    if (clip_surface && op == CAIRO_OPERATOR_CLEAR) {
	_cairo_pattern_init_solid (&solid_pattern, CAIRO_COLOR_WHITE,
				   CAIRO_CONTENT_COLOR);
	src = &solid_pattern.base;
	op = CAIRO_OPERATOR_DEST_OUT;
    }

    if (num_rects > 1) {
      if (_cairo_surface_get_clip_mode (dst) != CAIRO_CLIP_MODE_REGION)
	    return CAIRO_INT_STATUS_UNSUPPORTED;

	clip_serial = _cairo_surface_allocate_clip_serial (dst);
	status = _cairo_surface_set_clip_region (dst,
						 trap_region,
						 clip_serial);
	if (unlikely (status))
	    return status;
    }

    if (clip_surface)
	_cairo_pattern_init_for_surface (&mask, clip_surface);

    status = _cairo_surface_composite (op,
				       src,
				       clip_surface ? &mask.base : NULL,
				       dst,
				       extents->x, extents->y,
				       extents->x - (clip_surface ? clip->surface_rect.x : 0),
				       extents->y - (clip_surface ? clip->surface_rect.y : 0),
				       extents->x, extents->y,
				       extents->width, extents->height);

    /* Restore the original clip if we modified it temporarily. */
    if (num_rects > 1) {
	cairo_status_t status2 = _cairo_surface_set_clip (dst, clip);
	if (status == CAIRO_STATUS_SUCCESS)
	    status = status2;
    }

    if (clip_surface)
      _cairo_pattern_fini (&mask.base);

    return status;
}

typedef struct {
    cairo_traps_t *traps;
    cairo_antialias_t antialias;
} cairo_composite_traps_info_t;

static cairo_status_t
_composite_traps_draw_func (void                          *closure,
			    cairo_operator_t               op,
			    const cairo_pattern_t         *src,
			    cairo_surface_t               *dst,
			    int                            dst_x,
			    int                            dst_y,
			    const cairo_rectangle_int_t   *extents)
{
    cairo_composite_traps_info_t *info = closure;
    cairo_solid_pattern_t pattern;

    if (dst_x != 0 || dst_y != 0)
	_cairo_traps_translate (info->traps, - dst_x, - dst_y);

    if (src == NULL) {
	_cairo_pattern_init_solid (&pattern, CAIRO_COLOR_WHITE,
				   CAIRO_CONTENT_COLOR);
	src = &pattern.base;
    }

    return _cairo_surface_composite_trapezoids (op,
						src, dst, info->antialias,
						extents->x,         extents->y,
						extents->x - dst_x, extents->y - dst_y,
						extents->width,     extents->height,
						info->traps->traps,
						info->traps->num_traps);
}

/* Warning: This call modifies the coordinates of traps */
static cairo_status_t
_clip_and_composite_trapezoids (const cairo_pattern_t *src,
				cairo_operator_t op,
				cairo_surface_t *dst,
				cairo_traps_t *traps,
				cairo_clip_t *clip,
				cairo_antialias_t antialias)
{
    cairo_status_t status;
    cairo_region_t *trap_region = NULL;
    cairo_region_t *clear_region = NULL;
    cairo_rectangle_int_t extents;
    cairo_composite_traps_info_t traps_info;

    if (_cairo_operator_bounded_by_mask (op) && traps->num_traps == 0)
        return CAIRO_STATUS_SUCCESS;

    status = _cairo_surface_get_extents (dst, &extents);
    if (unlikely (status))
        return status;

    status = _cairo_traps_extract_region (traps, &trap_region);
    if (status && status != CAIRO_INT_STATUS_UNSUPPORTED)
	return status;

    if (_cairo_operator_bounded_by_mask (op)) {
        cairo_rectangle_int_t trap_extents;

        if (trap_region) {
            status = _cairo_clip_intersect_to_region (clip, trap_region);
            if (unlikely (status))
                goto out;

            cairo_region_get_extents (trap_region, &trap_extents);
        } else {
            cairo_box_t trap_box;
            _cairo_traps_extents (traps, &trap_box);
            _cairo_box_round_to_rectangle (&trap_box, &trap_extents);
        }

        if (! _cairo_rectangle_intersect (&extents, &trap_extents)) {
	    status = CAIRO_STATUS_SUCCESS;
	    goto out;
	}

        status = _cairo_clip_intersect_to_rectangle (clip, &extents);
        if (unlikely (status))
            goto out;
    } else {
        cairo_surface_t *clip_surface = clip ? clip->surface : NULL;

        if (trap_region && !clip_surface) {
            /* If we optimize drawing with an unbounded operator to
             * _cairo_surface_fill_rectangles() or to drawing with a
             * clip region, then we have an additional region to clear.
             */
            clear_region = cairo_region_create_rectangle (&extents);

	    status = cairo_region_status (clear_region);
	    if (unlikely (status))
		goto out;

            status = _cairo_clip_intersect_to_region (clip, clear_region);
            if (unlikely (status))
                goto out;

            cairo_region_get_extents (clear_region, &extents);

            status = cairo_region_subtract (clear_region, trap_region);
            if (unlikely (status))
                goto out;

            if (cairo_region_is_empty (clear_region)) {
                cairo_region_destroy (clear_region);
		clear_region = NULL;
            }
        } else {
            status = _cairo_clip_intersect_to_rectangle (clip, &extents);
        }
    }

    if (unlikely (status))
        goto out;

    if (trap_region) {
        cairo_surface_t *clip_surface = clip ? clip->surface : NULL;

        if ((src->type == CAIRO_PATTERN_TYPE_SOLID ||
             op == CAIRO_OPERATOR_CLEAR) && !clip_surface) {
            const cairo_color_t *color;

            if (op == CAIRO_OPERATOR_CLEAR) {
                color = CAIRO_COLOR_TRANSPARENT;
            } else {
                color = &((cairo_solid_pattern_t *)src)->color;
            }

            /* Solid rectangles special case */
            status = _cairo_surface_fill_region (dst, op, color, trap_region);

            if (!status && clear_region) {
                status = _cairo_surface_fill_region (dst, CAIRO_OPERATOR_CLEAR,
                                                     CAIRO_COLOR_TRANSPARENT,
                                                     clear_region);
	    }

            goto out;
        }

        if ((_cairo_operator_bounded_by_mask (op) &&
             op != CAIRO_OPERATOR_SOURCE) || !clip_surface) {
            /* For a simple rectangle, we can just use composite(), for more
             * rectangles, we have to set a clip region. The cost of rasterizing
             * trapezoids is pretty high for most backends currently, so it's
             * worthwhile even if a region is needed.
             *
             * If we have a clip surface, we set it as the mask; this only works
             * for bounded operators other than SOURCE; for unbounded operators,
             * clip and mask cannot be interchanged. For SOURCE, the operator
             * as implemented by the backends is different in its handling
             * of the mask then what we want.
             *
             * CAIRO_INT_STATUS_UNSUPPORTED will be returned if the region has
             * more than rectangle and the destination doesn't support clip
             * regions. In that case, we fall through.
             */
            status = _composite_trap_region (clip, src, op, dst,
                                             trap_region, &extents);

            if (status != CAIRO_INT_STATUS_UNSUPPORTED) {
                if (!status && clear_region)
                    status = _cairo_surface_fill_region (dst, CAIRO_OPERATOR_CLEAR,
                                                         CAIRO_COLOR_TRANSPARENT,
                                                         clear_region);
                goto out;
            }
        }
    }

    traps_info.traps = traps;
    traps_info.antialias = antialias;

    status = _clip_and_composite (clip, op, src,
                                  _composite_traps_draw_func,
                                  &traps_info, dst, &extents);

out:
    if (trap_region)
        cairo_region_destroy (trap_region);
    if (clear_region)
        cairo_region_destroy (clear_region);

    return status;
}

typedef struct {
    cairo_path_fixed_t		*path;
    cairo_fill_rule_t		 fill_rule;
    double			 tolerance;
    cairo_antialias_t		 antialias;
} cairo_composite_spans_fill_info_t;

static cairo_status_t
_composite_spans_fill_func (void                          *closure,
			    cairo_operator_t               op,
			    const cairo_pattern_t         *src,
			    cairo_surface_t               *dst,
			    int                            dst_x,
			    int                            dst_y,
			    const cairo_rectangle_int_t   *extents)
{
    cairo_composite_rectangles_t rects;
    cairo_composite_spans_fill_info_t *info = closure;
    cairo_solid_pattern_t pattern;

    _cairo_composite_rectangles_init (
	&rects, extents->x, extents->y,
	extents->width, extents->height);

    /* The incoming dst_x/y are where we're pretending the origin of
     * the dst surface is -- *not* the offset of a rectangle where
     * we'd like to place the result. */
    rects.dst.x -= dst_x;
    rects.dst.y -= dst_y;

    /* We're called without a source pattern from
     * _create_composite_mask_pattern(). */
    if (src == NULL) {
	_cairo_pattern_init_solid (&pattern, CAIRO_COLOR_WHITE,
				   CAIRO_CONTENT_COLOR);
	src = &pattern.base;
    }

    return _cairo_path_fixed_fill_using_spans (
	op, src, info->path, dst,
	info->fill_rule, info->tolerance, info->antialias,
	&rects);
}

cairo_status_t
_cairo_surface_fallback_paint (cairo_surface_t		*surface,
			       cairo_operator_t		 op,
			       const cairo_pattern_t	*source)
{
    cairo_status_t status;
    cairo_rectangle_int_t extents;
    cairo_box_t box;
    cairo_traps_t traps;

    status = _cairo_surface_get_extents (surface, &extents);
    if (unlikely (status))
	return status;

    if (_cairo_operator_bounded_by_source (op)) {
	cairo_rectangle_int_t source_extents;

	status = _cairo_pattern_get_extents (source, &source_extents);
	if (unlikely (status))
	    return status;

	if (! _cairo_rectangle_intersect (&extents, &source_extents))
	    return CAIRO_STATUS_SUCCESS;
    }

    status = _cairo_clip_intersect_to_rectangle (surface->clip, &extents);
    if (unlikely (status))
	return status;

    _cairo_box_from_rectangle (&box, &extents);

    _cairo_traps_init_box (&traps, &box);

    status = _clip_and_composite_trapezoids (source,
				             op,
					     surface,
					     &traps,
					     surface->clip,
					     CAIRO_ANTIALIAS_NONE);

    _cairo_traps_fini (&traps);

    return status;
}

static cairo_status_t
_cairo_surface_mask_draw_func (void                          *closure,
			       cairo_operator_t               op,
			       const cairo_pattern_t         *src,
			       cairo_surface_t               *dst,
			       int                            dst_x,
			       int                            dst_y,
			       const cairo_rectangle_int_t *extents)
{
    cairo_pattern_t *mask = closure;

    if (src)
	return _cairo_surface_composite (op,
					 src, mask, dst,
					 extents->x,         extents->y,
					 extents->x,         extents->y,
					 extents->x - dst_x, extents->y - dst_y,
					 extents->width,     extents->height);
    else
	return _cairo_surface_composite (op,
					 mask, NULL, dst,
					 extents->x,         extents->y,
					 0,                  0, /* unused */
					 extents->x - dst_x, extents->y - dst_y,
					 extents->width,     extents->height);
}

cairo_status_t
_cairo_surface_fallback_mask (cairo_surface_t		*surface,
			      cairo_operator_t		 op,
			      const cairo_pattern_t	*source,
			      const cairo_pattern_t	*mask)
{
    cairo_status_t status;
    cairo_rectangle_int_t extents, source_extents, mask_extents;

    status = _cairo_surface_get_extents (surface, &extents);
    if (unlikely (status))
	return status;

    if (_cairo_operator_bounded_by_source (op)) {
	status = _cairo_pattern_get_extents (source, &source_extents);
	if (unlikely (status))
	    return status;

	if (! _cairo_rectangle_intersect (&extents, &source_extents))
	    return CAIRO_STATUS_SUCCESS;
    }

    if (_cairo_operator_bounded_by_mask (op)) {
	status = _cairo_pattern_get_extents (mask, &mask_extents);
	if (unlikely (status))
	    return status;

	if (! _cairo_rectangle_intersect (&extents, &mask_extents))
	    return CAIRO_STATUS_SUCCESS;
    }

    status = _cairo_clip_intersect_to_rectangle (surface->clip, &extents);
    if (unlikely (status))
	return status;

    status = _clip_and_composite (surface->clip, op,
				  source,
				  _cairo_surface_mask_draw_func,
				  (void *) mask,
				  surface,
				  &extents);

    return status;
}

cairo_status_t
_cairo_surface_fallback_stroke (cairo_surface_t		*surface,
				cairo_operator_t	 op,
				const cairo_pattern_t	*source,
				cairo_path_fixed_t	*path,
				cairo_stroke_style_t	*stroke_style,
				cairo_matrix_t		*ctm,
				cairo_matrix_t		*ctm_inverse,
				double			 tolerance,
				cairo_antialias_t	 antialias)
{
    cairo_status_t status;
    cairo_traps_t traps;
    cairo_box_t box;
    cairo_rectangle_int_t extents;

    status = _cairo_surface_get_extents (surface, &extents);
    if (unlikely (status))
        return status;

    if (_cairo_operator_bounded_by_source (op)) {
	cairo_rectangle_int_t source_extents;
	status = _cairo_pattern_get_extents (source, &source_extents);
	if (unlikely (status))
	    return status;

	if (! _cairo_rectangle_intersect (&extents, &source_extents))
	    return CAIRO_STATUS_SUCCESS;
    }

    status = _cairo_clip_intersect_to_rectangle (surface->clip, &extents);
    if (unlikely (status))
        return status;

    if (extents.width == 0 || extents.height == 0)
	return CAIRO_STATUS_SUCCESS;

    _cairo_box_from_rectangle (&box, &extents);

    _cairo_traps_init (&traps);
    _cairo_traps_limit (&traps, &box);

    status = _cairo_path_fixed_stroke_to_traps (path,
						stroke_style,
						ctm, ctm_inverse,
						tolerance,
						&traps);
    if (unlikely (status))
	goto FAIL;

    status = _clip_and_composite_trapezoids (source,
				             op,
					     surface,
					     &traps,
					     surface->clip,
					     antialias);

FAIL:
    _cairo_traps_fini (&traps);

    return status;
}

cairo_status_t
_cairo_surface_fallback_fill (cairo_surface_t		*surface,
			      cairo_operator_t		 op,
			      const cairo_pattern_t	*source,
			      cairo_path_fixed_t	*path,
			      cairo_fill_rule_t		 fill_rule,
			      double			 tolerance,
			      cairo_antialias_t		 antialias)
{
    cairo_status_t status;
    cairo_traps_t traps;
    cairo_box_t box;
    cairo_rectangle_int_t extents;

    status = _cairo_surface_get_extents (surface, &extents);
    if (unlikely (status))
        return status;

    if (_cairo_operator_bounded_by_source (op)) {
	cairo_rectangle_int_t source_extents;

	status = _cairo_pattern_get_extents (source, &source_extents);
	if (unlikely (status))
	    return status;

	if (! _cairo_rectangle_intersect (&extents, &source_extents))
	    return CAIRO_STATUS_SUCCESS;
    }

    status = _cairo_clip_intersect_to_rectangle (surface->clip, &extents);
    if (unlikely (status))
        return status;

    if (extents.width == 0 || extents.height == 0)
	return CAIRO_STATUS_SUCCESS;

    /* Ask if the surface would like to render this combination of
     * op/source/dst/antialias with spans or not, but don't actually
     * make a renderer yet.  We'll try to hit the region optimisations
     * in _clip_and_composite_trapezoids() if it looks like the path
     * is a region. */
    /* TODO: Until we have a mono scan converter we won't even try
     * to use spans for CAIRO_ANTIALIAS_NONE. */
    /* TODO: The region filling code should be lifted from
     * _clip_and_composite_trapezoids() and given first priority
     * explicitly before deciding between spans and trapezoids. */
    if (antialias != CAIRO_ANTIALIAS_NONE &&
	!_cairo_path_fixed_is_box (path, &box) &&
	!_cairo_path_fixed_is_region (path) &&
	_cairo_surface_check_span_renderer (
	    op, source, surface, antialias, NULL))
    {
	cairo_composite_spans_fill_info_t info;
	info.path = path;
	info.fill_rule = fill_rule;
	info.tolerance = tolerance;
	info.antialias = antialias;

	if (_cairo_operator_bounded_by_mask (op)) {
	    cairo_rectangle_int_t path_extents;

	    _cairo_path_fixed_approximate_clip_extents (path,
							&path_extents);
	    if (! _cairo_rectangle_intersect (&extents, &path_extents))
		return CAIRO_STATUS_SUCCESS;
	}

	return _clip_and_composite (
	    surface->clip, op, source,
	    _composite_spans_fill_func,
	    &info,
	    surface,
	    &extents);
    }

    /* Fall back to trapezoid fills. */
    _cairo_box_from_rectangle (&box, &extents);
    _cairo_traps_init (&traps);
    _cairo_traps_limit (&traps, &box);

    status = _cairo_path_fixed_fill_to_traps (path,
					      fill_rule,
					      tolerance,
					      &traps);
    if (unlikely (status)) {
	_cairo_traps_fini (&traps);
	return status;
    }

    status = _clip_and_composite_trapezoids (source,
					     op,
					     surface,
					     &traps,
					     surface->clip,
					     antialias);

    _cairo_traps_fini (&traps);

    return status;
}

typedef struct {
    cairo_scaled_font_t *font;
    cairo_glyph_t *glyphs;
    int num_glyphs;
} cairo_show_glyphs_info_t;

static cairo_status_t
_cairo_surface_old_show_glyphs_draw_func (void                          *closure,
					  cairo_operator_t               op,
					  const cairo_pattern_t         *src,
					  cairo_surface_t               *dst,
					  int                            dst_x,
					  int                            dst_y,
					  const cairo_rectangle_int_t *extents)
{
    cairo_show_glyphs_info_t *glyph_info = closure;
    cairo_solid_pattern_t pattern;
    cairo_status_t status;

    /* Modifying the glyph array is fine because we know that this function
     * will be called only once, and we've already made a copy of the
     * glyphs in the wrapper.
     */
    if (dst_x != 0 || dst_y != 0) {
	int i;

	for (i = 0; i < glyph_info->num_glyphs; ++i)
	{
	    ((cairo_glyph_t *) glyph_info->glyphs)[i].x -= dst_x;
	    ((cairo_glyph_t *) glyph_info->glyphs)[i].y -= dst_y;
	}
    }

    if (src == NULL) {
	_cairo_pattern_init_solid (&pattern, CAIRO_COLOR_WHITE,
				   CAIRO_CONTENT_COLOR);
	src = &pattern.base;
    }

    status = _cairo_surface_old_show_glyphs (glyph_info->font, op, src,
					     dst,
					     extents->x, extents->y,
					     extents->x - dst_x,
					     extents->y - dst_y,
					     extents->width,
					     extents->height,
					     glyph_info->glyphs,
					     glyph_info->num_glyphs);
    if (status != CAIRO_INT_STATUS_UNSUPPORTED)
	return status;

    return _cairo_scaled_font_show_glyphs (glyph_info->font,
					   op,
					   src, dst,
					   extents->x,         extents->y,
					   extents->x - dst_x,
					   extents->y - dst_y,
					   extents->width,     extents->height,
					   glyph_info->glyphs,
					   glyph_info->num_glyphs);
}

cairo_status_t
_cairo_surface_fallback_show_glyphs (cairo_surface_t		*surface,
				     cairo_operator_t		 op,
				     const cairo_pattern_t	*source,
				     cairo_glyph_t		*glyphs,
				     int			 num_glyphs,
				     cairo_scaled_font_t	*scaled_font)
{
    cairo_status_t status;
    cairo_rectangle_int_t extents;
    cairo_show_glyphs_info_t glyph_info;

    status = _cairo_surface_get_extents (surface, &extents);
    if (unlikely (status))
	return status;

    if (_cairo_operator_bounded_by_mask (op)) {
        cairo_rectangle_int_t glyph_extents;

	status = _cairo_scaled_font_glyph_device_extents (scaled_font,
							  glyphs,
							  num_glyphs,
							  &glyph_extents);
	if (unlikely (status))
	    return status;

	if (! _cairo_rectangle_intersect (&extents, &glyph_extents))
	    return CAIRO_STATUS_SUCCESS;
    }

    status = _cairo_clip_intersect_to_rectangle (surface->clip, &extents);
    if (unlikely (status))
	return status;

    glyph_info.font = scaled_font;
    glyph_info.glyphs = glyphs;
    glyph_info.num_glyphs = num_glyphs;

    status = _clip_and_composite (surface->clip,
				  op,
				  source,
				  _cairo_surface_old_show_glyphs_draw_func,
				  &glyph_info,
				  surface,
				  &extents);

    return status;
}

cairo_surface_t *
_cairo_surface_fallback_snapshot (cairo_surface_t *surface)
{
    cairo_surface_t *snapshot;
    cairo_status_t status;
    cairo_format_t format;
    cairo_surface_pattern_t pattern;
    cairo_image_surface_t *image;
    void *image_extra;

    status = _cairo_surface_acquire_source_image (surface,
						  &image, &image_extra);
    if (unlikely (status))
	return _cairo_surface_create_in_error (status);

    format = image->format;
    if (format == CAIRO_FORMAT_INVALID) {
	/* Non-standard images formats can be generated when retrieving
	 * images from unusual xservers, for example.
	 */
	format = _cairo_format_from_content (image->base.content);
    }
    snapshot = cairo_image_surface_create (format,
					   image->width,
					   image->height);
    if (cairo_surface_status (snapshot)) {
	_cairo_surface_release_source_image (surface, image, image_extra);
	return snapshot;
    }

    _cairo_pattern_init_for_surface (&pattern, &image->base);
    status = _cairo_surface_composite (CAIRO_OPERATOR_SOURCE,
			               &pattern.base,
				       NULL,
				       snapshot,
				       0, 0,
				       0, 0,
				       0, 0,
				       image->width,
				       image->height);
    _cairo_pattern_fini (&pattern.base);
    _cairo_surface_release_source_image (surface, image, image_extra);
    if (unlikely (status)) {
	cairo_surface_destroy (snapshot);
	return _cairo_surface_create_in_error (status);
    }

    return snapshot;
}

cairo_status_t
_cairo_surface_fallback_composite (cairo_operator_t		 op,
				   const cairo_pattern_t	*src,
				   const cairo_pattern_t	*mask,
				   cairo_surface_t		*dst,
				   int				 src_x,
				   int				 src_y,
				   int				 mask_x,
				   int				 mask_y,
				   int				 dst_x,
				   int				 dst_y,
				   unsigned int			 width,
				   unsigned int			 height)
{
    fallback_state_t state;
    cairo_status_t status;

    status = _fallback_init (&state, dst, dst_x, dst_y, width, height);
    if (unlikely (status)) {
	if (status == CAIRO_INT_STATUS_NOTHING_TO_DO)
	    return CAIRO_STATUS_SUCCESS;
	return status;
    }

    /* We know this will never fail with the image backend; but
     * instead of calling into it directly, we call
     * _cairo_surface_composite so that we get the correct device
     * offset handling.
     */
    status = _cairo_surface_composite (op, src, mask,
				       &state.image->base,
				       src_x, src_y, mask_x, mask_y,
				       dst_x - state.image_rect.x,
				       dst_y - state.image_rect.y,
				       width, height);
    _fallback_fini (&state);

    return status;
}

cairo_status_t
_cairo_surface_fallback_fill_rectangles (cairo_surface_t         *surface,
					 cairo_operator_t	  op,
					 const cairo_color_t	 *color,
					 cairo_rectangle_int_t   *rects,
					 int			  num_rects)
{
    fallback_state_t state;
    cairo_rectangle_int_t *offset_rects = NULL;
    cairo_status_t status;
    int x1, y1, x2, y2;
    int i;

    assert (surface->snapshot_of == NULL);

    if (num_rects <= 0)
	return CAIRO_STATUS_SUCCESS;

    /* Compute the bounds of the rectangles, so that we know what area of the
     * destination surface to fetch
     */
    x1 = rects[0].x;
    y1 = rects[0].y;
    x2 = rects[0].x + rects[0].width;
    y2 = rects[0].y + rects[0].height;

    for (i = 1; i < num_rects; i++) {
	if (rects[i].x < x1)
	    x1 = rects[i].x;
	if (rects[i].y < y1)
	    y1 = rects[i].y;

	if ((int) (rects[i].x + rects[i].width) > x2)
	    x2 = rects[i].x + rects[i].width;
	if ((int) (rects[i].y + rects[i].height) > y2)
	    y2 = rects[i].y + rects[i].height;
    }

    status = _fallback_init (&state, surface, x1, y1, x2 - x1, y2 - y1);
    if (unlikely (status)) {
	if (status == CAIRO_INT_STATUS_NOTHING_TO_DO)
	    return CAIRO_STATUS_SUCCESS;
	return status;
    }

    /* If the fetched image isn't at 0,0, we need to offset the rectangles */

    if (state.image_rect.x != 0 || state.image_rect.y != 0) {
	offset_rects = _cairo_malloc_ab (num_rects, sizeof (cairo_rectangle_int_t));
	if (unlikely (offset_rects == NULL)) {
	    status = _cairo_error (CAIRO_STATUS_NO_MEMORY);
	    goto DONE;
	}

	for (i = 0; i < num_rects; i++) {
	    offset_rects[i].x = rects[i].x - state.image_rect.x;
	    offset_rects[i].y = rects[i].y - state.image_rect.y;
	    offset_rects[i].width = rects[i].width;
	    offset_rects[i].height = rects[i].height;
	}

	rects = offset_rects;
    }

    status = _cairo_surface_fill_rectangles (&state.image->base,
					     op, color,
					     rects, num_rects);

    free (offset_rects);

 DONE:
    _fallback_fini (&state);

    return status;
}

cairo_status_t
_cairo_surface_fallback_composite_trapezoids (cairo_operator_t		op,
					      const cairo_pattern_t    *pattern,
					      cairo_surface_t	       *dst,
					      cairo_antialias_t		antialias,
					      int			src_x,
					      int			src_y,
					      int			dst_x,
					      int			dst_y,
					      unsigned int		width,
					      unsigned int		height,
					      cairo_trapezoid_t	       *traps,
					      int			num_traps)
{
    fallback_state_t state;
    cairo_trapezoid_t *offset_traps = NULL;
    cairo_status_t status;

    status = _fallback_init (&state, dst, dst_x, dst_y, width, height);
    if (unlikely (status)) {
	if (status == CAIRO_INT_STATUS_NOTHING_TO_DO)
	    return CAIRO_STATUS_SUCCESS;
	return status;
    }

    /* If the destination image isn't at 0,0, we need to offset the trapezoids */

    if (state.image_rect.x != 0 || state.image_rect.y != 0) {
	offset_traps = _cairo_malloc_ab (num_traps, sizeof (cairo_trapezoid_t));
	if (!offset_traps) {
	    status = _cairo_error (CAIRO_STATUS_NO_MEMORY);
	    goto DONE;
	}

	_cairo_trapezoid_array_translate_and_scale (offset_traps, traps, num_traps,
                                                    - state.image_rect.x, - state.image_rect.y,
                                                    1.0, 1.0);
	traps = offset_traps;
    }

    status = _cairo_surface_composite_trapezoids (op, pattern,
					          &state.image->base,
						  antialias,
						  src_x, src_y,
						  dst_x - state.image_rect.x,
						  dst_y - state.image_rect.y,
						  width, height,
						  traps, num_traps);
    if (offset_traps)
	free (offset_traps);

 DONE:
    _fallback_fini (&state);

    return status;
}

cairo_status_t
_cairo_surface_fallback_clone_similar (cairo_surface_t	*surface,
				       cairo_surface_t	*src,
				       cairo_content_t	 content,
				       int		 src_x,
				       int		 src_y,
				       int		 width,
				       int		 height,
				       int		*clone_offset_x,
				       int		*clone_offset_y,
				       cairo_surface_t **clone_out)
{
    cairo_surface_t *new_surface;
    cairo_surface_pattern_t pattern;
    cairo_status_t status;

    new_surface = _cairo_surface_create_similar_scratch (surface,
							 src->content & content,
							 width, height);
    if (new_surface->status)
	return new_surface->status;

    /* We have to copy these here, so that the coordinate spaces are correct */
    new_surface->device_transform = src->device_transform;
    new_surface->device_transform_inverse = src->device_transform_inverse;

    _cairo_pattern_init_for_surface (&pattern, src);
    cairo_matrix_init_translate (&pattern.base.matrix, src_x, src_y);
    pattern.base.filter = CAIRO_FILTER_NEAREST;

    status = _cairo_surface_paint (new_surface,
				   CAIRO_OPERATOR_SOURCE,
				   &pattern.base, NULL);
    _cairo_pattern_fini (&pattern.base);

    if (unlikely (status)) {
	cairo_surface_destroy (new_surface);
	return status;
    }

    *clone_offset_x = src_x;
    *clone_offset_y = src_y;
    *clone_out = new_surface;
    return CAIRO_STATUS_SUCCESS;
}
