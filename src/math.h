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
static const v4sf V4SF_NAN = nanf("") * V4SF_UNIT;
static const v4ui V4SF_SIGN_MASK = 0x80000000 * V4UI_UNIT;
static const v4sf V4SF_MIN_NORM_POS = (v4sf)(0x00800000 * V4UI_UNIT); /* the smallest non-denormalized float */

static const v4ui V4UI_FLOAT_0P5 = 0x3F000000 * V4UI_UNIT;
static const v4ui V4UI_FLOAT_EXPONENT = 0x7F800000 * V4UI_UNIT;
static const v4ui V4UI_FLOAT_INV_EXPONENT = ~V4UI_FLOAT_EXPONENT;

// libmvec
// https://stackoverflow.com/questions/40475140/mathematical-functions-for-simd-registers
v4sf _ZGVbN4vv_powf(v4sf x, v4sf y);
v4sf _ZGVbN4v_sinf(v4sf x);

static inline gint bitselect(gint cond, gint if_t, gint if_f) {
  return (if_t & -cond) | (if_f & (~(-cond)));
}

// Note, function needs to be modified a little from the standard bitselect.
// For ints, 2 > 1 == 1.
// For vector ints, {2,2,2,2} > {1,1,1,1} == {-1,-1,-1,-1} (i.e. 0xFFFFFFFF).
// So the condition shouldn't be negated before ANDing with the results.
//
// Be careful, the following won't work as it does with bitselect:
//
// v4sf i = {1,1,1,1}; bitselect4(i, a, b);
//
// It must be written as follows:
//
// v4sf i = {1,1,1,1}; bitselect4(i != 0, a, b);
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

// See bitselect4.
static inline v4sf bitselect4f(v4si cond, v4sf if_t, v4sf if_f) {
  return (v4sf)(((v4si)if_t & cond) | ((v4si)if_f & (~cond)));
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

static inline v4sf max4f(v4sf a, v4sf b) {
  return bitselect4f(a > b, a, b);
}

static inline v4sf min4f(v4sf a, v4sf b) {
  return bitselect4f(a < b, a, b);
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
  return (v4sf)((v4si)f & ~V4SF_SIGN_MASK);
}

static inline v4si sign4f(v4sf f) {
  return (v4si)f & V4SF_SIGN_MASK;
}

static inline v4sf trunc4f(const v4sf f) {
  return __builtin_convertvector(__builtin_convertvector(f, v4si), v4sf);
}

static inline v4sf floor4f(const v4sf f) {
  v4sf t = trunc4f(f);
  return bitselect4f(t > f, t - 1, t);
}

// Return a float and integer exponent such that f == fraction * 2**exp.
// Adapted from Julien Pommier's fast SSE math functions.
static inline v4sf frexp4f(v4sf f, v4si* exp) {
  v4si fi = (v4si)f;

  *exp = ((fi >> 23) - 0x7F) + 1;

  // "| V4UI_FLOAT_0P5" means add 0.5, here
  return (v4sf)((fi & V4UI_FLOAT_INV_EXPONENT) | V4UI_FLOAT_0P5); 
}

// Adapted from glibc.
// No checks, such as NaN, under/overflow, etc!
// Returns 0 for all denormal numbers.
// Basically, mask and shift to extract the exponent, add 'n' to it, and mask and shift it back in.
static inline v4sf ldexp4f(v4sf x, v4si n) {
  v4si ix = (v4si)x;
  v4si k = (ix & V4UI_FLOAT_EXPONENT) >> 23;		/* extract exponent */

  /* Assuming k and n are bounded such that k = k+n does not overflow.  */

  // Clear exponent and insert k as the new exponent.
  ix = (ix & V4UI_FLOAT_INV_EXPONENT) | ((k + n)<<23);

  // When k == 0, then x is denormal or zero.
  return bitselect4f(k == 0, V4SF_ZERO, (v4sf)ix);
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

// Returns 0.0f when f is a denormal number.
static inline v4sf denorm_strip4f(v4sf f) {
  return bitselect4f(((v4si)f & V4UI_FLOAT_EXPONENT) == 0, V4SF_ZERO, f);
}

// Adapted from Cephes library / Julien Pommier's fast SSE math functions.
static inline v4sf sin_ps2(v4sf x) {
/* make argument positive but save the sign */
  v4sf sign = bitselect4f(x < 0.0f, -V4SF_UNIT, V4SF_UNIT);
  x = abs4f(x);

  /* integer part of x/PIO4 */
  v4sf y = floor4f(x / (gfloat)G_PI_4);

  /* note: integer overflow guard removed */
  
  /* convert to integer for tests on the phase angle */
  v4si j = __builtin_convertvector(y, v4si);
  
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

  v4sf patha = 1.0f - zz*0.5f + zz * zz * ((V4SF_COSCOF_P0 * zz + V4SF_COSCOF_P1) * zz + V4SF_COSCOF_P2);
  v4sf pathb = z + z * zz * ((V4SF_SINCOF_P0 * zz + V4SF_SINCOF_P1) * zz + V4SF_SINCOF_P2);
	
  y = bitselect4f((j==1) | (j==2), patha, pathb);

  return bitselect4f(sign < 0, -y, y);
}

// Adapted from Cephes library
static inline v4sf log4f(v4sf x)
{
/* Test for domain */
  x = bitselect4f(x <= 0.0, V4SF_NAN, x);

  v4si e;
  x = frexp4f(x, &e);
  e = bitselect4(x < 0.707106781186547524f /*SQRTHF*/, e - 1, e);
  x = bitselect4f(x < 0.707106781186547524f /*SQRTHF*/, x + x - 1.0f  /* 2x - 1 */, x - 1.0f);
  
  v4sf z = x * x;

  v4sf y =
	(((((((( 7.0376836292E-2f * x
			 - 1.1514610310E-1f) * x
		   + 1.1676998740E-1f) * x
		  - 1.2420140846E-1f) * x
		 + 1.4249322787E-1f) * x
		- 1.6668057665E-1f) * x
	   + 2.0000714765E-1f) * x
	  - 2.4999993993E-1f) * x
	 + 3.3333331174E-1f) * x * z;

  v4sf fe = bitselect4f(e != 0, __builtin_convertvector(e, v4sf), V4SF_ZERO);
  y = bitselect4f(e != 0, y + -2.12194440e-4f * fe, y);

  y += -0.5f * z;  /* y - 0.5 x^2 */
  z = x + y;   /* ... + x  */

  z = bitselect4f(e != 0, z + 0.693359375f * fe, z);

  return z;
}

// Adapted from Cephes library
// Domain checks removed: -103.278929903431851103 < x < 88.72283905206835
// This function will return incorrect values for denormal numbers.
static inline v4sf exp4f(v4sf x)
{
/* Express e**x = e**g 2**n
 *   = e**g e**( n loge(2) )
 *   = e**( g + n loge(2) )
 */
  v4sf z = floor4f( 1.44269504088896341f /*LOG2EF*/ * x + 0.5f ); /* floor() truncates toward -infinity. */
  x -= z * 0.693359375f /*C1*/;
  x -= z * -2.12194440e-4f /*C2*/;
  v4si n = __builtin_convertvector(z, v4si);

  z = x * x;
/* Theoretical peak relative error in [-0.5, +0.5] is 4.2e-9. */
  z =
	((((( 1.9875691500E-4f  * x
		  + 1.3981999507E-3f) * x
		+ 8.3334519073E-3f) * x
	   + 4.1665795894E-2f) * x
	  + 1.6666665459E-1f) * x
	 + 5.0000001201E-1f) * z
	+ x
	+ 1.0f;

/* multiply by power of 2 */
  return ldexp4f(z, n);
}

static inline v4sf powf4(v4sf base, v4sf exponent) {
  const v4si base_isneg = base < 0.0f;
  const v4sf base_nonneg = bitselect4f(base_isneg, -base, base);
  const v4sf r = exp4f(exponent*log4f(base_nonneg));
  return bitselect4f(
    base == V4SF_ZERO,
    V4SF_ZERO,
    bitselect4f(base_isneg, 1.0f / r, r)
    );
}

static inline void math_test(void) {
  {
    v4sf input = {-1.0f, 0.0f, 0.0f, 2.0f};
    v4sf expected = {0.5f, 1.0f, 1.0f, 4.0f};
    v4sf result = powf4(2 * V4SF_UNIT, input);
    for (int i = 0; i < 4; ++i) {
      printf("%x %x %x\n", ((v4si)input)[i], ((v4si)result)[i], ((v4si)expected)[i]);
      printf("%.20f %.20f %.20f\n", input[i], result[i], expected[i]);
    }
    g_assert(v4sf_eq(result, expected));
  }

  {
    v4sf input = {-1.0f, 1.0f, 100.5f, -100.5f};
    v4sf expected = {1.0f, 1.0f, 100.5f, 100.5f};
    g_assert(v4sf_eq(abs4f(input), expected));
  }

  {
    v4si inputa = {1, 2, 3, 4};
    v4si inputb = {100, 200, 300, 400};
    v4si expected = {1, 2, 300, 400};
    g_assert(v4si_eq(bitselect4(inputa <= 2, inputa, inputb), expected));
  }
  
  {
    v4sf inputa = {1.0f, 2.0f, 3.0f, 4.0f};
    v4sf inputb = {100.0f, 200.0f, 300.0f, 400.0f};
    v4sf expected = {1.0f, 2.0f, 300.0f, 400.0f};
    g_assert(v4sf_eq(bitselect4f(inputa <= 2, inputa, inputb), expected));
  }

  {
    v4sf input = {1.123f, 2.345f, 3.567f, 4.789f};
    v4sf expecteda;
    v4si expectedb = {};
    v4sf resulta;
    v4si resultb = {};
    for (int i = 0; i < 4; ++i) {
      expecteda[i] = frexpf(input[i], &expectedb[i]);
    }
	resulta = frexp4f(input, &resultb);
	g_assert(v4sf_eq(resulta, expecteda));
	g_assert(v4si_eq(resultb, expectedb));
  }

  {
    v4sf inputa = {1.0f, -2.0f, 0.0f, -4.0f};
    v4si inputb = {0, 1, 2, -3};
    v4sf expected = {1.0f, -4.0f, 0.0f, -0.5f};
    v4sf result = ldexp4f(inputa, inputb);
    for (int i = 0; i < 4; ++i) {
      //printf("%x %x %x\n", ((v4si)input)[i], ((v4si)result)[i], ((v4si)expected)[i]);
      printf("%.20f %d %.20f %.20f\n", inputa[i], inputb[i], result[i], expected[i]);
    }
    g_assert(v4sf_eq(result, expected));
  }
}
