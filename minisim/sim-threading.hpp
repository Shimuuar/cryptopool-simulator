#pragma once


/// Get sum of current thread's user and system time
double get_thread_time();

/// Get sum of process' user and system time
double get_total_time();

/// Get all clock time
double get_wall_time();
