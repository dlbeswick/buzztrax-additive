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

#include "src/envelope.h"
#include "src/math.h"
#include "src/properties_simple.h"
#include <gst/gstparamspecs.h>
#include <stdio.h>
#include <math.h>

struct _GstBtAdsrClass {
  GstControlSourceClass parent;
};
  
struct _GstBtAdsr {
  GstControlSource parent;

  const char* postfix;
  
  gfloat attack_level;
  gfloat attack_secs;
  gfloat attack_pow;
  gfloat sustain_level;
  gfloat decay_secs;
  gfloat decay_pow;
  gfloat release_secs;
  gfloat release_pow;

  gfloat on_level;
  gfloat off_level;
  GstClockTime ts_zero_end;
  GstClockTime ts_trigger;
  GstClockTime ts_attack_end;
  GstClockTime ts_decay_end;
  GstClockTime ts_release;
  GstClockTime ts_off_end;

  BtPropertiesSimple* props;
};

G_DEFINE_TYPE(GstBtAdsr, gstbt_adsr, GST_TYPE_CONTROL_SOURCE);

static inline gfloat ab_select(gfloat a, gfloat b, GstClockTime timeb, GstClockTime time) {
  return bitselect_f(time < timeb, a, b);
}

static inline gfloat lerp_knee(gfloat a, gfloat b,
							   GstClockTime timea, GstClockTime timeb,
							   GstClockTime time,
							   gfloat knee) {
  const gfloat alpha = MIN(1.0, MAX(0.0, ((float)(time - timea) / (timeb - timea) - knee)) / (1.0-knee));
  
  return a + (b-a) * alpha;
}

static inline gfloat plerp(gfloat a, gfloat b,
						   GstClockTime timea, GstClockTime timeb,
						   GstClockTime time,
						   gfloat exp) {
  
  const gfloat alpha = clamp((float)(time - timea) / (timeb - timea), 0.0f, 1.0f);
  return a + (b-a) * pow(alpha, exp);
}

static inline gfloat func_reset(GstBtAdsr* const self, const GstClockTime ts) {
  return plerp(self->on_level, 0.0, self->ts_trigger, self->ts_zero_end, ts, 1.0);
}

static inline gfloat func_attack(GstBtAdsr* const self, const GstClockTime ts) {
  return plerp(0.0, self->attack_level, self->ts_zero_end, self->ts_attack_end, ts, self->attack_pow);
}

static inline gfloat func_decay(GstBtAdsr* const self, const GstClockTime ts) {
  return plerp(self->attack_level, self->sustain_level, self->ts_attack_end, self->ts_decay_end, ts, self->decay_pow);
}

static inline gfloat func_sustain(GstBtAdsr* const self) {
  return self->sustain_level;
}

static inline gfloat func_release(GstBtAdsr* const self, const GstClockTime ts) {
  return plerp(self->off_level, 0.0, self->ts_release, self->ts_off_end, ts, self->release_pow);
}

static inline gfloat get_value_inline(GstBtAdsr* const self, const GstClockTime ts) {
  return ab_select(
	func_reset(self, ts),
	ab_select(
	  func_attack(self, ts),
	  ab_select(
		func_decay(self, ts),
		ab_select(
		  func_sustain(self),
		  func_release(self, ts),
		  self->ts_release,
		  ts),
		self->ts_decay_end,
		ts),
	  self->ts_attack_end,
	  ts),
	self->ts_zero_end,
	ts);
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

gfloat gstbt_adsr_get_value_f(GstBtAdsr* self, GstClockTime timestamp, gfloat* value) {
  return get_value_inline(self, timestamp);
}

void gstbt_adsr_get_value_array_f(GstBtAdsr* self, GstClockTime timestamp, GstClockTime interval,
								  guint n_values, gfloat* values) {
  for (guint i = 0; i < n_values; ++i) {
	values[i] = get_value_inline(self, timestamp);
	timestamp += interval;
  }
}

gboolean gstbt_adsr_mod_value_array_f(GstBtAdsr* self, GstClockTime timestamp, GstClockTime interval,
									  guint n_values, gfloat* values) {
  gfloat accum = 0;
  for (guint i = 0; i < n_values; ++i) {
	const gfloat val = get_value_inline(self, timestamp);
	values[i] *= val;
	accum += val;
	timestamp += interval;
  }
  return accum != 0;
}

gboolean gstbt_adsr_property_set(GObject* obj, guint prop_id, const GValue* value, GParamSpec* pspec) {
  return bt_properties_simple_set(((GstBtAdsr*)obj)->props, pspec, value);
}

gboolean gstbt_adsr_property_get(GObject* obj, guint prop_id, GValue* value, GParamSpec* pspec) {
  return bt_properties_simple_get(((GstBtAdsr*)obj)->props, pspec, value);
}

static void dispose(GObject* obj) {
  g_clear_object(&((GstBtAdsr*)obj)->props);
}

void gstbt_adsr_class_init(GstBtAdsrClass* const klass) {
  GObjectClass* const gobject_class = (GObjectClass *) klass;
  
  gobject_class->dispose = dispose;
  
//  g_param_spec_uint("overtones", "Overtones", "", 0, MAX_OVERTONES, 10, flags);
//	g_param_spec_float("ringmod-ot-offset", "Ringmod OT Offset", "Ring Modulation Overtone Offset", 0, 1, 0, flags);
}

void gstbt_adsr_props_add(GObjectClass* const klass, const char* postfix, guint* idx) {
  const GParamFlags flags =
	(GParamFlags)(G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE);
  
  GString* str = g_string_new("");
  
  str = g_string_assign(str, "attack-level");
  g_string_append(str, postfix);
  g_object_class_install_property(
	klass,
	(*idx)++,
	g_param_spec_float(str->str, str->str, "", 0, 1, 1, flags));

  str = g_string_assign(str, "attack-secs");
  g_string_append(str, postfix);
  g_object_class_install_property(
	klass,
	(*idx)++,
	g_param_spec_float(str->str, str->str, "", 0, 20, 0.5, flags));
  
  str = g_string_assign(str, "attack-pow");
  g_string_append(str, postfix);
  g_object_class_install_property(
	klass,
	(*idx)++,
	g_param_spec_float(str->str, str->str, "", 0, 10, 1, flags));
  
  str = g_string_assign(str, "sustain-level");
  g_string_append(str, postfix);
  g_object_class_install_property(
	klass,
	(*idx)++,
	g_param_spec_float(str->str, str->str, "", 0, 1, 0.75, flags));
  
  str = g_string_assign(str, "decay-secs");
  g_string_append(str, postfix);
  g_object_class_install_property(
	klass,
	(*idx)++,
	g_param_spec_float(str->str, str->str, "", 0, 20, 1, flags));
  
  str = g_string_assign(str, "decay-pow");
  g_string_append(str, postfix);
  g_object_class_install_property(
	klass,
	(*idx)++,
	g_param_spec_float(str->str, str->str, "", 0, 10, 1, flags));
  
  str = g_string_assign(str, "release-secs");
  g_string_append(str, postfix);
  g_object_class_install_property(
	klass,
	(*idx)++,
	g_param_spec_float(str->str, str->str, "", 0, 20, 0.5, flags));
  
  str = g_string_assign(str, "release-pow");
  g_string_append(str, postfix);
  g_object_class_install_property(
	klass,
	(*idx)++,
	g_param_spec_float(str->str, str->str, "", 0, 10, 1, flags));
  
  g_string_free(str, TRUE);
}

void gstbt_adsr_init(GstBtAdsr* const self) {
  self->parent.get_value = get_value;
  self->parent.get_value_array = get_value_array;
}

void gstbt_adsr_trigger(GstBtAdsr* const self, const GstClockTime time) {
  const gboolean envelope_never_triggered = self->ts_trigger == 0;
  
  if (envelope_never_triggered) {
	self->ts_zero_end = time;
  } else {
	gstbt_adsr_get_value_f(self, time, &self->on_level);
	self->ts_zero_end = time + (GstClockTime)(self->on_level * 0.05 * GST_SECOND);
  }
  
  self->ts_trigger = time;
  self->ts_attack_end = self->ts_zero_end + (GstClockTime)(self->attack_secs * GST_SECOND);
  self->ts_decay_end = self->ts_attack_end + (GstClockTime)(self->decay_secs * GST_SECOND);
  self->ts_release = ULONG_MAX;
  self->ts_off_end = ULONG_MAX;
}

void gstbt_adsr_off(GstBtAdsr* const self, const GstClockTime time) {
  gstbt_adsr_get_value_f(self, time, &self->off_level);
  self->ts_attack_end = MIN(self->ts_attack_end, time);
  self->ts_decay_end = MIN(self->ts_decay_end, time);
  self->ts_release = time;
  self->ts_off_end = self->ts_release + (GstClockTime)(self->release_secs * GST_SECOND);
}

void prop_add(GstBtAdsr* const self, const char* name, void* var) {
  GString* str = g_string_new(name);
  g_string_append(str, self->postfix);
  bt_properties_simple_add(self->props, str->str, var);
  g_string_free(str, TRUE);
}

GstBtAdsr* gstbt_adsr_new(GObject* owner, const char* property_postfix) {
  GstBtAdsr* result = g_object_new(gstbt_adsr_get_type(), NULL);
  result->props = bt_properties_simple_new(owner);
  result->postfix = property_postfix;
  prop_add(result, "attack-level", &result->attack_level);
  prop_add(result, "attack-secs", &result->attack_secs);
  prop_add(result, "attack-pow", &result->attack_pow);
  prop_add(result, "sustain-level", &result->sustain_level);
  prop_add(result, "decay-secs", &result->decay_secs);
  prop_add(result, "decay-pow", &result->decay_pow);
  prop_add(result, "release-secs", &result->release_secs);
  prop_add(result, "release-pow", &result->release_pow);
  return result;
}
