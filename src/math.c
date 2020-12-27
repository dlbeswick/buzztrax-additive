#include "src/math.h"

const gfloat F2PI = 2*(gfloat)G_PI;
const gfloat F4OPI = 4/(gfloat)G_PI;

const v4ui V4UI_UNIT = {1, 1, 1, 1};
const v4ui V4UI_ZERO = 0 * V4UI_UNIT;
const v4ui V4UI_TRUE = 0xFFFFFFFF * V4UI_UNIT;

const v4sf V4SF_UNIT = {1.0f, 1.0f, 1.0f, 1.0f};
const v4sf V4SF_ZERO = 0.0f * V4SF_UNIT;
const v4sf V4SF_NAN = nanf("") * V4SF_UNIT;
const v4ui V4SF_SIGN_MASK = 0x80000000 * V4UI_UNIT;
/* the smallest non-denormalized float, FLT_MIN, (1.175494351e-38) */
const v4sf V4SF_MIN_NORM_POS = (v4sf)(0x00800000 * V4UI_UNIT);

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
static const v4sf MINLOGF = V4SF_UNIT * -103.278929903431851103f; /* log(2^-149) */

// Adapted from Cephes library / Julien Pommier's fast SSE math functions.
v4sf sin4f(v4sf x) {
/* make argument positive but save the sign */
  v4si sign = x < 0;
  x = fabs4f(x);

  v4si j = __builtin_convertvector(F4OPI * x, v4si); /* integer part of x/PIO4 */
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

  // == V4SF_COSCOF_P0*z**8 + V4SF_COSCOF_P1*z**6 + V4SF_COSCOF_P2*z**4 - 0.5*z**2 + 1.0
  const v4sf patha = 1.0f - 0.5f*zz + zz * zz * ((V4SF_COSCOF_P0 * zz + V4SF_COSCOF_P1) * zz + V4SF_COSCOF_P2);

  // == V4SF_SINCOF_P0*z**7 + V4SF_SINCOF_P1*z**5  + V4SF_SINCOF_P2*z**3 + z
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
	4 -1
	5 -1
	6 1
	7 1
   */
  return bitselect4f(j+2 > 3, -y, y);
}

void sincos4f(v4sf x, v4sf* out_sin, v4sf* out_cos) {
  v4si sign = x < 0;
  x = fabs4f(x);

  v4si j = __builtin_convertvector(F4OPI * x, v4si); /* integer part of x/PIO4 */
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

v4sf pow4f(const v4sf base, const v4sf exponent) {
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

void math_test(void) {
  g_assert(v4sf_eq(ldexp4f(V4SF_UNIT, V4SI_UNIT * -127), V4SF_UNIT * 1.0e-30f));
  g_assert(v4sf_eq(ldexp4f(V4SF_UNIT, V4SI_UNIT * -128), V4SF_UNIT * 1.0e-30f));
  g_assert(v4sf_eq(ldexp4f(V4SF_UNIT, V4SI_UNIT * 128), V4SF_UNIT * 1.0e+30f));
	
  g_assert(v4sf_eq(withsignbit4f(V4SF_UNIT, V4SI_TRUE), -V4SF_UNIT));
  g_assert(v4sf_eq(withsignbit4f(-V4SF_UNIT, V4SI_TRUE), -V4SF_UNIT));
  g_assert(v4sf_eq(withsignbit4f(V4SF_UNIT, V4SI_ZERO), V4SF_UNIT));
  g_assert(v4sf_eq(withsignbit4f(-V4SF_UNIT, V4SI_ZERO), V4SF_UNIT));
  
  g_assert(v4sf_eq(copysign4f(V4SF_UNIT, V4SF_UNIT), V4SF_UNIT));
  g_assert(v4sf_eq(copysign4f(-V4SF_UNIT, V4SF_UNIT), V4SF_UNIT));
  g_assert(v4sf_eq(copysign4f(V4SF_UNIT, -V4SF_UNIT), -V4SF_UNIT));
  g_assert(v4sf_eq(copysign4f(-V4SF_UNIT, -V4SF_UNIT), -V4SF_UNIT));
  
  g_assert(v4sf_eq(denorm_strip4f((v4sf)(0x00000001 * V4SI_UNIT)), V4SF_ZERO));
  
  {
    v4sf input = {-1.0f, 0.0f, 3.0f, 2.0f};
    v4sf expected = {0.5f, 1.0f, 8.0f, 4.0f};
    v4sf result = pow4f(2 * V4SF_UNIT, input);
    for (int i = 0; i < 4; ++i) {
      printf("%x %x %x\n", ((v4si)input)[i], ((v4si)result)[i], ((v4si)expected)[i]);
      printf("%.20f %.20f %.20f\n", input[i], result[i], expected[i]);
    }
    g_assert(v4sf_eq(result, expected));
  }

  {
    v4sf input = {-1.5f, 0.0f, 3.0f, 2.0f};
    v4sf expected = {-0.5f, 1.0f, -8.0f, 4.0f};
    v4sf result = pow4f(-2 * V4SF_UNIT, input);
    for (int i = 0; i < 4; ++i) {
      printf("%x %x %x\n", ((v4si)input)[i], ((v4si)result)[i], ((v4si)expected)[i]);
      printf("%.20f %.20f %.20f\n", input[i], result[i], expected[i]);
    }
    g_assert(v4sf_eq(result, expected));
  }
  
  {
    v4sf input = {-1.0f, 1.0f, 100.5f, -100.5f};
    v4sf expected = {1.0f, 1.0f, 100.5f, 100.5f};
    g_assert(v4sf_eq(fabs4f(input), expected));
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
