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

#include "config.h"
#include "src/additive.h"
#include "src/genums.h"

#include "src/adsr.h"
#include "src/debug.h"
#include "src/math.h"
#include "src/voice.h"

#include "libbuzztrax-gst/audiosynth.h"
#include "libbuzztrax-gst/childbin.h"
#include "libbuzztrax-gst/musicenums.h"
#include "libbuzztrax-gst/propertymeta.h"
#include "libbuzztrax-gst/toneconversion.h"
#include <gst/controller/gstinterpolationcontrolsource.h>
#include <gst/controller/gstdirectcontrolbinding.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <pmmintrin.h>

GType gstbt_additive_get_type(void);
#define GSTBT_ADDITIVE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),gstbt_additive_get_type(),GstBtAdditive))

#define GST_MACHINE_NAME "additive"
#define GST_MACHINE_DESC "Additive synthesis via many sines"

GST_DEBUG_CATEGORY(GST_CAT_DEFAULT);

static GstStaticPadTemplate pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, "
        "format = (string) " GST_AUDIO_NE (F32) ", "
        "layout = (string) interleaved, "
        "rate = (int) [ 1, MAX ], " "channels = (int) 2")
    );


enum { MAX_OVERTONES = 600 };
enum { MAX_VIRTUAL_VOICES = 10 };

typedef struct {
  gfloat accum_rads;
  gfloat accum_rm_rads;
} StateOvertone;

typedef struct {
  GstBtAudioSynthClass parent_class;
} GstBtAdditiveClass;

typedef struct {
  GstBtNote note;
  StateOvertone states_overtone[MAX_OVERTONES];
  GstBtAdditiveV* voices[MAX_VOICES];
  gfloat* buf_srate_props;
  gboolean props_srate_nonzero[N_PROPERTIES_SRATE];
  gboolean props_srate_controlled[N_PROPERTIES_SRATE];
} StateVirtualVoice;

// Class instance data.
typedef struct {
  GstBtAudioSynth parent;

  guint overtones;
  gfloat freq_max;
  gint sum_start_idx;
  gfloat amp_pow_base;
  gfloat amp_exp_idx_mul;
  gfloat ampfreq_scale_idx_mul;
  gfloat ampfreq_scale_offset;
  gfloat ampfreq_scale_exp;
  gfloat amp_boost_center;
  gfloat amp_boost_sharpness;
  gfloat amp_boost_exp;
  gfloat amp_boost_db;
  gfloat bend;
  gfloat ringmod_rate;
  gfloat ringmod_depth;
  gfloat ringmod_ot_offset;
  gfloat stereo;
  gboolean release_on_note;
  gfloat vol;
  GstBtNote note;
  gfloat anticlick;
  
  // These are standard Buzztrax voices, repurposed as ADSR+LFOs.
  gulong n_voices;

  // These are the 'virtual voices' allowing multiple notes to play.
  guint n_virtual_voices;
  guint idx_next_virtual_voice;
  StateVirtualVoice virtual_voices[MAX_VIRTUAL_VOICES];
  GstBtAdditiveV* voices[MAX_VOICES];

  gfloat ringmod_ot_offset_calc;
  
  GstBtToneConversion* tones;

  guint buf_samples;
  guint nsamples_available;
  gfloat* buf;

  gint samples_generated;
  long time_accum;
} GstBtAdditive;

enum {
  PROP_CHILDREN = N_PROPERTIES_SRATE,
  PROP_OVERTONES,
  PROP_RELEASE_ON_NOTE,
  PROP_NOTE,
  PROP_ANTICLICK,
  N_PROPERTIES
};

static GParamSpec* properties[N_PROPERTIES] = { NULL, };


static GObject* child_proxy_get_child_by_index (GstChildProxy *child_proxy, guint index) {
  GstBtAdditive* self = GSTBT_ADDITIVE(child_proxy);

  g_return_val_if_fail(index < MAX_VOICES, NULL);

  return gst_object_ref(self->voices[index]);
}

static guint child_proxy_get_children_count (GstChildProxy *child_proxy) {
  GstBtAdditive* self = GSTBT_ADDITIVE(child_proxy);
  return self->n_voices;
}

static void child_proxy_interface_init (gpointer g_iface, gpointer iface_data) {
  GstChildProxyInterface* iface = (GstChildProxyInterface*)g_iface;

  iface->get_child_by_index = child_proxy_get_child_by_index;
  iface->get_children_count = child_proxy_get_children_count;
}

//-- the class
G_DEFINE_TYPE_WITH_CODE (
  GstBtAdditive,
  gstbt_additive,
  GSTBT_TYPE_AUDIO_SYNTH,
  G_IMPLEMENT_INTERFACE (GST_TYPE_CHILD_PROXY, child_proxy_interface_init)
  G_IMPLEMENT_INTERFACE (GSTBT_TYPE_CHILD_BIN, NULL))

static gboolean plugin_init(GstPlugin * plugin) {
  GST_DEBUG_CATEGORY_INIT(
    GST_CAT_DEFAULT,
    G_STRINGIFY(GST_CAT_DEFAULT),
    GST_DEBUG_FG_WHITE | GST_DEBUG_BG_BLACK,
    GST_MACHINE_DESC);

  return gst_element_register(
    plugin,
    GST_MACHINE_NAME,
    GST_RANK_NONE,
    gstbt_additive_get_type());
}

GST_PLUGIN_DEFINE(
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  additive,
  GST_MACHINE_DESC,
  plugin_init, VERSION, "GPL", PACKAGE_NAME, PACKAGE_BUGREPORT)


static void _set_property (GObject * object, guint prop_id, const GValue * value, GParamSpec * pspec) {
  GstBtAdditive *self = GSTBT_ADDITIVE (object);

  switch (prop_id) {
  case PROP_CHILDREN:
    self->n_voices = g_value_get_ulong(value);
    break;
  case PROP_NOTE: {
    GstBtNote note = g_value_get_enum(value);
    if (note == GSTBT_NOTE_OFF) {
      guint idx_last_virtual_voice =
        self->n_virtual_voices > 1 ?
        ((self->idx_next_virtual_voice - 1) + self->n_virtual_voices) % self->n_virtual_voices :
        0;
      
      StateVirtualVoice* vvoice = &self->virtual_voices[idx_last_virtual_voice];
      for (guint i = 0; i < self->n_voices; ++i) {
        gstbt_additivev_note_off(vvoice->voices[i], self->parent.running_time);
      }
    } else if (note != GSTBT_NOTE_NONE) {
      if (self->n_virtual_voices > 1 && self->release_on_note) {
        const guint idx_last_virtual_voice =
          ((self->idx_next_virtual_voice - 1) + self->n_virtual_voices) % self->n_virtual_voices;
        
        StateVirtualVoice* vvoice = &self->virtual_voices[idx_last_virtual_voice];
        for (guint i = 0; i < self->n_voices; ++i) {
          gstbt_additivev_note_off(vvoice->voices[i], self->parent.running_time);
        }
      }
      
      StateVirtualVoice* vvoice = &self->virtual_voices[self->idx_next_virtual_voice];
      self->idx_next_virtual_voice = (self->idx_next_virtual_voice + 1) % self->n_virtual_voices;
    
      self->note = note;
      vvoice->note = note;
      for (guint i = 0; i < self->n_voices; ++i) {
        gstbt_additivev_note_on(vvoice->voices[i], self->parent.running_time, self->anticlick);
      }
    }
    break;
  }
  case PROP_OVERTONES:
    self->overtones = g_value_get_uint(value);
    break;
  case PROP_FREQ_MAX:
    self->freq_max = g_value_get_float(value);
    break;
  case PROP_SUM_START_IDX:
    self->sum_start_idx = g_value_get_int(value);
    break;
  case PROP_AMP_POW_BASE:
    self->amp_pow_base = g_value_get_float(value);
    break;
  case PROP_AMP_EXP_IDX_MUL:
    self->amp_exp_idx_mul = g_value_get_float(value);
    break;
  case PROP_AMPFREQ_SCALE_IDX_MUL:
    self->ampfreq_scale_idx_mul = g_value_get_float(value);
    break;
  case PROP_AMPFREQ_SCALE_OFFSET:
    self->ampfreq_scale_offset = g_value_get_float(value);
    break;
  case PROP_AMPFREQ_SCALE_EXP:
    self->ampfreq_scale_exp = g_value_get_float(value);
    break;
  case PROP_AMP_BOOST_CENTER:
    self->amp_boost_center = g_value_get_float(value);
    break;
  case PROP_AMP_BOOST_SHARPNESS:
    self->amp_boost_sharpness = g_value_get_float(value);
    break;
  case PROP_AMP_BOOST_EXP:
    self->amp_boost_exp = g_value_get_float(value);
    break;
  case PROP_AMP_BOOST_DB:
    self->amp_boost_db = g_value_get_float(value);
    break;
  case PROP_RINGMOD_RATE:
    self->ringmod_rate = g_value_get_float(value);
    break;
  case PROP_RINGMOD_DEPTH:
    self->ringmod_depth = g_value_get_float(value);
    break;
  case PROP_RINGMOD_OT_OFFSET:
    self->ringmod_ot_offset = g_value_get_float(value);
    self->ringmod_ot_offset_calc = self->ringmod_ot_offset * F2PI;
    for (int j = 0; j < MAX_VIRTUAL_VOICES; ++j) {
      for (int i = 0; i < MAX_OVERTONES; ++i) {
        self->virtual_voices[j].states_overtone[i].accum_rads = self->ringmod_ot_offset_calc;
        self->virtual_voices[j].states_overtone[i].accum_rm_rads = self->ringmod_ot_offset_calc;
      }
    }
    break;
  case PROP_BEND:
    self->bend = g_value_get_float(value);
    break;
  case PROP_STEREO:
    self->stereo = g_value_get_float(value);
    break;
  case PROP_VIRTUAL_VOICES:
    self->n_virtual_voices = g_value_get_uint(value);
    break;
  case PROP_RELEASE_ON_NOTE:
    self->release_on_note = g_value_get_boolean(value);
    break;
  case PROP_VOL:
    self->vol = g_value_get_float(value);
    break;
  case PROP_ANTICLICK:
    self->anticlick = g_value_get_float(value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void _get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec) {
  GstBtAdditive *self = GSTBT_ADDITIVE (object);

  switch (prop_id) {
  case PROP_CHILDREN:
    g_value_set_ulong(value, self->n_voices);
    break;
  case PROP_OVERTONES:
    g_value_set_uint(value, self->overtones);
    break;
  case PROP_FREQ_MAX:
    g_value_set_float(value, self->freq_max);
    break;
  case PROP_SUM_START_IDX:
    g_value_set_int(value, self->sum_start_idx);
    break;
  case PROP_AMP_POW_BASE:
    g_value_set_float(value, self->amp_pow_base);
    break;
  case PROP_AMP_EXP_IDX_MUL:
    g_value_set_float(value, self->amp_exp_idx_mul);
    break;
  case PROP_AMPFREQ_SCALE_IDX_MUL:
    g_value_set_float(value, self->ampfreq_scale_idx_mul);
    break;
  case PROP_AMPFREQ_SCALE_OFFSET:
    g_value_set_float(value, self->ampfreq_scale_offset);
    break;
  case PROP_AMPFREQ_SCALE_EXP:
    g_value_set_float(value, self->ampfreq_scale_exp);
    break;
  case PROP_AMP_BOOST_CENTER:
    g_value_set_float(value, self->amp_boost_center);
    break;
  case PROP_AMP_BOOST_SHARPNESS:
    g_value_set_float(value, self->amp_boost_sharpness);
    break;
  case PROP_AMP_BOOST_EXP:
    g_value_set_float(value, self->amp_boost_exp);
    break;
  case PROP_AMP_BOOST_DB:
    g_value_set_float(value, self->amp_boost_db);
    break;
  case PROP_RINGMOD_RATE:
    g_value_set_float(value, self->ringmod_rate);
    break;
  case PROP_RINGMOD_DEPTH:
    g_value_set_float(value, self->ringmod_depth);
    break;
  case PROP_RINGMOD_OT_OFFSET:
    g_value_set_float(value, self->ringmod_ot_offset);
    break;
  case PROP_BEND:
    g_value_set_float(value, self->bend);
    break;
  case PROP_STEREO:
    g_value_set_float(value, self->stereo);
    break;
  case PROP_VIRTUAL_VOICES:
    g_value_set_uint(value, self->n_virtual_voices);
    break;
  case PROP_RELEASE_ON_NOTE:
    g_value_set_boolean(value, self->release_on_note);
    break;
  case PROP_VOL:
    g_value_set_float(value, self->vol);
    break;
  case PROP_ANTICLICK:
    g_value_set_float(value, self->anticlick);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static gfloat* srate_prop_buf_get(const GstBtAdditive* const self, const StateVirtualVoice* const vvoice, 
                                  AdditivePropsSrate prop) {
  return vvoice->buf_srate_props + self->buf_samples/2 * ((guint)prop-1);
}

static gboolean srate_prop_is_controlled(const StateVirtualVoice* const self, AdditivePropsSrate prop) {
  return self->props_srate_controlled[(guint)prop-1];
}

static gboolean srate_prop_is_nonzero(const StateVirtualVoice* const self, AdditivePropsSrate prop) {
  return self->props_srate_nonzero[(guint)prop-1];
}

static void srate_props_fill(GstBtAdditive* const self, StateVirtualVoice* const vvoice,
                             const GstClockTime timestamp, const GstClockTime interval, const guint nframes) {
  
  for (guint i = 1; i < N_PROPERTIES_SRATE; ++i) {
    GValue src = G_VALUE_INIT;
    g_value_init(&src, G_TYPE_FLOAT);
    g_object_get_property((GObject*)self, properties[i]->name, &src);
    
    gfloat value = g_value_get_float(&src);
    g_value_unset(&src);

    gfloat* const sratebuf = srate_prop_buf_get(self, vvoice, i);
    for (guint j = 0; j < nframes; ++j) {
      sratebuf[j] = value;
    }
  }

  memset(vvoice->props_srate_nonzero, 0, sizeof(vvoice->props_srate_nonzero));
  memset(vvoice->props_srate_controlled, 0, sizeof(vvoice->props_srate_controlled));
  
  for (guint i = 0; i < self->n_voices; ++i) {
    gstbt_additivev_mod_value_array_f_for_prop(
      vvoice->voices[i],
      timestamp,
      interval,
      nframes,
      vvoice->buf_srate_props,
      vvoice->props_srate_nonzero,
      vvoice->props_srate_controlled,
      vvoice->voices
      );
  }

  // Anything that can be done here will save it being done per-overtone.
  // Calculate values that differ from the initial value set in the property.
  {
    gfloat* const srate = srate_prop_buf_get(self, vvoice, PROP_AMP_BOOST_DB);
    for (guint i = 0; i < nframes; ++i)
      srate[i] = db_to_gain(srate[i]) - 1;
  }
  {
    v4sf* const srate = (v4sf*)srate_prop_buf_get(self, vvoice, PROP_FREQ_MAX);
    for (guint i = 0; i < nframes/4; ++i)
      // Constant below is the solution to the equation 440*2**(-5+1*m)=22050 for m.
      srate[i] = 440*powb24f(-5 + srate[i] * 10.64713132180759f);
  }
  {
    v4sf* const srate = (v4sf*)srate_prop_buf_get(self, vvoice, PROP_AMP_BOOST_CENTER);
    for (guint i = 0; i < nframes/4; ++i)
      // Constant below is the solution to the equation 440*2**(-5+1*m)=22050 for m.
      srate[i] = 440*powb24f(-5 + srate[i] * 10.64713132180759f);
  }
}

static gboolean is_machine_silent(const GstBtAdditive* const self, const StateVirtualVoice* const vvoice) {
  if (srate_prop_is_controlled(vvoice, PROP_VOL)) {
    return !srate_prop_is_nonzero(vvoice, PROP_VOL);
  } else {
    return self->vol == 0;
  }
}

static inline v4sf horizontal_accumulate(v4sf inc) {
  v4sf result = {inc[0], inc[0] + inc[1], inc[0] + inc[1] + inc[2], inc[0] + inc[1] + inc[2] + inc[3]};
  return result;
}

static void fill_buffer_internal(GstBtAdditive* const self, StateVirtualVoice* const vvoice, GstBuffer* gstbuf,
                                 v4sf* const buffer, int nframes) {
  g_assert(nframes*2 % 4 == 0);

  const int n4frames = nframes/4;
  
  memset(buffer, 0, n4frames*2*sizeof(typeof(*buffer)));
  
  const gfloat rate = self->parent.info.rate;

  srate_props_fill(self, vvoice, self->parent.running_time, GST_SECOND / rate, nframes);

  if (is_machine_silent(self, vvoice)) {
    return;
  }

  const gfloat freq_note = (gfloat)gstbt_tone_conversion_translate_from_number(self->tones, vvoice->note);

  v4sf* const srate_bend = (v4sf*)srate_prop_buf_get(self, vvoice, PROP_BEND);
  for (guint i = 0; i < n4frames; ++i) {
    srate_bend[i] = freq_note * powb24f(srate_bend[i]/12.0f);
  }
  
  const v4sf* const srate_freq_max = (v4sf*)srate_prop_buf_get(self, vvoice, PROP_FREQ_MAX);
  const v4sf* const srate_ampfreq_scale_idx_mul = (v4sf*)srate_prop_buf_get(self, vvoice, PROP_AMPFREQ_SCALE_IDX_MUL);
  const v4sf* const srate_amp_boost_center = (v4sf*)srate_prop_buf_get(self, vvoice, PROP_AMP_BOOST_CENTER);
  const v4sf* const srate_amp_boost_sharpness = (v4sf*)srate_prop_buf_get(self, vvoice, PROP_AMP_BOOST_SHARPNESS);
  const v4sf* const srate_amp_boost_exp = (v4sf*)srate_prop_buf_get(self, vvoice, PROP_AMP_BOOST_EXP);
  const v4sf* const srate_amp_boost_db = (v4sf*)srate_prop_buf_get(self, vvoice, PROP_AMP_BOOST_DB);
  const v4sf* const srate_amp_pow_base = (v4sf*)srate_prop_buf_get(self, vvoice, PROP_AMP_POW_BASE);
  const v4sf* const srate_amp_exp_idx_mul = (v4sf*)srate_prop_buf_get(self, vvoice, PROP_AMP_EXP_IDX_MUL);
  const v4sf* const srate_ampfreq_scale_offset = (v4sf*)srate_prop_buf_get(self, vvoice, PROP_AMPFREQ_SCALE_OFFSET);
  const v4sf* const srate_ampfreq_scale_exp = (v4sf*)srate_prop_buf_get(self, vvoice, PROP_AMPFREQ_SCALE_EXP);
  const v4sf* const srate_ringmod_rate = (v4sf*)srate_prop_buf_get(self, vvoice, PROP_RINGMOD_RATE);
  const v4sf* const srate_ringmod_depth = (v4sf*)srate_prop_buf_get(self, vvoice, PROP_RINGMOD_DEPTH);
  const v4sf* const srate_stereo = (v4sf*)srate_prop_buf_get(self, vvoice, PROP_STEREO);
  
  const gfloat secs_per_sample = 1.0f / rate;
  
  for (int j = self->sum_start_idx, idx_o = 0; idx_o < self->overtones; ++j, ++idx_o) {
    g_assert(idx_o < MAX_OVERTONES);
    StateOvertone* const overtone = &vvoice->states_overtone[idx_o];
    
	v4sf f = overtone->accum_rads * V4SF_UNIT;
	v4sf f_rm = overtone->accum_rm_rads * V4SF_UNIT;

    v4sf* buf4 = buffer;
    for (int i = 0; i < n4frames; ++i) {
      const v4sf hscale_freq = srate_ampfreq_scale_idx_mul[i] * (gfloat)j + srate_ampfreq_scale_offset[i];
	  
      const v4sf freq_note_bent = srate_bend[i];
      const v4sf freq_overtone = freq_note_bent * hscale_freq;

      // Update accumulators now so that they will still be updated even if a muted sample is skipped.
      const v4sf time_to_rads = F2PI * freq_overtone;
      const v4sf inc = time_to_rads * secs_per_sample;
      const v4sf inc_rm = inc * srate_ringmod_depth[i];

      f = horizontal_accumulate(inc) + f[3];
      f_rm = horizontal_accumulate(inc_rm) + f_rm[3];

      // Limit the number of overtones to reduce aliasing.
	  const v4si mute_sample = (freq_overtone <= 0) | (freq_overtone > srate_freq_max[i]);

      // broad check to save CPU
      if (v4si_eq(mute_sample, V4SI_TRUE)) {
		continue;
      } else {
        const v4sf amp_mute_sample = bitselect4f(mute_sample, V4SF_ZERO, V4SF_UNIT);
	  
        v4sf amp_boost = srate_amp_boost_db[i];
	  
        if (!v4sf_eq(amp_boost, V4SF_ZERO)) {
          amp_boost *= powpnz4f(window_sharp_cosine4(
                                  freq_overtone,
                                  srate_amp_boost_center[i],
                                  22050,
                                  srate_amp_boost_sharpness[i]),
                                srate_amp_boost_exp[i]+FLT_MIN);
        }

        const v4sf hscale_amp =
          pow4f_method(srate_amp_pow_base[i], (gfloat)j * srate_amp_exp_idx_mul[i])
          * pow4f_method(hscale_freq, srate_ampfreq_scale_exp[i])
          ;

        const v4sf sample = (amp_boost + hscale_amp) * amp_mute_sample * sin4f(f);
      
        v4sf sample_l;
        v4sf sample_r;
        if (!v4sf_eq(srate_ringmod_rate[i], V4SF_ZERO)) {
          // Avoid zero input to powpnz here by adding FLT_MIN
          sample_l = sample * powpnzsin4f(f_rm, srate_ringmod_rate[i]+FLT_MIN);
          sample_r = sample * powpnzsin4f(f_rm+F2PI*srate_stereo[i], srate_ringmod_rate[i]+FLT_MIN);
        } else {
          sample_l = sample;
          sample_r = sample;
        }
      
        buf4[0][0] += sample_l[0];
        buf4[0][1] += sample_r[0];
        buf4[0][2] += sample_l[1];
        buf4[0][3] += sample_r[1];
        buf4[1][0] += sample_l[2];
        buf4[1][1] += sample_r[2];
        buf4[1][2] += sample_l[3];
        buf4[1][3] += sample_r[3];
      }
      
      buf4 += 2;
    }

    overtone->accum_rads = fmodf(f[3], F2PI);
    overtone->accum_rm_rads = fmodf(f_rm[3], F2PI);
  }
  
  const v4sf* const vol_srate = (v4sf*)srate_prop_buf_get(self, vvoice, PROP_VOL);

  v4sf* buf4 = buffer;
  for (guint i = 0; i < n4frames; ++i) {
    *(buf4++) *= vol_srate[i];
    *(buf4++) *= vol_srate[i];
  }
}

static gboolean process(GstBtAudioSynth* synth, GstBuffer* gstbuf, GstMapInfo* info) {
  struct timespec clock_start;
  clock_gettime(CLOCK_MONOTONIC_RAW, &clock_start);

  GstBtAdditive* const self = GSTBT_ADDITIVE(synth);

  const guint requested_frames = self->parent.generate_samples_per_buffer;
  const guint required_bufsamps = (requested_frames+3)*4/16*4*2;
  g_assert(required_bufsamps % 4 == 0);
  
  if (self->buf_samples < required_bufsamps) {
    GST_INFO("Reallocating internal buffers (%d frames, from %d to %d 4-aligned)",
             self->parent.generate_samples_per_buffer, self->buf_samples, required_bufsamps);
    
    self->buf_samples = required_bufsamps;
    
    self->buf = g_realloc(self->buf, sizeof(typeof(*(self->buf))) * self->buf_samples);

    for (guint i = 0; i < MAX_VIRTUAL_VOICES; ++i) {
      self->virtual_voices[i].buf_srate_props =
        g_realloc(
          self->virtual_voices[i].buf_srate_props,
          sizeof(typeof(*(self->virtual_voices[i].buf_srate_props))) * (self->buf_samples/2) * (N_PROPERTIES_SRATE-1));
      
      for (int j = 0; j < MAX_VOICES; j++) {
        gstbt_additivev_on_buf_size_change(self->virtual_voices[i].voices[j], self->buf_samples/2);
      }
    }
  }

  gfloat* outbuf = (gfloat*)(info->data);
  guint to_copy = requested_frames*2;
  const gfloat* internal_buf = self->buf + self->buf_samples - self->nsamples_available;

  const guint n_residual_copy = MIN(self->nsamples_available, to_copy);
  memcpy(outbuf, internal_buf, sizeof(gfloat) * n_residual_copy);
  internal_buf += n_residual_copy;
  outbuf += n_residual_copy;
  self->nsamples_available -= n_residual_copy;
  to_copy -= n_residual_copy;

  if (self->nsamples_available == 0 && to_copy != 0) {
    for (int i = 0; i < self->n_voices; ++i) {
      gstbt_additivev_process(self->voices[i], gstbuf);
      for (guint j = 0; j < self->n_virtual_voices; ++j) {
        // tbd: this doesn't have to happen. The virtual voice LFOs and ADSRs could read directly from the parameters.
        gstbt_additivev_copy(self->voices[i], self->virtual_voices[j].voices[i]);
      }
    }

    self->nsamples_available = self->buf_samples;
    memset(self->buf, 0, self->nsamples_available*sizeof(typeof(*self->buf)));
  
    v4sf* const buf4 = (v4sf*)self->buf;
    v4sf* const buf4_vvoice = g_alloca(self->buf_samples/4 * sizeof(v4sf));
    for (guint i = 0; i < self->n_virtual_voices; ++i) {
      fill_buffer_internal(self, &self->virtual_voices[i], gstbuf, buf4_vvoice, self->buf_samples/2);
      for (guint j = 0; j < self->buf_samples/4; ++j) {
        buf4[j] += buf4_vvoice[j];
      }
    }

    internal_buf = self->buf;
  }
  
  g_assert(to_copy <= self->nsamples_available);
  memcpy(outbuf, internal_buf, sizeof(gfloat) * to_copy);
  self->nsamples_available -= to_copy;

  struct timespec clock_end;
  clock_gettime(CLOCK_MONOTONIC_RAW, &clock_end);
  self->time_accum += (clock_end.tv_sec - clock_start.tv_sec) * 1e9L + (clock_end.tv_nsec - clock_start.tv_nsec);
  self->samples_generated += self->parent.generate_samples_per_buffer;
  if (self->samples_generated > self->parent.info.rate) {
    GST_DEBUG("Avg perf: %f samples/sec\n", self->samples_generated / (self->time_accum / 1e9f));
    self->time_accum = 0;
    self->samples_generated = 0;
  }
  
  // Note: if FALSE is ever returned from this function then downstream effects stop making noise.
  return TRUE;
}

static void _negotiate (GstBtAudioSynth* base, GstCaps* caps) {
  for (guint i = 0; i < gst_caps_get_size(caps); ++i) {
    GstStructure* const s = gst_caps_get_structure(caps, i);
    
    GST_LOG("caps structure %d: %" GST_PTR_FORMAT, i, (void*)s);
  }
}

static void gstbt_additive_init(GstBtAdditive* const self) {
  self->tones = gstbt_tone_conversion_new(GSTBT_TONE_CONVERSION_EQUAL_TEMPERAMENT);

  for (int j = 0; j < MAX_VIRTUAL_VOICES; j++) {
    for (int i = 0; i < MAX_VOICES; i++) {
      GstBtAdditiveV* voice = gstbt_additivev_new(&properties[1], N_PROPERTIES_SRATE, i);

      char name[7];
      g_snprintf(name, sizeof(name), "voice%1d_%1d",j,i);
        
      gst_object_set_name((GstObject*)voice, name);
      gst_object_set_parent((GstObject*)voice, (GstObject *)self);

      self->virtual_voices[j].voices[i] = voice;
    }
  }

  for (int i = 0; i < MAX_VOICES; i++) {
    GstBtAdditiveV* voice = gstbt_additivev_new(&properties[1], N_PROPERTIES_SRATE, i);

    char name[7];
    g_snprintf(name, sizeof(name), "voice%1d",i);
        
    gst_object_set_name((GstObject*)voice, name);
    gst_object_set_parent((GstObject*)voice, (GstObject *)self);

    self->voices[i] = voice;
  }

  self->n_virtual_voices = 1;
}

static void _dispose (GObject* object) {
  GstBtAdditive* self = GSTBT_ADDITIVE(object);
  g_clear_object(&self->tones);
  g_clear_pointer(&self->buf, g_free);
  for (int i = 0; i < MAX_VIRTUAL_VOICES; i++) {
    g_clear_pointer(&self->virtual_voices[i].buf_srate_props, g_free);
  }
  
  // It's necessary to unparent children so they will be unreffed and cleaned up. GstObject doesn't hold variable
  // links to its children, so it wouldn't know to unparent them and this would cause a memory leak.
  for (int i = 0; i < MAX_VOICES; i++) {
    gst_object_unparent((GstObject*)self->voices[i]);
  }
  G_OBJECT_CLASS(gstbt_additive_parent_class)->dispose(object);
}

static void gstbt_additive_class_init(GstBtAdditiveClass * const klass) {
  GObjectClass* const gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = _set_property;
  gobject_class->get_property = _get_property;
  gobject_class->dispose = _dispose;

  GstElementClass* const element_class = (GstElementClass *) klass;
  gst_element_class_set_static_metadata(
    element_class,
    "Additive",
    "Source/Audio",
    GST_MACHINE_DESC,
    PACKAGE_BUGREPORT);

  GstBtAudioSynthClass *audio_synth_class = (GstBtAudioSynthClass *) klass;
  audio_synth_class->process = process;
  /*audio_synth_class->reset = gstbt_sim_syn_reset;*/
  audio_synth_class->negotiate = _negotiate;

    // TBD: docs
/*  gst_element_class_add_metadata (element_class, GST_ELEMENT_METADATA_DOC_URI,
"file://" DATADIR "" G_DIR_SEPARATOR_S "gtk-doc" G_DIR_SEPARATOR_S "html"
G_DIR_SEPARATOR_S "" PACKAGE "-gst" G_DIR_SEPARATOR_S "GstBtSimSyn.html");*/

  // Note: variables will not be set to default values unless G_PARAM_CONSTRUCT is given.
  const GParamFlags flags =
    (GParamFlags)(G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);
  
  // GstBtChildBin interface properties
  properties[PROP_CHILDREN] = g_param_spec_ulong(
    "children", "Children", "",
    0, MAX_VOICES, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  // Instance properties
  properties[PROP_OVERTONES] =
    g_param_spec_uint("overtones", "Overtones", "Overtones", 0, MAX_OVERTONES, 10, flags);
  properties[PROP_FREQ_MAX] =
    g_param_spec_float("freq-max", "Freq Max", "Freq Max", 0, 1, 1, flags);
  properties[PROP_SUM_START_IDX] =
    g_param_spec_int("sum-start-idx", "Sum Start Idx", "Sum Start Index", -10, 25, 1, flags);
  properties[PROP_AMP_POW_BASE] =
    g_param_spec_float("amp-pow-base", "Amp Power Base", "Amplitude Power Base", -10, 10, 1, flags);
  properties[PROP_AMP_EXP_IDX_MUL] =
    g_param_spec_float("amp-exp-idx-mul", "Amp Exp Idx Mul", "Amplitude Exponent Index Multiplier", -10, 10, 1, flags);
  properties[PROP_AMPFREQ_SCALE_IDX_MUL] =
    g_param_spec_float("ampfreq-scale-idx-mul", "Ampfreq Scale Idx Mul", "Amplitude + Frequency Scale Index Multiplier",
                       -10, 10, 1, flags);
  properties[PROP_AMPFREQ_SCALE_OFFSET] =
    g_param_spec_float("ampfreq-scale-offset", "Ampfreq Scale Offset", "Amplitude + Frequency Scale Offset",
                       -10, 10, 0, flags);
  properties[PROP_AMPFREQ_SCALE_EXP] =
    g_param_spec_float("ampfreq-scale-exp", "Ampfreq Scale Exp", "Amplitude + Frequency Scale Exponent",
                       -10, 1, -1, flags);
  properties[PROP_AMP_BOOST_CENTER] =
    g_param_spec_float("amp-boost-center", "AmpBoost Cntr", "AmpBoost Center Frequency", 0, 1, 0, flags);
  properties[PROP_AMP_BOOST_SHARPNESS] =
    g_param_spec_float("amp-boost-sharpness", "AmpBoost Shrp", "AmpBoost Sharpness", 0, 200, 0, flags);
  properties[PROP_AMP_BOOST_EXP] =
    g_param_spec_float("amp-boost-exp", "AmpBoost Exp", "AmpBoost Exponential Shape", 0, 1024, 2, flags);
  properties[PROP_AMP_BOOST_DB] =
    g_param_spec_float("amp-boost-db", "AmpBoost dB", "AmpBoost dB", 0, 30, 2, flags);
  properties[PROP_RINGMOD_RATE] =
    g_param_spec_float("ringmod-rate", "RM Rate", "Per-overtone Ringmod Rate (Shape)", 0, 5, 0, flags);
  properties[PROP_RINGMOD_DEPTH] =
    g_param_spec_float("ringmod-depth", "RM Depth", "Per-overtone Ringmod Depth", 0, 0.5, 0, flags);
  properties[PROP_RINGMOD_OT_OFFSET] =
    g_param_spec_float("ringmod-ot-offset", "Ringmod OT Offset", "Per-overtone Ring Modulation Offset (Phase)",
                       0, 1, 0, flags);
  properties[PROP_BEND] =
    g_param_spec_float("bend", "Bend (semitones)", "Bend (semitones)", -64, 64, 0, flags);
  properties[PROP_STEREO] =
    g_param_spec_float("stereo", "Stereo", "Stereo Width", 0, 1, 0.25, flags);
  properties[PROP_VIRTUAL_VOICES] =
    g_param_spec_uint("virtual-voices", "Virt. Voice", "Virtual Voices", 1, MAX_VIRTUAL_VOICES, 1, flags);
  properties[PROP_RELEASE_ON_NOTE] =
    g_param_spec_boolean("release-on-note", "Rel On Note", "Release virtual voice on each note", TRUE, flags);
  properties[PROP_VOL] =
    g_param_spec_float("vol", "Vol", "Volume", 0, 1, 0.5, flags);
  properties[PROP_NOTE] =
    g_param_spec_enum("note", "Note", "Note", GSTBT_TYPE_NOTE, GSTBT_NOTE_NONE,
                      G_PARAM_WRITABLE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS);

  properties[PROP_ANTICLICK] =
    g_param_spec_float("anticlick", "Anti-click", "Anti-click", 0, 1, 0.05, flags);
  
  for (int i = 1; i < N_PROPERTIES; ++i)
    g_assert(properties[i]);

  g_object_class_install_properties (gobject_class, N_PROPERTIES, properties);

  // Override AudioSynth's pad so that float format can be used.
  gst_element_class_add_static_pad_template (element_class, &pad_template);

  math_test();
}

