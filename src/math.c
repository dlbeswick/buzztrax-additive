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
const v4sf MINLOGF = V4SF_UNIT * -103.278929903431851103f; /* log(2^-149) */

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
  
  v4sf inputxa = (v4sf)(0x33000000 * V4SI_UNIT);
  v4sf inputxb = (v4sf)(0x40b36000 * V4SI_UNIT);
  printf("%f\n",pow4f(inputxa, inputxb)[0]);
  
  g_assert(v4sf_eq(denorm_strip4f(V4SF_MIN_NORM_POS - V4SF_MIN_NORM_POS*0.01f), V4SF_ZERO));
  
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
