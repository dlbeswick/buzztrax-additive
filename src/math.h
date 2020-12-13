#pragma once

#include <math.h>
#include "src/sse_mathfun.h"

typedef gdouble v4sd __attribute__ ((vector_size (32)));
typedef gint v4si __attribute__ ((vector_size (16)));
typedef guint v4ui __attribute__ ((vector_size (16)));
typedef gint16 v4ss __attribute__ ((vector_size (8)));

static const v4sf V4SF_UNIT = {1.0f, 1.0f, 1.0f, 1.0f};
static const v4sf V4SF_ZERO = 0.0f * V4SF_UNIT;

// libmvec
// https://stackoverflow.com/questions/40475140/mathematical-functions-for-simd-registers
v4sf _ZGVbN4vv_powf(v4sf x, v4sf y);

static inline v4sf powf4(v4sf a, v4sf b) {
  return _ZGVbN4vv_powf(a, b);
}

static inline v4sf maxf4(v4sf a, v4sf b) {
  v4si max = a > b;
  return a * __builtin_convertvector(-max, v4sf) + b * __builtin_convertvector(-(~max), v4sf);
}

static inline gint bitselect(gint cond, gint if_t, gint if_f) {
  return (if_t & -cond) | (if_f & (~(-cond)));
}

static inline v4si bitselect4(v4si cond, v4si if_t, v4si if_f) {
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

static inline v4sf bitselect_4f(v4si cond, v4sf if_t, v4sf if_f) {
  v4si ti, fi;
  memcpy(&ti, &if_t, sizeof(v4sf));
  memcpy(&fi, &if_f, sizeof(v4sf));
  v4si result = bitselect4(cond, ti, fi);
  v4sf resultf;
  memcpy(&resultf, &result, sizeof(v4si));
  return resultf;
}

static inline gfloat clamp(gfloat x, gfloat min, gfloat max) {
  gfloat result = bitselect_f(x < min, min, x);
  return bitselect_f(result > max, max, result);
}

static inline gfloat db_to_gain(gfloat db) {
  return powf(10.0f, db / 20.0f);
}

