// e7: interface of the port of V8 src/base/ieee754.{h,cc} (fdlibm). See LICENSE and VENDORED.txt.
//
// Copyright 2016 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// These are the functions Node.js (V8) uses for Math.acos ... Math.tanh and, in Node.js 22 (and
// Node.js 24 with --no-use-std-math-pow), for Math.pow and **. They give the same double on
// every platform: plain IEEE double arithmetic, no FMA contraction, no platform libm (only the
// exact sqrt / fabs / floor / scalbn).
#ifndef E7_THIRD_PARTY_FDLIBM_IEEE754_H_
#define E7_THIRD_PARTY_FDLIBM_IEEE754_H_

namespace e7 {
namespace fdlibm {

double acos(double x);
double acosh(double x);
double asin(double x);
double asinh(double x);
double atan(double x);
double atan2(double y, double x);
double cos(double x);
double sin(double x);
double tan(double x);
double exp(double x);
double atanh(double x);
double log(double x);
double log1p(double x);
double log2(double x);
double log10(double x);
double cbrt(double x);
double expm1(double x);
double cosh(double x);
double sinh(double x);
double tanh(double x);

// x to the power y with the ECMAScript special cases (NaN for (+-1) ** (+-Infinity), NaN ** 0 = 1);
// V8's base::ieee754::legacy::pow (base::ieee754::pow in V8 12.4).
double pow(double x, double y);

}  // namespace fdlibm
}  // namespace e7

#endif  // E7_THIRD_PARTY_FDLIBM_IEEE754_H_
