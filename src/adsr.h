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

#include "src/propsratecontrolsource.h"
G_DECLARE_FINAL_TYPE(GstBtAdsr, gstbt_adsr, GSTBT, ADSR, GstBtPropSrateControlSource);

GstBtAdsr* gstbt_adsr_new(GObject* owner, const char* property_postfix);
void gstbt_adsr_props_add(GObjectClass* const klass, const char* postfix, guint* idx);
void gstbt_adsr_trigger(GstBtAdsr* const self, const GstClockTime time, gfloat anticlick);
void gstbt_adsr_off(GstBtAdsr* const self, const GstClockTime time);

gboolean gstbt_adsr_property_set(GObject* obj, guint prop_id, const GValue* value, GParamSpec* pspec);
gboolean gstbt_adsr_property_get(GObject* obj, guint prop_id, GValue* value, GParamSpec* pspec);
