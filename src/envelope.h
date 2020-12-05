#pragma once

#include <gst/gstcontrolsource.h>
G_DECLARE_FINAL_TYPE(GstBtAdsr, gstbt_adsr, GSTBT, ADSR, GstControlSource);

void gstbt_adsr_trigger(GstBtAdsr* const self, const GstClockTime time);
void gstbt_adsr_off(GstBtAdsr* const self, const GstClockTime time);

