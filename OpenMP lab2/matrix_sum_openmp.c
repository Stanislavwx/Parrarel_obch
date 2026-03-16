#include <stdio.h>
#include <stdlib.h>
#include <omp.h>

long long sequential_sum(int *matrix, int rows, int cols) {
    long long sum = 0;

    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            sum += matrix[i * cols + j];
        }
    }

    return sum;
}

long long parallel_sum(int *matrix, int rows, int cols, int threads) {
    long long sum = 0;

    omp_set_num_threads(threads);

#pragma omp parallel for reduction(+ : sum)
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            sum += matrix[i * cols + j];
        }
    }

    return sum;
}

void demo_schedule(int threads, int iterations, omp_sched_t type, int chunk, const char *name) {
    omp_set_num_threads(threads);
    omp_set_schedule(type, chunk);

    printf("\nSchedule(%s", name);
    if (chunk > 0) {
        printf(", %d", chunk);
    }
    printf(")\n");

#pragma omp parallel for schedule(runtime)
    for (int i = 0; i < iterations; i++) {
#pragma omp critical
        printf("[%d]: calculation of the iteration number %d.\n", omp_get_thread_num(), i);
    }
}

int main(void) {
    int sizes[] = {1000, 2000, 4000};
    int threads_list[] = {1, 2, 4, 8};
    int size_count = sizeof(sizes) / sizeof(sizes[0]);
    int thread_count = sizeof(threads_list) / sizeof(threads_list[0]);

    omp_set_dynamic(0);
    srand(1);

    printf("Finding the sum of matrix elements\n");

    for (int s = 0; s < size_count; s++) {
        int n = sizes[s];
        int *matrix = malloc((size_t)n * n * sizeof(int));

        if (matrix == NULL) {
            printf("Memory allocation error\n");
            return 1;
        }
// заповнення матриці випадковими числами
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                matrix[i * n + j] = rand() % 100;
            }
        }

        double start = omp_get_wtime();
        long long seq_sum = sequential_sum(matrix, n, n);
        double end = omp_get_wtime();
        double seq_time = end - start;

        printf("\nMatrix size: %d x %d\n", n, n);
        printf("Sequential sum = %lld, time = %f s\n", seq_sum, seq_time);

        for (int t = 0; t < thread_count; t++) {
            int threads = threads_list[t];

            start = omp_get_wtime();
            long long par_sum = parallel_sum(matrix, n, n, threads);
            end = omp_get_wtime();

            printf("Threads = %d, parallel sum = %lld, time = %f s, speedup = %f, check = %s\n",
                   threads,
                   par_sum,
                   end - start,
                   seq_time / (end - start),
                   (seq_sum == par_sum) ? "OK" : "ERROR");
        }

        free(matrix);
    }

    printf("\nDistribution of iterations between threads:\n");
    demo_schedule(4, 8, omp_sched_static, 0, "static");
    demo_schedule(4, 8, omp_sched_dynamic, 1, "dynamic");
    demo_schedule(4, 8, omp_sched_guided, 2, "guided");

    return 0;
}
