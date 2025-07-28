#include "Disk.hpp"
#include <fstream>

namespace fs = std::filesystem;

fs::path Address::to_path() const {
  int address = this->address;
  auto plate = address % global.plates;
  address /= global.plates;
  auto sector = address % global.sectors;
  address /= global.sectors;
  auto track = address % global.tracks;
  address /= global.tracks;
  auto surface = address % 2;

  return disk_path / ('p' + std::to_string(plate)) /
         ('f' + std::to_string(surface)) / ('t' + std::to_string(track)) /
         ('s' + std::to_string(sector));
}

void make_disk() {
  fs::create_directory(disk_path);
  for (int plate = 0; plate < global.plates; plate++) {
    fs::path plate_path = disk_path / ("p" + std::to_string(plate));
    fs::create_directory(plate_path);
    for (int surface = 0; surface < 2; surface++) {
      fs::path surface_path = plate_path / ("f" + std::to_string(surface));
      fs::create_directory(surface_path);
      for (int track = 0; track < global.tracks; track++) {
        fs::path track_path = surface_path / ("t" + std::to_string(track));
        fs::create_directory(track_path);
        for (int sector = 0; sector < global.sectors; sector++) {
          fs::path sector_path = track_path / ("s" + std::to_string(sector));
          std::ofstream sector_file = sector_path;
          fs::resize_file(sector_path, global.bytes);
        }
      }
    }
  }
}
