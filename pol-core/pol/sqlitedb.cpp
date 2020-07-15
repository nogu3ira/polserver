/** @file sqlitedb.cpp made by Nix (ChaosAge/Mystic)
 *
 * @par History
 * TODO, test this functions with SQLite database

 * create a new os method?
 * FindItemsInStorageArea( [Storage Area Reference] area, [struct with desired information] item )
 * return array of Ref Items. Before, check if item already load in memory.
 * If no, load item. If yes, skip item.
 */


#include "sqlitedb.h"

#include <algorithm>
#include <exception>
#include <string>
#include <time.h>
#include <utility>

#include "../bscript/berror.h"
#include "../bscript/bobject.h"
#include "../bscript/contiter.h"
#include "../bscript/impstr.h"
#include "../clib/cfgelem.h"
#include "../clib/cfgfile.h"
#include "../clib/clib.h"
#include "../clib/fileutil.h"
#include "../clib/logfacility.h"
#include "../clib/rawtypes.h"
#include "../clib/streamsaver.h"
#include "../plib/poltype.h"
#include "../plib/systemstate.h"
#include "containr.h"
#include "fnsearch.h"
#include "globals/object_storage.h"
#include "globals/uvars.h"
#include "item/item.h"
#include "loaddata.h"
#include "mkscrobj.h"
#include "mobile/charactr.h"
#include "objtype.h"
#include "polcfg.h"
#include "ufunc.h"
#include "uimport.h"
#include <sqlite/sqlite3.h>
#include <boost/algorithm/string/replace.hpp>

namespace Pol
{
namespace Core
{
using namespace Bscript;
using namespace std;
using namespace boost;

SQLiteDB::SQLiteDB()
{
}

SQLiteDB::~SQLiteDB()
{
  Close();
}

// Insert root item only in SQLite Database. Don't load item in memory.
void SQLiteDB::insert_root_item( Items::Item* item, const string& areaName )
{
  if ( Plib::systemstate.config.enable_sqlite )
  {
    if ( !AddStorageItem( item, areaName ) )
    {
      INFO_PRINT_TRACE( 1 ) << "insert_root_item: no added in BD.\n";
      return;
    }
    INFO_PRINT_TRACE( 1 ) << "insert_root_item: yes added in BD.\n";
  }
}

// Insert root chr only in SQLite Database. Don't load item in memory.
void SQLiteDB::insert_root_chr( Mobile::Character* chr )
{
  if ( Plib::systemstate.config.enable_sqlite )
  {
    if ( !AddpcsObj( chr ) )
    {
      INFO_PRINT_TRACE( 1 ) << "insert_root_chr: no added in BD.\n";
      return;
    }
    INFO_PRINT_TRACE( 1 ) << "insert_root_chr: yes added in BD.\n";
  }
}

// Insert item only in pcs and pcequip SQLite Database. Don't load item in memory.
void SQLiteDB::insert_item( Items::Item* item, const u32 container_serial, int txt_flag )
{
  if ( Plib::systemstate.config.enable_sqlite )
  {
    if ( txt_flag == 1 )  // pcs.txt file
    {
      if ( !AddObj( item, container_serial, stmt_insert_pcs_main, stmt_insert_pcs_prop,
                    columns_main ) )
      {
        INFO_PRINT_TRACE( 1 ) << "insert_item(pcs): no added in BD.\n";
        return;
      }
      INFO_PRINT_TRACE( 1 ) << "insert_item(pcs): yes added in BD.\n";
    }
    else if ( txt_flag == 2 )  // pcequip.txt file
    {
      if ( !AddObj( item, container_serial, stmt_insert_pcequip_main, stmt_insert_pcequip_prop,
                    columns_main ) )
      {
        INFO_PRINT_TRACE( 1 ) << "insert_item(pcequip): no added in BD.\n";
        return;
      }
      INFO_PRINT_TRACE( 1 ) << "insert_item(pcequip): yes added in BD.\n";
    }
  }
}

// Insert item only in storage SQLite Database. Don't load item in memory.
void SQLiteDB::insert_item( Items::Item* item, const string& areaName, const u32 container_serial )
{
  if ( Plib::systemstate.config.enable_sqlite )
  {
    if ( !AddStorageItem( item, areaName, container_serial ) )
    {
      INFO_PRINT_TRACE( 1 ) << "insert_item: no added in BD.\n";
      return;
    }
    INFO_PRINT_TRACE( 1 ) << "insert_item: yes added in BD.\n";
  }
}

// Convert map format to elem format
// then, read_character() into memory
void SQLiteDB::chr_up( map<string, string> main, map<string, string> unusual,
                       map<string, string> cprops )
{
  Clib::ConfigElem elem;
  Clib::ConfigFile cf;
  cf.PropsToConfigElem( elem, main, unusual, cprops );

  try
  {
    Core::read_character( elem );
  }
  catch ( std::exception& )
  {
    if ( !Plib::systemstate.config.ignore_load_errors )
      throw;
  }
}

// Read one chr from SQLite Database
void SQLiteDB::read_chr( const u32& serial )
{
  //clock_t start = clock();
  map<string, string> main, unusual, cprops;

  GetItem( serial, main, columns_main, stmt_select_pcs_main );
  GetProps( serial, unusual, cprops, stmt_select_pcs_prop );
  chr_up( main, unusual, cprops );

  //clock_t end = clock();
  //int ms = static_cast<int>( ( end - start ) * 1000.0 / CLOCKS_PER_SEC );
  //INFO_PRINT << " 1 chr in " << ms << " ms.\n";
}

// Convert map format to elem format
// then, read_global_item() into memory
void SQLiteDB::item_up( map<string, string> main, map<string, string> unusual,
                        map<string, string> cprops )
{
  Clib::ConfigElem elem;
  Clib::ConfigFile cf;
  cf.PropsToConfigElem( elem, main, unusual, cprops );

  try
  {
    Core::read_global_item( elem, SYSFIND_SKIP_WORLD );
  }
  catch ( std::exception& )
  {
    if ( !Plib::systemstate.config.ignore_load_errors )
      throw;
  }
}

// Convert map format to elem format
// then, load_item() into memory
void SQLiteDB::item_up( string areaName, map<string, string> main, map<string, string> unusual,
                        map<string, string> cprops )
{
  Clib::ConfigElem elem;
  Clib::ConfigFile cf;
  cf.PropsToConfigElem( elem, main, unusual, cprops );
  StorageArea* area = gamestate.storage.find_area( areaName );

  if ( area != nullptr )
  {
    try
    {
      area->load_item( elem );
    }
    catch ( std::exception& )
    {
      if ( !Plib::systemstate.config.ignore_load_errors )
        throw;
    }
  }
  else
  {
    ERROR_PRINT << "Storage (SQLiteDB::item_up): Got an ITEM element, but don't have a "
                   "StorageArea to put it.\n";
    throw runtime_error( "Data file integrity error" );
  }
}

// Read one item from storage SQLite Database
u32 SQLiteDB::read_item( const string& name )
{
  //clock_t start = clock();
  map<string, string> main, unusual, cprops;

  BeginTransaction();
  GetItem( name, main, columns_main_storage, stmt_select_storage_main_name );
  u32 serial = lexical_cast<u32>( main["Serial"] );
  GetProps( serial, unusual, cprops, stmt_select_storage_prop );
  string areaName = GetNameArea( main["AreaId"] );
  EndTransaction();
  item_up( areaName, main, unusual, cprops );

  //clock_t end = clock();
  //int ms = static_cast<int>( ( end - start ) * 1000.0 / CLOCKS_PER_SEC );
  //INFO_PRINT << " 1 item in " << ms << " ms.\n";

  return serial;
}

// Read items in container from storage SQLite Database
void SQLiteDB::read_items_in_container( const u32 container_serial )
{
  //clock_t start = clock();
  auto& columns = columns_main_storage;
  unsigned int nobjects = 0;
  vector<map<string, string>> ItemsInfo_main;
  vector<u32> ItemsInfoSerial;

  BeginTransaction();
  // list contents of root containers
  int found_item =
      GetItems( container_serial, ItemsInfo_main, ItemsInfoSerial, t_storage_main, columns );

  INFO_PRINT_TRACE( 1 ) << "read_items_in_container: found_item = " << found_item << "\n";

  // list contents of sub-containers
  while ( found_item > 0 )
  {
    found_item = GetItems( 0, ItemsInfo_main, ItemsInfoSerial, t_storage_main, columns );
    INFO_PRINT_TRACE( 1 ) << "read_items_in_container: while found_item = " << found_item << "\n";
  }

  for ( auto main : ItemsInfo_main )
  {
    map<string, string> unusual, cprops;
    GetProps( lexical_cast<u32>( main["Serial"] ), unusual, cprops, stmt_select_storage_prop );
    string areaName = GetNameArea( main["AreaId"] );
    item_up( areaName, main, unusual, cprops );
    ++nobjects;
  }
  EndTransaction();

  //clock_t end = clock();
  //int ms = static_cast<int>( ( end - start ) * 1000.0 / CLOCKS_PER_SEC );
  //INFO_PRINT << " " << nobjects << " items in " << ms << " ms.\n";
}

// Read items in chr from SQLite Database
void SQLiteDB::read_chr_items( const u32& container_serial, const string& table_name,
                               sqlite3_stmt*& stmt, vector<u32>& root_ItemsInfoSerial )
{
  //clock_t start = clock();
  auto& columns = columns_main;
  unsigned int nobjects = 0;
  vector<map<string, string>> ItemsInfo_main;
  vector<u32> ItemsInfoSerial;

  // list contents of root containers
  int found_item =
      GetItems( container_serial, ItemsInfo_main, ItemsInfoSerial, table_name, columns );
  root_ItemsInfoSerial = ItemsInfoSerial;

  INFO_PRINT_TRACE( 1 ) << "read_chr_items: found_item = " << found_item << "\n";

  // list contents of sub-containers
  while ( found_item > 0 )
  {
    found_item = GetItems( 0, ItemsInfo_main, ItemsInfoSerial, table_name, columns );
    INFO_PRINT_TRACE( 1 ) << "read_chr_items: while found_item = " << found_item << "\n";
  }

  for ( auto main : ItemsInfo_main )
  {
    map<string, string> unusual, cprops;
    GetProps( lexical_cast<u32>( main["Serial"] ), unusual, cprops, stmt );
    item_up( main, unusual, cprops );
    ++nobjects;
  }

  //clock_t end = clock();
  //int ms = static_cast<int>( ( end - start ) * 1000.0 / CLOCKS_PER_SEC );
  //INFO_PRINT << " " << nobjects << " items in " << ms << " ms.\n";
}

bool SQLiteDB::ExistDB()
{
  dbpath = Plib::systemstate.config.world_data_path + dbname + ".db";
  if ( Clib::FileExists( dbpath ) )
    return true;

  return false;
}

void SQLiteDB::Connect()
{
  if ( Plib::systemstate.config.enable_sqlite )
  {
    if ( !ExistDB() )
    {
      if ( !CreateDatabase() )
        throw runtime_error( "Storage: Can't create database " + dbpath );
    }

    int rc = sqlite3_open( dbpath.c_str(), &db );
    if ( rc )
    {
      ERROR_PRINT << "Storage: Can't open database: " << sqlite3_errmsg( db )
                  << ".\n";
      throw runtime_error( "Storage: Can't open database " + dbpath );
    }
    PragmaSettings();
    INFO_PRINT << "\nSQLite database connected!\n";
    StartPrepStmt();

	// Set max serial for each table (storage, pcs and pcequip)
    SetCurrentStorageItemSerial();
    SetCurrentpcsCharSerial();
    SetCurrentpcsItemSerial();
    SetCurrentpcequipItemSerial();

	// Create a list of all serials for each table (storage, pcs and pcequip)
	ListAll( all_storage_serials, stmt_list_all_storage );
    ListAll( all_pcs_serials, stmt_list_all_pcs );
    ListAll( all_pcequip_serials, stmt_list_all_pcequip );
  }
}

void SQLiteDB::Finish( sqlite3_stmt*& stmt, bool x )
{
  if ( x )
  {
    ERROR_PRINT << "Storage: " << sqlite3_errmsg( db ) << ".\n";
  }
  sqlite3_finalize( stmt );
}

void SQLiteDB::Close()
{
  if ( Plib::systemstate.config.enable_sqlite )
    sqlite3_close( db );
}

// Find chr serial in pcs_prop db
bool SQLiteDB::start_stmt_pcs_prop_get_chrserial( sqlite3_stmt*& stmt )
{
  string sqlquery = "SELECT Serial FROM ";
  sqlquery += t_pcs_prop;
  sqlquery += " WHERE Serial IN (SELECT Serial FROM ";
  sqlquery += t_pcs_prop;
  sqlquery += " WHERE PropName = 'Account' AND PropValue = ? AND CProp = 0) AND ";
  sqlquery += "(PropName = 'CharIdx' AND PropValue = ? AND CProp = 0)";
  return prepare( sqlquery, stmt );
}

// AccountName and CharIdx
u32 SQLiteDB::pcs_prop_get_chrserial( const string& acctname, const string& idx, sqlite3_stmt*& stmt )
{
  u32 result = 0;
  int rc = 0;

  bind( 1, acctname, stmt );
  bind( 2, idx, stmt );

  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
    result = sqlite3_column_int( stmt, 0 );

  if ( rc != SQLITE_DONE )
    Finish( stmt );

  sqlite3_reset( stmt );
  return result;
}

bool SQLiteDB::start_stmt_Exist_Name( const string table_name, sqlite3_stmt*& stmt )
{
  string sqlquery = "SELECT EXISTS(SELECT 1 FROM ";
  sqlquery += table_name;
  sqlquery += " WHERE Name = ? LIMIT 1 ) AS result";
  return prepare( sqlquery, stmt );
}

bool SQLiteDB::Exist( const string& name, sqlite3_stmt*& stmt )
{
  // Works to FindStorageArea and FindRootItemInStorageArea
  int result = 0;
  int rc = 0;

  bind( 1, name, stmt );

  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
    result = sqlite3_column_int( stmt, 0 );

  if ( rc != SQLITE_DONE )
    Finish( stmt );

  sqlite3_reset( stmt );
  return ( result == 1 );
}

bool SQLiteDB::start_stmt_Exist_ContainerSerial( const string table_name,
                                                          sqlite3_stmt*& stmt )
{
  string sqlquery = "SELECT EXISTS(SELECT 1 FROM ";
  sqlquery += table_name;
  sqlquery += " WHERE Container = ? LIMIT 1 ) AS result";
  return prepare( sqlquery, stmt );
}

bool SQLiteDB::start_stmt_Exist_Serial( const string table_name, sqlite3_stmt*& stmt )
{
  string sqlquery = "SELECT EXISTS(SELECT 1 FROM ";
  sqlquery += table_name;
  sqlquery += " WHERE Serial = ? LIMIT 1 ) AS result";
  return prepare( sqlquery, stmt );
}

bool SQLiteDB::Exist( const u32 serial, sqlite3_stmt*& stmt )
{
  // Works to FindStorageArea and FindRootItemInStorageArea
  int result = 0;
  int rc = 0;

  bind( 1, serial, stmt );

  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
    result = sqlite3_column_int( stmt, 0 );

  if ( rc != SQLITE_DONE )
    Finish( stmt );

  sqlite3_reset( stmt );
  return ( result == 1 );
}

void SQLiteDB::load_storage_toplevel_owner( const u32 serial )
{
  map<string, string> main;
  u32 container = 0;
  auto& col = columns_main_storage;
  auto& stmt = stmt_select_storage_main;

  BeginTransaction();
  GetItem( serial, main, col, stmt );
  while ( main.find( "Container" ) != main.end() )
  {
    container = lexical_cast<u32>( main["Container"] );
    main.clear();
    GetItem( container, main, col, stmt );
  }

  if ( main.find( "Name" ) == main.end() )
  {
    ERROR_PRINT << "load_toplevel_owner(): ERROR! Name not found in root_item: " << main["Serial"]
                << "\n";
    return;
  }

  string areaName = GetNameArea( main["AreaId"] );
  StorageArea* area = gamestate.storage.find_area( areaName );
  EndTransaction();
  area->find_root_item( main["Name"] );
}

void SQLiteDB::load_pcs_toplevel_owner( const u32 serial )
{
  map<string, string> main;
  u32 container = 0;
  auto& col = columns_main;
  auto& stmt = stmt_select_pcs_main;

  BeginTransaction();
  GetItem( serial, main, col, stmt );
  while ( main.find( "Container" ) != main.end() )
  {
    container = lexical_cast<u32>( main["Container"] );
    main.clear();
    GetItem( container, main, col, stmt );
  }
  EndTransaction();
  load_chr_and_items( lexical_cast<u32>( main["Serial"] ) );
}

void SQLiteDB::load_pcequip_toplevel_owner( const u32 serial )
{
  map<string, string> main;
  u32 container = 0;
  auto& col = columns_main;
  auto& stmt = stmt_select_pcequip_main;

  BeginTransaction();
  GetItem( serial, main, col, stmt );
  while ( main.find( "Container" ) != main.end() )
  {
    container = lexical_cast<u32>( main["Container"] );
    main.clear();
    GetItem( container, main, col, stmt );
  }
  EndTransaction();
  load_pcs_toplevel_owner( container );
}

void SQLiteDB::ListStorageAreas()
{
  string sqlquery = "SELECT Name FROM StorageArea";

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( db, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    Finish( stmt );
    return;
  }
  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
  {
    string Name = UnEscape( string( reinterpret_cast<const char*>( sqlite3_column_text( stmt, 0 ) ) ) );
    gamestate.storage.create_area( Name );
  }
  if ( rc != SQLITE_DONE )
  {
    Finish( stmt );
    return;
  }
  Finish( stmt, false );
}

bool SQLiteDB::prepare( string sqlquery, sqlite3_stmt*& stmt )
{
  int rc = sqlite3_prepare_v2( db, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    ERROR_PRINT << "prepare(): " << sqlquery << "\n";
    Finish( stmt );
    return false;
  }
  return true;
}

// Order, Serial and stmt
void SQLiteDB::bind( int order, u32 serial_item, sqlite3_stmt*& stmt )
{
  sqlite3_bind_int( stmt, order, serial_item );
}

// Order, Text and stmt
void SQLiteDB::bind( int order, string text, sqlite3_stmt*& stmt )
{
  sqlite3_bind_text( stmt, order, UnEscape( text ).c_str(), -1, SQLITE_TRANSIENT );
}

void SQLiteDB::bind( int order, sqlite3_stmt*& stmt )
{
  sqlite3_bind_null( stmt, order );
}

bool SQLiteDB::start_stmt_AddStorageArea()
{
  string sqlquery = "INSERT INTO storage_area (Name) VALUES(?)";
  return prepare( sqlquery, stmt_insert_storage_area );
}

void SQLiteDB::AddStorageArea( const string& name )
{
  bind( 1, name, stmt_insert_storage_area );
  if ( !query_execute( stmt_insert_storage_area ) )
    ERROR_PRINT << "Storage: No StorageArea inserted. Name: " << name << "\n";
}

int SQLiteDB::GetMaxStorageItemSerial()
{
  int MaxSerial = 0;
  string sqlquery = "SELECT MAX(Serial) FROM storage_main";

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( db, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    Finish( stmt );
    return 0;
  }
  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
  {
    MaxSerial = sqlite3_column_int( stmt, 0 );
  }
  if ( rc != SQLITE_DONE )
  {
    Finish( stmt );
    return 0;
  }
  Finish( stmt, false );
  INFO_PRINT_TRACE( 1 ) << "GetMaxStorageItemSerial(): " << MaxSerial << " in BD.\n";
  return MaxSerial;
}

int SQLiteDB::GetMaxpcsCharSerial()
{
  int MaxSerial = 0;
  string sqlquery = "SELECT MAX(Serial) FROM pcs_main WHERE Container IS NULL";

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( db, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    Finish( stmt );
    return 0;
  }
  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
  {
    MaxSerial = sqlite3_column_int( stmt, 0 );
  }
  if ( rc != SQLITE_DONE )
  {
    Finish( stmt );
    return 0;
  }
  Finish( stmt, false );
  INFO_PRINT_TRACE( 1 ) << "GetMaxpcsCharSerial(): " << MaxSerial << " in BD.\n";
  return MaxSerial;
}

int SQLiteDB::GetMaxpcsItemSerial()
{
  int MaxSerial = 0;
  string sqlquery = "SELECT MAX(Serial) FROM pcs_main WHERE Container IS NOT NULL";

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( db, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    Finish( stmt );
    return 0;
  }
  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
  {
    MaxSerial = sqlite3_column_int( stmt, 0 );
  }
  if ( rc != SQLITE_DONE )
  {
    Finish( stmt );
    return 0;
  }
  Finish( stmt, false );
  INFO_PRINT_TRACE( 1 ) << "GetMaxpcsItemSerial(): " << MaxSerial << " in BD.\n";
  return MaxSerial;
}

int SQLiteDB::GetMaxpcequipItemSerial()
{
  int MaxSerial = 0;
  string sqlquery = "SELECT MAX(Serial) FROM pcequip_main";

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( db, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    Finish( stmt );
    return 0;
  }
  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
  {
    MaxSerial = sqlite3_column_int( stmt, 0 );
  }
  if ( rc != SQLITE_DONE )
  {
    Finish( stmt );
    return 0;
  }
  Finish( stmt, false );
  INFO_PRINT_TRACE( 1 ) << "GetMaxpcequipItemSerial(): " << MaxSerial << " in BD.\n";
  return MaxSerial;
}

int SQLiteDB::GetIdArea( const string& name )
{
  int AreaId = 0;
  string sqlquery = "SELECT AreaId FROM storage_area WHERE Name='";
  sqlquery += name;
  sqlquery += "'";

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( db, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    Finish( stmt );
    return 0;
  }
  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
  {
    AreaId = sqlite3_column_int( stmt, 0 );
  }
  if ( rc != SQLITE_DONE )
  {
    Finish( stmt );
    return 0;
  }
  Finish( stmt, false );
  return AreaId;
}

string SQLiteDB::GetNameArea( const string id )
{
  string Name = "";
  string sqlquery = "SELECT Name FROM storage_area WHERE AreaId = ";
  sqlquery += id;

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( db, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    Finish( stmt );
    return 0;
  }
  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
  {
    Name = UnEscape( lexical_cast<string>( sqlite3_column_text( stmt, 0 ) ) );
  }
  if ( rc != SQLITE_DONE )
  {
    Finish( stmt );
    return 0;
  }
  Finish( stmt, false );
  return Name;
}

void SQLiteDB::Prop_RowsToColumns( vector<string>& PropNames )
{
  string sqlquery = "SELECT PropName FROM storage_prop WHERE CProp = 0 GROUP BY PropName";

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( db, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    Finish( stmt );
    return;
  }
  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
  {
    if ( sqlite3_column_type( stmt, 0 ) != SQLITE_NULL )
      PropNames.emplace_back( lexical_cast<string>( sqlite3_column_text( stmt, 0 ) ) );
  }
  if ( rc != SQLITE_DONE )
  {
    Finish( stmt );
    return;
  }
  Finish( stmt, false );
}

void SQLiteDB::Prop_CastInteger( string& filters )
{
  // Check INTEGER value of properties.
  // Scripter may indicate using int(value) if that value need to be read as integer or float/double
  // Example:
  // Name = 'Shiny scythe' AND Amount >= int(12345)
  // Objtype <> int(0x5EE06BC1) OR Quality = int(1.104989);

  size_t s, e;
  while ( ( s = filters.find( "int(" ) ) != string::npos &&
          ( e = filters.find( ")", s ) ) != string::npos )
  {
    string sub = filters.substr( s + 4, e - s - 4 );
    string f_s = filters.substr( 0, s );
    string f_e = filters.substr( e + 1 );
    string type_cast = "INTEGER";

    if ( sub.find( "0x" ) != string::npos )
      sub = to_string( stol( sub, nullptr, 0 ) );

    if ( sub.find( "." ) != string::npos )
    {
      sub = to_string( stold( sub ) );
      type_cast = "FLOAT";
    }

    filters = f_s + "CAST(" + sub + " AS " + type_cast + ")" + f_e;
  }
}

bool SQLiteDB::GetItemCustomFilter( string filters, vector<u32>& serials, const string areaName,
                                    string& err_msg )
{
  auto AreaId = lexical_cast<string>( GetIdArea( areaName ) );

  string sqlquery =
      "SELECT Serial FROM storage_main AS m JOIN "
      "(SELECT t.Serial AS Serial_Prop, ";

  vector<string> PropNames;
  Prop_RowsToColumns( PropNames );

  for ( const auto& unusual : PropNames )
  {
    sqlquery += "MAX(CASE WHEN t.PropName = '" + unusual +
                "' AND t.CProp = 0 THEN t.PropValue END) AS " + unusual + ",";
  }
  sqlquery.pop_back();  // Remove last character ',' from string

  sqlquery +=
      " FROM storage_prop AS t GROUP BY t.Serial) AS p "
      "ON m.Serial = p.Serial_Prop WHERE AreaId = ";
  sqlquery += AreaId;
  sqlquery += " AND ";

  Prop_CastInteger( filters );

  sqlquery += filters;

  INFO_PRINT_TRACE( 1 ) << "GetItemCustomFilter: " << sqlquery << "\n";

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( db, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    ERROR_PRINT << "GetItem: some problem with prepare query.\n";
    err_msg = sqlite3_errmsg( db );
    Finish( stmt );
    return false;
  }

  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
    serials.emplace_back( sqlite3_column_int( stmt, 0 ) );

  if ( rc != SQLITE_DONE )
  {
    ERROR_PRINT << "GetItem: some problem in query.\n";
    err_msg = sqlite3_errmsg( db );
    Finish( stmt );
    return false;
  }

  Finish( stmt, false );
  INFO_PRINT_TRACE( 1 ) << "GetItemCustomFilter: OK.\n";
  return true;
}

bool SQLiteDB::start_stmt_GetItem_Name( const string table_name, sqlite3_stmt*& stmt )
{
  string sqlquery = "SELECT * FROM ";
  sqlquery += table_name;
  sqlquery += " WHERE Name = ? LIMIT 1";
  return prepare( sqlquery, stmt );
}

void SQLiteDB::GetItem( const string& name, map<string, string>& main,
                        vector<map<string, string>>& columns, sqlite3_stmt*& stmt )
{
  int rc = 0;

  bind( 1, name, stmt );
  INFO_PRINT_TRACE( 1 ) << "GetItem(): " << sqlite3_expanded_sql( stmt ) << "\n";

  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
    PrepareMainInfo( columns, main, stmt );

  if ( rc != SQLITE_DONE )
    Finish( stmt );

  sqlite3_reset( stmt );
}

bool SQLiteDB::start_stmt_GetItem_Serial( const string table_name, sqlite3_stmt*& stmt )
{
  string sqlquery = "SELECT * FROM ";
  sqlquery += table_name;
  sqlquery += " WHERE Serial = ? LIMIT 1";
  return prepare( sqlquery, stmt );
}

void SQLiteDB::GetItem( const u32& serial, map<string, string>& main,
                        vector<map<string, string>>& columns, sqlite3_stmt*& stmt )
{
  int rc = 0;

  bind( 1, serial, stmt );
  INFO_PRINT_TRACE( 1 ) << "GetItem(): " << sqlite3_expanded_sql( stmt ) << "\n";

  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
    PrepareMainInfo( columns, main, stmt );

  if ( rc != SQLITE_DONE )
    Finish( stmt );

  sqlite3_reset( stmt );
}

void SQLiteDB::PrepareQueryGetItems( sqlite3_stmt*& stmt, int params, const string table_name )
{
  string sqlquery = "SELECT * FROM ";
  sqlquery += table_name;
  sqlquery += " WHERE Container = ? ";

  if ( params > 1 )
  {
    for ( unsigned i = 1; i <= params; ++i )
    {
      sqlquery += " OR Container = ?";
    }
  }

  INFO_PRINT_TRACE( 1 ) << "PrepareQueryGetItems: sqlquery = " << sqlquery << ".\n";

  int rc = sqlite3_prepare_v2( db, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    ERROR_PRINT << "GetItems: some problem with prepare query.\n";
    Finish( stmt );
  }
}

int SQLiteDB::GetItems( const u32& container_serial, vector<map<string, string>>& ItemsInContainer,
                        vector<u32>& ItemsInfoSerial, const string& table_name,
                        vector<map<string, string>>& columns )
{
  INFO_PRINT_TRACE( 1 ) << "GetItems: start method.\n";

  int count = 0;
  int rc = 0;
  sqlite3_stmt* stmt;

  // first search
  if ( container_serial != 0 )
  {
    PrepareQueryGetItems( stmt, 1, table_name );
    sqlite3_bind_int( stmt, 1, container_serial );
    INFO_PRINT_TRACE( 1 ) << "GetItems: first search Container = " << container_serial << ".\n";
  }
  // search contents of sub-containers
  else
  {
    int params = static_cast<int>( ItemsInfoSerial.size() );
    PrepareQueryGetItems( stmt, params, table_name );
    for ( unsigned i = 1; i <= params; ++i )
    {
      // bind index starts with 1
      // vector ItemsInfoSerial index starts with 0
      sqlite3_bind_int( stmt, i, ItemsInfoSerial[i - 1] );
      INFO_PRINT_TRACE( 1 ) << "GetItems: for (...) Container = " << ItemsInfoSerial[i - 1]
                            << ".\n";
    }
  }

  ItemsInfoSerial.clear();

  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
  {
    INFO_PRINT_TRACE( 1 ) << "GetItems: insde while.\n";
    map<string, string> main;
    PrepareMainInfo( columns, main, stmt );
    u32 main_serial = lexical_cast<u32>( main["Serial"] );

    // if no duplicate, go ahead.
    if ( CanAddItemInfo( main_serial, ItemsInContainer ) )
    {
      ItemsInContainer.emplace_back( main );
      ItemsInfoSerial.emplace_back( main_serial );
      INFO_PRINT_TRACE( 1 ) << "GetItems: CanAddItemInfo() Serial = " << main["Serial"] << ".\n";
      count = 1;
    }
  }
  if ( rc != SQLITE_DONE )
  {
    ERROR_PRINT << "GetItems: some problem in query.\n";
    Finish( stmt );
    return count;
  }
  Finish( stmt, false );
  INFO_PRINT_TRACE( 1 ) << "GetItems: OK.\n";
  return count;
}

bool SQLiteDB::CanAddItemInfo( const u32 serial, vector<map<string, string>> ItemsInContainer )
{
  for ( auto iteminfo : ItemsInContainer )
  {
    if ( serial == lexical_cast<u32>( iteminfo["Serial"] ) )
    {
      INFO_PRINT_TRACE( 1 ) << "CanAddItemInfo: item is already added into vector. SERIAL: "
                            << serial << "\n";
      return false;
    }
  }
  return true;
}

string SQLiteDB::UnEscape( string value ) const
{
  boost::replace_all( value, "\\\"", "" );
  boost::replace_all( value, "\'", "" );
  return value;
}

void SQLiteDB::PrepareMainInfo( vector<map<string, string>>& columns, map<string, string>& main,
                                sqlite3_stmt*& stmt )
{
  int i = 0;
  for ( const auto& name_type : columns )
  {
    for ( const auto& c : name_type )
    {
      if ( c.second == "TEXT" )
      {
        if ( sqlite3_column_type( stmt, i ) != SQLITE_NULL )
        {
          main.insert(
              make_pair( c.first, UnEscape( lexical_cast<string>( sqlite3_column_text( stmt, i ) ) ) ) );
        }
      }
      else if ( c.second == "INTEGER" )
      {
        if ( sqlite3_column_type( stmt, i ) != SQLITE_NULL )
        {
          main.insert(
              make_pair( c.first, lexical_cast<string>( sqlite3_column_int( stmt, i ) ) ) );
        }
      }
      ++i;
    }
  }
}

bool SQLiteDB::RemoveItem( const string& name )
{
  string sqlquery = "DELETE FROM storage_main WHERE Name = '";
  sqlquery += name;
  sqlquery += "'";

  sqlite3_stmt* s;
  prepare( sqlquery, s );

  if ( !query_execute( s ) )
  {
    ERROR_PRINT << "Storage: No data deleted. Name: " << name << "\n";
    return false;
  }
  Finish( s, false );
  return true;
}

bool SQLiteDB::RemoveItem( const u32 serial, sqlite3_stmt*& stmt )
{
  bind( 1, serial, stmt );

  if ( !query_execute( stmt ) )
  {
    ERROR_PRINT << "RemoveItem: No Item deleted. Serial: " << serial << "\n";
    return false;
  }
  return true;
}

bool SQLiteDB::ExecuteUpdateItem( Clib::PreparePrint& pp, sqlite3_stmt*& stmtmain,
                                  sqlite3_stmt*& stmtprop, sqlite3_stmt*& stmtprop_remove,
                                  vector<map<string, string>>& columns )
{
  u32 serial = lexical_cast<u32>( pp.main["Serial"] );
  // basic (main) properties
  if ( !UpdateMain( pp.main, stmtmain, columns ) )
  {
    ERROR_PRINT << "Storage: No main prop updated. Serial: " << serial << "\n";
    RollbackTransaction();
    throw runtime_error( "Data file integrity error" );
    return false;
  }

  RemoveProps( serial, stmtprop_remove );

  // unusual properties
  if ( !AddProp( serial, pp.unusual, false, stmtprop ) )
  {
    ERROR_PRINT << "Storage: No Unusual Prop inserted. Serial: " << serial << "\n";
    RollbackTransaction();
    throw runtime_error( "Data file integrity error" );
    return false;
  }
  // cprop
  if ( !AddCProp( serial, pp.cprop, true, stmtprop ) )
  {
    ERROR_PRINT << "Storage: No CProp inserted. Serial: " << serial << "\n";
    RollbackTransaction();
    throw runtime_error( "Data file integrity error" );
    return false;
  }

  return true;
}

// table pcs
bool SQLiteDB::UpdatepcsObj( Clib::PreparePrint& pp )
{
  return ExecuteUpdateItem( pp, stmt_update_pcs_main, stmt_insert_pcs_prop, stmt_delete_pcs_prop,
                            columns_main );
}

// table pcequip
bool SQLiteDB::UpdatepcequipObj( Clib::PreparePrint& pp )
{
  return ExecuteUpdateItem( pp, stmt_update_pcequip_main, stmt_insert_pcequip_prop,
                            stmt_delete_pcequip_prop, columns_main );
}

// table storage
bool SQLiteDB::UpdateItem( Clib::PreparePrint& pp, const string& areaName )
{
  auto& stmtmain = stmt_update_storage_main;
  auto& stmtprop = stmt_insert_storage_prop;
  auto& stmtprop_remove = stmt_delete_storage_prop;
  auto& columns = columns_main_storage;

  AppendAreaId( pp, areaName );
  return ExecuteUpdateItem( pp, stmtmain, stmtprop, stmtprop_remove, columns );
}

// table storage
bool SQLiteDB::UpdateItem( Items::Item* item, const string& areaName )
{
  auto& stmtmain = stmt_update_storage_main;
  auto& stmtprop = stmt_insert_storage_prop;
  auto& stmtprop_remove = stmt_delete_storage_prop;
  auto& columns = columns_main_storage;

  Clib::PreparePrint pp;
  AppendAreaId( pp, areaName );
  item->printProperties( pp );
  return ExecuteUpdateItem( pp, stmtmain, stmtprop, stmtprop_remove, columns );
}

bool SQLiteDB::start_stmt_GetProps_Serial( const string table_name, sqlite3_stmt*& stmt )
{
  string sqlquery = "SELECT PropName, PropValue, CProp FROM ";
  sqlquery += table_name;
  sqlquery += " WHERE Serial = ?";
  return prepare( sqlquery, stmt );
}

void SQLiteDB::GetProps( const u32& serial, map<string, string>& unusual,
                         map<string, string>& cprops, sqlite3_stmt*& stmt )
{
  int rc = 0;

  bind( 1, serial, stmt );

  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
  {
    if ( ( sqlite3_column_type( stmt, 0 ) != SQLITE_NULL ) &&
         ( sqlite3_column_type( stmt, 1 ) != SQLITE_NULL ) )
    {
      auto PropName = string( reinterpret_cast<const char*>( sqlite3_column_text( stmt, 0 ) ) );
      auto PropValue =
          UnEscape( string( reinterpret_cast<const char*>( sqlite3_column_text( stmt, 1 ) ) ) );
      auto isCProp = sqlite3_column_int( stmt, 2 );

      if ( isCProp == 1 )
        cprops.insert( make_pair( PropName, PropValue ) );
      else
        unusual.insert( make_pair( PropName, PropValue ) );
    }
  }

  if ( rc != SQLITE_DONE )
    Finish( stmt );

  sqlite3_reset( stmt );
}

bool SQLiteDB::start_stmt_Remove( const string table_name, sqlite3_stmt*& stmt )
{
  string sqlquery = "DELETE FROM ";
  sqlquery += table_name;
  sqlquery += " WHERE Serial = ?";
  return prepare( sqlquery, stmt );
}

bool SQLiteDB::RemoveProps( const int serial, sqlite3_stmt*& stmt )
{
  bind( 1, serial, stmt );

  if ( !query_execute( stmt ) )
  {
    ERROR_PRINT << "RemoveProps: No Prop deleted. Serial: " << serial << "\n";
    return false;
  }
  return true;
}

// unusual prop
bool SQLiteDB::AddProp( const u32& serial_item, multimap<string, string>& props, bool isCProp,
                        sqlite3_stmt*& stmt )
{
  if ( props.empty() )
    return true;

  for ( const auto& kv : props )
  {
    sqlite3_bind_int( stmt, 1, serial_item );                               // Serial
    sqlite3_bind_text( stmt, 2, kv.first.c_str(), -1, SQLITE_TRANSIENT );   // PropName
    sqlite3_bind_text( stmt, 3, UnEscape( kv.second ).c_str(), -1, SQLITE_TRANSIENT );  // PropValue
    sqlite3_bind_int( stmt, 4, isCProp );                                   // CProp boolean

    if ( !query_execute( stmt ) )
      return false;
  }
  return true;
}

// cprop
bool SQLiteDB::AddCProp( const u32& serial_item, map<string, string>& props, bool isCProp,
                         sqlite3_stmt*& stmt )
{
  if ( props.empty() )
    return true;

  for ( const auto& kv : props )
  {
    sqlite3_bind_int( stmt, 1, serial_item );                               // Serial
    sqlite3_bind_text( stmt, 2, kv.first.c_str(), -1, SQLITE_TRANSIENT );   // PropName
    sqlite3_bind_text( stmt, 3, UnEscape( kv.second ).c_str(), -1, SQLITE_TRANSIENT );  // PropValue
    sqlite3_bind_int( stmt, 4, isCProp );                                   // CProp boolean

    if ( !query_execute( stmt ) )
      return false;
  }
  return true;
}

bool SQLiteDB::AddMain( map<string, string>& main, sqlite3_stmt*& stmt,
                        vector<map<string, string>>& columns )
{
  if ( main.find( "Container" ) == main.end() )
    main.insert( make_pair( "Container", "" ) );

  if ( main.find( "Name" ) == main.end() )
    main.insert( make_pair( "Name", "" ) );

  // bind main prop
  bind_properties( columns, main, stmt );

  if ( !query_execute( stmt ) )
    return false;

  return true;
}

bool SQLiteDB::start_stmt_Update( string table_name, vector<map<string, string>>& columns,
                               sqlite3_stmt*& stmt )
{
  columns.clear();
  // Get all column_name
  string sqlquery = "PRAGMA table_info(";
  sqlquery += table_name;
  sqlquery += ")";
  prepare( sqlquery, stmt );

  sqlquery = "UPDATE ";
  sqlquery += table_name;
  sqlquery += " SET";

  while ( ( sqlite3_step( stmt ) ) == SQLITE_ROW )
  {
    auto ColumnName = lexical_cast<string>( sqlite3_column_text( stmt, 1 ) );
    auto ColumnType = lexical_cast<string>( sqlite3_column_text( stmt, 2 ) );
    sqlquery += " ";
	sqlquery += ColumnName;  // Column name
    sqlquery += " = ?,";
    columns.push_back( {make_pair( ColumnName, ColumnType )} );
  }
  sqlquery.pop_back();  // Remove last character ',' from string
  sqlquery += " WHERE Serial = ?";

  return prepare( sqlquery, stmt );
}

bool SQLiteDB::UpdateMain( map<string, string>& main, sqlite3_stmt*& stmt,
                        vector<map<string, string>>& columns )
{
  if ( main.find( "Container" ) == main.end() )
    main.insert( make_pair( "Container", "" ) );

  if ( main.find( "Name" ) == main.end() )
    main.insert( make_pair( "Name", "" ) );

  // bind main prop
  bind_properties( columns, main, stmt );
  // Add bind to (WHERE Serial = ?)
  int order = int( columns.size() ) + 1;
  bind( order, lexical_cast<int>( main["Serial"] ), stmt );

  if ( !query_execute( stmt ) )
    return false;

  return true;
}

bool SQLiteDB::ExecuteInsertObj( Clib::PreparePrint& pp, sqlite3_stmt*& stmtmain,
                                 sqlite3_stmt*& stmtprop, vector<map<string, string>>& columns )
{
  // basic (main) properties
  if ( !AddMain( pp.main, stmtmain, columns ) )
  {
    ERROR_PRINT << "Storage: No Main Prop inserted.\n";
    RollbackTransaction();
    throw runtime_error( "Data file integrity error" );
    return false;
  }

  auto serial = lexical_cast<u32>( pp.main["Serial"] );
  // unusual properties
  if ( !AddProp( serial, pp.unusual, false, stmtprop ) )
  {
    ERROR_PRINT << "Storage: No Unusual Prop inserted.\n";
    RollbackTransaction();
    throw runtime_error( "Data file integrity error" );
    return false;
  }
  // cprop
  if ( !AddCProp( serial, pp.cprop, true, stmtprop ) )
  {
    ERROR_PRINT << "Storage: No CProp inserted.\n";
    RollbackTransaction();
    throw runtime_error( "Data file integrity error" );
    return false;
  }
  return true;
}

void SQLiteDB::AppendAreaId( Clib::PreparePrint& pp, const string& areaName )
{
  auto AreaId = lexical_cast<string>( GetIdArea( areaName ) );
  pp.main.insert( make_pair( "AreaId", AreaId ) );
}

bool SQLiteDB::AddStorageItem( Clib::PreparePrint& pp, const string& areaName )
{
  AppendAreaId( pp, areaName );
  return ExecuteInsertObj( pp, stmt_insert_storage_main, stmt_insert_storage_prop,
                           columns_main_storage );
}

bool SQLiteDB::AddStorageItem( Items::Item* item, const string& areaName,
                               const u32 container_serial )
{
  Clib::PreparePrint pp;
  AppendAreaId( pp, areaName );
  item->printProperties( pp );
  if ( container_serial != 0 )
    pp.main["Container"] = lexical_cast<string>( container_serial );

  return ExecuteInsertObj( pp, stmt_insert_storage_main, stmt_insert_storage_prop,
                           columns_main_storage );
}

bool SQLiteDB::AddpcequipObj( Clib::PreparePrint& pp )
{
  return ExecuteInsertObj( pp, stmt_insert_pcequip_main, stmt_insert_pcequip_prop, columns_main );
}

bool SQLiteDB::AddpcsObj( Clib::PreparePrint& pp )
{
  return ExecuteInsertObj( pp, stmt_insert_pcs_main, stmt_insert_pcs_prop, columns_main );
}

bool SQLiteDB::AddpcsObj( Mobile::Character* chr )
{
  Clib::PreparePrint pp;
  chr->printProperties( pp );

  return ExecuteInsertObj( pp, stmt_insert_pcs_main, stmt_insert_pcs_prop, columns_main );
}

// Works with pcs.txt and pcequip.txt
bool SQLiteDB::AddObj( Items::Item* item, const u32 container_serial, sqlite3_stmt*& stmt,
                       sqlite3_stmt*& stmtprop, vector<map<string, string>>& columns )
{
  Clib::PreparePrint pp;
  item->printProperties( pp );
  if ( container_serial != 0 )
    pp.main["Container"] = lexical_cast<string>( container_serial );

  return ExecuteInsertObj( pp, stmt, stmtprop, columns );
}

bool SQLiteDB::start_stmt_Add( string table_name, vector<map<string, string>>& columns, 
                               sqlite3_stmt*& stmt )
{
  columns.clear();
  // Get all column_name
  string sqlquery = "PRAGMA table_info(";
  sqlquery += table_name;
  sqlquery += ")";
  prepare( sqlquery, stmt );

  sqlquery = "INSERT INTO ";
  sqlquery += table_name;
  sqlquery += " (";

  int rows = 0;
  while ( ( sqlite3_step( stmt ) ) == SQLITE_ROW )
  {
    auto ColumnName = lexical_cast<string>( sqlite3_column_text( stmt, 1 ) );
    auto ColumnType = lexical_cast<string>( sqlite3_column_text( stmt, 2 ) );
    sqlquery += ColumnName;  // Column name
    sqlquery += ",";

    columns.push_back( {make_pair( ColumnName, ColumnType )} );
    ++rows;
  }
  sqlquery.pop_back();  // Remove last character ',' from string
  sqlquery += ")";
  sqlquery += " VALUES (";

  for ( int col = 0; col < rows; col++ )
  {
    sqlquery += "?,";  // value
  }
  sqlquery.pop_back();  // Remove last character ',' from string
  sqlquery += ")";

  return prepare( sqlquery, stmt );
}

void SQLiteDB::bind_properties( vector<map<string, string>>& columns,
                                map<string, string> properties, sqlite3_stmt*& stmt )
{
  int i = 1;
  for ( const auto& name_type : columns )
  {
    for ( const auto& c : name_type )
    {
      for ( const auto& p : properties )
      {
        if ( c.first == p.first )
        {
          if ( p.second.empty() && ( p.first == "Name" || p.first == "Container" ) )
          {
            bind( i, stmt );
            ++i;
          }
          else if ( c.second == "TEXT" )
          {
            bind( i, p.second, stmt );
            ++i;
          }
          else if ( c.second == "INTEGER" )
          {
            bind( i, lexical_cast<int>( p.second ), stmt );
            ++i;
          }
        }
      }
    }
  }
}

bool SQLiteDB::query_execute( sqlite3_stmt*& stmt )
{
  INFO_PRINT_TRACE( 1 ) << "\nquery_execute(): " << sqlite3_expanded_sql( stmt ) << "\n";
  int rc = sqlite3_step( stmt );
  if ( rc != SQLITE_DONE )
  {
    Finish( stmt );
    return false;
  }
  else if ( sqlite3_changes( db ) == 0 )
  {
    ERROR_PRINT << "Storage: No query executed.\n";
    Finish( stmt );
    return false;
  }
  sqlite3_reset( stmt );
  return true;
}

void SQLiteDB::UpdateDataStorage()
{
  int num_updated = 0;
  BeginTransaction();

  for ( const auto& it : modified_storage )
  {
    Clib::PreparePrint pp = it.second;
    // check if StorageArea exists
    // if not, create new StorageArea.
    if ( !Exist( it.first, stmt_exist_storage_area ) )
    {
      AddStorageArea( it.first );
      ++num_updated;
      INFO_PRINT_TRACE( 1 ) << "UpdateDataStorage(): StorageArea " << it.first
                            << " added AddStorageArea() in BD.\n";
    }

	u32 serial = lexical_cast<u32>( pp.main["Serial"] );
    // check if item exists
    // if not, add item.
    if ( !Exist( serial, stmt_exist_storage_main ) )
    {
      if ( !AddStorageItem( pp, it.first ) )
      {
        ERROR_PRINT << "UpdateDataStorage(): no added in BD.\n";
        RollbackTransaction();
        throw runtime_error( "Data file (Storage) integrity error on insert item" );
      }
      ++num_updated;
      INFO_PRINT_TRACE( 1 ) << "UpdateDataStorage(): item " << serial << " (" << fmt::hex( serial )
                            << ") added AddItem() in BD.\n";
    }
    else
    {
      // if yes, update item.
      if ( !UpdateItem( pp, it.first ) )
      {
        RollbackTransaction();
        throw runtime_error( "Data file (Storage) integrity error on update item" );
      }
      ++num_updated;
      INFO_PRINT_TRACE( 1 ) << "UpdateDataStorage(): item " << serial << " (" << fmt::hex( serial )
                            << ") updated UpdateItem() in BD.\n";
    }
  }
  EndTransaction();
  string sufix_plural = "";
  if ( num_updated > 1 )
    sufix_plural = "s";

  INFO_PRINT << num_updated << " item" << sufix_plural
             << " updated into SQLite database.\n";
}

// Update pcs database
void SQLiteDB::UpdateDataPCs()
{
  int num_updated = 0;
  BeginTransaction();

  for ( auto& pp : modified_pcs )
  {
    u32 serial = lexical_cast<u32>( pp.main["Serial"] );
    // check if item exists
    // if not, add item.
    if ( !Exist( serial, stmt_exist_pcs_main) )
    {
      if ( !AddpcsObj( pp ) )
      {
        ERROR_PRINT << "UpdateData(): no added in BD.\n";
        RollbackTransaction();
        throw runtime_error( "Data file (database) integrity error on insert item" );
      }
      ++num_updated;
      INFO_PRINT_TRACE( 1 ) << "UpdateDataPCs(): item " << serial << " (" << fmt::hex( serial ) << ")"
                            << " added AddpcsObj() in BD.\n";
    }
    else
    {
      // if yes, update item.
      if ( !UpdatepcsObj( pp ) )
      {
        RollbackTransaction();
        throw runtime_error( "Data file (database) integrity error on update item" );
      }
      ++num_updated;
      INFO_PRINT_TRACE( 1 ) << "UpdateDataPCs(): item " << serial << " (" << fmt::hex( serial ) << ")"
                            << " updated UpdatepcsObj() in BD.\n";
    }
  }
  EndTransaction();
  string sufix_plural = "";
  if ( num_updated > 1 )
    sufix_plural = "s";

  INFO_PRINT << num_updated << " item" << sufix_plural
             << " updated into SQLite database.\n";
}

// Update pcequip database
void SQLiteDB::UpdateDataPCEquip()
{
  int num_updated = 0;
  BeginTransaction();

  for ( auto& pp : modified_pcequip )
  {
    // check if item exists
    // if not, add item.
    if ( !Exist( lexical_cast<u32>( pp.main["Serial"] ), stmt_exist_pcequip_main ) )
    {
      if ( !AddpcequipObj( pp ) )
      {
        ERROR_PRINT << "UpdateData(): no added in BD.\n";
        RollbackTransaction();
        throw runtime_error( "Data file (database) integrity error on insert item" );
      }
      ++num_updated;
      INFO_PRINT_TRACE( 1 ) << "UpdateDataPCEquip(): item " << pp.main["Serial"] << " ("
                            << fmt::hex( lexical_cast<u32>( pp.main["Serial"] ) ) << ")"
                            << " added AddpcequipObj() in BD.\n";
    }
    else
    {
      // if yes, update item.
      if ( !UpdatepcequipObj( pp ) )
      {
        RollbackTransaction();
        throw runtime_error( "Data file (database) integrity error on update item" );
      }
      ++num_updated;
      INFO_PRINT_TRACE( 1 ) << "UpdateDataPCEquip(): item " << pp.main["Serial"] << " ("
                            << fmt::hex( lexical_cast<u32>( pp.main["Serial"] ) ) << ")"
                            << " updated UpdatepcequipObj() in BD.\n";
    }
  }
  EndTransaction();
  string sufix_plural = "";
  if ( num_updated > 1 )
    sufix_plural = "s";

  INFO_PRINT << num_updated << " item" << sufix_plural
             << " updated into SQLite database.\n";
}

void SQLiteDB::DeleteData( vector<u32>& deleted, sqlite3_stmt*& stmt )
{
  int num_removed = 0;
  BeginTransaction();
  for ( unsigned i = 0; i < deleted.size(); ++i )
  {
    if ( !RemoveItem( deleted[i], stmt ) )
    {
      RollbackTransaction();
      throw runtime_error( "Data file (Storage) integrity error on remove item" );
    }
    ++num_removed;
  }
  EndTransaction();
  string sufix_plural = "";
  if ( num_removed > 1 )
    sufix_plural = "s";

  INFO_PRINT << num_removed << " item" << sufix_plural
             << " removed into SQLite database.\n";
}

bool SQLiteDB::CreateDatabase()
{
  INFO_PRINT << "\nSQLite enabled.\n";
  INFO_PRINT << "\n  data/database.db: NOT FOUND!\n";
  INFO_PRINT << "\nCreating the SQLite database... ";

  int rc = sqlite3_open( dbpath.c_str(), &db );
  if ( rc )
  {
    ERROR_PRINT << "\nSQLiteDB: Can't open database.db: " << sqlite3_errmsg( db )
                << ".\n";
    return false;
  }

  // storage.txt tables
  string sqlquery =
      "								\
BEGIN TRANSACTION;										\
CREATE TABLE IF NOT EXISTS 'pcs_main' (             \
	'Name'	TEXT DEFAULT NULL,                          \
	'Serial'	INTEGER NOT NULL UNIQUE,                \
	'Container'	INTEGER DEFAULT NULL,                   \
	'ObjType'	INTEGER NOT NULL,                       \
	'Graphic'	INTEGER NOT NULL,                       \
	'X'	INTEGER NOT NULL,                               \
	'Y'	INTEGER NOT NULL,                               \
	'Z'	INTEGER NOT NULL,                               \
	'Revision'	INTEGER NOT NULL,                       \
	'Realm'	TEXT NOT NULL                              \
);                                                      \
CREATE TABLE IF NOT EXISTS 'pcs_prop' (            \
	'Serial'	INTEGER NOT NULL,                       \
	'PropName'	TEXT,                                   \
	'PropValue'	TEXT,                                   \
	'CProp'	INTEGER DEFAULT 0,                          \
	FOREIGN KEY('Serial')                               \
	REFERENCES 'pcs_main'('Serial')                 \
	ON UPDATE CASCADE ON DELETE CASCADE                 \
);                                                      \
CREATE INDEX IF NOT EXISTS 'pcs_main_Name'          \
ON 'pcs_main' (                                     \
	'Name'	ASC                                         \
);                                                      \
CREATE INDEX IF NOT EXISTS 'pcs_main_Serial'          \
ON 'pcs_main' (                                     \
	'Serial'	ASC                                     \
);                                                      \
CREATE INDEX IF NOT EXISTS 'pcs_main_Container'      \
ON 'pcs_main' (                                     \
	'Container'	ASC                                     \
);                                                      \
CREATE INDEX IF NOT EXISTS 'pcs_prop_Serial'          \
ON 'pcs_prop' (                                     \
	'Serial'	ASC                                     \
);                                                      \
CREATE INDEX IF NOT EXISTS 'pcs_prop_PropName'          \
ON 'pcs_prop' (                                     \
	'PropName'	ASC                                     \
);                                                      \
CREATE TABLE IF NOT EXISTS 'pcequip_main' (             \
	'Name'	TEXT DEFAULT NULL,                          \
	'Serial'	INTEGER NOT NULL UNIQUE,                \
	'Container'	INTEGER DEFAULT NULL,                   \
	'ObjType'	INTEGER NOT NULL,                       \
	'Graphic'	INTEGER NOT NULL,                       \
	'X'	INTEGER NOT NULL,                               \
	'Y'	INTEGER NOT NULL,                               \
	'Z'	INTEGER NOT NULL,                               \
	'Revision'	INTEGER NOT NULL,                       \
	'Realm'	TEXT NOT NULL                              \
);                                                      \
CREATE TABLE IF NOT EXISTS 'pcequip_prop' (            \
	'Serial'	INTEGER NOT NULL,                       \
	'PropName'	TEXT,                                   \
	'PropValue'	TEXT,                                   \
	'CProp'	INTEGER DEFAULT 0,                          \
	FOREIGN KEY('Serial')                               \
	REFERENCES 'pcequip_main'('Serial')                 \
	ON UPDATE CASCADE ON DELETE CASCADE                 \
);                                                      \
CREATE INDEX IF NOT EXISTS 'pcequip_main_Name'          \
ON 'pcequip_main' (                                     \
	'Name'	ASC                                         \
);                                                      \
CREATE INDEX IF NOT EXISTS 'pcequip_main_Serial'          \
ON 'pcequip_main' (                                     \
	'Serial'	ASC                                     \
);                                                      \
CREATE INDEX IF NOT EXISTS 'pcequip_main_Container'      \
ON 'pcequip_main' (                                     \
	'Container'	ASC                                     \
);                                                      \
CREATE INDEX IF NOT EXISTS 'pcequip_prop_Serial'          \
ON 'pcequip_prop' (                                     \
	'Serial'	ASC                                     \
);                                                      \
CREATE INDEX IF NOT EXISTS 'pcequip_prop_PropName'      \
ON 'pcequip_prop' (                                     \
	'PropName'	ASC                                     \
);                                                      \
CREATE TABLE IF NOT EXISTS 'storage_main' (             \
	'AreaId'	INTEGER NOT NULL,                   \
	'Name'	TEXT DEFAULT NULL,                          \
	'Serial'	INTEGER NOT NULL UNIQUE,                \
	'Container'	INTEGER DEFAULT NULL,                   \
	'ObjType'	INTEGER NOT NULL,                       \
	'Graphic'	INTEGER NOT NULL,                       \
	'X'	INTEGER NOT NULL,                               \
	'Y'	INTEGER NOT NULL,                               \
	'Z'	INTEGER NOT NULL,                               \
	'Revision'	INTEGER NOT NULL,                       \
	'Realm'	TEXT NOT NULL,                              \
	FOREIGN KEY('AreaId')                        \
	REFERENCES 'storage_area'('AreaId')   \
	ON UPDATE CASCADE ON DELETE CASCADE                 \
);                                                      \
CREATE TABLE IF NOT EXISTS 'storage_area' (      \
	'AreaId'	INTEGER NOT NULL,                   \
	'Name'	TEXT NOT NULL UNIQUE,                       \
	PRIMARY KEY('AreaId')                        \
);                                                      \
CREATE TABLE IF NOT EXISTS 'storage_prop' (            \
	'Serial'	INTEGER NOT NULL,                       \
	'PropName'	TEXT,                                   \
	'PropValue'	TEXT,                                   \
	'CProp'	INTEGER DEFAULT 0,                          \
	FOREIGN KEY('Serial')                               \
	REFERENCES 'storage_main'('Serial')                 \
	ON UPDATE CASCADE ON DELETE CASCADE                 \
);                                                      \
CREATE INDEX IF NOT EXISTS 'storage_main_Name'          \
ON 'storage_main' (                                     \
	'Name'	ASC                                         \
);                                                      \
CREATE INDEX IF NOT EXISTS 'storage_main_Serial'          \
ON 'storage_main' (                                     \
	'Serial'	ASC                                     \
);                                                      \
CREATE INDEX IF NOT EXISTS 'storage_main_Container'      \
ON 'storage_main' (                                     \
	'Container'	ASC                                     \
);                                                      \
CREATE INDEX IF NOT EXISTS 'storage_prop_Serial'          \
ON 'storage_prop' (                                     \
	'Serial'	ASC                                     \
);                                                      \
CREATE INDEX IF NOT EXISTS 'storage_prop_PropName'      \
ON 'storage_prop' (                                     \
	'PropName'	ASC                                     \
);                                                      \
COMMIT;                                                 \
  ";

  char* msgError;
  rc = sqlite3_exec( db, sqlquery.c_str(), NULL, 0, &msgError );
  if ( rc != SQLITE_OK )
  {
    ERROR_PRINT << "Error Insert!\n";
    sqlite3_free( msgError );
    Close();
    return false;
  }

  Close();
  INFO_PRINT << "Done!\n";
  return true;
}

void SQLiteDB::PragmaSettings()
{
  if ( Plib::systemstate.config.enable_sqlite )
  {
    sqlite3_exec( db, "PRAGMA cache_size = 10000", NULL, NULL, NULL );
    sqlite3_exec( db, "PRAGMA foreign_keys = ON", NULL, NULL, NULL );
    sqlite3_exec( db, "PRAGMA journal_mode = DELETE", NULL, NULL, NULL );
    sqlite3_exec( db, "PRAGMA synchronous = FULL", NULL, NULL, NULL );
  }
}

void SQLiteDB::PragmaImport()
{
  if ( Plib::systemstate.config.enable_sqlite )
  {
    sqlite3_exec( db, "PRAGMA foreign_keys = OFF", NULL, NULL, NULL );
    sqlite3_exec( db, "PRAGMA journal_mode = WAL", NULL, NULL, NULL );
    sqlite3_exec( db, "PRAGMA synchronous = NORMAL", NULL, NULL, NULL );
  }
}

// Ensures that storage serials are reserved
// This is necessary because Storage operates on demand
// and don't load all items into memory at startup
void SQLiteDB::SetCurrentStorageItemSerial()
{
  u32 ItemSerialNumber = GetMaxStorageItemSerial();
  if ( ItemSerialNumber > GetCurrentItemSerialNumber() )
    SetCurrentItemSerialNumber( ItemSerialNumber );
}

// Ensures that pcs serials (chr) are reserved
void SQLiteDB::SetCurrentpcsCharSerial()
{
  u32 CharSerialNumber = GetMaxpcsCharSerial();
  if ( CharSerialNumber > GetCurrentCharSerialNumber() )
    SetCurrentCharSerialNumber( CharSerialNumber );
}

// Ensures that pcs serials (item) are reserved
void SQLiteDB::SetCurrentpcsItemSerial()
{
  u32 ItemSerialNumber = GetMaxpcsItemSerial();
  if ( ItemSerialNumber > GetCurrentItemSerialNumber() )
    SetCurrentItemSerialNumber( ItemSerialNumber );
}

// Ensures that pcequip serials (item) are reserved
void SQLiteDB::SetCurrentpcequipItemSerial()
{
  u32 ItemSerialNumber = GetMaxpcequipItemSerial();
  if ( ItemSerialNumber > GetCurrentItemSerialNumber() )
    SetCurrentItemSerialNumber( ItemSerialNumber );
}

void SQLiteDB::BeginTransaction()
{
  if ( Plib::systemstate.config.enable_sqlite )
    sqlite3_exec( db, "BEGIN TRANSACTION", NULL, NULL, NULL );
}

void SQLiteDB::EndTransaction()
{
  if ( Plib::systemstate.config.enable_sqlite )
    sqlite3_exec( db, "END TRANSACTION", NULL, NULL, NULL );
}

void SQLiteDB::RollbackTransaction()
{
  if ( Plib::systemstate.config.enable_sqlite )
    sqlite3_exec( db, "ROLLBACK TRANSACTION", NULL, NULL, NULL );
}

void SQLiteDB::StartPrepStmt()
{
  if ( !( 
	  start_stmt_ListAll( t_storage_main, stmt_list_all_storage ) &&
      start_stmt_ListAll( t_pcs_main,     stmt_list_all_pcs ) &&
      start_stmt_ListAll( t_pcequip_main, stmt_list_all_pcequip ) &&

      start_stmt_GetItem_Name( t_storage_main, stmt_select_storage_main_name ) &&

      start_stmt_GetItem_Serial( t_storage_main, stmt_select_storage_main ) &&
      start_stmt_GetItem_Serial( t_pcs_main,     stmt_select_pcs_main ) &&
      start_stmt_GetItem_Serial( t_pcequip_main, stmt_select_pcequip_main ) &&

      start_stmt_GetProps_Serial( t_storage_prop, stmt_select_storage_prop ) &&
      start_stmt_GetProps_Serial( t_pcs_prop,     stmt_select_pcs_prop ) &&
      start_stmt_GetProps_Serial( t_pcequip_prop, stmt_select_pcequip_prop ) &&

      start_stmt_pcs_prop_get_chrserial( stmt_select_serial_pcs_prop ) &&

      start_stmt_Exist_Name( t_storage_area, stmt_exist_storage_area ) &&
      start_stmt_Exist_Name( t_storage_main, stmt_exist_storage_main_name ) &&

      start_stmt_Exist_Serial( t_storage_main, stmt_exist_storage_main ) &&
      start_stmt_Exist_Serial( t_pcs_main,     stmt_exist_pcs_main ) &&
      start_stmt_Exist_Serial( t_pcequip_main, stmt_exist_pcequip_main ) &&

      start_stmt_Exist_ContainerSerial( t_storage_main, stmt_exist_storage_main_container ) &&

      start_stmt_AddStorageArea() &&

      start_stmt_Add( t_storage_main, columns_main_storage, stmt_insert_storage_main ) &&
      start_stmt_Add( t_storage_prop, columns_prop, stmt_insert_storage_prop ) &&

      start_stmt_Add( t_pcs_main, columns_main, stmt_insert_pcs_main ) &&
      start_stmt_Add( t_pcs_prop, columns_prop, stmt_insert_pcs_prop ) &&

      start_stmt_Add( t_pcequip_main, columns_main, stmt_insert_pcequip_main ) &&
      start_stmt_Add( t_pcequip_prop, columns_prop, stmt_insert_pcequip_prop ) &&
	  
	  start_stmt_Update( t_storage_main, columns_main_storage, stmt_update_storage_main ) &&
	  start_stmt_Update( t_pcs_main, columns_main, stmt_update_pcs_main ) &&
	  start_stmt_Update( t_pcequip_main, columns_main, stmt_update_pcequip_main ) &&
	  
	  start_stmt_Remove( t_storage_main, stmt_delete_storage_main ) &&
	  start_stmt_Remove( t_storage_prop, stmt_delete_storage_prop ) &&

	  start_stmt_Remove( t_pcs_main, stmt_delete_pcs_main ) &&
      start_stmt_Remove( t_pcs_prop, stmt_delete_pcs_prop ) &&

	  start_stmt_Remove( t_pcequip_main, stmt_delete_pcequip_main ) &&
      start_stmt_Remove( t_pcequip_prop, stmt_delete_pcequip_prop ) 
	  ) )
  {
    ERROR_PRINT << "Storage: Prepared Statements error!\n";
    throw runtime_error( "Storage: Prepared Statements error! StartPrepStmt()" );
  }
}

bool SQLiteDB::start_stmt_ListAll( const string& table_name, sqlite3_stmt*& stmt )
{
  string sqlquery = "SELECT Serial FROM ";
  sqlquery += table_name;
  return prepare( sqlquery, stmt );
}

void SQLiteDB::ListAll( map<u32, bool>& all, sqlite3_stmt*& stmt )
{
  all.clear();
  int rc = 0;

  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
  {
    all.emplace( piecewise_construct, forward_as_tuple( sqlite3_column_int( stmt, 0 ) ),
                 forward_as_tuple( true ) );
  }
    //all.insert( make_pair( sqlite3_column_int( stmt, 0 ), true ) );

  if ( rc != SQLITE_DONE )
    Finish( stmt );

  sqlite3_reset( stmt );
}

void SQLiteDB::remove_from_list( const u32& serial, std::map<u32, bool>& all )
{
  auto it = all.find( serial );
  if ( it != all.end() )
    it->second = false;
}

// storage
void SQLiteDB::find_modified_item( Clib::PreparePrint& pp, const string& areaName )
{
  using namespace fmt;

  if ( pp.internal["SAVE_ON_EXIT"] )
  {
    if ( !pp.internal["ORPHAN"] && pp.internal["DIRTY"] )
    {
      modified_storage.insert( make_pair( areaName, pp ) );

      INFO_PRINT_TRACE( 1 ) << "modified_storage (dirty): " << pp.main["Serial"] << " (0x"
                            << hex( lexical_cast<u32>( pp.main["Serial"] ) ) << ")"
                            << " areaName: " << areaName << ".\n";
    }
    // Test: an item inside and this item isn't dirty
    else if ( !pp.internal["ORPHAN"] && !pp.internal["DIRTY"] )
    {
      INFO_PRINT_TRACE( 1 ) << "normal_storage (normal): " << pp.main["Serial"] << " (0x"
                            << hex( lexical_cast<u32>( pp.main["Serial"] ) ) << ")"
                            << " areaName: " << areaName << ".\n";
    }
    else if ( pp.internal["ORPHAN"] )  // is it really possible?
    {
      deleted_storage.emplace_back(
          lexical_cast<u32>( pp.internal["SERIAL_EXT"] ) );

      INFO_PRINT_TRACE( 1 ) << "deleted_storage (ORPHAN): " << pp.internal["SERIAL_EXT"] << " (0x"
                            << hex( lexical_cast<u32>( pp.internal["SERIAL_EXT"] ) ) << ")"
                            << " areaName: " << areaName << ".\n";
    }
  }
  else
  {
    // if SaveOnExit false, so remove from DB if it was saved before.
    deleted_storage.emplace_back( lexical_cast<u32>( pp.main["Serial"] ) );

    INFO_PRINT_TRACE( 1 ) << "deleted_storage (SAVE_ON_EXIT): " << pp.main["Serial"] << " (0x"
                          << hex( lexical_cast<u32>( pp.main["Serial"] ) ) << ")"
                          << " areaName: " << areaName << ".\n";
  }
}

// pcs and pcequip
void SQLiteDB::find_modified_item( Clib::PreparePrint& pp, vector<Clib::PreparePrint>& modified,
                                    vector<u32>& deleted )
{
  using namespace fmt;

  if ( pp.internal["SAVE_ON_EXIT"] )
  {
    if ( !pp.internal["ORPHAN"] && pp.internal["DIRTY"] )
    {
      modified.emplace_back( pp );

      INFO_PRINT_TRACE( 1 ) << "modified_item (dirty): " << pp.main["Serial"] << " (0x"
                            << hex( lexical_cast<u32>( pp.main["Serial"] ) ) << ").\n";
    }
    // Test: an item inside and this item isn't dirty
    else if ( !pp.internal["ORPHAN"] && !pp.internal["DIRTY"] )
    {
      INFO_PRINT_TRACE( 1 ) << "normal_item (normal): " << pp.main["Serial"] << " (0x"
                            << hex( lexical_cast<u32>( pp.main["Serial"] ) ) << ").\n";
    }
    else if ( pp.internal["ORPHAN"] )  // is it really possible?
    {
      deleted.emplace_back( lexical_cast<u32>( pp.internal["SERIAL_EXT"] ) );

      INFO_PRINT_TRACE( 1 ) << "deleted_item (ORPHAN): " << pp.internal["SERIAL_EXT"] << " (0x"
                            << hex( lexical_cast<u32>( pp.internal["SERIAL_EXT"] ) ) << ").\n";
    }
  }
  else
  {
    // if SaveOnExit false, so remove from DB if it was saved before.
    deleted.emplace_back( lexical_cast<u32>( pp.main["Serial"] ) );

    INFO_PRINT_TRACE( 1 ) << "deleted_item (SAVE_ON_EXIT): " << pp.main["Serial"] << " (0x"
                          << hex( lexical_cast<u32>( pp.main["Serial"] ) ) << ").\n";
  }
}

void SQLiteDB::find_deleted_items( map<u32, bool>& all, vector<u32>& deleted )
{
  using namespace fmt;

  for ( auto& sb : all )
  {
    if ( !sb.second )
      continue;

    // if found, the item was moved to another txt data
    Items::Item* item = system_find_item( sb.first );
    if ( item != nullptr )
    {
      deleted.emplace_back( item->serial );

      INFO_PRINT_TRACE( 1 ) << "find_deleted_items (moved): " << item->serial << " (0x"
                            << hex( lexical_cast<u32>( item->serial ) ) << ")"
                            << ".\n";
      continue;
    }

    // found orphan item in another txt data
    UObject* obj = objStorageManager.objecthash.Find( sb.first );
    if ( obj != nullptr && obj->orphan() )
    {
      deleted.emplace_back( sb.first );

      INFO_PRINT_TRACE( 1 ) << "find_deleted_items (orphan): " << sb.first << " (0x"
                            << hex( lexical_cast<u32>( sb.first ) ) << ")"
                            << ".\n";
    }
  }
}

void SQLiteDB::DropIndexes()
{
  if ( !Plib::systemstate.config.enable_sqlite )
    return;

  string sqlquery =
      "								                    \
BEGIN TRANSACTION;										\
DROP INDEX IF EXISTS 'pcs_main_Name';				    \
DROP INDEX IF EXISTS 'pcs_main_Serial';				    \
DROP INDEX IF EXISTS 'pcs_main_Container';				\
DROP INDEX IF EXISTS 'pcs_prop_Serial';				    \
DROP INDEX IF EXISTS 'pcs_prop_PropName';				\
DROP INDEX IF EXISTS 'pcequip_main_Name';				\
DROP INDEX IF EXISTS 'pcequip_main_Serial';				\
DROP INDEX IF EXISTS 'pcequip_main_Container';			\
DROP INDEX IF EXISTS 'pcequip_prop_Serial';				\
DROP INDEX IF EXISTS 'pcequip_prop_PropName';			\
DROP INDEX IF EXISTS 'storage_main_Name';				\
DROP INDEX IF EXISTS 'storage_main_Serial';				\
DROP INDEX IF EXISTS 'storage_main_Container';			\
DROP INDEX IF EXISTS 'storage_prop_Serial';				\
DROP INDEX IF EXISTS 'storage_prop_PropName';			\
COMMIT;                                                 \
  ";

  sqlite3_exec( db, sqlquery.c_str(), NULL, NULL, NULL );
}

void SQLiteDB::CreateIndexes()
{
  if ( !Plib::systemstate.config.enable_sqlite )
    return;

  string sqlquery =
      "								\
BEGIN TRANSACTION;										\
CREATE INDEX IF NOT EXISTS 'pcs_main_Name'          \
ON 'pcs_main' (                                     \
	'Name'	ASC                                         \
);                                                      \
CREATE INDEX IF NOT EXISTS 'pcs_main_Serial'          \
ON 'pcs_main' (                                     \
	'Serial'	ASC                                     \
);                                                      \
CREATE INDEX IF NOT EXISTS 'pcs_main_Container'      \
ON 'pcs_main' (                                     \
	'Container'	ASC                                     \
);                                                      \
CREATE INDEX IF NOT EXISTS 'pcs_prop_Serial'          \
ON 'pcs_prop' (                                     \
	'Serial'	ASC                                     \
);                                                      \
CREATE INDEX IF NOT EXISTS 'pcs_prop_PropName'          \
ON 'pcs_prop' (                                     \
	'PropName'	ASC                                     \
);                                                      \
CREATE INDEX IF NOT EXISTS 'pcequip_main_Name'          \
ON 'pcequip_main' (                                     \
	'Name'	ASC                                         \
);                                                      \
CREATE INDEX IF NOT EXISTS 'pcequip_main_Serial'          \
ON 'pcequip_main' (                                     \
	'Serial'	ASC                                     \
);                                                      \
CREATE INDEX IF NOT EXISTS 'pcequip_main_Container'      \
ON 'pcequip_main' (                                     \
	'Container'	ASC                                     \
);                                                      \
CREATE INDEX IF NOT EXISTS 'pcequip_prop_Serial'          \
ON 'pcequip_prop' (                                     \
	'Serial'	ASC                                     \
);                                                      \
CREATE INDEX IF NOT EXISTS 'pcequip_prop_PropName'      \
ON 'pcequip_prop' (                                     \
	'PropName'	ASC                                     \
);                                                      \
CREATE INDEX IF NOT EXISTS 'storage_main_Name'          \
ON 'storage_main' (                                     \
	'Name'	ASC                                         \
);                                                      \
CREATE INDEX IF NOT EXISTS 'storage_main_Serial'          \
ON 'storage_main' (                                     \
	'Serial'	ASC                                     \
);                                                      \
CREATE INDEX IF NOT EXISTS 'storage_main_Container'      \
ON 'storage_main' (                                     \
	'Container'	ASC                                     \
);                                                      \
CREATE INDEX IF NOT EXISTS 'storage_prop_Serial'          \
ON 'storage_prop' (                                     \
	'Serial'	ASC                                     \
);                                                      \
CREATE INDEX IF NOT EXISTS 'storage_prop_PropName'      \
ON 'storage_prop' (                                     \
	'PropName'	ASC                                     \
);                                                      \
COMMIT;                                                 \
  ";

  sqlite3_exec( db, sqlquery.c_str(), NULL, NULL, NULL );
}

void SQLiteDB::add_container_opened( const u32 item, const u32 chr )
{
  if ( !Plib::systemstate.config.enable_sqlite )
    return;

  auto& co = containers_opened;

  // if new item (no found)
  if ( co.find( item ) == co.end() )
  {
    vector<u32> vec_chr;
    vec_chr.emplace_back( chr );
    co.insert( make_pair( item, vec_chr ) );
  }
  // exist item
  else
  {
    // Check if chr not exists in vector
    auto it = find( co[item].begin(), co[item].end(), chr );
    if ( it == co[item].end() )
      co[item].emplace_back( chr );
  }
}

void SQLiteDB::CheckUnusedRootItem()
{
  auto& co = containers_opened;
  auto& column = columns_main_storage;
  auto& stmt = stmt_select_storage_main_name;
  multimap<StorageArea*, string> list_items;
  BeginTransaction();
  for ( const auto& c : co )
  {
    Items::Item* item = system_find_item( c.first );
    if ( item != nullptr )
    {
      bool logged_off = true;
      for ( auto chr_serial : c.second )
      {
        Mobile::Character* chr = find_character( chr_serial );
        if ( chr != nullptr )
          logged_off = false;
      }
      if ( logged_off )
      {
        map<string, string> main;
        GetItem( item->name_, main, column, stmt );
        string areaName = GetNameArea( main["AreaId"] );
        StorageArea* area = gamestate.storage.find_area( areaName );
        list_items.insert( make_pair( area, item->name_ ) );
        co.erase( c.first );
      }
    }
  }
  EndTransaction();

  for ( const auto& it : list_items )
    it.first->unload_root_item( it.second );
}

void SQLiteDB::RemoveObjectHash( const u32 serial )
{
  if ( !Plib::systemstate.config.enable_sqlite )
    return;

  // Erase from objecthash
  INFO_PRINT_TRACE( 1 ) << "RemoveObjectHash(): Erase objecthash " << serial << "(0x"
                        << fmt::hex( serial ) << ")\n";
  Core::objStorageManager.objecthash.Remove( serial );
}

// Load chr with held/equipped items
void SQLiteDB::load_chr_and_items( const u32& serial )
{
  BeginTransaction();

  // Read chr into pcs database
  read_chr( serial );

  // Read item into pcs database
  std::vector<u32> rootItemsSerial;
  auto& stmt_pcs_prop = stmt_select_pcs_prop;
  read_chr_items( serial, t_pcs_main, stmt_pcs_prop, rootItemsSerial );

  // Read item into pcequip database
  rootItemsSerial.insert( rootItemsSerial.begin(), serial );
  auto& stmt_pcequip_prop = stmt_select_pcequip_prop;
  std::vector<u32> noused;
  for ( auto r_serial : rootItemsSerial )
    read_chr_items( r_serial, t_pcequip_main, stmt_pcequip_prop, noused );

  EndTransaction();

  Mobile::Character* chr = system_find_mobile( serial );
  if ( chr->acct != nullptr )
    chr->logged_in( false );
}

void SQLiteDB::commit_storage()
{
  UpdateDataStorage();
  DeleteData( deleted_storage, stmt_delete_storage_main );
  modified_storage.clear();
  deleted_storage.clear();
  deleted_storage.shrink_to_fit();
  CheckUnusedRootItem();
  ListAll( all_storage_serials, stmt_list_all_storage );
}

void SQLiteDB::commit_pcs()
{
  UpdateDataPCs();
  DeleteData( deleted_pcs, stmt_delete_pcs_main );
  modified_pcs.clear();
  modified_pcs.shrink_to_fit();
  deleted_pcs.clear();
  deleted_pcs.shrink_to_fit();
  ListAll( all_pcs_serials, stmt_list_all_pcs );
}

void SQLiteDB::commit_pcequip()
{
  UpdateDataPCEquip();
  DeleteData( deleted_pcequip, stmt_delete_pcequip_main );
  modified_pcequip.clear();
  modified_pcequip.shrink_to_fit();
  deleted_pcequip.clear();
  deleted_pcequip.shrink_to_fit();
  ListAll( all_pcequip_serials, stmt_list_all_pcequip );
}

bool SQLiteDB::find_serial( const u32& serial )
{
  if ( !Plib::systemstate.config.enable_sqlite )
    return false;

  if ( all_pcs_serials.find( serial ) != all_pcs_serials.end() )
    return true;
  else if ( all_pcequip_serials.find( serial ) != all_pcequip_serials.end() )
    return true;
  else if ( all_storage_serials.find( serial ) != all_storage_serials.end() )
    return true;

  return false;
}

}  // namespace Core
}  // namespace Pol
