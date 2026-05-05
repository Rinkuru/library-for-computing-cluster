#include "integral.h"

#include <math.h>

double SimpsonMethod(Func f, double a, double b, int n) {
    double h = (b - a) / n;
    double sum = 0.0;
    double x0 = a;
    double x1 = a + h;

    for (int i = 0; i <= n-1; ++i) {
        sum += f(x0) + 4*f(x0 + h/2) + f(x1);
        x0 += h;
        x1 += h;
    }
    return (h / 6) * sum;
}

double SlowSimpson(Func f, double a, double b, double eps) {
    int n = 4;
    double ans2 = SimpsonMethod(f, a, b, n);
    double ans1;
    for (;;) { //можно было бы установаить количество итераций, для расходящихся интегралов
        n *= 2;
        ans1 = SimpsonMethod(f, a, b, n);
        if (fabs(ans1 - ans2) <= eps) {
            return ans1 + (ans1 - ans2) / 15.0;
        }
        ans2 = ans1;
    }
    return ans1;
}

double FastSimpson(Func f, double a, double b, double eps) {
    double n = 4.0;
    double sum1 = 0.0, sum2 = 0.0;
    double x, h;
    double index1, index2;
    int i;
    double func;

    for(;;) {
        h = (b - a) / n;
        sum1 = sum2 = -(f(a) + f(b));
        index1 = index2 = 4.0;
        i = 0;
        for(x = a; x <= b + h/2.0; x += h) {
            sum1 += (index1 = 6.0 - index1) * (func=f(x));
            if (i++%2 == 0) {
                sum2 += (index2 = 6.0 - index2) * func;
            };
        };
        if (fabs((sum1 - 2.0*sum2) / sum1) <= eps) {
            return (h * sum1)/3.0 + h*(2.0*sum2 - sum1)/45.0;
        } else {
            n *= 2.0;
        } 
    };
    return (h * sum1) / 3.0;
}

/*
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char **argv) {
    double eps = 0.000000000001;
    printf("slow = %.16f\n", SlowSimpson(SimpleFunc, 0, 1, eps));
    printf("fast   = %.16f\n", FastSimpson(SimpleFunc, 0, 1, eps));
    printf("         %.16f\n", M_PI);
    printf("         %.16f\n", eps);

    double exact = 2.0 + (1.0 - cos(2000000.0)) / 2000000.0; 
    printf("hard slow = %.16f\n", SlowSimpson(HardFunc, 0, 1, eps));
    printf("hard fast   = %.16f\n", FastSimpson(HardFunc, 0, 1, eps));
    printf("hard exact  = %.16f\n", exact);
    printf("hard eps    = %.16f\n", eps);
    return 0;
}
*/
/*
#include <time.h>
int main(int argc, char **argv) {
    double eps = 0.000000000001;
    double exact = 2.0 + (1.0 - cos(2000000.0)) / 2000000.0; 
    
    clock_t start = clock();
    double slow = SlowSimpson(HardFunc, 0, 1, eps);
    clock_t finish = clock();
    double elapsed1 = (double)(finish - start) / CLOCKS_PER_SEC;
    
    start = clock();
    double fast = FastSimpson(HardFunc, 0, 1, eps);
    finish = clock();
    double elapsed2 = (double)(finish - start) / CLOCKS_PER_SEC;

    printf("hard slow = %.16f\n", slow);
    printf("hard fast   = %.16f\n", fast);
    printf("hard exact  = %.16f\n", exact);
    printf("hard eps    = %.16f\n", eps);
    printf("\n\ntime slow = %f\n", elapsed1);
    printf("time fast   = %f\n", elapsed2);
    return 0;
}*/