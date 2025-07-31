#include "Table.hpp"
#include "Interpreter.hpp"
#include <fstream>
#include <iostream>
#include <map>
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

struct Table {
  Db::SmallString name;
  Address sector;
};

std::istream& operator>>(std::istream& is, Db::Type& type) {
  static const std::map<std::string, Db::Type> typeNames = {
      {"INT", Db::Type::Int},
      {"FLOAT", Db::Type::Float},
      {"BOOL", Db::Type::Bool},
      {"STRING", Db::Type::String}};

  std::string name;
  std::getline(is, name, ' ');
  name.erase(std::find_if(name.rbegin(), name.rend(),
                          [](unsigned char ch) {
                            return !std::isspace(ch);
                          })
                 .base(),
             name.end());
  type = typeNames.at(name);
  return is;
}

static constexpr Address NullAddress = {-1};

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

Address request_empty_sector(BufferManager& buffer_manager) {
  int total_sectors = global.plates * 2 * global.tracks * global.sectors;
  int total_blocks = total_sectors / global.block_size;
  for (int block_idx = 0; block_idx < total_blocks; block_idx++) {
    for (int s_offset = 0; s_offset < global.block_size; s_offset++) {
      Address address = {block_idx * global.block_size + s_offset};
      auto data = buffer_manager.load_sector(address);
      auto next_address = reinterpret_cast<const Address&>(*data);
      if (next_address.address == 0)
        return address;
    }
  }
  throw std::bad_alloc();
}

Address search_table(std::string_view table_name,
                     BufferManager& buffer_manager) {
  auto first_data = buffer_manager.load_sector({0});
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

Address* write_table_header(std::string_view table_name,
                            const std::vector<Db::Column>& columns,
                            BufferManager& buffer_manager) {
  auto first_data = buffer_manager.load_sector<false>({0});
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
  auto header_data = buffer_manager.load_sector<false>(header_sector);
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

void write_sector_header(Address*& next_sector, char*& record_data,
                         int*& record_count, char*& bitmap, int bitmap_size,
                         BufferManager& buffer_manager) {
  *next_sector = request_empty_sector(buffer_manager);
  record_data = buffer_manager.load_sector<false>(*next_sector);
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

void write_record(char*& record_data, std::stringstream ss,
                  std::span<const Db::Column> columns) {
  for (const auto& [name, type] : columns) {
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

void write_table_data(std::ifstream& file, Address* next_sector,
                      std::span<const Db::Column> columns,
                      int records_per_sector, BufferManager& buffer_manager) {
  int bitmap_size = (records_per_sector + 7) / 8;
  char* record_data;
  int* record_count;
  char* bitmap;
  write_sector_header(next_sector, record_data, record_count, bitmap,
                      bitmap_size, buffer_manager);

  for (std::string line; std::getline(file, line); (*record_count)++) {
    if (*record_count == records_per_sector)
      write_sector_header(next_sector, record_data, record_count, bitmap,
                          bitmap_size, buffer_manager);

    write_record(record_data, std::stringstream(std::move(line)), columns);
    bitmap[*record_count / 8] |= 1 << (*record_count % 8);
  }
}

struct TableHeaderInfo {
  Address records_address;
  std::size_t record_size;
  std::span<const Db::Column> columns;
  int bitmap_size;
};

TableHeaderInfo read_table_header(std::string_view table_name,
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

  int records_per_sector = 8 * (global.bytes - sizeof(Address) - sizeof(int)) /
                           (8 * record_size + 1);
  int bitmap_size = (records_per_sector + 7) / 8;
  return {records_address,
          record_size,
          {columns, static_cast<std::size_t>(columns_size)},
          bitmap_size};
}

template <bool Readonly = true, class Visitor>
void visit_records(Address records_address, int bitmap_size, int record_size,
                   BufferManager& buffer_manager, Visitor&& v) {
  while (records_address != NullAddress) {
    auto records_data = buffer_manager.load_sector<Readonly>(records_address);
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
} // namespace

void load_csv(std::string_view csv_name, BufferManager& buffer_manager) {
  std::ifstream file(std::string{csv_name} + ".csv");
  const auto header_sector = search_table(csv_name, buffer_manager);

  if (header_sector != NullAddress) {
    auto header_info = read_table_header(csv_name, buffer_manager);
    file.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    int records_per_sector = 8 *
                             (global.bytes - sizeof(Address) - sizeof(int)) /
                             (8 * header_info.record_size + 1);

    auto header_data = buffer_manager.load_sector<false>(header_sector);
    Address* next_address = reinterpret_cast<Address*>(header_data);
    auto records_address = *next_address;
    while (records_address != NullAddress) {
      auto records_data = buffer_manager.load_sector<false>(records_address);
      next_address = reinterpret_cast<Address*>(records_data);
      records_address = *next_address;
    }

    write_table_data(file, next_address, header_info.columns,
                     records_per_sector, buffer_manager);
  } else {
    std::string schema_str;
    std::getline(file, schema_str);
    auto [columns, record_size] =
        read_columns(std::stringstream(std::move(schema_str)));
    int records_per_sector = 8 *
                             (global.bytes - sizeof(Address) - sizeof(int)) /
                             (8 * record_size + 1);

    auto records_start = write_table_header(csv_name, columns, buffer_manager);
    write_table_data(file, records_start, columns, records_per_sector,
                     buffer_manager);
  }

  buffer_manager.unpin(header_sector);
}

void select_all(std::string_view table_name, BufferManager& buffer_manager) {
  TableHeaderInfo header_info;
  try {
    header_info = read_table_header(table_name, buffer_manager);
  } catch (...) {
    std::cerr << "Tabla " << table_name << " no existe\n";
    return;
  }

  visit_records(header_info.records_address, header_info.bitmap_size,
                header_info.record_size, buffer_manager,
                [columns = header_info.columns](const char* records_data,
                                                std::size_t record_idx,
                                                const char* bitmap) {
                  bool bit = (bitmap[record_idx / 8] >> (record_idx % 8)) & 1;
                  if (!bit)
                    return;

                  auto field = records_data;
                  for (const auto& column : columns) {
                    visit_type(field, column.type, [](auto&& arg) {
                      if constexpr (requires { std::cout << arg; })
                        std::cout << arg;
                      else
                        std::cout << arg.data();
                    });
                    field += size_of_type(column.type);
                    std::cout << '#';
                  }
                  std::cout << '\n';
                });
  auto header_sector = search_table(table_name, buffer_manager);
  buffer_manager.unpin(header_sector);
}

void select_all_where(std::string_view table_name, std::string_view expression,
                      BufferManager& buffer_manager) {
  TableHeaderInfo header_info;
  try {
    header_info = read_table_header(table_name, buffer_manager);
  } catch (...) {
    std::cerr << "Tabla " << table_name << " no existe\n";
    return;
  }

  auto tree = parseExpression(expression, header_info.columns);

  visit_records(
      header_info.records_address, header_info.bitmap_size,
      header_info.record_size, buffer_manager,
      [columns = header_info.columns, &tree](const char* records_data,
                                             std::size_t record_idx,
                                             const char* bitmap) {
        bool bit = (bitmap[record_idx / 8] >> (record_idx % 8)) & 1;
        if (!bit)
          return;

        bool selected =
            tree->evaluate(records_data, columns.data()).get<Db::Type::Bool>();
        if (!selected)
          return;

        auto field = records_data;
        for (const auto& column : columns) {
          visit_type(field, column.type, [](auto&& arg) {
            if constexpr (requires { std::cout << arg; })
              std::cout << arg;
            else
              std::cout << arg.data();
          });
          std::cout << '#';
          field += size_of_type(column.type);
        }
        std::cout << '\n';
      });
  auto header_sector = search_table(table_name, buffer_manager);
  buffer_manager.unpin(header_sector);
}

void delete_where(std::string_view table_name, std::string_view expression,
                  BufferManager& buffer_manager) {
  TableHeaderInfo header_info;
  try {
    header_info = read_table_header(table_name, buffer_manager);
  } catch (...) {
    std::cerr << "Tabla " << table_name << " no existe\n";
    return;
  }

  auto tree = parseExpression(expression, header_info.columns);

  visit_records<false>(
      header_info.records_address, header_info.bitmap_size,
      header_info.record_size, buffer_manager,
      [&tree, columns = header_info.columns](
          char* records_data, std::size_t record_idx, char* bitmap) {
        bool bit = (bitmap[record_idx / 8] >> (record_idx % 8)) & 1;
        if (!bit)
          return;

        bool selected =
            tree->evaluate(records_data, columns.data()).get<Db::Type::Bool>();
        if (!selected)
          return;
        auto field = records_data;
        for (const auto& column : columns) {
          visit_type(field, column.type, [](auto&& arg) {
            if constexpr (requires { std::cout << arg; })
              std::cout << arg;
            else
              std::cout << arg.data();
          });
          std::cout << '#';
          field += size_of_type(column.type);
        }
        std::cout << '\n';
        bitmap[record_idx / 8] &= ~(1 << record_idx % 8);
      });
  auto header_sector = search_table(table_name, buffer_manager);
  buffer_manager.unpin(header_sector);
}

void disk_info(BufferManager& buffer_manager) {
  auto total_bytes =
      global.plates * 2 * global.tracks * global.sectors * global.bytes;
  std::cout << "Capacidad total del disco: " << total_bytes << " bytes \n";

  int sectors_available = 0;
  std::cout << "Sectores disponibles:\n";
  int total_sectors = global.plates * 2 * global.tracks * global.sectors;
  int total_blocks = total_sectors / global.block_size;
  for (int block_idx = 0; block_idx < total_blocks; block_idx++) {
    for (int s_offset = 0; s_offset < global.block_size; s_offset++) {
      Address address = {block_idx * global.block_size + s_offset};
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
  int free_bytes = sectors_available * global.bytes;
  std::cout << "El disco tiene " << free_bytes << " bytes disponibles\n";
  std::cout << "El disco tiene " << total_bytes - free_bytes
            << " bytes ocupados\n";
}
