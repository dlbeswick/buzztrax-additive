#include "properties_simple.h"
#include <stdio.h>

struct _BtPropertiesSimpleClass {
  GObjectClass parent;
};
  
struct _BtPropertiesSimple {
  GObject parent;
  GObject* owner;
  GArray* props;
};

G_DEFINE_TYPE(BtPropertiesSimple, bt_properties_simple, G_TYPE_OBJECT);

typedef struct {
  GParamSpec* pspec;
  void* var;
} PspecVar;

gboolean bt_properties_simple_get(const BtPropertiesSimple* self, GParamSpec* pspec, GValue* value) {
  for (guint i = 0; i < self->props->len; ++i) {
	PspecVar* const pspec_var = &g_array_index(self->props, PspecVar, i);
	
	if (pspec_var->pspec->name == pspec->name) {
	  switch (pspec_var->pspec->value_type) {
	  case G_TYPE_INT:
		g_value_set_int(value, *(gint*)pspec_var->var);
		break;
	  case G_TYPE_UINT:
		g_value_set_uint(value, *(guint*)pspec_var->var);
		break;
	  case G_TYPE_FLOAT:
		g_value_set_float(value, *(gfloat*)pspec_var->var);
		break;
	  case G_TYPE_DOUBLE:
		g_value_set_double(value, *(gdouble*)pspec_var->var);
		break;
	  default:
		g_assert(FALSE);
	  }
	  return TRUE;
	}
  }
  return FALSE;
}

gboolean bt_properties_simple_set(const BtPropertiesSimple* self, GParamSpec* pspec, const GValue* value) {
  for (guint i = 0; i < self->props->len; ++i) {
	PspecVar* const pspec_var = &g_array_index(self->props, PspecVar, i);
	
	if (pspec_var->pspec == pspec) {
	  switch (pspec_var->pspec->value_type) {
	  case G_TYPE_INT:
		(*(gint*)pspec_var->var) = g_value_get_int(value);
		break;
	  case G_TYPE_UINT:
		(*(guint*)pspec_var->var) = g_value_get_uint(value);
		break;
	  case G_TYPE_FLOAT:
		(*(gfloat*)pspec_var->var) = g_value_get_float(value);
		break;
	  case G_TYPE_DOUBLE:
		(*(gdouble*)pspec_var->var) = g_value_get_double(value);
		break;
	  default:
		g_assert(FALSE);
	  }
	  return TRUE;
	}
  }
  return FALSE;
}
	
void bt_properties_simple_add(BtPropertiesSimple* self, const char* prop_name, void* var) {
  PspecVar pspec_var;

  pspec_var.pspec = g_object_class_find_property(G_OBJECT_GET_CLASS(self->owner), prop_name);
  g_assert(pspec_var.pspec);
  
  pspec_var.var = var;
  
  g_array_append_val(self->props, pspec_var);
}

void bt_properties_simple_dispose(GObject* const obj) {
  BtPropertiesSimple* const self = (BtPropertiesSimple*)obj;
  for (guint i = 0; i < self->props->len; ++i) {
	PspecVar* const pspec_var = &g_array_index(self->props, PspecVar, i);
	g_object_unref(pspec_var->pspec);
  }
  g_array_unref(self->props);
}

void bt_properties_simple_class_init(BtPropertiesSimpleClass* const klass) {
  GObjectClass* const gobject_class = (GObjectClass*)klass;
  gobject_class->dispose = bt_properties_simple_dispose;
}

void bt_properties_simple_init(BtPropertiesSimple* const self) {
  self->props = g_array_new(FALSE, FALSE, sizeof(PspecVar));
}

BtPropertiesSimple* bt_properties_simple_new(GObject* owner) {
  BtPropertiesSimple* const self = (BtPropertiesSimple*)g_object_new(bt_properties_simple_get_type(), NULL);
  self->owner = owner;
  return self;
}
