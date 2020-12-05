#include "envelope.h"
#include <stdio.h>
#include <math.h>

struct _GstBtAdsrClass
{
  GstControlSourceClass parent;
};
  
struct _GstBtAdsr
{
  GstControlSource parent;

  gdouble attack_level;
  gdouble sustain_level;
  gdouble secs_attack;
  gdouble secs_decay;
  gdouble secs_release;

  gdouble on_level;
  gdouble off_level;
  GstClockTime ts_zero_end;
  GstClockTime ts_trigger;
  GstClockTime ts_attack_end;
  GstClockTime ts_decay_end;
  GstClockTime ts_release;
  GstClockTime ts_off_end;
};

G_DEFINE_TYPE(GstBtAdsr, gstbt_adsr, GST_TYPE_CONTROL_SOURCE);

static inline gdouble clamp(gdouble x) {
  return MIN(MAX(x, 0.0), 1.0);
}

static inline gdouble lerp_knee(
  gdouble a, gdouble b, GstClockTime timea, GstClockTime timeb, GstClockTime time, gdouble knee) {
  
  const gdouble alpha = MIN(MAX(((double)(time - timea) / (timeb - timea) - knee),0.0) / (1.0-knee), 1.0);
  
  return a + (b-a) * alpha;
}

static inline gdouble plerp(gdouble a, gdouble b, GstClockTime timea, GstClockTime timeb, GstClockTime time,
							gdouble exp) {
  
  const gdouble alpha = clamp((double)(time - timea) / (timeb - timea));
  return a + (b-a) * pow(alpha, exp);
}

static inline gdouble func_reset(GstBtAdsr* const self, const GstClockTime ts) {
  return plerp(self->on_level, 0.0, self->ts_trigger, self->ts_zero_end, ts, 1.0);
}

static inline gdouble func_attack(GstBtAdsr* const self, const GstClockTime ts) {
  return plerp(0.0, self->attack_level, self->ts_zero_end, self->ts_attack_end, ts, 1.0);
}

static inline gdouble func_decay(GstBtAdsr* const self, const GstClockTime ts) {
  return plerp(self->attack_level, self->sustain_level, self->ts_attack_end, self->ts_decay_end, ts, 1.0);
}

static inline gdouble func_sustain(GstBtAdsr* const self) {
  return self->sustain_level;
}

static inline gdouble func_release(GstBtAdsr* const self, const GstClockTime ts) {
  return plerp(self->off_level, 0.0, self->ts_release, self->ts_off_end, ts, 1.0);
}

static inline gdouble get_value_inline(GstBtAdsr* const self, const GstClockTime ts) {
  const double knee = 0.99;

  return lerp_knee(
	func_reset(self, ts),
	lerp_knee(
	  func_attack(self, ts),
	  lerp_knee(
		func_decay(self, ts),
		lerp_knee(
		  func_sustain(self),
		  func_release(self, ts),
		  self->ts_decay_end,
		  self->ts_release,
		  ts,
		  knee),
		self->ts_attack_end,
		self->ts_decay_end,
		ts,
		knee),
	  self->ts_zero_end,
	  self->ts_attack_end,
	  ts,
	  knee),
	self->ts_trigger,
	self->ts_zero_end,
	ts,
	knee);
}

static gboolean get_value(GstControlSource* self, GstClockTime timestamp, gdouble* value) {
  *value = get_value_inline((GstBtAdsr*)self, timestamp);
  return TRUE;
}

static gboolean get_value_array(GstControlSource* self, GstClockTime timestamp, GstClockTime interval,
								guint n_values, gdouble* values) {
  for (guint i = 0; i < n_values; ++i) {
	values[i] = get_value_inline((GstBtAdsr*)self, timestamp);
	timestamp += interval;
  }
  return TRUE;
}

void gstbt_adsr_class_init(GstBtAdsrClass* const self) {
}

void gstbt_adsr_init(GstBtAdsr* const self) {
  self->parent.get_value = get_value;
  self->parent.get_value_array = get_value_array;
  
  self->attack_level = 1.0;
  self->sustain_level = 0.51;
  self->secs_attack = 0.5;//01;
  self->secs_decay = 0.25;
  self->secs_release = 0.5;
}

void gstbt_adsr_trigger(GstBtAdsr* const self, const GstClockTime time) {
  const gboolean envelope_never_triggered = self->ts_trigger == 0;
  
  if (envelope_never_triggered) {
	self->ts_zero_end = time;
  } else {
	get_value((GstControlSource*)self, time, &self->on_level);
	self->ts_zero_end = time + (GstClockTime)(self->on_level * 0.05 * GST_SECOND);
  }
  
  self->ts_trigger = time;
  self->ts_attack_end = self->ts_zero_end + (GstClockTime)(self->secs_attack * GST_SECOND);
  self->ts_decay_end = self->ts_attack_end + (GstClockTime)(self->secs_decay * GST_SECOND);
  self->ts_release = ULONG_MAX;
  self->ts_off_end = ULONG_MAX;
}

void gstbt_adsr_off(GstBtAdsr* const self, const GstClockTime time) {
  get_value((GstControlSource*)self, time, &self->off_level);
  self->ts_attack_end = MIN(self->ts_attack_end, time);
  self->ts_decay_end = MIN(self->ts_decay_end, time);
  self->ts_release = time;
  self->ts_off_end = self->ts_release + (GstClockTime)(self->secs_release * GST_SECOND);
}
