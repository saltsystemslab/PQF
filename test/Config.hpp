#ifndef CONFIG_HPP
#define CONFIG_HPP

#include <vector>
#include <string>
#include <set>

std::vector<std::pair<std::string, std::vector<double>>> readConfig(const char* filename, std::set<std::string> keywords);

#endif