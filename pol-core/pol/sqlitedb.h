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
  std::atomic<bool> is_import;
  std::string dbpath;

  // Tables
  std::string t_storage_area = "storage_area";
  std::string t_storage_main = "storage_main";
  std::string t_storage_prop = "storage_prop";
  std::string t_pcs_main = "pcs_main";
  std::string t_pcs_prop = "pcs_prop";
  std::string t_pcequip_main = "pcequip_main";
  std::string t_pcequip_prop = "pcequip_prop";

  // Database name
  std::string dbname = "database";

  // select array of all serials
  sqlite3_stmt* stmt_list_all_storage;
  sqlite3_stmt* stmt_list_all_pcs;
  sqlite3_stmt* stmt_list_all_pcequip;

  // select all by name
  sqlite3_stmt* stmt_select_storage_main_name;

  // select all by serial
  sqlite3_stmt* stmt_select_storage_main;
  sqlite3_stmt* stmt_select_storage_prop;
  sqlite3_stmt* stmt_select_pcs_main;
  sqlite3_stmt* stmt_select_pcs_prop;
  sqlite3_stmt* stmt_select_pcequip_main;
  sqlite3_stmt* stmt_select_pcequip_prop;

  // Select chr_serial by account and idx
  sqlite3_stmt* stmt_select_serial_pcs_prop;

  // Insert by area
  sqlite3_stmt* stmt_insert_storage_area;

  // Insert by serial
  sqlite3_stmt* stmt_insert_storage_main;
  sqlite3_stmt* stmt_insert_storage_prop;
  sqlite3_stmt* stmt_insert_pcs_main;
  sqlite3_stmt* stmt_insert_pcs_prop;
  sqlite3_stmt* stmt_insert_pcequip_main;
  sqlite3_stmt* stmt_insert_pcequip_prop;

  // Update main by serial
  sqlite3_stmt* stmt_update_storage_main;
  sqlite3_stmt* stmt_update_pcs_main;
  sqlite3_stmt* stmt_update_pcequip_main;

  // Delete by serial
  sqlite3_stmt* stmt_delete_storage_main;
  sqlite3_stmt* stmt_delete_storage_prop;
  sqlite3_stmt* stmt_delete_pcs_main;
  sqlite3_stmt* stmt_delete_pcs_prop;
  sqlite3_stmt* stmt_delete_pcequip_main;
  sqlite3_stmt* stmt_delete_pcequip_prop;

  // Select exist
  sqlite3_stmt* stmt_exist_storage_area;            // by area
  sqlite3_stmt* stmt_exist_storage_main_name;       // by name
  sqlite3_stmt* stmt_exist_storage_main_container;  // by container

  // Select exist by serial
  sqlite3_stmt* stmt_exist_storage_main;
  sqlite3_stmt* stmt_exist_pcs_main;
  sqlite3_stmt* stmt_exist_pcequip_main;

  // table columns
  std::vector<std::map<std::string, std::string>> columns_main_storage;  // works only to storage
  std::vector<std::map<std::string, std::string>> columns_main;          // works to pcs and pcequip
  std::vector<std::map<std::string, std::string>> columns_prop;  // works pcs, pcequip and storage

  // map with all serials
  std::map<u32, bool> all_storage_serials;
  std::map<u32, bool> all_pcs_serials;
  std::map<u32, bool> all_pcequip_serials;

  std::multimap<std::string, Clib::PreparePrint> modified_storage;
  std::vector<Clib::PreparePrint> modified_pcs;
  std::vector<Clib::PreparePrint> modified_pcequip;

  std::vector<u32> deleted_storage;
  std::vector<u32> deleted_pcs;
  std::vector<u32> deleted_pcequip;

  std::map<u32, std::vector<u32>> containers_opened;

  bool prepare( std::string sqlquery, sqlite3_stmt*& stmt );
  void bind( int order, u32 serial_item, sqlite3_stmt*& stmt );
  void bind( int order, std::string text, sqlite3_stmt*& stmt );
  void bind( int order, sqlite3_stmt*& stmt );
  void bind_properties( std::vector<std::map<std::string, std::string>>& columns,
                        std::map<std::string, std::string> properties, sqlite3_stmt*& stmt );
  void StartPrepStmt();
  bool start_stmt_pcs_prop_get_chrserial( sqlite3_stmt*& stmt );
  bool start_stmt_ListAll( const std::string& table_name, sqlite3_stmt*& stmt );
  bool start_stmt_Exist_Name( const std::string table_name, sqlite3_stmt*& stmt );
  bool start_stmt_Exist_Serial( const std::string table_name, sqlite3_stmt*& stmt );
  bool start_stmt_Exist_ContainerSerial( const std::string table_name, sqlite3_stmt*& stmt );
  bool start_stmt_AddStorageArea();
  bool start_stmt_Add( std::string table_name,
                       std::vector<std::map<std::string, std::string>>& columns,
                       sqlite3_stmt*& stmt );
  bool start_stmt_Update( std::string table_name,
                          std::vector<std::map<std::string, std::string>>& columns,
                          sqlite3_stmt*& stmt );
  bool start_stmt_GetItem_Name( const std::string table_name, sqlite3_stmt*& stmt );
  bool start_stmt_GetItem_Serial( const std::string table_name, sqlite3_stmt*& stmt );
  bool start_stmt_GetProps_Serial( const std::string table_name, sqlite3_stmt*& stmt );
  bool start_stmt_Remove( const std::string table_name, sqlite3_stmt*& stmt );

  u32 pcs_prop_get_chrserial( const std::string& acctname, const std::string& idx,
                              sqlite3_stmt*& stmt );
  bool Exist( const std::string& name, sqlite3_stmt*& stmt );
  bool Exist( const u32 serial, sqlite3_stmt*& stmt );
  bool RemoveItem( const std::string& name );
  bool RemoveItem( const u32 serial, sqlite3_stmt*& stmt );
  bool ExecuteInsertObj( Clib::PreparePrint& pp, sqlite3_stmt*& stmtmain, sqlite3_stmt*& stmtprop,
                         std::vector<std::map<std::string, std::string>>& columns );
  bool ExecuteUpdateItem( Clib::PreparePrint& pp, sqlite3_stmt*& stmtmain, sqlite3_stmt*& stmtprop,
                          sqlite3_stmt*& stmtprop_remove,
                          std::vector<std::map<std::string, std::string>>& columns );
  void AppendAreaId( Clib::PreparePrint& pp, const std::string& areaName );
  bool AddStorageItem( Clib::PreparePrint& pp, const std::string& areaName );
  bool AddStorageItem( Items::Item* item, const std::string& areaName,
                       const u32 container_serial = 0 );
  bool AddMain( std::map<std::string, std::string>& main, sqlite3_stmt*& stmt,
                std::vector<std::map<std::string, std::string>>& columns );
  bool UpdateMain( std::map<std::string, std::string>& main, sqlite3_stmt*& stmt,
                   std::vector<std::map<std::string, std::string>>& columns );
  bool AddProp( const u32& serial_item, std::multimap<std::string, std::string>& props,
                bool isCProp, sqlite3_stmt*& stmt );
  bool AddCProp( const u32& serial_item, std::map<std::string, std::string>& props, bool isCProp,
                 sqlite3_stmt*& stmt );
  bool RemoveProps( const int serial, sqlite3_stmt*& stmt );
  bool UpdatepcsObj( Clib::PreparePrint& pp );
  bool UpdatepcequipObj( Clib::PreparePrint& pp );
  bool UpdateItem( Clib::PreparePrint& pp, const std::string& areaName );
  bool UpdateItem( Items::Item* item, const std::string& areaName );
  bool CreateDatabase();
  bool ExistDB();

  bool AddpcequipObj( Clib::PreparePrint& pp );
  bool AddpcsObj( Clib::PreparePrint& pp );
  bool AddpcsObj( Mobile::Character* chr );
  bool AddObj( Items::Item* item, const u32 container_serial, sqlite3_stmt*& stmt,
               sqlite3_stmt*& stmtprop, std::vector<std::map<std::string, std::string>>& columns );

  int GetMaxStorageItemSerial();
  int GetMaxpcsCharSerial();
  int GetMaxpcsItemSerial();
  int GetMaxpcequipItemSerial();
  int GetIdArea( const std::string& name );
  std::string GetNameArea( const std::string id );

  void Prop_RowsToColumns( std::vector<std::string>& PropNames );
  void Prop_CastInteger( std::string& filters );
  bool GetItemCustomFilter( std::string filters, std::vector<u32>& serials,
                            const std::string areaName, std::string& err_msg );

  void load_storage_toplevel_owner( const u32 serial );
  void load_pcs_toplevel_owner( const u32 serial );
  void load_pcequip_toplevel_owner( const u32 serial );
  void Connect();
  void Close();
  void ListStorageAreas();
  void Finish( sqlite3_stmt*& stmt, bool x = true );
  void AddStorageArea( const std::string& name );
  void GetProps( const u32& serial, std::map<std::string, std::string>& unusual,
                 std::map<std::string, std::string>& cprops, sqlite3_stmt*& stmt );
  std::string UnEscape( std::string value ) const;
  void PrepareMainInfo( std::vector<std::map<std::string, std::string>>& columns,
                        std::map<std::string, std::string>& main, sqlite3_stmt*& stmt );
  bool CanAddItemInfo( const u32 serial,
                       std::vector<std::map<std::string, std::string>> ItemsInContainer );
  bool query_execute( sqlite3_stmt*& stmt );
  void GetItem( const std::string& name, std::map<std::string, std::string>& main,
                std::vector<std::map<std::string, std::string>>& columns, sqlite3_stmt*& stmt );
  void GetItem( const u32& serial, std::map<std::string, std::string>& main,
                std::vector<std::map<std::string, std::string>>& columns, sqlite3_stmt*& stmt );
  int GetItems( const u32& container_serial,
                std::vector<std::map<std::string, std::string>>& ItemsInContainer,
                std::vector<u32>& ItemsInfoSerial, const std::string& table_name,
                std::vector<std::map<std::string, std::string>>& columns );

  void insert_root_item( Items::Item* item, const std::string& areaName );
  void insert_item( Items::Item* item, const std::string& areaName, const u32 container_serial );
  void insert_root_chr( Mobile::Character* chr );
  void insert_item( Items::Item* item, const u32 container_serial, int txt_flag );

  void UpdateDataStorage();
  void UpdateDataPCs();
  void UpdateDataPCEquip();
  void DeleteData( std::vector<u32>& deleted, sqlite3_stmt*& stmt );
  void PragmaSettings();
  void PragmaImport();
  void SetCurrentStorageItemSerial();
  void SetCurrentpcsCharSerial();
  void SetCurrentpcsItemSerial();
  void SetCurrentpcequipItemSerial();
  void BeginTransaction();
  void EndTransaction();
  void RollbackTransaction();
  void ListAll( std::map<u32, bool>& all, sqlite3_stmt*& stmt );
  void remove_from_list( const u32& serial, std::map<u32, bool>& all );
  void find_modified_item( Clib::PreparePrint& pp, const std::string& areaName );
  void find_modified_item( Clib::PreparePrint& pp, std::vector<Clib::PreparePrint>& modified,
                           std::vector<u32>& deleted );
  void find_deleted_items( std::map<u32, bool>& all, std::vector<u32>& deleted );
  void PrepareQueryGetItems( sqlite3_stmt*& stmt, int params, const std::string table_name );
  void DropIndexes();
  void CreateIndexes();

  void item_up( std::string areaName, std::map<std::string, std::string> main,
                std::map<std::string, std::string> unusual,
                std::map<std::string, std::string> cprops );
  void item_up( std::map<std::string, std::string> main, std::map<std::string, std::string> unusual,
                std::map<std::string, std::string> cprops );
  void chr_up( std::map<std::string, std::string> main, std::map<std::string, std::string> unusual,
               std::map<std::string, std::string> cprops );
  u32 read_item( const std::string& name );
  void read_chr( const u32& serial );
  void read_items_in_container( const u32 container_serial );
  void read_chr_items( const u32& container_serial, const std::string& table_name,
                       sqlite3_stmt*& stmt, std::vector<u32>& root_ItemsInfoSerial );
  void add_container_opened( const u32 item, const u32 chr );
  void CheckUnusedRootItem();
  void RemoveObjectHash( const u32 serial );
  void load_chr_and_items( const u32& serial );
  void commit_storage();
  void commit_pcs();
  void commit_pcequip();
  bool find_serial( const u32& serial );

private:
};

}  // namespace Core
}  // namespace Pol
#endif
