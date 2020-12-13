#pragma once

#include <glib-object.h>
#include <gst/gst.h>

G_BEGIN_DECLS
G_DECLARE_FINAL_TYPE(GstBtAdditiveV, gstbt_additivev, GSTBT, ADDITIVEV, GstObject)

GstBtAdditiveV* gstbt_additivev_new(GParamSpec** parent_props, guint n_parent_props);
void gstbt_additivev_process(GstBtAdditiveV* self, GstBuffer* gstbuf);
void gstbt_additivev_note_off(GstBtAdditiveV* self, GstClockTime time);
void gstbt_additivev_note_on(GstBtAdditiveV* self, GstClockTime time);

void gstbt_additivev_get_value_array_f_for_prop(
  const GstBtAdditiveV* self,
  GstClockTime timestamp,
  GstClockTime interval,
  guint n_values,
  gfloat* values);

G_END_DECLS
