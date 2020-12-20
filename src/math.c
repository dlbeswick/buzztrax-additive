#include "src/math.h"

const gfloat F2PI = 2*(gfloat)G_PI;
const gfloat F4OPI = 4/(gfloat)G_PI;

const v4si V4SI_UNIT = {1, 1, 1, 1};

const v4ui V4UI_UNIT = {1, 1, 1, 1};
const v4ui V4UI_ZERO = 0 * V4UI_UNIT;
const v4ui V4UI_MAX = 0xFFFFFFFF * V4UI_UNIT;

const v4sf V4SF_UNIT = {1.0f, 1.0f, 1.0f, 1.0f};
const v4sf V4SF_ZERO = 0.0f * V4SF_UNIT;
const v4sf V4SF_NAN = nanf("") * V4SF_UNIT;
const v4ui V4SF_SIGN_MASK = 0x80000000 * V4UI_UNIT;
const v4sf V4SF_MIN_NORM_POS = (v4sf)(0x00800000 * V4UI_UNIT); /* the smallest non-denormalized float */
const v4ui V4UI_FLOAT_0P5 = 0x3F000000 * V4UI_UNIT;
const v4ui V4UI_FLOAT_EXPONENT = 0x7F800000 * V4UI_UNIT;
const v4ui V4UI_FLOAT_INV_EXPONENT = ~V4UI_FLOAT_EXPONENT;

// From Cephes library
const v4sf V4SF_DP1 = 0.78515625f * V4SF_UNIT;
const v4sf V4SF_DP2 = 2.4187564849853515625e-4f * V4SF_UNIT;
const v4sf V4SF_DP3 = 3.77489497744594108e-8f * V4SF_UNIT;
const v4sf V4SF_SINCOF_P0 = -1.9515295891E-4f * V4SF_UNIT;
const v4sf V4SF_SINCOF_P1 = 8.3321608736E-3f * V4SF_UNIT;
const v4sf V4SF_SINCOF_P2 = -1.6666654611E-1f * V4SF_UNIT;
const v4sf V4SF_COSCOF_P0 = 2.443315711809948E-005f * V4SF_UNIT;
const v4sf V4SF_COSCOF_P1 = -1.388731625493765E-003f * V4SF_UNIT;
const v4sf V4SF_COSCOF_P2 = 4.166664568298827E-002f * V4SF_UNIT;

// Adapted from Cephes library / Julien Pommier's fast SSE math functions.
v4sf sin4f(v4sf x) {
/* make argument positive but save the sign */
  v4si sign = x < 0;
  x = fabs4f(x);

  /* integer part of x/PIO4 */
  v4sf y = floor4f(F4OPI * x);

  /* note: integer overflow guard removed */
  
  /* convert to integer for tests on the phase angle */
  v4si j = __builtin_convertvector(y, v4si);
  
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
	x<0 octant result_sign
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
	= x < 0 XOR j < 3
   */
  return bitselect4f(~(sign ^ (j > 3)), -y, y);
}

v4sf cos4f(v4sf x)
{
/* make argument positive */
  x = fabs4f(x);

  v4si j = __builtin_convertvector(F4OPI * x, v4si); /* integer part of x/PIO4 */
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
	0 -1
	1 -1
	2 1
	3 1
   */
  return bitselect4f(j+2 > 3, -y, y);
}

v4sf pow4f(v4sf base, v4sf exponent) {
  const v4si base_isneg = base < 0.0f;
  const v4sf base_nonneg = bitselect4f(base_isneg, -base, base);
  const v4sf r = exp4f(exponent*log4f(base_nonneg));
  return bitselect4f(
    base == V4SF_ZERO,
    V4SF_ZERO,
    bitselect4f(base_isneg, 1.0f / r, r)
    );
}

v4sf exp4f(v4sf x)
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

v4sf log4f(v4sf x)
{
/* Test for domain */
  x = bitselect4f(x <= 0.0, V4SF_NAN, x);

  v4si e;
  x = frexp4f(x, &e);

  {
	const v4si x_lt_sqrthf = x < 0.707106781186547524f /*SQRTHF*/;
	e = bitselect4(x_lt_sqrthf, e - 1, e);
	x = bitselect4f(x_lt_sqrthf, x + x - 1.0f  /* 2x - 1 */, x - 1.0f);
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
