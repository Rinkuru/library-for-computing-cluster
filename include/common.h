#ifndef COMMON_H
#define COMMON_H

typedef double (*Func)(double x);

typedef double (*Method)(Func f, double a, double b, double eps);

#endif
