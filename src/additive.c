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
#include "src/genums.h"

#include "src/envelope.h"
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

GType gstbt_additive_get_type(void);
#define GSTBT_ADDITIVE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),gstbt_additive_get_type(),GstBtAdditive))

#define GST_MACHINE_NAME "additive"
#define GST_MACHINE_DESC "Additive synthesis via many sines"

#define GST_CAT_DEFAULT additive_debug
GST_DEBUG_CATEGORY_STATIC(GST_CAT_DEFAULT);

const float FPI = (gfloat)G_PI;
const float F2PI = (gfloat)(2*G_PI);

static gfloat lut_sin[1024];

enum { MAX_VOICES = 24 };
enum { MAX_OVERTONES = 600 };

typedef struct {
  gfloat accum_rads;
  gfloat accum_rm_rads;
} StateOvertone;

typedef struct
{
  GstBtAudioSynthClass parent_class;
} GstBtAdditiveClass;

// Class instance data.
typedef struct
{
  GstBtAudioSynth parent;

  gulong n_voices;
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
  gfloat vol;
  GstBtNote note;

  gfloat ringmod_ot_offset_calc;
  gfloat amp_boost_db_calc;
  gfloat accum_rads;
  GstBtToneConversion* tones;
  gfloat* buf;
  gfloat* buf_srate_props;
  StateOvertone* states_overtone;
  GstBtAdditiveV* voices[MAX_VOICES];

  gint calls;
  long time_accum;
  gboolean props_srate_nonzero[N_PROPERTIES_SRATE];
  gboolean props_srate_controlled[N_PROPERTIES_SRATE];
} GstBtAdditive;

enum {
  PROP_CHILDREN = N_PROPERTIES_SRATE,
  PROP_OVERTONES,
  PROP_NOTE,
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

  GST_INFO("initializing iface");

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
	  for (guint i = 0; i < self->n_voices; ++i) {
		gstbt_additivev_note_off(self->voices[i], self->parent.running_time);
	  }
	} else if (note != GSTBT_NOTE_NONE) {
	  self->note = note;
	  for (guint i = 0; i < self->n_voices; ++i) {
		gstbt_additivev_note_on(self->voices[i], self->parent.running_time);
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
	self->amp_boost_db_calc = db_to_gain(g_value_get_float(value));
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
	for (int i = 0; i < MAX_OVERTONES; ++i) {
	  self->states_overtone[i].accum_rads = self->ringmod_ot_offset_calc;
	  self->states_overtone[i].accum_rm_rads = self->ringmod_ot_offset_calc;
	}
	break;
  case PROP_BEND:
	self->bend = g_value_get_float(value);
	break;
  case PROP_VOL:
	self->vol = g_value_get_float(value);
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
  case PROP_VOL:
	g_value_set_float(value, self->vol);
	break;
  default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	break;
  }
}

// Domain: [0, 683565275.5764316]
// No performance benefit was recorded compared to fast sin.
// LUT size doesn't seem to affect performance.
static inline v4sf sin_ps_lut(const v4sf x) {
  const gfloat* const lut = (const gfloat*)lut_sin;
  const v4sf x_scale = x/F2PI * (sizeof(lut_sin) / sizeof(typeof(lut_sin)));
  const v4ui whole = __builtin_convertvector(x_scale, v4ui);
  const v4sf frac = x_scale - __builtin_convertvector(whole, v4sf);
  const v4ui idxa = whole & ((sizeof(lut_sin) / sizeof(typeof(lut_sin)))-1);
  const v4ui idxb = (idxa+1) & ((sizeof(lut_sin) / sizeof(typeof(lut_sin)))-1);
  const v4sf a = {lut[idxa[0]], lut[idxa[1]], lut[idxa[2]], lut[idxa[3]]};
  const v4sf b = {lut[idxb[0]], lut[idxb[1]], lut[idxb[2]], lut[idxb[3]]};

  return a + (b-a) * frac;
}

static inline v4sf sin_ps_method(const v4sf x) {
  return sin_ps(x);
//  return _ZGVbN4v_sinf(x);
}

// Return a sin with range 0 -> 1
static inline gfloat sin01(const gfloat x) {
  return (1.0f + sin(x)) * 0.5f;
}

static inline v4sf sin01_ps(const v4sf x) {
  return (1.0f + sin_ps_method(x)) * 0.5f;
}

static inline v4sf pow_posexp_ps(const v4sf x, const v4sf vexp) {
  return exp_ps(vexp*log_ps(x));
}

// Take a sin with range 0 -> 1 and exponentiate to 'vexp' power
// Return the result normalized back to -1 -> 1 range.
// A way of waveshaping using non-odd powers?
static inline v4sf powsin_ps(const v4sf x, const v4sf vexp) {
  // tbd: inline _ZGVbN4vv_powf?
  // tbd: use loop to avoid use of _ZGVbN4vv_powf? check that it will vectorize.
  return (_ZGVbN4vv_powf(sin01_ps(x), vexp) - 0.5f) * 2.0f;

// faster, but crackling
//  return (pow_posexp_ps(sin01_ps(x), vexp) - 0.5f) * 2.0f;
}

#if 0
static gfloat flcm(const gfloat a, const gfloat b) {
  gfloat i = MAX(a,b);
  for (gfloat x = i; x < FLT_MAX; x += i) {
	if (roundf(x / a) - (x / a) < 0.000001f && roundf(x / b) - (x / b) < 0.000001f)
	  return x;
	else
	  x += i;
  }
  return INFINITY;
}
#endif

static inline gfloat window_sharp_cosine(gfloat sample, gfloat sample_center, gfloat rate, gfloat sharpness) {
  return bitselect_f(
	sharpness == 0.0f,
	0.0f,
	0.5f +
	-0.5f * cos(F2PI *clamp(sharpness * (sample + rate/2.0f/sharpness - sample_center) / rate, 0.0f, 1.0f))
	);
}

static inline v4sf window_sharp_cosine4(v4sf sample, v4sf sample_center, v4sf rate, v4sf sharpness) {
  v4sf result;
  for (guint i = 0; i < 4; ++i) {
	result[i] = window_sharp_cosine(sample[i], sample_center[i], rate[i], sharpness[i]);
  }
  return result;
}

static gfloat* srate_prop_buf_get(const GstBtAdditive* const self, PropsSrate prop) {
  return self->buf_srate_props + self->parent.generate_samples_per_buffer * ((guint)prop-1);
}

static gboolean srate_prop_is_controlled(const GstBtAdditive* const self, PropsSrate prop) {
  return self->props_srate_controlled[(guint)prop-1];
}

static gboolean srate_prop_is_nonzero(const GstBtAdditive* const self, PropsSrate prop) {
  return self->props_srate_nonzero[(guint)prop-1];
}

static void srate_props_fill(GstBtAdditive* const self, const GstClockTime timestamp, const GstClockTime interval) {
  for (guint i = 0; i < N_PROPERTIES_SRATE; ++i) {
	GValue src = G_VALUE_INIT;
	g_value_init(&src, G_TYPE_FLOAT);
	g_object_get_property((GObject*)self, properties[i+1]->name, &src);
	
	gfloat value = g_value_get_float(&src);
	g_value_unset(&src);

	gfloat* const sratebuf = &self->buf_srate_props[i * self->parent.generate_samples_per_buffer];
	for (guint j = 0; j < self->parent.generate_samples_per_buffer; ++j)
	  sratebuf[j] = value;
  }

  memset(self->props_srate_nonzero, 0, sizeof(self->props_srate_nonzero));
  memset(self->props_srate_controlled, 0, sizeof(self->props_srate_controlled));
  
  for (guint i = 0; i < self->n_voices; ++i) {
	gstbt_additivev_get_value_array_f_for_prop(
	  self->voices[i],
	  timestamp,
	  interval,
	  self->parent.generate_samples_per_buffer,
	  self->buf_srate_props,
	  self->props_srate_nonzero,
	  self->props_srate_controlled
	  );
  }
}

static gboolean is_machine_silent(GstBtAdditive* self) {
  if (srate_prop_is_controlled(self, PROP_VOL)) {
	return !srate_prop_is_nonzero(self, PROP_VOL);
  } else {
	return self->vol == 0;
  }
}

static gboolean _process(GstBtAudioSynth* synth, GstBuffer* gstbuf, GstMapInfo* info) {
  struct timespec clock_start;
  clock_gettime(CLOCK_MONOTONIC_RAW, &clock_start);
  
  GstBtAdditive* const self = GSTBT_ADDITIVE(synth);

  const gfloat rate = synth->info.rate;
  const gfloat secs_per_sample = 1.0f / rate;

  for (int i = 0; i < self->n_voices; ++i) {
	gstbt_additivev_process(self->voices[i], gstbuf);
  }

  const gfloat freq_note = (gfloat)gstbt_tone_conversion_translate_from_number(self->tones, self->note);

  const int nbufelements = synth->generate_samples_per_buffer;
  const int nbuf4elements = nbufelements/4;

  srate_props_fill(self, synth->running_time, 1e9L / rate);

  if (is_machine_silent(self)) {
	memset(info->data, 0, synth->generate_samples_per_buffer * sizeof(guint16));

	// Note: if FALSE is returned here then downstream effects stop making noise.
	return TRUE;
  }

  v4sf* const srate_bend = (v4sf*)srate_prop_buf_get(self, PROP_BEND);
  for (guint i = 0; i < nbuf4elements; ++i) {
	srate_bend[i] = freq_note + freq_note * srate_bend[i];
  }
  
  v4sf* const buf4 = (v4sf*)self->buf;

  memset(buf4, 0, nbuf4elements*sizeof(typeof(*buf4)));

  const v4sf tiny = V4SF_UNIT * 1e-6f; // note: also -120db
  const v4sf freq_limit = V4SF_UNIT * 22050.0f;

  const v4sf* const srate_freq_max = (v4sf*)srate_prop_buf_get(self, PROP_FREQ_MAX);
  const v4sf* const srate_ampfreq_scale_idx_mul = (v4sf*)srate_prop_buf_get(self, PROP_AMPFREQ_SCALE_IDX_MUL);
  const v4sf* const srate_amp_boost_sharpness = (v4sf*)srate_prop_buf_get(self, PROP_AMP_BOOST_SHARPNESS);
  const v4sf* const srate_amp_boost_exp = (v4sf*)srate_prop_buf_get(self, PROP_AMP_BOOST_EXP);
  const v4sf* const srate_amp_pow_base = (v4sf*)srate_prop_buf_get(self, PROP_AMP_POW_BASE);
  const v4sf* const srate_amp_exp_idx_mul = (v4sf*)srate_prop_buf_get(self, PROP_AMP_EXP_IDX_MUL);
  const v4sf* const srate_ampfreq_scale_offset = (v4sf*)srate_prop_buf_get(self, PROP_AMPFREQ_SCALE_OFFSET);
  const v4sf* const srate_ampfreq_scale_exp = (v4sf*)srate_prop_buf_get(self, PROP_AMPFREQ_SCALE_EXP);
  const v4sf* const srate_amp_boost_center = (const v4sf*)srate_prop_buf_get(self, PROP_AMP_BOOST_CENTER);
  const v4sf* const srate_ringmod_rate = (const v4sf*)srate_prop_buf_get(self, PROP_RINGMOD_RATE);
  const v4sf* const srate_ringmod_depth = (const v4sf*)srate_prop_buf_get(self, PROP_RINGMOD_DEPTH);

  for (int j = self->sum_start_idx, idx_o = 0; idx_o < self->overtones; ++j, ++idx_o) {
	g_assert(idx_o < MAX_OVERTONES);
	StateOvertone* const overtone = &self->states_overtone[idx_o];
	
	for (int i = 0; i < nbuf4elements; ++i) {
	  const v4sf freq_note_bent = srate_bend[i];

	  const v4sf hscale_freq = maxf4(srate_ampfreq_scale_idx_mul[i] * (gfloat)j + srate_ampfreq_scale_offset[i], tiny);
	  const v4sf freq_overtone = freq_note_bent * hscale_freq;
	
	  // Limit the number of overtones to reduce aliasing.
	  const v4sf alias_mute = bitselect_4f(freq_overtone > srate_freq_max[i], V4SF_ZERO, V4SF_UNIT);
	
	  const v4sf amp_boost =
		alias_mute +
		powf4(window_sharp_cosine4(
				freq_overtone,
				srate_amp_boost_center[i],
				freq_limit,
				srate_amp_boost_sharpness[i]),
			  srate_amp_boost_exp[i]) *
		self->amp_boost_db_calc
		;
	
	  const v4sf hscale_amp =
		powf4(srate_amp_pow_base[i], (gfloat)j * srate_amp_exp_idx_mul[i]) *
		powf4(hscale_freq, srate_ampfreq_scale_exp[i]) *
		amp_boost;

	  const v4sf time_to_rads = F2PI * freq_overtone;
	  const v4sf inc = time_to_rads * secs_per_sample;
	  
	  const v4sf inc_rm = inc * srate_ringmod_depth[i];

	  const v4sf f = {
		overtone->accum_rads + inc[0],
		overtone->accum_rads + inc[0] + inc[1],
		overtone->accum_rads + inc[0] + inc[1] + inc[2],
		overtone->accum_rads + inc[0] + inc[1] + inc[2] + inc[3]
	  };
	  const v4sf f_rm = {
		overtone->accum_rm_rads + inc_rm[0],
		overtone->accum_rm_rads + inc_rm[0] + inc_rm[1],
		overtone->accum_rm_rads + inc_rm[0] + inc_rm[1] + inc_rm[2],
		overtone->accum_rm_rads + inc_rm[0] + inc_rm[1] + inc_rm[2] + inc_rm[3]
	  };

	  buf4[i] += hscale_amp * sin_ps_method(f) * powsin_ps(f_rm, srate_ringmod_rate[i]);

	  overtone->accum_rads = f[3];
	  overtone->accum_rm_rads = f_rm[3];
	}

	overtone->accum_rm_rads = fmodf(overtone->accum_rm_rads, F2PI);
	overtone->accum_rads = fmodf(overtone->accum_rads, F2PI);
  }

  const v4sf* const vol_srate = (v4sf*)srate_prop_buf_get(self, PROP_VOL);
  
  const gfloat fscale = 32768.0f;
  for (int i = 0; i < nbuf4elements; ++i)
	((v4ss*)info->data)[i] = __builtin_convertvector(buf4[i] * fscale * vol_srate[i], v4ss);
  
  struct timespec clock_end;
  clock_gettime(CLOCK_MONOTONIC_RAW, &clock_end);
  self->time_accum += (clock_end.tv_sec - clock_start.tv_sec) * 1e9L + (clock_end.tv_nsec - clock_start.tv_nsec);
  self->calls += nbuf4elements * 4;
  if (self->calls > rate) {
	GST_INFO("Avg perf: %f samples/sec\n", self->calls / (self->time_accum / 1e9f));
	self->time_accum = 0;
	self->calls = 0;
  }
  return TRUE;
}

static void _negotiate (GstBtAudioSynth* base, GstCaps* caps) {
  for (guint i = 0; i < gst_caps_get_size(caps); ++i) {
	GstStructure* const s = gst_caps_get_structure(caps, i);
	
    gst_structure_fixate_field_nearest_int(s, "channels", 1);

	GST_LOG("caps structure %d: %" GST_PTR_FORMAT, i, (void*)s);
  }
}

static void gstbt_additive_init(GstBtAdditive* const self) {
  self->tones = gstbt_tone_conversion_new(GSTBT_TONE_CONVERSION_EQUAL_TEMPERAMENT);
  self->buf = g_malloc(sizeof(typeof(*(self->buf))) * self->parent.generate_samples_per_buffer);
  self->buf_srate_props =
	g_malloc(sizeof(typeof(*(self->buf_srate_props))) * self->parent.generate_samples_per_buffer * N_PROPERTIES_SRATE);

  self->states_overtone = g_new0(StateOvertone, MAX_OVERTONES);

  for (int i = 0; i < MAX_VOICES; i++) {
	self->voices[i] = gstbt_additivev_new(&properties[1], N_PROPERTIES_SRATE);

	char name[7];
	g_snprintf(name, sizeof(name), "voice%1d", i);
		
	gst_object_set_name((GstObject *)self->voices[i], name);
	gst_object_set_parent((GstObject *)self->voices[i], (GstObject *)self);
  }
}

static void _dispose (GObject* object) {
  GstBtAdditive* self = GSTBT_ADDITIVE(object);
  g_clear_object(&self->tones);
  g_clear_pointer(&self->buf, g_free);
  g_clear_pointer(&self->buf_srate_props, g_free);
  g_clear_pointer(&self->states_overtone, g_free);
  
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
  audio_synth_class->process = _process;
  /*audio_synth_class->reset = gstbt_sim_syn_reset;*/
  audio_synth_class->negotiate = _negotiate;

	// TBD: docs
/*  gst_element_class_add_metadata (element_class, GST_ELEMENT_METADATA_DOC_URI,
"file://" DATADIR "" G_DIR_SEPARATOR_S "gtk-doc" G_DIR_SEPARATOR_S "html"
G_DIR_SEPARATOR_S "" PACKAGE "-gst" G_DIR_SEPARATOR_S "GstBtSimSyn.html");*/

  const GParamFlags flags =
	(GParamFlags)(G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS);
  
  // GstBtChildBin interface properties
  properties[PROP_CHILDREN] = g_param_spec_ulong(
	"children", "Children", "",
	1, MAX_VOICES, 1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  // Instance properties
  properties[PROP_OVERTONES] =
	g_param_spec_uint("overtones", "Overtones", "", 0, MAX_OVERTONES, 10, flags);
  properties[PROP_FREQ_MAX] =
	g_param_spec_float("freq-max", "Freq Max", "", 0, 22050, 22050, flags);
  properties[PROP_SUM_START_IDX] =
	g_param_spec_int("sum-start-idx", "Sum Start Idx", "Sum Start Index", -10, 25, 1, flags);
  properties[PROP_AMP_POW_BASE] =
	g_param_spec_float("amp-pow-base", "Amp Power Base", "Amplitude Power Base", -10, 10, 1, flags);
  properties[PROP_AMP_EXP_IDX_MUL] =
	g_param_spec_float("amp-exp-idx-mul", "Amp Exp Idx Mul", "Amplitude Exponent Index Multiplier", -10, 10, 1, flags);
  properties[PROP_AMPFREQ_SCALE_IDX_MUL] =
	g_param_spec_float("ampfreq-scale-idx-mul", "Ampfreq Scale Idx Mul", "Amplitude + Frequency Scale Index Multiplier", -10, 10, 1, flags);
  properties[PROP_AMPFREQ_SCALE_OFFSET] =
	g_param_spec_float("ampfreq-scale-offset", "Ampfreq Scale Offset", "Amplitude + Frequency Scale Offset", -10, 10, 0, flags);
  properties[PROP_AMPFREQ_SCALE_EXP] =
	g_param_spec_float("ampfreq-scale-exp", "Ampfreq Scale Exp", "Amplitude + Frequency Scale Exponent", -10, 1, -1, flags);
  properties[PROP_AMP_BOOST_CENTER] =
	g_param_spec_float("amp-boost-center", "AmpBoost Center", "", 0, 22050, 0, flags);
  properties[PROP_AMP_BOOST_SHARPNESS] =
	g_param_spec_float("amp-boost-sharpness", "AmpBoost Sharpness", "", 0, 200, 0, flags);
  properties[PROP_AMP_BOOST_EXP] =
	g_param_spec_float("amp-boost-exp", "AmpBoost Exp", "", 0, 1024, 2, flags);
  properties[PROP_AMP_BOOST_DB] =
	g_param_spec_float("amp-boost-db", "AmpBoost dB", "", 0, 100, 2, flags);
  properties[PROP_RINGMOD_RATE] =
	g_param_spec_float("ringmod-rate", "Ringmod Rate", "", 0, 100, 0, flags);
  properties[PROP_RINGMOD_DEPTH] =
	g_param_spec_float("ringmod-depth", "Ringmod Depth", "", 0, 0.5, 0, flags);
  properties[PROP_RINGMOD_OT_OFFSET] =
	g_param_spec_float("ringmod-ot-offset", "Ringmod OT Offset", "Ring Modulation Overtone Offset", 0, 1, 0, flags);
  properties[PROP_BEND] =
	g_param_spec_float("bend", "Bend", "", -1000, 1000, 0, flags);
  properties[PROP_VOL] =
	g_param_spec_float("vol", "vol", "", 0, 1, 0.5, flags);
  properties[PROP_NOTE] =
	g_param_spec_enum("note", "Note", "", GSTBT_TYPE_NOTE, GSTBT_NOTE_NONE,
					  G_PARAM_WRITABLE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS);

  for (int i = 1; i < N_PROPERTIES; ++i)
	g_assert(properties[i]);

  g_object_class_install_properties (gobject_class, N_PROPERTIES, properties);

  for (int i = 0; i < sizeof(lut_sin) / sizeof(typeof(lut_sin)); ++i) {
	lut_sin[i] = (gfloat)sin(G_PI * 2 * ((double)i / (sizeof(lut_sin) / sizeof(typeof(lut_sin)))));
  }

  v4sf a = {0,2,4,6};
  v4sf b = {7,5,3,1};
  v4sf c = {7,5,4,6};
  v4sf r = maxf4(a, b);
  v4sf x = bitselect_4f(a > b, a, b);
  g_assert(r[0] == c[0]);
  g_assert(r[1] == c[1]);
  g_assert(r[2] == c[2]);
  g_assert(r[3] == c[3]);
  
  g_assert(x[0] == c[0]);
  g_assert(x[1] == c[1]);
  g_assert(x[2] == c[2]);
  g_assert(x[3] == c[3]);
}
