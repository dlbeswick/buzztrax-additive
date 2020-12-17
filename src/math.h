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

#include <math.h>
#include "src/sse_mathfun.h"

typedef gdouble v4sd __attribute__ ((vector_size (32)));
typedef gint v4si __attribute__ ((vector_size (16)));
typedef guint v4ui __attribute__ ((vector_size (16)));
typedef gint16 v4ss __attribute__ ((vector_size (8)));

static const v4ui V4UI_UNIT = {1, 1, 1, 1};
static const v4ui V4UI_ZERO = 0 * V4UI_UNIT;
static const v4ui V4UI_MAX = 0xFFFFFFFF * V4UI_UNIT;
static const v4sf V4SF_UNIT = {1.0f, 1.0f, 1.0f, 1.0f};
static const v4sf V4SF_ZERO = 0.0f * V4SF_UNIT;

// libmvec
// https://stackoverflow.com/questions/40475140/mathematical-functions-for-simd-registers
v4sf _ZGVbN4vv_powf(v4sf x, v4sf y);
v4sf _ZGVbN4v_sinf(v4sf x);

static inline v4sf powf4(v4sf a, v4sf b) {
  return _ZGVbN4vv_powf(a, b);
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

// Note, function needs to be modified a little from the standard bitselect.
// For ints, 2 > 1 == 1.
// For vector ints, {2,2,2,2} > {1,1,1,1} == {-1,-1,-1,-1} (i.e. 0xFFFFFFFF).
// So the condition shouldn't be negated before ANDing with the results.
static inline v4sf bitselect_4f(v4ui cond, v4sf if_t, v4sf if_f) {
  v4si ti, fi;
  memcpy(&ti, &if_t, sizeof(v4sf));
  memcpy(&fi, &if_f, sizeof(v4sf));
  v4si result = (ti & cond) | (fi & (~cond));
  v4sf resultf;
  memcpy(&resultf, &result, sizeof(v4si));
  return resultf;
}

static inline gboolean v4ui_eq(v4ui a, v4ui b) {
  return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

static inline v4sf maxf4(v4sf a, v4sf b) {
  return bitselect_4f(a > b, a, b);
}

static inline gfloat clamp(gfloat x, gfloat min, gfloat max) {
  gfloat result = bitselect_f(x < min, min, x);
  return bitselect_f(result > max, max, result);
}

static inline gfloat lerp(const gfloat a, const gfloat b, const gfloat alpha) {
  return a + (b-a) * alpha;
}

static inline v4sf lerp4f(const v4sf a, const v4sf b, const v4sf alpha) {
  return a + (b-a) * alpha;
}

static inline gfloat db_to_gain(gfloat db) {
  return powf(10.0f, db / 20.0f);
}

static inline gfloat abs_fracf(gfloat f) {
  gfloat i;
  return fabs(modff(f, &i));
}
