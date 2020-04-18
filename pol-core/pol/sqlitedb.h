/** @file
 *
 * @par History
 */


#ifndef H_SQLITEDB_H
#define H_SQLITEDB_H

#include <map>
#include <string>
#include <vector>

#include "../clib/maputil.h"
#include <sqlite/sqlite3.h>

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
class StorageArea;
class Storage;

class SQLiteDB
{
public:
  SQLiteDB();
  ~SQLiteDB();

  sqlite3* db = nullptr;
  std::string prefix_table = "storage_";
  std::string table_Item = "Item";
  std::string table_CProp = "CProp";
  std::string table_StorageArea = "StorageArea";
  std::string dbpath = "data/database.db";
  std::map<Items::Item*, std::string> modified_storage;
  std::vector<u32> deleted_storage;
  std::vector<u32> all_storage_serials;

  bool ExistInStorage( const std::string& name, const std::string& table_name );
  bool ExistInStorage( const u32 serial, const std::string& table_name );
  bool RemoveItem( const std::string& name );
  bool RemoveItem( const u32 serial );
  bool AddItem( Items::Item* item, const std::string& areaName, const u32 container_serial = 0 );
  bool AddCProp( Items::Item* item );
  bool RemoveCProp( const int Serial );
  bool UpdateItem( Items::Item* item, const std::string& areaName );
  bool CreateDatabase();
  bool ExistDB();

  int GetIdArea( const std::string& name );

  void Connect();
  void Close();
  void ListStorageAreas();
  void Finish( sqlite3_stmt*& stmt, int x = 1 );
  void AddStorageArea( const std::string& name );
  void GetCProp( const int Serial, std::map<std::string, std::string>& CProps );
  void PrepareCProp( Items::Item* item, std::map<std::string, std::string>& allproperties );
  void PrepareItemInfo( sqlite3_stmt* stmt, struct ItemInfoDB* i );
  bool CanAddItemInfo( const u32 serial, std::vector<ItemInfoDB> ItemsInContainer );
  void query_value( std::string& q, const std::string& v, bool last = false );
  void query_value2( std::string& query, const std::string& column_name, const std::string& new_value, bool last = false );
  void GetItem( const std::string& name, struct ItemInfoDB* i );
  int GetItems( const u32 container_serial, std::vector<ItemInfoDB>& ItemsInContainer, std::vector<u32>& ItemsInfoSerial );

  void insert_root_item( Items::Item* item, const std::string& areaName );
  void insert_item( Items::Item* item, const std::string& areaName, const u32 container_serial );

  void UpdateDataStorage();
  void DeleteDataStorage();
  void BeginTransaction();
  void EndTransaction();
  void RollbackTransaction();
  void ListAllStorageItems();
  void remove_from_list(std::vector<u32>& vec, u32 serial);
  void find_deleted_storage_items();
  void EscapeSequence( std::string& value );
  void PrepareQueryGetItems( sqlite3_stmt*& stmt, int params );
  void DropIndexes();
  void CreateIndexes();

  Items::Item* read_item( const std::string& name );
  Items::Item* create_item_ref( struct ItemInfoDB* i, std::map<std::string, std::string>& CProps );
  std::map<Items::Item*, u32> read_items_in_container( const u32 container_serial );

private:
};

}  // namespace Core
}  // namespace Pol
#endif
