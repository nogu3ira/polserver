/** @file
 *
 * @par History
 */


#ifndef H_STORAGE_H
#define H_STORAGE_H

#include <map>
#include <string>

#include "../clib/maputil.h"
#include "../sqlite-amalgamation-3290000/sqlite3.h"

namespace Pol
{
namespace Items
{
class Item;
}
namespace Realms
{
class Realm;
}
namespace Clib
{
class ConfigFile;
class ConfigElem;
class StreamWriter;
}
namespace Core
{
class StorageArea
{
public:
  StorageArea( std::string name );
  ~StorageArea();

  Items::Item* find_root_item( const std::string& name );
  void insert_root_item( Items::Item* item );
  bool delete_root_item( const std::string& name );
  void on_delete_realm( Realms::Realm* realm );

  void print( Clib::StreamWriter& sw ) const;
  void load_item( Clib::ConfigElem& elem );
  size_t estimateSize() const;

private:
  std::string _name;

  // TODO: ref_ptr<Item> ?
  typedef std::map<std::string, Items::Item*, Clib::ci_cmp_pred> Cont;
  Cont _items;  // owns its items.

  friend class StorageAreaImp;
  friend class StorageAreaIterator;
  friend void write_dirty_storage( Clib::StreamWriter& );
};

class Storage
{
public:
  StorageArea* find_area( const std::string& name );
  StorageArea* create_area( const std::string& name );
  StorageArea* create_area( Clib::ConfigElem& elem );
  void on_delete_realm( Realms::Realm* realm );

  void print( Clib::StreamWriter& sw ) const;
  void read( Clib::ConfigFile& cf );
  void clear();
  size_t estimateSize() const;

private:
  // TODO: investigate if this could store objects. Does find()
  // return object copies, or references?
  typedef std::map<std::string, StorageArea*> AreaCont;
  AreaCont areas;

  sqlite3* SQLiteDB;
  void SQLite_Connect();
  bool SQLite_ExistStorageArea( const std::string& name );
  void SQLite_ListStorageAreas();
  StorageArea* create_areaCache( const std::string& name );
  void Storage::SQLite_finish( sqlite3_stmt* stmt, int x = 1 );
  typedef std::basic_string<unsigned char*> ustring;
  void SQLite_AddStorageArea( const std::string& name );

  friend class StorageAreasImp;
  friend class StorageAreasIterator;
  friend void write_dirty_storage( Clib::StreamWriter& );
};
}
}
#endif
