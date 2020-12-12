#pragma once

#include <math.h>

static inline gint bitselect(gint cond, gint if_t, gint if_f) {
  return (if_t & -cond) | (if_f & (~(-cond)));
}

static inline gfloat bitselect_f(gint cond, gfloat if_t, gfloat if_f) {
  gint ti, fi;
  memcpy(&ti, &if_t, sizeof(gfloat));
  memcpy(&fi, &if_f, sizeof(gfloat));
  gint result = bitselect(cond, ti, fi);
  gfloat resultf;
  memcpy(&resultf, &result, sizeof(gint));
  return resultf;
}

static inline gfloat clamp(gfloat x, gfloat min, gfloat max) {
  gfloat result = bitselect_f(x < min, min, x);
  return bitselect_f(result > max, max, result);
}

static inline gfloat db_to_gain(gfloat db) {
  return powf(10.0f, db / 20.0f);
}

