/*
 * Copyright © 2018  Ebrahim Byagowi
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
 */

#include "hb.hh"

#ifndef HB_NO_METRICS

#include "hb-ot-var-mvar-table.hh"
#include "hb-ot-gasp-table.hh" // Just so we compile it; unused otherwise.
#include "hb-ot-os2-table.hh"
#include "hb-ot-post-table.hh"
#include "hb-ot-hhea-table.hh"
#include "hb-ot-metrics.hh"
#include "hb-ot-face.hh"

#if 0
static bool
_get_gasp (hb_face_t *face, float *result, hb_ot_metrics_t metrics_tag)
{
  const OT::GaspRange& range = face->table.gasp->get_gasp_range (metrics_tag - HB_TAG ('g','s','p','0'));
  if (&range == &Null (OT::GaspRange)) return false;
  if (result) *result = range.rangeMaxPPEM + face->table.MVAR->get_var (metrics_tag, nullptr, 0);
  return true;
}
#endif

/**
 * hb_ot_metrics_get_position:
 * @font: a #hb_font_t object.
 * @metrics_tag: tag of metrics value you like to fetch.
 * @position: (out) (optional): result of metrics value from the font.
 *
 * It fetches metrics value corresponding to a given tag from a font.
 *
 * Returns: Whether found the requested metrics in the font.
 * Since: REPLACEME
 **/
hb_bool_t
hb_ot_metrics_get_position (hb_font_t       *font,
			    hb_ot_metrics_t  metrics_tag,
			    hb_position_t   *position     /* OUT.  May be NULL. */)
{
  hb_face_t *face = font->face;
  switch (metrics_tag)
  {
#ifndef HB_NO_VAR
#define GET_VAR hb_ot_metrics_get_variation (face, metrics_tag)
#else
#define GET_VAR 0
#endif
#define GET_METRIC_X(TABLE, ATTR) \
  (face->table.TABLE->has_data () && \
    (position && (*position = font->em_scalef_x (face->table.TABLE->ATTR + GET_VAR)), true))
#define GET_METRIC_Y(TABLE, ATTR) \
  (face->table.TABLE->has_data () && \
    (position && (*position = font->em_scalef_y (face->table.TABLE->ATTR + GET_VAR)), true))
  case HB_OT_METRICS_HORIZONTAL_ASCENDER:
    return (!face->table.OS2->use_typo_metrics () && GET_METRIC_Y (hhea, ascender)) ||
	   GET_METRIC_Y (OS2, sTypoAscender);
  case HB_OT_METRICS_HORIZONTAL_DESCENDER:
    return (!face->table.OS2->use_typo_metrics () && GET_METRIC_Y (hhea, descender)) ||
	   GET_METRIC_Y (OS2, sTypoDescender);
  case HB_OT_METRICS_HORIZONTAL_LINE_GAP:
    return (!face->table.OS2->use_typo_metrics () && GET_METRIC_Y (hhea, lineGap)) ||
	   GET_METRIC_Y (OS2, sTypoLineGap);
  case HB_OT_METRICS_VERTICAL_ASCENDER:           return GET_METRIC_X (vhea, ascender);
  case HB_OT_METRICS_VERTICAL_DESCENDER:          return GET_METRIC_X (vhea, descender);
  case HB_OT_METRICS_VERTICAL_LINE_GAP:           return GET_METRIC_X (vhea, lineGap);
  case HB_OT_METRICS_HORIZONTAL_CLIPPING_ASCENT:  return GET_METRIC_Y (OS2, usWinAscent);
  case HB_OT_METRICS_HORIZONTAL_CLIPPING_DESCENT: return GET_METRIC_Y (OS2, usWinDescent);
  case HB_OT_METRICS_HORIZONTAL_CARET_RISE:       return GET_METRIC_Y (hhea, caretSlopeRise);
  case HB_OT_METRICS_HORIZONTAL_CARET_RUN:        return GET_METRIC_X (hhea, caretSlopeRun);
  case HB_OT_METRICS_HORIZONTAL_CARET_OFFSET:     return GET_METRIC_X (hhea, caretOffset);
  case HB_OT_METRICS_VERTICAL_CARET_RISE:         return GET_METRIC_X (vhea, caretSlopeRise);
  case HB_OT_METRICS_VERTICAL_CARET_RUN:          return GET_METRIC_Y (vhea, caretSlopeRun);
  case HB_OT_METRICS_VERTICAL_CARET_OFFSET:       return GET_METRIC_Y (vhea, caretOffset);
  case HB_OT_METRICS_X_HEIGHT:                    return GET_METRIC_Y (OS2->v2 (), sxHeight);
  case HB_OT_METRICS_CAP_HEIGHT:                  return GET_METRIC_Y (OS2->v2 (), sCapHeight);
  case HB_OT_METRICS_SUBSCRIPT_EM_X_SIZE:         return GET_METRIC_X (OS2, ySubscriptXSize);
  case HB_OT_METRICS_SUBSCRIPT_EM_Y_SIZE:         return GET_METRIC_Y (OS2, ySubscriptYSize);
  case HB_OT_METRICS_SUBSCRIPT_EM_X_OFFSET:       return GET_METRIC_X (OS2, ySubscriptXOffset);
  case HB_OT_METRICS_SUBSCRIPT_EM_Y_OFFSET:       return GET_METRIC_Y (OS2, ySubscriptYOffset);
  case HB_OT_METRICS_SUPERSCRIPT_EM_X_SIZE:       return GET_METRIC_X (OS2, ySuperscriptXSize);
  case HB_OT_METRICS_SUPERSCRIPT_EM_Y_SIZE:       return GET_METRIC_Y (OS2, ySuperscriptYSize);
  case HB_OT_METRICS_SUPERSCRIPT_EM_X_OFFSET:     return GET_METRIC_X (OS2, ySuperscriptXOffset);
  case HB_OT_METRICS_SUPERSCRIPT_EM_Y_OFFSET:     return GET_METRIC_Y (OS2, ySuperscriptYOffset);
  case HB_OT_METRICS_STRIKEOUT_SIZE:              return GET_METRIC_Y (OS2, yStrikeoutSize);
  case HB_OT_METRICS_STRIKEOUT_OFFSET:            return GET_METRIC_Y (OS2, yStrikeoutPosition);
  case HB_OT_METRICS_UNDERLINE_SIZE:              return GET_METRIC_Y (post->table, underlineThickness);
  case HB_OT_METRICS_UNDERLINE_OFFSET:            return GET_METRIC_Y (post->table, underlinePosition);
#undef GET_METRIC_Y
#undef GET_METRIC_X
#undef GET_VAR
  default:                                        return false;
  }
}

#ifndef HB_NO_VAR
/**
 * hb_ot_metrics_get_variation:
 * @face:
 * @metrics_tag:
 *
 * Returns:
 *
 * Since: REPLACEME
 **/
float
hb_ot_metrics_get_variation (hb_face_t *face, hb_ot_metrics_t metrics_tag)
{
  return face->table.MVAR->get_var (metrics_tag, nullptr, 0);
}

/**
 * hb_ot_metrics_get_x_variation:
 * @font:
 * @metrics_tag:
 *
 * Returns:
 *
 * Since: REPLACEME
 **/
hb_position_t
hb_ot_metrics_get_x_variation (hb_font_t *font, hb_ot_metrics_t metrics_tag)
{
  return font->em_scalef_x (hb_ot_metrics_get_variation (font->face, metrics_tag));
}

/**
 * hb_ot_metrics_get_y_variation:
 * @font:
 * @metrics_tag:
 *
 * Returns:
 *
 * Since: REPLACEME
 **/
hb_position_t
hb_ot_metrics_get_y_variation (hb_font_t *font, hb_ot_metrics_t metrics_tag)
{
  return font->em_scalef_y (hb_ot_metrics_get_variation (font->face, metrics_tag));
}
#endif

#endif
