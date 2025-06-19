#include "Disk.hpp"
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
  int buffer_pool_capacity;
  std::cout << "Capacidad del buffer pool: ";
  std::cin >> buffer_pool_capacity;
  BufferManager buffer_pool(buffer_pool_capacity);

  std::cout << "Información del disco:\n";
  std::cout << "Número de platos: " << globalDiskInfo.plates << '\n';
  std::cout << "Número de pistas por plato: " << globalDiskInfo.tracks << '\n';
  std::cout << "Número de sectores por pista: " << globalDiskInfo.sectors
            << '\n';
  std::cout << "Número de bytes por sector: " << globalDiskInfo.bytes << '\n';
  std::cout << "Número de sectores por bloque: " << globalDiskInfo.block_size
            << '\n'
            << '\n';

  std::string line;
  while (std::cout << "  > ", std::getline(std::cin, line)) {
    std::stringstream ss{std::move(line)};
    std::string word;
    ss >> word;
    if (word == "LOAD") {
      std::string name;
      ss >> name;
      load_csv(name, buffer_pool);
      std::cout << "\tSe cargó la tabla " << name << " exitosamente\n";
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
            select_all_where(table_name, clause, buffer_pool);
          } else {
            select_all(table_name, buffer_pool);
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
          delete_where(table_name, clause, buffer_pool);
        }
      }
    } else if (word == "BUFFER") {
      buffer_pool.print();
    } else if (word == "REQUEST") {
      int page_idx;
      ss >> page_idx;
      char rw;
      ss >> rw;
      if (rw == 'W')
        buffer_pool.load_writeable_sector(
            {page_idx * globalDiskInfo.block_size});
      else if (rw == 'L')
        buffer_pool.load_sector({page_idx * globalDiskInfo.block_size});
    } else if (word == "PIN") {
      int page_idx;
      ss >> page_idx;
      buffer_pool.pin({page_idx * globalDiskInfo.block_size});
    } else if (word == "UNPIN") {
      int page_idx;
      ss >> page_idx;
      buffer_pool.unpin({page_idx * globalDiskInfo.block_size});
    } else if (word == "INFO")
      disk_info(buffer_pool);
  }
  std::cout << std::endl;
}
