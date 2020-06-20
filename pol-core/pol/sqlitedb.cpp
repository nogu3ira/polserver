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
#include "objtype.h"
#include "polcfg.h"
#include "ufunc.h"
#include <sqlite/sqlite3.h>
// #include <boost/bind.hpp>
#include <boost/algorithm/string/replace.hpp>

namespace Pol
{
namespace Core
{
using namespace Bscript;

SQLiteDB::SQLiteDB()
{
  INFO_PRINT_TRACE( 1 ) << "INSIDE CONSTRUCTOR SQLITEDB.\n";
  Connect();
}

SQLiteDB::~SQLiteDB()
{
  Close();
}

// Insert root item only in SQLite Database. Don't load item in memory.
void SQLiteDB::insert_root_item( Items::Item* item, const std::string& areaName )
{
  if ( Plib::systemstate.config.enable_sqlite )
  {
    if ( !AddItem( item, areaName ) )
    {
      INFO_PRINT_TRACE( 1 ) << "insert_root_item: no added in BD.\n";
      return;
    }
    INFO_PRINT_TRACE( 1 ) << "insert_root_item: yes added in BD.\n";
  }
}

// Insert item only in SQLite Database. Don't load item in memory.
void SQLiteDB::insert_item( Items::Item* item, const std::string& areaName,
                            const u32 container_serial )
{
  if ( Plib::systemstate.config.enable_sqlite )
  {
    if ( !AddItem( item, areaName, container_serial ) )
    {
      INFO_PRINT_TRACE( 1 ) << "insert_item: no added in BD.\n";
      return;
    }
    INFO_PRINT_TRACE( 1 ) << "insert_item: yes added in BD.\n";
  }
}

// Convert map format to elem format
// then, load_item() into memory
void SQLiteDB::item_up( std::string areaName, std::map<std::string, std::string> main,
                        std::map<std::string, std::string> unusual,
                        std::map<std::string, std::string> cprops )
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
    throw std::runtime_error( "Data file integrity error" );
  }
}

// Read one item from SQLite Database
u32 SQLiteDB::read_item( const std::string& name )
{
  clock_t start = clock();
  std::map<std::string, std::string> main, unusual, cprops;

  BeginTransaction();
  GetItem( name, main );
  GetProps( main["Serial"], unusual, cprops );
  std::string areaName = GetNameArea( main["AreaId"] );
  EndTransaction();
  item_up( areaName, main, unusual, cprops );

  clock_t end = clock();
  int ms = static_cast<int>( ( end - start ) * 1000.0 / CLOCKS_PER_SEC );
  INFO_PRINT << " 1 item in " << ms << " ms.\n";

  return boost::lexical_cast<u32>( main["Serial"] );
}

// Read items in container from SQLite Database
void SQLiteDB::read_items_in_container( const u32 container_serial )
{
  clock_t start = clock();
  unsigned int nobjects = 0;
  std::vector<std::map<std::string, std::string>> ItemsInfo_main;
  std::vector<u32> ItemsInfoSerial;

  BeginTransaction();
  // list contents of root containers
  int found_item = GetItems( container_serial, ItemsInfo_main, ItemsInfoSerial );

  INFO_PRINT_TRACE( 1 ) << "read_items_in_container: found_item = " << found_item << "\n";

  // list contents of sub-containers
  while ( found_item > 0 )
  {
    found_item = GetItems( 0, ItemsInfo_main, ItemsInfoSerial );
    INFO_PRINT_TRACE( 1 ) << "read_items_in_container: while found_item = " << found_item << "\n";
  }

  for ( auto main : ItemsInfo_main )
  {
    std::map<std::string, std::string> unusual, cprops;
    GetProps( main["Serial"], unusual, cprops );
    std::string areaName = GetNameArea( main["AreaId"] );
    item_up( areaName, main, unusual, cprops );
    ++nobjects;
  }
  EndTransaction();

  clock_t end = clock();
  int ms = static_cast<int>( ( end - start ) * 1000.0 / CLOCKS_PER_SEC );
  INFO_PRINT << " " << nobjects << " items in " << ms << " ms.\n";
}

bool SQLiteDB::ExistDB()
{
  gamestate.sqlitedb.dbpath = Plib::systemstate.config.world_data_path + dbname + ".db";
  if ( Clib::FileExists( gamestate.sqlitedb.dbpath ) )
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
        throw std::runtime_error( "Storage: Can't create database " + gamestate.sqlitedb.dbpath );
    }

    int rc = sqlite3_open( gamestate.sqlitedb.dbpath.c_str(), &gamestate.sqlitedb.db );
    if ( rc )
    {
      ERROR_PRINT << "Storage: Can't open database: " << sqlite3_errmsg( gamestate.sqlitedb.db )
                  << ".\n";
      throw std::runtime_error( "Storage: Can't open database " + gamestate.sqlitedb.dbpath );
    }
    PragmaSettings();
    INFO_PRINT << " SQLite database connected!\n";
    StartPrepStmt();
    SetCurrentStorageItemSerial();
  }
}

void SQLiteDB::Finish( sqlite3_stmt*& stmt, bool x )
{
  if ( x )
  {
    ERROR_PRINT << "Storage: " << sqlite3_errmsg( gamestate.sqlitedb.db ) << ".\n";
  }
  sqlite3_finalize( stmt );
}

void SQLiteDB::Close()
{
  if ( Plib::systemstate.config.enable_sqlite )
    sqlite3_close( gamestate.sqlitedb.db );
}

bool SQLiteDB::start_stmt_ExistInStorage_Name( const std::string table_name, sqlite3_stmt*& stmt )
{
  std::string sqlquery = "SELECT EXISTS(SELECT 1 FROM ";
  sqlquery += table_name;
  sqlquery += " WHERE Name = ? LIMIT 1 ) AS result";
  return prepare( sqlquery, stmt );
}

bool SQLiteDB::ExistInStorage( const std::string& name, sqlite3_stmt*& stmt )
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

bool SQLiteDB::start_stmt_ExistInStorage_Serial( const std::string table_name, sqlite3_stmt*& stmt )
{
  std::string sqlquery = "SELECT EXISTS(SELECT 1 FROM ";
  sqlquery += table_name;
  sqlquery += " WHERE Serial = ? LIMIT 1 ) AS result";
  return prepare( sqlquery, stmt );
}

bool SQLiteDB::ExistInStorage( const u32 serial, sqlite3_stmt*& stmt )
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

void SQLiteDB::load_toplevel_owner( const u32 serial )
{
  std::map<std::string, std::string> main;
  u32 container = 0;

  BeginTransaction();
  GetItem( serial, main );
  while ( main.find("Container") != main.end() )
  {
    container = boost::lexical_cast<u32>( main["Container"] );
    main.clear();
    GetItem( container, main );
  }

  if ( main.find("Name") == main.end() )
  {
    ERROR_PRINT << "find_toplevel_owner(): ERROR! Name not found in root_item: " << main["Serial"] << "\n";
    return;
  }

  std::string areaName = GetNameArea( main["AreaId"] );
  StorageArea* area = gamestate.storage.find_area( areaName );
  EndTransaction();
  area->find_root_item( main["Name"] );
}

void SQLiteDB::ListStorageAreas()
{
  std::string sqlquery = "SELECT Name FROM StorageArea";

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( gamestate.sqlitedb.db, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    Finish( stmt );
    return;
  }
  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
  {
    std::string Name =
        std::string( reinterpret_cast<const char*>( sqlite3_column_text( stmt, 0 ) ) );
    gamestate.storage.create_area( Name );
  }
  if ( rc != SQLITE_DONE )
  {
    Finish( stmt );
    return;
  }
  Finish( stmt, false );
}

bool SQLiteDB::prepare( std::string sqlquery, sqlite3_stmt*& stmt )
{
  int rc = sqlite3_prepare_v2( gamestate.sqlitedb.db, sqlquery.c_str(), -1, &stmt, NULL );
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
void SQLiteDB::bind( int order, std::string text, sqlite3_stmt*& stmt )
{
  sqlite3_bind_text( stmt, order, text.c_str(), -1, SQLITE_TRANSIENT );
}

void SQLiteDB::bind( int order, sqlite3_stmt*& stmt )
{
  sqlite3_bind_null( stmt, order );
}

bool SQLiteDB::start_stmt_AddStorageArea()
{
  std::string sqlquery = "INSERT INTO storage_Area (Name) VALUES(?)";
  return prepare( sqlquery, gamestate.sqlitedb.stmt_AddStorageArea );
}

void SQLiteDB::AddStorageArea( const std::string& name )
{
  bind( 1, name, gamestate.sqlitedb.stmt_AddStorageArea );
  if ( !query_execute( gamestate.sqlitedb.stmt_AddStorageArea ) )
    ERROR_PRINT << "Storage: No StorageArea inserted. Name: " << name << "\n";
}

int SQLiteDB::GetMaxStorageItemSerial()
{
  int MaxSerial = 0;
  std::string sqlquery = "SELECT MAX(Serial) FROM storage_Item";

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( gamestate.sqlitedb.db, sqlquery.c_str(), -1, &stmt, NULL );
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

int SQLiteDB::GetIdArea( const std::string& name )
{
  int AreaId = 0;
  std::string sqlquery = "SELECT AreaId FROM storage_Area WHERE Name='";
  sqlquery += name;
  sqlquery += "'";

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( gamestate.sqlitedb.db, sqlquery.c_str(), -1, &stmt, NULL );
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

std::string SQLiteDB::GetNameArea( const std::string id )
{
  std::string Name = "";
  std::string sqlquery = "SELECT Name FROM storage_Area WHERE AreaId = ";
  sqlquery += id;

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( gamestate.sqlitedb.db, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    Finish( stmt );
    return 0;
  }
  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
  {
    Name = boost::lexical_cast<std::string>( sqlite3_column_text( stmt, 0 ) );
  }
  if ( rc != SQLITE_DONE )
  {
    Finish( stmt );
    return 0;
  }
  Finish( stmt, false );
  return Name;
}

void SQLiteDB::Prop_RowsToColumns( std::vector<std::string>& PropNames )
{
  std::string sqlquery = "SELECT PropName FROM storage_Prop WHERE CProp = 0 GROUP BY PropName";

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( gamestate.sqlitedb.db, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    Finish( stmt );
    return;
  }
  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
  {
    if ( sqlite3_column_type( stmt, 0 ) != SQLITE_NULL )
      PropNames.push_back( boost::lexical_cast<std::string>( sqlite3_column_text( stmt, 0 ) ) );
  }
  if ( rc != SQLITE_DONE )
  {
    Finish( stmt );
    return;
  }
  Finish( stmt, false );
}

void SQLiteDB::Prop_CastInteger( std::string& filters )
{
  // Check INTEGER value of properties.
  // Scripter may indicate using int(value) if that value need to be read as integer or float/double
  // Example:
  // Name = 'Shiny scythe' AND Amount >= int(12345123456789)
  // Objtype <> int(0x5EE06BC1) OR Quality = int(1.104989);

  size_t s, e;
  while ( ( s = filters.find( "int(" ) ) != std::string::npos &&
          ( e = filters.find( ")", s ) ) != std::string::npos )
  {
    std::string sub = filters.substr( s + 4, e - s - 4 );
    std::string f_s = filters.substr( 0, s );
    std::string f_e = filters.substr( e + 1 );
    std::string type_cast = "INTEGER";

    if ( sub.find( "0x" ) != std::string::npos )
      sub = std::to_string( std::stol( sub, nullptr, 0 ) );

    if ( sub.find( "." ) != std::string::npos )
    {
      sub = std::to_string( std::stold( sub ) );
      type_cast = "FLOAT";
    }

    filters = f_s + "CAST(" + sub + " AS " + type_cast + ")" + f_e;
  }
}

bool SQLiteDB::GetItemCustomFilter( std::string filters, std::vector<u32>& serials,
                                    const std::string areaName, std::string& err_msg )
{
  auto AreaId = boost::lexical_cast<std::string>( GetIdArea( areaName ) );

  std::string sqlquery =
      "SELECT Serial FROM storage_Item AS m JOIN "
      "(SELECT t.Serial AS Serial_Prop, ";

  std::vector<std::string> PropNames;
  Prop_RowsToColumns( PropNames );

  for ( const auto& unusual : PropNames )
  {
    sqlquery += "MAX(CASE WHEN t.PropName = '" + unusual +
                "' AND t.CProp = 0 THEN t.PropValue END) AS " + unusual + ",";
  }
  sqlquery.pop_back();  // Remove last character ',' from string

  sqlquery += " FROM storage_Prop AS t GROUP BY t.Serial) AS p "
              "ON m.Serial = p.Serial_Prop WHERE AreaId = ";
  sqlquery += AreaId;
  sqlquery += " AND ";

  Prop_CastInteger( filters );

  sqlquery += filters;

  INFO_PRINT_TRACE( 1 ) << "GetItemCustomFilter: " << sqlquery << "\n";

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( gamestate.sqlitedb.db, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    ERROR_PRINT << "GetItem: some problem with prepare query.\n";
    err_msg = sqlite3_errmsg( gamestate.sqlitedb.db );
    Finish( stmt );
    return false;
  }

  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
    serials.push_back( sqlite3_column_int( stmt, 0 ) );

  if ( rc != SQLITE_DONE )
  {
    ERROR_PRINT << "GetItem: some problem in query.\n";
    err_msg = sqlite3_errmsg( gamestate.sqlitedb.db );
    Finish( stmt );
    return false;
  }

  Finish( stmt, false );
  INFO_PRINT_TRACE( 1 ) << "GetItemCustomFilter: OK.\n";
  return true;
}

void SQLiteDB::GetItem( const std::string name, std::map<std::string, std::string>& main )
{
  INFO_PRINT_TRACE( 1 ) << "GetItem: start method.\n";

  std::string sqlquery = "SELECT * FROM storage_Item WHERE Name = '";
  sqlquery += name;
  sqlquery += "' LIMIT 1";

  INFO_PRINT_TRACE( 1 ) << "GetItem: " << sqlquery << "\n";

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( gamestate.sqlitedb.db, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    ERROR_PRINT << "GetItem: some problem with prepare query.\n";
    Finish( stmt );
    return;
  }
  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
  {
    // INFO_PRINT_TRACE( 1 ) << "GetItem: inside while.\n";
    PrepareItemInfo( stmt, main );
  }
  if ( rc != SQLITE_DONE )
  {
    ERROR_PRINT << "GetItem: some problem in query.\n";
    Finish( stmt );
    return;
  }
  Finish( stmt, false );
  INFO_PRINT_TRACE( 1 ) << "GetItem: OK.\n";
}

void SQLiteDB::GetItem( const u32 serial, std::map<std::string, std::string>& main )
{
  std::string sqlquery = "SELECT * FROM storage_Item WHERE Serial = ";
  sqlquery += boost::lexical_cast<std::string>( serial );
  sqlquery += " LIMIT 1";

  INFO_PRINT_TRACE( 1 ) << "GetItem: " << sqlquery << "\n";

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( gamestate.sqlitedb.db, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    ERROR_PRINT << "GetItem: some problem with prepare query.\n";
    Finish( stmt );
    return;
  }
  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
  {
    // INFO_PRINT_TRACE( 1 ) << "GetItem: inside while.\n";
    PrepareItemInfo( stmt, main );
  }
  if ( rc != SQLITE_DONE )
  {
    ERROR_PRINT << "GetItem: some problem in query.\n";
    Finish( stmt );
    return;
  }
  Finish( stmt, false );
  INFO_PRINT_TRACE( 1 ) << "GetItem: OK.\n";
}

void SQLiteDB::PrepareQueryGetItems( sqlite3_stmt*& stmt, int params )
{
  std::string sqlquery = "SELECT * FROM storage_Item WHERE Container = ?";

  if ( params > 1 )
  {
    for ( unsigned i = 1; i <= params; ++i )
    {
      sqlquery += " OR Container = ?";
    }
  }

  INFO_PRINT_TRACE( 1 ) << "PrepareQueryGetItems: sqlquery = " << sqlquery << ".\n";

  int rc = sqlite3_prepare_v2( gamestate.sqlitedb.db, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    ERROR_PRINT << "GetItems: some problem with prepare query.\n";
    Finish( stmt );
  }
}

int SQLiteDB::GetItems( const u32 container_serial,
                        std::vector<std::map<std::string, std::string>>& ItemsInContainer,
                        std::vector<u32>& ItemsInfoSerial )
{
  INFO_PRINT_TRACE( 1 ) << "GetItems: start method.\n";

  int count = 0;
  int rc = 0;
  sqlite3_stmt* stmt;

  // first search
  if ( container_serial != 0 )
  {
    PrepareQueryGetItems( stmt, 1 );
    sqlite3_bind_int( stmt, 1, container_serial );
    INFO_PRINT_TRACE( 1 ) << "GetItems: first search Container = " << container_serial << ".\n";
  }
  // search contents of sub-containers
  else
  {
    int params = static_cast<int>( ItemsInfoSerial.size() );
    PrepareQueryGetItems( stmt, params );
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
    std::map<std::string, std::string> main;
    PrepareItemInfo( stmt, main );
    u32 main_serial = boost::lexical_cast<u32>( main["Serial"] );

    // if no duplicate, go ahead.
    if ( CanAddItemInfo( main_serial, ItemsInContainer ) )
    {
      ItemsInContainer.push_back( main );
      ItemsInfoSerial.push_back( main_serial );
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

bool SQLiteDB::CanAddItemInfo( const u32 serial,
                               std::vector<std::map<std::string, std::string>> ItemsInContainer )
{
  for ( auto iteminfo : ItemsInContainer )
  {
    if ( serial == boost::lexical_cast<u32>( iteminfo["Serial"] ) )
    {
      INFO_PRINT_TRACE( 1 ) << "CanAddItemInfo: item is already added into vector. SERIAL: "
                            << serial << "\n";
      return false;
    }
  }
  return true;
}

std::string SQLiteDB::UnEscapeSequence( std::string value ) const
{
  boost::replace_all( value, "\"\"", "\"" );
  boost::replace_all( value, "\'\'", "\'" );
  return value;
}

void SQLiteDB::PrepareItemInfo( sqlite3_stmt*& stmt, std::map<std::string, std::string>& main )
{
  main.insert( std::make_pair(
      "AreaId", boost::lexical_cast<std::string>( sqlite3_column_int( stmt, 0 ) ) ) );

  if ( sqlite3_column_type( stmt, 1 ) != SQLITE_NULL )
    main.insert( std::make_pair( "Name", UnEscapeSequence( boost::lexical_cast<std::string>(
                                             sqlite3_column_text( stmt, 1 ) ) ) ) );

  main.insert( std::make_pair(
      "Serial", boost::lexical_cast<std::string>( sqlite3_column_int( stmt, 2 ) ) ) );

  if ( sqlite3_column_type( stmt, 3 ) != SQLITE_NULL )
    main.insert( std::make_pair(
        "Container", boost::lexical_cast<std::string>( sqlite3_column_int( stmt, 3 ) ) ) );

  main.insert( std::make_pair(
      "ObjType", boost::lexical_cast<std::string>( sqlite3_column_int( stmt, 4 ) ) ) );
  main.insert( std::make_pair(
      "Graphic", boost::lexical_cast<std::string>( sqlite3_column_int( stmt, 5 ) ) ) );
  main.insert(
      std::make_pair( "X", boost::lexical_cast<std::string>( sqlite3_column_int( stmt, 6 ) ) ) );
  main.insert(
      std::make_pair( "Y", boost::lexical_cast<std::string>( sqlite3_column_int( stmt, 7 ) ) ) );
  main.insert(
      std::make_pair( "Z", boost::lexical_cast<std::string>( sqlite3_column_int( stmt, 8 ) ) ) );
  main.insert( std::make_pair(
      "Revision", boost::lexical_cast<std::string>( sqlite3_column_int( stmt, 9 ) ) ) );
  main.insert( std::make_pair( "Realm", UnEscapeSequence( boost::lexical_cast<std::string>(
                                            sqlite3_column_text( stmt, 10 ) ) ) ) );
}

bool SQLiteDB::RemoveItem( const std::string& name )
{
  std::string sqlquery = "DELETE FROM storage_Item WHERE Name = '";
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

bool SQLiteDB::RemoveItem( const u32 serial )
{
  std::string sqlquery = "DELETE FROM storage_Item WHERE Serial = ";
  sqlquery += boost::lexical_cast<std::string>( serial );

  sqlite3_stmt* s;
  prepare( sqlquery, s );

  if ( !query_execute( s ) )
  {
    ERROR_PRINT << "Storage: No data deleted. Serial: " << serial << "\n";
    return false;
  }
  Finish( s, false );
  return true;
}

bool SQLiteDB::ExecuteUpdateItem( Clib::PreparePrint& pp )
{
  // main properties
  std::string s = "UPDATE storage_Item";
  s += " SET";

  s += " AreaId = ";
  s += std::string( pp.main["AreaId"] ).append( "," );
  s += " Name = ";
  s += ( pp.main.find( "Name" ) == pp.main.end()
             ? "NULL,"
             : std::string( "'" ).append( pp.main["Name"] ).append( "'," ) );
  s += " Serial = ";
  s += std::string( pp.main["Serial"] ).append( "," );
  s += " Container = ";
  s += ( pp.main.find( "Container" ) == pp.main.end()
             ? "NULL,"
             : std::string( "'" ).append( pp.main["Container"] ).append( "'," ) );
  s += " ObjType = ";
  s += std::string( pp.main["ObjType"] ).append( "," );
  s += " Graphic = ";
  s += std::string( pp.main["Graphic"] ).append( "," );
  s += " X = ";
  s += std::string( pp.main["X"] ).append( "," );
  s += " Y = ";
  s += std::string( pp.main["Y"] ).append( "," );
  s += " Z = ";
  s += std::string( pp.main["Z"] ).append( "," );
  s += " Revision = ";
  s += std::string( pp.main["Revision"] ).append( "," );
  s += " Realm = ";
  s += std::string( "'" ).append( pp.main["Realm"] ).append( "'" );

  s += " WHERE Serial = ";
  s += pp.main["Serial"];

  sqlite3_stmt* ss;
  prepare( s, ss );

  if ( !query_execute( ss ) )
  {
    ERROR_PRINT << "Storage: No main prop updated. Serial: " << pp.main["Serial"] << "\n";
    RollbackTransaction();
    throw std::runtime_error( "Data file integrity error" );
    return false;
  }
  Finish( ss, false );

  RemoveProps( boost::lexical_cast<u32>( pp.main["Serial"] ) );

  // unusual properties
  if ( !AddProp( boost::lexical_cast<u32>( pp.main["Serial"] ), pp.unusual, false ) )
  {
    ERROR_PRINT << "Storage: No Unusual Prop inserted. Serial: " << pp.main["Serial"] << "\n";
    RollbackTransaction();
    throw std::runtime_error( "Data file integrity error" );
    return false;
  }

  if ( !AddCProp( boost::lexical_cast<u32>( pp.main["Serial"] ), pp.cprop, true ) )
  {
    ERROR_PRINT << "Storage: No CProp inserted. Serial: " << pp.main["Serial"] << "\n";
    RollbackTransaction();
    throw std::runtime_error( "Data file integrity error" );
    return false;
  }

  return true;
}

bool SQLiteDB::UpdateItem( Clib::PreparePrint& pp, const std::string& areaName )
{
  AppendAreaId( pp, areaName );
  return ExecuteUpdateItem( pp );
}

bool SQLiteDB::UpdateItem( Items::Item* item, const std::string& areaName )
{
  Clib::PreparePrint pp;
  AppendAreaId( pp, areaName );
  item->printProperties( pp );
  return ExecuteUpdateItem( pp );
}

void SQLiteDB::GetProps( std::string Serial, std::map<std::string, std::string>& unusual,
                         std::map<std::string, std::string>& cprops )
{
  std::string sqlquery = "SELECT PropName, PropValue, CProp FROM storage_Prop WHERE Serial = ";
  sqlquery += Serial;

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( gamestate.sqlitedb.db, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    ERROR_PRINT << "GetProps: some problem in prepare_query.\n";
    Finish( stmt );
    return;
  }
  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
  {
    if ( ( sqlite3_column_type( stmt, 0 ) != SQLITE_NULL ) &&
         ( sqlite3_column_type( stmt, 1 ) != SQLITE_NULL ) )
    {
      auto PropName =
          std::string( reinterpret_cast<const char*>( sqlite3_column_text( stmt, 0 ) ) );
      auto PropValue =
          std::string( reinterpret_cast<const char*>( sqlite3_column_text( stmt, 1 ) ) );
      auto isCProp = sqlite3_column_int( stmt, 2 );

      if ( isCProp == 1 )
        cprops.insert( make_pair( PropName, PropValue ) );
      else
        unusual.insert( make_pair( PropName, PropValue ) );
    }
  }
  if ( rc != SQLITE_DONE )
  {
    ERROR_PRINT << "GetProps: some problem in select.\n";
    Finish( stmt );
    return;
  }
  Finish( stmt, false );
}

bool SQLiteDB::RemoveProps( const int Serial )
{
  std::string sqlquery = "DELETE FROM storage_Prop WHERE Serial = ";
  sqlquery += boost::lexical_cast<std::string>( Serial );

  sqlite3_stmt* s;
  prepare( sqlquery, s );

  if ( !query_execute( s ) )
  {
    ERROR_PRINT << "Storage: No Prop deleted. Serial: " << Serial << "\n";
    return false;
  }
  Finish( s, false );
  return true;
}

// unusual prop
bool SQLiteDB::AddProp( const u32 serial_item, std::multimap<std::string, std::string> props,
                        bool isCProp )
{
  if ( props.empty() )
    return true;

  for ( const auto& kv : props )
  {
    sqlite3_bind_int( gamestate.sqlitedb.stmt_AddStorageProp, 1, serial_item );  // Serial
    sqlite3_bind_text( gamestate.sqlitedb.stmt_AddStorageProp, 2, kv.first.c_str(), -1,
                       SQLITE_TRANSIENT );  // PropName
    sqlite3_bind_text( gamestate.sqlitedb.stmt_AddStorageProp, 3, kv.second.c_str(), -1,
                       SQLITE_TRANSIENT );                                   // PropValue
    sqlite3_bind_int( gamestate.sqlitedb.stmt_AddStorageProp, 4, isCProp );  // CProp boolean

    if ( !query_execute( gamestate.sqlitedb.stmt_AddStorageProp ) )
      return false;
  }
  return true;
}

// cprop
bool SQLiteDB::AddCProp( const u32 serial_item, std::map<std::string, std::string> props,
                         bool isCProp )
{
  if ( props.empty() )
    return true;

  for ( const auto& kv : props )
  {
    sqlite3_bind_int( gamestate.sqlitedb.stmt_AddStorageProp, 1, serial_item );  // Serial
    sqlite3_bind_text( gamestate.sqlitedb.stmt_AddStorageProp, 2, kv.first.c_str(), -1,
                       SQLITE_TRANSIENT );  // PropName
    sqlite3_bind_text( gamestate.sqlitedb.stmt_AddStorageProp, 3, kv.second.c_str(), -1,
                       SQLITE_TRANSIENT );                                   // PropValue
    sqlite3_bind_int( gamestate.sqlitedb.stmt_AddStorageProp, 4, isCProp );  // CProp boolean

    if ( !query_execute( gamestate.sqlitedb.stmt_AddStorageProp ) )
      return false;
  }
  return true;
}

bool SQLiteDB::AddMain( std::map<std::string, std::string> main )
{
  if ( main.find( "Container" ) == main.end() )
    main.insert( std::make_pair( "Container", "" ) );

  if ( main.find( "Name" ) == main.end() )
    main.insert( std::make_pair( "Name", "" ) );

  // bind main prop
  bind_properties( gamestate.sqlitedb.columns_AddStorageItem, main,
                   gamestate.sqlitedb.stmt_AddStorageItem );

  if ( !query_execute( gamestate.sqlitedb.stmt_AddStorageItem ) )
    return false;

  return true;
}

bool SQLiteDB::ExecuteInsertItem( Clib::PreparePrint& pp )
{
  if ( !AddMain( pp.main ) )
  {
    ERROR_PRINT << "Storage: No Main Prop inserted.\n";
    RollbackTransaction();
    throw std::runtime_error( "Data file integrity error" );
    return false;
  }
  // unusual properties
  if ( !AddProp( boost::lexical_cast<u32>( pp.main["Serial"] ), pp.unusual, false ) )
  {
    ERROR_PRINT << "Storage: No Unusual Prop inserted.\n";
    RollbackTransaction();
    throw std::runtime_error( "Data file integrity error" );
    return false;
  }
  // cprop
  if ( !AddCProp( boost::lexical_cast<u32>( pp.main["Serial"] ), pp.cprop, true ) )
  {
    ERROR_PRINT << "Storage: No CProp inserted.\n";
    RollbackTransaction();
    throw std::runtime_error( "Data file integrity error" );
    return false;
  }
  return true;
}

void SQLiteDB::AppendAreaId( Clib::PreparePrint& pp, const std::string& areaName )
{
  auto AreaId = boost::lexical_cast<std::string>( GetIdArea( areaName ) );
  pp.main.insert( std::make_pair( "AreaId", AreaId ) );
}

bool SQLiteDB::AddItem( Clib::PreparePrint& pp, const std::string& areaName )
{
  AppendAreaId( pp, areaName );
  return ExecuteInsertItem( pp );
}

bool SQLiteDB::AddItem( Items::Item* item, const std::string& areaName, const u32 container_serial )
{
  Clib::PreparePrint pp;
  AppendAreaId( pp, areaName );
  item->printProperties( pp );
  if ( container_serial != 0 )
    pp.main["Container"] = boost::lexical_cast<std::string>( container_serial );

  return ExecuteInsertItem( pp );
}

bool SQLiteDB::start_stmt_AddStorage( std::vector<std::map<std::string, std::string>>& columns,
                                      std::string table_name, sqlite3_stmt*& stmt )
{
  columns.clear();
  // Get all column_name
  std::string sqlquery = "PRAGMA table_info(";
  sqlquery += table_name;
  sqlquery += ")";
  prepare( sqlquery, stmt );

  sqlquery = "INSERT INTO ";
  sqlquery += table_name;
  sqlquery += " (";

  int rows = 0;
  while ( ( sqlite3_step( stmt ) ) == SQLITE_ROW )
  {
    auto ColumnName = boost::lexical_cast<std::string>( sqlite3_column_text( stmt, 1 ) );
    auto ColumnType = boost::lexical_cast<std::string>( sqlite3_column_text( stmt, 2 ) );
    sqlquery += ColumnName;  // Column name
    sqlquery += ",";

    columns.push_back( {std::make_pair( ColumnName, ColumnType )} );
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

void SQLiteDB::bind_properties( std::vector<std::map<std::string, std::string>>& columns,
                                std::map<std::string, std::string> properties, sqlite3_stmt*& stmt )
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
            bind( i, boost::lexical_cast<int>( p.second ), stmt );
            ++i;
          }
        }
      }
    }
  }
}

bool SQLiteDB::query_execute( sqlite3_stmt*& stmt )
{
  INFO_PRINT_TRACE( 1 ) << "query_execute(): " << sqlite3_expanded_sql( stmt ) << "\n";
  int rc = sqlite3_step( stmt );
  if ( rc != SQLITE_DONE )
  {
    Finish( stmt );
    return false;
  }
  else if ( sqlite3_changes( gamestate.sqlitedb.db ) == 0 )
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
  using namespace boost;
  int num_updated = 0;
  BeginTransaction();

  for ( const auto& it : gamestate.sqlitedb.modified_storage )
  {
    Clib::PreparePrint pp = it.second;
    // check if StorageArea exists
    // if not, create new StorageArea.
    if ( !ExistInStorage( it.first, gamestate.sqlitedb.stmt_ExistInStorage_AreaName ) )
    {
      AddStorageArea( it.first );
      ++num_updated;
      INFO_PRINT_TRACE( 1 ) << "UpdateDataStorage(): StorageArea " << it.first
                            << " added AddStorageArea() in BD.\n";
    }

    // check if item exists
    // if not, add item.
    if ( !ExistInStorage( lexical_cast<u32>( pp.main["Serial"] ),
                          gamestate.sqlitedb.stmt_ExistInStorage_ItemSerial ) )
    {
      if ( !AddItem( pp, it.first ) )
      {
        ERROR_PRINT << "UpdateDataStorage(): no added in BD.\n";
        RollbackTransaction();
        throw std::runtime_error( "Data file (Storage) integrity error on insert item" );
      }
      ++num_updated;
      INFO_PRINT_TRACE( 1 ) << "UpdateDataStorage(): item " << pp.main["Serial"] << " ("
                            << fmt::hex( lexical_cast<u32>( pp.main["Serial"] ) ) << ")"
                            << " added AddItem() in BD.\n";
    }
    else
    {
      // if yes, update item.
      if ( !UpdateItem( pp, it.first ) )
      {
        RollbackTransaction();
        throw std::runtime_error( "Data file (Storage) integrity error on update item" );
      }
      ++num_updated;
      INFO_PRINT_TRACE( 1 ) << "UpdateDataStorage(): item " << pp.main["Serial"] << " ("
                            << fmt::hex( lexical_cast<u32>( pp.main["Serial"] ) ) << ")"
                            << " updated UpdateItem() in BD.\n";
    }
  }
  EndTransaction();
  std::string sufix_plural = "";
  if ( num_updated > 1 )
    sufix_plural = "s";

  INFO_PRINT << std::to_string( num_updated ) << " item" << sufix_plural
             << " updated into SQLite database.\n";
}

void SQLiteDB::DeleteDataStorage()
{
  int num_removed = 0;
  BeginTransaction();
  for ( unsigned i = 0; i < gamestate.sqlitedb.deleted_storage.size(); ++i )
  {
    if ( !RemoveItem( gamestate.sqlitedb.deleted_storage[i] ) )
    {
      RollbackTransaction();
      throw std::runtime_error( "Data file (Storage) integrity error on remove item" );
    }
    ++num_removed;
  }
  EndTransaction();
  std::string sufix_plural = "";
  if ( num_removed > 1 )
    sufix_plural = "s";

  INFO_PRINT << std::to_string( num_removed ) << " item" << sufix_plural
             << " removed into SQLite database.\n";
}

bool SQLiteDB::CreateDatabase()
{
  INFO_PRINT << " SQLite enabled.\n";
  INFO_PRINT << "\n  data/database.db: NOT FOUND!\n";
  INFO_PRINT << "\nCreating the SQLite database... ";

  int rc = sqlite3_open( gamestate.sqlitedb.dbpath.c_str(), &gamestate.sqlitedb.db );
  if ( rc )
  {
    ERROR_PRINT << "\nSQLiteDB: Can't open database.db: " << sqlite3_errmsg( gamestate.sqlitedb.db )
                << ".\n";
    return false;
  }

  // storage.txt tables
  std::string sqlquery =
      "								\
BEGIN TRANSACTION;										\
CREATE TABLE IF NOT EXISTS 'storage_Item' (             \
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
	REFERENCES 'storage_Area'('AreaId')   \
	ON UPDATE CASCADE ON DELETE CASCADE                 \
);                                                      \
CREATE TABLE IF NOT EXISTS 'storage_Area' (      \
	'AreaId'	INTEGER NOT NULL,                   \
	'Name'	TEXT NOT NULL UNIQUE,                       \
	PRIMARY KEY('AreaId')                        \
);                                                      \
CREATE TABLE IF NOT EXISTS 'storage_Prop' (            \
	'Serial'	INTEGER NOT NULL,                       \
	'PropName'	TEXT,                                   \
	'PropValue'	TEXT,                                   \
	'CProp'	INTEGER DEFAULT 0,                          \
	FOREIGN KEY('Serial')                               \
	REFERENCES 'storage_Item'('Serial')                 \
	ON UPDATE CASCADE ON DELETE CASCADE                 \
);                                                      \
CREATE INDEX IF NOT EXISTS 'storage_Item_Name'          \
ON 'storage_Item' (                                     \
	'Name'	ASC                                         \
);                                                      \
CREATE INDEX IF NOT EXISTS 'storage_Item_Serial'          \
ON 'storage_Item' (                                     \
	'Serial'	ASC                                     \
);                                                      \
CREATE INDEX IF NOT EXISTS 'storage_Item_Container'      \
ON 'storage_Item' (                                     \
	'Container'	ASC                                     \
);                                                      \
CREATE INDEX IF NOT EXISTS 'storage_Prop_Serial'          \
ON 'storage_Prop' (                                     \
	'Serial'	ASC                                     \
);                                                      \
COMMIT;                                                 \
  ";

  char* msgError;
  rc = sqlite3_exec( gamestate.sqlitedb.db, sqlquery.c_str(), NULL, 0, &msgError );
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
    sqlite3_exec( gamestate.sqlitedb.db, "PRAGMA cache_size = 10000", NULL, NULL, NULL );
    sqlite3_exec( gamestate.sqlitedb.db, "PRAGMA foreign_keys = ON", NULL, NULL, NULL );
    sqlite3_exec( gamestate.sqlitedb.db, "PRAGMA journal_mode = DELETE", NULL, NULL, NULL );
    sqlite3_exec( gamestate.sqlitedb.db, "PRAGMA synchronous = FULL", NULL, NULL, NULL );
  }
}

void SQLiteDB::PragmaImport()
{
  if ( Plib::systemstate.config.enable_sqlite )
  {
    sqlite3_exec( gamestate.sqlitedb.db, "PRAGMA foreign_keys = OFF", NULL, NULL, NULL );
    sqlite3_exec( gamestate.sqlitedb.db, "PRAGMA journal_mode = WAL", NULL, NULL, NULL );
    sqlite3_exec( gamestate.sqlitedb.db, "PRAGMA synchronous = NORMAL", NULL, NULL, NULL );
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

void SQLiteDB::BeginTransaction()
{
  if ( Plib::systemstate.config.enable_sqlite )
    sqlite3_exec( gamestate.sqlitedb.db, "BEGIN TRANSACTION", NULL, NULL, NULL );
}

void SQLiteDB::EndTransaction()
{
  if ( Plib::systemstate.config.enable_sqlite )
    sqlite3_exec( gamestate.sqlitedb.db, "END TRANSACTION", NULL, NULL, NULL );
}

void SQLiteDB::RollbackTransaction()
{
  if ( Plib::systemstate.config.enable_sqlite )
    sqlite3_exec( gamestate.sqlitedb.db, "ROLLBACK TRANSACTION", NULL, NULL, NULL );
}

void SQLiteDB::StartPrepStmt()
{
  if ( !( start_stmt_ExistInStorage_Name( storage_Area,
                                          gamestate.sqlitedb.stmt_ExistInStorage_AreaName ) &&
          start_stmt_ExistInStorage_Name( storage_Item,
                                          gamestate.sqlitedb.stmt_ExistInStorage_ItemName ) &&
          start_stmt_ExistInStorage_Serial( storage_Item,
                                            gamestate.sqlitedb.stmt_ExistInStorage_ItemSerial ) &&
          start_stmt_AddStorageArea() &&
          start_stmt_AddStorage( gamestate.sqlitedb.columns_AddStorageItem, storage_Item,
                                 gamestate.sqlitedb.stmt_AddStorageItem ) &&
          start_stmt_AddStorage( gamestate.sqlitedb.columns_AddStorageProp, storage_Prop,
                                 gamestate.sqlitedb.stmt_AddStorageProp ) ) )
  {
    ERROR_PRINT << "Storage: Prepared Statements error!\n";
    throw std::runtime_error( "Storage: Prepared Statements error! StartPrepStmt()" );
  }
}

void SQLiteDB::ListAllStorageItems()
{
  std::string sqlquery = "SELECT Serial FROM storage_Item";

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( gamestate.sqlitedb.db, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    Finish( stmt );
    return;
  }
  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
  {
    gamestate.sqlitedb.all_storage_serials.push_back( sqlite3_column_int( stmt, 0 ) );
  }
  if ( rc != SQLITE_DONE )
  {
    Finish( stmt );
    return;
  }
  Finish( stmt, false );
}

void SQLiteDB::remove_from_list( std::vector<u32>& vec, u32 serial )
{
  vec.erase( std::remove( vec.begin(), vec.end(), serial ), vec.end() );
}

void SQLiteDB::find_modified_storage_items( Clib::PreparePrint& pp, std::string areaName )
{
  using namespace fmt;
  using namespace boost;

  if ( pp.internal["SAVE_ON_EXIT"] )
  {
    if ( !pp.internal["ORPHAN"] && pp.internal["DIRTY"] )
    {
      gamestate.sqlitedb.modified_storage.insert( make_pair( areaName, pp ) );

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
      gamestate.sqlitedb.deleted_storage.push_back(
          lexical_cast<u32>( pp.internal["SERIAL_EXT"] ) );

      INFO_PRINT_TRACE( 1 ) << "deleted_storage (ORPHAN): " << pp.internal["SERIAL_EXT"] << " (0x"
                            << hex( lexical_cast<u32>( pp.internal["SERIAL_EXT"] ) ) << ")"
                            << " areaName: " << areaName << ".\n";
    }
  }
  else
  {
    // if SaveOnExit false, so remove from DB if it was saved before.
    gamestate.sqlitedb.deleted_storage.push_back( lexical_cast<u32>( pp.main["Serial"] ) );

    INFO_PRINT_TRACE( 1 ) << "deleted_storage (SAVE_ON_EXIT): " << pp.main["Serial"] << " (0x"
                          << hex( lexical_cast<u32>( pp.main["Serial"] ) ) << ")"
                          << " areaName: " << areaName << ".\n";
  }
}

void SQLiteDB::find_deleted_storage_items()
{
  using namespace fmt;
  using namespace boost;

  for ( auto serial : gamestate.sqlitedb.all_storage_serials )
  {
    // if found, the item was moved to another txt data
    Items::Item* item = system_find_item( serial );
    if ( item != nullptr )
    {
      gamestate.sqlitedb.deleted_storage.push_back( item->serial );

      INFO_PRINT_TRACE( 1 ) << "find_deleted_storage_items (moved): " << item->serial << " (0x"
                            << hex( lexical_cast<u32>( item->serial ) ) << ")"
                            << ".\n";
      continue;
    }

    // found orphan item to another txt data
    UObject* obj = objStorageManager.objecthash.Find( serial );
    if ( obj != nullptr && obj->isitem() && obj->orphan() )
    {
      gamestate.sqlitedb.deleted_storage.push_back( serial );

      INFO_PRINT_TRACE( 1 ) << "find_deleted_storage_items (orphan): " << serial << " (0x"
                            << hex( lexical_cast<u32>( serial ) ) << ")"
                            << ".\n";
    }
  }
}

void SQLiteDB::DropIndexes()
{
  if ( !Plib::systemstate.config.enable_sqlite )
    return;

  std::string sqlquery =
      "								\
BEGIN TRANSACTION;										\
DROP INDEX IF EXISTS 'storage_Item_Name';          \
DROP INDEX IF EXISTS 'storage_Item_Serial';          \
DROP INDEX IF EXISTS 'storage_Item_Container';          \
DROP INDEX IF EXISTS 'storage_Prop_Serial';          \
COMMIT;                                                 \
  ";

  sqlite3_exec( gamestate.sqlitedb.db, sqlquery.c_str(), NULL, NULL, NULL );
}

void SQLiteDB::CreateIndexes()
{
  if ( !Plib::systemstate.config.enable_sqlite )
    return;

  std::string sqlquery =
      "								\
BEGIN TRANSACTION;										\
CREATE INDEX IF NOT EXISTS 'storage_Item_Name'          \
ON 'storage_Item' (                                     \
	'Name'	ASC                                         \
);                                                      \
CREATE INDEX IF NOT EXISTS 'storage_Item_Serial'          \
ON 'storage_Item' (                                     \
	'Serial'	ASC                                     \
);                                                      \
CREATE INDEX IF NOT EXISTS 'storage_Item_Container'      \
ON 'storage_Item' (                                     \
	'Container'	ASC                                     \
);                                                      \
CREATE INDEX IF NOT EXISTS 'storage_Prop_Serial'          \
ON 'storage_Prop' (                                     \
	'Serial'	ASC                                     \
);                                                      \
COMMIT;                                                 \
  ";

  sqlite3_exec( gamestate.sqlitedb.db, sqlquery.c_str(), NULL, NULL, NULL );
}

}  // namespace Core
}  // namespace Pol
