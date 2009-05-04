//
// CServer.cpp
// Copyright Menace Software (www.menasoft.com).
//

#include "graysvr.h"	// predef header.

#include "../common/grayver.h"	// sphere version
#include "../common/CAssoc.h"
#include "../common/CFileList.h"
#ifdef _SUBVERSION
	#include "../common/subversion/SvnRevision.h"
#endif

#ifdef _WIN32
	#include "ntservice.h"	// g_Service
#endif

#if defined(_WIN32) && !defined(_DEBUG)
	#include "../common/crashdump/crashdump.h"
#endif

#ifndef _WIN32
	extern LinuxEv g_NetworkEvent;
#endif

////////////////////////////////////////////////////////
// -CTextConsole

CChar * CTextConsole::GetChar() const
{
	ADDTOCALLSTACK("CTextConsole::GetChar");
	return( const_cast <CChar *>( dynamic_cast <const CChar *>( this )));
}

int CTextConsole::OnConsoleKey( CGString & sText, TCHAR nChar, bool fEcho )
{
	ADDTOCALLSTACK("CTextConsole::OnConsoleKey");
	// eventaully we should call OnConsoleCmd
	// RETURN:
	//  0 = dump this connection.
	//  1 = keep processing.
	//  2 = process this.

	if ( sText.GetLength() >= SCRIPT_MAX_LINE_LEN )
	{
commandtoolong:
		SysMessage( "Command too long\n" );

		sText.Empty();
		return( 0 );
	}

	if ( nChar == '\r' || nChar == '\n' )
	{
		// Ignore the character if we have no text stored
		if (!sText.GetLength())
			return 1;

		if ( fEcho )
		{
			SysMessage("\n");
		}
		return 2;
	}
	else if ( nChar == 9 )			// TAB (auto-completion)
	{
		LPCTSTR	p;
		LPCTSTR tmp;
		int		inputLen;
		bool	matched(false);

		//	extract up to start of the word
		p = sText.GetPtr() + sText.GetLength();
		while (( p >= sText.GetPtr() ) && ( *p != '.' ) && ( *p != ' ' ) && ( *p != '/' ) && ( *p != '=' )) p--;
		p++;
		inputLen = strlen(p);

		// search in the auto-complete list for starting on P, and save coords of 1st and Last matched
		CGStringListRec	*firstmatch = NULL;
		CGStringListRec	*lastmatch = NULL;
		CGStringListRec	*curmatch = NULL;	// the one that should be set
		for ( curmatch = g_AutoComplete.GetHead(); curmatch != NULL; curmatch = curmatch->GetNext() )
		{
			if ( !strnicmp(curmatch->GetPtr(), p, inputLen) )	// matched
			{
				if ( firstmatch == NULL ) firstmatch = lastmatch = curmatch;
				else lastmatch = curmatch;
			}
			else if ( lastmatch ) break;					// if no longer matches - save time by instant quit
		}

		if (( firstmatch != NULL ) && ( firstmatch == lastmatch ))	// there IS a match and the ONLY
		{
			tmp = firstmatch->GetPtr() + inputLen;
			matched = true;
		}
		else if ( firstmatch != NULL )						// also make SE (if SERV/SERVER in dic) to become SERV
		{
			p = tmp = firstmatch->GetPtr();
			tmp += inputLen;
			inputLen = strlen(p);
			matched = true;
			for ( curmatch = firstmatch->GetNext(); curmatch != lastmatch->GetNext(); curmatch = curmatch->GetNext() )
			{
				if ( strnicmp(curmatch->GetPtr(), p, inputLen) )	// mismatched
				{
					matched = false;
					break;
				}
			}
		}

		if ( matched )
		{
			if ( fEcho ) SysMessage(tmp);
			sText += tmp;
			if ( sText.GetLength() > SCRIPT_MAX_LINE_LEN ) goto commandtoolong;
		}
		return 1;
	}

	if ( fEcho )
	{
		// Echo
		TCHAR szTmp[2];
		szTmp[0] = nChar;
		szTmp[1] = '\0';
		SysMessage( szTmp );
	}

	if ( nChar == 8 )
	{
		if ( sText.GetLength())	// back key
		{
			sText.SetLength( sText.GetLength() - 1 );
		}
		return 1;
	}

	sText += nChar;
	return 1;
}

////////////////////////////////////////////////////////////////////////////////////////
// -CServer

CServer::CServer() : CServerDef( GRAY_TITLE, CSocketAddressIP( SOCKET_LOCAL_ADDRESS ))
{
	m_iExitFlag = 0;
	m_fResyncPause = false;
	m_fResyncRequested = NULL;

	m_iAdminClients = 0;

	m_timeShutdown.Init();

	m_fConsoleTextReadyFlag = false;

	// we are in start up mode. // IsLoading()
	SetServerMode( SERVMODE_Loading );

	memset(m_PacketFilter, 0, sizeof(m_PacketFilter));
}

CServer::~CServer()
{
}

void CServer::SetSignals( bool fMsg )
{
	// We have just started or we changed Secure mode.

	// set_terminate(  &Exception_Terminate );
	// set_unexpected( &Exception_Unexpected );

	SetUnixSignals(g_Cfg.m_fSecure);
#ifndef _WIN32
	if ( g_Cfg.m_fSecure )
	{
		g_Log.Event( (IsLoading() ? 0 : LOGL_EVENT) | LOGM_INIT, "Signal handlers installed.\n" );
	}
	else
	{
		g_Log.Event( (IsLoading() ? 0 : LOGL_EVENT) | LOGM_INIT, "Signal handlers UNinstalled.\n" );
	}
#endif

	if ( fMsg && !IsLoading() )
	{
		g_World.Broadcast( g_Cfg.m_fSecure ?
			"The world is now running in SECURE MODE" :
			"WARNING: The world is NOT running in SECURE MODE" );
	}
}

void CServer::SetServerMode( SERVMODE_TYPE mode )
{
	ADDTOCALLSTACK("CServer::SetServerMode");
	m_iModeCode = mode;
#ifdef _WIN32
	NTWindow_SetWindowTitle();
#endif
}

bool CServer::IsValidBusy() const
{
	// We might appear to be stopped but it's really ok ?
	// ?
	switch ( m_iModeCode )
	{
	case SERVMODE_Saving:
		if ( g_World.IsSaving())
			return true;
		break;
	case SERVMODE_Loading:
	case SERVMODE_RestockAll:	// these may look stuck but are not.
		return( true );
	}
	return( false );
}

void CServer::SetExitFlag( int iFlag )
{
	ADDTOCALLSTACK("CServer::SetExitFlag");
	if ( m_iExitFlag )
		return;
	m_iExitFlag = iFlag;
}

void CServer::Shutdown( int iMinutes ) // If shutdown is initialized
{
	ADDTOCALLSTACK("CServer::Shutdown");
	if ( iMinutes == 0 )
	{
		if ( ! m_timeShutdown.IsTimeValid() )
			return;
		m_timeShutdown.Init();
		g_World.Broadcast( g_Cfg.GetDefaultMsg( DEFMSG_SERV_SHUTDOWN_CANCEL ) );
		return;
	}

	if ( iMinutes < 0 )
	{
		iMinutes = g_World.GetTimeDiff( m_timeShutdown ) / ( 60 * TICK_PER_SEC );
	}
	else
	{
		m_timeShutdown = CServTime::GetCurrentTime() + ( iMinutes * 60 * TICK_PER_SEC );
	}

	TCHAR *pszTemp = Str_GetTemp();
	sprintf(pszTemp, g_Cfg.GetDefaultMsg( DEFMSG_SERV_SHUTDOWN ), iMinutes);
	g_World.Broadcast(pszTemp);
}

void CServer::SysMessage( LPCTSTR pszMsg ) const
{
	// Print just to the main console.
	if ( !pszMsg || ISINTRESOURCE(pszMsg) )
		return;

#ifdef _WIN32
	NTWindow_PostMsg(pszMsg);
#else
	fputs(pszMsg, stdout);
#endif
}

void CServer::PrintTelnet( LPCTSTR pszMsg ) const
{
	if ( ! m_iAdminClients )
		return;

	for ( CClient * pClient = GetClientHead(); pClient!=NULL; pClient = pClient->GetNext())
	{
		if (( pClient->GetConnectType() == CONNECT_TELNET ) && pClient->GetAccount() )
		{
			if ( !pClient->GetAccount()->IsPriv(PRIV_TELNET_SHORT) )	// this client accepts broadcasts
				pClient->SysMessage(pszMsg);
		}
	}
}

void CServer::PrintStr( LPCTSTR pszMsg ) const
{
	// print to all consoles.
	SysMessage( pszMsg );
	PrintTelnet( pszMsg );
}

int CServer::PrintPercent( long iCount, long iTotal )
{
	ADDTOCALLSTACK("CServer::PrintPercent");
	// These vals can get very large. so use MulDiv to prevent overflow. (not IMULDIV)
	if ( iTotal <= 0 )
		return 100;

    int iPercent = MulDiv( iCount, 100, iTotal );
	TCHAR *pszTemp = Str_GetTemp();
	sprintf(pszTemp, "%d%%", iPercent);
	int len = strlen(pszTemp);

	PrintTelnet(pszTemp);

#ifndef _WIN32
	if( g_Log.m_fColoredConsole )
	{
		SysMessage(pszTemp);
#endif
		while ( len-- )	// backspace it
		{
			PrintTelnet("\x08");
#ifndef _WIN32
 			SysMessage("\x08");
#endif
		}
#ifndef _WIN32
	}
#endif

#ifdef _WIN32
	NTWindow_SetWindowTitle(pszTemp);
	g_Service.OnTick();
#endif
	return iPercent;
}

int CServer::GetAgeHours() const
{
	ADDTOCALLSTACK("CServer::GetAgeHours");
	return( CServTime::GetCurrentTime().GetTimeRaw() / (60*60*TICK_PER_SEC));
}

LPCTSTR CServer::GetStatusString( BYTE iIndex ) const
{
	ADDTOCALLSTACK("CServer::GetStatusString");
	// NOTE: The key names should match those in CServerDef::r_LoadVal
	// A ping will return this as well.
	// 0 or 0x21 = main status.

	TCHAR * pTemp = Str_GetTemp();
	int		iClients	= StatGet(SERV_STAT_CLIENTS);
	int		iHours		= GetAgeHours()/24;

	switch ( iIndex )
	{
		case 0x21:	// '!'
			// typical (first time) poll response.
			{
				TCHAR szVersion[128];
				sprintf( pTemp, GRAY_TITLE ", Name=%s, Port=%d, Ver=" GRAY_VERSION ", TZ=%d, EMail=%s, URL=%s, Lang=%s, CliVer=%s\n",
					GetName(), m_ip.GetPort(), m_TimeZone, (LPCTSTR) m_sEMail, (LPCTSTR) m_sURL, (LPCTSTR) m_sLang,
					m_ClientVersion.WriteClientVer(szVersion) );
			}
			break;
		case 0x22: // '"'
			{
			// shown in the INFO page in game.
			sprintf( pTemp, GRAY_TITLE ", Name=%s, Age=%i, Clients=%i, Items=%i, Chars=%i, Mem=%iK\n",
				GetName(), iHours, iClients, StatGet(SERV_STAT_ITEMS), StatGet(SERV_STAT_CHARS), StatGet(SERV_STAT_MEM) );
			}
			break;
		case 0x24: // '$'
			// show at startup.
			sprintf( pTemp, "Admin=%s, URL=%s, Lang=%s, TZ=%d\n", (LPCTSTR) m_sEMail, (LPCTSTR) m_sURL, (LPCTSTR) m_sLang, m_TimeZone );
			break;
		case 0x25: // '%'
			// ConnectUO Status string
			sprintf( pTemp, GRAY_TITLE " Items=%i, Mobiles=%i, Clients=%i, Mem=%i", StatGet(SERV_STAT_ITEMS), StatGet(SERV_STAT_CHARS), iClients, StatGet(SERV_STAT_MEM) );
			break;
	}

	return( pTemp );
}

//*********************************************************

void CServer::ListClients( CTextConsole * pConsole ) const
{
	ADDTOCALLSTACK("CServer::ListClients");
	// Mask which clients we want ?
	// Give a format of what info we want to SHOW ?
	if ( !pConsole )
		return;

	CChar * pCharCmd = pConsole->GetChar();

	TCHAR *pszMsg = Str_GetTemp();
	TCHAR *tmpMsg = Str_GetTemp();
	int numClients = 0;
	for ( CClient * pClient = GetClientHead(); pClient!=NULL; pClient = pClient->GetNext())
	{
		numClients++;
		CChar * pChar = pClient->GetChar();
		if ( pChar )
		{
			if ( pCharCmd &&
				! pCharCmd->CanDisturb( pChar ))
			{
				continue;
			}

			TCHAR chRank = '=';
			if ( pClient->IsPriv(PRIV_GM) || pClient->GetPrivLevel() >= PLEVEL_Counsel )
				chRank = pChar->IsStatFlag(STATF_Insubstantial) ? '*' : '+';

			sprintf(pszMsg, "%s%x:Acc%c'%s', (%s) Char='%s',(%s)\n",
				pszMsg,
				pClient->m_Socket.GetSocket(),
				chRank,
				(LPCTSTR) pClient->GetAccount()->GetName(),
				(LPCTSTR) pClient->m_PeerName.GetAddrStr(),
				(LPCTSTR) pChar->GetName(),
				(LPCTSTR) pChar->GetTopPoint().WriteUsed());
		}
		else
		{
			if ( pConsole->GetPrivLevel() < pClient->GetPrivLevel())
			{
				continue;
			}
			LPCTSTR pszState;
			switch ( pClient->GetConnectType() )
			{
			case CONNECT_TELNET:	pszState = "TelNet"; break;
			case CONNECT_HTTP:		pszState = "Web"; break;
			default: pszState = "NOT LOGGED IN"; break;
			}

			sprintf(pszMsg, "%s%x:Acc='%s', (%s) %s\n",
				pszMsg,
				pClient->m_Socket.GetSocket(),
				pClient->GetAccount() ? (LPCTSTR) pClient->GetAccount()->GetName() : "<NA>",
				(LPCTSTR) pClient->m_PeerName.GetAddrStr(),
				(LPCTSTR) pszState );
		}
	}

	if (numClients == 0)
	{
		sprintf(tmpMsg, "%s\n", g_Cfg.GetDefaultMsg( DEFMSG_HL_NO_CLIENT ) );
	} else if (numClients == 1)
	{
		sprintf(tmpMsg, "%s\n", g_Cfg.GetDefaultMsg( DEFMSG_HL_ONE_CLIENT ));
	} else
	{
		sprintf(tmpMsg, "%s %d\n", g_Cfg.GetDefaultMsg( DEFMSG_HL_MANY_CLIENTS ), numClients);
	}

	pConsole->SysMessage(tmpMsg);
	pConsole->SysMessage(pszMsg);
}

bool CServer::OnConsoleCmd( CGString & sText, CTextConsole * pSrc )
{
	ADDTOCALLSTACK("CServer::OnConsoleCmd");
	// RETURN: false = boot the client.
	int		len = sText.GetLength();

	// We can't have a command with no length
	if ( len <= 0 )
		return true;

	// Convert first character to lowercase
	char	low = tolower(sText[0]);

	if ((( len > 2 ) || (( len == 2 ) && ( sText[1] != '#' ))) && ( sText[0] != 'd' )) goto longcommand;

	switch ( low )
	{
		case '?':
			pSrc->SysMessagef(
				"Available Commands:\n"
				"# = Immediate Save world (## to save both world and statics)\n"
				"A = Accounts file update\n"
				"B message = Broadcast a message\n"
				"C = Clients List (%d)\n"
				"D = Dump data to external file (DA to dump areas)\n"
				"E = Clear internal variables (like script profile)\n"
				"G = Garbage collection\n"
				"H = Hear all that is said (%s)\n"
				"I = Information\n"
				"L = Toggle log file (%s)\n"
				"P = Profile Info (%s) (P# to dump to profiler_dump.txt)\n"
				"R = Resync Pause\n"
				"S = Secure mode toggle (%s)\n"
				"T = List of active Threads\n"
				"X = immediate exit of the server (X# to save world and statics before exit)\n"
				,
				m_Clients.GetCount(),
				g_Log.IsLoggedMask( LOGM_PLAYER_SPEAK ) ? "ON" : "OFF",
				g_Log.IsFileOpen() ? "OPEN" : "CLOSED",
				m_Profile.IsActive() ? "ON" : "OFF",
				g_Cfg.m_fSecure ? "ON" : "OFF"
				);
			break;
		case '#':	//	# - save world, ## - save both world and statics
			{		// Start a syncronous save or finish a background save synchronously
				if ( g_Serv.m_fResyncPause )
					goto do_resync;

				g_World.Save(true);
				if (( len > 1 ) && ( sText[1] == '#' ))
					g_World.SaveStatics();	// ## means
			} break;
		case 'a':
			{
				// Force periodic stuff
				g_Accounts.Account_SaveAll();
				g_Cfg.OnTick(true);
			} break;
		case 'c':	// List all clients on line.
			{
				ListClients( pSrc );
			} break;
		case 'd':
			{
				LPCTSTR		pszKey	= sText;			pszKey++;
				GETNONWHITESPACE( pszKey );
				if ( tolower(*pszKey) == 'a' )
				{
					pszKey++;	GETNONWHITESPACE( pszKey );
					if ( !g_World.DumpAreas( pSrc, pszKey ) )
						pSrc->SysMessage( "Area dump failed.\n" );
					else
						pSrc->SysMessage( "Area dump successful.\n" );
				}
				else
					goto longcommand;
			} break;
		case 'e':
			{
				if ( IsSetEF(EF_Script_Profiler) )
				{
					if ( g_profiler.initstate == 0xf1 )
					{
						TScriptProfiler::TScriptProfilerFunction	*pFun;
						TScriptProfiler::TScriptProfilerTrigger		*pTrig;

						for ( pFun = g_profiler.FunctionsHead; pFun != NULL; pFun = pFun->next )
						{
							pFun->average = pFun->called = pFun->max = pFun->min = pFun->total = 0;
						}
						for ( pTrig = g_profiler.TriggersHead; pTrig != NULL; pTrig = pTrig->next )
						{
							pTrig->average = pTrig->called = pTrig->max = pTrig->min = pTrig->total = 0;
						}

						g_profiler.called = g_profiler.total = 0;
						pSrc->SysMessage("Scripts profiler info cleared\n");
					}
				}
				pSrc->SysMessage("Complete!\n");
			} break;
		case 'g':
			{
				if ( g_Serv.m_fResyncPause )
				{
	do_resync:
					pSrc->SysMessage("Not allowed during resync pause. Use 'R' to restart.\n");
					break;
				}
				if ( g_World.IsSaving() )
				{
	do_saving:
					pSrc->SysMessage("Not allowed during background worldsave. Use '#' to finish.\n");
					break;
				}
				g_World.GarbageCollection();
			} break;
		case 'h':	// Hear all said.
			{
				CScript script( "HEARALL" );
				r_Verb( script, pSrc );
			} break;
		case 'i':
			{
				CScript script( "INFORMATION" );
				r_Verb( script, pSrc );
			} break;
		case 'l': // Turn the log file on or off.
			{
				CScript script( "LOG" );
				r_Verb( script, pSrc );
			} break;
		case 'p':	// Display profile information.
			{
				ProfileDump(pSrc, ( ( len > 1 ) && ( sText[1] == '#' ) ));
			} break;
		case 'r':	// resync Pause mode. Allows resync of things in files.
			{
				if ( !m_fResyncPause && g_World.IsSaving() )
					goto do_saving;
				SetResyncPause(!m_fResyncPause, pSrc, true);
			} break;
		case 's':
			{
				CScript script( "SECURE" );
				r_Verb( script, pSrc );
			} break;
		case 't':
			{
				pSrc->SysMessagef("Current active threads: %d.\n", ThreadHolder::getActiveThreads());
				for ( int iThreads = 0; iThreads < ThreadHolder::getActiveThreads(); ++iThreads)
				{
					IThread * thrCurrent = ThreadHolder::getThreadAt(iThreads);
					if ( thrCurrent != NULL )
						pSrc->SysMessagef("%d - Id: %d, Priority: %d, Name: %s.\n", iThreads + 1, thrCurrent->getId(), 
											thrCurrent->getPriority(), thrCurrent->getName() );
				}
			} break;
		case 'x':
			{
				if (( len > 1 ) && ( sText[1] == '#' ))	//	X# - exit with save. Such exit is not protected by secure mode
				{
					if ( g_Serv.m_fResyncPause )
						goto do_resync;

					g_World.Save(true);
					g_World.SaveStatics();
					g_Log.Event( LOGL_FATAL, "Immediate Shutdown initialized!\n");
					SetExitFlag(1);
				}
				else if ( g_Cfg.m_fSecure )
				{
					pSrc->SysMessage( "NOTE: Secure mode prevents keyboard exit!\n" );
				}
				else
				{
					g_Log.Event( LOGL_FATAL, "Immediate Shutdown initialized!\n");
					SetExitFlag( 1 );
				}
			} break;
#ifdef _TESTEXCEPTION
		case '%':	// throw simple exception
			{
				throw 0;
			}
		case '^':	// throw language exception (div 0)
			{
				int i = 5;
				int j = 0;
				i /= j;
			}
		case '&':	// throw NULL pointer exception
			{
				CChar *character = NULL;
				character->SetName("yo!");
			}
#endif
		default:
			goto longcommand;
	}
	goto endconsole;

longcommand:
	if (( len > 1 ) && ( sText[1] != ' ' ) || ( low == 'b' ))
	{
		LPCTSTR	pszText = sText;

		if ( !strnicmp(pszText, "tngstrip", 8) )
		{
			int				i = 0;
			CResourceScript	*script;
			FILE			*f, *f1;
			char			*z = Str_GetTemp();
			char			*y = Str_GetTemp();
			char			*x;
			LPCTSTR			dirname;
			if ( g_Cfg.m_sStripPath.IsEmpty() )
			{
				pSrc->SysMessage("StripPath not defined, function aborted.\n");
				return 1;
			}

			dirname = g_Cfg.m_sStripPath;

			strcpy(z, dirname);
			strcat(z, "tngstrip.scp");
			pSrc->SysMessagef("StripFile is %s.\n", z);
			f1 = fopen(z, "w");
			if ( !f1 )
			{
				pSrc->SysMessagef("Cannot open file %s for writing.\n", z);
				return 1;
			}

			while ( script = g_Cfg.GetResourceFile(i++) )
			{
				strcpy(z, script->GetFilePath());
				f = fopen(z, "r");
				if ( !f )
				{
					pSrc->SysMessagef("Cannot open file %s for reading.\n", z);
					continue;
				}

				while ( !feof(f) )
				{
					z[0] = 0;
					y[0] = 0;
					fgets(y, SCRIPT_MAX_LINE_LEN, f);

					x = y;
					while ( (x[0] == 0x20) || (x[0] == 0xA0) || (x[0] == 0x09) )
						x++;
					strcpy(z,x);

					_strlwr(z);

					if ( (( z[0] == '[' ) && strncmp(z, "[eof]", 5)) || !strncmp(z, "defname", 7) ||
						!strncmp(z, "name", 4) || !strncmp(z, "type", 4) || !strncmp(z, "id", 2) ||
						!strncmp(z, "weight", 6) || !strncmp(z, "value", 5) || !strncmp(z, "dam", 3) ||
						!strncmp(z, "armor", 5) || !strncmp(z, "skillmake", 9) || !strncmp(z, "on=@", 4) ||
						!strncmp(z, "dupeitem", 8) || !strncmp(z, "dupelist", 8) || !strncmp(z, "p=", 2) ||
						!strncmp(z, "can", 3) || !strncmp(z, "tevents", 7) || !strncmp(z, "subsection", 10) ||
						!strncmp(z, "description", 11) || !strncmp(z, "category", 8) || !strncmp(z, "color", 5) ||
						!strncmp(z, "resources", 9) )
					{
						fputs(y, f1);
					}
				}
				fclose(f);
			}
			fclose(f1);
			pSrc->SysMessagef("Scripts have just been stripped.\n");
			return 1;
		}
		else if ( !strnicmp(pszText, "strip", 5) )
		{
			int				i = 0;
			CResourceScript	*script;
			FILE			*f, *f1;
			char			*z = Str_GetTemp();
			LPCTSTR			dirname;
			pszText += 5;
			while ( *pszText == ' ' ) pszText++;
			dirname = ( *pszText ? pszText : "" );

			while ( script = g_Cfg.GetResourceFile(i++) )
			{
				strcpy(z, script->GetFilePath());
				f = fopen(z, "r");
				if ( !f )
				{
					pSrc->SysMessagef("Cannot open file %s for reading.\n", z);
					continue;
				}
				strcpy(z, dirname);
				strcat(z, script->GetFilePath());
				if ( !*dirname || !strcmpi(z, script->GetFilePath()) )
					strcat(z, ".strip");	// rename to *.strip if operating the same dir

				f1 = fopen(z, "w");
				if ( !f1 )
				{
					pSrc->SysMessagef("Cannot open file %s for writing.\n", z);
					fclose(f);
					continue;
				}
				while ( !feof(f) )
				{
					z[0] = 0;
					fgets(z, SCRIPT_MAX_LINE_LEN, f);
					_strlwr(z);
					if (( z[0] == '[' ) || !strncmp(z, "defname", 7) || !strncmp(z, "resources", 9) ||
						!strncmp(z, "name=", 5) || !strncmp(z, "type=", 5) || !strncmp(z, "id=", 3) ||
						!strncmp(z, "weight=", 7) || !strncmp(z, "value=", 6) || !strncmp(z, "dam=", 4) ||
						!strncmp(z, "armor=", 6) || !strncmp(z, "skillmake", 9) || !strncmp(z, "on=@", 4) ||
						!strncmp(z, "dupeitem", 8) || !strncmp(z, "dupelist", 8) || !strncmp(z, "id=", 3) ||
						!strncmp(z, "can=", 4) || !strncmp(z, "tevents=", 8) || !strncmp(z, "subsection=", 11) ||
						!strncmp(z, "description=", 12) || !strncmp(z, "category=", 9) || !strncmp(z, "p=", 2) ||
						!strncmp(z, "rect=", 5) || !strncmp(z, "group=", 6) )
					{
						fputs(z, f1);
					}
				}
				fclose(f);
				fclose(f1);
			}
			pSrc->SysMessagef("Scripts have just been stripped.\n");
			return 1;
		}

		if ( g_Cfg.IsConsoleCmd(low) ) pszText++;

		CScript	script(pszText);
		if ( !g_Cfg.CanUsePrivVerb(this, pszText, pSrc) )
		{
			pSrc->SysMessagef("not privileged for command '%s'\n", pszText);
		}
		else if ( !r_Verb(script, pSrc) )
		{
			pSrc->SysMessagef("unknown command '%s'\n", pszText);
		}
	}
	else pSrc->SysMessagef("unknown command '%s'\n", (LPCTSTR)sText);

endconsole:
	sText.Empty();
	return( true );
}

//************************************************

PLEVEL_TYPE CServer::GetPrivLevel() const
{
	return PLEVEL_Owner;
}

void CServer::ProfileDump( CTextConsole * pSrc, bool bDump )
{
	ADDTOCALLSTACK("CServer::ProfileDump");
	if ( !pSrc )
		return;

	bool bOk = bDump;
	CFileText * ftDump;

	if ( bDump )
	{
		ftDump = new CFileText();
		if ( ! ftDump->Open("profiler_dump.txt", OF_CREATE|OF_TEXT) )
		{
			bOk = false;
			delete ftDump;
		}
	}

	pSrc->SysMessagef("Profiles %s: (%d sec total)\n", m_Profile.IsActive() ? "ON" : "OFF", m_Profile.GetActiveWindow());
	if ( bOk )
		ftDump->Printf("Profiles %s: (%d sec total)\n", m_Profile.IsActive() ? "ON" : "OFF", m_Profile.GetActiveWindow());


	for ( int i=0; i < PROFILE_QTY; i++ )
	{
		pSrc->SysMessagef( "%-10s = %s\n", (LPCTSTR) m_Profile.GetName((PROFILE_TYPE) i), (LPCTSTR) m_Profile.GetDesc((PROFILE_TYPE) i ) );
		if ( bOk )
			ftDump->Printf( "%-10s = %s\n", (LPCTSTR) m_Profile.GetName((PROFILE_TYPE) i), (LPCTSTR) m_Profile.GetDesc((PROFILE_TYPE) i ) );
	}

	if ( IsSetEF(EF_Script_Profiler) )
	{
		if ( g_profiler.initstate != 0xf1 ) pSrc->SysMessagef("Scripts profiler is not initialized\n");
		else if ( !g_profiler.called ) pSrc->SysMessagef("Script profiler is not yet informational\n");
		else
		{
			LONGLONG	average = g_profiler.total / g_profiler.called;
			TScriptProfiler::TScriptProfilerFunction	*pFun;
			TScriptProfiler::TScriptProfilerTrigger		*pTrig;
			DWORD	divby(1);

			divby = llTimeProfileFrequency/1000;

			pSrc->SysMessagef( "Scripts: called %d times and took %i.%04i msec (%i.%04i msec average). Reporting with highest average.\n",
					g_profiler.called,
					(int)(g_profiler.total/divby),
					(int)(((g_profiler.total*10000)/(divby))%10000),
					(int)(average/divby),
					(int)(((average*10000)/(divby))%10000)
			);
			if ( bOk )
				ftDump->Printf("Scripts: called %d times and took %i.%04i msec (%i.%04i msec average). Reporting with highest average.\n",
					g_profiler.called,
					(int)(g_profiler.total/divby),
					(int)(((g_profiler.total*10000)/(divby))%10000),
					(int)(average/divby),
					(int)(((average*10000)/(divby))%10000)
				);

			for ( pFun = g_profiler.FunctionsHead; pFun != NULL; pFun = pFun->next )
			{
				if ( pFun->average > average )
				{
					pSrc->SysMessagef( "FUNCTION '%s' called %d times, took %i.%04i msec average (%i.%04i min, %i.%04i max), total: %i.%04i msec\n",
						pFun->name,
						pFun->called,
						(int)(pFun->average/divby),
						(int)(((pFun->average*10000)/(divby))%10000),
						(int)(pFun->min/divby),
						(int)(((pFun->min*10000)/(divby))%10000),
						(int)(pFun->max/divby),
						(int)(((pFun->max*10000)/(divby))%10000),
						(int)(pFun->total/divby),
						(int)(((pFun->total*10000)/(divby))%10000)
					);
					if ( bOk )
						ftDump->Printf("FUNCTION '%s' called %d times, took %i.%04i msec average (%i.%04i min, %i.%04i max), total: %i.%04i msec\n",
							pFun->name,
							pFun->called,
							(int)(pFun->average/divby),
							(int)(((pFun->average*10000)/(divby))%10000),
							(int)(pFun->min/divby),
							(int)(((pFun->min*10000)/(divby))%10000),
							(int)(pFun->max/divby),
							(int)(((pFun->max*10000)/(divby))%10000),
							(int)(pFun->total/divby),
							(int)(((pFun->total*10000)/(divby))%10000)
						);
				}
			}
			for ( pTrig = g_profiler.TriggersHead; pTrig != NULL; pTrig = pTrig->next )
			{
				if ( pTrig->average > average )
				{
					pSrc->SysMessagef( "TRIGGER '%s' called %d times, took %i.%04i msec average (%i.%04i min, %i.%04i max), total: %i.%04i msec\n",
						pTrig->name,
						pTrig->called,
						(int)(pTrig->average/divby),
						(int)(((pTrig->average*10000)/(divby))%10000),
						(int)(pTrig->min/divby),
						(int)(((pTrig->min*10000)/(divby))%10000),
						(int)(pTrig->max/divby),
						(int)(((pTrig->max*10000)/(divby))%10000),
						(int)(pTrig->total/divby),
						(int)(((pTrig->total*10000)/(divby))%10000)
					);
					if ( bOk )
						ftDump->Printf("TRIGGER '%s' called %d times, took %i.%04i msec average (%i.%04i min, %i.%04i max), total: %i.%04i msec\n",
							pTrig->name,
							pTrig->called,
							(int)(pTrig->average/divby),
							(int)(((pTrig->average*10000)/(divby))%10000),
							(int)(pTrig->min/divby),
							(int)(((pTrig->min*10000)/(divby))%10000),
							(int)(pTrig->max/divby),
							(int)(((pTrig->max*10000)/(divby))%10000),
							(int)(pTrig->total/divby),
							(int)(((pTrig->total*10000)/(divby))%10000)
						);
				}
			}

			pSrc->SysMessage("Report complete!\n");
		}
	}
	else pSrc->SysMessage("Script profiler is turned OFF\n");

	if ( bOk && ftDump )
	{
		ftDump->Close();
		delete ftDump;
	}
}

// ---------------------------------------------------------------------

bool CServer::r_GetRef( LPCTSTR & pszKey, CScriptObj * & pRef )
{
	ADDTOCALLSTACK("CServer::r_GetRef");
	if ( IsDigit( pszKey[0] ))
	{
		int i=1;
		while ( IsDigit( pszKey[i] ))
			i++;
		if ( pszKey[i] == '.' )
		{
			int index = ATOI( pszKey );	// must use this to stop at .
			pRef = g_Cfg.Server_GetDef(index);
			pszKey += i + 1;
			return( true );
		}
	}
	if ( g_Cfg.r_GetRef( pszKey, pRef ))
	{
		return( true );
	}
	if ( g_World.r_GetRef( pszKey, pRef ))
	{
		return( true );
	}
	return( CScriptObj::r_GetRef( pszKey, pRef ));
}

bool CServer::r_LoadVal( CScript &s )
{
	ADDTOCALLSTACK("CServer::r_LoadVal");

	if ( g_Cfg.r_LoadVal(s) )
		return true;
	if ( g_World.r_LoadVal(s) )
		return true;
	return CServerDef::r_LoadVal(s);
}

bool CServer::r_WriteVal( LPCTSTR pszKey, CGString & sVal, CTextConsole * pSrc )
{
	ADDTOCALLSTACK("CServer::r_WriteVal");
	if ( !strnicmp(pszKey, "ACCOUNT.", 8) )
	{
		pszKey += 8;
		CAccountRef pAccount = NULL;
		
		// extract account name/index to a temporary buffer
		char *pszTemp = Str_GetTemp();
		char *pszTempStart = pszTemp;

		strcpy(pszTemp, pszKey);
		char *split = strchr(pszTemp, '.');
		if ( split )
			*split = 0;

		// adjust pszKey to point to end of account name/index
		pszKey += strlen(pszTemp);

		//	try to fetch using indexes
		if (( *pszTemp >= '0' ) && ( *pszTemp <= '9' ))
		{
			int num = Exp_GetVal(pszTemp);
			if ((*pszTemp == '\0') && ( num >= 0 ) && ( num < g_Accounts.Account_GetCount() ))
				pAccount = g_Accounts.Account_Get(num);
		}

		//	indexes failed, try to fetch using names
		if ( !pAccount )
		{
			pszTemp = pszTempStart;
			pAccount = g_Accounts.Account_Find(pszTemp);
		}

		if ( !*pszKey) // we're just checking if the account exists
		{
			sVal.FormatVal( (pAccount? 1 : 0) );
			return true;
		}
		else if ( pAccount ) // we're retrieving a property from the account
		{
			SKIP_SEPARATORS(pszKey);
			return pAccount->r_WriteVal(pszKey, sVal, pSrc);
		}
		// we're trying to retrieve a property from an invalid account
		return false;
	}

	// Just do stats values for now.
	if ( g_Cfg.r_WriteVal(pszKey, sVal, pSrc) )
		return true;
	if ( g_World.r_WriteVal(pszKey, sVal, pSrc) )
		return true;
	return CServerDef::r_WriteVal(pszKey, sVal, pSrc);
}

enum SV_TYPE
{
	SV_ACCOUNT,
	SV_ALLCLIENTS,
	SV_B,
	SV_BLOCKIP,
	SV_CLEARLISTS,
	SV_CONSOLE,
#ifdef _TEST_EXCEPTION
	SV_CRASH,
#endif
	SV_EXPORT,
	SV_GARBAGE,
	SV_HEARALL,
	SV_IMPORT,
	SV_INFORMATION,
	SV_LOAD,
	SV_LOG,
	SV_PRINTLISTS,
	SV_RESPAWN,
	SV_RESTOCK,
	SV_RESTORE,
	SV_RESYNC,
	SV_SAVE,
	SV_SAVESTATICS,
	SV_SECURE,
	SV_SHRINKMEM,
	SV_SHUTDOWN,
	SV_UNBLOCKIP,
	SV_VARLIST,
	SV_QTY,
};

LPCTSTR const CServer::sm_szVerbKeys[SV_QTY+1] =
{
	"ACCOUNT",
	"ALLCLIENTS",
	"B",
	"BLOCKIP",
	"CLEARLISTS",
	"CONSOLE",
#ifdef _TEST_EXCEPTION
	"CRASH",
#endif
	"EXPORT",
	"GARBAGE",
	"HEARALL",
	"IMPORT",
	"INFORMATION",
	"LOAD",
	"LOG",
	"PRINTLISTS",
	"RESPAWN",
	"RESTOCK",
	"RESTORE",
	"RESYNC",
	"SAVE",
	"SAVESTATICS",
	"SECURE",
	"SHRINKMEM",
	"SHUTDOWN",
	"UNBLOCKIP",
	"VARLIST",
	NULL,
};

bool CServer::r_Verb( CScript &s, CTextConsole * pSrc )
{
	ADDTOCALLSTACK("CServer::r_Verb");
	if ( !pSrc )
		return false;

	EXC_TRY("Verb");
	LPCTSTR pszKey = s.GetKey();
	TCHAR *pszMsg = NULL;

	int index = FindTableSorted( s.GetKey(), sm_szVerbKeys, COUNTOF( sm_szVerbKeys )-1 );

	if ( index < 0 )
	{
		CGString sVal;
		CScriptTriggerArgs Args( s.GetArgRaw() );
		if ( r_Call( pszKey, pSrc, &Args, &sVal ) )
			return true;

		if ( !strnicmp(pszKey, "ACCOUNT.", 8) )
		{
			index = SV_ACCOUNT;
			pszKey += 8;

			CAccountRef pAccount = NULL;
			char *pszTemp = Str_GetTemp();

			strcpy(pszTemp, pszKey);
			char *split = strchr(pszTemp, '.');
			if ( split )
			{
				*split = 0;
				pAccount = g_Accounts.Account_Find(pszTemp);
				pszKey += strlen(pszTemp);
			}

			SKIP_SEPARATORS(pszKey);
			if ( pAccount && *pszKey )
			{
				CScript script(pszKey, s.GetArgStr());
				return pAccount->r_LoadVal(script);
			}
			return false;
		}

		if ( !strnicmp(pszKey, "CLEARVARS", 9) )
		{
			pszKey = s.GetArgStr();
			SKIP_SEPARATORS(pszKey);
			g_Exp.m_VarGlobals.ClearKeys(pszKey);
			return true;
		}
	}

	switch (index)
	{
		case SV_ACCOUNT: // "ACCOUNT"
			return g_Accounts.Account_OnCmd(s.GetArgRaw(), pSrc);

		case SV_ALLCLIENTS:	// "ALLCLIENTS"
			{
				// Send a verb to all clients
				for ( CClient * pClient = g_Serv.GetClientHead(); pClient!=NULL; pClient = pClient->GetNext())
				{
					if ( pClient->GetChar() == NULL )
						continue;
					CScript script( s.GetArgStr() );
					pClient->GetChar()->r_Verb( script, pSrc );
				}
			}
			break;

		case SV_B: // "B"
			g_World.Broadcast( s.GetArgStr());
			break;

		case SV_BLOCKIP:
			{
				if ( pSrc->GetPrivLevel() < PLEVEL_Admin )
					return( false );

				int iTimeDecay = -1;

				TCHAR * ppArgs[2];
				if ( !Str_ParseCmds( s.GetArgRaw(), ppArgs, COUNTOF(ppArgs), ", " ) )
					return( false );

				if ( ppArgs[1] )
					iTimeDecay = Exp_GetVal( ppArgs[1] );

				if ( g_Cfg.SetLogIPBlock( ppArgs[0], true, iTimeDecay ))
				{
					pSrc->SysMessage( "IP Blocked\n" );
				}
				else
				{
					pSrc->SysMessage( "IP Already Blocked\n" );
				}
				break;
			}
		case SV_CONSOLE:
			{
				CGString z = s.GetArgRaw();
				OnConsoleCmd(z, pSrc);
			}
			break;

#ifdef _TEST_EXCEPTION
		case SV_CRASH:
			{
				g_Log.Event(0, "Testing exceptions.\n");
				pSrc = NULL;
				pSrc->GetChar();
			} break;
#endif

		case SV_EXPORT: // "EXPORT" name [chars] [area distance]
			if ( pSrc->GetPrivLevel() < PLEVEL_Admin )
				return( false );
			if ( s.HasArgs())
			{
				TCHAR * Arg_ppCmd[5];
				int Arg_Qty = Str_ParseCmds( s.GetArgRaw(), Arg_ppCmd, COUNTOF( Arg_ppCmd ));
				if ( !Arg_Qty )
					break;
				// IMPFLAGS_ITEMS
				if ( ! g_World.Export( Arg_ppCmd[0], pSrc->GetChar(),
					(Arg_Qty>=2)? ATOI(Arg_ppCmd[1]) : IMPFLAGS_ITEMS,
					(Arg_Qty>=3)? ATOI(Arg_ppCmd[2]) : SHRT_MAX ))
				{
					pSrc->SysMessage( "Export failed\n" );
				}
			}
			else
			{
				pSrc->SysMessage( "EXPORT name [flags] [area distance]" );
			}
			break;
		case SV_GARBAGE:
			if ( g_Serv.m_fResyncPause || g_World.IsSaving() )
			{
				pSrc->SysMessage( "Not allowed during world save and/or resync pause" );
				break;
			}
			else
			{
				g_World.GarbageCollection();
			}
			break;

		case SV_HEARALL:	// "HEARALL" = Hear all said.
			{
				pszMsg = Str_GetTemp();
				g_Log.SetLogMask( s.GetArgFlag( g_Log.GetLogMask(), LOGM_PLAYER_SPEAK ));
				sprintf(pszMsg, "Hear All %s.\n", g_Log.IsLoggedMask(LOGM_PLAYER_SPEAK) ? "Enabled" : "Disabled" );
			}
			break;

		case SV_INFORMATION:
			pSrc->SysMessage( GetStatusString( 0x22 ));
			pSrc->SysMessage( GetStatusString( 0x24 ));
			break;

		case SV_IMPORT: // "IMPORT" name [flags] [area distance]
			if ( pSrc->GetPrivLevel() < PLEVEL_Admin )
				return( false );
			if (s.HasArgs())
			{
				TCHAR * Arg_ppCmd[5];
				int Arg_Qty = Str_ParseCmds( s.GetArgRaw(), Arg_ppCmd, COUNTOF( Arg_ppCmd ));
				if ( ! Arg_Qty )
				{
					break;
				}
				// IMPFLAGS_ITEMS
				if ( ! g_World.Import( Arg_ppCmd[0], pSrc->GetChar(),
					(Arg_Qty>=2)?ATOI(Arg_ppCmd[1]) : IMPFLAGS_BOTH,
					(Arg_Qty>=3)?ATOI(Arg_ppCmd[2]) : SHRT_MAX ))
					pSrc->SysMessage( "Import failed\n" );
			}
			else
			{
				pSrc->SysMessage( "IMPORT name [flags] [area distance]" );
			}
			break;
		case SV_LOAD:
			// Load a resource file.
			if ( g_Cfg.LoadResourcesAdd( s.GetArgStr()) == NULL )
				return( false );
			return( true );

		case SV_LOG:
			{
				LPCTSTR	pszArgs = s.GetArgStr();
				int		mask = LOGM_CLIENTS_LOG|LOGL_EVENT;
				if ( pszArgs && ( *pszArgs == '@' ))
				{
					pszArgs++;
					if ( *pszArgs != '@' )
						mask |= LOGM_NOCONTEXT;
				}
				g_Log.Event(mask, "%s\n", pszArgs);
			}
			break;

		case SV_RESPAWN:
			g_World.RespawnDeadNPCs();
			break;

		case SV_RESTOCK:
			// set restock time of all vendors in World.
			// set the respawn time of all spawns in World.
			g_World.Restock();
			break;

		case SV_RESTORE:	// "RESTORE" backupfile.SCP Account CharName
			if ( pSrc->GetPrivLevel() < PLEVEL_Admin )
				return( false );
			if (s.HasArgs())
			{
				TCHAR * Arg_ppCmd[4];
				int Arg_Qty = Str_ParseCmds( s.GetArgRaw(), Arg_ppCmd, COUNTOF( Arg_ppCmd ));
				if ( ! Arg_Qty )
				{
					break;
				}
				if ( ! g_World.Import( Arg_ppCmd[0], pSrc->GetChar(),
					IMPFLAGS_BOTH|IMPFLAGS_ACCOUNT, SHRT_MAX,
					Arg_ppCmd[1], Arg_ppCmd[2] ))
				{
					pSrc->SysMessage( "Restore failed\n" );
				}
				else
				{
					pSrc->SysMessage( "Restore success\n" );
				}
			}
			break;
		case SV_RESYNC:
			{
				// if a resync occurs whilst a script is executing then bad thing
				// will happen. Flag that a resync has been requested and it will
				// take place during a server tick
				m_fResyncRequested = pSrc;
				break;
			}
		case SV_SAVE: // "SAVE" x
			g_World.Save(s.GetArgVal());
			break;
		case SV_SAVESTATICS:
			g_World.SaveStatics();
			break;
		case SV_SECURE: // "SECURE"
			pszMsg = Str_GetTemp();
			g_Cfg.m_fSecure = s.GetArgFlag( g_Cfg.m_fSecure, true );
			SetSignals();
			sprintf(pszMsg, "Secure mode %s.\n", g_Cfg.m_fSecure ? "re-enabled" : "disabled" );
			break;
		case SV_SHRINKMEM:
			{
#ifdef _WIN32
				if ( GRAY_GetOSInfo()->dwPlatformId != 2 )
				{
					g_Log.EventError( "Command not aviable on Windows 95/98/ME.\n" );
					return( false );
				}

				if ( !SetProcessWorkingSetSize(GetCurrentProcess(),(SIZE_T)-1,(SIZE_T)-1) )
				{
					g_Log.EventError( "Error during process shrink.\n" );
					return false;
				}

				pSrc->SysMessage( "Memory shrinked succesfully.\n" );
#else
				g_Log.EventError( "Command not aviable on *NIX os.\n" );
				return false;
#endif
			} break;

		case SV_SHUTDOWN: // "SHUTDOWN"
			Shutdown(( s.HasArgs()) ? s.GetArgVal() : 15 );
			break;

		case SV_UNBLOCKIP:	// "UNBLOCKIP"
			if ( pSrc->GetPrivLevel() < PLEVEL_Admin )
				return( false );
			if ( g_Cfg.SetLogIPBlock( s.GetArgRaw(), false ))
			{
				pSrc->SysMessage( "IP UN-Blocked\n" );
			}
			else
			{
				pSrc->SysMessage( "IP Was NOT Blocked\n" );
			}
			break;

		case SV_VARLIST:
			if ( ! strcmpi( s.GetArgStr(), "log" ))
				pSrc = (CTextConsole *)&g_Serv;
			g_Exp.m_VarGlobals.DumpKeys(pSrc, "VAR.");
			break;
		case SV_PRINTLISTS:
			if ( ! strcmpi( s.GetArgStr(), "log" ))
				pSrc = (CTextConsole *)&g_Serv;
			g_Exp.m_ListGlobals.DumpKeys(pSrc, "LIST.");
			break;
		case SV_CLEARLISTS:
			g_Exp.m_ListGlobals.ClearKeys(s.GetArgStr());
			break;
		default:
			return CScriptObj::r_Verb(s, pSrc);
	}

	if ( pszMsg && *pszMsg )
		pSrc->SysMessage(pszMsg);

	return true;
	EXC_CATCH;

	EXC_DEBUG_START;
	EXC_ADD_SCRIPTSRC;
	g_Log.EventDebug("source '%s' char '%s' uid '0%lx'\n", (pSrc && pSrc->GetName()) ? pSrc->GetName() : "",
		(pSrc && pSrc->GetChar()) ? pSrc->GetChar()->GetName() : "",
		(pSrc && pSrc->GetChar()) ? (DWORD)pSrc->GetChar()->GetUID() : 0 );
	EXC_DEBUG_END;
	return false;
}

//*********************************************************

extern void defragSphere(char *);

bool CServer::CommandLine( int argc, TCHAR * argv[] )
{
	// Console Command line.
	// This runs after script file enum but before loading the world file.
	// RETURN:
	//  true = keep running after this.

	for ( int argn = 1; argn < argc; argn++ )
	{
		TCHAR * pArg = argv[argn];
		if ( ! _IS_SWITCH(pArg[0]))
			continue;

		pArg++;

		switch ( toupper(pArg[0]) )
		{
			case '?':
				PrintStr( GRAY_TITLE " \n"
					"Command Line Switches:\n"
#ifdef _WIN32
					"-cClassName Setup custom window class name for sphere (default: " GRAY_TITLE "Svr)\n"
#else
					"-c use colored console output (default: off)\n"
#endif
					"-D Dump global variable DEFNAMEs to defs.txt\n"
#if defined(_WIN32) && !defined(_DEBUG)
					"-E Enable the CrashDumper\n"
#endif
					"-Gpath/to/saves/ Defrags sphere saves\n"
#ifdef _WIN32
					"-k install/remove Installs or removes NT Service\n"
#endif
					"-Nstring Set the sphere name.\n"
					"-P# Set the port number.\n"
					"-Ofilename Output console to this file name\n"
					"-Q Quit when finished.\n"
					"-T Run tests and exit (avilable only in test builds).\n"
					);
				return( false );
#ifdef _WIN32
			case 'C':
			case 'K':
				//	these are parsed in other places - nt service, nt window part, etc
				continue;
#else
			case 'C':
				g_Log.m_fColoredConsole = true;
				continue;
#endif
			case 'P':
				m_ip.SetPort(ATOI(pArg+1));
				continue;
			case 'N':
				// Set the system name.
				SetName(pArg+1);
				continue;
			case 'D':
				// dump all the defines to a file.
				{
					CFileText File;
					if ( ! File.Open( "defs.txt", OF_WRITE|OF_TEXT ))
						return( false );
					int i = 0;
					for ( ; i < g_Exp.m_VarDefs.GetCount(); i++ )
					{
						if ( !( i%0x1ff ))
							PrintPercent( i, g_Exp.m_VarDefs.GetCount());

						CVarDefCont * pCont = g_Exp.m_VarDefs.GetAt(i);
						if ( pCont != NULL )
						{
							File.Printf( "%s=%s\n", pCont->GetKey(), pCont->GetValStr());
						}
					}
				}
				continue;
#if defined(_WIN32) && !defined(_DEBUG) && !defined(_NO_CRASHDUMP)
			case 'E':
				CrashDump::Enable();
				continue;
#endif
			case 'G':
				defragSphere(pArg + 1);
				continue;
			case 'O':
				if ( g_Log.Open(pArg+1, OF_SHARE_DENY_WRITE|OF_READWRITE|OF_TEXT) )
					g_Log.m_fLockOpen = true;
				continue;
			case 'Q':
				return false;

			case 'T':
				{
#ifdef VJAKA_REDO
					testThreads();
#endif
				} return false;

			default:
				g_Log.Event( LOGM_INIT|LOGL_CRIT, "Don't recognize command line data '%s'\n", (LPCTSTR)( argv[argn] ));
				break;
		}
	}

	return( true );
}

void CServer::SetResyncPause(bool fPause, CTextConsole * pSrc, bool bMessage)
{
	ADDTOCALLSTACK("CServer::SetResyncPause");
	if ( fPause )
	{
		m_fResyncPause = true;
		g_Serv.SysMessagef("%s\n",g_Cfg.GetDefaultMsg(DEFMSG_SERVER_RESYNC_START));

		if ( bMessage )
			g_World.Broadcast(g_Cfg.GetDefaultMsg(DEFMSG_SERVER_RESYNC_START));
		else if ( pSrc && pSrc->GetChar() )
			pSrc->SysMessage(g_Cfg.GetDefaultMsg(DEFMSG_SERVER_RESYNC_START));

		g_Cfg.Unload(true);
		SetServerMode(SERVMODE_ResyncPause);
	}
	else
	{
		g_Serv.SysMessagef("%s\n",g_Cfg.GetDefaultMsg(DEFMSG_SERVER_RESYNC_RESTART));
		SetServerMode(SERVMODE_ResyncLoad);

		if ( !g_Cfg.Load(true) )
		{
			g_Serv.SysMessagef("%s\n",g_Cfg.GetDefaultMsg(DEFMSG_SERVER_RESYNC_FAILED));
			if ( bMessage )
				g_World.Broadcast(g_Cfg.GetDefaultMsg(DEFMSG_SERVER_RESYNC_FAILED));
			else if ( pSrc && pSrc->GetChar() )
				pSrc->SysMessage(g_Cfg.GetDefaultMsg(DEFMSG_SERVER_RESYNC_FAILED));
		}
		else
		{
			g_Serv.SysMessagef("%s\n",g_Cfg.GetDefaultMsg(DEFMSG_SERVER_RESYNC_SUCCESS));
			if ( bMessage )
				g_World.Broadcast(g_Cfg.GetDefaultMsg(DEFMSG_SERVER_RESYNC_SUCCESS));
			else if ( pSrc && pSrc->GetChar() )
				pSrc->SysMessage(g_Cfg.GetDefaultMsg(DEFMSG_SERVER_RESYNC_SUCCESS));
		}

		m_fResyncPause = false;
		SetServerMode(SERVMODE_Run);
	}
}

//*********************************************************

CClient * CServer::SocketsReceive( CGSocket & socket ) // Check for messages from the clients
{
	ADDTOCALLSTACK("CServer::SocketsReceive");
	CSocketAddress client_addr;
	SOCKET hSocketClient = socket.Accept( client_addr );
	if ( hSocketClient < 0 || hSocketClient == INVALID_SOCKET )
	{
		// NOTE: Client_addr might be invalid.
		g_Log.Event( LOGL_FATAL|LOGM_CLIENTS_LOG, "Failed at client connection to '%s'(?)\n", (LPCTSTR) client_addr.GetAddrStr());
		return NULL;
	}

	CLogIP * pLogIP = g_Cfg.FindLogIP( client_addr, true );
	if ( pLogIP == NULL || pLogIP->CheckPingBlock( true ))
	{
		// kill it by allowing it to go out of scope.
		CGSocket::CloseSocket( hSocketClient );
		return NULL;
	}

	// too many connecting on this IP
	if ( ( (g_Cfg.m_iConnectingMaxIP > 0) && (pLogIP->m_iConnecting > g_Cfg.m_iConnectingMaxIP) )
			|| (  (g_Cfg.m_iClientsMaxIP > 0) && (pLogIP->m_iConnected > g_Cfg.m_iClientsMaxIP) ) )
	{
		// kill
		CGSocket::CloseSocket( hSocketClient );
		return NULL;
	}

	CClient *client = new CClient(hSocketClient);
	return client;
}

//	Berkeley sockets needs nfds to be updated that while in Windows that's ignored
#ifdef _WIN32
#define ADDTOSELECT(_x_)	FD_SET(_x_, &readfds)
#else
#define ADDTOSELECT(_x_)	{ FD_SET(_x_, &readfds); if ( _x_ > nfds ) nfds = _x_; }
#endif

void CServer::SocketsReceive() // Check for messages from the clients
{
	ADDTOCALLSTACK("CServer::SocketsReceive");
	//	Do not accept packets while loading
	if ( m_iModeCode > SERVMODE_ResyncPause ) return;

	// What sockets do I want to look at ?
	fd_set readfds;
	int nfds = 0;
	FD_ZERO(&readfds);
	
#ifndef _WIN32
	if ( !IsSetEF( EF_UseNetworkMulti ) )
#endif
		ADDTOSELECT(m_SocketMain.GetSocket());

	int	connecting	= 0;

	CClient * pClientNext;
	CClient * pClient = GetClientHead();
	for ( ; pClient!=NULL; pClient = pClientNext )
	{
		pClientNext = pClient->GetNext();
		if ( !pClient->m_Socket.IsOpen() || pClient->m_fClosed )
		{
			delete pClient;
			continue;
		}

		if ( pClient->IsConnecting() )
		{
			connecting++;
			if ( connecting > g_Cfg.m_iConnectingMax )
			{
				delete pClient;
				continue;
			}
		}

		ADDTOSELECT(pClient->m_Socket.GetSocket());
	}

	if ( connecting > g_Cfg.m_iConnectingMax )
	{
		g_Log.Event(LOGL_WARN|LOGM_CHEAT, "%d clients in connect mode (max %d), closing %d\n",
			connecting, g_Cfg.m_iConnectingMax, connecting - g_Cfg.m_iConnectingMax);
	}
	// we task sleep in here. NOTE: this is where we give time back to the OS.

	m_Profile.Start(PROFILE_IDLE);

	timeval Timeout;	// time to wait for data.
	Timeout.tv_sec = 0;
	Timeout.tv_usec = 100;	// micro seconds = 1/1000000
	int ret = select( nfds+1, &readfds, NULL, NULL, &Timeout );

	m_Profile.Start( PROFILE_NETWORK_RX );

	if ( ret <= 0 )
	{
		m_Profile.Start( PROFILE_OVERHEAD );
		return;
	}

#ifndef _WIN32
	if ( !IsSetEF( EF_UseNetworkMulti ) )
	{
#endif
	// Process new connections.
	if ( FD_ISSET( m_SocketMain.GetSocket(), &readfds))
	{
		SocketsReceive( m_SocketMain );
	}
#ifndef _WIN32
	}
#endif
		
	// Any events from clients ?
	for ( pClient = GetClientHead(); pClient!=NULL; pClient = pClientNext )
	{
		pClientNext = pClient->GetNext();
		if ( !pClient->m_Socket.IsOpen() || pClient->m_fClosed )
		{
			delete pClient;
			continue;
		}

		if ( FD_ISSET( pClient->m_Socket.GetSocket(), &readfds ))
		{
			pClient->m_timeLastEvent = CServTime::GetCurrentTime();	// We should always get pinged every couple minutes or so
			if ( !pClient->xRecvData() )
			{
				delete pClient;
				continue;
			}
			if ( pClient->m_fClosed )		// can happen due to data received
			{
				delete pClient;
				continue;
			}
		}
		else
		{
			// NOTE: Not all CClient are game clients.

			int iLastEventDiff = -g_World.GetTimeDiff( pClient->m_timeLastEvent );

			if ( g_Cfg.m_iDeadSocketTime &&
				iLastEventDiff > g_Cfg.m_iDeadSocketTime &&
				pClient->GetConnectType() != CONNECT_TELNET )
			{
				// We have not talked in several minutes.
				DEBUG_ERR(( "%x:Dead Socket Timeout\n", pClient->m_Socket.GetSocket()));
				delete pClient;
				continue;
			}
		}

		// On a timer allow the client to walk.
		// catch up with real time !
		// pClient->addWalkCode( EXTDATA_WalkCode_Add, 1 );
	}

	m_Profile.Start( PROFILE_OVERHEAD );
}

#undef ADDTOSELECT

void CServer::SocketsFlush() // Sends ALL buffered data
{
	ADDTOCALLSTACK("CServer::SocketsFlush");
	for ( CClient *pClient = GetClientHead(); pClient ; pClient = pClient->GetNext() )
	{
		pClient->xFlush(); // first we send any other packet
		pClient->xProcessTooltipQueue(); // then we process tooltip (lowest priority)
	}
}

bool CServer::SocketsInit( CGSocket & socket )
{
	ADDTOCALLSTACK("CServer::SocketsInit");
	// Initialize socket
	if ( !socket.Create() )
	{
		g_Log.Event(LOGL_FATAL|LOGM_INIT, "Unable to create socket!\n");
		return false;
	}

	linger lval;
	lval.l_onoff = 0;
	lval.l_linger = 10;
	socket.SetSockOpt(SO_LINGER, (const char*) &lval, sizeof(lval));
	socket.SetNonBlocking();

#ifndef _WIN32
	int onNotOff = 1;
	if(socket.SetSockOpt(SO_REUSEADDR, (char *)&onNotOff , sizeof(onNotOff )) == -1) {
		g_Log.Event(LOGL_FATAL|LOGM_INIT, "Unable to set SO_REUSEADDR!\n");
	}
#endif
	// Bind to just one specific port if they say so.
	CSocketAddress SockAddr = m_ip;
	// if ( fGod )
	//	SockAddr.SetPort(( g_Cfg.m_iUseGodPort > 1 ) ? g_Cfg.m_iUseGodPort : m_ip.GetPort()+1000);

	int iRet = socket.Bind(SockAddr);
	if ( iRet < 0 )			// Probably already a server running.
	{
		g_Log.Event(LOGL_FATAL|LOGM_INIT, "Unable to bind listen socket %s port %d - Error code: %i\n",
			SockAddr.GetAddrStr(), SockAddr.GetPort(), iRet);
		return false;
	}
	socket.Listen();
	
#ifndef _WIN32
	if ( IsSetEF( EF_UseNetworkMulti ) )
	{
		g_NetworkEvent.registerMainsocket();
	}
#endif
		
	return true;
}

bool CServer::SocketsInit() // Initialize sockets
{
	ADDTOCALLSTACK("CServer::SocketsInit");
	if ( !SocketsInit(m_SocketMain) )
		return false;

	// What are we listing our port as to the world.
	// Tell the admin what we know.

	TCHAR szName[ _MAX_PATH ];
	struct hostent * pHost = NULL;

	int iRet = gethostname(szName, sizeof(szName));
	if ( iRet )
		strcpy(szName, m_ip.GetAddrStr());
	else
	{
		pHost = gethostbyname(szName);
		if ( pHost && pHost->h_addr && pHost->h_name && pHost->h_name[0] )
			strcpy(szName, pHost->h_name);
	}

	g_Log.Event( LOGM_INIT, "Server started on '%s' port %d.\n", szName, m_ip.GetPort());
	if ( !iRet && pHost && pHost->h_addr )
	{
		for ( int i=0; pHost->h_addr_list[i] != NULL; i++ )
		{
			CSocketAddressIP ip;
			ip.SetAddrIP(*((DWORD*)(pHost->h_addr_list[i]))); // 0.1.2.3
			if ( !m_ip.IsLocalAddr() && !m_ip.IsSameIP(ip) )
				continue;
			g_Log.Event(LOGM_INIT, "Monitoring IP '%s'.\n", (LPCTSTR)ip.GetAddrStr());
		}
	}
	return true;
}

void CServer::SocketsClose()
{
	ADDTOCALLSTACK("CServer::SocketsClose");
#ifndef _WIN32
	if ( IsSetEF( EF_UseNetworkMulti ) )
	{
		g_NetworkEvent.unregisterMainsocket();
	}
#endif
	m_SocketMain.Close();
	m_Clients.DeleteAll();
}

void CServer::OnTick()
{
	ADDTOCALLSTACK("CServer::OnTick");
	EXC_TRY("Tick");

	TIME_PROFILE_INIT;
	if ( IsSetSpecific )
		TIME_PROFILE_START;

#ifndef _WIN32
	// Do a select operation on the stdin handle and check
	// if there is any input waiting.
	EXC_SET("unix");
	fd_set consoleFds;
	FD_ZERO(&consoleFds);
	FD_SET(STDIN_FILENO, &consoleFds);

	timeval tvTimeout;
	tvTimeout.tv_sec = 0;
	tvTimeout.tv_usec = 1;

	if( select(1, &consoleFds, 0, 0, &tvTimeout) )
	{
		int c = fgetc(stdin);
		if ( OnConsoleKey(m_sConsoleText, c, false) == 2 )
			m_fConsoleTextReadyFlag = true;
	}
#endif

	EXC_SET("ConsoleInput");
	if ( m_fConsoleTextReadyFlag )
	{
		EXC_SET("console input");
		CGString sText = m_sConsoleText;	// make a copy.
		m_sConsoleText.Empty();	// done using this.
		m_fConsoleTextReadyFlag = false; // rady to use again
		OnConsoleCmd( sText, this );
	}

	EXC_SET("ResyncCommand");
	if ( m_fResyncRequested != NULL )
	{
		if ( !m_fResyncPause )
		{
			SetResyncPause(true, m_fResyncRequested);
			SetResyncPause(false, m_fResyncRequested);
		}
		else SetResyncPause(false, m_fResyncRequested, true);
		m_fResyncRequested = NULL;
	}

	EXC_SET("SetTime");
	SetValidTime();	// we are a valid game server.

	// Check clients for incoming packets.
	// Do this on a timer so clients with faster connections can't overwealm the system.
	EXC_SET("receive");
	SocketsReceive();

	if ( !IsLoading() )
	{
		m_Profile.Start( PROFILE_CLIENTS );

		for ( CClient * pClient = GetClientHead(); pClient!=NULL; pClient = pClient->GetNext())
		{
			if ( !pClient->m_bin.GetDataQty() )
				continue;

			EXC_TRYSUB("ProcessMessage");

			pClient->xProcessMsg(pClient->xDispatchMsg());
			continue;

			EXC_CATCHSUB("Server");
			pClient->xProcessMsg();
		}
	}

	EXC_SET("send");
	m_Profile.Start( PROFILE_NETWORK_TX );
	SocketsFlush();
	m_Profile.Start( PROFILE_OVERHEAD );

	if ( m_timeShutdown.IsTimeValid() )
	{
		EXC_SET("shutdown");
		if ( g_World.GetTimeDiff(m_timeShutdown) <= 0 )
			SetExitFlag(2);
	}

	EXC_SET("generic");
	g_Cfg.OnTick(false);
	m_hdb.OnTick();
	if ( IsSetSpecific )
	{
		EXC_SET("time profile");
		TIME_PROFILE_END;
		LONGLONG	hi = TIME_PROFILE_GET_HI;
		if ( hi > 5L )
		{
			DEBUG_ERR(("CServer::OnTick() [socket operations] took %d.%d to run\n", hi, TIME_PROFILE_GET_LO));
		}
	}
	EXC_CATCH;

	EXC_DEBUG_START;
	EXC_DEBUG_END;
}

bool CServer::Load()
{
	EXC_TRY("Load");

#ifdef _WIN32
	EXC_SET("init winsock");
	TCHAR * wSockInfo = Str_GetTemp();
	if ( !m_SocketMain.IsOpen() )
	{
		WSADATA wsaData;
		int err = WSAStartup(MAKEWORD(2,2), &wsaData);
		if ( err )
		{
			if ( err == WSAVERNOTSUPPORTED )
			{
				err = WSAStartup(MAKEWORD(1,1), &wsaData);
				if ( err ) goto nowinsock;
			}
			else
			{
nowinsock:		g_Log.Event(LOGL_FATAL|LOGM_INIT, "Winsock 1.1 not found!\n");
				return( false );
			}
			sprintf(wSockInfo, "Using WinSock ver %d.%d (%s)\n", HIBYTE(wsaData.wVersion), LOBYTE(wsaData.wVersion), wsaData.szDescription);
		}
	}
#endif

	EXC_SET("loading ini");
	g_Cfg.LoadIni(false);

	EXC_SET("log write");
	g_Log.WriteString("\n");

#ifdef __SVNREVISION__
	g_Log.Event(LOGM_INIT, "%s, compiled at " __DATE__ " (" __TIME__ "), internal build #" __SVNREVISION__  "\n", g_szServerDescription);
#else
	g_Log.Event(LOGM_INIT, "%s, compiled at " __DATE__ " (" __TIME__ ")\n", g_szServerDescription);
#endif

#ifdef _WIN32
	if ( wSockInfo[0] )
		g_Log.Event(LOGM_INIT, wSockInfo);
#endif


#ifdef _NIGHTLYBUILD
	LPCTSTR pszWarning = " --- WARNING ---\r\n\r\n"
		"This is a nightly build of SphereServer. This build is to be used\r\n"
		"for testing and/or bug reporting ONLY. DO NOT run this build on a\r\n"
		"live shard unless you know what you are doing!\r\n"
		"Nightly builds are automatically made every night from source and\r\n"
		"might contain errors, might be unstable or even destroy your\r\n"
		"shard as they are mostly untested!\r\n"
		" ---------------------------------\r\n\r\n";

	g_Log.EventWarn(pszWarning);

	if (!g_Cfg.m_bAgree)
	{
		g_Log.EventError("Please set AGREE=1 in the INI file to acknowledge that\nyou understand the terms for nightly builds\n");
		return false;
	}
#endif

	EXC_SET("setting signals");
	SetSignals();

	EXC_SET("loading scripts");
	if ( !g_Cfg.Load(false) )
		return( false );

	EXC_SET("init encryption");
	if ( m_ClientVersion.GetClientVer() )
	{
		TCHAR szVersion[128];
		g_Log.Event( LOGM_INIT, "ClientVersion=%s\n", (LPCTSTR) m_ClientVersion.WriteClientVer(szVersion));
		if ( !m_ClientVersion.IsValid() )
		{
			g_Log.Event(LOGL_FATAL|LOGM_INIT, "Bad Client Version '%s'\n", szVersion);
			return false;
		}
	}

	EXC_SET("finilizing");
#ifdef _WIN32
	TCHAR *pszTemp = Str_GetTemp();
	sprintf(pszTemp, GRAY_TITLE " V" GRAY_VERSION " - %s", (LPCTSTR) GetName());
	SetConsoleTitle(pszTemp);
#endif

	return true;
	EXC_CATCH;

	EXC_DEBUG_START;
	EXC_DEBUG_END;
	return false;
}
