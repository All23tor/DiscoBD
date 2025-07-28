#ifndef BUFFER_MANAGER_HPP
#define BUFFER_MANAGER_HPP

#include "Disk.hpp"
#include <list>
#include <unordered_map>

struct Frame {
  std::string content;
  bool dirty_bit = false;
  int pin_count = 0;

  Frame(Frame&&) = default;
  Frame& operator=(Frame&&) = default;

  Frame(int frame_id);
  template <bool Readonly = true>
  auto data(this std::conditional_t<Readonly, const Frame&, Frame&> self);
};

class BufferManager {
  int hits = 0;
  int total_access = 0;
  std::unordered_map<int, Frame> pool;
  std::list<int> mru;

public:
  ~BufferManager();
  static constexpr int capacity = 8;
  template <bool Readonly = true>
  std::conditional_t<Readonly, const char*, char*>
  load_sector(Address sector_address);
  void pin(Address sector_address);
  void unpin(Address sector_address);
  void print() const;
};

#endif
