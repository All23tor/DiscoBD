#ifndef CSV_HPP
#define CSV_HPP

#include "BufferManager.hpp"
#include "Interpreter.hpp"
#include "Type.hpp"

#include <cstdint>
#include <ios>
#include <iostream>
#include <limits>
#include <utility>
#include <vector>

// Estructura POD que representa el modo en el que la información de una tabla
// se guarda en el disco
struct Table {
  Db::SmallString name;
  Address sector;
};

namespace {
// Valor que utilizamos de dirección nula
static constexpr Address NullAddress = {-1};

// Lee de una linea de un archivo csv las columnas,
// adicionalmente calcula el tamaño de cada registro y lo retorna
std::pair<std::vector<Db::Column>, std::size_t>
read_columns(std::stringstream schema) {
  std::size_t record_size = 0;
  std::vector<Db::Column> columns;
  std::string line;
  while (std::getline(schema, line, ',')) {
    std::stringstream ss(std::move(line));
    Db::Column column;
    std::string name;
    std::getline(ss, name, '#');
    name.resize(sizeof(Db::SmallString));
    ss >> column.type;
    for (int i = 0; i < name.size(); i++)
      column.name[i] = name[i];
    for (int i = name.size(); i < column.name.size(); i++)
      column.name[i] = '\0';
    record_size += Db::size_of_type(column.type);
    columns.push_back(std::move(column));
  }

  return {columns, record_size};
}

// Solicita un sector que contenga espacio para registros adicionales recibiendo
// la cantidad máxima de registros de la tabla por sector y el buffer manager
Address request_available_sector(int records_per_sector,
                                 BufferManager& buffer_manager) {
  int total_sectors = globalDiskInfo.plates * 2 * globalDiskInfo.tracks *
                      globalDiskInfo.sectors;
  int total_blocks = total_sectors / globalDiskInfo.block_size;
  for (int block_idx = 0; block_idx < total_blocks; block_idx++) {
    for (int s_offset = 0; s_offset < globalDiskInfo.block_size; s_offset++) {
      Address address = {block_idx * globalDiskInfo.block_size + s_offset};
      auto data = buffer_manager.load_sector(address);
      data += sizeof(Address);
      int record_count = reinterpret_cast<const int&>(*data);
      if (record_count < records_per_sector)
        return address;
    }
  }
  throw std::bad_alloc();
}

// Solicita un sector que no esté siendo utilizado recibiendo
// la cantidad máxima de registros de la tabla por sector y el buffer manager
Address request_empty_sector(BufferManager& buffer_manager) {
  int total_sectors = globalDiskInfo.plates * 2 * globalDiskInfo.tracks *
                      globalDiskInfo.sectors;
  int total_blocks = total_sectors / globalDiskInfo.block_size;
  for (int block_idx = 0; block_idx < total_blocks; block_idx++) {
    for (int s_offset = 0; s_offset < globalDiskInfo.block_size; s_offset++) {
      Address address = {block_idx * globalDiskInfo.block_size + s_offset};
      auto data = buffer_manager.load_sector(address);
      auto next_address = reinterpret_cast<const Address&>(*data);
      if (next_address.address == 0)
        return address;
    }
  }
  throw std::bad_alloc();
}

// Imprimeen pantalla la información del disco (para el acceso a este requiere
// del buffer manager)
void disk_info(BufferManager& buffer_manager) {
  auto total_bytes = globalDiskInfo.plates * 2 * globalDiskInfo.tracks *
                     globalDiskInfo.sectors * globalDiskInfo.bytes;
  std::cout << "Capacidad total del disco: " << total_bytes << " bytes \n";

  int sectors_available = 0;
  std::cout << "Sectores disponibles:\n";
  int total_sectors = globalDiskInfo.plates * 2 * globalDiskInfo.tracks *
                      globalDiskInfo.sectors;
  int total_blocks = total_sectors / globalDiskInfo.block_size;
  for (int block_idx = 0; block_idx < total_blocks; block_idx++) {
    for (int s_offset = 0; s_offset < globalDiskInfo.block_size; s_offset++) {
      Address address = {block_idx * globalDiskInfo.block_size + s_offset};
      auto data = buffer_manager.load_sector(address);
      auto next_address = reinterpret_cast<const Address&>(*data);
      if (next_address.address == 0) {
        sectors_available++;
        std::cout << address.to_path().string() << '\n';
      }
    }
  }
  std::cout << "En total hay " << sectors_available
            << " sectores disponibles\n";
  std::cout << "En total hay " << total_sectors - sectors_available
            << " sectores ocupados\n";
  int free_bytes = sectors_available * globalDiskInfo.bytes;
  std::cout << "El disco tiene " << free_bytes << " bytes disponibles\n";
  std::cout << "El disco tiene " << total_bytes - free_bytes
            << " bytes ocupados\n";
}

// Busca una tabla dentro del primer sector del disco (para esto requiere del
// buffer manager), de no encontrar la tabla, retorna la direccion nula
Address search_table(const std::string& table_name,
                     BufferManager& buffer_manager) {
  auto first_data = buffer_manager.load_sector({0}) + sizeof(DiskInfo);
  auto tables = reinterpret_cast<const Table*>(first_data);
  int table_idx = 0;

  while (tables[table_idx].name[0] != '\0') {
    auto& table = tables[table_idx++];
    bool found = true;
    for (int i = 0; i < table_name.size(); i++) {
      if (table.name[i] != table_name[i]) {
        found = false;
        break;
      }
    }
    if (found)
      return table.sector;
  }

  return NullAddress;
}

// Dentro del primer sector del disco (para esto requiere del
// buffer manager), escribe la información de una nueva tabla (table_name,
// columns), retorna un puntero al campo del primer sector que contiene
// registros de la tabla (su valor siempre será nulo, pero es útil el mantener
// su ubicación)
Address* write_table_header(const std::string& table_name,
                            const std::vector<Db::Column>& columns,
                            BufferManager& buffer_manager) {
  auto first_data =
      buffer_manager.load_writeable_sector({0}) + sizeof(DiskInfo);
  auto tables = reinterpret_cast<Table*>(first_data);
  int table_idx = 0;

  while (tables[table_idx].name[0] != '\0')
    table_idx++;

  auto& table = tables[table_idx];
  for (int i = 0; i < table_name.size(); i++)
    table.name[i] = table_name[i];
  for (int i = table_name.size(); i < table.name.size(); i++)
    table.name[i] = '\0';

  auto header_sector = request_empty_sector(buffer_manager);
  auto header_data = buffer_manager.load_writeable_sector(header_sector);
  buffer_manager.pin(header_sector);
  table.sector = header_sector;

  auto records_start = reinterpret_cast<Address*>(header_data);
  *records_start = NullAddress;
  header_data += sizeof(Address);

  int column_size = columns.size();
  for (char c : pun_cast(column_size))
    *(header_data++) = c;

  for (auto& column : columns)
    for (char c : pun_cast(column))
      *(header_data++) = c;

  return records_start;
}

// Escribe la cabecera de un sector, proporcionados un lugar donde guardar su
// dirección (next_sector), un lugar donde guardar los datos (record_data), y
// accesos rápidos para modificar la cuenta de registros y el bitmap de
// eliminaciones (record_count, bitmap), necesita para esto el tamaño del bitmap
// y acceso al buffer_manager
void write_sector_header(Address*& next_sector, char*& record_data,
                         int*& record_count, char*& bitmap, int bitmap_size,
                         BufferManager& buffer_manager) {
  *next_sector = request_empty_sector(buffer_manager);
  record_data = buffer_manager.load_writeable_sector(*next_sector);
  next_sector = reinterpret_cast<Address*>(record_data);
  *next_sector = NullAddress;
  record_data += sizeof(Address);
  record_count = reinterpret_cast<int*>(record_data);
  *record_count = 0;
  record_data += sizeof(int);
  bitmap = record_data;
  while (bitmap_size--) {
    *record_data = 0;
    record_data++;
  }
}

// Escribe un registro (ss) en la dirección proporcionada (record_data), para
// esto requiere la información de las columnas (columns, columns_size)
void write_record(char*& record_data, std::stringstream ss,
                  const Db::Column* columns, int columns_size) {
  for (int column_idx = 0; column_idx < columns_size; column_idx++) {
    const auto& [name, type] = columns[column_idx];
    std::string field;
    switch (type) {
    case Db::Type::Int: {
      std::getline(ss, field, ',');
      if (field.empty())
        field = "0";
      std::int64_t val = std::stol(field);
      for (char c : pun_cast(val))
        *(record_data++) = c;
      break;
    }
    case Db::Type::Float: {
      std::getline(ss, field, ',');
      if (field.empty())
        field = "0";
      double val = std::stod(field);
      for (char c : pun_cast(val))
        *(record_data++) = c;
      break;
    }
    case Db::Type::Bool: {
      std::getline(ss, field, ',');
      if (field == "yes")
        *(record_data++) = 1;
      else
        *(record_data++) = 0;
      break;
    }
    case Db::Type::String: {
      if (ss.peek() == '"')
        ss >> std::quoted(field), ss.ignore(1);
      else
        std::getline(ss, field, ',');
      for (char c : field)
        *(record_data++) = c;
      static_assert(Db::size_of_type(Db::Type::String) == 64);
      for (int i = field.size(); i < Db::size_of_type(Db::Type::String); i++)
        *(record_data++) = '\0';
      break;
    }
    }
  }
}

// Escribe todos los datos de un archivo (file), empezando por un sector
// (next_sector), y solicitando más en la medida que se requira, para esto
// requiere de la información de la columnas (columns, columns_size) y acceso al
// buffer manager
void write_table_data(std::ifstream& file, Address* next_sector,
                      const Db::Column* columns, int columns_size,
                      int records_per_sector, BufferManager& buffer_manager) {
  int bitmap_size = (records_per_sector + 7) / 8; // ceiling division
  char* record_data;
  int* record_count;
  char* bitmap;
  write_sector_header(next_sector, record_data, record_count, bitmap,
                      bitmap_size, buffer_manager);

  for (std::string line; std::getline(file, line); (*record_count)++) {
    if (*record_count == records_per_sector)
      write_sector_header(next_sector, record_data, record_count, bitmap,
                          bitmap_size, buffer_manager);

    write_record(record_data, std::stringstream(std::move(line)), columns,
                 columns_size);
    bitmap[*record_count / 8] |= 1 << (*record_count % 8);
  }
}

// Información de la cabecera de cada tabla en una estructura POD, solo se
// utiliza al leer información ya existente en el disco
struct TableHeaderInfo {
  Address records_address;
  std::size_t record_size;
  const Db::Column* columns;
  int columns_size;
  int bitmap_size;
};

// Lee la cabcera de una tabla (de exisitir, caso contrario no hace nada), para
// esto necesita acceso al buffer manager. Retorna la información leída.
TableHeaderInfo read_table_header(const std::string& table_name,
                                  BufferManager& buffer_manager) {
  auto header_sector = search_table(table_name, buffer_manager);
  if (header_sector == NullAddress)
    throw std::exception();

  auto header_data = buffer_manager.load_sector(header_sector);
  buffer_manager.pin(header_sector);
  auto records_address = reinterpret_cast<const Address&>(*header_data);
  header_data += sizeof(Address);
  int columns_size = reinterpret_cast<const int&>(*header_data);
  header_data += sizeof(columns_size);

  auto columns = reinterpret_cast<const Db::Column*>(header_data);
  auto record_size = 0uz;
  for (auto idx = 0uz; idx < columns_size; idx++)
    record_size += Db::size_of_type(columns[idx].type);

  int records_per_sector =
      8 * (globalDiskInfo.bytes - sizeof(Address) - sizeof(int)) /
      (8 * record_size + 1);
  int bitmap_size = (records_per_sector + 7) / 8; // ceiling division
  return {records_address, record_size, columns, columns_size, bitmap_size};
}

// Itera a traves de los registros invocando un funtor que no modifica los
// registros (v), (empezando por records_adress), para realizar el proceso
// requiere del tamaño del bitmap (bitmap_size), el tamaño de cada registro
// (record_size), acceso al buffer manager
template <class Visitor>
void visit_records(Address records_address, int bitmap_size, int record_size,
                   BufferManager& buffer_manager, Visitor&& v) {
  while (records_address != NullAddress) {
    auto records_data = buffer_manager.load_sector(records_address);
    auto next_address = reinterpret_cast<const Address&>(*records_data);
    records_data += sizeof(Address);
    auto record_count = reinterpret_cast<const int&>(*records_data);
    records_data += sizeof(int);
    auto bitmap = records_data;
    records_data += bitmap_size;

    for (auto record_idx = 0uz; record_idx < record_count; record_idx++) {
      v(records_data, record_idx, bitmap);
      records_data += record_size;
    }

    records_address = next_address;
  }
}

// Itera a traves de los registros invocando un funtor que puede modificar los
// registros (v), (empezando por records_adress), para realizar el proceso
// requiere del tamaño del bitmap (bitmap_size), el tamaño de cada registro
// (record_size), acceso al buffer manager
template <class Visitor>
void visit_writeable_records(Address records_address, int bitmap_size,
                             int record_size, BufferManager& buffer_manager,
                             Visitor&& v) {
  while (records_address != NullAddress) {
    auto records_data = buffer_manager.load_writeable_sector(records_address);
    auto next_address = reinterpret_cast<Address&>(*records_data);
    records_data += sizeof(Address);
    auto record_count = reinterpret_cast<int&>(*records_data);
    records_data += sizeof(int);
    auto bitmap = records_data;
    records_data += bitmap_size;

    for (auto record_idx = 0uz; record_idx < record_count; record_idx++) {
      v(records_data, record_idx, bitmap);
      records_data += record_size;
    }

    records_address = next_address;
  }
}

} // namespace

// Carga los registros de un .csv (y adicionalmente crea una tabla de no
// existir)
inline void load_csv(const std::string& csv_name,
                     BufferManager& buffer_manager) {
  std::ifstream file(csv_name + ".csv");
  const auto header_sector = search_table(csv_name, buffer_manager);

  if (header_sector != NullAddress) {
    auto header_info = read_table_header(csv_name, buffer_manager);
    file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    int records_per_sector =
        8 * (globalDiskInfo.bytes - sizeof(Address) - sizeof(int)) /
        (8 * header_info.record_size + 1);

    auto header_data = buffer_manager.load_writeable_sector(header_sector);
    Address* next_address = reinterpret_cast<Address*>(header_data);
    auto records_address = *next_address;
    while (records_address != NullAddress) {
      auto records_data = buffer_manager.load_writeable_sector(records_address);
      next_address = reinterpret_cast<Address*>(records_data);
      records_address = *next_address;
    }

    write_table_data(file, next_address, header_info.columns,
                     header_info.columns_size, records_per_sector,
                     buffer_manager);
  } else {
    std::string schema_str;
    std::getline(file, schema_str);
    auto [columns, record_size] =
        read_columns(std::stringstream(std::move(schema_str)));
    int records_per_sector =
        8 * (globalDiskInfo.bytes - sizeof(Address) - sizeof(int)) /
        (8 * record_size + 1);

    auto records_start = write_table_header(csv_name, columns, buffer_manager);
    write_table_data(file, records_start, columns.data(), columns.size(),
                     records_per_sector, buffer_manager);
  }

  buffer_manager.unpin(header_sector);
}

// Selecciona todos los campos de los registros de una tabla y los muestra en
// pantalla
inline void select_all(const std::string& table_name,
                       BufferManager& buffer_manager) {
  TableHeaderInfo header_info;
  try {
    header_info = read_table_header(table_name, buffer_manager);
  } catch (...) {
    std::cerr << "Tabla " << table_name << " no existe\n";
    return;
  }

  visit_records(
      header_info.records_address, header_info.bitmap_size,
      header_info.record_size, buffer_manager,
      [columns = header_info.columns, columns_size = header_info.columns_size](
          const char* records_data, std::size_t record_idx,
          const char* bitmap) {
        bool bit = (bitmap[record_idx / 8] >> (record_idx % 8)) & 1;
        if (!bit)
          return;

        for (auto column_idx = 0uz; column_idx < columns_size; column_idx++) {
          if (column_idx != 0)
            std::cout << '#';
          visit_field(records_data, column_idx, columns, [](auto&& arg) {
            if constexpr (requires { std::cout << arg; })
              std::cout << arg;
            else
              std::cout << arg.data();
          });
        }
        std::cout << '\n';
      });
  auto header_sector = search_table(table_name, buffer_manager);
  buffer_manager.unpin(header_sector);
}

// Selecciona todos los campos de los registros de una tabla que satisfagan
// cierta condifión (expression) y los muestra en pantalla
inline void select_all_where(const std::string& table_name,
                             const std::string& expression,
                             BufferManager& buffer_manager) {
  TableHeaderInfo header_info;
  try {
    header_info = read_table_header(table_name, buffer_manager);
  } catch (...) {
    std::cerr << "Tabla " << table_name << " no existe\n";
    return;
  }

  auto tree = parseExpression(expression, header_info.columns,
                              header_info.columns_size);

  visit_records(
      header_info.records_address, header_info.bitmap_size,
      header_info.record_size, buffer_manager,
      [columns = header_info.columns, columns_size = header_info.columns_size,
       &tree](const char* records_data, std::size_t record_idx,
              const char* bitmap) {
        bool bit = (bitmap[record_idx / 8] >> (record_idx % 8)) & 1;
        if (!bit)
          return;

        bool selected;
        selected = tree->evaluate(records_data, columns).get<Db::Type::Bool>();
        if (!selected)
          return;

        for (auto column_idx = 0uz; column_idx < columns_size; column_idx++) {
          if (column_idx != 0)
            std::cout << '#';
          visit_field(records_data, column_idx, columns, [](auto&& arg) {
            if constexpr (requires { std::cout << arg; })
              std::cout << arg;
            else
              std::cout << arg.data();
          });
        }
        std::cout << '\n';
      });
  auto header_sector = search_table(table_name, buffer_manager);
  buffer_manager.unpin(header_sector);
}

// Elimina todos los registros de una tabla que satisfagan
// cierta condifión (expression) y los muestra en pantalla
inline void delete_where(const std::string& table_name,
                         const std::string& expression,
                         BufferManager& buffer_manager) {
  TableHeaderInfo header_info;
  try {
    header_info = read_table_header(table_name, buffer_manager);
  } catch (...) {
    std::cerr << "Tabla " << table_name << " no existe\n";
    return;
  }

  auto tree = parseExpression(expression, header_info.columns,
                              header_info.columns_size);

  visit_writeable_records(
      header_info.records_address, header_info.bitmap_size,
      header_info.record_size, buffer_manager,
      [&tree, columns = header_info.columns,
       columns_size = header_info.columns_size](
          char* records_data, std::size_t record_idx, char* bitmap) {
        bool bit = (bitmap[record_idx / 8] >> (record_idx % 8)) & 1;
        if (!bit)
          return;

        bool selected;
        selected = tree->evaluate(records_data, columns).get<Db::Type::Bool>();
        if (!selected)
          return;

        for (auto column_idx = 0uz; column_idx < columns_size; column_idx++) {
          if (column_idx != 0)
            std::cout << '#';
          visit_field(records_data, column_idx, columns, [](auto&& arg) {
            if constexpr (requires { std::cout << arg; })
              std::cout << arg;
            else
              std::cout << arg.data();
          });
        }
        std::cout << '\n';
        bitmap[record_idx / 8] &= ~(1 << record_idx % 8);
      });
  auto header_sector = search_table(table_name, buffer_manager);
  buffer_manager.unpin(header_sector);
}
#endif
