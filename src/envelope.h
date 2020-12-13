#pragma once

#include <gst/gstcontrolsource.h>
G_DECLARE_FINAL_TYPE(GstBtAdsr, gstbt_adsr, GSTBT, ADSR, GstControlSource);

GstBtAdsr* gstbt_adsr_new(GObject* owner, const char* property_postfix);
void gstbt_adsr_props_add(GObjectClass* const klass, const char* postfix, guint* idx);
void gstbt_adsr_trigger(GstBtAdsr* const self, const GstClockTime time);
void gstbt_adsr_off(GstBtAdsr* const self, const GstClockTime time);

gboolean gstbt_adsr_property_set(GObject* obj, guint prop_id, const GValue* value, GParamSpec* pspec);
gboolean gstbt_adsr_property_get(GObject* obj, guint prop_id, GValue* value, GParamSpec* pspec);

gfloat gstbt_adsr_get_value_f(GstBtAdsr* self, GstClockTime timestamp, gfloat* value);
void gstbt_adsr_get_value_array_f(GstBtAdsr* self, GstClockTime timestamp, GstClockTime interval,
								  guint n_values, gfloat* values);
gboolean gstbt_adsr_mod_value_array_f(GstBtAdsr* self, GstClockTime timestamp, GstClockTime interval,
									  guint n_values, gfloat* values);
