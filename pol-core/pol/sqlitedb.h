/** @file
 *
 * @par History
 */


#ifndef H_SQLITEDB_H
#define H_SQLITEDB_H

#include <iterator>
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
class PreparePrint;
class vecPreparePrint;
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
  std::string storage_Area = "storage_Area";
  std::string storage_Item = "storage_Item";
  std::string storage_Prop = "storage_Prop";
  std::string dbpath = "data/database.db";

  sqlite3_stmt* stmt_ExistInStorage_AreaName;
  sqlite3_stmt* stmt_ExistInStorage_ItemName;
  sqlite3_stmt* stmt_ExistInStorage_ItemSerial;
  sqlite3_stmt* stmt_AddStorageArea;
  sqlite3_stmt* stmt_AddStorageItem;
  sqlite3_stmt* stmt_AddStorageProp;
  std::vector<std::map<std::string, std::string>> columns_AddStorageItem;
  std::vector<std::map<std::string, std::string>> columns_AddStorageProp;

  std::multimap<std::string, Clib::PreparePrint> modified_storage;
  std::vector<u32> deleted_storage;
  std::vector<u32> all_storage_serials;

  bool prepare( std::string sqlquery, sqlite3_stmt*& stmt );
  void bind( int order, u32 serial_item, sqlite3_stmt*& stmt );
  void bind( int order, std::string text, sqlite3_stmt*& stmt );
  void bind( int order, sqlite3_stmt*& stmt );
  void bind_properties( std::vector<std::map<std::string, std::string>>& columns,
                        std::map<std::string, std::string> properties, sqlite3_stmt*& stmt );
  void StartPrepStmt();
  bool start_stmt_ExistInStorage_Name( const std::string table_name, sqlite3_stmt*& stmt );
  bool start_stmt_ExistInStorage_Serial( const std::string table_name, sqlite3_stmt*& stmt );
  bool start_stmt_AddStorageArea();
  bool start_stmt_AddStorage( std::vector<std::map<std::string, std::string>>& columns,
                              std::string table_name, sqlite3_stmt*& stmt );

  bool ExistInStorage( const std::string& name, sqlite3_stmt*& stmt );
  bool ExistInStorage( const u32 serial, sqlite3_stmt*& stmt );
  bool RemoveItem( const std::string& name );
  bool RemoveItem( const u32 serial );
  bool ExecuteInsertItem( Clib::PreparePrint& pp );
  void AppendAreaId( Clib::PreparePrint& pp, const std::string& areaName );
  bool AddItem( Clib::PreparePrint& pp, const std::string& areaName );
  bool AddItem( Items::Item* item, const std::string& areaName, const u32 container_serial = 0 );
  bool AddMain( std::map<std::string, std::string> main );
  bool AddProp( const u32 serial_item, std::multimap<std::string, std::string> props,
                bool isCProp );
  bool AddCProp( const u32 serial_item, std::map<std::string, std::string> props, bool isCProp );
  bool RemoveProps( const int Serial );
  bool ExecuteUpdateItem( Clib::PreparePrint& pp );
  bool UpdateItem( Clib::PreparePrint& pp, const std::string& areaName );
  bool UpdateItem( Items::Item* item, const std::string& areaName );
  bool CreateDatabase();
  bool ExistDB();

  int GetMaxStorageItemSerial();
  int GetIdArea( const std::string& name );
  std::string GetNameArea( const std::string id );

  void load_toplevel_owner( const u32 serial );
  void Connect();
  void Close();
  void ListStorageAreas();
  void Finish( sqlite3_stmt*& stmt, bool x = true );
  void AddStorageArea( const std::string& name );
  void GetProps( std::string Serial, std::map<std::string, std::string>& unusual,
                 std::map<std::string, std::string>& cprops );
  std::string UnEscapeSequence( std::string value ) const;
  void PrepareItemInfo( sqlite3_stmt*& stmt, std::map<std::string, std::string>& main );
  bool CanAddItemInfo( const u32 serial,
                       std::vector<std::map<std::string, std::string>> ItemsInContainer );
  bool query_execute( sqlite3_stmt*& stmt );
  void GetItem( const std::string name, std::map<std::string, std::string>& main );
  void GetItem( const u32 serial, std::map<std::string, std::string>& main );
  int GetItems( const u32 container_serial,
                std::vector<std::map<std::string, std::string>>& ItemsInContainer,
                std::vector<u32>& ItemsInfoSerial );

  void insert_root_item( Items::Item* item, const std::string& areaName );
  void insert_item( Items::Item* item, const std::string& areaName, const u32 container_serial );

  void UpdateDataStorage();
  void DeleteDataStorage();
  void PragmaSettings();
  void PragmaImport();
  void SetCurrentStorageItemSerial();
  void BeginTransaction();
  void EndTransaction();
  void RollbackTransaction();
  void ListAllStorageItems();
  void remove_from_list( std::vector<u32>& vec, u32 serial );
  void find_modified_storage_items( Clib::PreparePrint& pp, std::string areaName );
  void find_deleted_storage_items();
  void PrepareQueryGetItems( sqlite3_stmt*& stmt, int params );
  void DropIndexes();
  void CreateIndexes();

  void item_up( std::string areaName, std::map<std::string, std::string> main,
                std::map<std::string, std::string> unusual,
                std::map<std::string, std::string> cprops );
  u32 read_item( const std::string& name );
  void read_items_in_container( const u32 container_serial );

private:
};

}  // namespace Core
}  // namespace Pol
#endif
