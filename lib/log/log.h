#ifndef __LOG_H__
#define __LOG_H__
#include <string>
#include <cstdarg>

void log_msg(const std::string &s);
void log_msg(const char *f, ...);

#endif