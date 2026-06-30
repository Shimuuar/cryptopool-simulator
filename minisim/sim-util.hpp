#pragma once

#include <stddef.h>
#include <string>


// MMapped file opened in read-only mode.
class MMappedFile {
public:
    explicit MMappedFile(const char* fname);
    explicit MMappedFile(const std::string& fname) :
        MMappedFile(fname.c_str())
    {}
    ~MMappedFile();

    // Size of file
    size_t      size()   { return m_size; }
    // Underlying buffer
    const unsigned char* buffer() { return m_ptr; }
private:
    int            m_fd;
    unsigned char *m_ptr;
    size_t         m_size;
};
