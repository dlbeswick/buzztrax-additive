#pragma once

#include <glib-object.h>
G_DECLARE_FINAL_TYPE(BtPropertiesSimple, bt_properties_simple, BT, PROPERTIES_SIMPLE, GObject);

BtPropertiesSimple* bt_properties_simple_new(GObject* owner);

void bt_properties_simple_add(BtPropertiesSimple* self, const char* prop_name, void* var);

gboolean bt_properties_simple_get(const BtPropertiesSimple* self, GParamSpec* pspec, GValue* value);
gboolean bt_properties_simple_set(const BtPropertiesSimple* self, GParamSpec* pspec, const GValue* value);

