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

  gfloat c_frequency;
  gfloat accum;
  gfloat integrate;
  
  BtPropertiesSimple* props;
  GObject* owner;
  guint idx_voice;
  guint buf_srate_nsamples;
  v4sf* buf_srate_props;
  gboolean props_srate_nonzero[GSTBT_LFO_FLOAT_PROP_N];
  gboolean props_srate_controlled[GSTBT_LFO_FLOAT_PROP_N];
  guint32 noise_state;
  gfloat noise_cur;
};

G_DEFINE_TYPE(GstBtLfoFloat, gstbt_lfo_float, G_TYPE_OBJECT);

static GParamSpec* properties[GSTBT_LFO_FLOAT_PROP_N] = { NULL, };

// https://www.pcg-random.org/pdf/hmc-cs-2014-0905.pdf
static const guint32 lcg_multiplier = 1103515245;
static const guint32 lcg_increment = 12345;

static inline gfloat lcg(guint32* state) {
  *state = (*state + lcg_increment) * lcg_multiplier;

  // Hexadecimal floating point literals are a means to define constant real values that can be exactly
  // represented as a floating point value.
  //
  // https://www.pcg-random.org/posts/bounded-rands.html
  // https://www.exploringbinary.com/hexadecimal-floating-point-constants/
  // pg 57-58: http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1256.pdf
  return -1.0f + 0x2.0p-32 * *state;
}

// 0 <= accum < get_accum_period()
static inline v4sf get_sample(const guint waveform, const v4sf accum_unbounded, const v4sf shape,
                              const v4sf accum_unbounded_prev, guint32 *noise_state, gfloat *noise_cur) {
  // accum % 1.0 (period)
  const v4sf accum = accum_unbounded - floor4f(accum_unbounded);
  
  switch (waveform) {
  case GSTBT_LFO_FLOAT_WAVEFORM_SINE:
	return powpnzsin4f(accum * F2PI, powpnz4f(shape+0.5f, 8 * V4SF_UNIT));
  case GSTBT_LFO_FLOAT_WAVEFORM_SQUARE:
    return bitselect4f(accum < shape, -V4SF_UNIT, V4SF_UNIT);
  case GSTBT_LFO_FLOAT_WAVEFORM_SAW:
    // Constant ensures that a shape value of 0.5 results in exponent of one. Range is 0.015 to 64.
    return -1 + pow4f(accum, 0.5f*powb24f(-10+(shape+0.41667f)*12)) * 2;
  case GSTBT_LFO_FLOAT_WAVEFORM_TRIANGLE:
    // s : 20; plot2d(if (x < 0.5) then (x*2)**s else ((1-x)*2)**s,[x,0,1],[y,0,1]);
    return powpnz4f(bitselect4f(accum < 0.5f, accum, 1 - accum) * 2, 0.5f*powb24f(-10+(shape+0.41667f)*12));
  case GSTBT_LFO_FLOAT_WAVEFORM_NOISE: {
    // For each sample, check to see if the accumulator value passed 1.0 since the last sample. If so, advance the
    // RNG.
    const v4sf accum_prev_smp = {accum_unbounded_prev[3], accum_unbounded[0], accum_unbounded[1], accum_unbounded[2]};
    const v4si period_changed = trunc4f(accum_prev_smp) != trunc4f(accum_unbounded);
    
    v4sf result;
    for (guint i = 0; i < 4; ++i) {
      if (period_changed[i]) {
        *noise_cur = lcg(noise_state);
      }
      result[i] = *noise_cur;
    }
    
    return result;
  }
  default:
    return V4SF_ZERO;
  }
}

static void srate_props_fill(GstBtLfoFloat* const self, const GstClockTime timestamp, const GstClockTime interval,
                             guint n_values, GstBtAdditiveV** voices) {

  g_assert(self->buf_srate_nsamples);
  
  for (guint i = 0; i < GSTBT_LFO_FLOAT_PROP_N; ++i) {
    GValue src = G_VALUE_INIT;

    v4sf value;
    if (i == GSTBT_LFO_FLOAT_PROP_FREQUENCY) {
      value = V4SF_UNIT * self->c_frequency;
    } else if (g_type_is_a(properties[i]->value_type, G_TYPE_ENUM)) {
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

  if (voices && self->idx_voice_master != -1 && self->voice_master_prop != GSTBT_LFO_FLOAT_PROP_NONE) {

    // Note: only modulate with LFO if "voice master" isn't set to reference itself.
    // In that case, the ADSR of the current voice will modulate, but not the LFO.
    gstbt_additivev_mod_value_array_f_for_prop_idx(
      voices[self->idx_voice_master],
      timestamp,
      interval,
      self->buf_srate_nsamples,
      (gfloat*)self->buf_srate_props,
      self->props_srate_nonzero,
      self->props_srate_controlled,
      voices,
      (guint)self->voice_master_prop,
      self->idx_voice_master != self->idx_voice
      );
  }
}

static gboolean mod_value_array_accum(GstBtLfoFloat* self, GstClockTime timestamp, GstClockTime interval,
                                      gfloat* values, guint n_values, GstBtAdditiveV** voices) {
  g_assert(self->c_frequency != 0);
  if (self->amplitude == 0)
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
  const guint waveform = srate_waveform[0][0];
  
  for (guint i = 0; i < n_values/4; ++i) {
    const v4sf inc = inc_base * srate_frequency[i];
    
    accum4[1] = accum4[0] + inc[0];
    accum4[2] = accum4[1] + inc[1];
    accum4[3] = accum4[2] + inc[2];
    
    const v4sf alpha = pow4f(srate_filter[i], 8 * V4SF_UNIT);
  
	const v4sf val =
      srate_offset[i] +
      get_sample(waveform, accum4 + srate_phase[i], srate_shape[i], accum4, &self->noise_state, &self->noise_cur) *
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

gfloat elerp(gfloat start, gfloat end, gfloat base, gfloat x) {
  return -1+start+powf(1+end-start,powf(x,base));
}

gboolean gstbt_lfo_float_property_set(GObject* obj, guint prop_id, const GValue* value, GParamSpec* pspec) {
  GstBtLfoFloat* self = (GstBtLfoFloat*)obj;
  gboolean result = bt_properties_simple_set(self->props, pspec, value);

  // GSTBT_LFO_FLOAT_PROP_* indices are zero-based, as opposed to the 1-based GObject props.
  if (prop_id-1 == GSTBT_LFO_FLOAT_PROP_FREQUENCY) {
    // Range from 1 minute period to every sample period (possibly useful for noise).
    self->c_frequency = elerp(1.0/60.0, 44100, 2, self->frequency);
  }
  
  return result;
}

gboolean gstbt_lfo_float_property_get(GObject* obj, guint prop_id, GValue* value, GParamSpec* pspec) {
  return bt_properties_simple_get(((GstBtLfoFloat*)obj)->props, pspec, value);
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
    g_param_spec_int("lfo-voice-master", "LFO Master", "Master voice that modulates one chosen parameter of this LFO",
                     -1, MAX_VOICES, -1, flags)
    );
  g_object_class_install_property(
    gobject_class,
    (*idx)++, 
    g_param_spec_enum("lfo-voice-master-prop", "LFO-M Prop.", "Property of this LFO modulated by LFO voice master",
                      gstbt_lfo_float_prop_get_type(), GSTBT_LFO_FLOAT_PROP_NONE, flags)
    );
  
  GParamSpec** prop = properties;

  *(prop++) = g_param_spec_float("lfo-amplitude", "LFO Amp", "LFO Amplitude", 0, 10, 0, flags);
  *(prop++) = g_param_spec_float("lfo-frequency", "LFO Freq", "LFO Frequency", 0, 1, 0, flags);
  *(prop++) = g_param_spec_float("lfo-shape", "LFO Shape", "LFO Shape", 0, 1, 0.5f, flags);
  *(prop++) = g_param_spec_float("lfo-filter", "LFO Filter", "LFO Filter", 0, 1, 1, flags);
  *(prop++) = g_param_spec_float("lfo-offset", "LFO Offset", "LFO Offset", -2, 2, 0, flags);
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
  bt_properties_simple_add(result->props, "lfo-voice-master", &result->idx_voice_master);
  bt_properties_simple_add(result->props, "lfo-voice-master-prop", &result->voice_master_prop);
  bt_properties_simple_add(result->props, "lfo-amplitude", &result->amplitude);
  bt_properties_simple_add(result->props, "lfo-frequency", &result->frequency);
  bt_properties_simple_add(result->props, "lfo-shape", &result->shape);
  bt_properties_simple_add(result->props, "lfo-filter", &result->filter);
  bt_properties_simple_add(result->props, "lfo-offset", &result->offset);
  bt_properties_simple_add(result->props, "lfo-phase", &result->phase);
  bt_properties_simple_add(result->props, "lfo-waveform", &result->waveform);
  return result;
}
