#ifndef BUFFER_MANAGER_HPP
#define BUFFER_MANAGER_HPP

#include "Disk.hpp"
#include <list>
#include <stdexcept>
#include <unordered_map>

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
  int hits = 0;
  int total_access = 0;
  const int capacity;
  std::unordered_map<int, Frame> frames;
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

  void print() const {
    std::cout << "ID\t" << "L/W\t" << "DIRTY\t" << "PINS\t" << "LRU\t\n";
    for (int idx{}; int frame_id : lru) {
      const auto& frame = frames.at(frame_id);
      std::cout << frame_id << '\t' << (frame.dirty_bit ? 'W' : 'L') << '\t'
                << frame.dirty_bit << '\t' << frame.pin_count << '\t' << idx++
                << '\n';
    }
    std::cout << '\n'
              << "Total access " << total_access << "\tHits " << hits << '\n';
    std::cout << "Hit rate " << static_cast<float>(hits) * 100 / total_access
              << "%\n";
  }

  const char* load_sector(Address sector_address) {
    total_access++;
    print();
    std::cout << '\n';
    int block_id = sector_address.address / globalDiskInfo.block_size;
    if (auto it = frames.find(block_id); it != frames.end()) {
      hits++;
      std::cout << "Updating " << block_id << '\n';
      lru.erase(std::find(lru.begin(), lru.end(), block_id));
      lru.push_back(block_id);
      return it->second.data() +
             globalDiskInfo.bytes *
                 (sector_address.address % globalDiskInfo.block_size);
    }

    if (frames.size() < capacity) {
      std::cout << "Adding " << block_id << '\n';
      lru.push_back(block_id);
      auto [it, _] = frames.insert({block_id, Frame(block_id)});
      return it->second.data() +
             globalDiskInfo.bytes *
                 (sector_address.address % globalDiskInfo.block_size);
    }

    auto lru_it = lru.begin();
    while (lru_it != lru.end() && frames.at(*lru_it).pin_count != 0)
      std::cout << "Ignoring " << *lru_it << '\n', lru_it++;

    if (lru_it == lru.end())
      throw std::runtime_error("Everything is pinned!");

    int lru_id = *lru_it;
    std::cout << "Erasing " << lru_id << '\n';
    const Frame& lru_frame = frames.at(lru_id);
    if (lru_frame.dirty_bit) {
      for (int sector = 0; sector < globalDiskInfo.block_size; sector++) {
        auto sector_data = lru_frame.data() + sector * globalDiskInfo.bytes;
        Address sector_address = {lru_id * globalDiskInfo.block_size + sector};
        std::ofstream sector_file = sector_address.to_path();
        sector_file.write(sector_data, globalDiskInfo.bytes);
      }
    }
    lru.erase(lru_it);
    frames.erase(lru_id);

    lru.push_back(block_id);
    std::cout << "Replacing with " << block_id << '\n';
    auto [it, _] = frames.insert({block_id, Frame(block_id)});
    return it->second.data() +
           globalDiskInfo.bytes *
               (sector_address.address % globalDiskInfo.block_size);
  }

  char* load_writeable_sector(Address sector_address) {
    total_access++;
    print();
    int block_id = sector_address.address / globalDiskInfo.block_size;
    if (auto it = frames.find(block_id); it != frames.end()) {
      hits++;
      std::cout << "Updating " << block_id << '\n';
      lru.erase(std::find(lru.begin(), lru.end(), block_id));
      lru.push_back(block_id);
      return it->second.writeable_data() +
             globalDiskInfo.bytes *
                 (sector_address.address % globalDiskInfo.block_size);
    }

    if (frames.size() < capacity) {
      std::cout << "Adding " << block_id << '\n';
      lru.push_back(block_id);
      auto [it, _] = frames.insert({block_id, Frame(block_id)});
      return it->second.writeable_data() +
             globalDiskInfo.bytes *
                 (sector_address.address % globalDiskInfo.block_size);
    }

    auto lru_it = lru.begin();
    while (lru_it != lru.end() && frames.at(*lru_it).pin_count != 0)
      std::cout << "Ignoring " << *lru_it << '\n', lru_it++;

    if (lru_it == lru.end())
      throw std::runtime_error("Everything is pinned!");

    int lru_id = *lru_it;
    std::cout << "Erasing " << lru_id << '\n';
    const Frame& lru_frame = frames.at(lru_id);
    if (lru_frame.dirty_bit) {
      for (int sector = 0; sector < globalDiskInfo.block_size; sector++) {
        auto sector_data = lru_frame.data() + sector * globalDiskInfo.bytes;
        Address sector_address = {lru_id * globalDiskInfo.block_size + sector};
        std::ofstream sector_file = sector_address.to_path();
        sector_file.write(sector_data, globalDiskInfo.bytes);
      }
    }
    lru.erase(lru_it);
    frames.erase(lru_id);

    lru.push_back(block_id);
    std::cout << "Replacing with " << block_id << '\n';
    auto [it, _] = frames.insert({block_id, Frame(block_id)});
    return it->second.writeable_data() +
           globalDiskInfo.bytes *
               (sector_address.address % globalDiskInfo.block_size);
  }

  void pin(Address sector_address) {
    int block_id = sector_address.address / globalDiskInfo.block_size;
    std::cout << "Pinning " << block_id << '\n';
    if (auto it = frames.find(block_id); it != frames.end()) {
      it->second.pin_count++;
    }
    print();
  }

  void unpin(Address sector_address) {
    int block_id = sector_address.address / globalDiskInfo.block_size;
    std::cout << "Unpinning " << block_id << '\n';
    if (auto it = frames.find(block_id); it != frames.end()) {
      if (it->second.pin_count > 0)
        it->second.pin_count--;
    }
    print();
  }
};

#endif
