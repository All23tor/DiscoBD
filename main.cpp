#include "Disk.hpp"
#include "Table.hpp"
#include <iostream>
#include <sstream>

void handle_inputs() {
  BufferManager buffer_manager;

  std::cout << "Información del disco:\n";
  std::cout << "Número de platos: " << global.plates << '\n';
  std::cout << "Número de pistas por plato: " << global.tracks << '\n';
  std::cout << "Número de sectores por pista: " << global.sectors << '\n';
  std::cout << "Número de bytes por sector: " << global.bytes << '\n';
  std::cout << "Número de sectores por bloque: " << global.block_size << '\n'
            << '\n';

  std::string line;
  while (std::cout << "  > ", std::getline(std::cin, line)) {
    std::stringstream ss{std::move(line)};
    std::string word;
    ss >> word;
    if (word == "LOAD") {
      std::string name;
      ss >> name;
      load_csv(name, buffer_manager);
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
            select_all_where(table_name, clause, buffer_manager);
          } else {
            select_all(table_name, buffer_manager);
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
          delete_where(table_name, clause, buffer_manager);
        }
      }
    } else if (word == "BUFFER") {
      buffer_manager.print();
    } else if (word == "REQUEST") {
      int page_idx;
      ss >> page_idx;
      char rw;
      ss >> rw;
      if (rw == 'W')
        buffer_manager.load_sector<false>({page_idx * global.block_size});
      else if (rw == 'L')
        buffer_manager.load_sector({page_idx * global.block_size});
    } else if (word == "PIN") {
      int page_idx;
      ss >> page_idx;
      buffer_manager.pin({page_idx * global.block_size});
    } else if (word == "UNPIN") {
      int page_idx;
      ss >> page_idx;
      buffer_manager.unpin({page_idx * global.block_size});
    } else if (word == "INFO")
      disk_info(buffer_manager);
  }
  std::cout << std::endl;
}

int main() {
  if (!fs::exists(disk_path)) {
    DiskInfo diskInfo;
    std::cout << "El disco aún no existe, se procederá a su creación\n\n";
    make_disk();
  }
  handle_inputs();
}
