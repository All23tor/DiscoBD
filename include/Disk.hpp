#ifndef DISK_HPP
#define DISK_HPP

#include <filesystem>
namespace fs = std::filesystem;
const static inline auto disk_path = fs::current_path() / "disk";

struct DiskInfo {
  int plates;
  int tracks;
  int sectors;
  int bytes;
  int block_size;
};

static constexpr DiskInfo global = {4, 16, 64, 512, 8};

struct Address {
  int address;
  bool operator==(const Address&) const = default;
  fs::path to_path() const;
};
static constexpr Address NullAddress = {-1};

void make_disk();

#endif
