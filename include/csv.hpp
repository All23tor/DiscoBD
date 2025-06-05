#ifndef CSV_HPP
#define CSV_HPP

#include "disk.hpp"

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

enum class Type : std::size_t {
  Int,
  Float,
  Bool,
  String,
};

static const std::map<Type, std::size_t> typeSizes = {
    {Type::Int, sizeof(std::int64_t)},
    {Type::Float, sizeof(double)},
    {Type::Bool, sizeof(bool)},
    {Type::String, sizeof(std::array<char, 64>)}};

inline std::istream& operator>>(std::istream& is, Type& type) {
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

inline Address request_empty_sector() {
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

static constexpr Address NullAddress = {-1};
static constexpr std::size_t string_size = 16uz;

using Columns = std::vector<std::pair<std::string, Type>>;
struct Table {
  std::array<char, 16> name;
  Address sector;
};

static Address search_table(const std::string& table_name) {
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

static Address* write_header(const std::string& table_name,
                             const Columns& columns) {
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

  for (char c : table_name)
    *(header_data++) = c;
  for (int i = table_name.size(); i < string_size; i++)
    *(header_data++) = '\0';

  auto records_start = reinterpret_cast<Address*>(header_data);
  *records_start = NullAddress;
  header_data += sizeof(Address);

  int column_size = columns.size();
  for (char c : pun_cast(column_size))
    *(header_data++) = c;

  for (auto& [name, type] : columns) {
    for (char c : pun_cast(type))
      *(header_data++) = c;
    for (char c : name)
      *(header_data++) = c;
    for (int i = name.size(); i < string_size; i++)
      *(header_data++) = '\0';
  }

  return records_start;
}

inline bool load_csv(const std::string& csv_name) {
  bool already_exists = (search_table(csv_name) != NullAddress);
  if (already_exists)
    return false;

  std::ifstream file(csv_name + ".csv");

  std::string schema_str;
  std::getline(file, schema_str);
  std::stringstream schema(std::move(schema_str));

  std::size_t record_size = 0;
  Columns columns;
  std::string line;
  while (std::getline(schema, line, ',')) {
    std::stringstream ss(std::move(line));
    Type type;
    std::string name;
    std::getline(ss, name, '#');
    ss >> type;
    columns.push_back({name, type});
    record_size += typeSizes.at(type);
  }

  auto records_start = write_header(csv_name, columns);

  int recods_per_sector =
      (globalDiskInfo.bytes - sizeof(Address)) / record_size;
  *records_start = request_empty_sector();
  auto record_data = CowBlock::load_writeable_sector(*records_start);
  auto next_sector = reinterpret_cast<Address*>(record_data);
  *next_sector = NullAddress;
  record_data += sizeof(Address);

  int records_written = 0;
  while (std::getline(file, line)) {
    if (records_written == recods_per_sector) {
      records_written = 0;
      *next_sector = request_empty_sector();
      record_data = CowBlock::load_writeable_sector(*next_sector);
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

void read_table(const std::string& table_name) {}

#endif
