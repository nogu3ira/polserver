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

  Items::Item* read_itemInDB( const std::string& name );
  void create_ItemCache( const std::string& name );
  Items::Item* read_item_struct( struct ItemInfoDB* i );
};

class Storage
{
public:
  StorageArea* find_area( const std::string& name );
  StorageArea* create_area( const std::string& name );
  StorageArea* create_area( Clib::ConfigElem& elem );
  std::string get_area_name( Clib::ConfigElem& elem );
  void on_delete_realm( Realms::Realm* realm );

  void print( Clib::StreamWriter& sw ) const;
  void read( Clib::ConfigFile& cf );
  void clear();
  size_t estimateSize() const;

  StorageArea* create_areaCache( const std::string& name );

private:
  // TODO: investigate if this could store objects. Does find()
  // return object copies, or references?
  typedef std::map<std::string, StorageArea*> AreaCont;
  AreaCont areas;

  friend class StorageAreasImp;
  friend class StorageAreasIterator;
  friend void write_dirty_storage( Clib::StreamWriter& );

};

class SQLiteDB
{
public:
  sqlite3* db = NULL;
  std::string table_Item = "Item";
  std::string table_StorageArea = "StorageArea";

  bool ExistInStorage( const std::string& name, const std::string& table_name );
  bool ExistInStorage( const u32 serial, const std::string& table_name );
  bool RemoveItem( const std::string& name );
  bool AddItem( Items::Item* item, const std::string& areaName );
  bool AddCProp( Items::Item* item, const int last_rowid );

  int GetIdArea( const std::string& name );
  int Last_Rowid();

  void Connect();
  void ListStorageAreas();
  void Finish( sqlite3_stmt*& stmt, int x = 1 );
  void AddStorageArea( const std::string& name );
  void PrepareCProp( Items::Item* item,
                                   std::map<std::string, std::string>& allproperties );
  void query_value( std::string& q, const std::string& v, bool last = false );
  ItemInfoDB* iteminfo;
  void GetItem( const std::string& name, struct ItemInfoDB* i );

private:
};

}  // namespace Core
}  // namespace Pol
#endif
