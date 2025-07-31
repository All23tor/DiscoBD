#ifndef CSV_HPP
#define CSV_HPP

#include <string_view>

void load_csv(std::string_view csv);
void select_all(std::string_view table);
void select_all_where(std::string_view table, std::string_view expr);
void delete_where(std::string_view table, std::string_view expr);
void disk_info();

#endif
