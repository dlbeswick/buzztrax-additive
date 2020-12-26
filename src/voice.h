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

#include <glib-object.h>
#include <gst/gst.h>

G_BEGIN_DECLS
G_DECLARE_FINAL_TYPE(GstBtAdditiveV, gstbt_additivev, GSTBT, ADDITIVEV, GstObject);

GstBtAdditiveV* gstbt_additivev_new(GParamSpec** parent_props, guint n_parent_props, guint srate_buf_size,
                                    guint idx_voice);

void gstbt_additivev_process(GstBtAdditiveV* self, GstBuffer* gstbuf);
void gstbt_additivev_note_off(GstBtAdditiveV* self, GstClockTime time);
void gstbt_additivev_note_on(GstBtAdditiveV* self, GstClockTime time);

void gstbt_additivev_mod_value_array_f_for_prop(
  GstBtAdditiveV* self,
  GstClockTime timestamp,
  GstClockTime interval,
  guint n_values,
  gfloat* values,
  gboolean props_active[],
  gboolean props_controlled[],
  GstBtAdditiveV** voices);

void gstbt_additivev_mod_value_array_f_for_prop_idx(
  GstBtAdditiveV* self,
  GstClockTime timestamp,
  GstClockTime interval,
  guint n_values,
  gfloat* values,
  gboolean props_active[],
  gboolean props_controlled[],
  GstBtAdditiveV** voices,
  guint idx);

G_END_DECLS
