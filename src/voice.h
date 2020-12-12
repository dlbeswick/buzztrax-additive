#pragma once

#include <gst/gst.h>

G_BEGIN_DECLS
G_DECLARE_FINAL_TYPE(GstBtAdditiveV, gstbt_Additivev, GSTBT, ADDITIVEV, GstObject)

GstBtAdditiveV* gstbt_Additivev_new(int channel);
void gstbt_additivev_process(GstBtAdditiveV* self, GstBuffer* gstbuf);

G_END_DECLS
