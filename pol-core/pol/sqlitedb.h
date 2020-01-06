/** @file
 *
 * @par History
 */


#ifndef H_SQLITEDB_H
#define H_SQLITEDB_H

#include <map>
#include <string>

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

  sqlite3* db = NULL;
  std::string prefix_table = "storage_";
  std::string table_Item = "Item";
  std::string table_StorageArea = "StorageArea";

  bool ExistInStorage( const std::string& name, const std::string& table_name );
  bool ExistInStorage( const u32 serial, const std::string& table_name );
  bool RemoveItem( const std::string& name );
  bool RemoveItem( const u32 serial );
  bool AddItem( Items::Item* item, const std::string& areaName, const u32 container_serial = 0 );
  bool AddCProp( Items::Item* item, const int last_rowid );
  bool RemoveCProp( const int ItemId );
  bool UpdateItem( Items::Item* item, const std::string& areaName );
  bool CreateDatabase( const std::string& dbpath );

  int GetIdArea( const std::string& name );
  int GetItemId( const std::string& name );
  int Last_Rowid();

  void Connect();
  void Close();
  void ListStorageAreas();
  void Finish( sqlite3_stmt*& stmt, int x = 1 );
  void AddStorageArea( const std::string& name );
  void PrepareCProp( Items::Item* item, std::map<std::string, std::string>& allproperties );
  void query_value( std::string& q, const std::string& v, bool last = false );
  void query_value2( std::string& query, const std::string& column_name, const std::string& new_value, bool last = false );
  void GetItem( const std::string& name, struct ItemInfoDB* i );

  void insert_root_item( Items::Item* item, const std::string& areaName );
  void insert_item( Items::Item* item, const std::string& areaName, const u32 container_serial );
  bool check_and_add_root_item( Items::Item* item, const std::string& areaName );

  void UpdateDataStorage( std::map<Items::Item*, std::string> modified_storage );
  void DeleteDataStorage();

  Items::Item* read_item( const std::string& name );
  Items::Item* create_item_ref( struct ItemInfoDB* i );

private:
};

}  // namespace Core
}  // namespace Pol
#endif
