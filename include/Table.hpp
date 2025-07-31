#ifndef CSV_HPP
#define CSV_HPP

#include "BufferManager.hpp"

void load_csv(std::string_view csv, BufferManager&);
void select_all(std::string_view table, BufferManager&);
void select_all_where(std::string_view table, std::string_view expr,
                      BufferManager&);
void delete_where(std::string_view table, std::string_view expr,
                  BufferManager&);
void disk_info(BufferManager&);

#endif
