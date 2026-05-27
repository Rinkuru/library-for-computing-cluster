#ifndef INTEGRAL_H
#define INTEGRAL_H

#define LONG_METHOD_STEPS 200000000L

typedef double (*Func)(double x);

typedef double (*IntegralMethod)(Func f, double a, double b, double eps);

double SimpleFunc(double x);

double HardFunc(double x);

double LongMethod2(Func f, double a, double b, double eps);

double LongMethod(Func f, double a, double b, double eps);

double SlowSimpson(Func f, double a, double b, double eps);

double FastSimpson(Func f, double a, double b, double eps);

#endif
