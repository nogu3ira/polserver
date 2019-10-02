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
}  // namespace Clib
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
  void load_item( Clib::ConfigElem& elem, const std::string& areaName );
  size_t estimateSize() const;

  void SQLite_insert_root_item_onlyDB( Items::Item* item, const std::string& areaName );

private:
  std::string _name;

  // TODO: ref_ptr<Item> ?
  typedef std::map<std::string, Items::Item*, Clib::ci_cmp_pred> Cont;
  Cont _items;  // owns its items.

  friend class StorageAreaImp;
  friend class StorageAreaIterator;
  friend void write_dirty_storage( Clib::StreamWriter& );

  std::string table_Item = "Item";
  StorageArea* create_areaCache( const std::string& name );
  bool SQLite_ExistInStorage( const std::string& name, const std::string& table_name );
  bool SQLite_ExistInStorage( const u32 serial, const std::string& table_name );
  bool SQLite_RemoveItem( const std::string& name );
  bool SQLite_AddItem( Items::Item* item, const std::string& areaName );
  Items::Item* read_itemInDB( const std::string& name );
  void create_ItemCache( const std::string& name );
  void SQLite_GetItem( const std::string& name, struct ItemInfoDB* i );
  Items::Item* read_item_struct( struct ItemInfoDB* i );
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

  friend class StorageAreasImp;
  friend class StorageAreasIterator;
  friend void write_dirty_storage( Clib::StreamWriter& );

  sqlite3* SQLiteDB;
  std::string table_StorageArea = "StorageArea";
  std::string get_area_name( Clib::ConfigElem& elem );
  StorageArea* create_areaCache( const std::string& name );
  bool SQLite_ExistInStorage( const std::string& name, const std::string& table_name );
  bool SQLite_ExistInStorage( const u32 serial, const std::string& table_name );
  bool SQLite_RemoveItem( const std::string& name );
  bool SQLite_AddItem( Items::Item* item, const std::string& areaName );
  bool SQLite_AddCProp( Items::Item* item, const int last_rowid );
  int SQLite_GetIdArea( const std::string& name );
  int SQLite_Last_Rowid();
  void SQLite_Connect();
  void SQLite_ListStorageAreas();
  void SQLite_finish( sqlite3_stmt*& stmt, int x = 1 );
  void SQLite_AddStorageArea( const std::string& name );
  void SQLite_PrepareCProp( Items::Item* item, std::map<std::string, std::string>& allproperties );
  void query_value( std::string& q, const std::string& v, bool last = false );
  ItemInfoDB* iteminfo;
  void SQLite_GetItem( const std::string& name, struct ItemInfoDB* i );
};
}  // namespace Core
}  // namespace Pol
#endif
