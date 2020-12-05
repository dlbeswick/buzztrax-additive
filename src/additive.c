#include "config.h"
//#include "src/genums.h"
//#include "src/generated/generated-genums.h"

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
#include "sse_mathfun.h"

GType gstbt_additive_get_type(void);
#define GSTBT_ADDITIVE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),gstbt_additive_get_type(),GstBtAdditive))

#define GST_MACHINE_NAME "additive"
#define GST_MACHINE_DESC "Controller for real Alpha Juno synth via MIDI"

#define GST_CAT_DEFAULT additive_debug
GST_DEBUG_CATEGORY_STATIC(GST_CAT_DEFAULT);

typedef gdouble v4sd __attribute__ ((vector_size (16)));
typedef gint v4si __attribute__ ((vector_size (16)));
typedef guint v4ui __attribute__ ((vector_size (16)));
typedef gint16 v4ss __attribute__ ((vector_size (8)));

const float FPI = (gfloat)G_PI;
const float F2PI = (gfloat)(2*G_PI);

static gfloat lut_sin[1024];

// libmvec
// https://stackoverflow.com/questions/40475140/mathematical-functions-for-simd-registers
v4sf _ZGVbN4vv_powf(v4sf x, v4sf y);

enum {
  MAX_VOICES = 6,
  MAX_OVERTONES = 600
};

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
  gfloat attack;
  gfloat decay;
  GstBtNote note;

  gfloat ringmod_ot_offset_calc;
  gfloat accum_rads;
  GstBtToneConversion* tones;
  gfloat* buf;
  gdouble* buf_srate;
  StateOvertone* states_overtone;
  GstTimedValueControlSource* cs;
  GstControlBinding* cb;
  GstClockTime running_time_last;

  gint calls;
  long time_accum;
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
  PROP_ATTACK,
  PROP_DECAY,
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
  case PROP_NOTE: {
	gdouble base = 0.0;
	gst_control_source_get_value((GstControlSource*)self->cs, self->parent.running_time, &base);
	
	GstBtNote note = g_value_get_enum(value);
	if (note == GSTBT_NOTE_OFF) {
	  gst_timed_value_control_source_unset_all(self->cs);
	  gst_timed_value_control_source_set(self->cs, self->parent.running_time, base);
	  gst_timed_value_control_source_set(self->cs, self->parent.running_time + self->decay * GST_SECOND, 0.0);
	} else if (note != GSTBT_NOTE_NONE) {
	  self->note = note;
	  
	  const gdouble noclick = base * 0.05;
	  
	  gst_timed_value_control_source_unset_all(self->cs);
	  gst_timed_value_control_source_set(self->cs, self->parent.running_time, base);
	  gst_timed_value_control_source_set(
		self->cs, self->parent.running_time + noclick * GST_SECOND, 0.0);
	  gst_timed_value_control_source_set(
		self->cs, self->parent.running_time + (noclick + self->attack) * GST_SECOND, 1.0);
	  gst_timed_value_control_source_set(
		self->cs, self->parent.running_time + (noclick + self->attack + self->decay) * GST_SECOND, 0.0);
	}
	break;
  }
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
  case PROP_ATTACK:
	self->attack = g_value_get_float(value);
	break;
  case PROP_DECAY:
	self->decay = g_value_get_float(value);
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
  case PROP_ATTACK:
	g_value_set_float(value, self->attack);
	break;
  case PROP_DECAY:
	g_value_set_float(value, self->decay);
	break;
  default:
	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	break;
  }
}

static inline v4sf lerp_ps(const v4sf a, const v4sf b, const v4sf alpha) {
  return a + (b-a) * alpha;
}

// Domain: [0, 683565275.5764316]
// No performance benefit was recorded compared to fast sin.
// LUT size doesn't seem to affect performance.
static inline v4sf sin_ps_lut(const v4sf x) {
  const gfloat* const lut = (const gfloat*)lut_sin;
  const v4sf x_scale = x/F2PI * (sizeof(lut_sin) / sizeof(gfloat));
  const v4ui whole = __builtin_convertvector(x_scale, v4ui);
  const v4sf frac = x_scale - __builtin_convertvector(whole, v4sf);
  const v4ui idxa = whole & ((sizeof(lut_sin) / sizeof(gfloat))-1);
  const v4ui idxb = (idxa+1) & ((sizeof(lut_sin) / sizeof(gfloat))-1);
  const v4sf a = {lut[idxa[0]], lut[idxa[1]], lut[idxa[2]], lut[idxa[3]]};
  const v4sf b = {lut[idxb[0]], lut[idxb[1]], lut[idxb[2]], lut[idxb[3]]};

  return a + (b-a) * frac;
}

static inline v4sf sin_ps_method(const v4sf x) {
  return sin_ps(x);
}

// Return a sin with range 0 -> 1
static inline v4sf sin01_ps(const v4sf x) {
  return (1.0f + sin_ps_method(x)) * 0.5f;
}

static inline v4sf pow_posexp_ps(const v4sf x, const v4sf vexp) {
  return exp_ps(vexp*log_ps(x));
}

// Take a sin with range 0 -> 1 and exponentiate to 'vexp' power
// Return the result normalized back to -1 -> 1 range.
// A way of waveshaping on non-odd powers, I suppose.
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

static gboolean _process (GstBtAudioSynth* synth, GstBuffer* gstbuf, GstMapInfo* info) {
  struct timespec clock_start;
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &clock_start);
  
  GstBtAdditive * const self = GSTBT_ADDITIVE(synth);
  const gfloat rate = (gfloat)GSTBT_AUDIO_SYNTH(self)->info.rate;

  /*  for (int i = 0; i < self->cntVoices; ++i) {
	gstbt_additivev_process(self->voices[i], gstbuf);
	}*/

  self->running_time_last = synth->running_time;
  
  gfloat* const buf = self->buf;
  v4sf* const buff = (v4sf*)self->buf;
  const int nbufelements = synth->generate_samples_per_buffer;
  const int nbuffelements = synth->generate_samples_per_buffer/4;
  
  memset(buf, 0, nbufelements*sizeof(gfloat));

  const gfloat freq_note = (gfloat)gstbt_tone_conversion_translate_from_number(self->tones, self->note);
  const gfloat freq = freq_note + freq_note * self->bend;

  const gfloat freq_top =
	freq * self->ampfreq_scale_idx_mul * (1+(gfloat)self->overtones) + self->ampfreq_scale_offset;

  gint max_overtones;
  if (freq_top > rate/2) {
	max_overtones = (gint)((rate/2 - self->ampfreq_scale_offset) / freq / self->ampfreq_scale_idx_mul - 1);
  } else {
	max_overtones = (gint)self->overtones;
  }
	
  for (int j = self->sum_start_idx, idx_o = 0; j != self->sum_start_idx + 1 + max_overtones; ++j, ++idx_o) {
	const gfloat hscale_freq = self->ampfreq_scale_idx_mul * (gfloat)j + self->ampfreq_scale_offset;
	if (hscale_freq <= 0)
	  continue;
	g_assert(idx_o < MAX_OVERTONES);

	StateOvertone* const overtone = &self->states_overtone[idx_o];
	
	const gfloat hscale_amp =
	  powf(self->amp_pow_base, (gfloat)j * self->amp_exp_idx_mul) *
	  powf(hscale_freq, self->ampfreq_scale_exp);

	const gfloat time_to_rads = F2PI * freq * hscale_freq;
	const gfloat inc = time_to_rads * (1.0f/rate);
	  
	if (self->ringmod_depth > 0) {
	  const gfloat inc_rm = time_to_rads * self->ringmod_depth * (1.0f/rate);

	  const v4sf f = {inc, inc * 2, inc * 3, inc * 4};
	  const v4sf f_rm = {inc_rm, inc_rm * 2, inc_rm * 3, inc_rm * 4};
	  const v4sf v_exp = {self->ringmod_rate, self->ringmod_rate, self->ringmod_rate, self->ringmod_rate};
	  for (int i = 0; i < nbuffelements; ++i) {
		buff[i] += hscale_amp *
		  sin_ps_method(overtone->accum_rads + f) *
		  powsin_ps(overtone->accum_rm_rads + f_rm, v_exp)
		  ;
		overtone->accum_rads += f[3];
		overtone->accum_rm_rads += f_rm[3];
	  }
	  overtone->accum_rm_rads = fmodf(overtone->accum_rm_rads, F2PI);
	} else {
	  const gfloat inc4 = inc * 4;
	  const v4sf f = {inc, inc * 2, inc * 3, inc * 4};
	  for (int i = 0; i < nbuffelements; ++i) {
		const v4sf ff = f + overtone->accum_rads;
		buff[i] += hscale_amp * sin_ps_method(ff);
		overtone->accum_rads += inc4;
	  }
	}
	overtone->accum_rads = fmodf(overtone->accum_rads, F2PI);
  }

  gst_control_source_get_value_array(
	(GstControlSource*)self->cs,
	synth->running_time,
	1e9L / synth->info.rate,
	nbufelements,
	self->buf_srate);
  
  const gfloat fscale = 32768.0f * self->vol;
  for (int i = 0; i < nbufelements; ++i)
	((guint16*)info->data)[i] = buf[i] * fscale * self->buf_srate[i];
  
  struct timespec clock_end;
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &clock_end);
  self->time_accum += (clock_end.tv_sec - clock_start.tv_sec) * 1e9L +  (clock_end.tv_nsec - clock_start.tv_nsec);
  if (++self->calls == 250) {
	GST_INFO("Avg perf: %f calls/sec\n", 1.0f / (self->time_accum / 1e9f / self->calls));
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

static void _dispose (GObject* object) {
  GstBtAdditive* self = GSTBT_ADDITIVE(object);
  g_clear_object(&self->tones);
  g_clear_object(&self->cs);
  g_clear_object(&self->cb);
  g_clear_pointer(&self->buf, free);
  g_clear_pointer(&self->buf_srate, free);
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
  properties[PROP_ATTACK] =
	g_param_spec_float("attack", "attack", "", 0.0001, 30, 2, flags);
  properties[PROP_DECAY] =
	g_param_spec_float("decay", "decay", "", 0.0001, 30, 2, flags);
  properties[PROP_NOTE] =
	g_param_spec_enum("note", "Note", "", GSTBT_TYPE_NOTE, GSTBT_NOTE_NONE,
					  G_PARAM_WRITABLE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS);

  for (int i = 1; i < N_PROPERTIES; ++i)
	g_assert(properties[i]);
  
  g_object_class_install_properties (gobject_class, N_PROPERTIES, properties);

  for (int i = 0; i < sizeof(lut_sin) / sizeof(gfloat); ++i) {
	lut_sin[i] = (gfloat)sin(G_PI * 2 * ((double)i / (sizeof(lut_sin) / sizeof(gfloat))));
  }
}

static void gstbt_additive_init (GstBtAdditive* const self) {
  self->tones = gstbt_tone_conversion_new(GSTBT_TONE_CONVERSION_EQUAL_TEMPERAMENT);
  self->buf = g_malloc(sizeof(gfloat)*self->parent.generate_samples_per_buffer);
  self->buf_srate = g_malloc(sizeof(gdouble)*self->parent.generate_samples_per_buffer);

  self->states_overtone = g_malloc(sizeof(StateOvertone) * MAX_OVERTONES);

  self->cs = g_object_new(GST_TYPE_INTERPOLATION_CONTROL_SOURCE,
						  "mode", GST_INTERPOLATION_MODE_CUBIC_MONOTONIC,
						  NULL);

  self->cb = (GstControlBinding*)gst_direct_control_binding_new(
	(GstObject*)self,
	properties[PROP_VOL]->name,
	(GstControlSource*)self->cs);
  
  /*
  
  for (int i = 0; i < MAX_VOICES; i++) {
	self->voices[i] = gstbt_additivev_new(i);

	char name[7];
	snprintf(name, sizeof(name), "voice%1d", i);
		
	gst_object_set_name((GstObject *)self->voices[i], name);
	gst_object_set_parent((GstObject *)self->voices[i], (GstObject *)self);
	}*/
}
