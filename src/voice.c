/*
  Additive synth for Buzztrax
  Copyright (C) 2020 David Beswick

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
  
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "src/voice.h"
#include "src/debug.h"
#include "src/adsr.h"
#include "src/lfo.h"
#include "src/genums.h"
#include "src/math.h"
#include "src/generated/generated-genums.h"

#include "libbuzztrax-gst/propertymeta.h"
#include <gst/controller/gstlfocontrolsource.h>
#include <unistd.h>
#include <stdio.h>

struct _GstBtAdditiveV
{
  GstObject parent;

  guint idx_target_prop;
  
  GstBtAdsr* adsr;
  GstBtLfoFloat* lfo;
  GParamSpec** parent_props;
  guint n_parent_props;
  guint srate_buf_size;
  v4sf* srate_buf;
  GstClockTime timestamp_last;
};

enum
{
  PROP_IDX_TARGET_PROP = 1,
  N_PROPERTIES
};

static GParamSpec* properties[N_PROPERTIES] = { NULL, };

G_DEFINE_TYPE (GstBtAdditiveV, gstbt_additivev, GST_TYPE_OBJECT)

static void property_set(GObject* const object, const guint prop_id, const GValue* const value,
						 GParamSpec* const pspec) {
  GstBtAdditiveV *self = GSTBT_ADDITIVEV(object);

  switch (prop_id) {
  case PROP_IDX_TARGET_PROP:
	self->idx_target_prop = MIN(g_value_get_enum(value), self->n_parent_props);
	break;
  default:
	if (!gstbt_adsr_property_set((GObject*)self->adsr, prop_id, value, pspec) &&
		!gstbt_lfo_float_property_set((GObject*)self->lfo, prop_id, value, pspec)) {
	  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
  }
}

static void property_get(GObject* const object, const guint prop_id, GValue* const value, GParamSpec* const pspec)
{
  GstBtAdditiveV *self = GSTBT_ADDITIVEV (object);

  switch (prop_id) {
  case PROP_IDX_TARGET_PROP:
	g_value_set_enum(value, self->idx_target_prop);
	break;
  default:
	if (!gstbt_adsr_property_get((GObject*)self->adsr, prop_id, value, pspec) &&
		!gstbt_lfo_float_property_get((GObject*)self->lfo, prop_id, value, pspec)) {
	  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
  }
}

void gstbt_additivev_process(GstBtAdditiveV* const self, GstBuffer* const gstbuf) {
  // Necessary to update parameters from pattern.
  //
  // The parent machine is responsible for delgating process to any children it has; the pattern control group
  // won't have called it for each voice. Although maybe it should?
  gst_object_sync_values((GstObject*)self, GST_BUFFER_PTS(gstbuf));
}

void gstbt_additivev_note_off(GstBtAdditiveV* self, GstClockTime time) {
  gstbt_adsr_off(self->adsr, time);
}

void gstbt_additivev_note_on(GstBtAdditiveV* self, GstClockTime time) {
  gstbt_adsr_trigger(self->adsr, time);
}

void gstbt_additivev_mod_value_array_f_for_prop(
  GstBtAdditiveV* self,
  GstClockTime timestamp,
  GstClockTime interval,
  guint n_values,
  gfloat* values,
  gboolean* props_active,
  gboolean* props_controlled,
  GstBtAdditiveV** voices) {

  guint idx = self->idx_target_prop - 1;
  if (idx < self->n_parent_props) {
    gstbt_additivev_mod_value_array_f_for_prop_idx(self, timestamp, interval, n_values, values, props_active,
                                                   props_controlled, voices, idx);
  }
}

void gstbt_additivev_mod_value_array_f_for_prop_idx(
  GstBtAdditiveV* self,
  GstClockTime timestamp,
  GstClockTime interval,
  guint n_values,
  gfloat* values,
  gboolean* props_active,
  gboolean* props_controlled,
  GstBtAdditiveV** voices,
  guint idx) {

  g_assert(self->srate_buf_size);
  
  // This function might be called multiple times in a frame as a voice could be targeting both a synth param and one
  // or more voice params. If the same timestamp is used, then the same result is modulated with "values".
  if (timestamp != self->timestamp_last) {
    self->timestamp_last = timestamp;
    
    gboolean any_nonzero = gstbt_prop_srate_cs_get_value_array_f(
      (GstBtPropSrateControlSource*)self->adsr, timestamp, interval, n_values, (gfloat*)self->srate_buf);
    
    any_nonzero =
      gstbt_lfo_float_mod_value_array_accum(self->lfo, timestamp, interval, (gfloat*)self->srate_buf, n_values, voices)
	  || any_nonzero;

    props_active[idx] = props_active[idx] || any_nonzero;
    props_controlled[idx] = TRUE;
  }

  v4sf* outbuf = (v4sf*)values + self->srate_buf_size/4 * idx;
  if (props_active[idx]) {
    for (guint i = 0; i < self->srate_buf_size/4; ++i)
      outbuf[i] *= self->srate_buf[i];
  } else {
    for (guint i = 0; i < self->srate_buf_size/4; ++i)
      outbuf[i] = V4SF_ZERO;
  }
}

void gstbt_additivev_on_buf_size_change(GstBtAdditiveV* const self, guint n_samples) {
  self->srate_buf_size = n_samples;
  self->srate_buf = g_realloc(self->srate_buf, sizeof(gfloat)*self->srate_buf_size);
  gstbt_lfo_float_on_buf_size_change(self->lfo, n_samples);
}

static void gstbt_additivev_init(GstBtAdditiveV* const self) {
  self->adsr = gstbt_adsr_new((GObject*)self, "");
  self->timestamp_last = -1;
}

static void dispose(GObject* const gobj) {
  GstBtAdditiveV* const self = (GstBtAdditiveV*)gobj;
  g_clear_object(&self->adsr);
  g_clear_object(&self->lfo);
  g_clear_object(&self->srate_buf);
}

static void gstbt_additivev_class_init(GstBtAdditiveVClass* const klass) {
  GObjectClass* const gobject_class = G_OBJECT_CLASS(klass);
  gobject_class->set_property = property_set;
  gobject_class->get_property = property_get;
  gobject_class->dispose = dispose;

  const GParamFlags flags = (GParamFlags)
	(G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS);

  properties[PROP_IDX_TARGET_PROP] =
	g_param_spec_enum("idx-target-prop", "Target Prop.", "Target Parent Property",
					  additive_props_srate_get_type(), PROP_VOL, flags);

  g_object_class_install_properties(gobject_class, N_PROPERTIES, properties);

  guint idx = N_PROPERTIES;

  gstbt_lfo_float_props_add(gobject_class, &idx);
  gstbt_adsr_props_add(gobject_class, "", &idx);
}

GstBtAdditiveV* gstbt_additivev_new(GParamSpec** parent_props, guint n_parent_props, guint idx_voice) {
  GstBtAdditiveV* result = (GstBtAdditiveV*)g_object_new(gstbt_additivev_get_type(), NULL);
  result->parent_props = parent_props;
  result->n_parent_props = n_parent_props;
  result->lfo = gstbt_lfo_float_new((GObject*)result, idx_voice);
  return result;
}
