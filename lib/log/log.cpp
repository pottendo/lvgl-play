#include <string>
#include <iostream>
#include <cstdarg>

#include "log.h"

void log_msg(const std::string &s) {
    std::cout << s << '\n';
}

void log_msg(const char *f, ...) {
    va_list val;
    va_start(val, f);
    vprintf(f, val);
    va_end(val);
}
