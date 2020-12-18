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
#include <stdio.h>


typedef gdouble v2sd __attribute__ ((vector_size (16)));
typedef gfloat v4sf __attribute__ ((vector_size (16)));
typedef gint v4si __attribute__ ((vector_size (16)));
typedef guint v4ui __attribute__ ((vector_size (16)));
typedef gint16 v4ss __attribute__ ((vector_size (8)));

static const v4ui V4UI_UNIT = {1, 1, 1, 1};
static const v4ui V4UI_ZERO = 0 * V4UI_UNIT;
static const v4ui V4UI_MAX = 0xFFFFFFFF * V4UI_UNIT;
static const v4sf V4SF_UNIT = {1.0f, 1.0f, 1.0f, 1.0f};
static const v4sf V4SF_ZERO = 0.0f * V4SF_UNIT;
static const v4ui V4SF_SIGN_MASK = 0x80000000 * V4UI_UNIT;

// libmvec
// https://stackoverflow.com/questions/40475140/mathematical-functions-for-simd-registers
v4sf _ZGVbN4vv_powf(v4sf x, v4sf y);
v4sf _ZGVbN4v_sinf(v4sf x);

static inline v4sf powf4(v4sf a, v4sf b) {
  // tbd: inline _ZGVbN4vv_powf?
  // tbd: use loop to avoid use of _ZGVbN4vv_powf? check that it will vectorize.
  return _ZGVbN4vv_powf(a, b);
}

static inline gint bitselect(gint cond, gint if_t, gint if_f) {
  return (if_t & -cond) | (if_f & (~(-cond)));
}

// Note, function needs to be modified a little from the standard bitselect.
// For ints, 2 > 1 == 1.
// For vector ints, {2,2,2,2} > {1,1,1,1} == {-1,-1,-1,-1} (i.e. 0xFFFFFFFF).
// So the condition shouldn't be negated before ANDing with the results.
static inline v4si bitselect4(v4si cond, v4si if_t, v4si if_f) {
  return (if_t & cond) | (if_f & (~cond));
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
static inline v4sf bitselect4f(v4si cond, v4sf if_t, v4sf if_f) {
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

static inline gboolean v4sf_eq(v4sf a, v4sf b) {
  return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

static inline gboolean v4si_eq(v4si a, v4si b) {
  return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3];
}

static inline v4sf maxf4(v4sf a, v4sf b) {
  return bitselect4f(a > b, a, b);
}

static inline gfloat clamp(gfloat x, gfloat min, gfloat max) {
  const gfloat result = bitselect_f(x < min, min, x);
  return bitselect_f(result > max, max, result);
}

static inline v4sf clamp4f(v4sf x, v4sf min, v4sf max) {
  const v4sf result = bitselect4f(x < min, min, x);
  return bitselect4f(result > max, max, result);
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

static inline v4sf abs4f(v4sf f) {
  v4ui fi;
  memcpy(&fi, &f, sizeof(v4sf));
  fi &= ~V4SF_SIGN_MASK;
  memcpy(&f, &fi, sizeof(v4si));
  return f;
}

static inline v4si sign4f(v4sf f) {
  v4si fi;
  memcpy(&fi, &f, sizeof(v4sf));
  fi &= V4SF_SIGN_MASK;
  return fi;
}

static inline v4ui xor4f(v4sf f, v4ui i) {
  v4ui fi;
  memcpy(&fi, &f, sizeof(v4sf));
  fi ^= i;
  return fi;
}

static inline v4sf floor4f(v4sf f) {
  return __builtin_convertvector(__builtin_convertvector(f, v4si), v4sf);
}

// From Cephes library
static const v4sf V4SF_DP1 = 0.78515625f * V4SF_UNIT;
static const v4sf V4SF_DP2 = 2.4187564849853515625e-4f * V4SF_UNIT;
static const v4sf V4SF_DP3 = 3.77489497744594108e-8f * V4SF_UNIT;
static const v4sf V4SF_SINCOF_P0 = -1.9515295891E-4f * V4SF_UNIT;
static const v4sf V4SF_SINCOF_P1 = 8.3321608736E-3f * V4SF_UNIT;
static const v4sf V4SF_SINCOF_P2 = -1.6666654611E-1f * V4SF_UNIT;
static const v4sf V4SF_COSCOF_P0 = 2.443315711809948E-005f * V4SF_UNIT;
static const v4sf V4SF_COSCOF_P1 = -1.388731625493765E-003f * V4SF_UNIT;
static const v4sf V4SF_COSCOF_P2 = 4.166664568298827E-002f * V4SF_UNIT;

// Adapted from Cephes library / Julien Pommier's fast SSE math functions.
static inline v4sfm sin_ps2(v4sf x) {
/* make argument positive but save the sign */
  v4sf sign = bitselect4f(x < 0.0f, -V4SF_UNIT, V4SF_UNIT);
  x = abs4f(x);

  /* integer part of x/PIO4 */
  v4sf y = floor4f(x / (gfloat)G_PI_4);

  /* strip high bits of integer part to prevent integer overflow */
  v4sf q = y / 16;
  q = floor4f(q);
  q = y - q * 16;
	
  /* convert to integer for tests on the phase angle */
  v4si j = __builtin_convertvector(q, v4si);
  
/* map zeros to origin */
  y = bitselect4f((j & 1) == 1, y + 1.0f, y);
  j = bitselect4((j & 1) == 1, j + 1, j);

  j = j & 07; /* octant modulo 360 degrees */

/* reflect in x axis */
  sign = bitselect4f(j > 3, -sign, sign);
  j = bitselect4(j > 3, j - 4, j);

/* Extended precision modular arithmetic */
  v4sf z = ((x - y * V4SF_DP1) - y * V4SF_DP2) - y * V4SF_DP3;

  v4sf zz = z * z;

  v4sf patha = V4SF_COSCOF_P0;
  patha *= zz;
  patha += V4SF_COSCOF_P1;
  patha *= zz;
  patha += V4SF_COSCOF_P2;
  patha *= zz;
  patha *= zz;
  patha -= zz * 0.5f;
  patha += V4SF_UNIT;
  
  v4sf pathb = V4SF_SINCOF_P0;
  pathb *= zz;
  pathb += V4SF_SINCOF_P1;
  pathb *= zz;
  pathb += V4SF_SINCOF_P2;
  pathb *= zz;
  pathb *= z;
  pathb += z;

  v4si mask = {
	j[0]==1 || j[0]==2 ? -1 : 0,
	j[1]==1 || j[1]==2 ? -1 : 0,
	j[2]==1 || j[2]==2 ? -1 : 0,
	j[3]==1 || j[3]==2 ? -1 : 0
	};
  y = bitselect4f(mask, patha, pathb);

  return bitselect4f(sign < 0, -y, y);
}
