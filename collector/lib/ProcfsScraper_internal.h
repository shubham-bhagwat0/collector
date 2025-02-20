#ifndef COLLECTOR_PROCFSSCRAPER_INTERNAL_H
#define COLLECTOR_PROCFSSCRAPER_INTERNAL_H

#include <string_view>

namespace collector {

// ExtractContainerID tries to extract a container ID from a cgroup line.
std::string_view ExtractContainerID(std::string_view cgroup_line);

}  // namespace collector

#endif
