# Проєктна робота OpenMP

## Тема

Варіант 4: **«Паралельний аналіз логів сервера та виявлення мережевих аномалій»**

## Що робить програма

Програма працює з великими текстовими логами у форматі:

```text
timestamp source_ip destination_ip method port status bytes
```

Приклад рядка:

```text
2026-03-01T12:05:10 203.0.113.10 10.0.0.10 GET 80 200 1200
```

Програма вміє:

- згенерувати лог випадково;
- згенерувати лог за детермінованою формулою;
- послідовно проаналізувати лог;
- паралельно проаналізувати лог за допомогою OpenMP;
- підрахувати статистику запитів за IP-адресами;
- знайти можливі аномалії;
- зберегти результати у текстовий файл і CSV.

## Які аномалії шукаються

1. **Можливий DDoS**
IP має дуже велику кількість запитів. У програмі використано поріг `10000` запитів.

2. **Можливе сканування портів**
IP звертався до великої кількості різних портів. У програмі використано поріг `20` портів.

3. **Підозріло високий відсоток помилок**
IP має щонайменше `150` запитів і не менше `60%` помилкових відповідей.

## Які конструкції OpenMP використані

- `#pragma omp parallel`
- `#pragma omp master`
- `#pragma omp single`
- `#pragma omp task`
- `#pragma omp taskwait`
- `#pragma omp for`
- `#pragma omp atomic`
- `omp_lock_t`
- `omp_get_wtime()`

Тобто вимога методички щодо використання тем лабораторних робіт і `task` виконана.

## Структура проєкту

- [src/log_analyzer.c](/home/stanislav/Desktop/Parralel%20/OMP_Project/src/log_analyzer.c)
- [README.md](/home/stanislav/Desktop/Parralel%20/OMP_Project/README.md)
- [report/project_report.docx](/home/stanislav/Desktop/Parralel%20/OMP_Project/report/project_report.docx)
- [results/random_report.txt](/home/stanislav/Desktop/Parralel%20/OMP_Project/results/random_report.txt)
- [results/formula_report.txt](/home/stanislav/Desktop/Parralel%20/OMP_Project/results/formula_report.txt)
- [results/random_benchmark.csv](/home/stanislav/Desktop/Parralel%20/OMP_Project/results/random_benchmark.csv)
- [results/formula_benchmark.csv](/home/stanislav/Desktop/Parralel%20/OMP_Project/results/formula_benchmark.csv)

## Як скомпілювати

### Варіант 1. Якщо є `gcc`

```bash
gcc -O2 -Wall -Wextra -std=c11 -fopenmp src/log_analyzer.c -o omp_log_analyzer
```

### Варіант 2. Якщо локально немає `gcc`, але є `podman`

```bash
podman run --rm -v "$PWD":/work:Z -w /work docker.io/library/gcc:14 \
  bash -lc "gcc -O2 -Wall -Wextra -std=c11 -fopenmp src/log_analyzer.c -o omp_log_analyzer"
```

## Як запускати

### 1. Згенерувати випадковий лог

```bash
./omp_log_analyzer generate-random data/random_log.txt 100000
```

### 2. Згенерувати лог за формулою

```bash
./omp_log_analyzer generate-formula data/formula_log.txt 150000
```

### 3. Проаналізувати лог

```bash
./omp_log_analyzer analyze data/random_log.txt results/random_report.txt results/random_stats.csv 4 1
```

Де:

- `4` — кількість потоків;
- `1` — розмір блока в мегабайтах.

### 4. Зняти таблицю продуктивності

```bash
./omp_log_analyzer benchmark data/random_log.txt results/random_benchmark.csv 8 1
```

У цьому випадку програма перевірить варіанти з `1`, `2`, `4` і `8` потоками.

## Як програма працює

1. Файл ділиться на блоки.
2. Межі блоків зміщуються до символу нового рядка, щоб один лог-запис не розірвався між двома потоками.
3. У послідовному режимі блоки обробляються один за одним.
4. У паралельному режимі для кожного блока створюється `OpenMP task`.
5. Кожен потік парсить свій блок і накопичує локальну статистику.
6. Після цього локальна статистика зливається в загальну таблицю.
7. Окремим проходом по зібраній статистиці визначаються аномальні IP.

## Що означають результати у файлах

### `report.txt`

Містить:

- час послідовного аналізу;
- час паралельного аналізу;
- прискорення;
- кількість знайдених аномалій;
- топ IP-адрес за кількістю запитів;
- список підозрілих IP.

### `stats.csv`

Містить детальну таблицю:

- IP-адреса;
- кількість запитів;
- кількість помилок;
- відсоток помилок;
- кількість унікальних портів;
- позначки `ddos`, `port_scan`, `error_source`.

## Як виконані вимоги методички

1. Є короткі теоретичні відомості у звіті.
2. Код прокоментований.
3. Є три способи отримання вхідних даних:
ручне задання шляху до власного лог-файлу, випадкова генерація, генерація за функцією.
4. Результати виводяться у файл.
5. Використано OpenMP і `task`.
6. Є чіткий результат роботи програми.
7. Є порівняння послідовного і паралельного варіантів для різної кількості потоків і різних розмірів даних.
8. Повний код є окремим файлом і додатком у звіті.

## Які файли краще показати викладачу

- [src/log_analyzer.c](/home/stanislav/Desktop/Parralel%20/OMP_Project/src/log_analyzer.c)
- [report/project_report.docx](/home/stanislav/Desktop/Parralel%20/OMP_Project/report/project_report.docx)
- [results/random_report.txt](/home/stanislav/Desktop/Parralel%20/OMP_Project/results/random_report.txt)
- [results/formula_report.txt](/home/stanislav/Desktop/Parralel%20/OMP_Project/results/formula_report.txt)
- [results/random_benchmark.csv](/home/stanislav/Desktop/Parralel%20/OMP_Project/results/random_benchmark.csv)
- [results/formula_benchmark.csv](/home/stanislav/Desktop/Parralel%20/OMP_Project/results/formula_benchmark.csv)
