#include "sim-threading.hpp"

#include <sys/resource.h>
#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include <iostream>
#include <queue>
#include <cassert>

#ifdef __MACH__
#include <mach/mach_init.h>
#include <mach/thread_act.h>
#include <mach/mach_port.h>
double get_thread_time() {
    mach_port_t thread;
    kern_return_t kr;
    mach_msg_type_number_t count;
    thread_basic_info_data_t info;

    thread = mach_thread_self();

    count = THREAD_BASIC_INFO_COUNT;
    kr = thread_info(thread, THREAD_BASIC_INFO, (thread_info_t) &info, &count);
    double ret = 0;
    if (kr == KERN_SUCCESS && (info.flags & TH_FLAGS_IDLE) == 0) {
        ret += info.user_time.seconds;
        ret += info.user_time.microseconds / 1000000.;
        ret += info.system_time.seconds;
        ret += info.system_time.microseconds / 1000000.;
    }
    else {
        abort();
    }
    mach_port_deallocate(mach_task_self(), thread);
    return ret;
}

#else
double get_thread_time() {
    struct rusage usage;
    getrusage (RUSAGE_THREAD, &usage);
    double ret = 0.;
    ret += usage.ru_utime.tv_sec;
    ret += usage.ru_utime.tv_usec / 1000000.;
    ret += usage.ru_stime.tv_sec;
    ret += usage.ru_stime.tv_usec / 1000000.;
    return ret;
}
#endif


double get_total_time() {
    struct rusage usage;
    getrusage(RUSAGE_SELF, &usage);
    double ret = 0.;
    ret += usage.ru_utime.tv_sec;
    ret += usage.ru_utime.tv_usec / 1000000.;
    ret += usage.ru_stime.tv_sec;
    ret += usage.ru_stime.tv_usec / 1000000.;
    return ret;
}

double get_wall_time() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    double ret = 0.;
    ret += tv.tv_sec;
    ret += tv.tv_usec / 1000000.;
    return ret;
}


//================================================================
//

namespace {
    struct Payload {
        std::queue<std::shared_ptr<Workload>> *queue;
        pthread_mutex_t *lock_queue;
        pthread_mutex_t *lock_result;
    };

    // RAII based wrapper for pthread mutex. It's mstly needed to
    // provide excepton safety
    class TakenMutex {
    public:
        explicit TakenMutex(pthread_mutex_t* p):
            mutex(p)
        {
            pthread_mutex_lock(mutex);
        }
        ~TakenMutex() {
            pthread_mutex_unlock(mutex);
        }
    private:
        pthread_mutex_t *mutex;
    private:
        TakenMutex(TakenMutex& ) = delete;
        TakenMutex& operator=( const TakenMutex& ) = delete;
    };
    
}


struct WorkQueue::Impl {
    pthread_mutex_t        lock_queue;
    pthread_mutex_t        lock_result;
    Status                 status;
    std::vector<pthread_t> threads;
    std::vector<Payload>   params;
    std::queue<std::shared_ptr<Workload>> queue;

    Impl(int n_threads);
    ~Impl() = default;
};

WorkQueue::Impl::Impl(int n_threads) :
    status(PREPARING),
    threads(n_threads),
    params(n_threads)
{
    pthread_mutex_init(&lock_queue,        nullptr);
    pthread_mutex_init(&lock_result, nullptr);
}

WorkQueue::WorkQueue(int n_threads) :
    impl(new WorkQueue::Impl(n_threads))
{}

WorkQueue::~WorkQueue() {
}

static void* worker(void* dat) {
    Payload* param = (Payload*)(dat);
    while(true) {
        std::shared_ptr<Workload> worker;
        {
            TakenMutex(param->lock_queue);
            if( param->queue->empty() ) {
                return nullptr;
            }
            worker = param->queue->front();
            param->queue->pop();
        }
        // Do work. On exception terminate program slightly more
        // gracefully than core dump
        try {
            worker->work();
        }
        catch ( const std::exception &e ) {
            std::cerr << "Error:   " << e.what()         << std::endl;
            std::cerr << "Of type: " << typeid(e).name() << std::endl;
            exit(1);
        }
        // Finalization
        {
            TakenMutex(param->lock_result);
            worker->fini();
        }
    }
    return nullptr;
}

void WorkQueue::start() {
    assert(impl->status == WorkQueue::PREPARING);
    impl->status = WorkQueue::RUNNING;
    for(size_t i = 0; i < impl->threads.size(); i++) {
        impl->params[i].queue       = &impl->queue;
        impl->params[i].lock_queue  = &impl->lock_queue;
        impl->params[i].lock_result = &impl->lock_result;
        if( 0 != pthread_create(&impl->threads[i], nullptr, worker, &impl->params[i]) ) {
            printf("Can't create thread %d!\n", 0);
            exit(1);
        }
    }
}

void WorkQueue::join() {
    assert(impl->status == WorkQueue::RUNNING);
    impl->status = WorkQueue::STOPPED;
    for(const auto& tid : impl->threads) {
        pthread_join(tid, nullptr);
    }
}

void WorkQueue::enqueue(std::unique_ptr<Workload> w) {
    impl->queue.push(std::shared_ptr<Workload>(w.release()));
}

void print_clock(std::string const &mesg, double start, double end) {
    printf("%s %.3lf sec\n", mesg.c_str(), double(end - start));
}
