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
#include "polcfg.h"
#include "ufunc.h"
#include "objtype.h"

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

  if ( Plib::systemstate.config.enable_sqlite )
  {
    if ( gamestate.sqlitedb.ExistInStorage( name,
                                            gamestate.sqlitedb.stmt_ExistInStorage_ItemName ) )
    {
      INFO_PRINT_TRACE( 1 ) << "find_root_item: yes found in BD. Name: " << name << "\n";
      u32 root_item_serial = create_root_item( name );
      gamestate.storage.load_items( root_item_serial );

      itr = _items.find( name );
      if ( itr != _items.end() )
      {
        return ( *itr ).second;
      }
      return nullptr;
    }
    INFO_PRINT_TRACE( 1 ) << "find_root_item: no found in BD. Name: " << name << "\n";
  }
  return nullptr;
}

std::vector<Items::Item*> StorageArea::find_items_filters( const std::string& filters,
                                                           std::string& err_msg )
{
  std::vector<Items::Item*> items;
  std::vector<u32> serials;

  if (!Plib::systemstate.config.enable_sqlite)
  {
    err_msg = "SQLite is not enabled.";
    return items;
  }

  if ( !gamestate.sqlitedb.GetItemCustomFilter( filters, serials, _name, err_msg ) )
    return items;

  for ( const auto& serial : serials )
  {
    Items::Item* item = system_find_item( serial );
	if ( item == nullptr )
	{
      gamestate.sqlitedb.load_toplevel_owner( serial );

      item = system_find_item( serial );
      if ( item != nullptr )
        items.push_back( item );
	}
	else
	{
      items.push_back( item );
	}
  }

  return items;
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
  // TODO: Pehaps need to check when false and root_item exists in SQLite database
  // when it isn't loaded into memory???
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
  INFO_PRINT_TRACE( 1 ) << "load_item: serial = " << serial << "\n";
  if ( get_save_index( serial ) > objStorageManager.current_incremental_save )
    return;

  u32 container_serial = 0;                                  // defaults to item at storage root,
  (void)elem.remove_prop( "CONTAINER", &container_serial );  // so the return value can be ignored

  INFO_PRINT_TRACE( 1 ) << "load_item: container_serial = " << container_serial << "\n";
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
    INFO_PRINT_TRACE( 1 ) << "load_item: added root item " << item->serial << "\n";
  }
  else
  {
    Items::Item* cont_item = Core::system_find_item( container_serial );

    if ( cont_item )
    {
      add_loaded_item( cont_item, item );
      INFO_PRINT_TRACE( 1 ) << "load_item: added Item " << item->serial
                            << " in Container: " << cont_item->serial << "\n";
    }
    else
    {
      defer_item_insertion( item, container_serial );
      INFO_PRINT_TRACE( 1 ) << "load_item: defer_item_insertion " << item->serial
                            << " to Container: " << container_serial << "\n";
    }
  }
}

void StorageArea::load_item_file( Clib::ConfigElem& elem )
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
    // this is a root container.
    gamestate.sqlitedb.insert_root_item( item, _name );
    insert_root_item( item );
  }
  else
  {
    // this is an item inside a container
    gamestate.sqlitedb.insert_item( item, _name, container_serial );
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
    INFO_PRINT_TRACE( 1 ) << "find_area: no found in areas. Name: " << name << "\n";

    if ( Plib::systemstate.config.enable_sqlite )
    {
      if ( !gamestate.sqlitedb.ExistInStorage( name,
                                               gamestate.sqlitedb.stmt_ExistInStorage_AreaName ) )
      {
        INFO_PRINT_TRACE( 1 ) << "find_area: no found in BD. Name: " << name << "\n";
        return nullptr;
      }
      else
      {
        INFO_PRINT_TRACE( 1 ) << "find_area: yes found in BD. Name: " << name << "\n";
        return Storage::create_area( name );
      }
    }
    else
    {
      return nullptr;
    }
  }
  else
  {
    INFO_PRINT_TRACE( 1 ) << "find_area: yes found in areas. Name: " << name << "\n";
    return ( *itr ).second;
  }
}

u32 StorageArea::create_root_item( const std::string& name )
{
  INFO_PRINT_TRACE( 1 ) << "create_root_item: trying to create the item " << name << "\n";
  // Get item from SQLite DB, transform to Item ref and add item in memory
  return gamestate.sqlitedb.read_item( name );
}

StorageArea* Storage::create_area( const std::string& name )
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

// Add StorageArea in SQLite database and in memory
StorageArea* Storage::create_area_file( Clib::ConfigElem& elem )
{
  std::string areaName;
  const char* rest = elem.rest();
  if ( rest != nullptr && rest[0] )
  {
    areaName = rest;
  }
  else
  {
    std::string name = elem.remove_string( "NAME" );
    areaName = name;
  }

  if ( Plib::systemstate.config.enable_sqlite )
  {
    if ( !gamestate.sqlitedb.ExistInStorage( areaName,
                                             gamestate.sqlitedb.stmt_ExistInStorage_AreaName ) )
    {
      INFO_PRINT_TRACE( 1 ) << "create_area: no found in BD. Creating into DB.\n";
      // Create into DB
      gamestate.sqlitedb.AddStorageArea( areaName );
      return create_area( areaName );
    }
    else
    {
      ERROR_PRINT << "Duplicate StorageArea read from datafiles. Name: " << areaName << "\n";
      throw std::runtime_error( "Data integrity error" );
      return nullptr;
    }
  }

  return create_area( areaName );
}

void StorageArea::print( Clib::vecPreparePrint& vpp ) const
{
  for ( const auto& cont_item : _items )
  {
    const Items::Item* item = cont_item.second;
    vpp << *item;
    item->clear_dirty();
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

  gamestate.sqlitedb.Connect();
  gamestate.sqlitedb.DropIndexes();
  gamestate.sqlitedb.PragmaImport();
  gamestate.sqlitedb.BeginTransaction();

  if ( Plib::systemstate.config.enable_sqlite )
    INFO_PRINT << "\nStarting import into the database: ";

  while ( cf.read( elem ) )
  {
    if ( --num_until_dot == 0 )
    {
      INFO_PRINT << ".";
      num_until_dot = 1000;
    }
    if ( elem.type_is( "StorageArea" ) )
    {
      area = create_area_file( elem );
    }
    else if ( elem.type_is( "Item" ) )
    {
      if ( area != nullptr )
      {
        try
        {
          area->load_item_file( elem );
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

  INFO_PRINT << "\nDone!\n";

  gamestate.sqlitedb.EndTransaction();
  gamestate.sqlitedb.PragmaSettings();
  gamestate.sqlitedb.CreateIndexes();

  clock_t end = clock();
  int ms = static_cast<int>( ( end - start ) * 1000.0 / CLOCKS_PER_SEC );

  INFO_PRINT << " " << nobjects << " elements in " << ms << " ms.\n";
}

// Load the contents of a container from SQLite database
void Storage::load_items( const u32 container_serial )
{
  if ( gamestate.sqlitedb.ExistInStorage( container_serial,
                                          gamestate.sqlitedb.stmt_ExistInStorage_ItemSerial ) )
    gamestate.sqlitedb.read_items_in_container( container_serial );
}

// Print StorageArea and Items from SQL
void Storage::print() const
{
  // Save the areaname and vector with itemprops 
  std::map<std::string, Clib::vecPreparePrint> AreasPreparePrints;
  for (const auto& area : areas)
  {
    Clib::vecPreparePrint vpp;
    area.second->print( vpp );
    AreasPreparePrints.insert( make_pair( area.first, vpp ) );
  }

  // Checking in Storage Database if item was moved/deleted from there.

  // Step 1:
  // Create a list all_storage_serials with all items (serials) 
  // in database sqlite (array of serials);
  gamestate.sqlitedb.ListAllStorageItems();

  for ( const auto& areapp : AreasPreparePrints )
  {
    std::string areaName = areapp.first;
    for ( auto pp : areapp.second.v )
    {
      // Step 2:
      // All found items in storage (root item and container item),
      // i'm removing from the list all_storage_serials;
      gamestate.sqlitedb.remove_from_list( gamestate.sqlitedb.all_storage_serials,
                                           boost::lexical_cast<u32>( pp.main["Serial"] ) );
      // Step 3:
	  // check if item was changed
	  gamestate.sqlitedb.find_modified_storage_items( pp, areaName );
    }
  }

  // There will be left over items that:
  // 	-> have been moved to another .txt file or
  // 	-> have been removed or
  // 	-> they just weren't loaded into memory.

  // Step 3:
  // Use system_find_item( serial ). If found, it's because it went to another txt file.
  // Remove item from DB.

  // Step 4:
  // If found and are orphan, the item will be destroyed.
  // Remove item from DB.

  gamestate.sqlitedb.find_deleted_storage_items();

  // Step 5:
  // The rest means that they were not loaded into memory. Do nothing.

  // Step 6:
  // Complete check.
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

void Storage::commit_sqlitedb()
{
  gamestate.sqlitedb.UpdateDataStorage();
  gamestate.sqlitedb.DeleteDataStorage();
  gamestate.sqlitedb.modified_storage.clear();
  gamestate.sqlitedb.deleted_storage.clear();
  gamestate.sqlitedb.all_storage_serials.clear();
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
      gamestate.sqlitedb.ListStorageAreas();
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
      gamestate.sqlitedb.ListStorageAreas();
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
