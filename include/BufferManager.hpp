#ifndef BUFFER_MANAGER_HPP
#define BUFFER_MANAGER_HPP

#include "Disk.hpp"
#include <list>
#include <map>

struct Frame {
  std::string content;
  bool dirty_bit = false;
  int pin_count = 0;

  Frame(Frame&&) = default;
  Frame& operator=(Frame&&) = default;
  Frame(int frame_id) {
    std::ostringstream oss;
    for (int sector = 0; sector < globalDiskInfo.block_size; sector++) {
      Address sector_address = {frame_id * globalDiskInfo.block_size + sector};
      std::ifstream sector_file = sector_address.to_path();
      oss << sector_file.rdbuf();
    }

    content = std::move(oss).str();
  }

  const char* data() const {
    return content.data();
  }

  char* writeable_data() {
    dirty_bit = true;
    return content.data();
  }
};

class BufferPool {
  const int capacity;
  std::map<int, Frame> frames;
  std::list<int> lru;

public:
  BufferPool(int _capacity) : capacity{_capacity} {}
  ~BufferPool() {
    for (auto& [frame_id, frame] : frames) {
      if (frame.dirty_bit) {
        for (int sector = 0; sector < globalDiskInfo.block_size; sector++) {
          auto sector_data = frame.data() + sector * globalDiskInfo.bytes;
          Address sector_address = {frame_id * globalDiskInfo.block_size +
                                    sector};
          std::ofstream sector_file = sector_address.to_path();
          sector_file.write(sector_data, globalDiskInfo.bytes);
        }
      }
    }
  }

  const char* load_sector(Address sector_address) {
    int block_id = sector_address.address / globalDiskInfo.block_size;
    if (auto it = frames.find(block_id); it != frames.end()) {
      lru.erase(std::find(lru.begin(), lru.end(), block_id));
      lru.push_back(block_id);
      return it->second.data() +
             globalDiskInfo.bytes *
                 (sector_address.address % globalDiskInfo.block_size);
    }

    if (frames.size() < capacity) {
      lru.push_back(block_id);
      auto [it, _] = frames.insert({block_id, Frame(block_id)});
      return it->second.data() +
             globalDiskInfo.bytes *
                 (sector_address.address % globalDiskInfo.block_size);
    }

    int lru_id = lru.front();
    lru.pop_front();
    Frame lru_frame = std::move(frames.at(lru_id));
    frames.erase(lru_id);
    if (lru_frame.dirty_bit) {
      for (int sector = 0; sector < globalDiskInfo.block_size; sector++) {
        auto sector_data = lru_frame.data() + sector * globalDiskInfo.bytes;
        Address sector_address = {lru_id * globalDiskInfo.block_size + sector};
        std::ofstream sector_file = sector_address.to_path();
        sector_file.write(sector_data, globalDiskInfo.bytes);
      }
    }

    lru.push_back(block_id);
    auto [it, _] = frames.insert({block_id, Frame(block_id)});
    return it->second.data() +
           globalDiskInfo.bytes *
               (sector_address.address % globalDiskInfo.block_size);
  }

  char* load_writeable_sector(Address sector_address) {
    int block_id = sector_address.address / globalDiskInfo.block_size;
    if (auto it = frames.find(block_id); it != frames.end()) {
      lru.erase(std::find(lru.begin(), lru.end(), block_id));
      lru.push_back(block_id);
      return it->second.writeable_data() +
             globalDiskInfo.bytes *
                 (sector_address.address % globalDiskInfo.block_size);
    }

    if (frames.size() < capacity) {
      lru.push_back(block_id);
      auto [it, _] = frames.insert({block_id, Frame(block_id)});
      return it->second.writeable_data() +
             globalDiskInfo.bytes *
                 (sector_address.address % globalDiskInfo.block_size);
    }

    int lru_id = lru.front();
    lru.pop_front();
    Frame lru_frame = std::move(frames.at(lru_id));
    frames.erase(lru_id);
    if (lru_frame.dirty_bit) {
      for (int sector = 0; sector < globalDiskInfo.block_size; sector++) {
        auto sector_data = lru_frame.data() + sector * globalDiskInfo.bytes;
        Address sector_address = {lru_id * globalDiskInfo.block_size + sector};
        std::ofstream sector_file = sector_address.to_path();
        sector_file.write(sector_data, globalDiskInfo.bytes);
      }
    }

    lru.push_back(block_id);
    auto [it, _] = frames.insert({block_id, Frame(block_id)});
    return it->second.writeable_data() +
           globalDiskInfo.bytes *
               (sector_address.address % globalDiskInfo.block_size);
  }
};

#endif
