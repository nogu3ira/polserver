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
    if ( gamestate.sqlitedb.ExistInStorage( name, gamestate.sqlitedb.table_Item ) )
    {
      ERROR_PRINT << "yes found in BD.\n";
      StorageArea::create_ItemCache( name );

      itr = _items.find( name );
      if ( itr != _items.end() )
      {
        return ( *itr ).second;
      }
      return nullptr;
    }
    ERROR_PRINT << "no found in BD.\n";
  }
  return nullptr;
}

bool StorageArea::delete_root_item( const std::string& name )
{
  Cont::iterator itr = _items.find( name );
  if ( itr != _items.end() )
  {
    if ( Plib::systemstate.config.enable_sqlite )
    {
      if ( !gamestate.sqlitedb.RemoveItem( name ) )
      {
        ERROR_PRINT << "no deleted in BD.\n";
        return false;
      }
      ERROR_PRINT << "yes deleted in BD.\n";
    }
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

void StorageArea::load_item( Clib::ConfigElem& elem, const std::string& areaName )
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
    // this is a container.
    if ( Plib::systemstate.config.enable_sqlite )
    {
      if ( !gamestate.sqlitedb.ExistInStorage( item->name(), gamestate.sqlitedb.table_Item ) )
      {
        gamestate.sqlitedb.insert_root_item( item, areaName );
      }
    }
    else
    {
      insert_root_item( item );
    }
  }
  else
  {
    if ( Plib::systemstate.config.enable_sqlite )
    {
      if ( gamestate.sqlitedb.ExistInStorage( container_serial, gamestate.sqlitedb.table_Item ) )
      {
        gamestate.sqlitedb.insert_root_item( item, areaName );
      }
      else
      {
        // TODO!
        // CHECK THIS FUNCTION BELOW!

        // When does deferred insertions run?
        // defer_item_insertion( item, container_serial );
      }
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
}

StorageArea* Storage::find_area( const std::string& name )
{
  AreaCont::iterator itr = areas.find( name );
  if ( itr == areas.end() )
  {
    ERROR_PRINT << "no found in areas.\n";

    if ( Plib::systemstate.config.enable_sqlite )
    {
      if ( !gamestate.sqlitedb.ExistInStorage( name, gamestate.sqlitedb.table_StorageArea ) )
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

void StorageArea::create_ItemCache( const std::string& name )
{
  // Get item from SQLite DB
  // and transform to Item ref
  Items::Item* temp_item = gamestate.sqlitedb.read_item( name );

  // Add item in memory
  insert_root_item( temp_item );
}

StorageArea* Storage::create_area( const std::string& name )
{
  AreaCont::iterator itr = areas.find( name );
  if ( itr == areas.end() )
  {
    ERROR_PRINT << "no found in areas.\n";

    if ( Plib::systemstate.config.enable_sqlite )
    {
      if ( !gamestate.sqlitedb.ExistInStorage( name, gamestate.sqlitedb.table_StorageArea ) )
      {
        ERROR_PRINT << "no found in BD.\n";
        // Create into DB
        gamestate.sqlitedb.AddStorageArea( name );
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

std::string Storage::get_area_name( Clib::ConfigElem& elem )
{
  const char* rest = elem.rest();
  if ( rest != nullptr && rest[0] )
  {
    return rest;
  }
  else
  {
    std::string name = elem.remove_string( "NAME" );
    return name;
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
  std::string areaName = "";

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
      areaName = get_area_name( elem );
    }
    else if ( elem.type_is( "Item" ) )
    {
      if ( area != nullptr )
      {
        try
        {
          area->load_item( elem, areaName );
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
  if ( Plib::systemstate.config.enable_sqlite )
  {
    gamestate.sqlitedb.Close();
    gamestate.sqlitedb.Connect();
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
