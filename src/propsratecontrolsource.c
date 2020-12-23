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

#include "src/propsratecontrolsource.h"

G_DEFINE_ABSTRACT_TYPE(GstBtPropSrateControlSource, gstbt_prop_srate_cs, GST_TYPE_CONTROL_SOURCE);

void gstbt_prop_srate_cs_get_value_f(GstBtPropSrateControlSource* self, GstClockTime timestamp, gfloat* value) {
  GstBtPropSrateControlSourceClass* klass = GSTBT_PROP_SRATE_CONTROL_SOURCE_GET_CLASS(self);
  g_assert(klass->get_value_f != NULL);
  klass->get_value_f(self, timestamp, value);
}

void gstbt_prop_srate_cs_get_value_array_f(GstBtPropSrateControlSource* self, GstClockTime timestamp,
										   GstClockTime interval, guint n_values, gfloat* values) {
  GstBtPropSrateControlSourceClass* klass = GSTBT_PROP_SRATE_CONTROL_SOURCE_GET_CLASS(self);
  g_assert(klass->get_value_array_f != NULL);
  klass->get_value_array_f(self, timestamp, interval, n_values, values);
}

gboolean gstbt_prop_srate_cs_mod_value_array_f(GstBtPropSrateControlSource* self, GstClockTime timestamp,
											   GstClockTime interval, guint n_values, gfloat* values) {
  GstBtPropSrateControlSourceClass* klass = GSTBT_PROP_SRATE_CONTROL_SOURCE_GET_CLASS(self);
  g_assert(klass->mod_value_array_f != NULL);
  return klass->mod_value_array_f(self, timestamp, interval, n_values, values);
}

void gstbt_prop_srate_cs_class_init(GstBtPropSrateControlSourceClass* const klass) {
}

void gstbt_prop_srate_cs_init(GstBtPropSrateControlSource* const self) {
}
