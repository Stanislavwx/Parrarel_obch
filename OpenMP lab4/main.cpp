#include <omp.h>

#include <cmath>
#include <iomanip>
#include <iostream>
#include <random>
#include <vector>

using Matrix = std::vector<std::vector<double>>;

const double EPS = 1e-10;
const int THREADS = 4;

Matrix createMatrix(int n) {
    return Matrix(n, std::vector<double>(n, 0.0));
}

void fillRandom(Matrix &a, double minValue, double maxValue, std::mt19937 &generator) {
    std::uniform_real_distribution<double> dist(minValue, maxValue);
    int n = static_cast<int>(a.size());

    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            a[i][j] = dist(generator);
        }
    }
}

bool luSequential(const Matrix &a, Matrix &l, Matrix &u) {
    int n = static_cast<int>(a.size());
    l = createMatrix(n);
    u = createMatrix(n);

    for (int k = 0; k < n; ++k) {
        for (int i = k; i < n; ++i) {
            double sum = 0.0;
            for (int j = 0; j < k; ++j) {
                sum += l[i][j] * u[j][k];
            }
            l[i][k] = a[i][k] - sum;
        }

        if (std::fabs(l[k][k]) < EPS) {
            return false;
        }

        u[k][k] = 1.0;

        for (int i = k + 1; i < n; ++i) {
            double sum = 0.0;
            for (int j = 0; j < k; ++j) {
                sum += l[k][j] * u[j][i];
            }
            u[k][i] = (a[k][i] - sum) / l[k][k];
        }
    }

    return true;
}

bool luParallelFor(const Matrix &a, Matrix &l, Matrix &u, int threads) {
    int n = static_cast<int>(a.size());
    l = createMatrix(n);
    u = createMatrix(n);
    bool ok = true;

#pragma omp parallel num_threads(threads) shared(a, l, u, ok)
    {
        for (int k = 0; k < n; ++k) {
#pragma omp for schedule(static)
            for (int i = k; i < n; ++i) {
                if (!ok) {
                    continue;
                }

                double sum = 0.0;
                for (int j = 0; j < k; ++j) {
                    sum += l[i][j] * u[j][k];
                }
                l[i][k] = a[i][k] - sum;
            }

#pragma omp single
            {
                if (std::fabs(l[k][k]) < EPS) {
                    ok = false;
                } else {
                    u[k][k] = 1.0;
                }
            }

#pragma omp for schedule(static)
            for (int i = k + 1; i < n; ++i) {
                if (!ok) {
                    continue;
                }

                double sum = 0.0;
                for (int j = 0; j < k; ++j) {
                    sum += l[k][j] * u[j][i];
                }
                u[k][i] = (a[k][i] - sum) / l[k][k];
            }
        }
    }

    return ok;
}

bool luParallelTask(const Matrix &a, Matrix &l, Matrix &u, int threads) {
    int n = static_cast<int>(a.size());
    l = createMatrix(n);
    u = createMatrix(n);
    bool ok = true;

#pragma omp parallel num_threads(threads) shared(a, l, u, ok)
    {
#pragma omp single
        {
            for (int k = 0; k < n; ++k) {
                for (int i = k; i < n; ++i) {
#pragma omp task firstprivate(i, k) shared(a, l, u, ok)
                    {
                        if (ok) {
                            double sum = 0.0;
                            for (int j = 0; j < k; ++j) {
                                sum += l[i][j] * u[j][k];
                            }
                            l[i][k] = a[i][k] - sum;
                        }
                    }
                }

#pragma omp taskwait
                if (std::fabs(l[k][k]) < EPS) {
                    ok = false;
                    break;
                }

                u[k][k] = 1.0;

                for (int i = k + 1; i < n; ++i) {
#pragma omp task firstprivate(i, k) shared(a, l, u, ok)
                    {
                        if (ok) {
                            double sum = 0.0;
                            for (int j = 0; j < k; ++j) {
                                sum += l[k][j] * u[j][i];
                            }
                            u[k][i] = (a[k][i] - sum) / l[k][k];
                        }
                    }
                }

#pragma omp taskwait
            }
        }
    }

    return ok;
}

double checkLU(const Matrix &a, const Matrix &l, const Matrix &u) {
    int n = static_cast<int>(a.size());
    double maxError = 0.0;

    for (int i = 0; i < n; ++i) {
        for (int k = 0; k < n; ++k) {
            double sum = 0.0;
            for (int j = 0; j < n; ++j) {
                sum += l[i][j] * u[j][k];
            }

            double error = std::fabs(a[i][k] - sum);
            if (error > maxError) {
                maxError = error;
            }
        }
    }

    return maxError;
}

int getRepeats(int n) {
    if (n == 10) {
        return 2000;
    }
    if (n == 100) {
        return 200;
    }
    if (n == 500) {
        return 5;
    }
    return 2;
}

Matrix generateGoodMatrix(int n, double minValue, double maxValue) {
    std::mt19937 generator(42 + n);
    Matrix a = createMatrix(n);
    Matrix l;
    Matrix u;

    while (true) {
        fillRandom(a, minValue, maxValue, generator);
        if (luSequential(a, l, u)) {
            return a;
        }
    }
}

double measureSequential(const Matrix &a, int repeats) {
    Matrix l;
    Matrix u;
    double total = 0.0;

    for (int i = 0; i < repeats; ++i) {
        double start = omp_get_wtime();
        if (!luSequential(a, l, u)) {
            return -1.0;
        }
        double end = omp_get_wtime();
        total += end - start;
    }

    return total * 1000.0 / repeats;
}

double measureParallelFor(const Matrix &a, int repeats, int threads) {
    Matrix l;
    Matrix u;
    double total = 0.0;

    for (int i = 0; i < repeats; ++i) {
        double start = omp_get_wtime();
        if (!luParallelFor(a, l, u, threads)) {
            return -1.0;
        }
        double end = omp_get_wtime();
        total += end - start;
    }

    return total * 1000.0 / repeats;
}

double measureParallelTask(const Matrix &a, int repeats, int threads) {
    Matrix l;
    Matrix u;
    double total = 0.0;

    for (int i = 0; i < repeats; ++i) {
        double start = omp_get_wtime();
        if (!luParallelTask(a, l, u, threads)) {
            return -1.0;
        }
        double end = omp_get_wtime();
        total += end - start;
    }

    return total * 1000.0 / repeats;
}

int main() {
    std::vector<int> sizes = {10, 100, 500, 1000};
    double minValue;
    double maxValue;

    std::cout << "Введіть мінімальне і максимальне випадкове значення: ";
    std::cin >> minValue >> maxValue;

    if (minValue >= maxValue) {
        std::cout << "Помилка: мінімальне значення повинно бути менше за максимальне.\n";
        return 1;
    }

    omp_set_dynamic(0);

    std::cout << "\nКількість потоків: " << THREADS << "\n";
    std::cout << "Розміри матриць: 10, 100, 500, 1000\n\n";

    std::cout << std::left << std::setw(8) << "n"
              << std::setw(10) << "Повт."
              << std::setw(18) << "Seq, мс"
              << std::setw(18) << "omp for, мс"
              << std::setw(18) << "omp task, мс"
              << std::setw(18) << "Err seq"
              << std::setw(18) << "Err for"
              << std::setw(18) << "Err task"
              << "\n";

    for (int n : sizes) {
        int repeats = getRepeats(n);
        Matrix a = generateGoodMatrix(n, minValue, maxValue);
        Matrix l;
        Matrix u;

        if (!luSequential(a, l, u)) {
            std::cout << "Помилка в послідовному LU-розкладі.\n";
            return 1;
        }
        double errorSeq = checkLU(a, l, u);

        if (!luParallelFor(a, l, u, THREADS)) {
            std::cout << "Помилка в LU-розкладі через omp for.\n";
            return 1;
        }
        double errorFor = checkLU(a, l, u);

        if (!luParallelTask(a, l, u, THREADS)) {
            std::cout << "Помилка в LU-розкладі через omp task.\n";
            return 1;
        }
        double errorTask = checkLU(a, l, u);

        double timeSeq = measureSequential(a, repeats);
        double timeFor = measureParallelFor(a, repeats, THREADS);
        double timeTask = measureParallelTask(a, repeats, THREADS);

        std::cout << std::left << std::setw(8) << n
                  << std::setw(10) << repeats
                  << std::setw(18) << std::fixed << std::setprecision(4) << timeSeq
                  << std::setw(18) << timeFor
                  << std::setw(18) << timeTask
                  << std::setw(18) << errorSeq
                  << std::setw(18) << errorFor
                  << std::setw(18) << errorTask
                  << "\n";
    }

    return 0;
}
