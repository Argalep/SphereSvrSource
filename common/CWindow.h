//
// CWindow = a base window class for controls.
// Copyright Menace Software (www.menasoft.com).
//

#ifndef _INC_CWINDOW_H
#define _INC_CWINDOW_H
#pragma once

#include "../common/graycom.h"
#include "cstring.h"
#include <RICHEDIT.H>	// CRichEditCtrl

class CWindow    // similar to Std MFC class CWnd
{
public:
	static const char *m_sClassName;
	HWND m_hWnd;
public:
	operator HWND () const       // cast as a HWND
	{
		return( m_hWnd );
	}
	CWindow()
	{
		m_hWnd = NULL;
	}
	~CWindow()
	{
		DestroyWindow();
	}

	// Standard message handlers.
	BOOL OnCreate( HWND hwnd, LPCREATESTRUCT lpCreateStruct = NULL  )
	{
		m_hWnd = hwnd;
		return( TRUE );
	}
	void OnDestroy()
	{
		m_hWnd = NULL;
	}
	void OnDestroy( HWND hwnd )
	{
		m_hWnd = NULL;
	}

	// Basic window functions.
	BOOL IsWindow() const
	{
		if ( this == NULL )
			return( false );
		if ( m_hWnd == NULL )
			return( false );
		return( ::IsWindow( m_hWnd ));
	}
	HWND GetParent() const
	{
		ASSERT( m_hWnd );
		return( ::GetParent(m_hWnd));
	}
	LRESULT SendMessage( UINT uMsg, WPARAM wParam = 0, LPARAM lParam = 0 ) const
	{
		ASSERT( m_hWnd );
		return( ::SendMessage( m_hWnd, uMsg, wParam, lParam ));
	}
	BOOL PostMessage( UINT uMsg, WPARAM wParam = 0, LPARAM lParam = 0 ) const
	{
		ASSERT( m_hWnd );
		return( ::PostMessage( m_hWnd, uMsg, wParam, lParam ));
	}
	HWND GetDlgItem( int id ) const
	{
		ASSERT(m_hWnd);
		return( ::GetDlgItem( m_hWnd, id ));
	}
	BOOL SetDlgItemText( int nIDDlgItem, LPCSTR lpString )
	{
		ASSERT(m_hWnd);
		return( ::SetDlgItemText( m_hWnd, nIDDlgItem, lpString ));
	}

	// Create/Destroy
	void DestroyWindow()
	{
		if ( m_hWnd == NULL )
			return;
		::DestroyWindow( m_hWnd );
		ASSERT( m_hWnd == NULL );
	}

	// Area and location
	BOOL MoveWindow( int X, int Y, int nWidth, int nHeight, BOOL bRepaint = TRUE )
	{
		return( ::MoveWindow( m_hWnd, X, Y, nWidth, nHeight, bRepaint ));
	}
	BOOL SetForegroundWindow()
	{
		ASSERT( m_hWnd );
		return( ::SetForegroundWindow( m_hWnd ));
	}
	HWND SetFocus()
	{
		ASSERT( m_hWnd );
		return( ::SetFocus( m_hWnd ));
	}
	BOOL ShowWindow( int nCmdShow )
	{
		// SW_SHOW
		return( ::ShowWindow( m_hWnd, nCmdShow ));
	}

	// Standard windows props.
	int GetWindowText( LPSTR lpszText, int iLen )
	{
		ASSERT( m_hWnd );
		return ::GetWindowText( m_hWnd, lpszText, iLen );
	}
	BOOL SetWindowText( LPCSTR lpszText )
	{
		ASSERT( m_hWnd );
		return ::SetWindowText( m_hWnd, lpszText );
	}

	void SetFont( HFONT hFont, BOOL fRedraw = false )
	{
		SendMessage( WM_SETFONT, (WPARAM) hFont, MAKELPARAM(fRedraw, 0));
	}
   

	HICON SetIcon( HICON hIcon, BOOL fType = false )
	{
		// ICON_BIG vs ICON_SMALL
		return( (HICON)(DWORD) SendMessage( WM_SETICON, (WPARAM)fType, (LPARAM) hIcon ));
	}

	UINT SetTimer( UINT uTimerID, UINT uWaitmSec )
	{
		ASSERT(m_hWnd);
		return( ::SetTimer( m_hWnd, uTimerID, uWaitmSec, NULL ));
	}
	BOOL KillTimer( UINT uTimerID )
	{
		ASSERT(m_hWnd);
		return( ::KillTimer( m_hWnd, uTimerID ));
	}
	int MessageBox( LPCSTR lpszText, LPCSTR lpszTitle, UINT fuStyle = MB_OK	) const
	{
		// ASSERT( m_hWnd ); ok for this to be NULL !
		return( ::MessageBox( m_hWnd, lpszText, lpszTitle, fuStyle ));
	}
	LONG SetWindowLong( int nIndex, LONG dwNewLong )
	{
		ASSERT(m_hWnd);
		return( ::SetWindowLong( m_hWnd, nIndex, dwNewLong ));
	}
	LONG GetWindowLong( int nIndex ) const
	{
		ASSERT(m_hWnd);
		return( ::GetWindowLong( m_hWnd, nIndex ));
	}

	int SetDlgItemText( int ID, LPCSTR lpszText ) const
	{
		return ::SetDlgItemText(m_hWnd, ID, lpszText);
	}
};

class CDialogBase : public CWindow
{
public:
	static const char *m_sClassName;
	static BOOL CALLBACK DialogProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam );
public:
	virtual BOOL DefDialogProc( UINT message, WPARAM wParam, LPARAM lParam )
	{
		return FALSE;
	}
};

class CWindowBase : public CWindow
{
public:
	static const char *m_sClassName;
	static LRESULT WINAPI WndProc( HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam );
public:
	static ATOM RegisterClass( WNDCLASS & wc );
	virtual LRESULT DefWindowProc( UINT message, WPARAM wParam, LPARAM lParam )
	{
		return ::DefWindowProc( m_hWnd, message, wParam, lParam );
	}
};

class CWinApp	// Similar to MFC type
{
public:
	static const char *m_sClassName;
	LPCSTR	 	m_pszAppName;	// Specifies the name of the application. (display freindly)
	HINSTANCE 	m_hInstance;	// Identifies the current instance of the application.
	LPSTR 		m_lpCmdLine;	// Points to a null-terminated string that specifies the command line for the application.
	CWindow *	m_pMainWnd;		// Holds a pointer to the application's main window. For an example of how to initialize m_pMainWnd, see InitInstance.
	CGString	m_pszExeName;	// The module name of the application.
	CGString	m_pszProfileName;	// the path to the profile.

public:
	CWinApp()
	{
		m_hInstance = NULL;
		m_pMainWnd = NULL;
	}

	void InitInstance( LPCTSTR pszAppName, HINSTANCE hInstance, LPSTR lpszCmdLine )
	{
		m_pszAppName = pszAppName;	// assume this is a static data pointer valid forever.
		m_hInstance	= hInstance;
		m_lpCmdLine	= lpszCmdLine;

		char szFileName[ _MAX_PATH ];
		if ( ! GetModuleFileName( m_hInstance, szFileName, sizeof( szFileName )))
			return;
		m_pszExeName = szFileName;

        LPSTR pszTmp = const_cast<LPSTR>(strrchr( m_pszExeName, '\\' ));	// Get title
        lstrcpy( szFileName, ( pszTmp == NULL ) ? m_pszExeName : ( pszTmp + 1 ));
		pszTmp = strrchr( szFileName, '.' );	// Get extension.
		if ( pszTmp != NULL )
			pszTmp[0] = '\0';
		lstrcat( szFileName, ".INI" );

		OFSTRUCT ofs;
		if ( OpenFile( szFileName, &ofs, OF_EXIST ) != HFILE_ERROR)
		{
			m_pszProfileName = ofs.szPathName;
		}
		else
		{
			m_pszProfileName = szFileName;
		}
	}

	HICON LoadIcon( int id ) const
	{
		return( ::LoadIcon( m_hInstance, MAKEINTRESOURCE( id )));
	}
	HMENU LoadMenu( int id ) const
	{
		return( ::LoadMenu( m_hInstance, MAKEINTRESOURCE( id )));
	}
};

class CScrollBar : public CWindow
{
// Constructors
public:
	static const char *m_sClassName;
	CScrollBar::CScrollBar() 
	{
	}

// Attributes
	void CScrollBar::GetScrollRange(LPINT lpMinPos, LPINT lpMaxPos) const
	{
		ASSERT(IsWindow());
		::GetScrollRange(m_hWnd, SB_CTL, lpMinPos, lpMaxPos);
	}
	BOOL CScrollBar::GetScrollInfo(LPSCROLLINFO lpScrollInfo, UINT nMask)
	{
		lpScrollInfo->cbSize = sizeof(*lpScrollInfo);
		lpScrollInfo->fMask = nMask;
		return ::GetScrollInfo(m_hWnd, SB_CTL, lpScrollInfo);
	}

// Implementation
public:
	virtual ~CScrollBar()
	{
	}
};

class CEdit : public CWindow
{
// Constructors
public:
	static const char *m_sClassName;
	CEdit() {}

// Operations

	void SetSel( DWORD dwSelection, BOOL bNoScroll = FALSE )
	{
		ASSERT(IsWindow());
		SendMessage( EM_SETSEL, (WPARAM) dwSelection, (LPARAM) dwSelection );
	}
	void SetSel( int nStartChar, int nEndChar, BOOL bNoScroll = FALSE )
	{
		ASSERT(IsWindow());
		SendMessage( EM_SETSEL, (WPARAM) nStartChar, (LPARAM) nEndChar );
	}
	DWORD GetSel() const
	{
		ASSERT(IsWindow());
		return((DWORD) SendMessage( EM_GETSEL ));
	}
	void GetSel(int& nStartChar, int& nEndChar) const
	{
		ASSERT(IsWindow());
		DWORD dwSel = GetSel();
		nStartChar = LOWORD(dwSel);
		nEndChar = HIWORD(dwSel);
	}

	void ReplaceSel( LPCTSTR lpszNewText, BOOL bCanUndo = FALSE )
	{
		ASSERT(IsWindow());
		SendMessage( EM_REPLACESEL, (WPARAM) bCanUndo, (LPARAM) lpszNewText );
	}

// Implementation
public:
	virtual ~CEdit()
	{
	}
};



class CRichEditCtrl : public CEdit
{
public:
	static const char *m_sClassName;
	COLORREF SetBackgroundColor( BOOL bSysColor, COLORREF cr )
	{ 
		return( (COLORREF)(DWORD) SendMessage( EM_SETBKGNDCOLOR, (WPARAM) bSysColor, (LPARAM) cr ));
	}

	void SetSel( int nStartChar, int nEndChar, BOOL bNoScroll = FALSE )
	{
		ASSERT(IsWindow());
		CHARRANGE range;
		range.cpMin = nStartChar;
		range.cpMax = nEndChar;
		SendMessage( EM_EXSETSEL, 0, (LPARAM) &range );
	}
	void GetSel(int& nStartChar, int& nEndChar) const
	{
		ASSERT(IsWindow());
		CHARRANGE range;
		SendMessage( EM_EXGETSEL, 0, (LPARAM) &range );
		nStartChar = range.cpMin;
		nEndChar = range.cpMax;
	}

	DWORD Scroll( int iAction = SB_PAGEDOWN )
	{
		return( (DWORD) SendMessage( EM_SCROLL, (WPARAM) iAction ));
	}

	// Formatting.
	BOOL SetDefaultCharFormat( CHARFORMAT& cf )
	{
		return( (BOOL)(DWORD) SendMessage( EM_SETCHARFORMAT, (WPARAM) SCF_DEFAULT, (LPARAM) &cf ));
	}
	BOOL SetSelectionCharFormat( CHARFORMAT& cf )
	{
		return( (BOOL)(DWORD) SendMessage( EM_SETCHARFORMAT, (WPARAM) SCF_SELECTION, (LPARAM) &cf ));
	}

	// Events.
	long GetEventMask() const
	{
		return( (DWORD) SendMessage( EM_GETEVENTMASK ));
	}
	DWORD SetEventMask( DWORD dwEventMask = ENM_NONE )
	{
		// ENM_NONE = default.
		return( (DWORD) SendMessage( EM_SETEVENTMASK, 0, (LPARAM) dwEventMask ));
	}
};

class CListbox : public CWindow
{
// Constructors
public:
	static const char *m_sClassName;
	CListbox() {}

// Operations

	void ResetContent()
	{
		ASSERT(IsWindow());
		SendMessage( LB_RESETCONTENT );
	}
	int GetCount() const
	{
		return( (int)(DWORD) SendMessage( LB_GETCOUNT ));
	}
	int AddString( LPCTSTR lpsz ) const
	{
		return( (int)(DWORD) SendMessage( LB_ADDSTRING, 0L, (LPARAM)(lpsz)));
	}

// Implementation
public:
	virtual ~CListbox()
	{
	}
};

#endif	// _INC_CWINDOW_H