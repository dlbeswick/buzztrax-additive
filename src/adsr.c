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

#include "src/adsr.h"
#include "src/debug.h"
#include "src/propsratecontrolsource.h"
#include "src/math.h"
#include "src/properties_simple.h"
#include <gst/gstparamspecs.h>
#include <stdio.h>
#include <math.h>

struct _GstBtAdsrClass {
  GstBtPropSrateControlSource parent;
};
  
struct _GstBtAdsr {
  GstBtPropSrateControlSource parent;

  const char* postfix;
  
  gfloat attack_level;
  gfloat attack_secs;
  gfloat attack_pow;
  gfloat sustain_level;
  gfloat decay_secs;
  gfloat decay_pow;
  gfloat release_secs;
  gfloat release_pow;
  gboolean auto_release;

  gfloat on_level;
  gfloat off_level;
  GstClockTime ts_zero_end;
  GstClockTime ts_trigger;
  GstClockTime ts_attack_end;
  GstClockTime ts_decay_end;
  GstClockTime ts_release;
  GstClockTime ts_off_end;
  gboolean released;

  v4ui ts_zero_end4;
  v4ui ts_trigger4;
  v4ui ts_attack_end4;
  v4ui ts_decay_end4;
  v4ui ts_release4;
  v4ui ts_off_end4;
  
  BtPropertiesSimple* props;
};

G_DEFINE_TYPE(GstBtAdsr, gstbt_adsr, gstbt_prop_srate_cs_get_type());


static inline v4sf ab_select4(v4sf a, v4sf b, v4ui timeb, v4ui time) {
  return bitselect4f(time < timeb, a, b);
}

static inline gfloat lerp_knee(gfloat a, gfloat b,
							   GstClockTime timea, GstClockTime timeb,
							   GstClockTime time,
							   gfloat knee) {
  const gfloat alpha = MIN(1.0, MAX(0.0, ((float)(time - timea) / (timeb - timea) - knee)) / (1.0-knee));
  
  return a + (b-a) * alpha;
}

static inline v4sf plerp4(v4sf a, v4sf b, v4ui timea, v4ui timeb, v4ui time, v4sf exp) {
  const v4ui clamptime = clamp4ui(time, timea, timeb);
  const v4sf alpha = __builtin_convertvector(clamptime - timea, v4sf) / __builtin_convertvector(timeb - timea, v4sf);
  
  return bitselect4f(timea == timeb, a, a + (b-a) * powpnz4f(alpha, exp));
}

static inline v4sf func_reset4(const GstBtAdsr* const self, const v4ui ts) {
  return plerp4(self->on_level * V4SF_UNIT, V4SF_ZERO, self->ts_trigger4, self->ts_zero_end4,
                ts, V4SF_UNIT);
}

static inline v4sf func_attack4(const GstBtAdsr* const self, const v4ui ts) {
  return plerp4(V4SF_ZERO, self->attack_level * V4SF_UNIT, self->ts_zero_end4,
                self->ts_attack_end4, ts, self->attack_pow * V4SF_UNIT);
}

static inline v4sf func_decay4(const GstBtAdsr* const self, const v4ui ts) {
  return plerp4(self->attack_level * V4SF_UNIT, self->sustain_level * V4SF_UNIT, self->ts_attack_end4,
                self->ts_decay_end4, ts, self->decay_pow * V4SF_UNIT);
}

static inline v4sf func_sustain4(const GstBtAdsr* const self) {
  return self->sustain_level * V4SF_UNIT;
}

static inline v4sf func_release4(const GstBtAdsr* const self, const v4ui ts) {
  return plerp4(self->off_level * V4SF_UNIT, V4SF_ZERO, self->ts_release4, self->ts_off_end4, ts,
                self->release_pow * V4SF_UNIT);
}

static inline v4sf get_value_inline4(const GstBtAdsr* const self, const v4ui ts) {
  return ab_select4(
	func_reset4(self, ts),
	ab_select4(
	  func_attack4(self, ts),
	  ab_select4(
		func_decay4(self, ts),
		ab_select4(
		  func_sustain4(self),
		  func_release4(self, ts),
		  self->ts_release4,
		  ts),
		self->ts_decay_end4,
		ts),
	  self->ts_attack_end4,
	  ts),
	self->ts_zero_end4,
	ts);
}

gboolean gstbt_adsr_get_value_array_f(GstBtPropSrateControlSource* super, GstClockTime timestamp, GstClockTime interval,
                                      guint n_values, gfloat* values) {
  g_assert(n_values % 4 == 0);
  
  GstBtAdsr* self = (GstBtAdsr*)super;

  if (timestamp > self->ts_off_end || timestamp < self->ts_trigger) {
    for (gint i = 0; i < n_values; ++i)
      values[i] = 0;
    return FALSE;
  }

  const v4ui inc = {0,1,2,3};
  v4ui inc2 = {4,3,2,1};
  const guint uint_interval = (guint)(interval/1e2L);
  inc2 *= uint_interval;
  
  v4si accum = V4SI_ZERO;
  v4sf* const values4 = (v4sf*)values;
  v4ui ts = (guint)((timestamp - self->ts_trigger)/1e2L) + inc * uint_interval;
  for (guint i = 0; i < n_values/4; ++i) {
	values4[i] = get_value_inline4(self, ts);
	accum = (accum != V4SI_ZERO) | (values4[i] != V4SF_ZERO);
	ts += inc2;
  }

  return !v4si_eq(accum, V4SI_ZERO);
}

static gboolean get_value_array(GstControlSource* super, GstClockTime timestamp, GstClockTime interval,
								guint n_values, gdouble* values) {
  for (guint i = 0; i < n_values; ++i) {
    v4sf fval;
	gstbt_adsr_get_value_array_f((GstBtPropSrateControlSource*)super, timestamp, 0, 4, (float*)&fval);
    values[i] = fval[0];
	timestamp += interval;
  }
  return TRUE;
}

static gboolean get_value(GstControlSource* super, GstClockTime timestamp, gdouble* value) {
  get_value_array(super, timestamp, 0, 1, value);
  return TRUE;
}

void gstbt_adsr_get_value_f(GstBtPropSrateControlSource* super, GstClockTime timestamp, gfloat* value) {
  v4sf fval;
  gstbt_adsr_get_value_array_f(super, timestamp, 0, 4, (float*)&fval);
  *value = fval[0];
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
  GObjectClass* const gobject_class = (GObjectClass*)klass;
  gobject_class->dispose = dispose;

  GstBtPropSrateControlSourceClass* const klass_cs = (GstBtPropSrateControlSourceClass*)klass;
  klass_cs->get_value_f = gstbt_adsr_get_value_f;
  klass_cs->get_value_array_f = gstbt_adsr_get_value_array_f;
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

  str = g_string_assign(str, "auto-release");
  g_string_append(str, postfix);
  g_object_class_install_property(
	klass,
	(*idx)++,
	g_param_spec_boolean(str->str, str->str, "", FALSE, flags));
  
  g_string_free(str, TRUE);
}

void gstbt_adsr_init(GstBtAdsr* const self) {
  self->parent.parent_instance.get_value = get_value;
  self->parent.parent_instance.get_value_array = get_value_array;
}

void gstbt_adsr_off(GstBtAdsr* const self, const GstClockTime time) {
  if (self->released)
    return;

  self->released = TRUE;
  
  gstbt_adsr_get_value_f((GstBtPropSrateControlSource*)self, time, &self->off_level);
  self->ts_zero_end = MIN(self->ts_zero_end, time-1);
  self->ts_zero_end4 = (guint)((self->ts_zero_end - self->ts_trigger)/1e2L) * V4UI_UNIT;
  self->ts_attack_end = MIN(self->ts_attack_end, time-1);
  self->ts_attack_end4 = (guint)((self->ts_attack_end - self->ts_trigger)/1e2L) * V4UI_UNIT;
  self->ts_decay_end = MIN(self->ts_decay_end, time-1);
  self->ts_decay_end4 = (guint)((self->ts_decay_end - self->ts_trigger)/1e2L) * V4UI_UNIT;
  self->ts_release = time;
  self->ts_release4 = (guint)((self->ts_release - self->ts_trigger)/1e2L) * V4UI_UNIT;
  self->ts_off_end = self->ts_release + (GstClockTime)(self->release_secs * GST_SECOND);
  self->ts_off_end4 = (guint)((self->ts_off_end - self->ts_trigger)/1e2L) * V4UI_UNIT;
}

void gstbt_adsr_trigger(GstBtAdsr* const self, const GstClockTime time) {
  const gboolean envelope_never_triggered = self->ts_trigger == 0;

  gfloat onlevel;
  gstbt_adsr_get_value_f((GstBtPropSrateControlSource*)self, time, &onlevel);
  
  self->ts_trigger = time;
  self->ts_trigger4 = V4UI_ZERO;
  
  if (envelope_never_triggered) {
	self->ts_zero_end = self->ts_trigger;
  } else {
    self->on_level = onlevel;
    GST_INFO("ONLEVEL %f", self->on_level);
	self->ts_zero_end = self->ts_trigger + (GstClockTime)(self->on_level * 0.05 * GST_SECOND);
  }
  self->ts_zero_end4 = (guint)((self->ts_zero_end - time)/1e2L) * V4UI_UNIT;
  
  self->ts_attack_end = self->ts_zero_end + (GstClockTime)(self->attack_secs * GST_SECOND);
  self->ts_attack_end4 = (guint)((self->ts_attack_end - self->ts_trigger)/1e2L) * V4UI_UNIT;
  self->ts_decay_end = self->ts_attack_end + (GstClockTime)(self->decay_secs * GST_SECOND);
  self->ts_decay_end4 = (guint)((self->ts_decay_end - self->ts_trigger)/1e2L) * V4UI_UNIT;
  self->ts_release = ULONG_MAX;
  self->ts_release4 = V4UI_TRUE;
  self->ts_off_end = ULONG_MAX;
  self->ts_off_end4 = V4UI_TRUE;

  self->released = FALSE;
  
  if (self->auto_release) {
    GST_INFO("AUTORELEASE %ld", self->ts_decay_end);
    gstbt_adsr_off(self, self->ts_decay_end);
  }
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
  prop_add(result, "auto-release", &result->auto_release);
  return result;
}
