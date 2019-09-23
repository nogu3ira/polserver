/** @file
 *
 * @par History
 * - 2005/11/26 Shinigami: changed "strcmp" into "stricmp" to suppress Script Errors
 */


#include "storage.h"

#include <exception>
#include <string>
#include <time.h>

#include "../bscript/berror.h"
#include "../bscript/bobject.h"
#include "../bscript/contiter.h"
#include "../bscript/impstr.h"
#include "../clib/cfgelem.h"
#include "../clib/cfgfile.h"
#include "../clib/clib.h"
#include "../clib/logfacility.h"
#include "../clib/rawtypes.h"
#include "../clib/streamsaver.h"
#include "../plib/poltype.h"
#include "../plib/systemstate.h"
#include "../sqlite-amalgamation-3290000/sqlite3.h"
#include "containr.h"
#include "fnsearch.h"
#include "globals/object_storage.h"
#include "globals/uvars.h"
#include "item/item.h"
#include "loaddata.h"
#include "mkscrobj.h"
#include "polcfg.h"
#include "ufunc.h"

namespace Pol
{
namespace Core
{
using namespace Bscript;

StorageArea::StorageArea( std::string name ) : _name( name ) {}

StorageArea::~StorageArea()
{
  while ( !_items.empty() )
  {
    Cont::iterator itr = _items.begin();
    Items::Item* item = ( *itr ).second;
    item->destroy();
    _items.erase( itr );
  }
}

size_t StorageArea::estimateSize() const
{
  size_t size = _name.capacity();
  for ( const auto& item : _items )
    size += item.first.capacity() + sizeof( Items::Item* ) + ( sizeof( void* ) * 3 + 1 ) / 2;
  return size;
}


Items::Item* StorageArea::find_root_item( const std::string& name )
{
  // LINEAR_SEARCH
  Cont::iterator itr = _items.find( name );
  if ( itr != _items.end() )
  {
    return ( *itr ).second;
  }
  return nullptr;
}

bool StorageArea::delete_root_item( const std::string& name )
{
  Cont::iterator itr = _items.find( name );
  if ( itr != _items.end() )
  {
    Items::Item* item = ( *itr ).second;
    item->destroy();
    _items.erase( itr );
    return true;
  }
  return false;
}

void StorageArea::insert_root_item( Items::Item* item )
{
  item->inuse( true );

  _items.insert( make_pair( item->name(), item ) );
}

extern Items::Item* read_item( Clib::ConfigElem& elem );  // from UIMPORT.CPP

void StorageArea::load_item( Clib::ConfigElem& elem )
{
  // if this object is modified in a subsequent incremental save,
  // don't load it yet.
  pol_serial_t serial = 0;
  elem.get_prop( "SERIAL", &serial );
  if ( get_save_index( serial ) > objStorageManager.current_incremental_save )
    return;

  u32 container_serial = 0;                                  // defaults to item at storage root,
  (void)elem.remove_prop( "CONTAINER", &container_serial );  // so the return value can be ignored

  Items::Item* item = read_item( elem );
  // Austin added 8/10/2006, protect against further crash if item is null. Should throw instead?
  if ( item == nullptr )
  {
    elem.warn_with_line( "Error reading item SERIAL or OBJTYPE." );
    return;
  }
  if ( container_serial == 0 )
  {
    insert_root_item( item );
  }
  else
  {
    Items::Item* cont_item = Core::system_find_item( container_serial );

    if ( cont_item )
    {
      add_loaded_item( cont_item, item );
    }
    else
    {
      defer_item_insertion( item, container_serial );
    }
  }
}

StorageArea* Storage::find_area( const std::string& name )
{
  AreaCont::iterator itr = areas.find( name );
  if ( itr == areas.end() )
  {
    ERROR_PRINT << "no found in areas.\n";

    if ( Plib::systemstate.config.enable_sqlite )
    {
      if ( !Storage::SQLite_ExistStorageArea( name ) )
      {
        ERROR_PRINT << "no found in BD.\n";
        return nullptr;
      }
      else
      {
        ERROR_PRINT << "yes found in BD.\n";
        return Storage::create_areaCache( name );
      }
    }
    else
    {
      return nullptr;
    }
  }
  else
  {
    ERROR_PRINT << "yes found in areas.\n";
    return ( *itr ).second;
  }
}

static int SQLite_callback( void* NotUsed, int count, char** rowdata, char** columns )
{
  NotUsed;
  int i;
  for ( i = 0; i < count; i++ )
  {
    ERROR_PRINT << "Storage result: " << columns[i] << " = " << rowdata[i] << "\n";
  }
  ERROR_PRINT << "Storage ended.\n";
  return 0;
}

void Storage::SQLite_Connect()
{
  std::string dbpath = Plib::systemstate.config.world_data_path + "storage.db";
  int rc = sqlite3_open( dbpath.c_str(), &SQLiteDB );
  if ( rc )
  {
    ERROR_PRINT << "Storage: Can't open database: " << sqlite3_errmsg( SQLiteDB ) << ".\n";
    sqlite3_close( SQLiteDB );
    throw std::runtime_error( "Storage: Can't open database " + dbpath );
  }
}

void Storage::SQLite_finish( sqlite3_stmt* stmt, int x = 1 )
{
  if (x)
  {
	ERROR_PRINT << "Storage: " << sqlite3_errmsg( SQLiteDB ) << ".\n";
  }
  sqlite3_finalize( stmt );
  sqlite3_close( SQLiteDB );
}

bool Storage::SQLite_ExistStorageArea( const std::string& name )
{
  int result = 0;
  Storage::SQLite_Connect();

  std::string sqlquery =
      "SELECT EXISTS(SELECT 1 FROM StorageArea WHERE Name='" + name + "' LIMIT 1) AS result";

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( SQLiteDB, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    Storage::SQLite_finish( stmt );
    return false;
  }
  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
  {
    result = sqlite3_column_int( stmt, 0 );
  }
  if ( rc != SQLITE_DONE )
  {
    Storage::SQLite_finish( stmt );
    return false;
  }
  Storage::SQLite_finish( stmt, 0 );
  return ( result == 1 );
}

void Storage::SQLite_ListStorageAreas()
{
  Storage::SQLite_Connect();

  std::string sqlquery = "SELECT Name FROM StorageArea";

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( SQLiteDB, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    Storage::SQLite_finish( stmt );
    return;
  }
  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
  {
    std::string Name =
        std::string( reinterpret_cast<const char*>( sqlite3_column_text( stmt, 0 ) ) );
    Storage::create_areaCache( Name );
  }
  if ( rc != SQLITE_DONE )
  {
    Storage::SQLite_finish( stmt );
    return;
  }
  Storage::SQLite_finish( stmt, 0 );
}

void Storage::SQLite_AddStorageArea( const std::string& name )
{
  Storage::SQLite_Connect();

  std::string sqlquery =
      "INSERT INTO StorageArea (Name) "
      "VALUES('" +
      name + "' )";

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( SQLiteDB, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    Storage::SQLite_finish( stmt );
    return;
  }
  rc = sqlite3_step( stmt );

  if ( rc != SQLITE_DONE )
  {
    Storage::SQLite_finish( stmt );
    return;
  }
  else if ( sqlite3_changes( SQLiteDB ) == 0 )
  {
    ERROR_PRINT << "Storage: No data inserted.\n";
    Storage::SQLite_finish( stmt );
    return;
  }
  Storage::SQLite_finish( stmt, 0 );
}

StorageArea* Storage::create_area( const std::string& name )
{
  AreaCont::iterator itr = areas.find( name );
  if ( itr == areas.end() )
  {
    ERROR_PRINT << "no found in areas.\n";

    if ( Plib::systemstate.config.enable_sqlite )
    {
      if ( !Storage::SQLite_ExistStorageArea( name ) )
      {
        ERROR_PRINT << "no found in BD.\n";
        // Create into DB
        Storage::SQLite_AddStorageArea( name );
        // Add into AreaCont
        return Storage::create_areaCache( name );
      }
      else
      {
        ERROR_PRINT << "yes found in BD.\n";
        // Add into AreaCont
        return Storage::create_areaCache( name );
      }
    }

    StorageArea* area = new StorageArea( name );
    areas[name] = area;
    return area;
  }
  else
  {
    return ( *itr ).second;
  }
}

StorageArea* Storage::create_areaCache( const std::string& name )
{
  AreaCont::iterator itr = areas.find( name );
  if ( itr == areas.end() )
  {
    StorageArea* area = new StorageArea( name );
    areas[name] = area;
    return area;
  }
  else
  {
    return ( *itr ).second;
  }
}

StorageArea* Storage::create_area( Clib::ConfigElem& elem )
{
  const char* rest = elem.rest();
  if ( rest != nullptr && rest[0] )
  {
    return create_area( rest );
  }
  else
  {
    std::string name = elem.remove_string( "NAME" );
    return create_area( name );
  }
}


void StorageArea::print( Clib::StreamWriter& sw ) const
{
  for ( const auto& cont_item : _items )
  {
    const Items::Item* item = cont_item.second;
    if ( item->saveonexit() )
      sw << *item;
  }
}

void StorageArea::on_delete_realm( Realms::Realm* realm )
{
  for ( Cont::const_iterator itr = _items.begin(), itrend = _items.end(); itr != itrend; ++itr )
  {
    Items::Item* item = ( *itr ).second;
    if ( item )
    {
      setrealmif( item, (void*)realm );
      if ( item->isa( UOBJ_CLASS::CLASS_CONTAINER ) )
      {
        UContainer* cont = static_cast<UContainer*>( item );
        cont->for_each_item( setrealmif, (void*)realm );
      }
    }
  }
}

void Storage::on_delete_realm( Realms::Realm* realm )
{
  for ( AreaCont::const_iterator itr = areas.begin(), itrend = areas.end(); itr != itrend; ++itr )
  {
    itr->second->on_delete_realm( realm );
  }
}

void Storage::read( Clib::ConfigFile& cf )
{
  static int num_until_dot = 1000;
  unsigned int nobjects = 0;

  StorageArea* area = nullptr;
  Clib::ConfigElem elem;

  clock_t start = clock();

  while ( cf.read( elem ) )
  {
    if ( --num_until_dot == 0 )
    {
      INFO_PRINT << ".";
      num_until_dot = 1000;
    }
    if ( elem.type_is( "StorageArea" ) )
    {
      area = create_area( elem );
    }
    else if ( elem.type_is( "Item" ) )
    {
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
        ERROR_PRINT << "Storage: Got an ITEM element, but don't have a StorageArea to put it.\n";
        throw std::runtime_error( "Data file integrity error" );
      }
    }
    else
    {
      ERROR_PRINT << "Unexpected element type " << elem.type() << " in storage file.\n";
      throw std::runtime_error( "Data file integrity error" );
    }
    ++nobjects;
  }

  clock_t end = clock();
  int ms = static_cast<int>( ( end - start ) * 1000.0 / CLOCKS_PER_SEC );

  INFO_PRINT << " " << nobjects << " elements in " << ms << " ms.\n";
}

void Storage::print( Clib::StreamWriter& sw ) const
{
  for ( const auto& area : areas )
  {
    sw() << "StorageArea" << '\n'
         << "{" << '\n'
         << "\tName\t" << area.first << '\n'
         << "}" << '\n'
         << '\n';
    area.second->print( sw );
    sw() << '\n';
  }
}

void Storage::clear()
{
  while ( !areas.empty() )
  {
    delete ( ( *areas.begin() ).second );
    areas.erase( areas.begin() );
  }
}

size_t Storage::estimateSize() const
{
  size_t size = 0;
  for ( const auto& area : areas )
  {
    size += area.first.capacity() + ( sizeof( void* ) * 3 + 1 ) / 2;
    if ( area.second != nullptr )
      size += area.second->estimateSize();
  }
  return size;
}

class StorageAreaIterator final : public ContIterator
{
public:
  StorageAreaIterator( StorageArea* area, BObject* pIter );
  virtual BObject* step() override;

private:
  BObject* m_pIterVal;
  std::string key;
  StorageArea* _area;
};

StorageAreaIterator::StorageAreaIterator( StorageArea* area, BObject* pIter )
    : ContIterator(), m_pIterVal( pIter ), key( "" ), _area( area )
{
}

BObject* StorageAreaIterator::step()
{
  StorageArea::Cont::iterator itr = _area->_items.lower_bound( key );
  if ( !key.empty() && itr != _area->_items.end() )
  {
    ++itr;
  }

  if ( itr == _area->_items.end() )
    return nullptr;

  key = ( *itr ).first;
  m_pIterVal->setimp( new String( key ) );
  BObject* result = new BObject( make_itemref( ( *itr ).second ) );
  return result;
}


class StorageAreaImp final : public BObjectImp
{
public:
  StorageAreaImp( StorageArea* area ) : BObjectImp( BObjectImp::OTUnknown ), _area( area ) {}
  virtual BObjectImp* copy() const override { return new StorageAreaImp( _area ); }
  virtual std::string getStringRep() const override { return _area->_name; }
  virtual size_t sizeEstimate() const override { return sizeof( *this ); }
  ContIterator* createIterator( BObject* pIterVal ) override
  {
    return new StorageAreaIterator( _area, pIterVal );
  }

  BObjectRef get_member( const char* membername ) override;

private:
  StorageArea* _area;
};
BObjectRef StorageAreaImp::get_member( const char* membername )
{
  if ( stricmp( membername, "count" ) == 0 )
  {
    return BObjectRef( new BLong( static_cast<int>( _area->_items.size() ) ) );
  }
  else if ( stricmp( membername, "totalcount" ) == 0 )
  {
    unsigned int total = 0;
    for ( StorageArea::Cont::iterator itr = _area->_items.begin(); itr != _area->_items.end();
          ++itr )
    {
      Items::Item* item = ( *itr ).second;
      total += item->item_count();
    }
    return BObjectRef( new BLong( total ) );
  }
  return BObjectRef( UninitObject::create() );
}


class StorageAreasIterator final : public ContIterator
{
public:
  StorageAreasIterator( BObject* pIter );
  virtual BObject* step() override;

private:
  BObject* m_pIterVal;
  std::string key;
};

StorageAreasIterator::StorageAreasIterator( BObject* pIter )
    : ContIterator(), m_pIterVal( pIter ), key( "" )
{
}

BObject* StorageAreasIterator::step()
{
  Storage::AreaCont::iterator itr = gamestate.storage.areas.lower_bound( key );
  if ( !key.empty() && itr != gamestate.storage.areas.end() )
  {
    ++itr;
  }

  if ( itr == gamestate.storage.areas.end() )
    return nullptr;

  key = ( *itr ).first;
  m_pIterVal->setimp( new String( key ) );
  BObject* result = new BObject( new StorageAreaImp( ( *itr ).second ) );
  return result;
}

class StorageAreasImp final : public BObjectImp
{
public:
  StorageAreasImp() : BObjectImp( BObjectImp::OTUnknown ) {}
  virtual BObjectImp* copy() const override { return new StorageAreasImp(); }
  virtual std::string getStringRep() const override { return "<StorageAreas>"; }
  virtual size_t sizeEstimate() const override { return sizeof( *this ); }
  ContIterator* createIterator( BObject* pIterVal ) override
  {
    return new StorageAreasIterator( pIterVal );
  }

  BObjectRef get_member( const char* membername ) override;

  BObjectRef OperSubscript( const BObject& obj ) override;
};

BObjectImp* CreateStorageAreasImp()
{
  return new StorageAreasImp();
}

BObjectRef StorageAreasImp::get_member( const char* membername )
{
  if ( stricmp( membername, "count" ) == 0 )
  {
    if ( Plib::systemstate.config.enable_sqlite )
    {
      gamestate.storage.SQLite_ListStorageAreas();
    }
    return BObjectRef( new BLong( static_cast<int>( gamestate.storage.areas.size() ) ) );
  }
  return BObjectRef( UninitObject::create() );
}
BObjectRef StorageAreasImp::OperSubscript( const BObject& obj )
{
  if ( obj.isa( OTString ) )
  {
    String& rtstr = (String&)obj.impref();
    std::string key = rtstr.value();

    if ( Plib::systemstate.config.enable_sqlite )
    {
      gamestate.storage.SQLite_ListStorageAreas();
    }

    Storage::AreaCont::iterator itr = gamestate.storage.areas.find( key );
    if ( itr != gamestate.storage.areas.end() )
    {
      return BObjectRef( new BObject( new StorageAreaImp( ( *itr ).second ) ) );
    }
    else
    {
      return BObjectRef( new BObject( new BError( "Storage Area not found" ) ) );
    }
  }
  return BObjectRef( new BObject( new BError( "Invalid parameter type" ) ) );
}
}  // namespace Core
}  // namespace Pol
