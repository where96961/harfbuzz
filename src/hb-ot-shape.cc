/*
 * Copyright © 2009,2010  Red Hat, Inc.
 * Copyright © 2010,2011,2012  Google, Inc.
 *
 *  This is part of HarfBuzz, a text shaping library.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and its documentation for any purpose, provided that the
 * above copyright notice and the following two paragraphs appear in
 * all copies of this software.
 *
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN
 * IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 * Red Hat Author(s): Behdad Esfahbod
 * Google Author(s): Behdad Esfahbod
 */

#define HB_SHAPER ot
#define hb_ot_shaper_face_data_t hb_ot_layout_t
#define hb_ot_shaper_shape_plan_data_t hb_ot_shape_plan_t
#include "hb-shaper-impl-private.hh"

#include "hb-ot-shape-private.hh"
#include "hb-ot-shape-complex-private.hh"
#include "hb-ot-shape-fallback-private.hh"
#include "hb-ot-shape-normalize-private.hh"

#include "hb-ot-layout-private.hh"
#include "hb-set-private.hh"


static hb_tag_t common_features[] = {
  HB_TAG('c','c','m','p'),
  HB_TAG('l','i','g','a'),
  HB_TAG('l','o','c','l'),
  HB_TAG('m','a','r','k'),
  HB_TAG('m','k','m','k'),
  HB_TAG('r','l','i','g'),
};


static hb_tag_t horizontal_features[] = {
  HB_TAG('c','a','l','t'),
  HB_TAG('c','l','i','g'),
  HB_TAG('c','u','r','s'),
  HB_TAG('k','e','r','n'),
  HB_TAG('r','c','l','t'),
};

/* Note:
 * Technically speaking, vrt2 and vert are mutually exclusive.
 * According to the spec, valt and vpal are also mutually exclusive.
 * But we apply them all for now.
 */
static hb_tag_t vertical_features[] = {
  HB_TAG('v','a','l','t'),
  HB_TAG('v','e','r','t'),
  HB_TAG('v','k','r','n'),
  HB_TAG('v','p','a','l'),
  HB_TAG('v','r','t','2'),
};



static void
hb_ot_shape_collect_features (hb_ot_shape_planner_t          *planner,
			      const hb_segment_properties_t  *props,
			      const hb_feature_t             *user_features,
			      unsigned int                    num_user_features)
{
  hb_ot_map_builder_t *map = &planner->map;

  switch (props->direction) {
    case HB_DIRECTION_LTR:
      map->add_global_bool_feature (HB_TAG ('l','t','r','a'));
      map->add_global_bool_feature (HB_TAG ('l','t','r','m'));
      break;
    case HB_DIRECTION_RTL:
      map->add_global_bool_feature (HB_TAG ('r','t','l','a'));
      map->add_feature (HB_TAG ('r','t','l','m'), 1, F_NONE);
      break;
    case HB_DIRECTION_TTB:
    case HB_DIRECTION_BTT:
    case HB_DIRECTION_INVALID:
    default:
      break;
  }

  if (planner->shaper->collect_features)
    planner->shaper->collect_features (planner);

  for (unsigned int i = 0; i < ARRAY_LENGTH (common_features); i++)
    map->add_global_bool_feature (common_features[i]);

  if (HB_DIRECTION_IS_HORIZONTAL (props->direction))
    for (unsigned int i = 0; i < ARRAY_LENGTH (horizontal_features); i++)
      map->add_feature (horizontal_features[i], 1, F_GLOBAL |
			(horizontal_features[i] == HB_TAG('k','e','r','n') ?
			 F_HAS_FALLBACK : F_NONE));
  else
    for (unsigned int i = 0; i < ARRAY_LENGTH (vertical_features); i++)
      map->add_feature (vertical_features[i], 1, F_GLOBAL |
			(vertical_features[i] == HB_TAG('v','k','r','n') ?
			 F_HAS_FALLBACK : F_NONE));

  if (planner->shaper->override_features)
    planner->shaper->override_features (planner);

  for (unsigned int i = 0; i < num_user_features; i++) {
    const hb_feature_t *feature = &user_features[i];
    map->add_feature (feature->tag, feature->value,
		      (feature->start == 0 && feature->end == (unsigned int) -1) ?
		       F_GLOBAL : F_NONE);
  }
}


/*
 * shaper face data
 */

hb_ot_shaper_face_data_t *
_hb_ot_shaper_face_data_create (hb_face_t *face)
{
  return _hb_ot_layout_create (face);
}

void
_hb_ot_shaper_face_data_destroy (hb_ot_shaper_face_data_t *data)
{
  _hb_ot_layout_destroy (data);
}


/*
 * shaper font data
 */

struct hb_ot_shaper_font_data_t {};

hb_ot_shaper_font_data_t *
_hb_ot_shaper_font_data_create (hb_font_t *font)
{
  return (hb_ot_shaper_font_data_t *) HB_SHAPER_DATA_SUCCEEDED;
}

void
_hb_ot_shaper_font_data_destroy (hb_ot_shaper_font_data_t *data)
{
}


/*
 * shaper shape_plan data
 */

hb_ot_shaper_shape_plan_data_t *
_hb_ot_shaper_shape_plan_data_create (hb_shape_plan_t    *shape_plan,
				      const hb_feature_t *user_features,
				      unsigned int        num_user_features)
{
  hb_ot_shape_plan_t *plan = (hb_ot_shape_plan_t *) calloc (1, sizeof (hb_ot_shape_plan_t));
  if (unlikely (!plan))
    return NULL;

  hb_ot_shape_planner_t planner (shape_plan);

  planner.shaper = hb_ot_shape_complex_categorize (&planner);

  hb_ot_shape_collect_features (&planner, &shape_plan->props, user_features, num_user_features);

  planner.compile (*plan);

  if (plan->shaper->data_create) {
    plan->data = plan->shaper->data_create (plan);
    if (unlikely (!plan->data))
      return NULL;
  }

  return plan;
}

void
_hb_ot_shaper_shape_plan_data_destroy (hb_ot_shaper_shape_plan_data_t *plan)
{
  if (plan->shaper->data_destroy)
    plan->shaper->data_destroy (const_cast<void *> (plan->data));

  plan->finish ();

  free (plan);
}


/*
 * shaper
 */

struct hb_ot_shape_context_t
{
  hb_ot_shape_plan_t *plan;
  hb_font_t *font;
  hb_face_t *face;
  hb_buffer_t  *buffer;
  const hb_feature_t *user_features;
  unsigned int        num_user_features;

  /* Transient stuff */
  hb_direction_t target_direction;
};



/* Main shaper */


/* Prepare */

static void
hb_set_unicode_props (hb_buffer_t *buffer)
{
  unsigned int count = buffer->len;
  for (unsigned int i = 0; i < count; i++)
    _hb_glyph_info_set_unicode_props (&buffer->info[i], buffer->unicode);
}

static void
hb_insert_dotted_circle (hb_buffer_t *buffer, hb_font_t *font)
{
  if (!(buffer->flags & HB_BUFFER_FLAG_BOT) ||
      _hb_glyph_info_get_general_category (&buffer->info[0]) !=
      HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK)
    return;

  hb_codepoint_t dottedcircle_glyph;
  if (!font->get_glyph (0x25CC, 0, &dottedcircle_glyph))
    return;

  hb_glyph_info_t dottedcircle;
  dottedcircle.codepoint = 0x25CC;
  _hb_glyph_info_set_unicode_props (&dottedcircle, buffer->unicode);

  buffer->clear_output ();

  buffer->idx = 0;
  hb_glyph_info_t info = dottedcircle;
  info.cluster = buffer->cur().cluster;
  info.mask = buffer->cur().mask;
  buffer->output_info (info);
  while (buffer->idx < buffer->len)
    buffer->next_glyph ();

  buffer->swap_buffers ();
}

static void
hb_form_clusters (hb_buffer_t *buffer)
{
  unsigned int count = buffer->len;
  for (unsigned int i = 1; i < count; i++)
    if (HB_UNICODE_GENERAL_CATEGORY_IS_MARK (_hb_glyph_info_get_general_category (&buffer->info[i])))
      buffer->merge_clusters (i - 1, i + 1);
}

static void
hb_ensure_native_direction (hb_buffer_t *buffer)
{
  hb_direction_t direction = buffer->props.direction;

  /* TODO vertical:
   * The only BTT vertical script is Ogham, but it's not clear to me whether OpenType
   * Ogham fonts are supposed to be implemented BTT or not.  Need to research that
   * first. */
  if ((HB_DIRECTION_IS_HORIZONTAL (direction) && direction != hb_script_get_horizontal_direction (buffer->props.script)) ||
      (HB_DIRECTION_IS_VERTICAL   (direction) && direction != HB_DIRECTION_TTB))
  {
    hb_buffer_reverse_clusters (buffer);
    buffer->props.direction = HB_DIRECTION_REVERSE (buffer->props.direction);
  }
}


/* Substitute */

static inline void
hb_ot_mirror_chars (hb_ot_shape_context_t *c)
{
  if (HB_DIRECTION_IS_FORWARD (c->target_direction))
    return;

  hb_unicode_funcs_t *unicode = c->buffer->unicode;
  hb_mask_t rtlm_mask = c->plan->map.get_1_mask (HB_TAG ('r','t','l','m'));

  unsigned int count = c->buffer->len;
  for (unsigned int i = 0; i < count; i++) {
    hb_codepoint_t codepoint = unicode->mirroring (c->buffer->info[i].codepoint);
    if (likely (codepoint == c->buffer->info[i].codepoint))
      c->buffer->info[i].mask |= rtlm_mask;
    else
      c->buffer->info[i].codepoint = codepoint;
  }
}

static inline void
hb_ot_shape_setup_masks (hb_ot_shape_context_t *c)
{
  hb_ot_map_t *map = &c->plan->map;

  hb_mask_t global_mask = map->get_global_mask ();
  c->buffer->reset_masks (global_mask);

  if (c->plan->shaper->setup_masks)
    c->plan->shaper->setup_masks (c->plan, c->buffer, c->font);

  for (unsigned int i = 0; i < c->num_user_features; i++)
  {
    const hb_feature_t *feature = &c->user_features[i];
    if (!(feature->start == 0 && feature->end == (unsigned int)-1)) {
      unsigned int shift;
      hb_mask_t mask = map->get_mask (feature->tag, &shift);
      c->buffer->set_masks (feature->value << shift, mask, feature->start, feature->end);
    }
  }
}

static inline void
hb_ot_map_glyphs_fast (hb_buffer_t  *buffer)
{
  /* Normalization process sets up glyph_index(), we just copy it. */
  unsigned int count = buffer->len;
  for (unsigned int i = 0; i < count; i++)
    buffer->info[i].codepoint = buffer->info[i].glyph_index();
}

static inline void
hb_synthesize_glyph_classes (hb_ot_shape_context_t *c)
{
  unsigned int count = c->buffer->len;
  for (unsigned int i = 0; i < count; i++)
    c->buffer->info[i].glyph_props() = _hb_glyph_info_get_general_category (&c->buffer->info[i]) == HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK ?
				       HB_OT_LAYOUT_GLYPH_PROPS_MARK :
				       HB_OT_LAYOUT_GLYPH_PROPS_BASE_GLYPH;
}

static inline void
hb_ot_substitute_default (hb_ot_shape_context_t *c)
{
  if (c->plan->shaper->preprocess_text)
    c->plan->shaper->preprocess_text (c->plan, c->buffer, c->font);

  hb_ot_mirror_chars (c);

  HB_BUFFER_ALLOCATE_VAR (c->buffer, glyph_index);

  _hb_ot_shape_normalize (c->plan, c->buffer, c->font);

  hb_ot_shape_setup_masks (c);

  /* This is unfortunate to go here, but necessary... */
  if (!hb_ot_layout_has_positioning (c->face))
    _hb_ot_shape_fallback_position_recategorize_marks (c->plan, c->font, c->buffer);

  hb_ot_map_glyphs_fast (c->buffer);

  HB_BUFFER_DEALLOCATE_VAR (c->buffer, glyph_index);
}

static inline void
hb_ot_substitute_complex (hb_ot_shape_context_t *c)
{
  hb_ot_layout_substitute_start (c->font, c->buffer);

  if (!hb_ot_layout_has_glyph_classes (c->face))
    hb_synthesize_glyph_classes (c);

  c->plan->substitute (c->font, c->buffer);

  hb_ot_layout_substitute_finish (c->font, c->buffer);

  return;
}

static inline void
hb_ot_substitute (hb_ot_shape_context_t *c)
{
  hb_ot_substitute_default (c);
  hb_ot_substitute_complex (c);
}

/* Position */

static inline void
hb_ot_position_default (hb_ot_shape_context_t *c)
{
  hb_ot_layout_position_start (c->font, c->buffer);

  unsigned int count = c->buffer->len;
  for (unsigned int i = 0; i < count; i++)
  {
    c->font->get_glyph_advance_for_direction (c->buffer->info[i].codepoint,
					      c->buffer->props.direction,
					      &c->buffer->pos[i].x_advance,
					      &c->buffer->pos[i].y_advance);
    c->font->subtract_glyph_origin_for_direction (c->buffer->info[i].codepoint,
						  c->buffer->props.direction,
						  &c->buffer->pos[i].x_offset,
						  &c->buffer->pos[i].y_offset);

  }

  /* Zero'ing mark widths by GDEF (as used in Myanmar spec) happens
   * *before* GPOS. */
  switch (c->plan->shaper->zero_width_marks)
  {
    case HB_OT_SHAPE_ZERO_WIDTH_MARKS_BY_GDEF:
      for (unsigned int i = 0; i < count; i++)
	if ((c->buffer->info[i].glyph_props() & HB_OT_LAYOUT_GLYPH_PROPS_MARK))
	{
	  c->buffer->pos[i].x_advance = 0;
	  c->buffer->pos[i].y_advance = 0;
	}
      break;

    default:
    case HB_OT_SHAPE_ZERO_WIDTH_MARKS_NONE:
    case HB_OT_SHAPE_ZERO_WIDTH_MARKS_BY_UNICODE:
      break;
  }
}

static inline bool
hb_ot_position_complex (hb_ot_shape_context_t *c)
{
  bool ret = false;
  unsigned int count = c->buffer->len;

  if (hb_ot_layout_has_positioning (c->face))
  {
    /* Change glyph origin to what GPOS expects, apply GPOS, change it back. */

    for (unsigned int i = 0; i < count; i++) {
      c->font->add_glyph_origin_for_direction (c->buffer->info[i].codepoint,
					       HB_DIRECTION_LTR,
					       &c->buffer->pos[i].x_offset,
					       &c->buffer->pos[i].y_offset);
    }

    c->plan->position (c->font, c->buffer);

    for (unsigned int i = 0; i < count; i++) {
      c->font->subtract_glyph_origin_for_direction (c->buffer->info[i].codepoint,
						    HB_DIRECTION_LTR,
						    &c->buffer->pos[i].x_offset,
						    &c->buffer->pos[i].y_offset);
    }

    ret = true;
  }

  /* Zero'ing mark widths by Unicode happens
   * *after* GPOS. */
  switch (c->plan->shaper->zero_width_marks)
  {
    case HB_OT_SHAPE_ZERO_WIDTH_MARKS_BY_UNICODE:
      for (unsigned int i = 0; i < count; i++)
	if (_hb_glyph_info_get_general_category (&c->buffer->info[i]) == HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK)
	{
	  c->buffer->pos[i].x_advance = 0;
	  c->buffer->pos[i].y_advance = 0;
	}
      break;

    default:
    case HB_OT_SHAPE_ZERO_WIDTH_MARKS_NONE:
    case HB_OT_SHAPE_ZERO_WIDTH_MARKS_BY_GDEF:
      break;
  }

  hb_ot_layout_position_finish (c->font, c->buffer);

  return ret;
}

static inline void
hb_ot_truetype_kern (hb_ot_shape_context_t *c)
{
  unsigned int count = c->buffer->len;
  hb_mask_t kern_mask = c->plan->map.get_1_mask (HB_DIRECTION_IS_HORIZONTAL (c->buffer->props.direction) ?
						 HB_TAG ('k','e','r','n') : HB_TAG ('v','k','r','n'));

  if (unlikely (!count)) return;

  bool enabled = c->buffer->info[0].mask & kern_mask;
  for (unsigned int i = 1; i < count; i++)
  {
    bool next = c->buffer->info[i].mask & kern_mask;
    if (enabled && next)
    {
      hb_position_t x_kern, y_kern, kern1, kern2;
      c->font->get_glyph_kerning_for_direction (c->buffer->info[i - 1].codepoint, c->buffer->info[i].codepoint,
						c->buffer->props.direction,
						&x_kern, &y_kern);

      kern1 = x_kern >> 1;
      kern2 = x_kern - kern1;
      c->buffer->pos[i - 1].x_advance += kern1;
      c->buffer->pos[i].x_advance += kern2;
      c->buffer->pos[i].x_offset += kern2;

      kern1 = y_kern >> 1;
      kern2 = y_kern - kern1;
      c->buffer->pos[i - 1].y_advance += kern1;
      c->buffer->pos[i].y_advance += kern2;
      c->buffer->pos[i].y_offset += kern2;
    }
    enabled = next;
  }
}

static inline void
hb_ot_position (hb_ot_shape_context_t *c)
{
  hb_ot_position_default (c);

  hb_bool_t fallback = !hb_ot_position_complex (c);

  if (fallback && c->plan->shaper->fallback_position)
    _hb_ot_shape_fallback_position (c->plan, c->font, c->buffer);

  if (HB_DIRECTION_IS_BACKWARD (c->buffer->props.direction))
    hb_buffer_reverse (c->buffer);

  /* Visual fallback goes here. */

  if (fallback)
    hb_ot_truetype_kern (c);
}


/* Post-process */

static void
hb_ot_hide_default_ignorables (hb_ot_shape_context_t *c)
{
  if (c->buffer->flags & HB_BUFFER_FLAG_PRESERVE_DEFAULT_IGNORABLES)
    return;

  hb_codepoint_t space = 0;

  unsigned int count = c->buffer->len;
  for (unsigned int i = 0; i < count; i++)
    if (unlikely (!is_a_ligature (c->buffer->info[i]) &&
		  _hb_glyph_info_is_default_ignorable (&c->buffer->info[i])))
    {
      if (!space) {
        /* We assume that the space glyph is not gid0. */
        if (unlikely (!c->font->get_glyph (' ', 0, &space)) || !space)
	return; /* No point! */
      }
      c->buffer->info[i].codepoint = space;
      c->buffer->pos[i].x_advance = 0;
      c->buffer->pos[i].y_advance = 0;
    }
}


/* Pull it all together! */

static void
hb_ot_shape_internal (hb_ot_shape_context_t *c)
{
  c->buffer->deallocate_var_all ();

  /* Save the original direction, we use it later. */
  c->target_direction = c->buffer->props.direction;

  HB_BUFFER_ALLOCATE_VAR (c->buffer, unicode_props0);
  HB_BUFFER_ALLOCATE_VAR (c->buffer, unicode_props1);

  c->buffer->clear_output ();

  hb_set_unicode_props (c->buffer);
  hb_insert_dotted_circle (c->buffer, c->font);
  hb_form_clusters (c->buffer);

  hb_ensure_native_direction (c->buffer);

  hb_ot_substitute (c);
  hb_ot_position (c);

  hb_ot_hide_default_ignorables (c);

  HB_BUFFER_DEALLOCATE_VAR (c->buffer, unicode_props1);
  HB_BUFFER_DEALLOCATE_VAR (c->buffer, unicode_props0);

  c->buffer->props.direction = c->target_direction;

  c->buffer->deallocate_var_all ();
}


hb_bool_t
_hb_ot_shape (hb_shape_plan_t    *shape_plan,
	      hb_font_t          *font,
	      hb_buffer_t        *buffer,
	      const hb_feature_t *features,
	      unsigned int        num_features)
{
  hb_ot_shape_context_t c = {HB_SHAPER_DATA_GET (shape_plan), font, font->face, buffer, features, num_features};
  hb_ot_shape_internal (&c);

  return true;
}


void
hb_ot_shape_plan_collect_lookups (hb_shape_plan_t *shape_plan,
				  hb_tag_t         table_tag,
				  hb_set_t        *lookup_indexes /* OUT */)
{
  /* XXX Does the first part always succeed? */
  HB_SHAPER_DATA_GET (shape_plan)->collect_lookups (table_tag, lookup_indexes);
}


/* TODO Move this to hb-ot-shape-normalize, make it do decompose, and make it public. */
static void
add_char (hb_font_t          *font,
	  hb_unicode_funcs_t *unicode,
	  hb_bool_t           mirror,
	  hb_codepoint_t      u,
	  hb_set_t           *glyphs)
{
  hb_codepoint_t glyph;
  if (font->get_glyph (u, 0, &glyph))
    glyphs->add (glyph);
  if (mirror)
  {
    hb_codepoint_t m = unicode->mirroring (u);
    if (m != u && font->get_glyph (m, 0, &glyph))
      glyphs->add (glyph);
  }
}


void
hb_ot_shape_glyphs_closure (hb_font_t          *font,
			    hb_buffer_t        *buffer,
			    const hb_feature_t *features,
			    unsigned int        num_features,
			    hb_set_t           *glyphs)
{
  hb_ot_shape_plan_t plan;

  const char *shapers[] = {"ot", NULL};
  hb_shape_plan_t *shape_plan = hb_shape_plan_create_cached (font->face, &buffer->props,
							     features, num_features, shapers);

  bool mirror = hb_script_get_horizontal_direction (buffer->props.script) == HB_DIRECTION_RTL;

  unsigned int count = buffer->len;
  for (unsigned int i = 0; i < count; i++)
    add_char (font, buffer->unicode, mirror, buffer->info[i].codepoint, glyphs);

  hb_set_t lookups;
  lookups.init ();
  hb_ot_shape_plan_collect_lookups (shape_plan, HB_OT_TAG_GSUB, &lookups);

  /* And find transitive closure. */
  hb_set_t copy;
  copy.init ();
  do {
    copy.set (glyphs);
    for (hb_codepoint_t lookup_index = -1; hb_set_next (&lookups, &lookup_index);)
      hb_ot_layout_lookup_substitute_closure (font->face, lookup_index, glyphs);
  } while (!copy.is_equal (glyphs));

  hb_shape_plan_destroy (shape_plan);
}
