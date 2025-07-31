#include "Disk.hpp"
#include "Table.hpp"
#include <iostream>
#include <sstream>

void handle_inputs() {
  std::clog << "Información del disco:\n";
  std::clog << "Número de platos: " << global.plates << '\n';
  std::clog << "Número de pistas por plato: " << global.tracks << '\n';
  std::clog << "Número de sectores por pista: " << global.sectors << '\n';
  std::clog << "Número de bytes por sector: " << global.bytes << '\n';
  std::clog << "Número de sectores por bloque: " << global.block_size << '\n'
            << '\n';

  std::string line;
  while (std::clog << "  > ", std::getline(std::cin, line)) {
    std::stringstream ss{std::move(line)};
    std::string word;
    ss >> word;
    if (word == "LOAD") {
      std::string name;
      ss >> name;
      load_csv(name);
      std::clog << "\tSe cargó la tabla " << name << " exitosamente\n";
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
    } else if (word == "INFO")
      disk_info();
  }
  std::clog << std::endl;
}

int main() {
  if (!fs::exists(disk_path)) {
    DiskInfo diskInfo;
    std::cout << "El disco aún no existe, se procederá a su creación\n\n";
    make_disk();
  }
  handle_inputs();
}
