#ifndef DISK_HPP
#define DISK_HPP

#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <tuple>

namespace fs = std::filesystem;
namespace ip = boost::interprocess;

template <class T>
auto& pun_cast(T& t) {
  return reinterpret_cast<std::array<char, sizeof(T)>&>(t);
}
template <class T>
const auto& pun_cast(const T& t) {
  return reinterpret_cast<const std::array<char, sizeof(T)>&>(t);
}

const static inline auto disk_path = fs::current_path() / "disk";
const static inline auto blocks_path = fs::current_path() / "blocks";

static inline struct DiskInfo {
  int plates;
  int tracks;
  int sectors;
  int bytes;
  int block_size;
} globalDiskInfo = {-1, -1, -1, -1, -1};

struct Address {
  int address;

  bool operator==(const Address&) const = default;
  static Address from_tree(int plate, int surface, int track, int sector) {
    return {plate +
            globalDiskInfo.plates *
                (sector + globalDiskInfo.sectors *
                              (track + globalDiskInfo.tracks * surface))};
  }

  fs::path to_path() const {
    int address = this->address;
    auto plate = address % globalDiskInfo.plates;
    address /= globalDiskInfo.plates;
    auto sector = address % globalDiskInfo.sectors;
    address /= globalDiskInfo.sectors;
    auto track = address % globalDiskInfo.tracks;
    address /= globalDiskInfo.tracks;
    auto surface = address % 2;

    return disk_path / ('p' + std::to_string(plate)) /
           ('f' + std::to_string(surface)) / ('t' + std::to_string(track)) /
           ('s' + std::to_string(sector));
  }
};

inline void make_disk(const DiskInfo& diskInfo) {
  fs::create_directory(disk_path);
  std::cout << disk_path << '\n';
  for (int plate = 0; plate < diskInfo.plates; plate++) {
    fs::path plate_path = disk_path / ("p" + std::to_string(plate));
    fs::create_directory(plate_path);
    std::cout << "╚═ " << plate_path << '\n';
    for (int surface = 0; surface < 2; surface++) {
      fs::path surface_path = plate_path / ("f" + std::to_string(surface));
      fs::create_directory(surface_path);
      std::cout << "   ╚═ " << surface_path << '\n';
      for (int track = 0; track < diskInfo.tracks; track++) {
        fs::path track_path = surface_path / ("t" + std::to_string(track));
        fs::create_directory(track_path);
        std::cout << "      ╚═ " << track_path << '\n';
        for (int sector = 0; sector < diskInfo.sectors; sector++) {
          fs::path sector_path = track_path / ("s" + std::to_string(sector));
          std::ofstream sector_file = sector_path;
          fs::resize_file(sector_path, diskInfo.bytes);
          std::cout << "         ╚═ " << sector_path << '\n';
        }
      }
    }
  }
  auto first_path = disk_path / "p0" / "f0" / "t0" / "s0";
  ip::file_mapping first_file(first_path.c_str(), ip::mode_t::read_write);
  ip::mapped_region first_map(first_file, ip::mode_t::read_write);
  auto data = reinterpret_cast<char*>(first_map.get_address());
  globalDiskInfo = diskInfo;
  reinterpret_cast<DiskInfo&>(*data) = globalDiskInfo;

  fs::create_directory(blocks_path);
}

inline void read_disk_info() {
  auto disk = fs::current_path() / "disk";
  auto first_path = disk / "p0" / "f0" / "t0" / "s0";
  ip::file_mapping first_file(first_path.c_str(), ip::mode_t::read_only);
  ip::mapped_region first_map(first_file, ip::mode_t::read_only);
  auto data = reinterpret_cast<char*>(first_map.get_address());
  globalDiskInfo = reinterpret_cast<DiskInfo&>(*data);
}

class CowBlock {
  mutable int written = false;
  int block_idx;
  ip::mapped_region block_map;

  static inline std::set<CowBlock, std::less<>> loaded_blocks;

  CowBlock(const int block_idx) : block_idx(block_idx) {
    auto block_path = blocks_path / ("b" + std::to_string(block_idx));
    std::ofstream block_stream = block_path;

    for (int sector = 0; sector < globalDiskInfo.block_size; sector++) {
      Address sector_address = {block_idx * globalDiskInfo.block_size + sector};
      auto sector_path = sector_address.to_path();
      std::ifstream sector_file = sector_path;
      block_stream << sector_file.rdbuf();
    }

    block_stream.close();
    ip::file_mapping block_file(block_path.c_str(), ip::mode_t::read_write);
    block_map = ip::mapped_region(block_file, ip::mode_t::read_write);
  }

public:
  CowBlock(CowBlock&&) = default;
  static const char* load_sector(Address sector_address) {
    int block = sector_address.address / globalDiskInfo.block_size;
    auto it = loaded_blocks.find(block);
    if (it == loaded_blocks.end())
      std::tie(it, std::ignore) = loaded_blocks.insert(CowBlock(block));
    return it->data() + globalDiskInfo.bytes * (sector_address.address %
                                                globalDiskInfo.block_size);
  }

  static char* load_writeable_sector(Address sector_address) {
    int block = sector_address.address / globalDiskInfo.block_size;
    auto it = loaded_blocks.find(block);
    if (it == loaded_blocks.end())
      std::tie(it, std::ignore) = loaded_blocks.emplace(CowBlock(block));
    return it->writeable_data() +
           globalDiskInfo.bytes *
               (sector_address.address % globalDiskInfo.block_size);
  }

  static void unload_sectors() {
    loaded_blocks.clear();
  }

  const char* data() const {
    return reinterpret_cast<const char*>(block_map.get_address());
  }

  char* writeable_data() const {
    written = true;
    return reinterpret_cast<char*>(block_map.get_address());
  }

  ~CowBlock() {
    if (!written)
      return;

    for (int sector = 0; sector < globalDiskInfo.block_size; sector++) {
      auto sector_data = data() + sector * globalDiskInfo.bytes;
      Address sector_address = {block_idx * globalDiskInfo.block_size + sector};
      auto sector_path = sector_address.to_path();
      std::ofstream sector_file = sector_path;
      sector_file.write(sector_data, globalDiskInfo.bytes);
    }
  }

  friend bool operator<(const CowBlock& lhs, const CowBlock& rhs) {
    return lhs.block_idx < rhs.block_idx;
  }
  friend bool operator<(const CowBlock& block, int idx) {
    return block.block_idx < idx;
  }
  friend bool operator<(int idx, const CowBlock& block) {
    return idx < block.block_idx;
  }
};

#endif
