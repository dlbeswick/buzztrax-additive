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
G_DECLARE_FINAL_TYPE(BtPropertiesSimple, bt_properties_simple, BT, PROPERTIES_SIMPLE, GObject);

BtPropertiesSimple* bt_properties_simple_new(GObject* owner);

void bt_properties_simple_add(BtPropertiesSimple* self, const char* prop_name, void* var);

gboolean bt_properties_simple_get(const BtPropertiesSimple* self, GParamSpec* pspec, GValue* value);
gboolean bt_properties_simple_set(const BtPropertiesSimple* self, GParamSpec* pspec, const GValue* value);

