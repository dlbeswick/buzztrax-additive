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

#include "src/lfo.h"
#include "src/genums.h"
#include "src/generated/generated-genums.h"
#include "src/math.h"
#include "src/properties_simple.h"
#include "src/debug.h"

#include <gst/gstparamspecs.h>

struct _GstBtLfoFloatClass {
  GstControlSourceClass parent;
};
  
struct _GstBtLfoFloat {
  GstBtPropSrateControlSource parent;

  gfloat amplitude;
  gfloat frequency;
  gfloat shape;
  gfloat filter;
  gfloat offset;
  gfloat phase;
  guint waveform;

  gfloat accum;
  gfloat integrate;
  
  BtPropertiesSimple* props;
};

G_DEFINE_TYPE(GstBtLfoFloat, gstbt_lfo_float, gstbt_prop_srate_cs_get_type());

static gfloat timestamp_to_accum(const GstBtLfoFloat* self, GstClockTime time) {
  if (self->frequency == 0) {
	return time;
  }
  
  const gfloat period_secs = 1.0f / self->frequency;
  const GstClockTime period_nsecs = period_secs * GST_SECOND;
  const GstClockTime phase_nsecs = period_nsecs * self->phase;
  const gfloat result = (gfloat)((time + phase_nsecs) % period_nsecs)/GST_SECOND/period_secs;
  
  switch (self->waveform) {
  case GSTBT_LFO_FLOAT_WAVEFORM_SINE: {
	return result * 2*G_PI;
  }
  default:
	return result;
  }
}

// Return the period of the waveform in units expected by the waveform.
// I.e. period of the sine waveform is expressed in radians.
static gfloat get_accum_period(const GstBtLfoFloat* self) {
  switch (self->waveform) {
  case GSTBT_LFO_FLOAT_WAVEFORM_SINE:
	return (gfloat)(2*G_PI);
  default:
	return 1.f;
  }
}

static gfloat get_shape(const GstBtLfoFloat* self) {
  switch (self->waveform) {
  case GSTBT_LFO_FLOAT_WAVEFORM_SINE:
	return self->shape * 10.f;
  default:
	return self->shape;
  }
}

// 0 <= accum < get_accum_period()
static inline v4sf get_sample(const GstBtLfoFloatWaveform waveform, const v4sf accum, const v4sf shape) {
  switch (waveform) {
  case GSTBT_LFO_FLOAT_WAVEFORM_SINE:
	return powsin4f(accum, shape);
  case GSTBT_LFO_FLOAT_WAVEFORM_SQUARE:
	return -1 + bitselect4f(accum < shape, -V4SF_UNIT, V4SF_UNIT) * 2;
  case GSTBT_LFO_FLOAT_WAVEFORM_SAW: {
	return bitselect4f(
      shape == 1.0f,
      V4SF_ZERO,
      bitselect4f(
        shape == 0.0f,
        V4SF_UNIT,
        max4f(-V4SF_UNIT, -1 + (1 - ((1-accum) * tan4f(FPI/2*shape))) * 2)
        )
      );
  }
  default:
	return V4SF_ZERO;
  }
}

gboolean mod_value_array_accum(GstBtLfoFloat* self, gfloat* accum, GstClockTime interval, guint n_values,
                               gfloat* values) {
  if (self->frequency == 0.0f)
	return FALSE;
  
  v4si any_nonzero = V4SI_ZERO;
  v4sf* const out = (v4sf*)values;
  const gfloat period = get_accum_period(self);
  const gfloat inc = (gfloat)interval/GST_SECOND * period * self->frequency;
  const v4sf t0 = {0, 1, 2, 3};
  v4sf accum4 = *accum + inc * t0;
  const v4sf inc4 = inc*4 * V4SF_UNIT;
  const v4sf shape4 = V4SF_UNIT * get_shape(self);

  const gfloat alpha = powf(self->filter, 8);
  
  for (guint i = 0; i < n_values/4; ++i) {
	const v4sf val = (self->offset + get_sample(self->waveform, accum4, shape4)) * self->amplitude;
    for (guint j = 0; j < 4; ++j) {
      self->integrate = alpha*val[j] + (1 - alpha)*self->integrate;
      out[i][j] *= self->integrate;
    }
	any_nonzero = (any_nonzero != 0) | (out[i] != 0);
	accum4 += inc4;
  }
  
  *accum = fmod(accum4[3], period);
  
  return !v4si_eq(any_nonzero, V4SI_ZERO);
}

gboolean gstbt_lfo_float_mod_value_array_accum(GstBtLfoFloat* self, GstClockTime interval, guint n_values,
                                               gfloat* values) {
  return mod_value_array_accum(self, &self->accum, interval, n_values, values);
}

static gboolean mod_value_array_f(GstBtPropSrateControlSource* super, GstClockTime timestamp, GstClockTime interval,
								   guint n_values, gfloat* values) {
  GstBtLfoFloat* self = (GstBtLfoFloat*)super;
  gfloat accum = timestamp_to_accum(self, timestamp);
  return mod_value_array_accum(self, &accum, interval, n_values, values);
}

static void get_value_array_f(GstBtPropSrateControlSource* super, GstClockTime timestamp, GstClockTime interval,
							   guint n_values, gfloat* values) {
  for (guint i = 0; i < n_values; ++i)
	values[i] = 1.0f;
  mod_value_array_f(super, timestamp, interval, n_values, values);
}

static void get_value_f(GstBtPropSrateControlSource* super, GstClockTime timestamp, gfloat* value) {
  get_value_array_f(super, timestamp, 0, 1, value);
}

gboolean gstbt_lfo_float_property_set(GObject* obj, guint prop_id, const GValue* value, GParamSpec* pspec) {
  return bt_properties_simple_set(((GstBtLfoFloat*)obj)->props, pspec, value);
}

gboolean gstbt_lfo_float_property_get(GObject* obj, guint prop_id, GValue* value, GParamSpec* pspec) {
  return bt_properties_simple_get(((GstBtLfoFloat*)obj)->props, pspec, value);
}

static void dispose(GObject* obj) {
  g_clear_object(&((GstBtLfoFloat*)obj)->props);
}

void gstbt_lfo_float_class_init(GstBtLfoFloatClass* const klass) {
  GObjectClass* const gobject_class = (GObjectClass*)klass;
  gobject_class->dispose = dispose;

  GstBtPropSrateControlSourceClass* const klass_cs = (GstBtPropSrateControlSourceClass*)klass;
  klass_cs->get_value_f = get_value_f;
  klass_cs->get_value_array_f = get_value_array_f;
  klass_cs->mod_value_array_f = mod_value_array_f;
}

void gstbt_lfo_float_props_add(GObjectClass* const gobject_class, guint* idx) {
  const GParamFlags flags =
	(GParamFlags)(G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE);
  
  g_object_class_install_property(
	gobject_class,
	(*idx)++,
	g_param_spec_float("lfo-amplitude", "LFO Amp", "LFO Amplitude", 0, 10, 0, flags)
	);

  g_object_class_install_property(
	gobject_class,
	(*idx)++,
	g_param_spec_float("lfo-frequency", "LFO Freq", "LFO Frequency", 0, 100, 0, flags)
	);

  g_object_class_install_property(
	gobject_class,
	(*idx)++,
	g_param_spec_float("lfo-shape", "LFO Shape", "LFO Shape", 0, 1, 1, flags)
	);
  
  g_object_class_install_property(
	gobject_class,
	(*idx)++,
	g_param_spec_float("lfo-filter", "LFO Filter", "LFO Filter", 0, 1, 1, flags)
	);
  
  g_object_class_install_property(
	gobject_class,
	(*idx)++,
	g_param_spec_float("lfo-offset", "LFO Offset", "LFO Offset", -1, 1, 0, flags)
	);

  g_object_class_install_property(
	gobject_class,
	(*idx)++,
	g_param_spec_float("lfo-phase", "LFO Phase", "LFO Phase", -1, 1, 0, flags)
	);

  g_object_class_install_property(
	gobject_class,
	(*idx)++,
	g_param_spec_enum("lfo-waveform", "LFO Wave", "LFO Waveform",
					  gst_bt_lfo_float_waveform_get_type(), GSTBT_LFO_FLOAT_WAVEFORM_SINE, flags)
	);
}

static void gstbt_lfo_float_init(GstBtLfoFloat* const self) {
/*  self->parent.get_value = get_value;
	self->parent.get_value_array = get_value_array;*/
}

static void prop_add(GstBtLfoFloat* const self, const char* name, void* const var) {
  bt_properties_simple_add(self->props, name, var);
}

GstBtLfoFloat* gstbt_lfo_float_new(GObject* const owner) {
  GstBtLfoFloat* result = g_object_new(gstbt_lfo_float_get_type(), NULL);
  result->props = bt_properties_simple_new(owner);
  prop_add(result, "lfo-amplitude", &result->amplitude);
  prop_add(result, "lfo-frequency", &result->frequency);
  prop_add(result, "lfo-shape", &result->shape);
  prop_add(result, "lfo-filter", &result->filter);
  prop_add(result, "lfo-offset", &result->offset);
  prop_add(result, "lfo-phase", &result->phase);
  prop_add(result, "lfo-waveform", &result->waveform);
  return result;
}
