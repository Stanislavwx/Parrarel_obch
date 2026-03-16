#include <stdio.h>
#include <omp.h>

int main() {
    //Потоки
    int n = 4;
    omp_set_num_threads(n);

    //2) parallel: кожен потік друкує інформацію
    #pragma omp parallel
    {
        int tid = omp_get_thread_num();
        int nt  = omp_get_num_threads();
        int np  = omp_get_num_procs();

        printf("CPU available: %d | I am %d thread from %d threads!\n", np, tid, nt);
    }

    //3) Приклади private/shared/firstprivate/reduction
    int shared_x = 10;     
    int sum = 0;

    #pragma omp parallel shared(shared_x) reduction(+:sum)
    {
        int tid = omp_get_thread_num();

        //private 
        int priv = 0;
        priv = tid + shared_x;

        //reduction
        sum += priv;
    }
    printf("After reduction: sum = %d (depends on n and shared_x)\n", sum);

    //firstprivate
    int a = 5;
    #pragma omp parallel firstprivate(a)
    {
        int tid = omp_get_thread_num();
        a += tid; // кожен потік змінює СВОЮ копію
        printf("firstprivate demo: tid=%d, a(local)=%d\n", tid, a);
    }
    printf("After firstprivate: a(main)=%d (unchanged)\n", a);

    //4) sections: незалежні секції коду в паралельній області
    #pragma omp parallel
    {
        #pragma omp sections
        {
            #pragma omp section
            {
                printf("Section 1 executed by tid=%d\n", omp_get_thread_num());
            }
            #pragma omp section
            {
                printf("Section 2 executed by tid=%d\n", omp_get_thread_num());
            }
            #pragma omp section
            {
                printf("Section 3 executed by tid=%d\n", omp_get_thread_num());
            }
        }
    }

    return 0;
}