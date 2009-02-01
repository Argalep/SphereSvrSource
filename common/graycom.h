//
// graycom.h
// Copyright Menace Software (www.menasoft.com).
// common header file.
//

#ifndef _INC_GRAYCOM_H
#define _INC_GRAYCOM_H
#pragma once

//---------------------------SYSTEM DEFINITIONS---------------------------

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <ctype.h>
#include <assert.h>

#ifdef _WIN32

	#ifndef STRICT
		#define STRICT			// strict conversion of handles and pointers.
	#endif	// STRICT

	#include <io.h>
	#include <winsock2.h>
	#include <windows.h>
	#include <dos.h>
	#include <limits.h>
	#include <conio.h>
	#include <sys/timeb.h>

	#define strcmpi		_strcmpi	// Non ANSI equiv functions ?
	#define strnicmp	_strnicmp

	extern const OSVERSIONINFO * GRAY_GetOSInfo();

#else	// _WIN32 else assume LINUX

	#include <sys/types.h>
	#include <sys/timeb.h>

	#define HANDLE			DWORD
	#define _cdecl
	#define __cdecl
	#define LONG			DWORD
	#define LONGLONG		DWORD	// This should be 64 bit ???
	#define WCHAR			unsigned short
	#define FAR
	#define E_FAIL			0x80004005
	#define BOOL			unsigned short

	#ifdef _BSD
		int getTimezone();
		#define _timezone		getTimezone()
	#else
		#define _timezone		timezone
	#endif

	#define PUINT			unsigned int *
	#define LPTSTR			LPCTSTR

	#define IsBadReadPtr( p, len )		((p) == NULL)
	#define IsBadStringPtr( p, len )	((p) == NULL)
	#define Sleep(mSec)								usleep(mSec*1000)	// arg is microseconds = 1/1000000

	#define strcmpi		strcasecmp
	#define strnicmp	strncasecmp
	#define _vsnprintf	vsnprintf

	#ifndef INT_MIN	// now in limits.h
		#define INT_MIN			(-2147483647) // - 1)
		#define INT_MAX       2147483647    /* maximum (signed) int value */
	#endif
	#ifndef SHRT_MIN
		#define SHRT_MIN    (-32768)        /* minimum (signed) short value */
		#define SHRT_MAX      32767         /* maximum (signed) short value */
		#define USHRT_MAX	0xffff
	#endif
#endif // !_WIN32

#ifdef _DEBUG
	#ifndef ASSERT
		extern void Assert_CheckFail( const char * pExp, const char *pFile, unsigned uLine );
		#define ASSERT(exp)			(void)( (exp) || (Assert_CheckFail(#exp, __FILE__, __LINE__), 0) )
	#endif	// ASSERT

	#ifndef STATIC_CAST
		#define STATIC_CAST dynamic_cast
	#endif

#else	// _DEBUG

	#ifndef ASSERT
		#ifndef _WIN32
			// In linux, if we get an access violation, an exception isn't thrown.  Instead, we get
			// a SIG_SEGV, and the process cores. The following code takes care of this for us.
			extern void Assert_CheckFail( const char * pExp, const char *pFile, unsigned uLine );
			#define ASSERT(exp)			(void)( (exp) || (Assert_CheckFail(#exp, __FILE__, __LINE__), 0) )
		#else
			#define ASSERT(exp)
		#endif
	#endif	// ASSERT

	#ifndef STATIC_CAST
		#define STATIC_CAST static_cast
	#endif

#endif	// ! _DEBUG

#ifdef _WIN32
	#define ATOI atoi
	#define ITOA _itoa
	#define LTOA _ltoa
	#define STRREV _strrev
#else
	int ATOI( const char * str );
	char * ITOA(int value, char *string, int radix);
	char * LTOA(long value, char *string, int radix);
	void STRREV( char* string );
#endif

// Macro for fast NoCrypt Client version check
#define IsClientVer( value )		( m_Crypt.GetClientVer() && m_Crypt.GetClientVer() >= (value) )
#define IsClientLessVer( value )	( m_Crypt.GetClientVer() && m_Crypt.GetClientVer() < (value) )
#define IsNoCryptVer( value )		( !m_Crypt.GetClientVer() && m_reportedCliver >= (value) )
#define IsNoCryptLessVer( value )	( !m_Crypt.GetClientVer() && m_reportedCliver < (value) )
#define IsClientVersion( _x_ )		( IsClientVer(_x_) || IsNoCryptVer(_x_) )
#define IsAosFlagEnabled( value )	( g_Cfg.m_iFeatureAOS & (value) )
#define IsResClient( value )		( GetAccount()->GetResDisp() >= (value) )

#define FEATURE_T2A_UPDATE 		0x01
#define FEATURE_T2A_CHAT 		0x02
#define FEATURE_LBR_UPDATE		0x01
#define FEATURE_LBR_SOUND		0x02

#define FEATURE_AOS_UPDATE_A	0x01	// AOS Monsters, Map, Skills
#define FEATURE_AOS_UPDATE_B	0x02	// Tooltip, Fightbook, Necro/paladin on creation, Single/Six char selection screen
#define FEATURE_AOS_POPUP		0x04	// PopUp Menus
#define FEATURE_AOS_DAMAGE		0x08

#define FEATURE_SE_UPDATE		0x01	// 0x08 in 0xA9
#define FEATURE_SE_NINJASAM		0x02	// 0x040 in feature
#define FEATURE_ML_UPDATE		0x01 	// 0x100 on charlist and 0x0080 for feature to activate
#define FEATURE_ML_NINTHAGE		0x02	// 0x200 feature (unlock house designer items)
#define FEATURE_KR_UPDATE		0x01	// 0x200 | 0x400 in 0xA9 (KR crapness)

#include "common.h"
#include "CException.h"

#include "CSocket.h"
#include "CEncrypt.h"

#include "CArray.h"
#include "CString.h"
#include "CFile.h"
#include "CScript.h"

class CTextConsole; // swapped these two includes, so need to declare this here
#include "CVarDefMap.h"
#include "ListDefContMap.h"
#include "CExpression.h"
#include "CVarFloat.h"
#include "CScriptObj.h"

class CObjBase;
class CChar;
class CItem;
class CResourceDef;

struct CGrayUIDBase		// A unique system serial id. 4 bytes long
{
	// This is a ref to a game object. It may or may not be valid.
	// The top few bits are just flags.
#define UID_CLEAR			0
#define UID_UNUSED			0xFFFFFFFF	// 0 = not used as well.

#define UID_F_RESOURCE		0x80000000	// ALSO: pileable or special macro flag passed to client.
#define UID_F_ITEM			0x40000000	// CItem as apposed to CChar based

#define UID_O_EQUIPPED		0x20000000	// This item is equipped.
#define UID_O_CONTAINED		0x10000000	// This item is inside another container
#define UID_O_DISCONNECT	0x30000000	// Not attached yet.
#define UID_O_INDEX_MASK	0x0FFFFFFF	// lose the upper bits.
#define UID_O_INDEX_FREE	0x01000000	// Spellbook needs unused UID's ?

protected:
	DWORD m_dwInternalVal;
public:

	bool IsValidUID() const
	{
		return( m_dwInternalVal && ( m_dwInternalVal & UID_O_INDEX_MASK ) != UID_O_INDEX_MASK );
	}
	void InitUID()
	{
		m_dwInternalVal = UID_UNUSED;
	}
	void ClearUID()
	{
		m_dwInternalVal = UID_CLEAR;
	}

	bool IsResource() const
	{
		if ( m_dwInternalVal & UID_F_RESOURCE )
			return( IsValidUID() );
		return( false );
	}
	bool IsItem() const	// Item vs. Char
	{
		if (( m_dwInternalVal & (UID_F_ITEM|UID_F_RESOURCE)) == UID_F_ITEM )
			return( true );	// might be static in client ?
		return( false );
	}
	bool IsChar() const	// Item vs. Char
	{
		if (( m_dwInternalVal & (UID_F_ITEM|UID_F_RESOURCE)) == 0 )
			return( IsValidUID());
		return( false );
	}

	bool IsObjDisconnected() const	// Not in the game world for some reason.
	{
		if (( m_dwInternalVal & (UID_F_RESOURCE|UID_O_DISCONNECT)) == UID_O_DISCONNECT )
			return( true );
		return( false );
	}
	bool IsObjTopLevel() const	// on the ground in the world.
	{
		if (( m_dwInternalVal & (UID_F_RESOURCE|UID_O_DISCONNECT)) == 0 )
			return( true );	// might be static in client ?
		return( false );
	}

	bool IsItemEquipped() const
	{
		if (( m_dwInternalVal & (UID_F_RESOURCE|UID_F_ITEM|UID_O_DISCONNECT)) == (UID_F_ITEM|UID_O_EQUIPPED))
			return( IsValidUID() );
		return( false );
	}
	bool IsItemInContainer() const
	{
		if (( m_dwInternalVal & (UID_F_RESOURCE|UID_F_ITEM|UID_O_DISCONNECT)) == (UID_F_ITEM|UID_O_CONTAINED))
			return( IsValidUID() );
		return( false );
	}

	void SetObjContainerFlags( DWORD dwFlags = 0 )
	{
		m_dwInternalVal = ( m_dwInternalVal & (UID_O_INDEX_MASK|UID_F_ITEM )) | dwFlags;
	}

	void SetPrivateUID( DWORD dwVal )
	{
		m_dwInternalVal = dwVal;
	}
	DWORD GetPrivateUID() const
	{
		return m_dwInternalVal;
	}

	DWORD GetObjUID() const
	{
		return( m_dwInternalVal & (UID_O_INDEX_MASK|UID_F_ITEM) );
	}
	void SetObjUID( DWORD dwVal )
	{
		// can be set to -1 by the client.
		m_dwInternalVal = ( dwVal & (UID_O_INDEX_MASK|UID_F_ITEM)) | UID_O_DISCONNECT;
	}

	bool operator == ( DWORD index ) const
	{
		return( GetObjUID() == index );
	}
	bool operator != ( DWORD index ) const
	{
		return( GetObjUID() != index );
	}
    operator DWORD () const
    {
		return( GetObjUID());
    }

	CObjBase * ObjFind() const;
	CItem * ItemFind() const; // Does item still exist or has it been deleted
	CChar * CharFind() const; // Does character still exist
};

struct CGrayUID : public CGrayUIDBase
{
	CGrayUID()
	{
		InitUID();
	}
	CGrayUID( DWORD dw )
	{
		SetPrivateUID( dw );
	}
};

#ifdef _DEBUG
	#ifdef _NO_DEBUG_ASSERTS
		#undef ASSERT
		#define ASSERT(exp)
	#endif
#endif

#endif	// _INC_GRAYCOM_H
