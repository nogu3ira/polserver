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

void StorageArea::SQLite_insert_root_item_onlyDB( Items::Item* item, const std::string& areaName )
{
  if ( Plib::systemstate.config.enable_sqlite )
  {
    if ( !gamestate.sqlitedb.AddItem( item, areaName ) )
    {
      ERROR_PRINT << "no added in BD.\n";
    }
    ERROR_PRINT << "yes added in BD.\n";
  }
}

struct ItemInfoDB
{
  int ItemId, StorageAreaId, Serial, ObjType, Graphic, Color, X, Y, Z, Facing, Revision, Amount,
      Layer, Movable, Invisible, Container, DecayAt, SellPrice, BuyPrice, Newbie, Insured,
      FireResist, ColdResist, EnergyResist, PoisonResist, PhysicalResist, FireDamage, ColdDamage,
      EnergyDamage, PoisonDamage, PhysicalDamage, LowerReagentCost, SpellDamageIncrease,
      FasterCasting, FasterCastRecovery, DefenceIncrease, DefenceIncreaseCap, LowerManaCost,
      FireResistCap, ColdResistCap, EnergyResistCap, PhysicalResistCap, PoisonResistCap, Luck,
      MaxHp_mod, Hp, Quality, NoDrop, FireResistMod, ColdResistMod, EnergyResistMod,
      PoisonResistMod, PhysicalResistMod, FireDamageMod, ColdDamageMod, EnergyDamageMod,
      PoisonDamageMod, PhysicalDamageMod, LowerReagentCostMod, DefenceIncreaseMod,
      DefenceIncreaseCapMod, LowerManaCostMod, HitChanceMod, FireResistCapMod, ColdResistCapMod,
      EnergyResistCapMod, PhysicalResistCapMod, PoisonResistCapMod, SpellDamageIncreaseMod,
      FasterCastingMod, FasterCastRecoveryMod, LuckMod;
  std::string Name, Realm, OnUseScript, EquipScript, UnequipScript, NameSuffix;
};

Items::Item* StorageArea::read_itemInDB( const std::string& name )
{
  ItemInfoDB* iteminfo;
  gamestate.sqlitedb.GetItem( name, iteminfo );
  return StorageArea::read_item_struct( iteminfo );
}

// read item from SQLite Database
Items::Item* StorageArea::read_item_struct( struct ItemInfoDB* i )
{
  u32 serial = i->Serial;
  u32 objtype = i->ObjType;
  if ( serial == 0 )
  {
    ERROR_PRINT << "Item element has no SERIAL property, omitting.\n";
    return nullptr;
  }

  if ( !IsItem( serial ) )
  {
    ERROR_PRINT
        << "Item element does not have an item serial\n(should be larger than 0x40000000)\n";
    if ( Plib::systemstate.config.check_integrity )
    {
      throw std::runtime_error( "Data integrity error" );
    }
    return nullptr;
  }

  if ( Plib::systemstate.config.check_integrity )
  {
    if ( system_find_item( serial ) )
    {
      ERROR_PRINT.Format( "Duplicate item read from datafiles (Serial=0x{:X})\n" ) << serial;
      throw std::runtime_error( "Data integrity error" );
    }
  }
  if ( objtype == 0 )
  {
    ERROR_PRINT.Format( "Item (Serial 0x{:X}) has no OBJTYPE property, omitting." ) << serial;
    return nullptr;
  }
  if ( gamestate.old_objtype_conversions.count( objtype ) )
    objtype = gamestate.old_objtype_conversions[objtype];

  Items::Item* item = Items::Item::create( objtype, serial );
  if ( item == nullptr )
  {
    ERROR_PRINT.Format( "Unable to create item: objtype=0x{:X}, serial=0x{:X}" )
        << objtype << serial;
    if ( !Plib::systemstate.config.ignore_load_errors )
      throw std::runtime_error( "Item::create failed!" );
    else
      return nullptr;
  }
  item->realm = find_realm( "britannia" );

  //------------------------------->
  //base::readProperties( elem );

  item->name_ = i->Name;

  if ( static_cast<u16>( i->Graphic ) > ( Plib::systemstate.config.max_tile_id ) )
  {
    item->graphic = GRAPHIC_NODRAW;
  }
  else
  {
    item->graphic = static_cast<u16>( i->Graphic );
  }

  item->color = static_cast<u16>( i->Color );

  item->realm = find_realm( i->Realm );
  if ( !item->realm )
  {
    ERROR_PRINT.Format( "'{}' (0x{:X}): has an invalid realm property '{}'.\n" )
        << item->name_ << serial << i->Realm;
    throw std::runtime_error( "Data integrity error" );
  }
  item->x = static_cast<u16>( i->X );
  item->y = static_cast<u16>( i->Y );
  item->z = static_cast<s8>( i->Z );

  item->facing = static_cast<unsigned char>( static_cast<u16>( i->Facing ) );

  item->_rev = static_cast<unsigned int>( i->Revision );

  s16 mod_value = static_cast<s16>( i->FireResistMod );
  if ( mod_value != 0 )
    item->fire_resist( item->fire_resist().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( i->ColdResistMod );
  if ( mod_value != 0 )
    item->cold_resist( item->cold_resist().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( i->EnergyResistMod );
  if ( mod_value != 0 )
    item->energy_resist( item->energy_resist().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( i->PoisonResistMod );
  if ( mod_value != 0 )
    item->poison_resist( item->poison_resist().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( i->PhysicalResistMod );
  if ( mod_value != 0 )
    item->physical_resist( item->physical_resist().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( i->FireDamageMod );
  if ( mod_value != 0 )
    item->fire_damage( item->fire_damage().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( i->ColdDamageMod );
  if ( mod_value != 0 )
    item->cold_damage( item->cold_damage().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( i->EnergyDamageMod );
  if ( mod_value != 0 )
    item->energy_damage( item->energy_damage().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( i->PoisonDamageMod );
  if ( mod_value != 0 )
    item->poison_damage( item->poison_damage().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( i->PhysicalDamageMod );
  if ( mod_value != 0 )
    item->physical_damage( item->physical_damage().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( i->DefenceIncreaseMod );
  if ( mod_value != 0 )
    item->defence_increase( item->defence_increase().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( i->DefenceIncreaseCapMod );
  if ( mod_value != 0 )
    item->defence_increase_cap( item->defence_increase_cap().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( i->LowerManaCostMod );
  if ( mod_value != 0 )
    item->lower_mana_cost( item->lower_mana_cost().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( i->HitChanceMod );
  if ( mod_value != 0 )
    item->hit_chance( item->hit_chance().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( i->FireResistCapMod );
  if ( mod_value != 0 )
    item->fire_resist_cap( item->fire_resist_cap().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( i->ColdResistCapMod );
  if ( mod_value != 0 )
    item->cold_resist_cap( item->cold_resist_cap().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( i->EnergyResistCapMod );
  if ( mod_value != 0 )
    item->energy_resist_cap( item->energy_resist_cap().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( i->PoisonResistCapMod );
  if ( mod_value != 0 )
    item->poison_resist_cap( item->poison_resist_cap().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( i->PhysicalResistCapMod );
  if ( mod_value != 0 )
    item->physical_resist_cap( item->physical_resist_cap().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( i->LowerReagentCostMod );
  if ( mod_value != 0 )
    item->lower_reagent_cost( item->lower_reagent_cost().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( i->SpellDamageIncreaseMod );
  if ( mod_value != 0 )
    item->spell_damage_increase( item->spell_damage_increase().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( i->FasterCastingMod );
  if ( mod_value != 0 )
    item->faster_casting( item->faster_casting().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( i->FasterCastRecoveryMod );
  if ( mod_value != 0 )
    item->faster_cast_recovery( item->faster_cast_recovery().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( i->LuckMod );
  if ( mod_value != 0 )
    item->luck( item->luck().setAsMod( mod_value ) );

  //
  //proplist_.readProperties( elem ); //LEITURA DAS CPROPS

  ////<-------------------------------

  ////------------------------------->
  ////item->readProperties( elem );

  //// Changed from Valid Color Mask to cfg mask in ssopt.
  //color &= Core::settingsManager.ssopt.item_color_mask;

  //amount_ = elem.remove_ushort( "AMOUNT", 1 );
  //layer = static_cast<unsigned char>( elem.remove_ushort( "LAYER", 0 ) );
  //movable( elem.remove_bool( "MOVABLE", default_movable() ) );
  //invisible( elem.remove_bool( "INVISIBLE", default_invisible() ) );

  //// NOTE, container is handled specially - it is extracted by the creator.

  //on_use_script_ = elem.remove_string( "ONUSESCRIPT", "" );
  //equip_script_ = elem.remove_string( "EQUIPSCRIPT", equip_script_.get().c_str() );
  //unequip_script_ = elem.remove_string( "UNEQUIPSCRIPT", unequip_script_.get().c_str() );

  //decayat_gameclock_ = elem.remove_ulong( "DECAYAT", 0 );
  //sellprice_( elem.remove_ulong( "SELLPRICE", SELLPRICE_DEFAULT ) );
  //buyprice_( elem.remove_ulong( "BUYPRICE", BUYPRICE_DEFAULT ) );

  //// buyprice used to be read in with remove_int (which was wrong).
  //// the UINT_MAX values used to be written out (which was wrong).
  //// when UINT_MAX is read in by atoi, it returned 2147483647 (0x7FFFFFFF)
  //// correct for this.
  //if ( buyprice_() == 2147483647 )
  //  buyprice_( BUYPRICE_DEFAULT );
  //newbie( elem.remove_bool( "NEWBIE", default_newbie() ) );
  //insured( elem.remove_bool( "INSURED", default_insured() ) );
  //hp_ = elem.remove_ushort( "HP", itemdesc().maxhp );
  //setQuality( elem.remove_double( "QUALITY", itemdesc().quality ) );

  //maxhp_mod( static_cast<s16>( elem.remove_int( "MAXHP_MOD", 0 ) ) );
  //name_suffix( elem.remove_string( "NAMESUFFIX", "" ) );
  //no_drop( elem.remove_bool( "NODROP", default_no_drop() ) );

  //s16 value = static_cast<s16>( elem.remove_int( "FIRERESIST", 0 ) );
  //if ( value != 0 )
  //  fire_resist( fire_resist().setAsValue( value ) );
  //value = static_cast<s16>( elem.remove_int( "COLDRESIST", 0 ) );
  //if ( value != 0 )
  //  cold_resist( cold_resist().setAsValue( value ) );
  //value = static_cast<s16>( elem.remove_int( "ENERGYRESIST", 0 ) );
  //if ( value != 0 )
  //  energy_resist( energy_resist().setAsValue( value ) );
  //value = static_cast<s16>( elem.remove_int( "POISONRESIST", 0 ) );
  //if ( value != 0 )
  //  poison_resist( poison_resist().setAsValue( value ) );
  //value = static_cast<s16>( elem.remove_int( "PHYSICALRESIST", 0 ) );
  //if ( value != 0 )
  //  physical_resist( physical_resist().setAsValue( value ) );

  //value = static_cast<s16>( elem.remove_int( "FIREDAMAGE", 0 ) );
  //if ( value != 0 )
  //  fire_damage( fire_damage().setAsValue( value ) );
  //value = static_cast<s16>( elem.remove_int( "COLDDAMAGE", 0 ) );
  //if ( value != 0 )
  //  cold_damage( cold_damage().setAsValue( value ) );
  //value = static_cast<s16>( elem.remove_int( "ENERGYDAMAGE", 0 ) );
  //if ( value != 0 )
  //  energy_damage( energy_damage().setAsValue( value ) );
  //value = static_cast<s16>( elem.remove_int( "POISONDAMAGE", 0 ) );
  //if ( value != 0 )
  //  poison_damage( poison_damage().setAsValue( value ) );
  //value = static_cast<s16>( elem.remove_int( "PHYSICALDAMAGE", 0 ) );
  //if ( value != 0 )
  //  physical_damage( physical_damage().setAsValue( value ) );
  //value = static_cast<s16>( elem.remove_int( "DEFENCEINCREASE", 0 ) );
  //if ( value != 0 )
  //  defence_increase( defence_increase().setAsValue( value ) );
  //value = static_cast<s16>( elem.remove_int( "DEFENCEINCREASECAP", 0 ) );
  //if ( value != 0 )
  //  defence_increase_cap( defence_increase_cap().setAsValue( value ) );
  //value = static_cast<s16>( elem.remove_int( "LOWERMANACOST", 0 ) );
  //if ( value != 0 )
  //  lower_mana_cost( lower_mana_cost().setAsValue( value ) );
  //value = static_cast<s16>( elem.remove_int( "HITCHANCE", 0 ) );
  //if ( value != 0 )
  //  hit_chance( hit_chance().setAsValue( value ) );
  //value = static_cast<s16>( elem.remove_int( "FIRERESISTCAP", 0 ) );
  //if ( value != 0 )
  //  fire_resist_cap( fire_resist_cap().setAsValue( value ) );
  //value = static_cast<s16>( elem.remove_int( "COLDRESISTCAP", 0 ) );
  //if ( value != 0 )
  //  cold_resist_cap( cold_resist_cap().setAsValue( value ) );
  //value = static_cast<s16>( elem.remove_int( "ENERGYRESISTCAP", 0 ) );
  //if ( value != 0 )
  //  energy_resist_cap( energy_resist_cap().setAsValue( value ) );
  //value = static_cast<s16>( elem.remove_int( "POISONRESISTCAP", 0 ) );
  //if ( value != 0 )
  //  poison_resist_cap( poison_resist_cap().setAsValue( value ) );
  //value = static_cast<s16>( elem.remove_int( "PHYSICALRESISTCAP", 0 ) );
  //if ( value != 0 )
  //  physical_resist_cap( physical_resist_cap().setAsValue( value ) );
  //value = static_cast<s16>( elem.remove_int( "LOWERREAGENTCOST", 0 ) );
  //if ( value != 0 )
  //  lower_reagent_cost( lower_reagent_cost().setAsValue( value ) );
  //value = static_cast<s16>( elem.remove_int( "SPELLDAMAGEINCREASE", 0 ) );
  //if ( value != 0 )
  //  spell_damage_increase( spell_damage_increase().setAsValue( value ) );
  //value = static_cast<s16>( elem.remove_int( "FASTERCASTING", 0 ) );
  //if ( value != 0 )
  //  faster_casting( faster_casting().setAsValue( value ) );
  //value = static_cast<s16>( elem.remove_int( "FASTERCASTRECOVERY", 0 ) );
  //if ( value != 0 )
  //  faster_cast_recovery( faster_cast_recovery().setAsValue( value ) );
  //value = static_cast<s16>( elem.remove_int( "LUCK", 0 ) );
  //if ( value != 0 )
  //  luck( luck().setAsValue( value ) );

  ////<-------------------------------

  item->clear_dirty();

  return item;
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
        StorageArea::SQLite_insert_root_item_onlyDB( item, areaName );
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
        StorageArea::SQLite_insert_root_item_onlyDB( item, areaName );
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

//static int SQLite_callback( void* NotUsed, int count, char** rowdata, char** columns )
//{
//  NotUsed;
//  int i;
//  for ( i = 0; i < count; i++ )
//  {
//    ERROR_PRINT << "Storage result: " << columns[i] << " = " << rowdata[i] << "\n";
//  }
//  ERROR_PRINT << "Storage ended.\n";
//  return 0;
//}

void SQLiteDB::Connect()
{
  std::string dbpath = Plib::systemstate.config.world_data_path + "storage.db";
  int rc = sqlite3_open( dbpath.c_str(), &db );
  if ( rc )
  {
    ERROR_PRINT << "Storage: Can't open database: " << sqlite3_errmsg( db ) << ".\n";
    sqlite3_close( db );
    throw std::runtime_error( "Storage: Can't open database " + dbpath );
  }
}

void SQLiteDB::Finish( sqlite3_stmt*& stmt, int x )
{
  if ( x )
  {
    ERROR_PRINT << "Storage: " << sqlite3_errmsg( db ) << ".\n";
  }
  sqlite3_finalize( stmt );
}

bool SQLiteDB::ExistInStorage( const std::string& name, const std::string& table_name )
{
  // Works to FindStorageArea and FindRootItemInStorageArea
  int result = 0;

  std::string sqlquery = "SELECT EXISTS(SELECT 1 FROM '";
  sqlquery += table_name;
  sqlquery += "' WHERE Name='";
  sqlquery += name;
  sqlquery += "' LIMIT 1) AS result";

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( db, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    Finish( stmt );
    return false;
  }
  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
  {
    result = sqlite3_column_int( stmt, 0 );
  }
  if ( rc != SQLITE_DONE )
  {
    Finish( stmt );
    return false;
  }
  Finish( stmt, 0 );
  return ( result == 1 );
}

bool SQLiteDB::ExistInStorage( const u32 serial, const std::string& table_name )
{
  // Works to FindStorageArea and FindRootItemInStorageArea
  int result = 0;

  std::string sqlquery = "SELECT EXISTS(SELECT 1 FROM '";
  sqlquery += table_name;
  sqlquery += "' WHERE Serial='";
  sqlquery += serial;
  sqlquery += "' LIMIT 1) AS result";

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( db, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    Finish( stmt );
    return false;
  }
  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
  {
    result = sqlite3_column_int( stmt, 0 );
  }
  if ( rc != SQLITE_DONE )
  {
    Finish( stmt );
    return false;
  }
  Finish( stmt, 0 );
  return ( result == 1 );
}

void SQLiteDB::ListStorageAreas()
{
  std::string sqlquery = "SELECT Name FROM StorageArea";

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( db, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    Finish( stmt );
    return;
  }
  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
  {
    std::string Name =
        std::string( reinterpret_cast<const char*>( sqlite3_column_text( stmt, 0 ) ) );
    gamestate.storage.create_areaCache( Name );
  }
  if ( rc != SQLITE_DONE )
  {
    Finish( stmt );
    return;
  }
  Finish( stmt, 0 );
}

void SQLiteDB::AddStorageArea( const std::string& name )
{
  std::string sqlquery = "INSERT INTO StorageArea (Name) VALUES('";
  sqlquery += name;
  sqlquery += "')";

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( db, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    Finish( stmt );
    return;
  }
  rc = sqlite3_step( stmt );

  if ( rc != SQLITE_DONE )
  {
    Finish( stmt );
    return;
  }
  else if ( sqlite3_changes( db ) == 0 )
  {
    ERROR_PRINT << "Storage: No data inserted.\n";
    Finish( stmt );
    return;
  }
  Finish( stmt, 0 );
}

int SQLiteDB::GetIdArea( const std::string& name )
{
  int StorageAreaId = 0;
  std::string sqlquery = "SELECT StorageAreaId FROM StorageArea ";
  sqlquery += "WHERE Name='";
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
    StorageAreaId = sqlite3_column_int( stmt, 0 );
  }
  if ( rc != SQLITE_DONE )
  {
    Finish( stmt );
    return 0;
  }
  Finish( stmt, 0 );
  return StorageAreaId;
}

void SQLiteDB::GetItem( const std::string& name, struct ItemInfoDB* i )
{
  std::string sqlquery = "SELECT * FROM Item WHERE Name = '";
  sqlquery += name;
  sqlquery += "' LIMIT 1";

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( db, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    Finish( stmt );
    return;
  }
  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
  {
    i->ItemId = sqlite3_column_int( stmt, 0 );
    i->StorageAreaId = sqlite3_column_int( stmt, 1 );
    i->Name = std::string( reinterpret_cast<const char*>( sqlite3_column_text( stmt, 2 ) ) );
    i->Serial = sqlite3_column_int( stmt, 3 );
    i->ObjType = sqlite3_column_int( stmt, 4 );
    i->Graphic = sqlite3_column_int( stmt, 5 );
    i->Color = sqlite3_column_int( stmt, 6 );
    i->X = sqlite3_column_int( stmt, 7 );
    i->Y = sqlite3_column_int( stmt, 8 );
    i->Z = sqlite3_column_int( stmt, 9 );
    i->Facing = sqlite3_column_int( stmt, 10 );
    i->Revision = sqlite3_column_int( stmt, 11 );
    i->Realm = std::string( reinterpret_cast<const char*>( sqlite3_column_text( stmt, 12 ) ) );
    i->Amount = sqlite3_column_int( stmt, 13 );
    i->Layer = sqlite3_column_int( stmt, 14 );
    i->Movable = sqlite3_column_int( stmt, 15 );
    i->Invisible = sqlite3_column_int( stmt, 16 );
    i->Container = sqlite3_column_int( stmt, 17 );
    i->OnUseScript =
        std::string( reinterpret_cast<const char*>( sqlite3_column_text( stmt, 18 ) ) );
    i->EquipScript =
        std::string( reinterpret_cast<const char*>( sqlite3_column_text( stmt, 19 ) ) );
    i->UnequipScript =
        std::string( reinterpret_cast<const char*>( sqlite3_column_text( stmt, 20 ) ) );
    i->DecayAt = sqlite3_column_int( stmt, 21 );
    i->SellPrice = sqlite3_column_int( stmt, 22 );
    i->BuyPrice = sqlite3_column_int( stmt, 23 );
    i->Newbie = sqlite3_column_int( stmt, 24 );
    i->Insured = sqlite3_column_int( stmt, 25 );
    i->FireResist = sqlite3_column_int( stmt, 26 );
    i->ColdResist = sqlite3_column_int( stmt, 27 );
    i->EnergyResist = sqlite3_column_int( stmt, 28 );
    i->PoisonResist = sqlite3_column_int( stmt, 29 );
    i->PhysicalResist = sqlite3_column_int( stmt, 30 );
    i->FireDamage = sqlite3_column_int( stmt, 31 );
    i->ColdDamage = sqlite3_column_int( stmt, 32 );
    i->EnergyDamage = sqlite3_column_int( stmt, 33 );
    i->PoisonDamage = sqlite3_column_int( stmt, 34 );
    i->PhysicalDamage = sqlite3_column_int( stmt, 35 );
    i->LowerReagentCost = sqlite3_column_int( stmt, 36 );
    i->SpellDamageIncrease = sqlite3_column_int( stmt, 37 );
    i->FasterCasting = sqlite3_column_int( stmt, 38 );
    i->FasterCastRecovery = sqlite3_column_int( stmt, 39 );
    i->DefenceIncrease = sqlite3_column_int( stmt, 40 );
    i->DefenceIncreaseCap = sqlite3_column_int( stmt, 41 );
    i->LowerManaCost = sqlite3_column_int( stmt, 42 );
    i->FireResistCap = sqlite3_column_int( stmt, 43 );
    i->ColdResistCap = sqlite3_column_int( stmt, 44 );
    i->EnergyResistCap = sqlite3_column_int( stmt, 45 );
    i->PhysicalResistCap = sqlite3_column_int( stmt, 46 );
    i->PoisonResistCap = sqlite3_column_int( stmt, 47 );
    i->Luck = sqlite3_column_int( stmt, 48 );
    i->MaxHp_mod = sqlite3_column_int( stmt, 49 );
    i->Hp = sqlite3_column_int( stmt, 50 );
    i->Quality = sqlite3_column_int( stmt, 51 );
    i->NameSuffix = std::string( reinterpret_cast<const char*>( sqlite3_column_text( stmt, 52 ) ) );
    i->NoDrop = sqlite3_column_int( stmt, 53 );
    i->FireResistMod = sqlite3_column_int( stmt, 54 );
    i->ColdResistMod = sqlite3_column_int( stmt, 55 );
    i->EnergyResistMod = sqlite3_column_int( stmt, 56 );
    i->PoisonResistMod = sqlite3_column_int( stmt, 57 );
    i->PhysicalResistMod = sqlite3_column_int( stmt, 58 );
    i->FireDamageMod = sqlite3_column_int( stmt, 59 );
    i->ColdDamageMod = sqlite3_column_int( stmt, 60 );
    i->EnergyDamageMod = sqlite3_column_int( stmt, 61 );
    i->PoisonDamageMod = sqlite3_column_int( stmt, 62 );
    i->PhysicalDamageMod = sqlite3_column_int( stmt, 63 );
    i->LowerReagentCostMod = sqlite3_column_int( stmt, 64 );
    i->DefenceIncreaseMod = sqlite3_column_int( stmt, 65 );
    i->DefenceIncreaseCapMod = sqlite3_column_int( stmt, 66 );
    i->LowerManaCostMod = sqlite3_column_int( stmt, 67 );
    i->HitChanceMod = sqlite3_column_int( stmt, 68 );
    i->FireResistCapMod = sqlite3_column_int( stmt, 69 );
    i->ColdResistCapMod = sqlite3_column_int( stmt, 70 );
    i->EnergyResistCapMod = sqlite3_column_int( stmt, 71 );
    i->PhysicalResistCapMod = sqlite3_column_int( stmt, 72 );
    i->PoisonResistCapMod = sqlite3_column_int( stmt, 73 );
    i->SpellDamageIncreaseMod = sqlite3_column_int( stmt, 74 );
    i->FasterCastingMod = sqlite3_column_int( stmt, 75 );
    i->FasterCastRecoveryMod = sqlite3_column_int( stmt, 76 );
    i->LuckMod = sqlite3_column_int( stmt, 77 );
  }
  if ( rc != SQLITE_DONE )
  {
    Finish( stmt );
    return;
  }
  Finish( stmt, 0 );
}

bool SQLiteDB::RemoveItem( const std::string& name )
{
  std::string sqlquery = "DELETE FROM Item WHERE Name = '";
  sqlquery += name;
  sqlquery += "'";

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( db, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    Finish( stmt );
    return false;
  }
  rc = sqlite3_step( stmt );

  if ( rc != SQLITE_DONE )
  {
    Finish( stmt );
    return false;
  }
  else if ( sqlite3_changes( db ) == 0 )
  {
    ERROR_PRINT << "Storage: No data deleted.\n";
    Finish( stmt );
    return false;
  }
  Finish( stmt, 0 );
  return true;
}

void SQLiteDB::query_value( std::string& q, const std::string& v, bool last )
{
  if ( v != "NULL" )
  {
    q += "'";
    q += v;
  }
  else
  {
    q += "NULL";
  }

  if ( !last && v != "NULL" )
  {
    q += "',";
  }
  else if ( !last && v == "NULL" )
  {
    q += ",";
  }
  else if ( v != "NULL" )
  {
    q += "'";
  }
}

int SQLiteDB::Last_Rowid()
{
  int rowid = 0;
  std::string sqlquery = "SELECT last_insert_rowid()";

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( db, sqlquery.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    Finish( stmt );
    return 0;
  }
  while ( ( rc = sqlite3_step( stmt ) ) == SQLITE_ROW )
  {
    rowid = sqlite3_column_int( stmt, 0 );
  }
  if ( rc != SQLITE_DONE )
  {
    Finish( stmt );
    return 0;
  }
  Finish( stmt, 0 );
  return rowid;
}

void SQLiteDB::PrepareCProp( Items::Item* item,
                                   std::map<std::string, std::string>& allproperties )
{
  std::vector<std::string> propnames;
  item->getpropnames( propnames );

  for ( const auto& propname : propnames )
  {
    const std::string& hash = "#";
    if ( propname != hash )
    {
      std::string value;
      if ( item->getprop( propname, value ) )
      {
        allproperties[propname] = value;
      }
      else
      {
        ERROR_PRINT << "Storage Error: Property " << propname << " not found\n";
      }
    }
  }
}

bool SQLiteDB::AddCProp( Items::Item* item, const int last_rowid )
{
  std::map<std::string, std::string> allproperties;
  PrepareCProp( item, allproperties );

  auto CPropId = "NULL";
  auto ItemId = std::to_string( last_rowid );

  for ( const auto& kv : allproperties )
  {
    std::string s = "INSERT INTO CProp VALUES(";
    query_value( s, CPropId );
    query_value( s, kv.first );
    query_value( s, kv.second );
    query_value( s, ItemId, true );
    s += ")";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2( db, s.c_str(), -1, &stmt, NULL );
    if ( rc != SQLITE_OK )
    {
      Finish( stmt );
      return false;
    }
    rc = sqlite3_step( stmt );

    if ( rc != SQLITE_DONE )
    {
      Finish( stmt );
      return false;
    }
    else if ( sqlite3_changes( db ) == 0 )
    {
      ERROR_PRINT << "Storage: No CProp inserted.\n";
      Finish( stmt );
      return false;
    }
    Finish( stmt, 0 );
  }
  return true;
}

bool SQLiteDB::AddItem( Items::Item* item, const std::string& areaName )
{
  auto ItemId = "NULL";
  auto StorageAreaId = std::to_string( GetIdArea( areaName ) );
  auto Name = item->name();
  auto Serial = std::to_string( item->serial );
  auto ObjType = std::to_string( item->objtype_ );
  auto Graphic = std::to_string( item->graphic );
  auto Color = item->color ? std::to_string( item->color ) : "NULL";
  auto X = std::to_string( item->x );
  auto Y = std::to_string( item->y );
  auto Z = std::to_string( item->z );
  auto Facing = item->facing ? std::to_string( item->facing ) : "NULL";
  auto Revision = std::to_string( item->rev() );
  auto Realm = ( item->realm == nullptr ) ? "britannia" : item->get_realm();
  auto Amount = ( item->getamount() != 1 ) ? std::to_string( item->getamount() ) : "NULL";
  auto Layer = item->layer ? std::to_string( item->layer ) : "NULL";
  auto Movable =
      ( item->movable() != item->default_movable() ) ? std::to_string( item->movable() ) : "NULL";
  auto Invisible = ( item->invisible() != item->default_invisible() )
                       ? std::to_string( item->invisible() )
                       : "NULL";
  auto Container =
      ( item->container != nullptr ) ? std::to_string( item->container->serial ) : "NULL";
  auto OnUseScript = ( !item->on_use_script_.get().empty() ) ? item->on_use_script_.get() : "NULL";
  auto EquipScript = ( item->equip_script_ != item->getItemdescEquipScript() )
                         ? item->equip_script_.get()
                         : "NULL";
  auto UnequipScript = ( item->unequip_script_ != item->getItemdescUnequipScript() )
                           ? item->unequip_script_.get()
                           : "NULL";
  auto DecayAt = item->decayat_gameclock_ ? std::to_string( item->decayat_gameclock_ ) : "NULL";
  auto SellPrice = item->has_sellprice_() ? std::to_string( item->sellprice_() ) : "NULL";
  auto BuyPrice = item->has_buyprice_() ? std::to_string( item->buyprice_() ) : "NULL";
  auto Newbie =
      ( item->newbie() != item->default_newbie() ) ? std::to_string( item->newbie() ) : "NULL";
  auto Insured =
      ( item->insured() != item->default_insured() ) ? std::to_string( item->insured() ) : "NULL";
  auto FireResist = item->has_fire_resist() ? std::to_string( item->fire_resist().value ) : "NULL";
  auto ColdResist = item->has_cold_resist() ? std::to_string( item->cold_resist().value ) : "NULL";
  auto EnergyResist =
      item->has_energy_resist() ? std::to_string( item->energy_resist().value ) : "NULL";
  auto PoisonResist =
      item->has_poison_resist() ? std::to_string( item->poison_resist().value ) : "NULL";
  auto PhysicalResist =
      item->has_physical_resist() ? std::to_string( item->physical_resist().value ) : "NULL";
  auto FireDamage = item->has_fire_damage() ? std::to_string( item->fire_damage().value ) : "NULL";
  auto ColdDamage = item->has_cold_damage() ? std::to_string( item->cold_damage().value ) : "NULL";
  auto EnergyDamage =
      item->has_energy_damage() ? std::to_string( item->energy_damage().value ) : "NULL";
  auto PoisonDamage =
      item->has_poison_damage() ? std::to_string( item->poison_damage().value ) : "NULL";
  auto PhysicalDamage =
      item->has_physical_damage() ? std::to_string( item->physical_damage().value ) : "NULL";
  auto LowerReagentCost =
      item->has_lower_reagent_cost() ? std::to_string( item->lower_reagent_cost().value ) : "NULL";
  auto SpellDamageIncrease = item->has_spell_damage_increase()
                                 ? std::to_string( item->spell_damage_increase().value )
                                 : "NULL";
  auto FasterCasting =
      item->has_faster_casting() ? std::to_string( item->faster_casting().value ) : "NULL";
  auto FasterCastRecovery = item->has_faster_cast_recovery()
                                ? std::to_string( item->faster_cast_recovery().value )
                                : "NULL";
  auto DefenceIncrease =
      item->has_defence_increase() ? std::to_string( item->defence_increase().value ) : "NULL";
  auto DefenceIncreaseCap = item->has_defence_increase_cap()
                                ? std::to_string( item->defence_increase_cap().value )
                                : "NULL";
  auto LowerManaCost =
      item->has_lower_mana_cost() ? std::to_string( item->lower_mana_cost().value ) : "NULL";
  auto FireResistCap =
      item->has_fire_resist_cap() ? std::to_string( item->fire_resist_cap().value ) : "NULL";
  auto ColdResistCap =
      item->has_cold_resist_cap() ? std::to_string( item->cold_resist_cap().value ) : "NULL";
  auto EnergyResistCap =
      item->has_energy_resist_cap() ? std::to_string( item->energy_resist_cap().value ) : "NULL";
  auto PhysicalResistCap = item->has_physical_resist_cap()
                               ? std::to_string( item->physical_resist_cap().value )
                               : "NULL";
  auto PoisonResistCap =
      item->has_poison_resist_cap() ? std::to_string( item->poison_resist_cap().value ) : "NULL";
  auto Luck = item->has_luck() ? std::to_string( item->luck().value ) : "NULL";
  auto MaxHp_mod = item->maxhp_mod() ? std::to_string( item->maxhp_mod() ) : "NULL";
  auto Hp = ( item->hp_ != item->getItemdescMaxhp() ) ? std::to_string( item->hp_ ) : "NULL";
  auto Quality = ( item->getQuality() != item->getItemdescQuality() )
                     ? std::to_string( item->getQuality() )
                     : "NULL";
  auto NameSuffix = !item->name_suffix().empty() ? item->name_suffix() : "NULL";
  auto NoDrop =
      ( item->no_drop() != item->default_no_drop() ) ? std::to_string( item->no_drop() ) : "NULL";
  auto FireResistMod = item->fire_resist().mod ? std::to_string( item->fire_resist().mod ) : "NULL";
  auto ColdResistMod = item->cold_resist().mod ? std::to_string( item->cold_resist().mod ) : "NULL";
  auto EnergyResistMod =
      item->energy_resist().mod ? std::to_string( item->energy_resist().mod ) : "NULL";
  auto PoisonResistMod =
      item->poison_resist().mod ? std::to_string( item->poison_resist().mod ) : "NULL";
  auto PhysicalResistMod =
      item->physical_resist().mod ? std::to_string( item->physical_resist().mod ) : "NULL";
  auto FireDamageMod = item->fire_damage().mod ? std::to_string( item->fire_damage().mod ) : "NULL";
  auto ColdDamageMod = item->cold_damage().mod ? std::to_string( item->cold_damage().mod ) : "NULL";
  auto EnergyDamageMod =
      item->energy_damage().mod ? std::to_string( item->energy_damage().mod ) : "NULL";
  auto PoisonDamageMod =
      item->poison_damage().mod ? std::to_string( item->poison_damage().mod ) : "NULL";
  auto PhysicalDamageMod =
      item->physical_damage().mod ? std::to_string( item->physical_damage().mod ) : "NULL";
  auto LowerReagentCostMod =
      item->lower_reagent_cost().mod ? std::to_string( item->lower_reagent_cost().mod ) : "NULL";
  auto DefenceIncreaseMod =
      item->defence_increase().mod ? std::to_string( item->defence_increase().mod ) : "NULL";
  auto DefenceIncreaseCapMod = item->defence_increase_cap().mod
                                   ? std::to_string( item->defence_increase_cap().mod )
                                   : "NULL";
  auto LowerManaCostMod =
      item->lower_mana_cost().mod ? std::to_string( item->lower_mana_cost().mod ) : "NULL";
  auto HitChanceMod = item->hit_chance().mod ? std::to_string( item->hit_chance().mod ) : "NULL";
  auto FireResistCapMod =
      item->fire_resist_cap().mod ? std::to_string( item->fire_resist_cap().mod ) : "NULL";
  auto ColdResistCapMod =
      item->cold_resist_cap().mod ? std::to_string( item->cold_resist_cap().mod ) : "NULL";
  auto EnergyResistCapMod =
      item->energy_resist_cap().mod ? std::to_string( item->energy_resist_cap().mod ) : "NULL";
  auto PhysicalResistCapMod =
      item->physical_resist_cap().mod ? std::to_string( item->physical_resist_cap().mod ) : "NULL";
  auto PoisonResistCapMod =
      item->poison_resist_cap().mod ? std::to_string( item->poison_resist_cap().mod ) : "NULL";
  auto SpellDamageIncreaseMod = item->spell_damage_increase().mod
                                    ? std::to_string( item->spell_damage_increase().mod )
                                    : "NULL";
  auto FasterCastingMod =
      item->faster_casting().mod ? std::to_string( item->faster_casting().mod ) : "NULL";
  auto FasterCastRecoveryMod = item->faster_cast_recovery().mod
                                   ? std::to_string( item->faster_cast_recovery().mod )
                                   : "NULL";
  auto LuckMod = item->luck().mod ? std::to_string( item->luck().mod ) : "NULL";

  std::string s = "INSERT INTO Item VALUES(";
  query_value( s, ItemId );
  query_value( s, StorageAreaId );
  query_value( s, Name );
  query_value( s, Serial );
  query_value( s, ObjType );
  query_value( s, Graphic );
  query_value( s, Color );
  query_value( s, X );
  query_value( s, Y );
  query_value( s, Z );
  query_value( s, Facing );
  query_value( s, Revision );
  query_value( s, Realm );
  query_value( s, Amount );
  query_value( s, Layer );
  query_value( s, Movable );
  query_value( s, Invisible );
  query_value( s, Container );
  query_value( s, OnUseScript );
  query_value( s, EquipScript );
  query_value( s, UnequipScript );
  query_value( s, DecayAt );
  query_value( s, SellPrice );
  query_value( s, BuyPrice );
  query_value( s, Newbie );
  query_value( s, Insured );
  query_value( s, FireResist );
  query_value( s, ColdResist );
  query_value( s, EnergyResist );
  query_value( s, PoisonResist );
  query_value( s, PhysicalResist );
  query_value( s, FireDamage );
  query_value( s, ColdDamage );
  query_value( s, EnergyDamage );
  query_value( s, PoisonDamage );
  query_value( s, PhysicalDamage );
  query_value( s, LowerReagentCost );
  query_value( s, SpellDamageIncrease );
  query_value( s, FasterCasting );
  query_value( s, FasterCastRecovery );
  query_value( s, DefenceIncrease );
  query_value( s, DefenceIncreaseCap );
  query_value( s, LowerManaCost );
  query_value( s, FireResistCap );
  query_value( s, ColdResistCap );
  query_value( s, EnergyResistCap );
  query_value( s, PhysicalResistCap );
  query_value( s, PoisonResistCap );
  query_value( s, Luck );
  query_value( s, MaxHp_mod );
  query_value( s, Hp );
  query_value( s, Quality );
  query_value( s, NameSuffix );
  query_value( s, NoDrop );
  query_value( s, FireResistMod );
  query_value( s, ColdResistMod );
  query_value( s, EnergyResistMod );
  query_value( s, PoisonResistMod );
  query_value( s, PhysicalResistMod );
  query_value( s, FireDamageMod );
  query_value( s, ColdDamageMod );
  query_value( s, EnergyDamageMod );
  query_value( s, PoisonDamageMod );
  query_value( s, PhysicalDamageMod );
  query_value( s, LowerReagentCostMod );
  query_value( s, DefenceIncreaseMod );
  query_value( s, DefenceIncreaseCapMod );
  query_value( s, LowerManaCostMod );
  query_value( s, HitChanceMod );
  query_value( s, FireResistCapMod );
  query_value( s, ColdResistCapMod );
  query_value( s, EnergyResistCapMod );
  query_value( s, PhysicalResistCapMod );
  query_value( s, PoisonResistCapMod );
  query_value( s, SpellDamageIncreaseMod );
  query_value( s, FasterCastingMod );
  query_value( s, FasterCastRecoveryMod );
  query_value( s, LuckMod, true );
  s += ")";

  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2( db, s.c_str(), -1, &stmt, NULL );
  if ( rc != SQLITE_OK )
  {
    Finish( stmt );
    return false;
  }
  rc = sqlite3_step( stmt );

  if ( rc != SQLITE_DONE )
  {
    Finish( stmt );
    return false;
  }
  else if ( sqlite3_changes( db ) == 0 )
  {
    ERROR_PRINT << "Storage: No Item inserted.\n";
    Finish( stmt );
    return false;
  }
  Finish( stmt, 0 );

  int last_rowid = Last_Rowid();

  if ( !last_rowid )
  {
    ERROR_PRINT << "Storage: No lastRowId found.\n";
    Finish( stmt );
    throw std::runtime_error( "Data file integrity error" );
  }

  if ( !AddCProp( item, last_rowid ) )
  {
    ERROR_PRINT << "Storage: No CProp inserted.\n";
    Finish( stmt );
    throw std::runtime_error( "Data file integrity error" );
  }

  return true;
}

void StorageArea::create_ItemCache( const std::string& name )
{
  // Get item from SQLite DB
  // and transform to Item ref
  Items::Item* temp_item = StorageArea::read_itemInDB( name );

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
  if ( Plib::systemstate.config.enable_sqlite )
  {
    gamestate.sqlitedb.Connect();
  }

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
    sqlite3_close( gamestate.sqlitedb.db );
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
