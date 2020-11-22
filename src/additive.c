#include "config.h"
//#include "src/genums.h"
//#include "src/generated/generated-genums.h"

#include "libbuzztrax-gst/audiosynth.h"
#include "libbuzztrax-gst/childbin.h"
#include "libbuzztrax-gst/musicenums.h"
#include "libbuzztrax-gst/propertymeta.h"
#include "libbuzztrax-gst/toneconversion.h"
#include <stdio.h>
#include <fcntl.h>
#include <math.h>
#include <unistd.h>

GType gstbt_additive_get_type(void);
#define GSTBT_ADDITIVE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),gstbt_additive_get_type(),GstBtAdditive))

#define GST_MACHINE_NAME "additive"
#define GST_MACHINE_DESC "Controller for real Alpha Juno synth via MIDI"

#define G_FPI (gfloat)G_PI

GST_DEBUG_CATEGORY (additive_debug);

enum {
  MAX_VOICES = 6,
  MAX_OVERTONES = 600
};

typedef struct {
  gfloat accum;
  gfloat accum_rm;
} StateOvertone;

typedef struct
{
  GstBtAudioSynthClass parent_class;
} GstBtAdditiveClass;

// Class instance data.
typedef struct
{
  GstBtAudioSynth parent;

  guint overtones;
  gint sum_start_idx;
  gfloat amp_pow_base;
  gfloat amp_exp_idx_mul;
  gfloat ampfreq_scale_idx_mul;
  gfloat ampfreq_scale_offset;
  gfloat ampfreq_scale_exp;
  
  gfloat bend;
  gfloat ringmod_rate;
  gfloat ringmod_depth;
  gfloat ringmod_ot_offset;
  gfloat vol;
  GstBtNote note;

  gfloat ringmod_ot_offset_calc;
  gdouble accum;
  GstBtToneConversion* tones;
  gfloat* buf;
  StateOvertone* states_overtone;
} GstBtAdditive;

enum
{
  PROP_CHILDREN = 1,
  PROP_OVERTONES,
  PROP_SUM_START_IDX,
  PROP_AMP_POW_BASE,
  PROP_AMP_EXP_IDX_MUL,
  PROP_AMPFREQ_SCALE_IDX_MUL,
  PROP_AMPFREQ_SCALE_OFFSET,
  PROP_AMPFREQ_SCALE_EXP,
  PROP_RINGMOD_RATE,
  PROP_RINGMOD_DEPTH,
  PROP_RINGMOD_OT_OFFSET,
  PROP_BEND,
  PROP_VOL,
  PROP_NOTE,
  N_PROPERTIES
};
static GParamSpec *properties[N_PROPERTIES] = { NULL, };



static GObject *gstbt_additive_child_proxy_get_child_by_index (GstChildProxy *child_proxy, guint index) {
  //GstBtAdditive *self = GSTBT_ADDITIVE(child_proxy);

  g_return_val_if_fail(index < MAX_VOICES, NULL);

  return NULL;//(GObject *)gst_object_ref(self->voices[index]);
}

static guint gstbt_additive_child_proxy_get_children_count (GstChildProxy *child_proxy) {
  //GstBtAdditive *self = GSTBT_ADDITIVE(child_proxy);
  return 0;//self->cntVoices;
}

static void gstbt_additive_child_proxy_interface_init (gpointer g_iface, gpointer iface_data) {
  GstChildProxyInterface *iface = (GstChildProxyInterface *)g_iface;

  GST_INFO("initializing iface");

  iface->get_child_by_index = gstbt_additive_child_proxy_get_child_by_index;
  iface->get_children_count = gstbt_additive_child_proxy_get_children_count;
}

//-- the class
G_DEFINE_TYPE_WITH_CODE (
  GstBtAdditive,
  gstbt_additive,
  GSTBT_TYPE_AUDIO_SYNTH,
  G_IMPLEMENT_INTERFACE (GST_TYPE_CHILD_PROXY, gstbt_additive_child_proxy_interface_init)
  G_IMPLEMENT_INTERFACE (GSTBT_TYPE_CHILD_BIN, NULL))

static gboolean plugin_init(GstPlugin * plugin) {
  GST_DEBUG_CATEGORY_INIT(
	GST_CAT_DEFAULT,
	GST_MACHINE_NAME,
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
	//self->cntVoices = g_value_get_ulong(value);
	break;
  case PROP_OVERTONES:
	self->overtones = g_value_get_uint(value);
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
  case PROP_RINGMOD_RATE:
	self->ringmod_rate = g_value_get_float(value);
	break;
  case PROP_RINGMOD_DEPTH:
	self->ringmod_depth = g_value_get_float(value);
	break;
  case PROP_RINGMOD_OT_OFFSET:
	self->ringmod_ot_offset = g_value_get_float(value);
	self->ringmod_ot_offset_calc = self->ringmod_ot_offset * 2 * G_FPI;
	break;
  case PROP_BEND:
	self->bend = g_value_get_float(value);
	break;
  case PROP_VOL:
	self->vol = g_value_get_float(value);
	break;
  case PROP_NOTE: {
	GstBtNote note = g_value_get_enum(value);
	if (note != GSTBT_NOTE_NONE)
		self->note = note;
	break;
  }
  default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	break;
  }
}

static void _get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec) {
  GstBtAdditive *self = GSTBT_ADDITIVE (object);

  switch (prop_id) {
  case PROP_CHILDREN:
	//self->cntVoices = g_value_get_ulong(value);
	break;
  case PROP_OVERTONES:
	g_value_set_uint(value, (guint)self->overtones);
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

static inline gfloat sin01(gfloat x) {
  return (1.0f + sinf(x)) * 0.5f;
}

static inline gfloat powsin(gfloat x, gfloat exp) {
  return powf(sin01(x), exp) - 0.5f;
}

static gboolean _process (GstBtAudioSynth* synth, GstBuffer* gstbuf, GstMapInfo* info) {
  GstBtAdditive * const self = GSTBT_ADDITIVE(synth);
  
  /*  for (int i = 0; i < self->cntVoices; ++i) {
	gstbt_additivev_process(self->voices[i], gstbuf);
	}*/

  gfloat* const buf = self->buf;
  
  memset(buf, 0, synth->generate_samples_per_buffer*sizeof(gfloat));
  
  if (self->note != GSTBT_NOTE_OFF) {
	const gfloat rate = (gfloat)GSTBT_AUDIO_SYNTH(self)->info.rate;
	const gfloat freq = (gfloat)gstbt_tone_conversion_translate_from_number(self->tones, self->note);

	const gfloat freq_top =
	  freq * self->ampfreq_scale_idx_mul * (1+(gfloat)self->overtones) + self->ampfreq_scale_offset;
	
	gint max_overtones;
	if (freq_top > rate/2) {
	  max_overtones = (gint)((rate/2 - self->ampfreq_scale_offset) / freq / self->ampfreq_scale_idx_mul - 1);
	} else {
	  max_overtones = (gint)self->overtones;
	}
	
	for (int j = self->sum_start_idx, idx_o = 0; j != self->sum_start_idx + 1 + max_overtones; ++j, ++idx_o) {
	  const gdouble hscale_freq = self->ampfreq_scale_idx_mul * (gfloat)j + self->ampfreq_scale_offset;
	  if (hscale_freq <= 0)
		continue;

	  StateOvertone* const overtone = &self->states_overtone[idx_o];
	  
	  const gfloat hscale_amp =
		powf(self->amp_pow_base, (gfloat)j * self->amp_exp_idx_mul) *
		powf(hscale_freq, self->ampfreq_scale_exp);
	  
	  const gdouble inc = 2.0 * G_PI * (1.0/rate) * freq * hscale_freq;
	  
	  if (self->ringmod_depth > 0) {
		const gdouble inc_rm = inc * self->ringmod_depth;
		
		for (int i = 0; i < synth->generate_samples_per_buffer; ++i) {
		  buf[i] +=
			hscale_amp * (
			  sinf(overtone->accum) *
			  powsin(overtone->accum_rm, self->ringmod_rate)
//			  powsin(hscale_freq * freq * self->ringmod_depth * (t + self->ringmod_ot_offset_calc), self->ringmod_rate)
			);
		  overtone->accum += inc;
		  overtone->accum_rm += inc_rm;
		}
	  } else {
		for (int i = 0; i < synth->generate_samples_per_buffer; ++i) {
		  buf[i] += hscale_amp * sinf(overtone->accum);
		  overtone->accum += inc;
		}
	  }
	  
	  overtone->accum = fmod(overtone->accum, 2 * G_PI);
	  overtone->accum_rm = fmod(overtone->accum_rm, 2 * G_PI);
	}

	//self->accum = fmod(self->accum + (2.0 * G_PI * (1.0/rate)) * synth->generate_samples_per_buffer, 2 * G_PI);
  }

  const float fscale = 32768.0f * self->vol;
  gint16* const out = (gint16*)info->data;
  for (int i = 0; i < synth->generate_samples_per_buffer; ++i)
	out[i] = (gint16)(buf[i] * fscale);
  
  return TRUE;
}

static void _negotiate (GstBtAudioSynth* base, GstCaps* caps) {
  for (guint i = 0; i < gst_caps_get_size(caps); ++i) {
	GstStructure* const s = gst_caps_get_structure(caps, i);
	
    gst_structure_fixate_field_nearest_int(s, "channels", 1);

	GST_LOG("caps structure %d: %" GST_PTR_FORMAT, i, (void*)s);
  }
}

static void _dispose (GObject* object) {
  GstBtAdditive* self = GSTBT_ADDITIVE(object);
  g_clear_object(&self->tones);
  g_clear_pointer(&self->buf, g_free);
  g_free(self->states_overtone);
  
  // It's necessary to unparent children so they will be unreffed and cleaned up. GstObject doesn't hold variable
  // links to its children, so wouldn't know to unparent them.
  /*
  for (int i = 0; i < MAX_VOICES; i++) {
	gst_object_unparent((GstObject*)self->voices[i]);
  }
  */
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
	"children",
	"",
	"",
	0/*1*/, MAX_VOICES, 1,
	G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  // Instance properties
  properties[PROP_OVERTONES] =
	g_param_spec_uint("overtones", "Overtones", "", 0, MAX_OVERTONES, 10, flags);
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
  properties[PROP_RINGMOD_RATE] =
	g_param_spec_float("ringmod-rate", "Ringmod Rate", "", 0, 100, 0, flags);
  properties[PROP_RINGMOD_DEPTH] =
	g_param_spec_float("ringmod-depth", "Ringmod Depth", "", 0, 0.5, 0, flags);
  properties[PROP_RINGMOD_OT_OFFSET] =
	g_param_spec_float("ringmod-ot-offset", "Ringmod OT Offset", "Ring Modulation Overtone Offset", 0, 1, 0, flags);
  properties[PROP_BEND] =
	g_param_spec_float("bend", "Bend", "", -1, 1, 0, flags);
  properties[PROP_VOL] =
	g_param_spec_float("vol", "vol", "", 0, 1, 0.5, flags);
  properties[PROP_NOTE] =
	g_param_spec_enum("note", "Note", "", GSTBT_TYPE_NOTE, GSTBT_NOTE_NONE,
					  G_PARAM_WRITABLE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS);

  for (int i = 1; i < N_PROPERTIES; ++i)
	g_assert(properties[i]);
  
  g_object_class_install_properties (gobject_class, N_PROPERTIES, properties);
}

static void gstbt_additive_init (GstBtAdditive* const self) {
  self->tones = gstbt_tone_conversion_new(GSTBT_TONE_CONVERSION_EQUAL_TEMPERAMENT);
  self->buf = g_malloc(sizeof(gfloat)*self->parent.generate_samples_per_buffer);

  self->states_overtone = g_malloc(sizeof(StateOvertone) * MAX_OVERTONES);

  /*
  
  for (int i = 0; i < MAX_VOICES; i++) {
	self->voices[i] = gstbt_additivev_new(i);

	char name[7];
	snprintf(name, sizeof(name), "voice%1d", i);
		
	gst_object_set_name((GstObject *)self->voices[i], name);
	gst_object_set_parent((GstObject *)self->voices[i], (GstObject *)self);
	}*/
}