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

/**
 * An LFO source that produces floating-point values.
 *
 * It operates using an accumulator mode that will produce a different result to the function mode when parameters are
 * changed during its operation. The output will be the integration of all the changes over time, rather than the
 * result given the current parameters. This can be useful when modulating the LFO's own parameters as it tends to
 * produce a "smoother" and more intuitive result, but on the other hand it's not possible to get the output at a given
 * time as it is with the control source approach.
 */
G_DECLARE_FINAL_TYPE(GstBtLfoFloat, gstbt_lfo_float, GSTBT, LFO_FLOAT, GObject);

GstBtLfoFloat* gstbt_lfo_float_new(GObject* owner, guint idx_voice);

/**
 * The LFO needs a buffer of "s-rate" samples that will be used to modulate sound output, and/or other LFO outputs.
 * The size of this buffer will generally need to match the size of the buffer used for the sound output that the LFO
 * is modulating. It should be called before using the LFO and any time the main sound output buffer size changes.
 */ 
void gstbt_lfo_float_on_buf_size_change(GstBtLfoFloat* self, guint n_samples);

/**
 * Can be used to add the LFO class properties to another class.
 */
void gstbt_lfo_float_props_add(GObjectClass* const klass, guint* idx);

/**
 * These are intended to be called from the owning object's set/get_property signal handlers.
 */
gboolean gstbt_lfo_float_property_set(GObject* obj, guint prop_id, const GValue* value, GParamSpec* pspec);
gboolean gstbt_lfo_float_property_get(GObject* obj, guint prop_id, GValue* value, GParamSpec* pspec);

gboolean gstbt_lfo_float_mod_value_array_accum(GstBtLfoFloat* self, GstClockTime timestamp, GstClockTime interval,
                                               gfloat* values, guint n_values, GstBtAdditiveV** voices);
