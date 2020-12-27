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
#include "src/additive.h"
#include "src/debug.h"
#include "src/genums.h"
#include "src/math.h"
#include "src/properties_simple.h"
#include "src/voice.h"
#include "src/generated/generated-genums.h"

#include <gst/gstparamspecs.h>

struct _GstBtLfoFloat {
  guint idx_voice_master;
  GstbtLfoFloatProp voice_master_prop;
  gfloat amplitude;
  gfloat frequency;
  gfloat shape;
  gfloat filter;
  gfloat offset;
  gfloat phase;
  GstBtLfoFloatWaveform waveform;

  gfloat accum;
  gfloat integrate;
  
  BtPropertiesSimple* props;
  GObject* owner;
  guint idx_voice;
  guint buf_srate_nsamples;
  v4sf* buf_srate_props;
  gboolean props_srate_nonzero[GSTBT_LFO_FLOAT_PROP_N];
  gboolean props_srate_controlled[GSTBT_LFO_FLOAT_PROP_N];
};

G_DEFINE_TYPE(GstBtLfoFloat, gstbt_lfo_float, G_TYPE_OBJECT);

static GParamSpec* properties[GSTBT_LFO_FLOAT_PROP_N] = { NULL, };

static v4sf get_shape(const v4sf waveform, const v4sf shape) {
  return bitselect4f(
    waveform == GSTBT_LFO_FLOAT_WAVEFORM_SINE,
	pow4f(shape+0.5f, 8 * V4SF_UNIT),
	shape
    );
}

// 0 <= accum < get_accum_period()
static inline v4sf get_sample(const v4sf waveform, const v4sf accum_unbounded, const v4sf shape) {
  // accum % 1.0 (period)
  v4sf accum = accum_unbounded - floor4f(accum_unbounded);
  
  return bitselect4f(
    waveform == GSTBT_LFO_FLOAT_WAVEFORM_SINE,
	powsin4f(accum * F2PI, shape),
    bitselect4f(
      waveform == GSTBT_LFO_FLOAT_WAVEFORM_SQUARE,
      bitselect4f(accum < shape, -V4SF_UNIT, V4SF_UNIT),
      bitselect4f(
        waveform == GSTBT_LFO_FLOAT_WAVEFORM_SAW,
        bitselect4f(
          shape == 1.0f,
          V4SF_ZERO,
          bitselect4f(
            shape == 0.0f,
            V4SF_UNIT,
            max4f(-V4SF_UNIT, -1 + (1 - ((1-accum) * tan4f(FPI/2*shape))) * 2)
            )
          ),
        V4SF_ZERO // default
        )
      )
    );
}

static void srate_props_fill(GstBtLfoFloat* const self, const GstClockTime timestamp, const GstClockTime interval,
                             guint n_values, GstBtAdditiveV** voices) {

  g_assert(self->buf_srate_nsamples);
  
  for (guint i = 0; i < GSTBT_LFO_FLOAT_PROP_N; ++i) {
    GValue src = G_VALUE_INIT;

    v4sf value;
    if (g_type_is_a(properties[i]->value_type, G_TYPE_ENUM)) {
      g_value_init(&src, G_TYPE_UINT);
      g_object_get_property((GObject*)self->owner, properties[i]->name, &src);
      value = (gfloat)g_value_get_uint(&src) * V4SF_UNIT;
    } else {
      g_value_init(&src, G_TYPE_FLOAT);
      g_object_get_property((GObject*)self->owner, properties[i]->name, &src);
      value = g_value_get_float(&src) * V4SF_UNIT;
    }
    
    g_value_unset(&src);

    v4sf* const sratebuf = &self->buf_srate_props[self->buf_srate_nsamples/4*i];
    for (guint j = 0; j < n_values/4; ++j) {
      sratebuf[j] = value;
    }
  }

  memset(self->props_srate_nonzero, 0, sizeof(self->props_srate_nonzero));
  memset(self->props_srate_controlled, 0, sizeof(self->props_srate_controlled));

  if (voices && self->idx_voice_master != -1 && self->voice_master_prop != GSTBT_LFO_FLOAT_PROP_NONE &&
      self->idx_voice_master != self->idx_voice) {
    
    gstbt_additivev_mod_value_array_f_for_prop_idx(
      voices[self->idx_voice_master],
      timestamp,
      interval,
      self->buf_srate_nsamples,
      (gfloat*)self->buf_srate_props,
      self->props_srate_nonzero,
      self->props_srate_controlled,
      voices,
      (guint)self->voice_master_prop
      );
  }
}

static gboolean mod_value_array_accum(GstBtLfoFloat* self, GstClockTime timestamp, GstClockTime interval,
                                      gfloat* values, guint n_values, GstBtAdditiveV** voices) {
  if (self->frequency == 0.0f)
	return FALSE;

  g_assert(n_values % 4 == 0);
  
  srate_props_fill(self, timestamp, interval, n_values, voices);
  
  const v4sf* const srate_amplitude = &self->buf_srate_props[self->buf_srate_nsamples/4*GSTBT_LFO_FLOAT_PROP_AMPLITUDE];
  const v4sf* const srate_frequency = &self->buf_srate_props[self->buf_srate_nsamples/4*GSTBT_LFO_FLOAT_PROP_FREQUENCY];
  const v4sf* const srate_shape = &self->buf_srate_props[self->buf_srate_nsamples/4*GSTBT_LFO_FLOAT_PROP_SHAPE];
  const v4sf* const srate_filter = &self->buf_srate_props[self->buf_srate_nsamples/4*GSTBT_LFO_FLOAT_PROP_FILTER];
  const v4sf* const srate_offset = &self->buf_srate_props[self->buf_srate_nsamples/4*GSTBT_LFO_FLOAT_PROP_OFFSET];
  const v4sf* const srate_phase = &self->buf_srate_props[self->buf_srate_nsamples/4*GSTBT_LFO_FLOAT_PROP_PHASE];
  const v4sf* const srate_waveform = &self->buf_srate_props[self->buf_srate_nsamples/4*GSTBT_LFO_FLOAT_PROP_WAVEFORM];
  
  v4si any_nonzero = V4SI_ZERO;
  v4sf* const out = (v4sf*)values;
  const v4sf period = V4SF_UNIT;

  v4sf accum4;
  accum4[0] = self->accum;

  const v4sf inc_base = (gfloat)interval/GST_SECOND * period;
  
  for (guint i = 0; i < n_values/4; ++i) {
    const v4sf inc = inc_base * srate_frequency[i];
    
    accum4[1] = accum4[0] + inc[0];
    accum4[2] = accum4[1] + inc[1];
    accum4[3] = accum4[2] + inc[2];
    
    const v4sf alpha = pow4f(srate_filter[i], 8 * V4SF_UNIT);
  
	const v4sf val =
      (srate_offset[i] +
       get_sample(srate_waveform[i],
                  accum4 + srate_phase[i],
                  get_shape(srate_waveform[i], srate_shape[i]))) *
      srate_amplitude[i];

    v4sf integrate = alpha*val;
    integrate[0] += (1 - alpha[0]) * self->integrate;
    integrate[1] += (1 - alpha[1]) * integrate[0];
    integrate[2] += (1 - alpha[2]) * integrate[1];
    integrate[3] += (1 - alpha[3]) * integrate[2];
    self->integrate = integrate[3];

    out[i] *= integrate;
    
	any_nonzero = (any_nonzero != 0) | (out[i] != 0);
    
	accum4[0] = accum4[3] + inc[3];
  }
  
  self->accum = fmod(accum4[0], period[0]);
  
  return !v4si_eq(any_nonzero, V4SI_ZERO);
}

gboolean gstbt_lfo_float_mod_value_array_accum(GstBtLfoFloat* self, GstClockTime timestamp, GstClockTime interval,
                                               gfloat* values, guint n_values, GstBtAdditiveV** voices) {
  return mod_value_array_accum(self, timestamp, interval, values, n_values, voices);
}

gboolean gstbt_lfo_float_property_set(GObject* obj, guint prop_id, const GValue* value, GParamSpec* pspec) {
  return bt_properties_simple_set(((GstBtLfoFloat*)obj)->props, pspec, value);
}

gboolean gstbt_lfo_float_property_get(GObject* obj, guint prop_id, GValue* value, GParamSpec* pspec) {
  return bt_properties_simple_get(((GstBtLfoFloat*)obj)->props, pspec, value);
}

static void prop_add(GstBtLfoFloat* const self, const char* name, void* const var) {
  bt_properties_simple_add(self->props, name, var);
}

static void dispose(GObject* obj) {
  GstBtLfoFloat* self = (GstBtLfoFloat*)obj;
  g_clear_object(&self->props);
  g_clear_pointer(&self->buf_srate_props, g_free);
}

void gstbt_lfo_float_on_buf_size_change(GstBtLfoFloat* self, guint n_samples) {
  g_assert(n_samples % 4 == 0);
  self->buf_srate_nsamples = n_samples;
  self->buf_srate_props = g_realloc(self->buf_srate_props,
                                    sizeof(gfloat) * self->buf_srate_nsamples * GSTBT_LFO_FLOAT_PROP_N);
}

static void gstbt_lfo_float_init(GstBtLfoFloat* const self) {
}

void gstbt_lfo_float_class_init(GstBtLfoFloatClass* const klass) {
  GObjectClass* const gobject_class = (GObjectClass*)klass;
  gobject_class->dispose = dispose;
}

void gstbt_lfo_float_props_add(GObjectClass* const gobject_class, guint* idx) {
  const GParamFlags flags =
	(GParamFlags)(G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE);

  g_object_class_install_property(
    gobject_class,
    (*idx)++, 
    g_param_spec_int("lfo-voice-master", "Master Voice", "Master voice that modulates parameters",
                     -1, MAX_VOICES, -1, flags)
    );
  g_object_class_install_property(
    gobject_class,
    (*idx)++, 
    g_param_spec_enum("lfo-voice-master-prop", "M-Voice Prop.", "Property of this LFO modulated by master voice",
                      gstbt_lfo_float_prop_get_type(), GSTBT_LFO_FLOAT_PROP_NONE, flags)
    );
  
  GParamSpec** prop = properties;

  *(prop++) = g_param_spec_float("lfo-amplitude", "LFO Amp", "LFO Amplitude", 0, 10, 0, flags);
  *(prop++) = g_param_spec_float("lfo-frequency", "LFO Freq", "LFO Frequency", 0, 100, 0, flags);
  *(prop++) = g_param_spec_float("lfo-shape", "LFO Shape", "LFO Shape", 0, 1, 0.5f, flags);
  *(prop++) = g_param_spec_float("lfo-filter", "LFO Filter", "LFO Filter", 0, 1, 1, flags);
  *(prop++) = g_param_spec_float("lfo-offset", "LFO Offset", "LFO Offset", -1, 1, 0, flags);
  *(prop++) = g_param_spec_float("lfo-phase", "LFO Phase", "LFO Phase", -1, 1, 0, flags);
  *(prop++) = g_param_spec_enum("lfo-waveform", "LFO Wave", "LFO Waveform",
                                gst_bt_lfo_float_waveform_get_type(), GSTBT_LFO_FLOAT_WAVEFORM_SINE, flags);

  for (guint i = 0; i < GSTBT_LFO_FLOAT_PROP_N; ++i) {
    g_object_class_install_property(gobject_class, (*idx)++, properties[i]);
  }
}

GstBtLfoFloat* gstbt_lfo_float_new(GObject* const owner, const guint idx_voice) {
  GstBtLfoFloat* result = g_object_new(gstbt_lfo_float_get_type(), NULL);
  result->owner = owner;
  result->idx_voice = idx_voice;
  result->props = bt_properties_simple_new(owner);
  prop_add(result, "lfo-voice-master", &result->idx_voice_master);
  prop_add(result, "lfo-voice-master-prop", &result->voice_master_prop);
  prop_add(result, "lfo-amplitude", &result->amplitude);
  prop_add(result, "lfo-frequency", &result->frequency);
  prop_add(result, "lfo-shape", &result->shape);
  prop_add(result, "lfo-filter", &result->filter);
  prop_add(result, "lfo-offset", &result->offset);
  prop_add(result, "lfo-phase", &result->phase);
  prop_add(result, "lfo-waveform", &result->waveform);
  return result;
}
