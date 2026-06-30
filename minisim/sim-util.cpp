#include "sim-util.hpp"

#include <sys/fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdexcept>

#ifndef MAP_NOCACHE
#define MAP_NOCACHE 0
#endif

MMappedFile::MMappedFile(const char* name) {
    m_fd = open(name, O_RDONLY);
    if( m_fd < 0 ) {
        throw std::runtime_error("Cannot open file for reading");
    }
    lseek(m_fd, 0, SEEK_END);
    m_size = lseek(m_fd, 0, SEEK_CUR);
    m_ptr  = (unsigned char*)::mmap(
        nullptr, m_size, PROT_READ, MAP_NOCACHE|MAP_FILE|MAP_SHARED, m_fd, 0);
    if( m_ptr == MAP_FAILED) {
        throw std::runtime_error("mmap failed");
    }
}

MMappedFile::~MMappedFile() {
    ::munmap(m_ptr, m_size);
    close(m_fd);
}

