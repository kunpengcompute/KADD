#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

void print_array(const int *arr, int n)
{
    if (!arr || n <= 0) {
        printf("[]");
        return;
    }

    printf("[");
    for (int i = 0; i < n; i++) {
        printf("%d", arr[i]);
        if (i < n - 1) printf(", ");
    }
    printf("]");
}

#ifdef _WIN32
void* timer_start(void)
{
    struct {
        LARGE_INTEGER freq;
        LARGE_INTEGER start;
    } *timer = malloc(sizeof(*timer));

    QueryPerformanceFrequency(&timer->freq);
    QueryPerformanceCounter(&timer->start);

    return timer;
}

double timer_end(void *timer)
{
    if (!timer) return -1.0;

    struct {
        LARGE_INTEGER freq;
        LARGE_INTEGER start;
    } *t = (void*)timer;

    LARGE_INTEGER end;
    QueryPerformanceCounter(&end);

    double elapsed = (double)(end.QuadPart - t->start.QuadPart) * 1000.0 / t->freq.QuadPart;
    free(timer);

    return elapsed;
}
#else
void* timer_start(void)
{
    struct timeval* tv = (struct timeval*)malloc(sizeof(struct timeval));
    gettimeofday(tv, NULL);
    return tv;
}

double timer_end(void *timer)
{
    if (!timer) return -1.0;

    struct timeval* start = (struct timeval*)timer;
    struct timeval end;
    gettimeofday(&end, NULL);

    double elapsed = (end.tv_sec - start->tv_sec) * 1000.0 +
                     (end.tv_usec - start->tv_usec) / 1000.0;

    free(timer);
    return elapsed;
}
#endif
