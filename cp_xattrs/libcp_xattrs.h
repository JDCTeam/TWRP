#ifndef LIBCPXATTRS_H
#define LIBCPXATTRS_H

#include <string>

bool cp_xattrs_single_file(const std::string& from, const std::string& to);
bool cp_xattrs_recursive(const std::string& from, const std::string& to, unsigned char type);

#endif
