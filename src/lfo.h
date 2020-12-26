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

#include "src/voice.h"
#include "src/propsratecontrolsource.h"

/*
  An LFO source that produces floating-point values.

  It operates using an accumulator mode will produce a different result to the function mode when parameters are changed
  during its operation as the output will basically be the integration of all the changes over time, rather than the
  result given the current parameters. This can be useful when modulating the LFO's own parameters, but it's not
  possible to get the output as a given time as it is with the control source approach.
*/
G_DECLARE_FINAL_TYPE(GstBtLfoFloat, gstbt_lfo_float, GSTBT, LFO_FLOAT, GObject);

GstBtLfoFloat* gstbt_lfo_float_new(GObject* owner, guint buf_srate_size, guint idx_voice);

void gstbt_lfo_float_props_add(GObjectClass* const klass, guint* idx);
gboolean gstbt_lfo_float_property_set(GObject* obj, guint prop_id, const GValue* value, GParamSpec* pspec);
gboolean gstbt_lfo_float_property_get(GObject* obj, guint prop_id, GValue* value, GParamSpec* pspec);

gboolean gstbt_lfo_float_mod_value_array_accum(GstBtLfoFloat* self, GstClockTime timestamp, GstClockTime interval,
                                               gfloat* values, guint n_values, GstBtAdditiveV** voices);
