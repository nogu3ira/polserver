/** @file
 *
 * @par History
 * - 2009/08/25 Shinigami: STLport-5.2.1 fix: init order changed of realm and saveonexit_
 * - 2009/09/14 MuadDib:   UObject::setgraphic added error printing.
 * - 2009/12/02 Turley:    added config.max_tile_id - Tomi
 */


#include "uobject.h"

#include <atomic>
#include <iosfwd>
#include <stddef.h>
#include <string>

#include "../clib/cfgelem.h"
#include "../clib/logfacility.h"
#include "../clib/passert.h"
#include "../clib/rawtypes.h"
#include "../clib/refptr.h"
#include "../clib/streamsaver.h"
#include "../plib/clidata.h"
#include "../plib/systemstate.h"
#include "../plib/uconst.h"
#include "baseobject.h"
#include "dynproperties.h"
#include "globals/state.h"
#include "globals/uvars.h"
#include "item/itemdesc.h"
#include "objtype.h"
#include "polcfg.h"
#include "proplist.h"
#include "realms.h"
#include "realms/realm.h"
#include "syshookscript.h"
#include "tooltips.h"
#include "uobjcnt.h"
#include <boost/algorithm/string/replace.hpp>

namespace Pol
{
namespace Core
{
std::set<UObject*> unreaped_orphan_instances;
std::ofstream orphans_txt( "orphans.txt", std::ios::out | std::ios::trunc );

int display_orphan( UObject* o )
{
  Clib::FMTStreamWriter sw;
  Clib::OFStreamWriter sw_orphan( &orphans_txt );
  sw() << o->name() << ", " << o->ref_counted_count() << '\n';
  o->printOn( sw );
  o->printOnDebug( sw_orphan );
  INFO_PRINT << sw().str();
  // ref_ptr<UObject>::display_referers( o->as_ref_counted() );

  return 0;
}
void display_unreaped_orphan_instances()
{
  //    orphans_txt.open( "orphans.txt", ios::out|ios::trunc );

  for ( auto& obj : unreaped_orphan_instances )
  {
    display_orphan( obj );
  }
  // for( std::set<UObject*>::iterator itr = unreaped_orphan_instances.begin();
}


std::atomic<unsigned int> UObject::dirty_writes;
std::atomic<unsigned int> UObject::clean_writes;

UObject::UObject( u32 objtype, UOBJ_CLASS i_uobj_class )
    : ref_counted(),
      ULWObject( i_uobj_class ),
      DynamicPropsHolder(),
      serial_ext( 0 ),
      objtype_( objtype ),
      color( 0 ),
      facing( Plib::FACING_N ),
      name_( "" ),
      _rev( 0 ),
      flags_(),
      proplist_( CPropProfiler::class_to_type( i_uobj_class ) )
{
  graphic = Items::getgraphic( objtype );
  flags_.set( OBJ_FLAGS::DIRTY );
  flags_.set( OBJ_FLAGS::SAVE_ON_EXIT );
  height = Plib::tileheight( graphic );
  ++stateManager.uobjcount.uobject_count;
}

UObject::~UObject()
{
  if ( ref_counted::count() != 0 )
  {
    POLLOG_INFO << "Ouch! UObject::~UObject() with count()==" << ref_counted::count() << "\n";
  }
  passert( ref_counted::count() == 0 );
  if ( serial == 0 )
  {
    --stateManager.uobjcount.unreaped_orphans;
  }
  --stateManager.uobjcount.uobject_count;
}

size_t UObject::estimatedSize() const
{
  size_t size = sizeof( UObject ) + proplist_.estimatedSize();
  size += estimateSizeDynProps();
  return size;
}

//
//    Every UObject is registered with the objecthash after creation.
//    (This can't happen in the ctor since the object isn't fully created yet)
//
//    Scripts may still reference any object, so rather than outright delete,
//    we set its serial to 0 (marking it "orphan", though "zombie" would probably be a better term).
//    Later, when all _other_ references to the object have been deleted,
//    objecthash.Reap() will remove its reference to this object, deleting it.
//
void UObject::destroy()
{
  if ( serial != 0 )
  {
    if ( ref_counted::count() < 1 )
    {
      POLLOG_INFO << "Ouch! UObject::destroy() with count()==" << ref_counted::count() << "\n";
    }

    set_dirty();  // we will have to write a 'object deleted' directive once

    serial =
        0;  // used to set serial_ext to 0.  This way, if debugging, one can find out the old serial
    passert( ref_counted::count() >= 1 );

    ++stateManager.uobjcount.unreaped_orphans;
  }
}

void UObject::unload()
{
  if ( serial != 0 )
  {
    if ( ref_counted::count() < 1 )
    {
      POLLOG_INFO << "Ouch! UObject::unload() with count()==" << ref_counted::count() << "\n";
    }

    passert( ref_counted::count() >= 1 );

	gamestate.sqlitedb.RemoveObjectHash( serial );
  }
}

bool UObject::dirty() const
{
  return flags_.get( OBJ_FLAGS::DIRTY );
}

void UObject::clear_dirty() const
{
  if ( dirty() )
    ++dirty_writes;
  else
    ++clean_writes;
  flags_.remove( OBJ_FLAGS::DIRTY );
}

bool UObject::getprop( const std::string& propname, std::string& propval ) const
{
  return proplist_.getprop( propname, propval );
}

void UObject::setprop( const std::string& propname, const std::string& propvalue )
{
  if ( propname[0] != '#' )
    set_dirty();
  proplist_.setprop( propname, propvalue );  // VOID_RETURN
}

void UObject::eraseprop( const std::string& propname )
{
  if ( propname[0] != '#' )
    set_dirty();
  proplist_.eraseprop( propname );  // VOID_RETURN
}

void UObject::copyprops( const UObject& from )
{
  set_dirty();
  proplist_.copyprops( from.proplist_ );
}

void UObject::copyprops( const PropertyList& proplist )
{
  set_dirty();
  proplist_.copyprops( proplist );
}

void UObject::getpropnames( std::vector<std::string>& propnames ) const
{
  proplist_.getpropnames( propnames );
}

const PropertyList& UObject::getprops() const
{
  return proplist_;
}

std::string UObject::name() const
{
  return name_;
}

std::string UObject::description() const
{
  return name_;
}

void UObject::setname( const std::string& newname )
{
  set_dirty();
  increv();
  send_object_cache_to_inrange( this );
  name_ = newname;
}

UObject* UObject::owner()
{
  return nullptr;
}

const UObject* UObject::owner() const
{
  return nullptr;
}

UObject* UObject::self_as_owner()
{
  return this;
}

const UObject* UObject::self_as_owner() const
{
  return this;
}

UObject* UObject::toplevel_owner()
{
  return this;
}

const UObject* UObject::toplevel_owner() const
{
  return this;
}

std::string UObject::get_realm() const
{
  return realm->name();
}

std::string UObject::EscapeSequence( std::string value ) const
{
  boost::replace_all( value, "\"", "\"\"" );
  boost::replace_all( value, "\'", "\'\'" );
  return value;
}

std::string UObject::UnEscapeSequence( std::string value ) const
{
  boost::replace_all( value, "\"\"", "\"" );
  boost::replace_all( value, "\'\'", "\'" );
  return value;
}

void UObject::printProperties( Clib::PreparePrint& pp ) const
{
  using namespace std;
  using namespace boost;

  pp.internal.insert( make_pair( "DIRTY", dirty() ) );
  pp.internal.insert( make_pair( "SAVE_ON_EXIT", saveonexit() ) );
  pp.internal.insert( make_pair( "ORPHAN", orphan() ) );
  pp.internal.insert( make_pair( "SERIAL_EXT", serial_ext ) );

  if ( !name_.get().empty() )
    pp.main.insert( make_pair( "Name", EscapeSequence( name_.get() ) ) );

  pp.main.insert( make_pair( "Serial", lexical_cast<string>(serial) ) );
  pp.main.insert( make_pair( "ObjType", lexical_cast<string>( objtype_ ) ) );
  pp.main.insert( make_pair( "Graphic", lexical_cast<string>( graphic ) ) );

  if ( color != 0 )
    pp.unusual.insert( make_pair( "Color", lexical_cast<string>( color ) ) );

  pp.main.insert( make_pair( "X", lexical_cast<string>( x ) ) );
  pp.main.insert( make_pair( "Y", lexical_cast<string>( y ) ) );
  pp.main.insert( make_pair( "Z", lexical_cast<string>( (int)z ) ) );

  if ( facing )
    pp.unusual.insert( make_pair( "Facing", lexical_cast<string>( static_cast<int>( facing ) ) ) );

  pp.main.insert( make_pair( "Revision", lexical_cast<string>( rev() ) ) );
  if ( realm == nullptr )
    pp.main.insert( make_pair( "Realm", "britannia" ) );
  else 
	pp.main.insert( make_pair( "Realm", EscapeSequence( realm->name() ) ) );

  s16 value = fire_resist().mod;
  if ( value != 0 )
    pp.unusual.insert( make_pair( "FireResistMod", lexical_cast<string>( static_cast<int>( value ) ) ) );
  value = cold_resist().mod;
  if ( value != 0 )
    pp.unusual.insert( make_pair( "ColdResistMod", lexical_cast<string>( static_cast<int>( value ) ) ) );
  value = energy_resist().mod;
  if ( value != 0 )
    pp.unusual.insert( make_pair( "EnergyResistMod", lexical_cast<string>( static_cast<int>( value ) ) ) );
  value = poison_resist().mod;
  if ( value != 0 )
    pp.unusual.insert( make_pair( "PoisonResistMod", lexical_cast<string>( static_cast<int>( value ) ) ) );
  value = physical_resist().mod;
  if ( value != 0 )
    pp.unusual.insert( make_pair( "PhysicalResistMod", lexical_cast<string>( static_cast<int>( value ) ) ) );

  value = fire_damage().mod;
  if ( value != 0 )
    pp.unusual.insert( make_pair( "FireDamageMod", lexical_cast<string>( static_cast<int>( value ) ) ) );
  value = cold_damage().mod;
  if ( value != 0 )
    pp.unusual.insert( make_pair( "ColdDamageMod", lexical_cast<string>( static_cast<int>( value ) ) ) );
  value = energy_damage().mod;
  if ( value != 0 )
    pp.unusual.insert( make_pair( "EnergyDamageMod", lexical_cast<string>( static_cast<int>( value ) ) ) );
  value = poison_damage().mod;
  if ( value != 0 )
    pp.unusual.insert( make_pair( "PoisonDamageMod", lexical_cast<string>( static_cast<int>( value ) ) ) );
  value = physical_damage().mod;
  if ( value != 0 )
    pp.unusual.insert( make_pair( "PhysicalDamageMod", lexical_cast<string>( static_cast<int>( value ) ) ) );
  // new mod stuff
  value = lower_reagent_cost().mod;
  if ( value )
    pp.unusual.insert( make_pair( "LowerReagentCostMod", lexical_cast<string>( static_cast<int>( value ) ) ) );
  value = defence_increase().mod;
  if ( value )
    pp.unusual.insert( make_pair( "DefenceIncreaseMod", lexical_cast<string>( static_cast<int>( value ) ) ) );
  value = defence_increase_cap().mod;
  if ( value )
    pp.unusual.insert( make_pair( "DefenceIncreaseCapMod", lexical_cast<string>( static_cast<int>( value ) ) ) );
  value = lower_mana_cost().mod;
  if ( value )
    pp.unusual.insert( make_pair( "LowerManaCostMod", lexical_cast<string>( static_cast<int>( value ) ) ) );
  value = hit_chance().mod;
  if ( value )
    pp.unusual.insert( make_pair( "HitChanceMod", lexical_cast<string>( static_cast<int>( value ) ) ) );
  value = fire_resist_cap().mod;
  if ( value )
    pp.unusual.insert( make_pair( "FireResistCapMod", lexical_cast<string>( static_cast<int>( value ) ) ) );
  value = cold_resist_cap().mod;
  if ( value )
    pp.unusual.insert( make_pair( "ColdResistCapMod", lexical_cast<string>( static_cast<int>( value ) ) ) );
  value = energy_resist_cap().mod;
  if ( value )
    pp.unusual.insert( make_pair( "EnergyResistCapMod", lexical_cast<string>( static_cast<int>( value ) ) ) );
  value = physical_resist_cap().mod;
  if ( value )
    pp.unusual.insert( make_pair( "PhysicalResistCapMod", lexical_cast<string>( static_cast<int>( value ) ) ) );
  value = poison_resist_cap().mod;
  if ( value )
    pp.unusual.insert( make_pair( "PoisonResistCapMod", lexical_cast<string>( static_cast<int>( value ) ) ) );
  value = spell_damage_increase().mod;
  if ( value )
    pp.unusual.insert( make_pair( "SpellDamageIncreaseMod", lexical_cast<string>( static_cast<int>( value ) ) ) );
  value = faster_casting().mod;
  if ( value )
    pp.unusual.insert( make_pair( "FasterCastingMod", lexical_cast<string>( static_cast<int>( value ) ) ) );
  value = faster_cast_recovery().mod;
  if ( value )
    pp.unusual.insert( make_pair( "FasterCastRecoveryMod", lexical_cast<string>( static_cast<int>( value ) ) ) );
  value = luck().mod;
  if ( value )
    pp.unusual.insert( make_pair( "LuckMod", lexical_cast<string>( static_cast<int>( value ) ) ) );
  // end new mod stuff


  proplist_.printProperties( pp.cprop );
}

void UObject::printProperties( Clib::StreamWriter& sw ) const
{
  Clib::PreparePrint pp;
  printProperties( pp );
  ToStreamWriter( sw, pp );
}

void UObject::ToStreamWriter( Clib::StreamWriter& sw, Clib::PreparePrint& pp ) const
{
  using namespace fmt;
  using namespace std;
  using namespace boost;
  string hexProps[] = {"Serial",    "ObjType",   "Graphic",     "Color",     "Container",
                       "TrueColor", "TrueObjtype", "RegisteredHouse", "Traveller", "Component"};

  for ( const auto& m : pp.main )
  {
    if ( find( begin( hexProps ), end( hexProps ), m.first ) != end( hexProps ) )
      sw() << "\t" << m.first << "\t0x" << hex( lexical_cast<u32>( m.second ) ) << pf_endl;
    else
      sw() << "\t" << m.first << "\t" << UnEscapeSequence( m.second ) << pf_endl;
  }

  for ( const auto& m : pp.unusual )
  {
    if ( find( begin( hexProps ), end( hexProps ), m.first ) != end( hexProps ) )
      sw() << "\t" << m.first << "\t0x" << hex( lexical_cast<u32>( m.second ) ) << pf_endl;
    else
      sw() << "\t" << m.first << "\t" << UnEscapeSequence( m.second ) << pf_endl;
  }

  for ( const auto& m : pp.cprop )
    sw() << "\tCProp\t" << m.first << " " << UnEscapeSequence( m.second ) << pf_endl;
}

void UObject::printDebugProperties( Clib::StreamWriter& sw ) const
{
  sw() << "# uobj_class: " << (int)uobj_class_ << pf_endl;
}

/// Fixes invalid graphic, moving here to allow it to be overridden in subclass (see Multi)
void UObject::fixInvalidGraphic()
{
  if ( graphic > ( Plib::systemstate.config.max_tile_id ) )
    graphic = GRAPHIC_NODRAW;
}

void UObject::readProperties( Clib::ConfigElem& elem )
{
  name_ = elem.remove_string( "NAME", "" );

  // serial, objtype extracted by caller
  graphic = elem.remove_ushort( "GRAPHIC", static_cast<u16>( objtype_ ) );
  fixInvalidGraphic();

  height = Plib::tileheight( graphic );

  color = elem.remove_ushort( "COLOR", 0 );


  std::string realmstr = elem.remove_string( "Realm", "britannia" );
  realm = find_realm( realmstr );
  if ( !realm )
  {
    ERROR_PRINT.Format( "{} '{}' (0x{:X}): has an invalid realm property '{}'.\n" )
        << classname() << name() << serial << realmstr;
    throw std::runtime_error( "Data integrity error" );
  }
  x = elem.remove_ushort( "X" );
  y = elem.remove_ushort( "Y" );
  z = static_cast<s8>( elem.remove_int( "Z" ) );
  if ( !realm->valid( x, y, z ) )
  {
    x = static_cast<u16>( realm->width() ) - 1;
    y = static_cast<u16>( realm->height() ) - 1;
    z = 0;
  }

  unsigned short tmp = elem.remove_ushort( "FACING", 0 );
  setfacing( static_cast<unsigned char>( tmp ) );

  _rev = elem.remove_ulong( "Revision", 0 );

  s16 mod_value = static_cast<s16>( elem.remove_int( "FIRERESISTMOD", 0 ) );
  if ( mod_value != 0 )
    fire_resist( fire_resist().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( elem.remove_int( "COLDRESISTMOD", 0 ) );
  if ( mod_value != 0 )
    cold_resist( cold_resist().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( elem.remove_int( "ENERGYRESISTMOD", 0 ) );
  if ( mod_value != 0 )
    energy_resist( energy_resist().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( elem.remove_int( "POISONRESISTMOD", 0 ) );
  if ( mod_value != 0 )
    poison_resist( poison_resist().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( elem.remove_int( "PHYSICALRESISTMOD", 0 ) );
  if ( mod_value != 0 )
    physical_resist( physical_resist().setAsMod( mod_value ) );

  mod_value = static_cast<s16>( elem.remove_int( "FIREDAMAGEMOD", 0 ) );
  if ( mod_value != 0 )
    fire_damage( fire_damage().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( elem.remove_int( "COLDDAMAGEMOD", 0 ) );
  if ( mod_value != 0 )
    cold_damage( cold_damage().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( elem.remove_int( "ENERGYDAMAGEMOD", 0 ) );
  if ( mod_value != 0 )
    energy_damage( energy_damage().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( elem.remove_int( "POISONDAMAGEMOD", 0 ) );
  if ( mod_value != 0 )
    poison_damage( poison_damage().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( elem.remove_int( "PHYSICALDAMAGEMOD", 0 ) );
  if ( mod_value != 0 )
    physical_damage( physical_damage().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( elem.remove_int( "DEFENCEINCREASEMOD", 0 ) );
  if ( mod_value != 0 )
    defence_increase( defence_increase().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( elem.remove_int( "DEFENCEINCREASECAPMOD", 0 ) );
  if ( mod_value != 0 )
    defence_increase_cap( defence_increase_cap().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( elem.remove_int( "LOWERMANACOSTMOD", 0 ) );
  if ( mod_value != 0 )
    lower_mana_cost( lower_mana_cost().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( elem.remove_int( "HITCHANCEMOD", 0 ) );
  if ( mod_value != 0 )
    hit_chance( hit_chance().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( elem.remove_int( "FIRERESISTCAPMOD", 0 ) );
  if ( mod_value != 0 )
    fire_resist_cap( fire_resist_cap().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( elem.remove_int( "COLDRESISTCAPMOD", 0 ) );
  if ( mod_value != 0 )
    cold_resist_cap( cold_resist_cap().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( elem.remove_int( "ENERGYRESISTCAPMOD", 0 ) );
  if ( mod_value != 0 )
    energy_resist_cap( energy_resist_cap().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( elem.remove_int( "POISONRESISTCAPMOD", 0 ) );
  if ( mod_value != 0 )
    poison_resist_cap( poison_resist_cap().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( elem.remove_int( "PHYSICALRESISTCAPMOD", 0 ) );
  if ( mod_value != 0 )
    physical_resist_cap( physical_resist_cap().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( elem.remove_int( "LOWERREAGENTCOSTMOD", 0 ) );
  if ( mod_value != 0 )
    lower_reagent_cost( lower_reagent_cost().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( elem.remove_int( "SPELLDAMAGEINCREASEMOD", 0 ) );
  if ( mod_value != 0 )
    spell_damage_increase( spell_damage_increase().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( elem.remove_int( "FASTERCASTINGMOD", 0 ) );
  if ( mod_value != 0 )
    faster_casting( faster_casting().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( elem.remove_int( "FASTERCASTRECOVERYMOD", 0 ) );
  if ( mod_value != 0 )
    faster_cast_recovery( faster_cast_recovery().setAsMod( mod_value ) );
  mod_value = static_cast<s16>( elem.remove_int( "LUCKMOD", 0 ) );
  if ( mod_value != 0 )
    luck( luck().setAsMod( mod_value ) );


  proplist_.readProperties( elem );
}

void UObject::printSelfOn( Clib::vecPreparePrint& vpp ) const
{
  printOn( vpp );
}

void UObject::printSelfOn( Clib::StreamWriter& sw ) const
{
  printOn( sw );
}

void UObject::printOn( Clib::vecPreparePrint& vpp ) const
{
  printProperties( vpp.v.back() );
}

void UObject::printOn( Clib::StreamWriter& sw ) const
{
  sw() << classname() << pf_endl;
  sw() << "{" << pf_endl;
  printProperties( sw );
  sw() << "}" << pf_endl;
  sw() << pf_endl;
  // sw.flush();
}

void UObject::printOnDebug( Clib::StreamWriter& sw ) const
{
  sw() << classname() << pf_endl;
  sw() << "{" << pf_endl;
  printProperties( sw );
  printDebugProperties( sw );
  sw() << "}" << pf_endl;
  sw() << pf_endl;
  // sw.flush();
}

Clib::vecPreparePrint& operator<<( Clib::vecPreparePrint& vpp, const UObject& obj )
{
  // for each new printOn UObject, add a new PreparePrint into vector
  Clib::PreparePrint pp;
  vpp.v.push_back( pp );
  obj.printOn( vpp );
  return vpp;
}

Clib::StreamWriter& operator<<( Clib::StreamWriter& writer, const UObject& obj )
{
  obj.printOn( writer );
  return writer;
}

bool UObject::setgraphic( u16 /*newgraphic*/ )
{
  ERROR_PRINT.Format(
      "UOBject::SetGraphic used, object class does not have a graphic member! Object Serial: "
      "0x{:X}\n" )
      << serial;
  return false;
}

bool UObject::setcolor( u16 newcolor )
{
  set_dirty();

  if ( color != newcolor )
  {
    color = newcolor;
    on_color_changed();
  }

  return true;
}

void UObject::on_color_changed() {}

void UObject::on_facing_changed() {}

bool UObject::saveonexit() const
{
  return flags_.get( OBJ_FLAGS::SAVE_ON_EXIT );
}

void UObject::saveonexit( bool newvalue )
{
  flags_.change( OBJ_FLAGS::SAVE_ON_EXIT, newvalue );
}

const char* UObject::target_tag() const
{
  return "object";
}

bool UObject::get_method_hook( const char* methodname, Bscript::Executor* ex, ExportScript** hook,
                               unsigned int* PC ) const
{
  return gamestate.system_hooks.get_method_hook( gamestate.system_hooks.uobject_method_script.get(),
                                                 methodname, ex, hook, PC );
}
}  // namespace Core
}  // namespace Pol
