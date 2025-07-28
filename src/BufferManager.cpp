#include "BufferManager.hpp"
#include <fstream>
#include <iostream>

Frame::Frame(int frame_id) {
  std::ostringstream oss;
  for (int sector = 0; sector < global.block_size; sector++) {
    Address sector_address = {frame_id * global.block_size + sector};
    std::ifstream sector_file = sector_address.to_path();
    oss << sector_file.rdbuf();
  }

  content = std::move(oss).str();
}

template <bool Readonly>
auto Frame::data(this std::conditional_t<Readonly, const Frame&, Frame&> self) {
  if constexpr (!Readonly)
    self.dirty_bit = true;
  return self.content.data();
}

template auto Frame::data<true>(this const Frame& self);
template auto Frame::data<false>(this Frame& self);

BufferManager::~BufferManager() {
  for (auto& [frame_id, frame] : pool) {
    if (frame.dirty_bit) {
      for (int sector = 0; sector < global.block_size; sector++) {
        auto sector_data = frame.data() + sector * global.bytes;
        Address sector_address = {frame_id * global.block_size + sector};
        std::ofstream sector_file = sector_address.to_path();
        sector_file.write(sector_data, global.bytes);
      }
    }
  }
}

template <bool Readonly>
std::conditional_t<Readonly, const char*, char*>
BufferManager::load_sector(Address sector_address) {
  total_access++;
  int block_id = sector_address.address / global.block_size;
  if (auto it = pool.find(block_id); it != pool.end()) {
    hits++;
    std::cout << "Updating " << block_id << '\n';
    mru.erase(std::find(mru.begin(), mru.end(), block_id));
    mru.push_front(block_id);
    auto res = it->second.data<Readonly>() +
               global.bytes * (sector_address.address % global.block_size);
    print();
    return res;
  }

  if (pool.size() < capacity) {
    std::cout << "Adding " << block_id << '\n';
    mru.push_front(block_id);
    auto [it, _] = pool.insert({block_id, Frame(block_id)});
    auto res = it->second.data<Readonly>() +
               global.bytes * (sector_address.address % global.block_size);
    print();
    return res;
  }

  auto mru_it = mru.begin();
  while (mru_it != mru.end() && pool.at(*mru_it).pin_count != 0)
    std::cout << "Ignoring " << *mru_it << '\n', mru_it++;

  if (mru_it == mru.end())
    throw std::runtime_error("Everything is pinned!");

  int mru_id = *mru_it;
  std::cout << "Erasing " << mru_id << '\n';
  const Frame& mru_frame = pool.at(mru_id);
  if (mru_frame.dirty_bit) {
    for (int sector = 0; sector < global.block_size; sector++) {
      auto sector_data = mru_frame.data() + sector * global.bytes;
      Address sector_address = {mru_id * global.block_size + sector};
      std::ofstream sector_file = sector_address.to_path();
      sector_file.write(sector_data, global.bytes);
    }
  }
  mru.erase(mru_it);
  pool.erase(mru_id);

  mru.push_front(block_id);
  std::cout << "Replacing with " << block_id << '\n';
  auto [it, _] = pool.insert({block_id, Frame(block_id)});
  auto res = it->second.data<Readonly>() +
             global.bytes * (sector_address.address % global.block_size);
  print();
  return res;
}
template const char* BufferManager::load_sector<true>(Address sector_address);
template char* BufferManager::load_sector<false>(Address sector_address);

void BufferManager::print() const {
  std::cout << "ID\t" << "L/W\t" << "DIRTY\t" << "PINS\t" << "MRU\t\n";
  for (int idx{}; int frame_id : mru) {
    const auto& frame = pool.at(frame_id);
    std::cout << frame_id << '\t' << (frame.dirty_bit ? 'W' : 'L') << '\t'
              << frame.dirty_bit << '\t' << frame.pin_count << '\t' << idx++
              << '\n';
  }
  std::cout << '\n'
            << "Total access " << total_access << "\tHits " << hits << '\n';
  std::cout << "Hit rate " << static_cast<float>(hits) * 100 / total_access
            << "%\n";
}

void BufferManager::pin(Address sector_address) {
  int block_id = sector_address.address / global.block_size;
  std::cout << "Pinning " << block_id << '\n';
  if (auto it = pool.find(block_id); it != pool.end()) {
    it->second.pin_count++;
  }
  print();
}

void BufferManager::unpin(Address sector_address) {
  int block_id = sector_address.address / global.block_size;
  std::cout << "Unpinning " << block_id << '\n';
  if (auto it = pool.find(block_id); it != pool.end()) {
    if (it->second.pin_count > 0)
      it->second.pin_count--;
  }
  print();
}
