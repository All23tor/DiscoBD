#include "Table.hpp"
#include <sstream>

int main() {
  if (!fs::exists(disk_path)) {
    DiskInfo diskInfo;
    std::cout << "El disco aún no existe, se procederá a su creación\n\n";
    std::cout << "Cantidad de platos: ";
    std::cin >> diskInfo.plates;
    if (diskInfo.plates < 1) {
      std::cout << "Debe haber por lo menos un plato\n";
      return 1;
    }
    std::cout << "Cantidad de pistas por superficie: ";
    std::cin >> diskInfo.tracks;
    if (diskInfo.tracks < 1) {
      std::cout << "Debe haber por lo menos una pista por superficie\n";
      return 1;
    }
    std::cout << "Cantidad de sectores por pista: ";
    std::cin >> diskInfo.sectors;
    if (diskInfo.sectors < 1) {
      std::cout << "Debe haber por lo menos un sector por pista\n";
      return 1;
    }
    std::cout << "Tamaño de cada sector en bytes: ";
    std::cin >> diskInfo.bytes;
    if (diskInfo.bytes < 16) {
      std::cout << "Debe haber por lo 16 bytes en cada sector\n";
      return 1;
    }
    std::cout << "Número de sectores por bloque: ";
    std::cin >> diskInfo.block_size;
    if (diskInfo.block_size < 1) {
      std::cout << "Debe haber por lo 1 sector en cada bloque\n";
      return 1;
    }
    make_disk(diskInfo);
  } else {
    read_disk_info();
  }

  std::cout << '\n';
  std::cout << "Información del disco:\n";
  std::cout << "Número de platos: " << globalDiskInfo.plates << '\n';
  std::cout << "Número de pistas por plato: " << globalDiskInfo.tracks << '\n';
  std::cout << "Número de sectores por pista: " << globalDiskInfo.sectors
            << '\n';
  std::cout << "Número de bytes por sector: " << globalDiskInfo.bytes << '\n';
  std::cout << "Número de sectores por bloque: " << globalDiskInfo.block_size
            << '\n'
            << '\n';

  std::cout << "  > ";
  std::string line;
  while (std::getline(std::cin, line)) {
    std::stringstream ss{std::move(line)};
    std::string word;
    ss >> word;
    if (word == "LOAD") {
      std::string name;
      ss >> name;
      if (load_csv(name))
        std::cout << "\tSe cargó la tabla " << name << " exitosamente\n";
      else
        std::cout << "\tLa tabla " << name
                  << " no se pudo cargar o ya existe\n";
    } else if (word == "SELECT") {
      std::string fields;
      ss >> fields;
      if (fields == "*") {
        std::string FROM;
        ss >> FROM;
        if (FROM == "FROM") {
          std::string table_name;
          ss >> table_name;

          std::string WHERE;
          ss >> WHERE;
          if (WHERE == "WHERE") {
            std::string clause;
            std::getline(ss, clause, '\n');
            select_all_where(table_name, clause);
          } else {
            select_all(table_name);
          }
        }
      }
    } else if (word == "DELETE") {
      std::string FROM;
      ss >> FROM;
      if (FROM == "FROM") {
        std::string table_name;
        ss >> table_name;

        std::string WHERE;
        ss >> WHERE;
        if (WHERE == "WHERE") {
          std::string clause;
          std::getline(ss, clause, '\n');
          delete_where(table_name, clause);
        }
      }
    } else if (word == "INFO") {
      disk_info();
    }
    std::cout << "  > ";
  }
  std::cout << std::endl;
}
