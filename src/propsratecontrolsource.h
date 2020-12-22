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

#pragma once

#include "src/math.h"
#include <gst/gstcontrolsource.h>

/*
  A Control Source holding data that defines what property the generated values should apply to.
  
  It can write its data into a group of buffers indexed by the property, for convenience.

  Unlike GST's Control Source, floats are the type used internally and these can be returned directly.
  
  The results are intended for use as s-rate modifiers for machine controls, and the interface is intended to give
  the client will as much latitude possible as to where and how to apply the data to make sure it's done efficiently.
  It can also be used as a regular control source, though, if needed.
*/
G_DECLARE_DERIVABLE_TYPE(GstBtPropSrateControlSource, gstbt_prop_srate_cs, GSTBT,
						 PROP_SRATE_CONTROL_SOURCE, GstControlSource);

struct _GstBtPropSrateControlSourceClass {
  GstControlSourceClass parent;

  void (*get_value_f)(GstBtPropSrateControlSource* self, GstClockTime timestamp, gfloat* value);
  void (*get_value_array_f)(GstBtPropSrateControlSource* self, GstClockTime timestamp, GstClockTime interval,
							guint n_values, gfloat *values);

  // Return true if any of the input values were modulated to non-zero values.
  gboolean (*mod_value_array_f)(GstBtPropSrateControlSource* self, GstClockTime timestamp, GstClockTime interval,
								guint n_values, v4sf* values);
};

void gstbt_prop_srate_cs_get_value_f(GstBtPropSrateControlSource* self, GstClockTime timestamp, gfloat* value);
void gstbt_prop_srate_cs_get_value_array_f(GstBtPropSrateControlSource* self, GstClockTime timestamp,
										   GstClockTime interval, guint n_values, gfloat* values);
gboolean gstbt_prop_srate_cs_mod_value_array_f(GstBtPropSrateControlSource* self, GstClockTime timestamp,
											   GstClockTime interval, guint n_values, v4sf* values);
