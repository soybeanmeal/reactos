/*
 * PROJECT:     ReactOS system libraries
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        dll/shellext/stobject/hotplug.cpp
 * PURPOSE:     Removable devices notification icon handler
 * PROGRAMMERS: Shriraj Sawant a.k.a SR13 <sr.official@hotmail.com> 
 */
#include <windows.h>
#include "precomp.h"
#include <mmsystem.h>
#include <mmddk.h>
#include <atlstr.h>
#include <atlsimpcoll.h>
#include <dbt.h>
#include <setupapi.h>
#include <cfgmgr32.h>

WINE_DEFAULT_DEBUG_CHANNEL(stobject);
#define DISPLAY_NAME_LEN 40

//BOOL WINAPI UnregisterDeviceNotification(HDEVNOTIFY Handle);

CSimpleArray<DEVINST> g_devList;
/*static HDEVNOTIFY g_hDevNotify = NULL;*/
static HICON g_hIconHotplug = NULL;
static LPCWSTR g_strTooltip = L"Safely Remove Hardware and Eject Media";
static WCHAR g_strMenuSel[DISPLAY_NAME_LEN];
static BOOL g_IsRunning = FALSE;
static BOOL g_IsRemoving = FALSE;

/*++
* @name EnumHotpluggedDevices
*
* Enumerates the connected safely removable devices.
*
* @param devList
*        List of device instances, representing the currently attached devices.
*
* @return The error code.
*
*--*/
HRESULT EnumHotpluggedDevices(CSimpleArray<DEVINST> &devList)
{
    devList.RemoveAll(); // Clear current devList
    HDEVINFO hdev = SetupDiGetClassDevs(NULL, NULL, 0, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (INVALID_HANDLE_VALUE == hdev)
        return E_HANDLE;
    SP_DEVINFO_DATA did = { 0 };
    did.cbSize = sizeof(did);

    // Enumerate all the attached devices.
    for (int idev = 0; SetupDiEnumDeviceInfo(hdev, idev, &did); idev++)
    {
        DWORD dwCapabilities = 0, dwSize = sizeof(dwCapabilities);
        WCHAR dispName[DISPLAY_NAME_LEN];
        ULONG ulStatus = 0, ulPnum = 0, ulLength = DISPLAY_NAME_LEN * sizeof(WCHAR);
        CONFIGRET cr = CM_Get_DevNode_Status(&ulStatus, &ulPnum, did.DevInst, 0);
        if (cr != CR_SUCCESS)
            continue;
        cr = CM_Get_DevNode_Registry_Property(did.DevInst, CM_DRP_DEVICEDESC, NULL, dispName, &ulLength, 0);
        if (cr != CR_SUCCESS)
            continue;
        cr = CM_Get_DevNode_Registry_Property(did.DevInst, CM_DRP_CAPABILITIES, NULL, &dwCapabilities, &dwSize, 0);
        if (cr != CR_SUCCESS)
            continue;

        // Filter and make list of only the appropriate safely removable devices.
        if ( (dwCapabilities & CM_DEVCAP_REMOVABLE) &&
            !(dwCapabilities & CM_DEVCAP_DOCKDEVICE) &&
            !(dwCapabilities & CM_DEVCAP_SURPRISEREMOVALOK) &&
            ((dwCapabilities & CM_DEVCAP_EJECTSUPPORTED) || (ulStatus & DN_DISABLEABLE)) &&
            !ulPnum)
        {
            devList.Add(did.DevInst);
        }
    }
    SetupDiDestroyDeviceInfoList(hdev);

    if (NO_ERROR != GetLastError() && ERROR_NO_MORE_ITEMS != GetLastError())
    {
        return E_UNEXPECTED;
    }

    return S_OK;
}

/*++
* @name NotifyBalloon
*
* Pops the balloon notification of the given notification icon.
*
* @param pSysTray
*        Provides interface for acquiring CSysTray information as required.
* @param szTitle
*        Title for the balloon notification.
* @param szInfo
*        Main content for the balloon notification.
* @param uId
*        Represents the particular notification icon.
*
* @return The error code.
*
*--*/
HRESULT NotifyBalloon(CSysTray* pSysTray, LPCWSTR szTitle = NULL, LPCWSTR szInfo = NULL, UINT uId = ID_ICON_HOTPLUG)
{
    NOTIFYICONDATA nim = { 0 };
    nim.cbSize = sizeof(NOTIFYICONDATA);
    nim.uID = uId;
    nim.hWnd = pSysTray->GetHWnd();

    nim.uFlags = NIF_INFO;
    nim.uTimeout = 10;
    nim.dwInfoFlags = NIIF_INFO;

    StringCchCopy(nim.szInfoTitle, _countof(nim.szInfoTitle), szTitle);
    StringCchCopy(nim.szInfo, _countof(nim.szInfo), szInfo);
    BOOL ret = Shell_NotifyIcon(NIM_MODIFY, &nim);

    Sleep(10000); /* As per windows, the balloon notification remains visible for atleast 10 sec.
                     This timer maintains the same condition.
                     Also it is required so that the icon doesn't hide instantly after last device is removed,
                     as that will prevent popping of notification.
                  */
    StringCchCopy(nim.szInfoTitle, _countof(nim.szInfoTitle), L"");
    StringCchCopy(nim.szInfo, _countof(nim.szInfo), L"");
    ret = Shell_NotifyIcon(NIM_MODIFY, &nim);
    g_IsRemoving = FALSE; /* This flag is used to prevent instant icon hiding after last device is removed.
                             The above timer maintains the required state for the same.
                          */
    return ret ? S_OK : E_FAIL;
}

HRESULT STDMETHODCALLTYPE Hotplug_Init(_In_ CSysTray * pSysTray)
{ 
    TRACE("Hotplug_Init\n");
    g_hIconHotplug = LoadIcon(g_hInstance, MAKEINTRESOURCE(IDI_HOTPLUG_OK));
    g_IsRunning = TRUE;
    EnumHotpluggedDevices(g_devList);

    return pSysTray->NotifyIcon(NIM_ADD, ID_ICON_HOTPLUG, g_hIconHotplug, g_strTooltip, NIS_HIDDEN);
}

HRESULT STDMETHODCALLTYPE Hotplug_Update(_In_ CSysTray * pSysTray)
{
    TRACE("Hotplug_Update\n");

    if(g_devList.GetSize() || g_IsRemoving)
        return pSysTray->NotifyIcon(NIM_MODIFY, ID_ICON_HOTPLUG, g_hIconHotplug, g_strTooltip);
    else
        return pSysTray->NotifyIcon(NIM_MODIFY, ID_ICON_HOTPLUG, g_hIconHotplug, g_strTooltip, NIS_HIDDEN);
}

HRESULT STDMETHODCALLTYPE Hotplug_Shutdown(_In_ CSysTray * pSysTray)
{
    TRACE("Hotplug_Shutdown\n");

    g_IsRunning = FALSE;

    return pSysTray->NotifyIcon(NIM_DELETE, ID_ICON_HOTPLUG, NULL, NULL);
}

static void _RunHotplug(CSysTray * pSysTray)
{
    ShellExecuteW(pSysTray->GetHWnd(), L"open", L"rundll32.exe shell32.dll,Control_RunDLL hotplug.dll", NULL, NULL, SW_SHOWNORMAL);
}

static void _ShowContextMenu(CSysTray * pSysTray)
{
    HMENU hPopup = CreatePopupMenu();
    ULONG ulLength = DISPLAY_NAME_LEN * sizeof(WCHAR);

    for (INT index = 0; index < g_devList.GetSize(); index++)
    {
        WCHAR dispName[DISPLAY_NAME_LEN], menuName[DISPLAY_NAME_LEN + 10];
        CONFIGRET cr = CM_Get_DevNode_Registry_Property(g_devList[index], CM_DRP_DEVICEDESC, NULL, dispName, &ulLength, 0);
        if (cr != CR_SUCCESS)
            StrCpyW(dispName, L"Unknown Device");

        swprintf(menuName, L"Eject %wS", dispName);
        AppendMenuW(hPopup, MF_STRING, index+1, menuName);
    }

    SetForegroundWindow(pSysTray->GetHWnd());
    DWORD flags = TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTALIGN | TPM_BOTTOMALIGN;
    POINT pt;
    GetCursorPos(&pt);

    DWORD id = TrackPopupMenuEx(hPopup, flags,
        pt.x, pt.y,
        pSysTray->GetHWnd(), NULL);

    if (id > 0)
    {
        id--; // since array indices starts from zero.
        CONFIGRET cr = CM_Get_DevNode_Registry_Property(g_devList[id], CM_DRP_DEVICEDESC, NULL, g_strMenuSel, &ulLength, 0);
        if (cr != CR_SUCCESS)
            StrCpyW(g_strMenuSel, L"Unknown Device");

        cr = CM_Request_Device_Eject_Ex(g_devList[id], 0, 0, 0, 0, 0);
        if (cr != CR_SUCCESS)
        {
            WCHAR strInfo[128];
            swprintf(strInfo, L"Problem Ejecting %wS", g_strMenuSel);
            MessageBox(0, L"The device cannot be stopped right now! Try stopping it again later!", strInfo, MB_OKCANCEL | MB_ICONEXCLAMATION);
        }
        else
        {
            //MessageBox(0, L"Device ejected successfully!! You can safely remove the device now!", L"Safely Remove Hardware", MB_OKCANCEL | MB_ICONINFORMATION);
            g_IsRemoving = TRUE;
            g_devList.RemoveAt(id); /* thing is.. even after removing id at this point, the devnode_change occurs after some seconds of sucessful removal
                                       and since pendrive is still plugged in it gets enumerated, if problem number is not filtered.
                                    */
        }
    }

    DestroyMenu(hPopup);
}

static void _ShowContextMenuR(CSysTray * pSysTray)
{
    CString strMenu((LPWSTR)IDS_HOTPLUG_REMOVE_2);
    HMENU hPopup = CreatePopupMenu();
    AppendMenuW(hPopup, MF_STRING, IDS_HOTPLUG_REMOVE_2, strMenu);

    SetForegroundWindow(pSysTray->GetHWnd());
    DWORD flags = TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTALIGN | TPM_BOTTOMALIGN;
    POINT pt;
    GetCursorPos(&pt);

    DWORD id = TrackPopupMenuEx(hPopup, flags,
        pt.x, pt.y,
        pSysTray->GetHWnd(), NULL);

    if (id == IDS_HOTPLUG_REMOVE_2)
    {
        _RunHotplug(pSysTray);
    }

    DestroyMenu(hPopup);
}

HRESULT STDMETHODCALLTYPE Hotplug_Message(_In_ CSysTray * pSysTray, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT &lResult)
{
    HRESULT hr = E_FAIL;
    TRACE("Hotplug_Message uMsg=%d, wParam=%x, lParam=%x\n", uMsg, wParam, lParam);

    switch (uMsg)
    {
        /*case WM_CREATE:
            TRACE("Hotplug_Message: WM_CREATE\n");
            DEV_BROADCAST_DEVICEINTERFACE NotificationFilter;

            ZeroMemory(&NotificationFilter, sizeof(NotificationFilter));
            NotificationFilter.dbcc_size = sizeof(DEV_BROADCAST_DEVICEINTERFACE);
            NotificationFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;

            g_hDevNotify = RegisterDeviceNotification(pSysTray->GetHWnd(), &NotificationFilter, DEVICE_NOTIFY_WINDOW_HANDLE | DEVICE_NOTIFY_ALL_INTERFACE_CLASSES);
            if (g_hDevNotify != NULL)
            {
                lResult = true;
                return S_OK;
            }
            return S_FALSE;*/

        case WM_USER + 220:
            TRACE("Hotplug_Message: WM_USER+220\n");
            if (wParam == 1)
            {
                if (lParam == FALSE)
                    return Hotplug_Init(pSysTray);
                else
                    return Hotplug_Shutdown(pSysTray);
            }
            return S_FALSE;

        case WM_USER + 221:
            TRACE("Hotplug_Message: WM_USER+221\n");
            if (wParam == 1)
            {
                lResult = (LRESULT)g_IsRunning;
                return S_OK;
            }
            return S_FALSE;

        case ID_ICON_HOTPLUG:
            Hotplug_Update(pSysTray);

            switch (lParam)
            {
                case WM_LBUTTONDOWN:
                    break;

                case WM_LBUTTONUP:
                    _ShowContextMenu(pSysTray);
                    break;

                case WM_LBUTTONDBLCLK:
                    _RunHotplug(pSysTray);
                    break;

                case WM_RBUTTONDOWN:
                    break;

                case WM_RBUTTONUP:
                    _ShowContextMenuR(pSysTray);
                    break;

                case WM_RBUTTONDBLCLK:
                    break;

                case WM_MOUSEMOVE:
                    break;
            }
            return S_OK;

        case WM_DEVICECHANGE:
            switch (wParam)
            {
                case DBT_DEVNODES_CHANGED:
                    hr = EnumHotpluggedDevices(g_devList);
                    if (FAILED(hr))
                        return hr;

                    lResult = true;
                    break;
                case DBT_DEVICEARRIVAL:
                    break;
                case DBT_DEVICEQUERYREMOVE:
                    break;
                case DBT_DEVICEQUERYREMOVEFAILED:
                    break;
                case DBT_DEVICEREMOVECOMPLETE:
                    WCHAR strInfo[128];
                    swprintf(strInfo, L"The %wS can now be safely removed from the system.", g_strMenuSel);
                    NotifyBalloon(pSysTray, L"Safe to Remove Hardware", strInfo);

                    lResult = true;
                    break;
                case DBT_DEVICEREMOVEPENDING:
                    break;
            }
            return S_OK;

        /*case WM_CLOSE:
            if (!UnregisterDeviceNotification(hDeviceNotify))
            {
                return S_FALSE;
            }
            return S_OK;*/

        default:
            TRACE("Hotplug_Message received for unknown ID %d, ignoring.\n");
            return S_FALSE;
    }

    return S_FALSE;
}
