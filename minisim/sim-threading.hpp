#pragma once

#include <pthread.h>
#include <vector>
#include <memory>

/// Get sum of current thread's user and system time
double get_thread_time();

/// Get sum of process' user and system time
double get_total_time();

/// Get all clock time
double get_wall_time();



// Closure for parallel execution.
class Workload {
public:
    // Parallelized part of work.
    virtual void work() = 0;
    // Store results of computation. This part of work is executed
    // serially and protected by mutex.
    virtual void fini() = 0;
    virtual ~Workload() = default;
};


// Simple queue for executing N workloads on M threads.
class WorkQueue {
public:
    explicit WorkQueue(int n_threads);

    // Add task for execution. Queue takes ownership.
    void enqueue(Workload *w);

    // Start execution.
    void start();
    // Wait for all threads to finish.
    void join();
private:
    struct Impl;
    enum Status {
        PREPARING,
        RUNNING,
        STOPPED
    };
    std::unique_ptr<Impl> impl;
};
