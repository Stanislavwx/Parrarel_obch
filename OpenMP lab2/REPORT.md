# Лабораторна робота №2

## Тема роботи

Паралельні цикли в OpenMP-програмах.

## Мета роботи

Ознайомитися з директивою `#pragma omp for` та її опціями, навчитися
розпаралелювати цикл в OpenMP і вимірювати час виконання програми.

## Індивідуальне завдання

Варіант 2: написати паралельну OpenMP програму, яка знаходить суму елементів матриці.

## Короткі теоретичні відомості

Директива `#pragma omp parallel for` дозволяє розподілити ітерації циклу між потоками.

Опція `reduction(+:sum)` використовується тоді, коли кілька потоків обчислюють спільну суму.
Кожний потік має свою локальну копію змінної `sum`, а після завершення циклу всі локальні суми додаються.

Опція `schedule(...)` задає спосіб розподілу ітерацій між потоками:

- `static` - ітерації розподіляються наперед;
- `dynamic` - ітерації видаються потокам під час роботи;
- `guided` - спочатку видаються більші блоки, потім менші.

## Хід роботи

Було написано програму мовою C з використанням OpenMP.

Програма:

1. створює квадратну матрицю і заповнює її псевдовипадковими числами;
2. обчислює суму її елементів послідовно;
3. обчислює суму її елементів паралельно за допомогою `reduction`;
4. вимірює час роботи через `omp_get_wtime()` для різних розмірів матриці і різної кількості потоків;
5. демонструє роботу `schedule(static)`, `schedule(dynamic, 1)` та `schedule(guided, 2)`.

Файл програми: `matrix_sum_openmp.c`

## Запуск програми

Спочатку потрібно скомпілювати програму:

```bash
make
```

Після цього запустити її:

```bash
./matrix_sum_openmp
```

Якщо `make` не використовувати, програму можна скомпілювати так:

```bash
gcc -fopenmp matrix_sum_openmp.c -o matrix_sum_openmp
./matrix_sum_openmp
```

## Основний фрагмент програми

```c
#pragma omp parallel for reduction(+ : sum)
for (int i = 0; i < rows; i++) {
    for (int j = 0; j < cols; j++) {
        sum += matrix[i * cols + j];
    }
}
```

## Результати виконання програми

### Матриця 1000 x 1000

Послідовне виконання:

- сума = `49498583`
- час = `0.000727` с

Паралельне виконання:

| Кількість потоків | Сума | Час, с | Прискорення | Перевірка |
|---|---:|---:|---:|---|
| 1 | 49498583 | 0.000887 | 0.819547 | OK |
| 2 | 49498583 | 0.000633 | 1.149067 | OK |
| 4 | 49498583 | 0.000497 | 1.463463 | OK |
| 8 | 49498583 | 0.000523 | 1.390943 | OK |

### Матриця 2000 x 2000

Послідовне виконання:

- сума = `198022445`
- час = `0.003372` с

Паралельне виконання:

| Кількість потоків | Сума | Час, с | Прискорення | Перевірка |
|---|---:|---:|---:|---|
| 1 | 198022445 | 0.003616 | 0.932491 | OK |
| 2 | 198022445 | 0.001989 | 1.694966 | OK |
| 4 | 198022445 | 0.001315 | 2.563509 | OK |
| 8 | 198022445 | 0.001053 | 3.201326 | OK |

### Матриця 4000 x 4000

Послідовне виконання:

- сума = `792128424`
- час = `0.014018` с

Паралельне виконання:

| Кількість потоків | Сума | Час, с | Прискорення | Перевірка |
|---|---:|---:|---:|---|
| 1 | 792128424 | 0.014544 | 0.963859 | OK |
| 2 | 792128424 | 0.007738 | 1.811506 | OK |
| 4 | 792128424 | 0.004151 | 3.376903 | OK |
| 8 | 792128424 | 0.003072 | 4.563007 | OK |

## Демонстрація schedule

### `schedule(static)`

```text
[2]: calculation of the iteration number 4.
[2]: calculation of the iteration number 5.
[1]: calculation of the iteration number 2.
[1]: calculation of the iteration number 3.
[3]: calculation of the iteration number 6.
[3]: calculation of the iteration number 7.
[0]: calculation of the iteration number 0.
[0]: calculation of the iteration number 1.
```

### `schedule(dynamic, 1)`

```text
[1]: calculation of the iteration number 0.
[3]: calculation of the iteration number 1.
[2]: calculation of the iteration number 2.
[2]: calculation of the iteration number 6.
[2]: calculation of the iteration number 7.
[3]: calculation of the iteration number 5.
[0]: calculation of the iteration number 3.
[1]: calculation of the iteration number 4.
```

### `schedule(guided, 2)`

```text
[2]: calculation of the iteration number 0.
[2]: calculation of the iteration number 1.
[1]: calculation of the iteration number 4.
[1]: calculation of the iteration number 5.
[0]: calculation of the iteration number 6.
[0]: calculation of the iteration number 7.
[3]: calculation of the iteration number 2.
[3]: calculation of the iteration number 3.
```

З наведених результатів видно, що при різних типах `schedule` ітерації циклу розподіляються між потоками по-різному.

## Висновок

У ході лабораторної роботи було вивчено директиву `#pragma omp for` та її опції.
Було написано паралельну OpenMP програму для знаходження суми елементів матриці.
Для правильного підсумовування використано `reduction`.
За допомогою `omp_get_wtime()` виміряно час роботи програми для різних розмірів матриці та різної кількості потоків.
Також було продемонстровано роботу `schedule(static)`, `schedule(dynamic, 1)` і `schedule(guided, 2)`.
У всіх випадках паралельний результат збігся з послідовним.
