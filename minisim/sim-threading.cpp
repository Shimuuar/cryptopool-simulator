#include "sim-threading.hpp"

#include <sys/resource.h>
#include <sys/time.h>


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
