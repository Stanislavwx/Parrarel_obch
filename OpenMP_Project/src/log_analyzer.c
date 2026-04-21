#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <omp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#define MAX_IP_LENGTH 16
#define MAX_METHOD_LENGTH 8
#define MAX_TRACKED_PORTS 64
#define DEFAULT_CHUNK_MB 4
#define DEFAULT_DDOS_THRESHOLD 10000
#define DEFAULT_PORT_SCAN_THRESHOLD 20
#define DEFAULT_ERROR_RATE_THRESHOLD 60
#define DEFAULT_ERROR_MIN_REQUESTS 150

typedef struct {
    char ip[MAX_IP_LENGTH];
    long long requests;
    long long error_requests;
    int unique_ports;
    int tracked_ports[MAX_TRACKED_PORTS];
    int port_overflow;
    int used;
} IpStat;

typedef struct {
    IpStat *entries;
    size_t capacity;
    size_t used_count;
} StatsTable;

typedef struct {
    long long total_lines;
    long long valid_lines;
    long long invalid_lines;
    StatsTable stats;
} AnalysisResult;

typedef struct {
    long long start;
    long long end;
} ChunkRange;

typedef struct {
    char ip[MAX_IP_LENGTH];
    long long requests;
    long long error_requests;
    int unique_ports;
    int is_ddos;
    int is_port_scan;
    int is_error_source;
} ReportRow;

static int build_chunk_ranges(const char *path, size_t chunk_size_bytes, ChunkRange **ranges, int *count);
static int process_chunk(const char *path, ChunkRange range, AnalysisResult *result);

static unsigned long hash_ip(const char *text) {
    unsigned long hash = 1469598103934665603UL;

    while (*text != '\0') {
        hash ^= (unsigned char) *text;
        hash *= 1099511628211UL;
        ++text;
    }

    return hash;
}

static size_t next_power_of_two(size_t value) {
    size_t result = 1024;

    while (result < value) {
        result <<= 1;
    }

    return result;
}

static void print_usage(const char *program_name) {
    printf("Usage:\n");
    printf("  %s generate-random <output_file> <line_count>\n", program_name);
    printf("  %s generate-formula <output_file> <line_count>\n", program_name);
    printf("  %s analyze <input_file> <report_file> <stats_csv> [threads] [chunk_mb]\n", program_name);
    printf("  %s benchmark <input_file> <output_csv> <max_threads> [chunk_mb]\n", program_name);
    printf("\n");
    printf("Log format:\n");
    printf("  timestamp source_ip destination_ip method port status bytes\n");
}

static int parse_long_long(const char *text, long long *value) {
    char *end_ptr = NULL;
    long long parsed = strtoll(text, &end_ptr, 10);

    if (text == end_ptr || *end_ptr != '\0') {
        return 0;
    }

    *value = parsed;
    return 1;
}

static int parse_int_value(const char *text, int *value) {
    char *end_ptr = NULL;
    long parsed = strtol(text, &end_ptr, 10);

    if (text == end_ptr || *end_ptr != '\0') {
        return 0;
    }

    *value = (int) parsed;
    return 1;
}

static int get_file_size(const char *path, long long *file_size) {
    struct stat info;

    if (stat(path, &info) != 0) {
        fprintf(stderr, "Cannot read file info for %s: %s\n", path, strerror(errno));
        return 0;
    }

    *file_size = (long long) info.st_size;
    return 1;
}

static void free_stats_table(StatsTable *table) {
    free(table->entries);
    table->entries = NULL;
    table->capacity = 0;
    table->used_count = 0;
}

static int rehash_table(StatsTable *table, size_t new_capacity) {
    size_t i;
    IpStat *new_entries = (IpStat *) calloc(new_capacity, sizeof(IpStat));

    if (new_entries == NULL) {
        return 0;
    }

    for (i = 0; i < table->capacity; ++i) {
        if (table->entries[i].used) {
            size_t index = hash_ip(table->entries[i].ip) & (new_capacity - 1);

            while (new_entries[index].used) {
                index = (index + 1) & (new_capacity - 1);
            }

            new_entries[index] = table->entries[i];
        }
    }

    free(table->entries);
    table->entries = new_entries;
    table->capacity = new_capacity;
    return 1;
}

static int init_stats_table(StatsTable *table, size_t estimated_items) {
    size_t capacity = next_power_of_two(estimated_items * 2 + 1);

    table->entries = (IpStat *) calloc(capacity, sizeof(IpStat));
    if (table->entries == NULL) {
        fprintf(stderr, "Not enough memory for statistics table.\n");
        return 0;
    }

    table->capacity = capacity;
    table->used_count = 0;
    return 1;
}

static int ensure_table_capacity(StatsTable *table) {
    if ((table->used_count + 1) * 10 < table->capacity * 7) {
        return 1;
    }

    return rehash_table(table, table->capacity * 2);
}

static IpStat *find_ip_slot(StatsTable *table, const char *ip, int create_if_missing) {
    size_t index;

    /* Open addressing keeps the structure simple and fast enough for the course project. */
    if (create_if_missing && !ensure_table_capacity(table)) {
        return NULL;
    }

    index = hash_ip(ip) & (table->capacity - 1);
    while (table->entries[index].used) {
        if (strcmp(table->entries[index].ip, ip) == 0) {
            return &table->entries[index];
        }

        index = (index + 1) & (table->capacity - 1);
    }

    if (!create_if_missing) {
        return NULL;
    }

    memset(&table->entries[index], 0, sizeof(IpStat));
    table->entries[index].used = 1;
    snprintf(table->entries[index].ip, sizeof(table->entries[index].ip), "%s", ip);
    table->used_count++;
    return &table->entries[index];
}

static const IpStat *lookup_ip_stat(const StatsTable *table, const char *ip) {
    size_t index = hash_ip(ip) & (table->capacity - 1);

    while (table->entries[index].used) {
        if (strcmp(table->entries[index].ip, ip) == 0) {
            return &table->entries[index];
        }

        index = (index + 1) & (table->capacity - 1);
    }

    return NULL;
}

static void update_port_info(IpStat *stat, int port) {
    int tracked_count;
    int i;

    if (port <= 0) {
        return;
    }

    tracked_count = stat->unique_ports < MAX_TRACKED_PORTS ? stat->unique_ports : MAX_TRACKED_PORTS;
    for (i = 0; i < tracked_count; ++i) {
        if (stat->tracked_ports[i] == port) {
            return;
        }
    }

    /* We keep exact ports only up to a small limit. For anomaly detection it is enough
       to know that the number of different ports is "more than the limit". */
    if (stat->unique_ports < MAX_TRACKED_PORTS) {
        stat->tracked_ports[stat->unique_ports] = port;
        stat->unique_ports++;
    } else {
        stat->port_overflow = 1;
        stat->unique_ports = MAX_TRACKED_PORTS + 1;
    }
}

static int update_stats_table(StatsTable *table, const char *ip, int port, int status) {
    IpStat *stat = find_ip_slot(table, ip, 1);

    if (stat == NULL) {
        return 0;
    }

    stat->requests++;
    if (status >= 400) {
        stat->error_requests++;
    }

    update_port_info(stat, port);
    return 1;
}

static int merge_ip_stat(StatsTable *destination, const IpStat *source) {
    int tracked_count;
    int i;
    IpStat *target = find_ip_slot(destination, source->ip, 1);

    if (target == NULL) {
        return 0;
    }

    target->requests += source->requests;
    target->error_requests += source->error_requests;

    tracked_count = source->unique_ports < MAX_TRACKED_PORTS ? source->unique_ports : MAX_TRACKED_PORTS;
    for (i = 0; i < tracked_count; ++i) {
        update_port_info(target, source->tracked_ports[i]);
    }

    if (source->port_overflow || source->unique_ports > MAX_TRACKED_PORTS) {
        target->port_overflow = 1;
        if (target->unique_ports <= MAX_TRACKED_PORTS) {
            target->unique_ports = MAX_TRACKED_PORTS + 1;
        }
    }

    return 1;
}

static int merge_tables(StatsTable *destination, const StatsTable *source) {
    size_t i;

    for (i = 0; i < source->capacity; ++i) {
        if (source->entries[i].used && !merge_ip_stat(destination, &source->entries[i])) {
            return 0;
        }
    }

    return 1;
}

static int init_analysis_result(AnalysisResult *result, size_t estimated_items) {
    result->total_lines = 0;
    result->valid_lines = 0;
    result->invalid_lines = 0;
    return init_stats_table(&result->stats, estimated_items);
}

static void free_analysis_result(AnalysisResult *result) {
    free_stats_table(&result->stats);
    result->total_lines = 0;
    result->valid_lines = 0;
    result->invalid_lines = 0;
}

static size_t estimate_initial_items(long long file_size) {
    size_t estimate = (size_t) (file_size / 96);

    if (estimate < 1024) {
        estimate = 1024;
    }

    return estimate;
}

static int parse_log_line(const char *line, char *source_ip, int *port, int *status) {
    char timestamp[32];
    char destination_ip[MAX_IP_LENGTH];
    char method[MAX_METHOD_LENGTH];
    int bytes = 0;

    if (sscanf(line, "%31s %15s %15s %7s %d %d %d",
               timestamp,
               source_ip,
               destination_ip,
               method,
               port,
               status,
               &bytes) != 7) {
        return 0;
    }

    return 1;
}

static int analyze_line(const char *line, AnalysisResult *result) {
    char source_ip[MAX_IP_LENGTH];
    int port = 0;
    int status = 0;

    /* Every valid line updates the aggregated statistics for one source IP. */
    result->total_lines++;
    if (!parse_log_line(line, source_ip, &port, &status)) {
        result->invalid_lines++;
        return 1;
    }

    result->valid_lines++;
    return update_stats_table(&result->stats, source_ip, port, status);
}

static int analyze_buffer(char *buffer, size_t size, AnalysisResult *result) {
    size_t i;
    char *line_start = buffer;

    for (i = 0; i <= size; ++i) {
        if (buffer[i] == '\n' || buffer[i] == '\0') {
            if (line_start < buffer + i) {
                if (*(buffer + i - 1) == '\r') {
                    *(buffer + i - 1) = '\0';
                }
                buffer[i] = '\0';
                if (!analyze_line(line_start, result)) {
                    return 0;
                }
            }
            line_start = buffer + i + 1;
        }
    }

    return 1;
}

static int analyze_file_sequential(const char *path, size_t chunk_size_bytes, AnalysisResult *result) {
    ChunkRange *ranges = NULL;
    int range_count = 0;
    int i;
    long long file_size = 0;

    if (!get_file_size(path, &file_size)) {
        return 0;
    }

    if (!build_chunk_ranges(path, chunk_size_bytes, &ranges, &range_count)) {
        return 0;
    }

    if (!init_analysis_result(result, estimate_initial_items(file_size))) {
        free(ranges);
        return 0;
    }

    for (i = 0; i < range_count; ++i) {
        AnalysisResult local_result;

        if (!process_chunk(path, ranges[i], &local_result)) {
            free_analysis_result(result);
            free(ranges);
            return 0;
        }

        if (!merge_tables(&result->stats, &local_result.stats)) {
            fprintf(stderr, "Not enough memory during sequential analysis.\n");
            free_analysis_result(&local_result);
            free_analysis_result(result);
            free(ranges);
            return 0;
        }

        result->total_lines += local_result.total_lines;
        result->valid_lines += local_result.valid_lines;
        result->invalid_lines += local_result.invalid_lines;
        free_analysis_result(&local_result);
    }

    free(ranges);
    return 1;
}

static int build_chunk_ranges(const char *path, size_t chunk_size_bytes, ChunkRange **ranges, int *count) {
    ChunkRange *items = NULL;
    int item_count = 0;
    int item_capacity = 0;
    long long file_size = 0;
    long long start = 0;
    FILE *input = NULL;

    if (!get_file_size(path, &file_size)) {
        return 0;
    }

    if (file_size == 0) {
        *ranges = NULL;
        *count = 0;
        return 1;
    }

    item_capacity = (int) (file_size / (long long) chunk_size_bytes) + 2;
    items = (ChunkRange *) malloc((size_t) item_capacity * sizeof(ChunkRange));
    if (items == NULL) {
        fprintf(stderr, "Not enough memory for chunk ranges.\n");
        return 0;
    }

    input = fopen(path, "rb");
    if (input == NULL) {
        fprintf(stderr, "Cannot open %s: %s\n", path, strerror(errno));
        free(items);
        return 0;
    }

    while (start < file_size) {
        long long end = start + (long long) chunk_size_bytes;

        if (end >= file_size) {
            end = file_size;
        } else {
            int ch;
            /* The end of the chunk is moved to the next line break,
               so different tasks never split the same log record. */
            if (fseeko(input, (off_t) end, SEEK_SET) != 0) {
                fprintf(stderr, "Cannot seek in %s.\n", path);
                fclose(input);
                free(items);
                return 0;
            }

            while (end < file_size && (ch = fgetc(input)) != EOF) {
                end++;
                if (ch == '\n') {
                    break;
                }
            }
        }

        items[item_count].start = start;
        items[item_count].end = end;
        item_count++;
        start = end;
    }

    fclose(input);
    *ranges = items;
    *count = item_count;
    return 1;
}

static int process_chunk(const char *path, ChunkRange range, AnalysisResult *result) {
    char *buffer = NULL;
    size_t bytes_to_read = (size_t) (range.end - range.start);
    size_t bytes_read;
    FILE *input = NULL;

    if (!init_analysis_result(result, estimate_initial_items(range.end - range.start))) {
        return 0;
    }

    buffer = (char *) malloc(bytes_to_read + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Not enough memory for file chunk.\n");
        free_analysis_result(result);
        return 0;
    }

    input = fopen(path, "rb");
    if (input == NULL) {
        fprintf(stderr, "Cannot open %s: %s\n", path, strerror(errno));
        free(buffer);
        free_analysis_result(result);
        return 0;
    }

    if (fseeko(input, (off_t) range.start, SEEK_SET) != 0) {
        fprintf(stderr, "Cannot seek in %s.\n", path);
        fclose(input);
        free(buffer);
        free_analysis_result(result);
        return 0;
    }

    bytes_read = fread(buffer, 1, bytes_to_read, input);
    buffer[bytes_read] = '\0';

    fclose(input);

    if (!analyze_buffer(buffer, bytes_read, result)) {
        free(buffer);
        free_analysis_result(result);
        return 0;
    }

    free(buffer);
    return 1;
}

static int analyze_file_parallel(const char *path,
                                 int requested_threads,
                                 size_t chunk_size_bytes,
                                 AnalysisResult *result,
                                 int *real_threads_used) {
    ChunkRange *ranges = NULL;
    int range_count = 0;
    int failed = 0;
    int i;
    long long total_lines = 0;
    long long valid_lines = 0;
    long long invalid_lines = 0;
    long long file_size = 0;
    omp_lock_t merge_lock;

    if (!get_file_size(path, &file_size)) {
        return 0;
    }

    if (!build_chunk_ranges(path, chunk_size_bytes, &ranges, &range_count)) {
        return 0;
    }

    if (!init_analysis_result(result, estimate_initial_items(file_size))) {
        free(ranges);
        return 0;
    }

    *real_threads_used = 0;
    omp_init_lock(&merge_lock);

    /* One thread creates tasks, other threads process file chunks in parallel. */
    #pragma omp parallel num_threads(requested_threads) shared(failed, total_lines, valid_lines, invalid_lines, result, real_threads_used)
    {
        #pragma omp master
        {
            *real_threads_used = omp_get_num_threads();
        }

        #pragma omp single
        {
            for (i = 0; i < range_count; ++i) {
                ChunkRange current_range = ranges[i];

                #pragma omp task firstprivate(current_range)
                {
                    AnalysisResult local_result;
                    int local_ok = 1;

                    if (!process_chunk(path, current_range, &local_result)) {
                        local_ok = 0;
                    }

                    if (local_ok) {
                        int merged_ok;

                        /* Local tables are merged under a lock to keep the final map consistent. */
                        omp_set_lock(&merge_lock);
                        merged_ok = merge_tables(&result->stats, &local_result.stats);
                        omp_unset_lock(&merge_lock);

                        if (!merged_ok) {
                            local_ok = 0;
                        } else {
                            #pragma omp atomic
                            total_lines += local_result.total_lines;

                            #pragma omp atomic
                            valid_lines += local_result.valid_lines;

                            #pragma omp atomic
                            invalid_lines += local_result.invalid_lines;
                        }

                        free_analysis_result(&local_result);
                    }

                    if (!local_ok) {
                        #pragma omp atomic write
                        failed = 1;
                    }
                }
            }

            #pragma omp taskwait
        }
    }

    omp_destroy_lock(&merge_lock);
    free(ranges);

    if (failed) {
        fprintf(stderr, "Parallel analysis failed.\n");
        free_analysis_result(result);
        return 0;
    }

    result->total_lines = total_lines;
    result->valid_lines = valid_lines;
    result->invalid_lines = invalid_lines;
    return 1;
}

static int has_port(const IpStat *stat, int port) {
    int tracked_count = stat->unique_ports < MAX_TRACKED_PORTS ? stat->unique_ports : MAX_TRACKED_PORTS;
    int i;

    for (i = 0; i < tracked_count; ++i) {
        if (stat->tracked_ports[i] == port) {
            return 1;
        }
    }

    return 0;
}

static int same_port_info(const IpStat *first, const IpStat *second) {
    int first_overflow = first->port_overflow || first->unique_ports > MAX_TRACKED_PORTS;
    int second_overflow = second->port_overflow || second->unique_ports > MAX_TRACKED_PORTS;
    int first_tracked = first->unique_ports < MAX_TRACKED_PORTS ? first->unique_ports : MAX_TRACKED_PORTS;
    int second_tracked = second->unique_ports < MAX_TRACKED_PORTS ? second->unique_ports : MAX_TRACKED_PORTS;
    int i;

    if (first_overflow != second_overflow) {
        return 0;
    }

    if (first_overflow && second_overflow) {
        return 1;
    }

    if (!first_overflow && first->unique_ports != second->unique_ports) {
        return 0;
    }

    if (first_tracked != second_tracked) {
        return 0;
    }

    for (i = 0; i < first_tracked; ++i) {
        if (!has_port(second, first->tracked_ports[i])) {
            return 0;
        }
    }

    return 1;
}

static int results_match(const AnalysisResult *first, const AnalysisResult *second) {
    size_t i;

    if (first->total_lines != second->total_lines ||
        first->valid_lines != second->valid_lines ||
        first->invalid_lines != second->invalid_lines ||
        first->stats.used_count != second->stats.used_count) {
        return 0;
    }

    for (i = 0; i < first->stats.capacity; ++i) {
        if (first->stats.entries[i].used) {
            const IpStat *other = lookup_ip_stat(&second->stats, first->stats.entries[i].ip);

            if (other == NULL) {
                return 0;
            }

            if (first->stats.entries[i].requests != other->requests ||
                first->stats.entries[i].error_requests != other->error_requests ||
                !same_port_info(&first->stats.entries[i], other)) {
                return 0;
            }
        }
    }

    return 1;
}

static ReportRow *collect_report_rows(const StatsTable *table, size_t *count) {
    size_t i;
    size_t index = 0;
    ReportRow *rows = (ReportRow *) calloc(table->used_count, sizeof(ReportRow));

    if (rows == NULL) {
        return NULL;
    }

    for (i = 0; i < table->capacity; ++i) {
        if (table->entries[i].used) {
            snprintf(rows[index].ip, sizeof(rows[index].ip), "%s", table->entries[i].ip);
            rows[index].requests = table->entries[i].requests;
            rows[index].error_requests = table->entries[i].error_requests;
            rows[index].unique_ports = table->entries[i].unique_ports;
            index++;
        }
    }

    *count = index;
    return rows;
}

static int calculate_error_rate(const ReportRow *row) {
    if (row->requests == 0) {
        return 0;
    }

    return (int) ((100.0 * (double) row->error_requests) / (double) row->requests + 0.5);
}

static void mark_anomalies(ReportRow *rows,
                           size_t count,
                           int thread_count,
                           int ddos_threshold,
                           int port_scan_threshold,
                           int error_rate_threshold,
                           int error_min_requests) {
    size_t i;

    /* This pass is independent for each IP, so OpenMP for is enough here. */
    #pragma omp parallel for num_threads(thread_count)
    for (i = 0; i < count; ++i) {
        int error_rate = calculate_error_rate(&rows[i]);

        rows[i].is_ddos = rows[i].requests >= ddos_threshold;
        rows[i].is_port_scan = rows[i].unique_ports >= port_scan_threshold;
        rows[i].is_error_source = rows[i].requests >= error_min_requests && error_rate >= error_rate_threshold;
    }
}

static int compare_rows_by_requests(const void *left, const void *right) {
    const ReportRow *first = (const ReportRow *) left;
    const ReportRow *second = (const ReportRow *) right;

    if (first->requests < second->requests) {
        return 1;
    }
    if (first->requests > second->requests) {
        return -1;
    }

    return strcmp(first->ip, second->ip);
}

static int write_stats_csv(const char *path, const ReportRow *rows, size_t count) {
    size_t i;
    FILE *output = fopen(path, "w");

    if (output == NULL) {
        fprintf(stderr, "Cannot create %s: %s\n", path, strerror(errno));
        return 0;
    }

    fprintf(output, "ip,requests,error_requests,error_rate,unique_ports,ddos,port_scan,error_source\n");
    for (i = 0; i < count; ++i) {
        fprintf(output,
                "%s,%lld,%lld,%d,%d,%s,%s,%s\n",
                rows[i].ip,
                rows[i].requests,
                rows[i].error_requests,
                calculate_error_rate(&rows[i]),
                rows[i].unique_ports,
                rows[i].is_ddos ? "yes" : "no",
                rows[i].is_port_scan ? "yes" : "no",
                rows[i].is_error_source ? "yes" : "no");
    }

    fclose(output);
    return 1;
}

static int write_report_file(const char *path,
                             const char *input_file,
                             const ReportRow *rows,
                             size_t count,
                             double sequential_time,
                             double parallel_time,
                             int requested_threads,
                             int real_threads_used,
                             size_t chunk_size_bytes) {
    int ddos_count = 0;
    int port_scan_count = 0;
    int error_source_count = 0;
    size_t i;
    FILE *output = fopen(path, "w");

    if (output == NULL) {
        fprintf(stderr, "Cannot create %s: %s\n", path, strerror(errno));
        return 0;
    }

    for (i = 0; i < count; ++i) {
        ddos_count += rows[i].is_ddos;
        port_scan_count += rows[i].is_port_scan;
        error_source_count += rows[i].is_error_source;
    }

    fprintf(output, "Parallel analysis of server logs and network anomalies\n");
    fprintf(output, "Input file: %s\n", input_file);
    fprintf(output, "Unique IP addresses: %zu\n", count);
    fprintf(output, "Sequential time: %.6f s\n", sequential_time);
    fprintf(output, "Parallel time: %.6f s\n", parallel_time);
    fprintf(output, "Speedup: %.3f\n", parallel_time > 0.0 ? sequential_time / parallel_time : 0.0);
    fprintf(output, "Requested threads: %d\n", requested_threads);
    fprintf(output, "Real threads used: %d\n", real_threads_used);
    fprintf(output, "Chunk size: %.2f MB\n", (double) chunk_size_bytes / (1024.0 * 1024.0));
    fprintf(output, "\n");
    fprintf(output, "Detected anomalies:\n");
    fprintf(output, "  Possible DDoS sources: %d\n", ddos_count);
    fprintf(output, "  Possible port scanners: %d\n", port_scan_count);
    fprintf(output, "  High error rate sources: %d\n", error_source_count);
    fprintf(output, "\n");
    fprintf(output, "Top 10 IP addresses by number of requests:\n");

    for (i = 0; i < count && i < 10; ++i) {
        fprintf(output,
                "%2zu. %s | requests=%lld | errors=%lld | error_rate=%d%% | unique_ports=%d\n",
                i + 1,
                rows[i].ip,
                rows[i].requests,
                rows[i].error_requests,
                calculate_error_rate(&rows[i]),
                rows[i].unique_ports);
    }

    fprintf(output, "\n");
    fprintf(output, "Suspicious IP addresses:\n");
    for (i = 0; i < count; ++i) {
        if (rows[i].is_ddos || rows[i].is_port_scan || rows[i].is_error_source) {
            fprintf(output, "- %s: ", rows[i].ip);

            if (rows[i].is_ddos) {
                fprintf(output, "[DDoS] ");
            }
            if (rows[i].is_port_scan) {
                fprintf(output, "[PORT_SCAN] ");
            }
            if (rows[i].is_error_source) {
                fprintf(output, "[HIGH_ERROR_RATE] ");
            }

            fprintf(output,
                    "requests=%lld, errors=%lld, error_rate=%d%%, unique_ports=%d\n",
                    rows[i].requests,
                    rows[i].error_requests,
                    calculate_error_rate(&rows[i]),
                    rows[i].unique_ports);
        }
    }

    fclose(output);
    return 1;
}

static void make_timestamp(long long index, char *buffer, size_t buffer_size) {
    int day = 1 + (int) ((index / 86400) % 28);
    int hour = (int) ((index / 3600) % 24);
    int minute = (int) ((index / 60) % 60);
    int second = (int) (index % 60);

    snprintf(buffer, buffer_size, "2026-03-%02dT%02d:%02d:%02d", day, hour, minute, second);
}

static void write_log_line(FILE *output,
                           long long index,
                           const char *source_ip,
                           const char *destination_ip,
                           const char *method,
                           int port,
                           int status,
                           int bytes) {
    char timestamp[32];

    make_timestamp(index, timestamp, sizeof(timestamp));
    fprintf(output, "%s %s %s %s %d %d %d\n",
            timestamp,
            source_ip,
            destination_ip,
            method,
            port,
            status,
            bytes);
}

static void build_pool_ip(int first, int second, int index, char *buffer, size_t buffer_size) {
    int third = 1 + (index / 240) % 240;
    int fourth = 1 + index % 240;

    snprintf(buffer, buffer_size, "%d.%d.%d.%d", first, second, third, fourth);
}

static int generate_random_log(const char *path, long long line_count) {
    static const int normal_ports[] = {80, 443, 22, 21, 25, 53, 8080, 3306};
    static const int normal_statuses[] = {200, 200, 200, 200, 201, 204, 301, 404, 500};
    static const char *methods[] = {"GET", "POST", "PUT", "HEAD"};
    const char *ddos_ip = "203.0.113.10";
    const char *scan_ip = "198.51.100.77";
    const char *error_ip = "192.0.2.55";
    FILE *output = fopen(path, "w");
    long long i;

    if (output == NULL) {
        fprintf(stderr, "Cannot create %s: %s\n", path, strerror(errno));
        return 0;
    }

    srand(42);
    for (i = 0; i < line_count; ++i) {
        char source_ip[MAX_IP_LENGTH];
        char destination_ip[MAX_IP_LENGTH];
        const char *method = methods[rand() % 4];
        int port = normal_ports[rand() % 8];
        int status = normal_statuses[rand() % 9];
        int bytes = 200 + rand() % 4000;

        build_pool_ip(172, 16, rand() % 180, source_ip, sizeof(source_ip));
        build_pool_ip(10, 0, rand() % 40, destination_ip, sizeof(destination_ip));

        /* Several synthetic attack patterns are inserted on purpose
           so the analyzer has something visible to detect. */
        if (i % 10 == 0) {
            write_log_line(output, i, ddos_ip, "10.0.0.10", "GET", (i % 2 == 0) ? 80 : 443, 200, 1200);
        } else if (i % 37 == 0) {
            write_log_line(output, i, scan_ip, "10.0.0.20", "SCAN", 1000 + (int) (i % 5000), 403, 90);
        } else if (i % 29 == 0) {
            int error_statuses[] = {401, 403, 404, 500};
            write_log_line(output, i, error_ip, "10.0.0.30", "POST", 80, error_statuses[i % 4], 150);
        } else {
            write_log_line(output, i, source_ip, destination_ip, method, port, status, bytes);
        }
    }

    fclose(output);
    return 1;
}

static int generate_formula_log(const char *path, long long line_count) {
    static const int normal_ports[] = {80, 443, 22, 53, 8080, 3306};
    static const int status_pattern[] = {200, 200, 201, 204, 301, 404, 500};
    static const char *methods[] = {"GET", "POST", "PUT", "HEAD"};
    const char *ddos_ip = "203.0.113.200";
    const char *scan_ip = "198.51.100.5";
    const char *error_ip = "192.0.2.99";
    FILE *output = fopen(path, "w");
    long long i;

    if (output == NULL) {
        fprintf(stderr, "Cannot create %s: %s\n", path, strerror(errno));
        return 0;
    }

    for (i = 0; i < line_count; ++i) {
        char source_ip[MAX_IP_LENGTH];
        char destination_ip[MAX_IP_LENGTH];
        int method_id = (int) ((i * 3 + 1) % 4);
        int port = normal_ports[(int) ((i * 5 + 2) % 6)];
        int status = status_pattern[(int) ((i * 7 + 3) % 7)];
        int bytes = 256 + (int) ((i * 17) % 4096);

        build_pool_ip(192, 168, (int) ((i * 13 + 5) % 220), source_ip, sizeof(source_ip));
        build_pool_ip(10, 1, (int) ((i * 11 + 9) % 60), destination_ip, sizeof(destination_ip));

        /* This generator is deterministic, so the same input size always gives the same file. */
        if (i >= line_count / 5 && i < line_count / 5 + line_count / 10) {
            write_log_line(output, i, ddos_ip, "10.1.0.10", "GET", (i % 3 == 0) ? 80 : 443, 200, 1400);
        } else if (i % 31 == 0) {
            write_log_line(output, i, scan_ip, "10.1.0.50", "SCAN", 2000 + (int) ((i * 19) % 7000), 403, 70);
        } else if (i % 19 == 0) {
            int error_statuses[] = {401, 403, 404, 500, 502};
            write_log_line(output, i, error_ip, "10.1.0.25", "POST", 80, error_statuses[i % 5], 110);
        } else {
            write_log_line(output, i, source_ip, destination_ip, methods[method_id], port, status, bytes);
        }
    }

    fclose(output);
    return 1;
}

static int run_analysis_command(const char *input_file,
                                const char *report_file,
                                const char *stats_file,
                                int threads,
                                size_t chunk_size_bytes) {
    AnalysisResult sequential_result;
    AnalysisResult parallel_result;
    ReportRow *rows = NULL;
    size_t row_count = 0;
    double sequential_time;
    double parallel_time;
    double start_time;
    int real_threads_used = 0;

    start_time = omp_get_wtime();
    if (!analyze_file_sequential(input_file, chunk_size_bytes, &sequential_result)) {
        return 0;
    }
    sequential_time = omp_get_wtime() - start_time;

    start_time = omp_get_wtime();
    if (!analyze_file_parallel(input_file, threads, chunk_size_bytes, &parallel_result, &real_threads_used)) {
        free_analysis_result(&sequential_result);
        return 0;
    }
    parallel_time = omp_get_wtime() - start_time;

    if (!results_match(&sequential_result, &parallel_result)) {
        fprintf(stderr, "Sequential and parallel results do not match.\n");
        free_analysis_result(&sequential_result);
        free_analysis_result(&parallel_result);
        return 0;
    }

    rows = collect_report_rows(&parallel_result.stats, &row_count);
    if (rows == NULL) {
        fprintf(stderr, "Not enough memory for final report rows.\n");
        free_analysis_result(&sequential_result);
        free_analysis_result(&parallel_result);
        return 0;
    }

    mark_anomalies(rows,
                   row_count,
                   threads,
                   DEFAULT_DDOS_THRESHOLD,
                   DEFAULT_PORT_SCAN_THRESHOLD,
                   DEFAULT_ERROR_RATE_THRESHOLD,
                   DEFAULT_ERROR_MIN_REQUESTS);
    qsort(rows, row_count, sizeof(ReportRow), compare_rows_by_requests);

    if (!write_stats_csv(stats_file, rows, row_count) ||
        !write_report_file(report_file,
                           input_file,
                           rows,
                           row_count,
                           sequential_time,
                           parallel_time,
                           threads,
                           real_threads_used,
                           chunk_size_bytes)) {
        free(rows);
        free_analysis_result(&sequential_result);
        free_analysis_result(&parallel_result);
        return 0;
    }

    printf("Analysis completed successfully.\n");
    printf("Total lines: %lld\n", parallel_result.total_lines);
    printf("Valid lines: %lld\n", parallel_result.valid_lines);
    printf("Invalid lines: %lld\n", parallel_result.invalid_lines);
    printf("Unique IP addresses: %zu\n", row_count);
    printf("Sequential time: %.6f s\n", sequential_time);
    printf("Parallel time: %.6f s\n", parallel_time);
    printf("Speedup: %.3f\n", parallel_time > 0.0 ? sequential_time / parallel_time : 0.0);
    printf("Report file: %s\n", report_file);
    printf("CSV statistics: %s\n", stats_file);

    free(rows);
    free_analysis_result(&sequential_result);
    free_analysis_result(&parallel_result);
    return 1;
}

static int run_benchmark_command(const char *input_file,
                                 const char *output_csv,
                                 int max_threads,
                                 size_t chunk_size_bytes) {
    AnalysisResult sequential_result;
    double sequential_time;
    double start_time;
    int threads;
    FILE *output = NULL;

    start_time = omp_get_wtime();
    if (!analyze_file_sequential(input_file, chunk_size_bytes, &sequential_result)) {
        return 0;
    }
    sequential_time = omp_get_wtime() - start_time;

    output = fopen(output_csv, "w");
    if (output == NULL) {
        fprintf(stderr, "Cannot create %s: %s\n", output_csv, strerror(errno));
        free_analysis_result(&sequential_result);
        return 0;
    }

    fprintf(output, "threads,sequential_time,parallel_time,speedup,match\n");
    for (threads = 1; threads <= max_threads; threads *= 2) {
        AnalysisResult parallel_result;
        double parallel_time;
        int real_threads = 0;
        int match_ok;

        start_time = omp_get_wtime();
        if (!analyze_file_parallel(input_file, threads, chunk_size_bytes, &parallel_result, &real_threads)) {
            fclose(output);
            free_analysis_result(&sequential_result);
            return 0;
        }
        parallel_time = omp_get_wtime() - start_time;
        match_ok = results_match(&sequential_result, &parallel_result);

        fprintf(output,
                "%d,%.6f,%.6f,%.3f,%s\n",
                real_threads,
                sequential_time,
                parallel_time,
                parallel_time > 0.0 ? sequential_time / parallel_time : 0.0,
                match_ok ? "yes" : "no");

        printf("threads=%d parallel_time=%.6f speedup=%.3f match=%s\n",
               real_threads,
               parallel_time,
               parallel_time > 0.0 ? sequential_time / parallel_time : 0.0,
               match_ok ? "yes" : "no");

        free_analysis_result(&parallel_result);
    }

    fclose(output);
    free_analysis_result(&sequential_result);
    printf("Benchmark saved to %s\n", output_csv);
    return 1;
}

int main(int argc, char **argv) {
    long long line_count = 0;
    int threads = omp_get_max_threads();
    int max_threads = 0;
    size_t chunk_size_bytes = (size_t) DEFAULT_CHUNK_MB * 1024U * 1024U;

    if (argc < 2) {
        print_usage(argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "generate-random") == 0) {
        if (argc != 4 || !parse_long_long(argv[3], &line_count) || line_count <= 0) {
            print_usage(argv[0]);
            return 1;
        }

        return generate_random_log(argv[2], line_count) ? 0 : 1;
    }

    if (strcmp(argv[1], "generate-formula") == 0) {
        if (argc != 4 || !parse_long_long(argv[3], &line_count) || line_count <= 0) {
            print_usage(argv[0]);
            return 1;
        }

        return generate_formula_log(argv[2], line_count) ? 0 : 1;
    }

    if (strcmp(argv[1], "analyze") == 0) {
        if (argc < 5 || argc > 7) {
            print_usage(argv[0]);
            return 1;
        }

        if (argc >= 6) {
            if (!parse_int_value(argv[5], &threads) || threads <= 0) {
                fprintf(stderr, "Invalid thread count.\n");
                return 1;
            }
        }

        if (argc == 7) {
            int chunk_mb = 0;
            if (!parse_int_value(argv[6], &chunk_mb) || chunk_mb <= 0) {
                fprintf(stderr, "Invalid chunk size.\n");
                return 1;
            }
            chunk_size_bytes = (size_t) chunk_mb * 1024U * 1024U;
        }

        return run_analysis_command(argv[2], argv[3], argv[4], threads, chunk_size_bytes) ? 0 : 1;
    }

    if (strcmp(argv[1], "benchmark") == 0) {
        if (argc < 5 || argc > 6) {
            print_usage(argv[0]);
            return 1;
        }

        if (!parse_int_value(argv[4], &max_threads) || max_threads <= 0) {
            fprintf(stderr, "Invalid max_threads value.\n");
            return 1;
        }

        if (argc == 6) {
            int chunk_mb = 0;
            if (!parse_int_value(argv[5], &chunk_mb) || chunk_mb <= 0) {
                fprintf(stderr, "Invalid chunk size.\n");
                return 1;
            }
            chunk_size_bytes = (size_t) chunk_mb * 1024U * 1024U;
        }

        return run_benchmark_command(argv[2], argv[3], max_threads, chunk_size_bytes) ? 0 : 1;
    }

    print_usage(argv[0]);
    return 1;
}
