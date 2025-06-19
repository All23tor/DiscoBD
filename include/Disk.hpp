#ifndef DISK_HPP
#define DISK_HPP

#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;
namespace ip = boost::interprocess;

// Funciones auxiliares para escribir información aarchivos binarios
template <class T>
auto& pun_cast(T& t) {
  return reinterpret_cast<std::array<char, sizeof(T)>&>(t);
}
template <class T>
const auto& pun_cast(const T& t) {
  return reinterpret_cast<const std::array<char, sizeof(T)>&>(t);
}

// Ruta a la raíz del disco
const static inline auto disk_path = fs::current_path() / "disk";

// Información global acerca del disco
static inline struct DiskInfo {
  int plates;
  int tracks;
  int sectors;
  int bytes;
  int block_size;
} globalDiskInfo = {-1, -1, -1, -1, -1};

// Direccion de un sector guardada con lógica de aritmética modular para
// minimizar el movimiento de la aguja
struct Address {
  int address;

  // Comparación de dos direcciones
  bool operator==(const Address&) const = default;
  static Address from_tree(int plate, int surface, int track, int sector) {
    return {plate +
            globalDiskInfo.plates *
                (sector + globalDiskInfo.sectors *
                              (track + globalDiskInfo.tracks * surface))};
  }

  // Convierte una dirección a la ruta al archivo del sector correspondiente
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

// Crea un disko con la información proporcinada (diskInfo)
// Inicializa en su primer sector esta misma información
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
}

// Lee la información del disco de un secotr ya existente, este es el único
// acceso al disco que se realiza fuera del buffer manager.
inline void read_disk_info() {
  auto disk = fs::current_path() / "disk";
  auto first_path = disk / "p0" / "f0" / "t0" / "s0";
  ip::file_mapping first_file(first_path.c_str(), ip::mode_t::read_only);
  ip::mapped_region first_map(first_file, ip::mode_t::read_only);
  auto data = reinterpret_cast<char*>(first_map.get_address());
  globalDiskInfo = reinterpret_cast<DiskInfo&>(*data);
}

#endif
