#define _WIN32_WINNT 0x500
#include <windows.h>
#include <winuser.h>
#include <setupapi.h>

#include <stdio.h>

#include <string>
#include <vector>

#include "resource.h"

#define BUTTON_NUM 12
#define VOLUME_NUM 2

typedef struct {
	WORD buttonStatus;
	BYTE VolumeStatus[VOLUME_NUM];
} CONTROLLER_STATUS;

typedef struct {
	WORD buttonKeyAssign[BUTTON_NUM];
	WORD volumeKeyAssign[VOLUME_NUM][2];
} KEY_ASSIGN;

CONTROLLER_STATUS now_conStat = {0};
CONTROLLER_STATUS old_conStat = {0};

KEY_ASSIGN keyMap = {0};

int Button_View_ID[BUTTON_NUM] = {
		IDC_BTN1, IDC_BTN2, IDC_BTN3, IDC_BTN4,
		IDC_BTN5, IDC_BTN6, IDC_BTN7, IDC_BTN8,
		IDC_BTN9, IDC_BTN10, IDC_BTN11, IDC_BTN12
};
COLORREF Button_View_Back_Color[BUTTON_NUM];
COLORREF old_Button_View_Back_Color[BUTTON_NUM];

int Volume_View_ID[VOLUME_NUM][2] = {
		{IDC_VOL11, IDC_VOL12},
		{IDC_VOL21, IDC_VOL22}
};
COLORREF Volume_View_Back_Color[VOLUME_NUM][2];
COLORREF old_Volume_View_Back_Color[VOLUME_NUM][2];

typedef struct{
	bool exit_flag;
	int run_status;
	WCHAR error_msg[255];
	WCHAR portName[128];
} THREAD_PARAM;

typedef std::vector<std::wstring> COM_PORT_ITEM;

typedef std::vector<COM_PORT_ITEM> COM_PORT_LIST;

DWORD ThreadProc(LPVOID lParam)
{
	THREAD_PARAM *tParam = (THREAD_PARAM *)lParam;

	tParam->exit_flag = false;
	tParam->run_status = 1;

	HANDLE hCom = CreateFileW(tParam->portName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
	if(hCom == INVALID_HANDLE_VALUE){
		tParam->run_status = 3;
		lstrcpyW(tParam->error_msg, L"シリアルポートオープンに失敗しました");
		return 0;
	}

	//COM設定
	DCB comStat = {0};

	comStat.DCBlength = sizeof(DCB);
	comStat.BaudRate = 1500000;
	comStat.fBinary = TRUE;
	comStat.fParity = TRUE;
	comStat.Parity = ODDPARITY;
	comStat.StopBits = ONESTOPBIT;
	comStat.ByteSize = 8;

	SetCommState(hCom, &comStat);

	//タイムアウトの設定
	COMMTIMEOUTS comTimeout = {0};

	comTimeout.ReadTotalTimeoutConstant = 100;
	comTimeout.ReadTotalTimeoutMultiplier = 10;
	SetCommTimeouts(hCom, &comTimeout);

	WORD dataSize;
	BYTE *comBuffer;
	DWORD rn;
	int identifer_size = 4;
	BYTE *identifer = (LPBYTE)"BVST";
	int identifer_idx;
	int failed_counter = 0;
	while(tParam->exit_flag == false){
		if(++failed_counter > 10){
			tParam->run_status = 3;
			lstrcpyW(tParam->error_msg, L"コントローラとの通信に失敗しました");
			CloseHandle(hCom);
			return 0;
		}

		for(identifer_idx = 0; identifer_idx < identifer_size; identifer_idx++){
			BYTE id_tmp;
			ReadFile(hCom, &id_tmp, 1, &rn, NULL);
			if(rn == 0 || id_tmp != identifer[identifer_idx]){
				break;
			}
		}

		if(identifer_idx != identifer_size){
			continue;
		}

		ReadFile(hCom, &dataSize, 2, &rn, NULL);
		if(rn == 0){
			continue;
		}

		failed_counter = 0;

		comBuffer = new BYTE[dataSize];

		ReadFile(hCom, comBuffer, dataSize, &rn, NULL);
		if(rn == dataSize){
			now_conStat.buttonStatus = *((WORD *)comBuffer);

			now_conStat.VolumeStatus[0] = comBuffer[2];
			now_conStat.VolumeStatus[1] = comBuffer[3];

			INPUT keyInput[2] = {0};
			keyInput[0].type = INPUT_KEYBOARD;
			keyInput[1].type = INPUT_KEYBOARD;

			for(int i = 0; i < BUTTON_NUM; i++){
				BYTE now_bit = (now_conStat.buttonStatus >> i) & 1;
				BYTE old_bit = (old_conStat.buttonStatus >> i) & 1;

				if(now_bit != old_bit){
					BYTE scanCode = keyMap.buttonKeyAssign[i] & 0xFF;
					BYTE extended = keyMap.buttonKeyAssign[i] >> 8;

					if(scanCode == 0) continue;

					if(now_bit == 0){
						keyInput[0].ki.wScan = scanCode;
						keyInput[0].ki.dwFlags = KEYEVENTF_SCANCODE | extended;
					}else{
						keyInput[0].ki.wScan = scanCode;
						keyInput[0].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP | extended;
					}
					SendInput(1, keyInput, sizeof(INPUT));
				}
			}

			for(int i = 0; i < VOLUME_NUM; i++){
				if(now_conStat.VolumeStatus[i] != old_conStat.VolumeStatus[i]){
					BYTE scanCode[2];
					BYTE extended[2];

					scanCode[0] = keyMap.volumeKeyAssign[i][0] & 0xFF;
					scanCode[1] = keyMap.volumeKeyAssign[i][1] & 0xFF;
					extended[0] = keyMap.volumeKeyAssign[i][0] >> 8;
					extended[1] = keyMap.volumeKeyAssign[i][1] >> 8;

					if(scanCode[0] == 0 || scanCode[1] == 0) continue;

					switch(now_conStat.VolumeStatus[i]){
					case 0:
						keyInput[0].ki.wScan = scanCode[0];
						keyInput[0].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP | extended[0];
						keyInput[1].ki.wScan = scanCode[1];
						keyInput[1].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP | extended[1];
						break;
					case 1:
						keyInput[0].ki.wScan = scanCode[0];
						keyInput[0].ki.dwFlags = KEYEVENTF_SCANCODE | extended[0];
						keyInput[1].ki.wScan = scanCode[1];
						keyInput[1].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP | extended[1];
						break;
					case 2:
						keyInput[0].ki.wScan = scanCode[0];
						keyInput[0].ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP | extended[0];
						keyInput[1].ki.wScan = scanCode[1];
						keyInput[1].ki.dwFlags = KEYEVENTF_SCANCODE | extended[1];
						break;
					}
					SendInput(2, keyInput, sizeof(INPUT));
				}
			}

			memcpy(&old_conStat, &now_conStat, sizeof(CONTROLLER_STATUS));
		}

		delete[] comBuffer;

		tParam->run_status = 2;
	}
	CloseHandle(hCom);

	tParam->run_status = 3;

	return 1;
}

HWND hDlgGlobal;
WNDPROC old_EditProc;
LRESULT CALLBACK EditHookProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch(uMsg){
	case WM_KEYUP:
	case WM_SYSKEYUP:
	{
		WORD scanCode = (lParam >> 16) & 0x1FF;

		if(wParam == VK_DELETE){
			scanCode = 0;
			SetWindowTextW(hWnd, L"");
		}else{
			WCHAR buff[128];

			GetKeyNameTextW(lParam, buff, 128);
			SetWindowTextW(hWnd, buff);
			SetFocus(hWnd);
		}

		for(int i = 0; i < BUTTON_NUM; i++){
			HWND hCon = GetDlgItem(hDlgGlobal, Button_View_ID[i]);

			if(hCon == hWnd){
				keyMap.buttonKeyAssign[i] = scanCode;
				return 0;
			}
		}
		for(int i = 0; i < VOLUME_NUM; i++){
			for(int i2 = 0; i2 < 2; i2++){
				HWND hCon = GetDlgItem(hDlgGlobal, Volume_View_ID[i][i2]);

				if(hCon == hWnd){
					keyMap.volumeKeyAssign[i][i2] = scanCode;
					return 0;
				}
			}
		}
	}
		break;
	}

	return CallWindowProcW(old_EditProc, hWnd, uMsg, wParam, lParam);
}

COM_PORT_LIST getCOMPortList()
{
	COM_PORT_LIST result;

	HDEVINFO hDevInfo;
	SP_DEVINFO_DATA	sDevInfo;

	hDevInfo = SetupDiGetClassDevsW(NULL, 0, 0, DIGCF_PRESENT | DIGCF_ALLCLASSES);

	sDevInfo.cbSize = sizeof(SP_DEVINFO_DATA);

	DWORD dwIndex = 0;
	while(1){
		BOOL res = SetupDiEnumDeviceInfo(hDevInfo, dwIndex++, &sDevInfo);
		if(res == FALSE) break;

		DWORD dwRegType;
		DWORD dwSize;
		WCHAR pszName[1024];
		BOOL bRet = SetupDiGetDeviceRegistryPropertyW(hDevInfo, &sDevInfo, SPDRP_FRIENDLYNAME, &dwRegType,(BYTE*)pszName,1024,&dwSize);

		if(bRet){
			HKEY Key = SetupDiOpenDevRegKey(hDevInfo, &sDevInfo, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
			if(Key){
				WCHAR portName[2048];
				DWORD dwReqSize;
				dwReqSize = 1024;
				LONG lRet = RegQueryValueExW(Key, L"PortName", 0, &dwRegType, (LPBYTE) &portName, &dwReqSize);
				RegCloseKey(Key);

				if(lRet == ERROR_SUCCESS && wcsnicmp(portName, L"COM", 3) == 0){
					COM_PORT_ITEM item_tmp;
					item_tmp.push_back(portName);

					lstrcatW(portName, L" : ");
					lstrcatW(portName, pszName);

					item_tmp.push_back(portName);
					result.push_back(item_tmp);
				}
			}
		}
	}

	SetupDiDestroyDeviceInfoList(hDevInfo);

	return result;
}

INT_PTR CALLBACK DlgProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	static THREAD_PARAM tParam = {0};
	static bool key_assign_flag = false;
	static COM_PORT_LIST currentCOMPortList;

	switch(uMsg){
	case WM_INITDIALOG:
	{
		hDlgGlobal = hDlg;

		currentCOMPortList = getCOMPortList();
		COM_PORT_LIST::iterator it
						= currentCOMPortList.begin();

		for(; it != currentCOMPortList.end(); it++){
			SendDlgItemMessageW(hDlg, IDC_PortList, CB_ADDSTRING, (WPARAM)0, (LPARAM)(it->at(1).c_str()));
		}
		SendDlgItemMessageW(hDlg, IDC_PortList, CB_SETCURSEL, (WPARAM)0, (LPARAM)NULL);


		WCHAR local_path[255];
		GetModuleFileNameW(NULL, local_path, sizeof(local_path) / sizeof(WCHAR));
		for(int i = lstrlenW(local_path); i >= 0; i--){
			if(local_path[i] == L'\\'){
				local_path[i] = '\0';
				break;
			}
		}
		lstrcatW(local_path, L"\\keymap.ini");

		WCHAR keyName[100];
		WCHAR buff[250];
		old_EditProc = (WNDPROC)GetWindowLongW(GetDlgItem(hDlg, Button_View_ID[0]), GWL_WNDPROC);
		for(int i = 0; i < BUTTON_NUM; i++){
			Button_View_Back_Color[i] = RGB(0xFF, 0xFF, 0xFF);

			SetWindowLongW(GetDlgItem(hDlg, Button_View_ID[i]), GWL_WNDPROC, (LONG)EditHookProc);

			EnableWindow(GetDlgItem(hDlg, Button_View_ID[i]), FALSE);

			wsprintfW(keyName, L"Button%02d", i);
			keyMap.buttonKeyAssign[i] =
					GetPrivateProfileIntW(L"KEYMAP", keyName, 0, local_path);

			if(keyMap.buttonKeyAssign[i] > 0){
				GetKeyNameTextW(keyMap.buttonKeyAssign[i] << 16, buff, sizeof(buff) / sizeof(WCHAR));
				SetDlgItemTextW(hDlg, Button_View_ID[i], buff);
			}
		}
		for(int i = 0; i < VOLUME_NUM; i++){
			Volume_View_Back_Color[i][0] = RGB(0xFF, 0xFF, 0xFF);
			Volume_View_Back_Color[i][1] = RGB(0xFF, 0xFF, 0xFF);

			SetWindowLongW(GetDlgItem(hDlg, Volume_View_ID[i][0]), GWL_WNDPROC, (LONG)EditHookProc);
			SetWindowLongW(GetDlgItem(hDlg, Volume_View_ID[i][1]), GWL_WNDPROC, (LONG)EditHookProc);

			EnableWindow(GetDlgItem(hDlg, Volume_View_ID[i][0]), FALSE);
			EnableWindow(GetDlgItem(hDlg, Volume_View_ID[i][1]), FALSE);

			wsprintfW(keyName, L"Volume%02d0", i);
			keyMap.volumeKeyAssign[i][0] =
					GetPrivateProfileIntW(L"KEYMAP", keyName, 0, local_path);
			wsprintfW(keyName, L"Volume%02d1", i);
			keyMap.volumeKeyAssign[i][1] =
					GetPrivateProfileIntW(L"KEYMAP", keyName, 0, local_path);

			if(keyMap.volumeKeyAssign[i][0] > 0){
				GetKeyNameTextW(keyMap.volumeKeyAssign[i][0] << 16, buff, sizeof(buff) / sizeof(WCHAR));
				SetDlgItemTextW(hDlg, Volume_View_ID[i][0], buff);
			}
			if(keyMap.volumeKeyAssign[i][1] > 0){
				GetKeyNameTextW(keyMap.volumeKeyAssign[i][1] << 16, buff, sizeof(buff) / sizeof(WCHAR));
				SetDlgItemTextW(hDlg, Volume_View_ID[i][1], buff);
			}
		}

		tParam.error_msg[0] = L'\0';

		SetTimer(hDlg, 1000, 100, (TIMERPROC)DlgProc);
		SetTimer(hDlg, 1001, 1000, (TIMERPROC)DlgProc);
	}
		break;
	case WM_TIMER:
		if(wParam == 1000){
			if(tParam.error_msg[0] != L'\0'){
				WCHAR buff[255];
				lstrcpyW(buff, tParam.error_msg);
				tParam.error_msg[0] = L'\0';

				MessageBoxW(hDlg, buff, L"エラー", MB_ICONEXCLAMATION);

				if(tParam.run_status > 0){
					SendMessageW(hDlg, WM_COMMAND, IDC_CONNECT, (LPARAM)NULL);
				}
			}

			if(tParam.run_status == 2){
				for(int i = 0; i < BUTTON_NUM; i++){
					BYTE bit = (now_conStat.buttonStatus >> i) & 1;

					if(bit == 0){
						Button_View_Back_Color[i] = RGB(255, 128, 128);
					}else{
						Button_View_Back_Color[i] = RGB(255, 255, 255);
					}
				}
				for(int i = 0; i < VOLUME_NUM; i++){
					switch(now_conStat.VolumeStatus[i]){
					case 0:
						Volume_View_Back_Color[i][0] = RGB(255, 255, 255);
						Volume_View_Back_Color[i][1] = RGB(255, 255, 255);
						break;
					case 1:
						Volume_View_Back_Color[i][0] = RGB(255, 128, 128);
						Volume_View_Back_Color[i][1] = RGB(255, 255, 255);
						break;
					case 2:
						Volume_View_Back_Color[i][0] = RGB(255, 255, 255);
						Volume_View_Back_Color[i][1] = RGB(255, 128, 128);
						break;
					}
				}

				if(memcmp(old_Button_View_Back_Color, Button_View_Back_Color, sizeof(Button_View_Back_Color)) != 0 ||
					memcmp(old_Volume_View_Back_Color, Volume_View_Back_Color, sizeof(Volume_View_Back_Color)) != 0){

					InvalidateRgn(hDlg, NULL, FALSE);
					UpdateWindow(hDlg);

					memcpy(old_Button_View_Back_Color, Button_View_Back_Color, sizeof(Button_View_Back_Color));
					memcpy(old_Volume_View_Back_Color, Volume_View_Back_Color, sizeof(Volume_View_Back_Color));
				}
			}
		}else if(wParam == 1001){
			COM_PORT_LIST new_com_tmp;

			new_com_tmp = getCOMPortList();

			if(new_com_tmp.size() != currentCOMPortList.size()){
				while(SendDlgItemMessageW(hDlg, IDC_PortList, CB_GETCOUNT, (WPARAM)0, (LPARAM)0) > 0){
					SendDlgItemMessageW(hDlg, IDC_PortList, CB_DELETESTRING, (WPARAM)0, (LPARAM)0);
				}

				currentCOMPortList = new_com_tmp;

				COM_PORT_LIST::iterator it = currentCOMPortList.begin();
				for(; it != currentCOMPortList.end(); it++){
					SendDlgItemMessageW(hDlg, IDC_PortList, CB_ADDSTRING, (WPARAM)0, (LPARAM)(it->at(1).c_str()));
				}
				SendDlgItemMessageW(hDlg, IDC_PortList, CB_SETCURSEL, (WPARAM)0, (LPARAM)NULL);
			}
		}
		break;
	case WM_CTLCOLORSTATIC:
	{
		HDC hDC = (HDC)wParam;
		HWND hCtrl = (HWND)lParam;

		for(int i = 0; i < BUTTON_NUM; i++){
			if(hCtrl == GetDlgItem(hDlg, Button_View_ID[i])){
				SetBkColor(hDC, Button_View_Back_Color[i]);

				return (LRESULT)GetStockObject(WHITE_BRUSH);
			}
		}

		for(int i = 0; i < VOLUME_NUM; i++){
			for(int i2 = 0; i2 < 2; i2++){
				if(hCtrl == GetDlgItem(hDlg, Volume_View_ID[i][i2])){
					SetBkColor(hDC, Volume_View_Back_Color[i][i2]);

					return (LRESULT)GetStockObject(WHITE_BRUSH);
				}
			}
		}

		break;
	}
	case WM_COMMAND:
		switch(LOWORD(wParam)){
		case IDC_CONNECT:
		{
			if(tParam.run_status == 0){
				EnableWindow(GetDlgItem(hDlg, IDC_PortList), FALSE);

				LONG curSel =
						SendDlgItemMessageW(hDlg, IDC_PortList, CB_GETCURSEL, (WPARAM)NULL, (LPARAM)NULL);
				if(curSel >= 0){
					WCHAR buff[128];

					SendDlgItemMessageW(hDlg, IDC_PortList, CB_GETLBTEXT, (WPARAM)curSel, (LPARAM)buff);

					LPWSTR foundStr = wcsstr(buff, L" : ");
					if(foundStr > buff){
						lstrcpynW(tParam.portName, buff, foundStr - buff + 1);
					}

					tParam.exit_flag = false;
					CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ThreadProc, &tParam, 0, NULL);
					while(tParam.run_status == 0) Sleep(1);

					SetDlgItemTextW(hDlg, IDC_CONNECT, L"切断");
				}
			}else{
				tParam.exit_flag = true;
				while(tParam.run_status != 3) Sleep(100);
				tParam.run_status = 0;

				SetDlgItemTextW(hDlg, IDC_CONNECT, L"接続");

				for(int i = 0; i < BUTTON_NUM; i++){
					EnableWindow(GetDlgItem(hDlg, Button_View_ID[i]), FALSE);
				}
				for(int i = 0; i < VOLUME_NUM; i++){
					EnableWindow(GetDlgItem(hDlg, Volume_View_ID[i][0]), FALSE);
					EnableWindow(GetDlgItem(hDlg, Volume_View_ID[i][1]), FALSE);
				}
				key_assign_flag = false;
				SetDlgItemTextW(hDlg, IDC_KEYASSIGN, L"キー割り当て");

				EnableWindow(GetDlgItem(hDlg, IDC_PortList), TRUE);
			}
		}
			break;
		case IDC_KEYASSIGN:
			if(tParam.run_status == 2){
				BOOL enabled = FALSE;
				if(key_assign_flag == false){
					enabled = TRUE;
					SetDlgItemTextW(hDlg, IDC_KEYASSIGN, L"キー割り当て解除");
				}else{
					SetDlgItemTextW(hDlg, IDC_KEYASSIGN, L"キー割り当て");
				}
				key_assign_flag = !key_assign_flag;

				for(int i = 0; i < BUTTON_NUM; i++){
					EnableWindow(GetDlgItem(hDlg, Button_View_ID[i]), enabled);
				}
				for(int i = 0; i < VOLUME_NUM; i++){
					EnableWindow(GetDlgItem(hDlg, Volume_View_ID[i][0]), enabled);
					EnableWindow(GetDlgItem(hDlg, Volume_View_ID[i][1]), enabled);
				}
			}
			break;
		case IDCANCEL:
			if(tParam.run_status > 0){
				SendMessageW(hDlg, WM_COMMAND, IDC_CONNECT, (LPARAM)NULL);
			}

			EndDialog(hDlg, 0);
			break;
		}
		break;
	case WM_DESTROY:
	{
		WCHAR local_path[255];

		GetModuleFileNameW(NULL, local_path, sizeof(local_path) / sizeof(WCHAR));

		for(int i = lstrlenW(local_path); i >= 0; i--){
			if(local_path[i] == L'\\'){
				local_path[i] = '\0';
				break;
			}
		}

		lstrcatW(local_path, L"\\keymap.ini");

		WCHAR keyName[100];
		WCHAR value[100];
		for(int i = 0; i < BUTTON_NUM; i++){
			wsprintfW(keyName, L"Button%02d", i);
			wsprintfW(value, L"%d", keyMap.buttonKeyAssign[i]);

			WritePrivateProfileStringW(L"KEYMAP", keyName, value, local_path);
		}
		for(int i = 0; i < VOLUME_NUM; i++){
			if(keyMap.volumeKeyAssign[i][0] == 0 || keyMap.volumeKeyAssign[i][1] == 0){
				keyMap.volumeKeyAssign[i][0] = 0;
				keyMap.volumeKeyAssign[i][1] = 0;
			}

			wsprintfW(keyName, L"Volume%02d0", i);
			wsprintfW(value, L"%d", keyMap.volumeKeyAssign[i][0]);

			WritePrivateProfileStringW(L"KEYMAP", keyName, value, local_path);

			wsprintfW(keyName, L"Volume%02d1", i);
			wsprintfW(value, L"%d", keyMap.volumeKeyAssign[i][1]);

			WritePrivateProfileStringW(L"KEYMAP", keyName, value, local_path);
		}
	}
		break;
	default:
		return 0;
		break;
	}

	return 1;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
	DialogBox(hInstance, MAKEINTRESOURCE(IDD_DIALOG1), NULL, (DLGPROC)DlgProc);

	return 0;
}
