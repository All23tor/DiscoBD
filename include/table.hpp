#ifndef CSV_HPP
#define CSV_HPP

#include "disk.hpp"
#include "interpreter.hpp"
#include "type.hpp"

#include <cstdint>
#include <iostream>
#include <utility>
#include <vector>

struct Table {
  Db::SmallString name;
  Address sector;
};

namespace {
static constexpr Address NullAddress = {-1};

Address request_empty_sector() {
  int total_sectors = globalDiskInfo.plates * 2 * globalDiskInfo.tracks *
                      globalDiskInfo.sectors;
  int total_blocks = total_sectors / globalDiskInfo.block_size;
  for (int block_idx = 0; block_idx < total_blocks; block_idx++) {
    for (int s_offset = 0; s_offset < globalDiskInfo.block_size; s_offset++) {
      Address address = {block_idx * globalDiskInfo.block_size + s_offset};
      auto data = CowBlock::load_sector(address);
      int data_begin = reinterpret_cast<const int&>(*data);
      if (data_begin == 0)
        return address;
    }
  }
  throw std::bad_alloc();
}

void disk_info() {
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
      auto data = CowBlock::load_sector(address);
      int data_begin = reinterpret_cast<const int&>(*data);
      if (data_begin == 0) {
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

Address search_table(const std::string& table_name) {
  auto first_data = CowBlock::load_sector({0}) + sizeof(DiskInfo);
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

Address* write_header(const std::string& table_name,
                      const std::vector<Db::Column>& columns) {
  auto first_data = CowBlock::load_writeable_sector({0}) + sizeof(DiskInfo);
  auto tables = reinterpret_cast<Table*>(first_data);
  int table_idx = 0;

  while (tables[table_idx].name[0] != '\0')
    table_idx++;

  auto& table = tables[table_idx];
  for (int i = 0; i < table_name.size(); i++)
    table.name[i] = table_name[i];
  for (int i = table_name.size(); i < table.name.size(); i++)
    table.name[i] = '\0';

  auto header_sector = request_empty_sector();
  auto header_data = CowBlock::load_writeable_sector(header_sector);
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
} // namespace

inline bool load_csv(const std::string& csv_name) {
  bool already_exists = (search_table(csv_name) != NullAddress);
  if (already_exists)
    return false;

  std::ifstream file(csv_name + ".csv");
  if (!file)
    return false;

  std::string schema_str;
  std::getline(file, schema_str);
  std::stringstream schema(std::move(schema_str));

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

  auto records_start = write_header(csv_name, columns);

  int recods_per_sector =
      (globalDiskInfo.bytes - sizeof(Address) - sizeof(int)) / record_size;
  *records_start = request_empty_sector();
  auto record_data = CowBlock::load_writeable_sector(*records_start);
  auto next_sector = reinterpret_cast<Address*>(record_data);
  record_data += sizeof(Address);
  auto record_count = reinterpret_cast<int*>(record_data);
  record_data += sizeof(int);
  *next_sector = NullAddress;
  *record_count = 0;

  while (std::getline(file, line)) {
    if (*record_count == recods_per_sector) {
      *next_sector = request_empty_sector();
      record_data = CowBlock::load_writeable_sector(*next_sector);
      next_sector = reinterpret_cast<Address*>(record_data);
      record_data += sizeof(Address);
      record_count = reinterpret_cast<int*>(record_data);
      record_data += sizeof(int);
      *next_sector = NullAddress;
      *record_count = 0;
    }

    std::stringstream ss(std::move(line));
    for (auto& [name, type] : columns) {
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
        for (int i = field.size(); i < Db::size_of_type(Db::Type::String); i++)
          *(record_data++) = '\0';
        break;
      }
      }
    }
    (*record_count)++;
  }
  return true;
}

inline void select_all(const std::string& table_name) {
  auto header_sector = search_table(table_name);
  if (header_sector == NullAddress) {
    std::cerr << "Tabla " << table_name << " no existe\n";
    return;
  }
  auto header_data = CowBlock::load_sector(header_sector);
  auto records_adress = reinterpret_cast<const Address&>(*header_data);
  header_data += sizeof(Address);

  int column_size = reinterpret_cast<const int&>(*header_data);
  header_data += sizeof(column_size);

  auto columns = reinterpret_cast<const Db::Column*>(header_data);
  auto record_size = 0uz;
  for (auto idx = 0uz; idx < column_size; idx++)
    record_size += Db::size_of_type(columns[idx].type);

  while (records_adress != NullAddress) {
    auto records_data = CowBlock::load_sector(records_adress);
    auto next_adress = reinterpret_cast<const Address&>(*records_data);
    records_data += sizeof(Address);
    auto record_count = reinterpret_cast<const int&>(*records_data);
    records_data += sizeof(int);

    for (auto record_idx = 0uz; record_idx < record_count; record_idx++) {
      for (auto column_idx = 0uz; column_idx < column_size; column_idx++) {
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
      records_data += record_size;
    }

    records_adress = next_adress;
  }
}

inline void select_all_where(const std::string& table_name,
                             const std::string& expression) {
  auto header_sector = search_table(table_name);
  if (header_sector == NullAddress) {
    std::cerr << "Tabla " << table_name << " no existe\n";
    return;
  }
  auto header_data = CowBlock::load_sector(header_sector);
  auto records_adress = reinterpret_cast<const Address&>(*header_data);
  header_data += sizeof(Address);
  int column_size = reinterpret_cast<const int&>(*header_data);
  header_data += sizeof(column_size);

  auto columns = reinterpret_cast<const Db::Column*>(header_data);
  auto record_size = 0uz;
  for (auto idx = 0uz; idx < column_size; idx++)
    record_size += Db::size_of_type(columns[idx].type);

  auto tree = parseExpression(expression, columns, column_size);
  while (records_adress != NullAddress) {
    auto records_data = CowBlock::load_sector(records_adress);
    auto next_adress = reinterpret_cast<const Address&>(*records_data);
    records_data += sizeof(Address);
    auto record_count = reinterpret_cast<const int&>(*records_data);
    records_data += sizeof(int);

    for (auto record_idx = 0uz; record_idx < record_count; record_idx++) {
      bool selected;
      selected = tree->evaluate(records_data, columns).get<Db::Type::Bool>();

      if (!selected) {
        records_data += record_size;
        continue;
      }

      for (auto column_idx = 0uz; column_idx < column_size; column_idx++) {
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
      records_data += record_size;
    }

    records_adress = next_adress;
  }
}
#endif
