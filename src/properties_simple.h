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

/**
 * Add a simple property definition.
 * @prop_name: The name of a property already defined on this object's owner by a g_param_spec_* function.
 * @var: Data from the owner whose value is retrieved and modified in the 'get' and 'set' property function.
 *       The data must have an underlying type suitable for the type defined by the property.
 */
void bt_properties_simple_add(BtPropertiesSimple* self, const char* prop_name, void* var);

/**
 * Get a simple property's underlying data and store it in a GValue.
 * This method is intended to be called inside the GObject get_property signal handler.
 * Note that GObjects may have some properties managed by BtPropertiesSimple, while others may not be.
 *
 * Returns: TRUE if this BtPropertiesSimple instance manages the given GParamSpec and "value" was modified.
 */
gboolean bt_properties_simple_get(const BtPropertiesSimple* self, GParamSpec* pspec, GValue* value);

/**
 * Set a simple property's underlying data from a GValue.
 * This method is intended to be called inside the GObject set_property signal handler.
 * Note that GObjects may have some properties managed by BtPropertiesSimple, while others may not be.
 *
 * Returns: TRUE if this BtPropertiesSimple instance manages the given GParamSpec and the data was set.
 */
gboolean bt_properties_simple_set(const BtPropertiesSimple* self, GParamSpec* pspec, const GValue* value);

