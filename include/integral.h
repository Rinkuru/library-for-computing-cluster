#ifndef INTEGRAL_H
#define INTEGRAL_H

#include "common.h"

double SlowSimpson(Func f, double a, double b, double eps);

double FastSimpson(Func f, double a, double b, double eps);

#endif