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

#include "src/sse_mathfun.h"
#include <glib.h>
#include <math.h>
#include <stdio.h>


typedef gdouble v2sd __attribute__ ((vector_size (16)));
typedef gfloat v4sf __attribute__ ((vector_size (16)));
typedef gint v4si __attribute__ ((vector_size (16)));
typedef guint v4ui __attribute__ ((vector_size (16)));
typedef gint16 v4ss __attribute__ ((vector_size (8)));

#define FPI ((gfloat)G_PI)
const float F2PI;

const v4ui V4UI_UNIT;
const v4ui V4UI_ZERO;
const v4ui V4UI_TRUE;
#define V4SI_UNIT ((v4si)V4UI_UNIT)
#define V4SI_ZERO ((v4si)V4UI_ZERO)
#define V4SI_TRUE ((v4si)V4UI_TRUE)

const v4sf V4SF_UNIT;
const v4sf V4SF_ZERO;
const v4sf V4SF_NAN;
const v4ui V4SF_SIGN_MASK;
const v4sf V4SF_MIN_NORM_POS;

const v4ui V4UI_FLOAT_0P5;
const v4ui V4UI_FLOAT_EXPONENT;
const v4ui V4UI_FLOAT_INV_EXPONENT;
const v4sf MINLOGF;

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
// It must be written with a boolean test as follows:
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
static inline v4sf bitselect4f(const v4si cond, const v4sf if_t, const v4sf if_f) {
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

static inline v4sf fabs4f(v4sf f) {
  return (v4sf)((v4si)f & ~V4SF_SIGN_MASK);
}

static inline v4sf truncmin4f(v4sf x, v4sf min) {
  return bitselect4f(fabs4f(x) < min, V4SF_ZERO, x);
}

static inline v4si signbit4f(v4sf f) {
  return (v4si)f & V4SF_SIGN_MASK;
}

static inline v4sf withsignbit4f(v4sf f, v4si cond) {
  return (v4sf)((v4si)f ^ (((v4si)f ^ cond) & V4SF_SIGN_MASK));
}

static inline v4sf copysign4f(v4sf dst, v4sf src) {
  return (v4sf)(((v4si)dst & ~V4SF_SIGN_MASK) | ((v4si)src & V4SF_SIGN_MASK));
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

// Multiply x by 2**n.
// Adapted from glibc.
// Returns 0 for denormal numbers.
// Basically, mask and shift to extract the exponent, add 'n' to it, and mask and shift it back in while checking for
// over/underflow of the exponent.
static inline v4sf ldexp4f(v4sf x, v4si n) {
  v4si ix = (v4si)x;
  v4si k = (ix & V4UI_FLOAT_EXPONENT) >> 23;		/* extract exponent */

  /* Assuming k and n are bounded such that k = k+n does not overflow. */

  v4si newk = k+n;
  // Clear exponent and insert k as the new exponent.
  ix = (ix & V4UI_FLOAT_INV_EXPONENT) | (newk<<23);

  //intf("!@# %d %d %d\n", k[0], n[0], newk[0]);
  return bitselect4f(
	k == 0, // When k == 0, then x is denormal or zero. Must check original k as 0**x == 0.
	V4SF_ZERO,
	bitselect4f(
	  newk <= 0,
	  copysign4f(V4SF_UNIT * 1.0e-30f, x),
	  bitselect4f(
		newk > 254,
		copysign4f(V4SF_UNIT * 1.0e+30f, x),
		(v4sf)ix
		)
	  )
	);
}

// From Cephes library
const v4sf V4SF_DP1;
const v4sf V4SF_DP2;
const v4sf V4SF_DP3;
const v4sf V4SF_SINCOF_P0;
const v4sf V4SF_SINCOF_P1;
const v4sf V4SF_SINCOF_P2;
const v4sf V4SF_COSCOF_P0;
const v4sf V4SF_COSCOF_P1;
const v4sf V4SF_COSCOF_P2;

// https://stackoverflow.com/questions/2487653/avoiding-denormal-values-in-c
#define CSR_FLUSH_TO_ZERO         (1 << 15)

static inline unsigned denormals_disable(void) {
  unsigned csr = __builtin_ia32_stmxcsr();
  csr |= CSR_FLUSH_TO_ZERO;
  __builtin_ia32_ldmxcsr(csr);
  return csr;
}

static inline void denormals_restore(unsigned csr) {
  __builtin_ia32_ldmxcsr(csr);
}

// Returns 0.0f when f is a denormal number.
static inline v4sf denorm_strip4f(v4sf f) {
  return bitselect4f(((v4ui)f & V4UI_FLOAT_EXPONENT) == V4SI_ZERO, V4SF_ZERO, f);
}

// Adapted from Cephes library / Julien Pommier's fast SSE math functions.
// Adapted from Cephes library / Julien Pommier's fast SSE math functions.
static inline v4sf sin4f(v4sf x) {
/* make argument positive but save the sign */
  v4si sign = x < 0;
  x = fabs4f(x);

  v4si j = __builtin_convertvector(4/(gfloat)G_PI * x, v4si); /* integer part of x/PIO4 */
  v4sf y = __builtin_convertvector(j, v4sf);
  
  /* note: integer overflow guard removed */
  
/* map zeros to origin */
  {
	const v4si j_and_1 = (j & 1) == 1;
	y = bitselect4f(j_and_1, y + 1.0f, y);
	j = bitselect4(j_and_1, j + 1, j);
  }

  j = j & 07; /* octant modulo 360 degrees */

/* Extended precision modular arithmetic */
  const v4sf z = ((x - y * V4SF_DP1) - y * V4SF_DP2) - y * V4SF_DP3;

  const v4sf zz = z * z;

  const v4sf patha = 1.0f - 0.5f*zz + zz * zz * ((V4SF_COSCOF_P0 * zz + V4SF_COSCOF_P1) * zz + V4SF_COSCOF_P2);
  const v4sf pathb = z + z * zz * ((V4SF_SINCOF_P0 * zz + V4SF_SINCOF_P1) * zz + V4SF_SINCOF_P2);
	
  /*
	octant patha/b
	0 b
	1 a
	2 a
	3 b
	4 b
	5 a
	6 a
	7 b
  */
  y = bitselect4f(((j-1)&3) < 2, patha, pathb);

  /*
	x<0 octant result_multiplier
	1 0 -1
	1 1 -1
	1 2 -1
	1 3 -1
	1 4 1
	1 5 1
	1 6 1
	1 7 1
	0 0 1
	0 1 1
	0 2 1
	0 3 1
	0 4 -1
	0 5 -1
	0 6 -1
	0 7 -1

	((x < 0) && (j <= 3)) || ((x >= 0) && (j > 3))
	= x < 0 XOR j > 3
   */
  return bitselect4f(sign ^ (j > 3), -y, y);
}

static inline v4sf cos4f(v4sf x)
{
/* make argument positive */
  x = fabs4f(x);

  v4si j = __builtin_convertvector(4/(gfloat)G_PI * x, v4si); /* integer part of x/PIO4 */
  v4sf y = __builtin_convertvector(j, v4sf);
  
/* integer and fractional part modulo one octant */
  {
	const v4si j_and_1 = (j & 1) != 0;
	j = bitselect4(j_and_1, j + 1, j);
	y = bitselect4f(j_and_1, y + 1.0f, y);
  }
  
  j &= 7;

/* Extended precision modular arithmetic */
  x = ((x - y * V4SF_DP1) - y * V4SF_DP2) - y * V4SF_DP3;

  const v4sf z = x * x;

  const v4sf patha = x + x * z * ((V4SF_SINCOF_P0 * z + V4SF_SINCOF_P1) * z + V4SF_SINCOF_P2);
  const v4sf pathb = 1.0f - 0.5f*z + z * z * ((V4SF_COSCOF_P0 * z + V4SF_COSCOF_P1) * z + V4SF_COSCOF_P2);

  /*
	octant patha/b
	0 b
	1 a
	2 a
	3 b
	4 b
	5 a
	6 a
	7 b
  */
  y = bitselect4f(((j-1)&3) < 2, patha, pathb);

  /*
	octant result_sign
	0 1
	1 1
	2 -1
	3 -1
	4 -1
	5 -1
	6 1
	7 1
   */
  return bitselect4f(j+2 > 3, -y, y);
}

static inline void sincos4f(v4sf x, v4sf* out_sin, v4sf* out_cos) {
  v4si sign = x < 0;
  x = fabs4f(x);

  v4si j = __builtin_convertvector(4/(gfloat)G_PI * x, v4si); /* integer part of x/PIO4 */
  v4sf y = __builtin_convertvector(j, v4sf);
  
/* map zeros to origin */
  {
	const v4si j_and_1 = (j & 1) == 1;
	y = bitselect4f(j_and_1, y + 1.0f, y);
	j = bitselect4(j_and_1, j + 1, j);
  }

  j = j & 07; /* octant modulo 360 degrees */

/* Extended precision modular arithmetic */
  const v4sf z = ((x - y * V4SF_DP1) - y * V4SF_DP2) - y * V4SF_DP3;

  const v4sf zz = z * z;

  const v4sf pathasin = 1.0f - 0.5f*zz + zz * zz * ((V4SF_COSCOF_P0 * zz + V4SF_COSCOF_P1) * zz + V4SF_COSCOF_P2);
  const v4sf pathbsin = z + z * zz * ((V4SF_SINCOF_P0 * zz + V4SF_SINCOF_P1) * zz + V4SF_SINCOF_P2);

  const v4si path_select = ((j-1)&3) < 2;
  
  *out_sin = bitselect4f(path_select, pathasin, pathbsin);
  *out_sin = bitselect4f(sign ^ (j > 3), -*out_sin, *out_sin);
  *out_cos = bitselect4f(path_select, pathbsin, pathasin);
  *out_cos = bitselect4f(j+2 > 3, -*out_cos, *out_cos);
}


static inline v4sf tan4f(v4sf x) {
  v4sf sinv,cosv;
  sincos4f(x,&sinv,&cosv);
  return sinv / cosv;
}

static inline v4sf log4f(v4sf x) {
/* Test for domain */
  const v4si out_of_domain = x <= 0.0;

  if (v4si_eq(out_of_domain, V4SI_TRUE))
	return MINLOGF;
  
  x = bitselect4f(out_of_domain, MINLOGF, x);

  v4si e;
  x = frexp4f(x, &e);

  {
	const v4si x_lt_sqrthf = x < 0.707106781186547524f /*SQRTHF*/;
	e = bitselect4(x_lt_sqrthf, e - 1, e);
	x = bitselect4f(x_lt_sqrthf, x + x /* 2x */, x) - 1.0f;
  }
  
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

  const v4si e_ne_0 = e != 0;
  const v4sf fe = bitselect4f(e_ne_0, __builtin_convertvector(e, v4sf), V4SF_ZERO);
  y = bitselect4f(e_ne_0, y + -2.12194440e-4f * fe, y);

  y += -0.5f * z;  /* y - 0.5 x^2 */
  z = x + y;   /* ... + x  */

  z = bitselect4f(e_ne_0, z + 0.693359375f * fe, z);

  return z;
}


// Domain checks removed: -103.278929903431851103 < x < 88.72283905206835
// This function will return incorrect values for denormal numbers.
static inline v4sf exp4f(v4sf x) {
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

// Returns zero if base is negative and exponent is not an integer.
// No mathematical basis to this; it just helps simplify the use of real parameters as exponents.
static inline v4sf pow4f(v4sf base, v4sf exponent) {
  const v4si exponent_int = __builtin_convertvector(exponent,v4si);
  const v4si is_neg_base = base < 0;
  
  // Only integer negative exponents produce a real value when the base is also negative.
  const v4sf exponent_fixed =
    bitselect4f(is_neg_base & (exponent < 0), __builtin_convertvector(exponent_int,v4sf), exponent);
  
  const v4sf r = exp4f(exponent_fixed*log4f(fabs4f(base))); 
  return bitselect4f(
    base == V4SF_ZERO,
    V4SF_ZERO,
    withsignbit4f(r, is_neg_base & ((exponent_int & 1) != 0))
    );
}

static inline v4sf sin4f_method(const v4sf x) {
  return sin4f(x);
  //return _ZGVbN4v_sinf(x);
}

// Sine with range  0 -> 1
static inline v4sf sin014f(const v4sf x) {
  return (1.0f + sin4f_method(x)) * 0.5f;
}

static inline v4sf pow4f_method(const v4sf x, const v4sf vexp) {
  return pow4f(x, vexp);
}

// Take a sin with range 0 -> 1 and exponentiate to 'vexp' power
// Return the result normalized back to -1 -> 1 range.
// A way of waveshaping using non-odd powers?
static inline v4sf powsin4f(const v4sf x, const v4sf vexp) {
  return (pow4f_method(sin014f(x), vexp) - 0.5f) * 2.0f;
}

// A cosine window whose slope can be controlled with a "sharpness" value.
// Basically, that value modulates the cos functions frequency, which can narrow or widen the peak.
static inline v4sf window_sharp_cosine4(v4sf sample, v4sf sample_center, gfloat rate, v4sf sharpness) {
  return bitselect4f(
    sharpness == 0.0f,
    V4SF_ZERO,
    0.5f +
    -0.5f * cos4f(F2PI * clamp4f(sharpness * (sample + rate/2.0f/sharpness - sample_center) / rate,
								 V4SF_ZERO,
								 V4SF_UNIT))
    );
}

void math_test(void);
