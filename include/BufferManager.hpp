#ifndef BUFFER_MANAGER_HPP
#define BUFFER_MANAGER_HPP

#include "Disk.hpp"
#include <list>
#include <stdexcept>
#include <unordered_map>

// Clase que representa un frame en memoria,
// guarda el contenido de una página como una copia
// que le pertenece
struct Frame {
  std::string content;
  bool dirty_bit = false;
  int pin_count = 0;

  Frame(Frame&&) = default;
  Frame& operator=(Frame&&) = default;

  // Construye un Frame en función de su id, copiando el contenido de la página
  // Es decir,el constenido de los sectores que corresponden al bloque
  // (asumiendo un bloque por página)
  Frame(int frame_id) {
    std::ostringstream oss;
    for (int sector = 0; sector < globalDiskInfo.block_size; sector++) {
      Address sector_address = {frame_id * globalDiskInfo.block_size + sector};
      std::ifstream sector_file = sector_address.to_path();
      oss << sector_file.rdbuf();
    }

    content = std::move(oss).str();
  }

  // Acceso de lectura, retorna un puntero a memoria
  // del frame
  const char* data() const {
    return content.data();
  }

  // Acceso de escritura (setea el dirty_bit), retorna un puntero a memoria
  // del frame
  char* writeable_data() {
    dirty_bit = true;
    return content.data();
  }
};

// BufferManager, permite el acceso a los secotres a través
// de sus Address, internamente no trabaja con sectores, sino con páginas,
// pero la interfaz al usuario es a traves de direcciones de sectores
// Adicionalmente guarda el hit rate
class BufferManager {
  int hits = 0;
  int total_access = 0;
  const int capacity;
  std::unordered_map<int, Frame> pool;
  std::list<int> lru;

public:
  // Inicializamos con la capacidad (que se mantendrá constante)
  BufferManager(int _capacity) : capacity{_capacity} {}
  // Al momento de dejas de utilizar memoria, escribimos todas los frames
  // con el dirty bit seteado.
  ~BufferManager() {
    for (auto& [frame_id, frame] : pool) {
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

  // Simple función de impresión de la tabla del buffer pool
  void print() const {
    std::cout << "ID\t" << "L/W\t" << "DIRTY\t" << "PINS\t" << "LRU\t\n";
    for (int idx{}; int frame_id : lru) {
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

  // Recibe una dirección de sector, se encarga de hallar el bloque
  // correspondiente, y lo carga a una página, de ahí verifica bajo la política
  // de remplazo LRU cómo se ubicará en el Buffer Pool (esta versión es de solo
  // lectura)
  const char* load_sector(Address sector_address) {
    total_access++;
    int block_id = sector_address.address / globalDiskInfo.block_size;
    if (auto it = pool.find(block_id); it != pool.end()) {
      hits++;
      std::cout << "Updating " << block_id << '\n';
      lru.erase(std::find(lru.begin(), lru.end(), block_id));
      lru.push_back(block_id);
      print();
      return it->second.data() +
             globalDiskInfo.bytes *
                 (sector_address.address % globalDiskInfo.block_size);
    }

    if (pool.size() < capacity) {
      std::cout << "Adding " << block_id << '\n';
      lru.push_back(block_id);
      auto [it, _] = pool.insert({block_id, Frame(block_id)});
      print();
      return it->second.data() +
             globalDiskInfo.bytes *
                 (sector_address.address % globalDiskInfo.block_size);
    }

    auto lru_it = lru.begin();
    while (lru_it != lru.end() && pool.at(*lru_it).pin_count != 0)
      std::cout << "Ignoring " << *lru_it << '\n', lru_it++;

    if (lru_it == lru.end())
      throw std::runtime_error("Everything is pinned!");

    int lru_id = *lru_it;
    std::cout << "Erasing " << lru_id << '\n';
    const Frame& lru_frame = pool.at(lru_id);
    if (lru_frame.dirty_bit) {
      for (int sector = 0; sector < globalDiskInfo.block_size; sector++) {
        auto sector_data = lru_frame.data() + sector * globalDiskInfo.bytes;
        Address sector_address = {lru_id * globalDiskInfo.block_size + sector};
        std::ofstream sector_file = sector_address.to_path();
        sector_file.write(sector_data, globalDiskInfo.bytes);
      }
    }
    lru.erase(lru_it);
    pool.erase(lru_id);

    lru.push_back(block_id);
    std::cout << "Replacing with " << block_id << '\n';
    auto [it, _] = pool.insert({block_id, Frame(block_id)});
    print();
    return it->second.data() +
           globalDiskInfo.bytes *
               (sector_address.address % globalDiskInfo.block_size);
  }

  // Recibe una dirección de sector, se encarga de hallar el bloque
  // correspondiente, y lo carga a una página, de ahí verifica bajo la política
  // de remplazo LRU cómo se ubicará en el Buffer Pool (esta versión es de
  // lectura y escritura)
  char* load_writeable_sector(Address sector_address) {
    total_access++;
    int block_id = sector_address.address / globalDiskInfo.block_size;
    if (auto it = pool.find(block_id); it != pool.end()) {
      hits++;
      std::cout << "Updating " << block_id << '\n';
      lru.erase(std::find(lru.begin(), lru.end(), block_id));
      lru.push_back(block_id);
      print();
      return it->second.writeable_data() +
             globalDiskInfo.bytes *
                 (sector_address.address % globalDiskInfo.block_size);
    }

    if (pool.size() < capacity) {
      std::cout << "Adding " << block_id << '\n';
      lru.push_back(block_id);
      auto [it, _] = pool.insert({block_id, Frame(block_id)});
      print();
      return it->second.writeable_data() +
             globalDiskInfo.bytes *
                 (sector_address.address % globalDiskInfo.block_size);
    }

    auto lru_it = lru.begin();
    while (lru_it != lru.end() && pool.at(*lru_it).pin_count != 0)
      std::cout << "Ignoring " << *lru_it << '\n', lru_it++;

    if (lru_it == lru.end())
      throw std::runtime_error("Everything is pinned!");

    int lru_id = *lru_it;
    std::cout << "Erasing " << lru_id << '\n';
    const Frame& lru_frame = pool.at(lru_id);
    if (lru_frame.dirty_bit) {
      for (int sector = 0; sector < globalDiskInfo.block_size; sector++) {
        auto sector_data = lru_frame.data() + sector * globalDiskInfo.bytes;
        Address sector_address = {lru_id * globalDiskInfo.block_size + sector};
        std::ofstream sector_file = sector_address.to_path();
        sector_file.write(sector_data, globalDiskInfo.bytes);
      }
    }
    lru.erase(lru_it);
    pool.erase(lru_id);

    lru.push_back(block_id);
    std::cout << "Replacing with " << block_id << '\n';
    auto [it, _] = pool.insert({block_id, Frame(block_id)});
    print();
    return it->second.writeable_data() +
           globalDiskInfo.bytes *
               (sector_address.address % globalDiskInfo.block_size);
  }

  // Recibe una dirección de sector, se encarga de hallar el bloque
  // correspondiente, de estar en una página que corresponde a un frame,
  // incrementa su pin_count.
  void pin(Address sector_address) {
    int block_id = sector_address.address / globalDiskInfo.block_size;
    std::cout << "Pinning " << block_id << '\n';
    if (auto it = pool.find(block_id); it != pool.end()) {
      it->second.pin_count++;
    }
    print();
  }

  // Recibe una dirección de sector, se encarga de hallar el bloque
  // correspondiente, de estar en una página que corresponde a un frame,
  // decrementa su pin_count (no a menos de 0).
  void unpin(Address sector_address) {
    int block_id = sector_address.address / globalDiskInfo.block_size;
    std::cout << "Unpinning " << block_id << '\n';
    if (auto it = pool.find(block_id); it != pool.end()) {
      if (it->second.pin_count > 0)
        it->second.pin_count--;
    }
    print();
  }
};

#endif
