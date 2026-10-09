// Speed-up benchmark for the ThreadPool in src/ (the pool itself is not modified).
//
//   g++ -std=c++11 -O2 -pthread -Isrc docs/figures/bench.cc src/thread-pool.cc src/Semaphore.cc -o bench
//   ./bench > docs/figures/bench.csv
//
// Two workloads, each scheduled as independent thunks followed by one wait():
//   cpu   - 240 tasks, each a fixed floating-point loop (compute bound)
//   sleep - 96 tasks, each sleeping 5 ms (latency bound, like blocking I/O)
// Every (workload, workers) pair is run 5 times; the CSV keeps every run.
#include <sys/resource.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

#include "thread-pool.h"

static double cpu_seconds() {
    rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_utime.tv_sec + ru.ru_stime.tv_sec + 1e-6 * (ru.ru_utime.tv_usec + ru.ru_stime.tv_usec);
}

static void cpu_task() {
    volatile double acc = 0;
    for (int i = 1; i < 4000000; i++) acc = acc + std::sqrt((double)i);
}

static void sleep_task() { std::this_thread::sleep_for(std::chrono::milliseconds(5)); }

int main() {
    const int workers[] = {1, 2, 3, 4, 6, 8, 10, 12, 16, 24};
    std::printf("workload,workers,rep,wall_s,cpu_s\n");
    for (const char* w : {"cpu", "sleep"}) {
        bool cpu = w[0] == 'c';
        int tasks = cpu ? 240 : 96;
        for (int n : workers)
            for (int rep = 0; rep < 5; rep++) {
                double c0 = cpu_seconds();
                auto t0 = std::chrono::steady_clock::now();
                {
                    ThreadPool pool(n);
                    for (int i = 0; i < tasks; i++) pool.schedule(cpu ? cpu_task : sleep_task);
                    pool.wait();
                }  // destructor joins the threads
                double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                std::printf("%s,%d,%d,%.4f,%.4f\n", w, n, rep, wall, cpu_seconds() - c0);
                std::fflush(stdout);
            }
    }
}
