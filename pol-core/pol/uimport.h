/** @file
 *
 * @par History
 * - 2011/11/28 MuadDib:   Removed last of uox referencing code.
 */

#ifndef __UIMPORT_H
#define __UIMPORT_H
namespace Pol
{
namespace Core
{
int read_data();
int write_data( unsigned int& dirty_writes, unsigned int& clean_writes, long long& elapsed_ms );

void read_starting_locations();
void read_gameservers();
void read_character( Clib::ConfigElem& elem );
void read_global_item( Clib::ConfigElem& elem, int /*sysfind_flags*/ );
bool rename_txt_file( const std::string& basename );
bool BackupSQLiteDatabase();
}
namespace Accounts
{
void read_account_data();
}
}
#endif
