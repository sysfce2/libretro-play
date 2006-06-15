#include <stdio.h>
#include "MainWindow.h"
#include "PtrMacro.h"
#include "PS2VM.h"
#include "GSH_OpenGL.h"
#include "PH_DirectInput.h"
#include "FileDialog.h"
#include "VFSManagerWnd.h"
#include "Debugger.h"
#include "SysInfoWnd.h"
#include "AboutWnd.h"
#include "resource.h"
#include "Config.h"

#define CLSNAME						_X("MainWindow")
#define WNDSTYLE					(WS_CLIPCHILDREN | WS_DLGFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX)

#define STATUSPANEL					1
#define FPSPANEL					2

#define TITLEA						"Purei!"
#define TITLEW						L"プレイ!"

#define VMMENUPOS					1

#define ID_MAIN_VM_STATESLOT_0		(0xBEEF)
#define MAX_STATESLOTS				10

#define ID_MAIN_DEBUG_SHOW			(0xDEAD)

#define PREF_UI_PAUSEWHENFOCUSLOST	"ui.pausewhenfocuslost"

using namespace Framework;

CMainWindow::CMainWindow(char* sCmdLine)
{
	RECT rc;
	xchar sVersion[256];

	CConfig::GetInstance()->RegisterPreferenceBoolean(PREF_UI_PAUSEWHENFOCUSLOST, true);

	if(!DoesWindowClassExist(CLSNAME))
	{
		WNDCLASSEX wc;
		memset(&wc, 0, sizeof(WNDCLASSEX));
		wc.cbSize			= sizeof(WNDCLASSEX);
		wc.hCursor			= LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground	= (HBRUSH)(COLOR_WINDOW); 
		wc.hInstance		= GetModuleHandle(NULL);
		wc.lpszClassName	= CLSNAME;
		wc.lpfnWndProc		= CWindow::WndProc;
		wc.style			= CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
		RegisterClassEx(&wc);
	}

	SetRect(&rc, 0, 0, 640, 480);
	
	Create(NULL, CLSNAME, _X(""), WNDSTYLE, &rc, NULL, NULL);
	SetClassPtr();

	CPS2VM::Initialize();

	SetIcon(ICON_SMALL, LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_PUREI)));
	SetIcon(ICON_BIG, LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_PUREI)));

	SetMenu(LoadMenu(GetModuleHandle(NULL), MAKEINTRESOURCE(IDR_MAINWINDOW)));

#ifdef DEBUGGER_INCLUDED
	m_pDebugger = new CDebugger;
	CreateDebugMenu();
#endif

	PrintVersion(sVersion, countof(sVersion));

	SetRect(&rc, 0, 0, 1, 1);
	m_pOutputWnd = new COutputWnd(m_hWnd, &rc);

	SetRect(&rc, 0, 0, 240, 20);
	m_pStatusBar = new CNiceStatus(m_hWnd, &rc);

	m_pStatusBar->InsertPanel(STATUSPANEL,	0.7, sVersion);
	m_pStatusBar->InsertPanel(FPSPANEL,		0.3, _X("0 fps"));

	m_pOnOutputWndSizeChangeHandler = new CEventHandlerMethod<CMainWindow, int>(this, &CMainWindow::OnOutputWndSizeChange);
	m_pOutputWnd->m_OnSizeChange.InsertHandler(m_pOnOutputWndSizeChangeHandler);

	CGSH_OpenGL::CreateGSHandler(m_pOutputWnd);

	CPH_DirectInput::CreatePadHandler(m_hWnd);

	//Initialize FPS counter
	m_nFrames = 0;

	m_nStateSlot = 0;

	m_nDeactivatePause = false;
	m_nPauseFocusLost = CConfig::GetInstance()->GetPreferenceBoolean(PREF_UI_PAUSEWHENFOCUSLOST);

	m_pOnNewFrameHandler = new CEventHandlerMethod<CMainWindow, int>(this, &CMainWindow::OnNewFrame);
	CPS2VM::m_OnNewFrame.InsertHandler(m_pOnNewFrameHandler);

	SetTimer(m_hWnd, NULL, 1000, NULL);

	m_pOnExecutableChange = new CEventHandlerMethod<CMainWindow, int>(this, &CMainWindow::OnExecutableChange);
	CPS2OS::m_OnExecutableChange.InsertHandler(m_pOnExecutableChange);

	CreateStateSlotMenu();
	CreateAccelerators();

	if(strstr(sCmdLine, "-cdrom0") != NULL)
	{
		BootCDROM();
	}
	else if(strlen(sCmdLine))
	{
		LoadELF(sCmdLine);
	}

	UpdateUI();
	Show(SW_SHOW);

#if (_DEBUG && DEBUGGER_INCLUDED)
	ShowDebugger();
#endif
}

CMainWindow::~CMainWindow()
{
	CPS2VM::Pause();

	CPS2VM::DestroyPadHandler();
	CPS2VM::DestroyGSHandler();

#ifdef DEBUGGER_INCLUDED
	DELETEPTR(m_pDebugger);
#endif

	m_pOutputWnd->m_OnSizeChange.RemoveHandler(m_pOnOutputWndSizeChangeHandler);
	CPS2VM::m_OnNewFrame.RemoveHandler(m_pOnNewFrameHandler);

	DELETEPTR(m_pOutputWnd);
	DELETEPTR(m_pStatusBar);

	DestroyAcceleratorTable(m_nAccTable);

	CPS2VM::Destroy();
}

int CMainWindow::Loop()
{
	MSG msg;
	HWND hActive;
	bool nDispatched;

	while(IsWindow())
	{
		GetMessage(&msg, NULL, 0, 0);
		nDispatched = false;
		hActive = GetActiveWindow();

		if(hActive == m_hWnd)
		{
			nDispatched = TranslateAccelerator(m_hWnd, m_nAccTable, &msg) != 0;
		}
#ifdef DEBUGGER_INCLUDED
		else if(hActive == m_pDebugger->m_hWnd)
		{
			nDispatched = TranslateAccelerator(m_pDebugger->m_hWnd, m_pDebugger->GetAccelerators(), &msg) != 0;
		}
#endif
		if(!nDispatched)
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return 0;
}

long CMainWindow::OnCommand(unsigned short nID, unsigned short nCmd, HWND hSender)
{
	switch(nID)
	{
	case ID_MAIN_FILE_LOADELF:
		OpenELF();
		break;
	case ID_MAIN_FILE_BOOTCDROM:
		BootCDROM();
		break;
	case ID_MAIN_FILE_EXIT:
		DestroyWindow(m_hWnd);
		break;
	case ID_MAIN_VM_RESUME:
		ResumePause();
		break;
	case ID_MAIN_VM_RESET:
		Reset();
		break;
	case ID_MAIN_VM_PAUSEFOCUS:
		PauseWhenFocusLost();
		break;
	case ID_MAIN_VM_SAVESTATE:
		SaveState();
		break;
	case ID_MAIN_VM_LOADSTATE:
		LoadState();
		break;
	case ID_MAIN_VM_STATESLOT_0 + 0:
	case ID_MAIN_VM_STATESLOT_0 + 1:
	case ID_MAIN_VM_STATESLOT_0 + 2:
	case ID_MAIN_VM_STATESLOT_0 + 3:
	case ID_MAIN_VM_STATESLOT_0 + 4:
	case ID_MAIN_VM_STATESLOT_0 + 5:
	case ID_MAIN_VM_STATESLOT_0 + 6:
	case ID_MAIN_VM_STATESLOT_0 + 7:
	case ID_MAIN_VM_STATESLOT_0 + 8:
	case ID_MAIN_VM_STATESLOT_0 + 9:
		ChangeStateSlot(nID - ID_MAIN_VM_STATESLOT_0);
		break;
	case ID_MAIN_OPTIONS_RENDERER:
		ShowRendererSettings();
		break;
	case ID_MAIN_OPTIONS_VFSMANAGER:
		ShowVFSManager();
		break;
	case ID_MAIN_DEBUG_SHOW:
		ShowDebugger();
		break;
	case ID_MAIN_HELP_SYSINFO:
		ShowSysInfo();
		break;
	case ID_MAIN_HELP_ABOUT:
		ShowAbout();
		break;

	}
	return TRUE;
}

long CMainWindow::OnTimer()
{
	xchar sTemp[256];

	xsnprintf(sTemp, countof(sTemp), _X("%i fps"), m_nFrames);

	m_pStatusBar->SetCaption(FPSPANEL, sTemp);

	m_nFrames = 0;

	return TRUE;
}

long CMainWindow::OnActivateApp(bool nActive, unsigned long nThreadId)
{
	if(m_nPauseFocusLost == true)
	{
		if(nActive == false)
		{
			if(CPS2VM::m_nStatus == PS2VM_STATUS_RUNNING)
			{
				ResumePause();
				m_nDeactivatePause = true;
			}
		}
		
		if((nActive == true) && (m_nDeactivatePause == true))
		{
			ResumePause();
			m_nDeactivatePause = false;
		}
	}
	return FALSE;
}

void CMainWindow::OpenELF()
{
	CFileDialog d;
	int nRet;
	char sConvert[256];

	d.m_OFN.lpstrFilter = _X("ELF Executable Files (*.elf)\0*.elf\0All files (*.*)\0*.*\0");

	Enable(FALSE);
	nRet = d.Summon(m_hWnd);
	Enable(TRUE);
	SetFocus();

	if(nRet == 0) return;

	xconvert(sConvert, d.m_sFile, 256);
	LoadELF(sConvert);
}

void CMainWindow::ResumePause()
{
	if(CPS2VM::m_nStatus == PS2VM_STATUS_RUNNING)
	{
		CPS2VM::Pause();
		SetStatusText(_X("Virtual Machine paused."));
	}
	else
	{
		CPS2VM::Resume();
		SetStatusText(_X("Virtual Machine resumed."));
	}
}

void CMainWindow::Reset()
{
	//Calling this will clear the RAM completely and remove the executable from memory...
	
	//CPS2VM::Reset();
	//SetStatusText(_X("Virtual Machine reset."));
}

void CMainWindow::PauseWhenFocusLost()
{
	m_nPauseFocusLost = !m_nPauseFocusLost;
	if(m_nPauseFocusLost)
	{
		m_nDeactivatePause = false;
	}

	CConfig::GetInstance()->SetPreferenceBoolean(PREF_UI_PAUSEWHENFOCUSLOST, m_nPauseFocusLost);
	UpdateUI();
}

void CMainWindow::SaveState()
{
	char sPath[MAX_PATH];

	if(CPS2OS::GetELF() == NULL) return;

	GenerateStatePath(sPath);
	if(CPS2VM::SaveState(sPath) == 0)
	{
		PrintStatusTextA("Saved state to slot %i.", m_nStateSlot);
	}
	else
	{
		PrintStatusTextA("Error saving state to slot %i.", m_nStateSlot);
	}
}

void CMainWindow::LoadState()
{
	char sPath[MAX_PATH];

	if(CPS2OS::GetELF() == NULL) return;

	GenerateStatePath(sPath);
	if(CPS2VM::LoadState(sPath) == 0)
	{
		PrintStatusTextA("Loaded state from slot %i.", m_nStateSlot);
	}
	else
	{
		PrintStatusTextA("Error loading state from slot %i.", m_nStateSlot);
	}
}

void CMainWindow::ChangeStateSlot(unsigned int nSlot)
{
	m_nStateSlot = nSlot % MAX_STATESLOTS;
	UpdateUI();
}

void CMainWindow::ShowDebugger()
{
#ifdef DEBUGGER_INCLUDED
	m_pDebugger->Show(SW_MAXIMIZE);
	SetForegroundWindow(m_pDebugger->m_hWnd);
#endif
}

void CMainWindow::ShowSysInfo()
{
	CSysInfoWnd SysInfoWnd(m_hWnd);

	SysInfoWnd.Center();
	SysInfoWnd.Show(SW_SHOW);
	CWindow::StdMsgLoop(&SysInfoWnd);

	Redraw();
}

void CMainWindow::ShowAbout()
{
	CAboutWnd AboutWnd(m_hWnd);

	AboutWnd.Center();
	AboutWnd.Show(SW_SHOW);
	CWindow::StdMsgLoop(&AboutWnd);

	Redraw();
}

void CMainWindow::ShowRendererSettings()
{
	bool nPaused;
	CModalWindow* pWindow;
	CSettingsDialogProvider* pProvider;

	nPaused = false;

	if(CPS2VM::m_nStatus == PS2VM_STATUS_RUNNING)
	{
		nPaused = true;
		ResumePause();
	}

	pProvider = CPS2VM::GetGSHandler()->GetSettingsDialogProvider();
	pWindow = pProvider->CreateSettingsDialog(m_hWnd);
	pWindow->DoModal();
	DELETEPTR(pWindow);
	pProvider->OnSettingsDialogDestroyed();

	Redraw();

	if(nPaused)
	{
		ResumePause();
	}
}

void CMainWindow::ShowVFSManager()
{
	bool nPaused;

	nPaused = false;

	if(CPS2VM::m_nStatus == PS2VM_STATUS_RUNNING)
	{
		nPaused = true;
		ResumePause();
	}

	CVFSManagerWnd VFSManagerWnd(m_hWnd);
	VFSManagerWnd.DoModal();

	Redraw();

	if(nPaused)
	{
		ResumePause();
	}
}

void CMainWindow::LoadELF(const char* sFilename)
{
	if(!CPS2OS::IsInitialized()) return;

	CPS2VM::Reset();

	try
	{
		CPS2OS::BootFromFile(sFilename);
#ifndef _DEBUG
		CPS2VM::Resume();
#endif
		PrintStatusTextA("Loaded executable '%s'.", CPS2OS::GetExecutableName());
	}
	catch(const char* sError)
	{
		xchar sMessage[256];
		xconvert(sMessage, sError, 256);
		MessageBox(m_hWnd, sMessage, NULL, 16);
	}
}

void CMainWindow::BootCDROM()
{
	if(!CPS2OS::IsInitialized()) return;

	CPS2VM::Reset();

	try
	{
		CPS2OS::BootFromCDROM();
#ifndef _DEBUG
		CPS2VM::Resume();
#endif
		PrintStatusTextA("Loaded executable '%s' cdrom0.", CPS2OS::GetExecutableName());
	}
	catch(const char* sError)
	{
		xchar sMessage[256];
		xconvert(sMessage, sError, 256);
		MessageBox(m_hWnd, sMessage, NULL, 16);
	}
}

void CMainWindow::RefreshLayout()
{
	RECT rc;
	unsigned int nViewW, nViewH;

	GetWindowRect(m_pOutputWnd->m_hWnd, &rc);

	nViewW = rc.right - rc.left;
	nViewH = rc.bottom - rc.top;

	SetRect(&rc, 0, 0, nViewW, nViewH);
	rc.bottom += 23;

	AdjustWindowRect(&rc, WNDSTYLE, TRUE);

	SetSize(rc.right - rc.left, rc.bottom - rc.top);

	m_pStatusBar->SetPosition(0, nViewH + 2);
	m_pStatusBar->SetSize(nViewW, 21);

	Center();
}

void CMainWindow::PrintStatusTextA(const char* sFormat, ...)
{
	xchar sConvert[256];
	char sText[256];
	va_list Args;

	va_start(Args, sFormat);
	_vsnprintf(sText, 256, sFormat, Args);
	va_end(Args);

	xconvert(sConvert, sText, 256);

	m_pStatusBar->SetCaption(STATUSPANEL, sConvert);
}

void CMainWindow::SetStatusText(xchar* sText)
{
	m_pStatusBar->SetCaption(STATUSPANEL, sText);
}

void CMainWindow::CreateAccelerators()
{
	ACCEL Accel[4];

	Accel[0].cmd	= ID_MAIN_VM_RESUME;
	Accel[0].key	= VK_F5;
	Accel[0].fVirt	= FVIRTKEY;

	Accel[1].cmd	= ID_MAIN_FILE_LOADELF;
	Accel[1].key	= 'O';
	Accel[1].fVirt	= FVIRTKEY | FCONTROL;

	Accel[2].cmd	= ID_MAIN_VM_SAVESTATE;
	Accel[2].key	= VK_F7;
	Accel[2].fVirt	= FVIRTKEY;

	Accel[3].cmd	= ID_MAIN_VM_LOADSTATE;
	Accel[3].key	= VK_F8;
	Accel[3].fVirt	= FVIRTKEY;

	m_nAccTable = CreateAcceleratorTable(Accel, sizeof(Accel) / sizeof(ACCEL));
}

void CMainWindow::CreateDebugMenu()
{
	HMENU hMenu;
	MENUITEMINFO ItemInfo;

	hMenu = CreatePopupMenu();
	InsertMenu(hMenu, 0, MF_STRING, ID_MAIN_DEBUG_SHOW, _X("Show Debugger"));

	memset(&ItemInfo, 0, sizeof(MENUITEMINFO));
	ItemInfo.cbSize		= sizeof(MENUITEMINFO);
	ItemInfo.fMask		= MIIM_STRING | MIIM_SUBMENU;
	ItemInfo.dwTypeData	= _X("Debug");
	ItemInfo.hSubMenu	= hMenu;

	InsertMenuItem(GetMenu(m_hWnd), 3, TRUE, &ItemInfo);
}

void CMainWindow::CreateStateSlotMenu()
{
	HMENU hMenu;
	MENUITEMINFO ItemInfo;
	xchar sCaption[256];
	unsigned int i;

	hMenu = CreatePopupMenu();
	for(i = 0; i < MAX_STATESLOTS; i++)
	{
		xsnprintf(sCaption, countof(sCaption), _X("Slot %i"), i);
		InsertMenu(hMenu, i, MF_STRING, ID_MAIN_VM_STATESLOT_0 + i, sCaption);
	}

	memset(&ItemInfo, 0, sizeof(MENUITEMINFO));
	ItemInfo.cbSize		= sizeof(MENUITEMINFO);
	ItemInfo.fMask		= MIIM_SUBMENU;
	ItemInfo.hSubMenu	= hMenu;

	hMenu = GetSubMenu(GetMenu(m_hWnd), VMMENUPOS);
	SetMenuItemInfo(hMenu, ID_MAIN_VM_STATESLOT, FALSE, &ItemInfo);
}

void CMainWindow::GenerateStatePath(char* sPath)
{
	sprintf(sPath, "%s/%s.st%i", "./states", CPS2OS::GetExecutableName(), m_nStateSlot);
}

void CMainWindow::UpdateUI()
{
	HMENU hMenu;
	const char* sExec;
	xchar sConvert[256];
	xchar sTitle[256];
	MENUITEMINFO MenuItem;
	unsigned int i;

	//Fix the virtual machine sub menu
	hMenu = GetSubMenu(GetMenu(m_hWnd), VMMENUPOS);

	EnableMenuItem(hMenu, ID_MAIN_VM_RESUME, (CPS2OS::GetELF() == NULL ? MF_GRAYED : 0) | MF_BYCOMMAND);
//	EnableMenuItem(hMenu, ID_MAIN_VM_RESET, (CPS2OS::GetELF() == NULL ? MF_GRAYED : 0) | MF_BYCOMMAND);
	CheckMenuItem(hMenu, ID_MAIN_VM_PAUSEFOCUS, (m_nPauseFocusLost ? MF_CHECKED : MF_UNCHECKED) | MF_BYCOMMAND);
	EnableMenuItem(hMenu, ID_MAIN_VM_SAVESTATE, (CPS2OS::GetELF() == NULL ? MF_GRAYED : 0) | MF_BYCOMMAND);
	EnableMenuItem(hMenu, ID_MAIN_VM_LOADSTATE, (CPS2OS::GetELF() == NULL ? MF_GRAYED : 0) | MF_BYCOMMAND);

	//Get state slot sub-menu
	memset(&MenuItem, 0, sizeof(MENUITEMINFO));
	MenuItem.cbSize = sizeof(MENUITEMINFO);
	MenuItem.fMask	= MIIM_SUBMENU;

	GetMenuItemInfo(hMenu, ID_MAIN_VM_STATESLOT, FALSE, &MenuItem);
	hMenu = MenuItem.hSubMenu;

	//Change state slot number checkbox
	for(i = 0; i < MAX_STATESLOTS; i++)
	{
		memset(&MenuItem, 0, sizeof(MENUITEMINFO));
		MenuItem.cbSize = sizeof(MENUITEMINFO);
		MenuItem.fMask	= MIIM_STATE;
		MenuItem.fState	= (m_nStateSlot == i) ? MFS_CHECKED : MFS_UNCHECKED;

		SetMenuItemInfo(hMenu, ID_MAIN_VM_STATESLOT_0 + i, FALSE, &MenuItem);
	}

	sExec = CPS2OS::GetExecutableName();
	if(strlen(sExec))
	{
		xconvert(sConvert, sExec, 256);
		xsnprintf(sTitle, countof(sTitle), _X("%s - [ %s ]"), xcond(TITLEA, TITLEW), sConvert);
	}
	else
	{
		xsnprintf(sTitle, countof(sTitle), _X("%s"), xcond(TITLEA, TITLEW));
	}

	SetText(sTitle);
}

void CMainWindow::PrintVersion(xchar* sVersion, size_t nCount)
{
	xchar sDate[256];
	xconvert(sDate, __DATE__, (unsigned int)strlen(__DATE__) + 1);

	xsnprintf(sVersion, nCount, _X("プレイ! v%i.%0.2i - %s"), VERSION_MAJOR, VERSION_MINOR, sDate);
}

void CMainWindow::OnOutputWndSizeChange(int nNothing)
{
	RefreshLayout();
}

void CMainWindow::OnNewFrame(int nNothing)
{
	m_nFrames++;
}

void CMainWindow::OnExecutableChange(int nNothing)
{
	UpdateUI();
}
