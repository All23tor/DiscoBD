#ifndef DISK_HPP
#define DISK_HPP

#include <filesystem>

const extern std::filesystem::path disk_path;
const extern std::filesystem::path blocks_path;

struct DiskInfo {
  int plates;
  int tracks;
  int sectors;
  int bytes;
  int block_size;
};

extern DiskInfo globalDiskInfo;

struct Address {
  int address;

  bool operator==(const Address&) const = default;
  static Address from_tree(int plate, int surface, int track, int sector);
  std::filesystem::path to_path() const;
};

void make_disk(const DiskInfo& diskInfo);
void read_disk_info();
const char* load_sector(Address sector_address);
char* load_writeable_sector(Address sector_address);

#endif
