#ifndef CSV_HPP
#define CSV_HPP

#include "BufferManager.hpp"

using str_view = std::string_view;
void load_csv(str_view csv, BufferManager&);
void select_all(str_view table, BufferManager&);
void select_all_where(str_view table, str_view expr, BufferManager&);
void delete_where(str_view table, str_view expr, BufferManager&);
void disk_info(BufferManager&);

#endif
