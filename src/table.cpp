#include "table.hpp"
#include "disk.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <map>
#include <utility>
#include <vector>

namespace {
template <class T>
auto& pun_cast(T& t) {
  return reinterpret_cast<std::array<char, sizeof(T)>&>(t);
}
template <class T>
const auto& pun_cast(const T& t) {
  return reinterpret_cast<const std::array<char, sizeof(T)>&>(t);
}
enum class Type : std::size_t {
  Int,
  Float,
  Bool,
  String,
};

const std::map<Type, std::size_t> typeSizes = {
    {Type::Int, sizeof(std::int64_t)},
    {Type::Float, sizeof(double)},
    {Type::Bool, sizeof(bool)},
    {Type::String, sizeof(std::array<char, 64>)}};

std::istream& operator>>(std::istream& is, Type& type) {
  static const std::map<std::string, Type> typeNames = {
      {"INT", Type::Int},
      {"FLOAT", Type::Float},
      {"BOOL", Type::Bool},
      {"STRING", Type::String}};

  std::string name;
  std::getline(is, name, ' ');
  type = typeNames.at(name);
  return is;
}

Address request_empty_sector() {
  int total_sectors = globalDiskInfo.plates * 2 * globalDiskInfo.tracks *
                      globalDiskInfo.sectors;
  int total_blocks = total_sectors / globalDiskInfo.block_size;
  for (int block_idx = 0; block_idx < total_blocks; block_idx++) {
    for (int s_offset = 0; s_offset < globalDiskInfo.block_size; s_offset++) {
      Address address = {block_idx * globalDiskInfo.block_size + s_offset};
      auto data = load_sector(address);
      int data_begin = reinterpret_cast<const int&>(*data);
      if (data_begin == 0)
        return address;
    }
  }
  throw std::bad_alloc();
}

constexpr Address NullAddress = {-1};
using SmallString = std::array<char, 16>;

struct Column {
  SmallString name;
  Type type;
};

struct Table {
  SmallString name;
  Address sector;
};

Address search_table(const std::string& table_name) {
  auto first_data = load_sector({0}) + sizeof(DiskInfo);
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
                      const std::vector<Column>& columns) {
  auto first_data = load_writeable_sector({0}) + sizeof(DiskInfo);
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
  auto header_data = load_writeable_sector(header_sector);
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

bool load_csv(const std::string& csv_name) {
  bool already_exists = (search_table(csv_name) != NullAddress);
  if (already_exists)
    return false;

  std::ifstream file(csv_name + ".csv");

  std::string schema_str;
  std::getline(file, schema_str);
  std::stringstream schema(std::move(schema_str));

  std::size_t record_size = 0;
  std::vector<Column> columns;
  std::string line;
  while (std::getline(schema, line, ',')) {
    std::stringstream ss(std::move(line));
    Column column;
    std::string name;
    std::getline(ss, name, '#');
    ss >> column.type;
    for (int i = 0; i < name.size(); i++)
      column.name[i] = name[i];
    for (int i = name.size(); i < column.name.size(); i++)
      column.name[i] = '\0';
    record_size += typeSizes.at(column.type);
    columns.push_back(std::move(column));
  }

  auto records_start = write_header(csv_name, columns);

  int recods_per_sector =
      (globalDiskInfo.bytes - sizeof(Address)) / record_size;
  *records_start = request_empty_sector();
  auto record_data = load_writeable_sector(*records_start);
  auto next_sector = reinterpret_cast<Address*>(record_data);
  *next_sector = NullAddress;
  record_data += sizeof(Address);

  int records_written = 0;
  while (std::getline(file, line)) {
    if (records_written == recods_per_sector) {
      records_written = 0;
      *next_sector = request_empty_sector();
      record_data = load_writeable_sector(*next_sector);
      next_sector = reinterpret_cast<Address*>(record_data);
      *next_sector = NullAddress;
      record_data += sizeof(Address);
    }

    std::stringstream ss(std::move(line));
    for (auto& [name, type] : columns) {
      std::string field;
      switch (type) {
      case Type::Int: {
        std::getline(ss, field, ',');
        if (field.empty())
          field = "0";
        std::int64_t val = std::stol(field);
        for (char c : pun_cast(val))
          *(record_data++) = c;
        break;
      }
      case Type::Float: {
        std::getline(ss, field, ',');
        if (field.empty())
          field = "0";
        double val = std::stod(field);
        for (char c : pun_cast(val))
          *(record_data++) = c;
        break;
      }
      case Type::Bool: {
        std::getline(ss, field, ',');
        if (field == "yes")
          *(record_data++) = 1;
        else
          *(record_data++) = 0;
        break;
      }
      case Type::String: {
        if (ss.peek() == '"')
          ss >> std::quoted(field), ss.ignore(1);
        else
          std::getline(ss, field, ',');
        for (char c : field)
          *(record_data++) = c;
        for (int i = field.size(); i < typeSizes.at(Type::String); i++)
          *(record_data++) = '\0';
        break;
      }
      }
    }
    records_written++;
  }
  return true;
}

void read_table(const std::string& table_name) {
  auto header_sector = search_table(table_name);
  if (header_sector == NullAddress) {
    std::cerr << "Tabla " << table_name << " no existe\n";
    return;
  }
  auto header_data = load_sector(header_sector);
  auto records_adress = reinterpret_cast<const Address&>(*header_data);
  header_data += sizeof(Address);
  if (records_adress == NullAddress)
    return;

  int column_size = reinterpret_cast<const int&>(*header_data);
  header_data += sizeof(column_size);

  auto columns = reinterpret_cast<const Column*>(header_data);
  auto record_size = 0uz;
  for (auto idx = 0uz; idx < column_size; idx++)
    record_size += typeSizes.at(columns[idx].type);

  int recods_per_sector =
      (globalDiskInfo.bytes - sizeof(Address)) / record_size;

  while (records_adress != NullAddress) {
    auto records_data = load_sector(records_adress);
    auto next_adress = reinterpret_cast<const Address&>(records_data);
    records_data += sizeof(Address);

    for (auto record_idx = 0uz; record_idx < recods_per_sector; record_idx++) {
      for (auto column_idx = 0uz; column_idx < column_size; column_idx++) {}
    }

    records_adress = next_adress;
    std::cout << records_adress.address << '\n';
  }
}
