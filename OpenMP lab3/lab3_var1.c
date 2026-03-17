#include <math.h>
#include <omp.h>
#include <stdio.h>

double sum_with_atomic(double x, long long terms, int threads) {
    double sum = 0.0;

#pragma omp parallel for num_threads(threads) default(none) shared(sum, x, terms)
    for (long long n = 1; n <= terms; n++) {
        double sign = (n % 2 == 0) ? -1.0 : 1.0;
        double term = sign * pow(x, (double)n) / (double)n;

#pragma omp atomic
        sum += term;
    }

    return sum;
}

double sum_with_lock(double x, long long terms, int threads) {
    double sum = 0.0;
    omp_lock_t lock;

    omp_init_lock(&lock);

#pragma omp parallel for num_threads(threads) default(none) shared(sum, x, terms, lock)
    for (long long n = 1; n <= terms; n++) {
        double sign = (n % 2 == 0) ? -1.0 : 1.0;
        double term = sign * pow(x, (double)n) / (double)n;

        omp_set_lock(&lock);
        sum += term;
        omp_unset_lock(&lock);
    }

    omp_destroy_lock(&lock);

    return sum;
}

int main(void) {
    double x;
    double result;
    double exact;
    long long terms;
    int threads;
    int method;

    printf("Obchyslennia ln(1 + x) za riadom\n");
    printf("x nalezhyt (-1; 1]\n");
    printf("Vvedit x: ");
    if (scanf("%lf", &x) != 1) {
        printf("Pomylka vvedennia x.\n");
        return 1;
    }

    if (x <= -1.0 || x > 1.0) {
        printf("x povynen nalezhaty promizhku (-1; 1].\n");
        return 1;
    }

    printf("Vvedit kilkist chleniv riadu: ");
    if (scanf("%lld", &terms) != 1 || terms <= 0) {
        printf("Kilkist chleniv povynna buty dodatnoiu.\n");
        return 1;
    }

    printf("Vvedit kilkist potokiv: ");
    if (scanf("%d", &threads) != 1 || threads <= 0) {
        printf("Kilkist potokiv povynna buty dodatnoiu.\n");
        return 1;
    }

    printf("Obereit metod synkhronizatsii:\n");
    printf("1 - atomic\n");
    printf("2 - lock\n");
    printf("Vash vybir: ");
    if (scanf("%d", &method) != 1) {
        printf("Pomylka vvedennia metodu.\n");
        return 1;
    }

    if (method == 1) {
        result = sum_with_atomic(x, terms, threads);
    } else if (method == 2) {
        result = sum_with_lock(x, terms, threads);
    } else {
        printf("Nevidomyi metod synkhronizatsii.\n");
        return 1;
    }

    exact = log1p(x);

    printf("\nNablyzhene znachennia: %.12f\n", result);
    printf("Tochne znachennia:     %.12f\n", exact);
    printf("Absoliutna pokhybka:   %.12f\n", fabs(result - exact));

    return 0;
}
