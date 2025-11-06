/*
 * 极简 Win32 二维码识别器 - ZXing-CPP 版本
 * (已修复 Stride 导致的闪退问题)
 *
 * 修复版 (by Gemini):
 * 1. 修复中文编码问题 (GUI转为Unicode, 增加WideToUTF8转换)
 * 2. 启用 Windows 视觉样式 (Common Controls 6.0)
 * 3. 修复GUI窗口定位 (居中) 和拖动问题 (修复WndProc)
 */

#ifdef UNICODE
#undef UNICODE
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <thread>
#include <string>
#include <vector>
#include <commctrl.h> // for WC_STATICW etc.

// ZXing-CPP 头文件 
#include <ZXing/ReadBarcode.h>
#include <ZXing/BarcodeFormat.h>
#include <ZXing/DecodeHints.h>
#include <ZXing/ImageView.h>
#include <ZXing/Barcode.h>  // 使用新的 Barcode 替代 Result

// nayuki QR code generator 头文件
#include "qrcodegen.hpp" 

#pragma comment(lib, "gdiplus.lib")

// 启用 Windows 视觉样式 (XP/Vista/7/10/11 主题)
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// --- 全局变量 ---
const char* CLASS_NAME = "MinimalQRTrayApp";
const char* OVERLAY_CLASS_NAME = "QRScreenshotOverlay";
const UINT WM_APP_TRAYMSG = WM_APP + 1;
const UINT WM_APP_SHOW_RESULT = WM_APP + 2;
const UINT WM_APP_DO_RECOGNITION = WM_APP + 3;
const UINT HOTKEY_ID = 1;
const UINT MENU_SCAN_QR = 1001;
const UINT MENU_GENERATE_QR = 1002;
const UINT MENU_SETTINGS = 1003;
const UINT MENU_EXIT = 1004;
const UINT MENU_SETTINGS_HOTKEY_SCAN = 1005;
const UINT MENU_SETTINGS_HOTKEY_GENERATE = 1006;
const UINT MENU_SETTINGS_AUTOSTART = 1007;

// QR Generation Dialog IDs
const int IDC_EDIT_TEXT = 2001;
const int IDC_COMBO_SIZE = 2002;
const int IDC_COMBO_ECC = 2003;
const int IDC_STATIC_PREVIEW = 2004;
const int IDC_BTN_SAVE_PNG = 2005;
const int IDC_BTN_SAVE_JPG = 2006;
const int IDC_BTN_COPY = 2007;
const int IDC_BTN_GENERATE = 2008;

// Settings Dialog IDs
const int IDC_HOTKEY_CTRL = 3001;
const int IDC_HOTKEY_ALT = 3002;
const int IDC_HOTKEY_SHIFT = 3003;
const int IDC_HOTKEY_WIN = 3004;
const int IDC_COMBO_KEY = 3005;
const int IDC_BTN_APPLY = 3006;
const int IDC_BTN_RESET = 3007;
const int IDC_CHECK_AUTOSTART = 3008;

// QR Generation Hotkey IDs
const int IDC_HOTKEY_GEN_CTRL = 3101;
const int IDC_HOTKEY_GEN_ALT = 3102;
const int IDC_HOTKEY_GEN_SHIFT = 3103;
const int IDC_HOTKEY_GEN_WIN = 3104;
const int IDC_COMBO_GEN_KEY = 3105;
const int IDC_BTN_GEN_APPLY = 3106;
const int IDC_BTN_GEN_RESET = 3107;
const int IDC_CHECK_GEN_ENABLE = 3108;

HWND g_hwnd;
HINSTANCE g_hinstance;
bool g_is_scanning = false;
std::thread g_scanThread;
std::string g_qr_result; // 存储识别结果
HBITMAP g_captured_bitmap = nullptr; // 存储截图位图

// 热键配置
struct HotkeyConfig {
    UINT modifiers; // MOD_CONTROL, MOD_ALT, MOD_SHIFT, MOD_WIN
    UINT vkCode;    // 虚拟键码
};

HotkeyConfig g_hotkeyConfig = {MOD_CONTROL | MOD_ALT, 'Q'}; // 默认 Ctrl+Alt+Q
HotkeyConfig g_hotkeyGenConfig = {MOD_CONTROL, 'Q'}; // 默认 Ctrl+Q
bool g_hotkeyGenEnabled = false; // 默认禁用生成快捷键
bool g_autoStartEnabled = false; // 开机自启
const UINT HOTKEY_GEN_ID = 2;

struct OverlayData {
    RECT selection;
    POINT startPoint;
    bool isSelecting;
    bool isCompleted;
    bool isCancelled;
    HBITMAP hScreenshot;
};

OverlayData g_overlayData = {0};

// QR Generation Dialog Data
struct QRGenData {
    HBITMAP hPreviewBitmap;
    Gdiplus::Bitmap* pGdiplusBitmap;
    std::string currentText; // 存储 UTF-8 文本
    int scale;
    qrcodegen::QrCode::Ecc eccLevel;
};

QRGenData g_qrGenData = {0};

// --- 函数声明 ---
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK OverlayWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK QRGenDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam); // 更改：LRESULT
LRESULT CALLBACK SettingsDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK GenerateHotkeyDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
void AddTrayIcon(HWND hwnd);
void RemoveTrayIcon(HWND hwnd);
void ShowContextMenu(HWND hwnd);
void TriggerScanProcess(HWND hwnd);
void ShowQRGenerationWindow(HWND hwnd);
void ShowSettingsWindow(HWND hwnd);
void ShowScanHotkeySettings(HWND hwnd);
void ShowGenerateHotkeySettings(HWND hwnd);
bool RegisterCurrentHotkey(HWND hwnd);
bool RegisterGenerateHotkey(HWND hwnd);
void SaveHotkeyConfig();
void LoadHotkeyConfig();
void SaveAutoStartConfig();
void LoadAutoStartConfig();
bool SetAutoStart(bool enable);
bool IsAutoStartEnabled();
std::wstring GetKeyName(UINT vkCode);
bool ShowScreenshotOverlay(RECT* outRect);
bool ScanImageForQR(HBITMAP hBitmap, std::string& outErrorMsg); // 声明
std::string GenerateQRCode(const std::string& text);
void UpdateQRPreview(HWND hwndDlg);
void SaveQRCodeImage(HWND hwndDlg, bool asPNG);
void CopyQRToClipboard(HWND hwndDlg);
void CopyToClipboard(const std::string& text);
void CopyBitmapToClipboard(HBITMAP hBitmap);
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid);
HBITMAP CaptureScreen();
HBITMAP CaptureScreenRegion(const RECT& rect);
std::string WideToUTF8(const std::wstring& wideString); // 新增

// --- GDI+ 初始化 ---
ULONG_PTR g_gdiplusToken;
void InitializeGDIPlus() {
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, NULL);
}
void ShutdownGDIPlus() {
    Gdiplus::GdiplusShutdown(g_gdiplusToken);
}

// --- 单实例 ---
HANDLE g_mutex;
bool IsAlreadyRunning() {
    g_mutex = CreateMutexA(NULL, TRUE, "MinimalQRTrayApp_Mutex");
    if (g_mutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (g_mutex) CloseHandle(g_mutex);
        MessageBoxA(NULL, "程序已在运行中。\n请检查系统托盘。", "QRTray", MB_OK | MB_ICONINFORMATION);
        return true;
    }
    return false;
}
void ReleaseMutex() {
    if (g_mutex) {
        ReleaseMutex(g_mutex);
        CloseHandle(g_mutex);
    }
}

// --- 程序入口点 ---
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    g_hinstance = hInstance;
    SetProcessDPIAware();

    if (IsAlreadyRunning()) {
        return 0;
    }

    InitializeGDIPlus();
    LoadHotkeyConfig(); // 加载快捷键配置
    LoadAutoStartConfig(); // 加载开机自启配置

    // 注册窗口类
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    
    if (!RegisterClassA(&wc)) {
        MessageBoxA(NULL, "注册窗口类失败!", "错误", MB_OK | MB_ICONERROR);
        return 0;
    }

    // 注册覆盖层窗口类
    WNDCLASSA overlayWc = {0};
    overlayWc.lpfnWndProc = OverlayWindowProc;
    overlayWc.hInstance = hInstance;
    overlayWc.lpszClassName = OVERLAY_CLASS_NAME;
    overlayWc.hCursor = LoadCursor(NULL, IDC_CROSS);
    overlayWc.hbrBackground = NULL;
    
    if (!RegisterClassA(&overlayWc)) {
        MessageBoxA(NULL, "注册覆盖层窗口类失败!", "错误", MB_OK | MB_ICONERROR);
        return 0;
    }

    // 创建隐藏的窗口
    g_hwnd = CreateWindowExA(
        0, CLASS_NAME, "Minimal QR Tray App",
        0, 0, 0, 0, 0,
        HWND_MESSAGE,
        NULL, hInstance, NULL
    );

    if (g_hwnd == NULL) {
        MessageBoxA(NULL, "创建窗口失败!", "错误", MB_OK | MB_ICONERROR);
        return 0;
    }

    // 注册全局热键
    if (!RegisterCurrentHotkey(g_hwnd)) {
        std::wstring keyName = GetKeyName(g_hotkeyConfig.vkCode);
        std::wstring msg = L"注册全局热键失败!\n请检查是否有其他程序占用了该快捷键。\n当前设置: " + keyName;
        MessageBoxW(NULL, msg.c_str(), L"错误", MB_OK | MB_ICONERROR);
    }
    
    // 注册生成快捷键（如果启用）
    if (g_hotkeyGenEnabled) {
        RegisterGenerateHotkey(g_hwnd);
    }

    AddTrayIcon(g_hwnd);

    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    // 清理
    ReleaseMutex();
    ShutdownGDIPlus();
    return (int)msg.wParam;
}

// --- 窗口消息处理函数 ---
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    
    switch (uMsg) {
        case WM_CLOSE:
            DestroyWindow(hwnd);
            break;
        
        case WM_DESTROY:
            RemoveTrayIcon(hwnd);
            UnregisterHotKey(hwnd, HOTKEY_ID);
            UnregisterHotKey(hwnd, HOTKEY_GEN_ID);
            
            // 等待扫描线程结束
            if (g_scanThread.joinable()) {
                g_scanThread.join();
            }
            PostQuitMessage(0);
            break;

        case WM_HOTKEY:
            if (wParam == HOTKEY_ID) {
                TriggerScanProcess(hwnd);
            } else if (wParam == HOTKEY_GEN_ID) {
                ShowQRGenerationWindow(hwnd);
            }
            break;
        
        case WM_APP_TRAYMSG:
            switch (lParam) {
                case WM_LBUTTONDBLCLK:
                    TriggerScanProcess(hwnd);
                    break;
                case WM_RBUTTONUP:
                    ShowContextMenu(hwnd);
                    break;
            }
            break;
        
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case MENU_SCAN_QR:
                    TriggerScanProcess(hwnd);
                    break;
                case MENU_GENERATE_QR:
                    ShowQRGenerationWindow(hwnd);
                    break;
                case MENU_SETTINGS:
                    ShowSettingsWindow(hwnd);
                    break;
                case MENU_SETTINGS_HOTKEY_SCAN:
                    ShowScanHotkeySettings(hwnd);
                    break;
                case MENU_SETTINGS_HOTKEY_GENERATE:
                    ShowGenerateHotkeySettings(hwnd);
                    break;
                case MENU_SETTINGS_AUTOSTART:
                    g_autoStartEnabled = !g_autoStartEnabled;
                    SetAutoStart(g_autoStartEnabled);
                    SaveAutoStartConfig();
                    break;
                case MENU_EXIT:
                    DestroyWindow(hwnd);
                    break;
            }
            break;
            
        case WM_APP_SHOW_RESULT:
            // 在主线程中显示识别结果
            try {
                
                // 确保消息框在最顶层
                SetForegroundWindow(hwnd);
                
                if (wParam == 1) { // 成功
                    // 转换为 WCHAR (UTF-16) 来显示中文
                    std::wstring wResult;
                    int wideLen = MultiByteToWideChar(CP_UTF8, 0, g_qr_result.c_str(), -1, NULL, 0);
                    if (wideLen > 0) {
                        wResult.resize(wideLen - 1);
                        MultiByteToWideChar(CP_UTF8, 0, g_qr_result.c_str(), -1, &wResult[0], wideLen);
                    } else {
                        wResult = L"[转换结果失败]";
                    }

                    std::wstring successMsg = L"识别成功！已复制到剪贴板:\n\n" + wResult;
                    successMsg += L"\n\n格式: QR Code"; // TODO: result.formatString()
                    successMsg += L"\n长度: " + std::to_wstring(wResult.length()) + L" 字符";
                    
                    MessageBoxW(hwnd, successMsg.c_str(), L"二维码扫描 (ZXing)", MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
                    
                } else { // 失败
                    // 失败消息通常是 ANSI (英文)，但也转为 WCHAR
                    std::wstring wErrorMsg;
                    int wideLen = MultiByteToWideChar(CP_ACP, 0, g_qr_result.c_str(), -1, NULL, 0);
                     if (wideLen > 0) {
                        wErrorMsg.resize(wideLen - 1);
                        MultiByteToWideChar(CP_ACP, 0, g_qr_result.c_str(), -1, &wErrorMsg[0], wideLen);
                    } else {
                        wErrorMsg = L"[转换错误信息失败]";
                    }
                    MessageBoxW(hwnd, wErrorMsg.c_str(), L"扫描结果", MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
                }
                // 重置扫描状态
                g_is_scanning = false;
                
            } catch (const std::exception& e) {
                g_is_scanning = false;
                MessageBoxA(hwnd, "显示结果时发生错误", "错误", MB_OK | MB_ICONERROR | MB_TOPMOST);
            } catch (...) {
                g_is_scanning = false;
                MessageBoxA(hwnd, "显示结果时发生未知错误", "错误", MB_OK | MB_ICONERROR | MB_TOPMOST);
            }
            break;
            
        case WM_APP_DO_RECOGNITION:
            // 在主线程中执行ZXing识别
            try {
                
                if (g_captured_bitmap) {
                    std::string errorMsg;
                    bool success = ScanImageForQR(g_captured_bitmap, errorMsg);
                    
                    // 清理位图
                    DeleteObject(g_captured_bitmap);
                    g_captured_bitmap = nullptr;
                    
                    if (success) {
                        // 成功消息已由 ScanImageForQR (通过PostMessage) 处理
                    } else {
                        g_qr_result = errorMsg;
                        PostMessage(hwnd, WM_APP_SHOW_RESULT, 0, 0);
                    }
                } else {
                    g_qr_result = "内部错误：位图数据丢失";
                    PostMessage(hwnd, WM_APP_SHOW_RESULT, 0, 0);
                }
                
            } catch (const std::exception& e) {
                if (g_captured_bitmap) {
                    DeleteObject(g_captured_bitmap);
                    g_captured_bitmap = nullptr;
                }
                g_qr_result = "识别过程异常: ";
                g_qr_result += e.what();
                PostMessage(hwnd, WM_APP_SHOW_RESULT, 0, 0);
            } catch (...) {
                if (g_captured_bitmap) {
                    DeleteObject(g_captured_bitmap);
                    g_captured_bitmap = nullptr;
                }
                g_qr_result = "识别过程发生未知异常";
                PostMessage(hwnd, WM_APP_SHOW_RESULT, 0, 0);
            }
            break;

        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
    return 0;
}

// --- 覆盖层窗口消息处理函数 ---
LRESULT CALLBACK OverlayWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE:
            SetLayeredWindowAttributes(hwnd, 0, 128, LWA_ALPHA);
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            HDC memDC = CreateCompatibleDC(hdc);
            HBITMAP memBitmap = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
            HBITMAP oldBitmap = (HBITMAP)SelectObject(memDC, memBitmap);
            HBRUSH bgBrush = CreateSolidBrush(RGB(0, 0, 0));
            FillRect(memDC, &clientRect, bgBrush);
            DeleteObject(bgBrush);
            if (g_overlayData.isSelecting || g_overlayData.isCompleted) {
                HPEN pen = CreatePen(PS_SOLID, 2, RGB(255, 0, 0));
                HPEN oldPen = (HPEN)SelectObject(memDC, pen);
                HBRUSH oldBrush = (HBRUSH)SelectObject(memDC, GetStockObject(NULL_BRUSH));
                Rectangle(memDC, g_overlayData.selection.left, g_overlayData.selection.top, g_overlayData.selection.right, g_overlayData.selection.bottom);
                SelectObject(memDC, oldPen);
                SelectObject(memDC, oldBrush);
                DeleteObject(pen);
                if (g_overlayData.selection.right > g_overlayData.selection.left && g_overlayData.selection.bottom > g_overlayData.selection.top) {
                    int width = g_overlayData.selection.right - g_overlayData.selection.left;
                    int height = g_overlayData.selection.bottom - g_overlayData.selection.top;
                    char sizeText[64];
                    sprintf_s(sizeText, "%d x %d", width, height);
                    SetTextColor(memDC, RGB(255, 255, 255));
                    SetBkMode(memDC, TRANSPARENT);
                    RECT textRect = {g_overlayData.selection.left, g_overlayData.selection.top - 25, g_overlayData.selection.right, g_overlayData.selection.top};
                    DrawTextA(memDC, sizeText, -1, &textRect, DT_LEFT | DT_TOP);
                }
            }
            SetTextColor(memDC, RGB(255, 255, 255));
            SetBkMode(memDC, TRANSPARENT);
            const char* helpText = "拖拽鼠标选择区域，按 ESC 取消 (ZXing版)";
            RECT helpRect = {10, 10, clientRect.right - 10, 50};
            DrawTextA(memDC, helpText, -1, &helpRect, DT_LEFT | DT_TOP);
            BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, memDC, 0, 0, SRCCOPY);
            SelectObject(memDC, oldBitmap);
            DeleteObject(memBitmap);
            DeleteDC(memDC);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            g_overlayData.startPoint.x = x;
            g_overlayData.startPoint.y = y;
            g_overlayData.selection.left = x;
            g_overlayData.selection.top = y;
            g_overlayData.selection.right = x;
            g_overlayData.selection.bottom = y;
            g_overlayData.isSelecting = true;
            SetCapture(hwnd);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (g_overlayData.isSelecting) {
                int x = LOWORD(lParam);
                int y = HIWORD(lParam);
                g_overlayData.selection.left = (g_overlayData.startPoint.x < x) ? g_overlayData.startPoint.x : x;
                g_overlayData.selection.top = (g_overlayData.startPoint.y < y) ? g_overlayData.startPoint.y : y;
                g_overlayData.selection.right = (g_overlayData.startPoint.x > x) ? g_overlayData.startPoint.x : x;
                g_overlayData.selection.bottom = (g_overlayData.startPoint.y > y) ? g_overlayData.startPoint.y : y;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        case WM_LBUTTONUP: {
            if (g_overlayData.isSelecting) {
                g_overlayData.isSelecting = false;
                ReleaseCapture();
                
                int width = g_overlayData.selection.right - g_overlayData.selection.left;
                int height = g_overlayData.selection.bottom - g_overlayData.selection.top;
                
                if (width >= 10 && height >= 10) {
                    g_overlayData.isCompleted = true;
                    PostMessage(hwnd, WM_CLOSE, 0, 0);
                } else {
                    g_overlayData.isCompleted = false;
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;
        }

        case WM_KEYDOWN: {
            if (wParam == VK_ESCAPE) {
                g_overlayData.isCancelled = true;
                PostMessage(hwnd, WM_CLOSE, 0, 0);
            }
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            return 0;

        default:
            return DefWindowProc(hwnd, uMsg, wParam, lParam);
    }
}

// --- 托盘图标函数 ---
void AddTrayIcon(HWND hwnd) {
    NOTIFYICONDATAA nid = {0};
    nid.cbSize = sizeof(NOTIFYICONDATAA);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_APP_TRAYMSG;
    nid.hIcon = LoadIcon(NULL, IDI_INFORMATION); 
    strcpy_s(nid.szTip, "二维码识别工具 (ZXing版)");
    Shell_NotifyIconA(NIM_ADD, &nid);
}

void RemoveTrayIcon(HWND hwnd) {
    NOTIFYICONDATAA nid = {0};
    nid.cbSize = sizeof(NOTIFYICONDATAA);
    nid.hWnd = hwnd;
    nid.uID = 1;
    Shell_NotifyIconA(NIM_DELETE, &nid);
}

void ShowContextMenu(HWND hwnd) {
    HMENU hMenu = CreatePopupMenu();
    InsertMenuA(hMenu, -1, MF_BYPOSITION, MENU_SCAN_QR, "截图扫码 (ZXing)");
    InsertMenuA(hMenu, -1, MF_BYPOSITION, MENU_GENERATE_QR, "生成二维码");
    
    // 创建设置子菜单
    HMENU hSettingsMenu = CreatePopupMenu();
    InsertMenuA(hSettingsMenu, -1, MF_BYPOSITION, MENU_SETTINGS_HOTKEY_SCAN, "扫码快捷键设置");
    InsertMenuA(hSettingsMenu, -1, MF_BYPOSITION, MENU_SETTINGS_HOTKEY_GENERATE, "生成快捷键设置");
    InsertMenuA(hSettingsMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    
    // 开机自启选项（带勾选状态）
    UINT autoStartFlags = MF_BYPOSITION;
    if (g_autoStartEnabled) {
        autoStartFlags |= MF_CHECKED;
    }
    InsertMenuA(hSettingsMenu, -1, autoStartFlags, MENU_SETTINGS_AUTOSTART, "开机自启");
    
    // 添加设置子菜单到主菜单
    InsertMenuA(hMenu, -1, MF_BYPOSITION | MF_POPUP, (UINT_PTR)hSettingsMenu, "设置");
    
    InsertMenuA(hMenu, -1, MF_BYPOSITION | MF_SEPARATOR, 0, NULL);
    InsertMenuA(hMenu, -1, MF_BYPOSITION, MENU_EXIT, "退出");
    SetMenuDefaultItem(hMenu, MENU_SCAN_QR, FALSE);
    SetForegroundWindow(hwnd); 
    POINT pt;
    GetCursorPos(&pt);
    TrackPopupMenu(hMenu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, NULL);
    PostMessage(hwnd, WM_NULL, 0, 0); 
    DestroyMenu(hMenu);
}

// --- 核心功能 ---
void TriggerScanProcess(HWND hwnd) {
    if (g_is_scanning) {
        // 静默忽略，避免重复弹出提示框
        return; 
    }
    
    // 确保之前的线程已经结束
    if (g_scanThread.joinable()) {
        g_scanThread.join();
    }
    
    g_is_scanning = true;
    

    if (g_scanThread.joinable()) {
        g_scanThread.join();
    }
    
    g_scanThread = std::thread([hwnd]() {
        try {
            
            RECT selectionRect;
            bool success = ShowScreenshotOverlay(&selectionRect);
            
            if (!success) {
                g_is_scanning = false;
                return;
            }

            HBITMAP hBitmap = CaptureScreenRegion(selectionRect);
            
            if (hBitmap) {
                
                g_captured_bitmap = hBitmap;
                BOOL postResult = PostMessage(hwnd, WM_APP_DO_RECOGNITION, 0, 0);
                
                if (!postResult) {
                    DeleteObject(hBitmap);
                    g_captured_bitmap = nullptr;
                    g_is_scanning = false;
                }
            } else {
                g_qr_result = "截图失败，请重试";
                PostMessage(hwnd, WM_APP_SHOW_RESULT, 0, 0);
            }
        } catch (const std::exception& e) {
            
            std::string errorMsg = "扫描过程中发生异常: ";
            errorMsg += e.what();
            
            g_qr_result = errorMsg;
            PostMessage(hwnd, WM_APP_SHOW_RESULT, 0, 0); 
        } catch (...) {
            
            g_qr_result = "扫描过程中发生未知错误";
            PostMessage(hwnd, WM_APP_SHOW_RESULT, 0, 0);
        }
    }); 
}

// 显示全屏覆盖窗口让用户选择区域
bool ShowScreenshotOverlay(RECT* outRect) {
    
    memset(&g_overlayData, 0, sizeof(g_overlayData));
    
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    
    HWND overlayWnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        OVERLAY_CLASS_NAME,
        "Screenshot Overlay",
        WS_POPUP,
        0, 0, screenWidth, screenHeight,
        NULL, NULL, g_hinstance, NULL
    );
    
    if (!overlayWnd) {
        MessageBoxA(NULL, "创建覆盖层窗口失败!", "错误", MB_OK | MB_ICONERROR);
        return false;
    }
    
    ShowWindow(overlayWnd, SW_SHOW);
    UpdateWindow(overlayWnd);
    SetForegroundWindow(overlayWnd);
    
    MSG msg;
    int msgCount = 0;
    
    while (IsWindow(overlayWnd)) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            msgCount++;
            if (msgCount % 100 == 0) {
            }
            
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            Sleep(1);
        }
    }
    
    if (IsWindow(overlayWnd)) {
        DestroyWindow(overlayWnd);
    }
    
    if (g_overlayData.isCompleted && !g_overlayData.isCancelled) {
        *outRect = g_overlayData.selection;
        return true;
    } else {
        return false;
    }
}

/**
 * @brief 使用 ZXing-CPP 库扫描 HBITMAP (已修复 Stride 问题)
 */
bool ScanImageForQR(HBITMAP hBitmap, std::string& outErrorMsg) {
    
    try {
        BITMAP bmp;
        if (!GetObject(hBitmap, sizeof(BITMAP), &bmp)) {
            outErrorMsg = "无法获取位图信息";
            return false;
        }

        if (bmp.bmWidth <= 0 || bmp.bmHeight <= 0) {
            outErrorMsg = "位图尺寸无效";
            return false;
        }

        // 1. 计算正确的 Stride (行字节数)
        int bytesPerPixel = 3;
        int stride = ((bmp.bmWidth * bytesPerPixel + 3) / 4) * 4;

        BITMAPINFOHEADER bi = {0};
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = bmp.bmWidth;
        bi.biHeight = -bmp.bmHeight; // Top-down DIB
        bi.biPlanes = 1;
        bi.biBitCount = 24; // Request 24-bit RGB
        bi.biCompression = BI_RGB;

        HDC hdc = GetDC(NULL);
        if (!hdc) {
            outErrorMsg = "无法获取设备上下文 (GetDC)";
            return false;
        }
        
        // 2. 分配正确大小的缓冲区 (stride * height)
        std::vector<uint8_t> rgbBuffer(stride * bmp.bmHeight);
        int dibResult = GetDIBits(hdc, hBitmap, 0, bmp.bmHeight, rgbBuffer.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS);
        ReleaseDC(NULL, hdc);

        if (dibResult == 0) {
            outErrorMsg = "图像数据转换失败 (GetDIBits)";
            return false;
        }
        
        // 3. 配置 ZXing
        ZXing::DecodeHints hints;
        hints.setFormats(ZXing::BarcodeFormat::QRCode);
        hints.setTryHarder(true); // 启用更强的识别
        hints.setTryRotate(true); // 启用旋转识别

        
        // 4. 创建 ImageView, 关键：传入正确的 stride
        ZXing::ImageView imageView(rgbBuffer.data(), bmp.bmWidth, bmp.bmHeight, ZXing::ImageFormat::RGB, stride);
        
        // 5. 识别
        ZXing::Barcode result = ZXing::ReadBarcode(imageView, hints);

        if (result.isValid()) {
            std::string qrText = result.text(); // text() 返回 UTF-8
            
            CopyToClipboard(qrText); // CopyToClipboard 内部处理 UTF-8 到 UTF-16
            
            g_qr_result = qrText; // 主线程将使用这个
            PostMessage(g_hwnd, WM_APP_SHOW_RESULT, 1, 0); // 发送成功消息
            
            return true;
        } else {
            outErrorMsg = "未能在图像中识别到二维码。\n请确保截图清晰且完整。";
            return false;
        }
        
    } catch (const std::exception& e) {
        outErrorMsg = "识别异常: ";
        outErrorMsg += e.what();
        return false;
    } catch (...) {
        outErrorMsg = "识别过程中发生未知异常";
        return false;
    }
}


// --- 二维码生成功能 ---

// Wide (UTF-16) 字符串转 UTF-8 (std::string)
std::string WideToUTF8(const std::wstring& wideString) {
    if (wideString.empty()) {
        return std::string();
    }

    int size_needed = WideCharToMultiByte(CP_UTF8, 0, wideString.c_str(), (int)wideString.length(), NULL, 0, NULL, NULL);
    
    std::string utf8String(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, wideString.c_str(), (int)wideString.length(), &utf8String[0], size_needed, NULL, NULL);
    
    return utf8String;
}


void ShowQRGenerationWindow(HWND hwnd) {
    
    // --- 优化布局：增加窗口宽度以改善布局 ---
    int winWidth = 700;
    int winHeight = 650;
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenWidth - winWidth) / 2;
    int y = (screenHeight - winHeight) / 2;

    // 创建模态对话框 (使用 W (Unicode) 版本)
    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        WC_STATICW,
        L"二维码生成器",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, winWidth, winHeight,
        hwnd, NULL, g_hinstance, NULL
    );
    
    if (!hDlg) {
        MessageBoxW(hwnd, L"创建对话框失败", L"错误", MB_OK | MB_ICONERROR);
        return;
    }
    
    // 初始化数据
    g_qrGenData.hPreviewBitmap = NULL;
    g_qrGenData.pGdiplusBitmap = NULL;
    g_qrGenData.currentText = "";
    g_qrGenData.scale = 8;
    g_qrGenData.eccLevel = qrcodegen::QrCode::Ecc::MEDIUM;
    
    // 创建控件 (全部使用 W 版本) - 优化布局
    int margin = 15;
    int yPos = margin;
    
    // 文本输入标签
    CreateWindowExW(0, WC_STATICW, L"输入文本:", WS_CHILD | WS_VISIBLE,
        margin, yPos, 100, 20, hDlg, NULL, g_hinstance, NULL);
    yPos += 25;
    
    // 文本输入框 (多行) - 修复：添加 ES_WANTRETURN 支持换行
    HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, WC_EDITW, L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL,
        margin, yPos, 660, 100, hDlg, (HMENU)IDC_EDIT_TEXT, g_hinstance, NULL);
    yPos += 110;
    
    // 控制选项区域 - 优化布局
    CreateWindowExW(0, WC_STATICW, L"二维码尺寸:", WS_CHILD | WS_VISIBLE,
        margin, yPos, 100, 20, hDlg, NULL, g_hinstance, NULL);
    
    HWND hComboSize = CreateWindowExW(0, WC_COMBOBOXW, L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        120, yPos - 3, 120, 200, hDlg, (HMENU)IDC_COMBO_SIZE, g_hinstance, NULL);
    SendMessageA(hComboSize, CB_ADDSTRING, 0, (LPARAM)"小 (4x)"); 
    SendMessageA(hComboSize, CB_ADDSTRING, 0, (LPARAM)"中 (8x)");
    SendMessageA(hComboSize, CB_ADDSTRING, 0, (LPARAM)"大 (12x)");
    SendMessageA(hComboSize, CB_ADDSTRING, 0, (LPARAM)"超大 (16x)");
    SendMessageA(hComboSize, CB_SETCURSEL, 1, 0); // 默认选中 "中"
    
    // 纠错级别选择
    CreateWindowExW(0, WC_STATICW, L"纠错级别:", WS_CHILD | WS_VISIBLE,
        270, yPos, 100, 20, hDlg, NULL, g_hinstance, NULL);
    
    HWND hComboECC = CreateWindowExW(0, WC_COMBOBOXW, L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        360, yPos - 3, 120, 200, hDlg, (HMENU)IDC_COMBO_ECC, g_hinstance, NULL);
    SendMessageA(hComboECC, CB_ADDSTRING, 0, (LPARAM)"低 (L)");
    SendMessageA(hComboECC, CB_ADDSTRING, 0, (LPARAM)"中 (M)");
    SendMessageA(hComboECC, CB_ADDSTRING, 0, (LPARAM)"高 (Q)");
    SendMessageA(hComboECC, CB_ADDSTRING, 0, (LPARAM)"最高 (H)");
    SendMessageA(hComboECC, CB_SETCURSEL, 1, 0); // 默认选中 "中"
    
    // 生成按钮 - 移到右侧
    CreateWindowExW(0, WC_BUTTONW, L"生成预览",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        510, yPos - 3, 150, 30, hDlg, (HMENU)IDC_BTN_GENERATE, g_hinstance, NULL);
    yPos += 40;
    
    // 预览区域标签
    CreateWindowExW(0, WC_STATICW, L"预览 (自动生成):", WS_CHILD | WS_VISIBLE,
        margin, yPos, 150, 20, hDlg, NULL, g_hinstance, NULL);
    yPos += 25;
    
    // 预览区域 - 增大尺寸以适应不同大小的二维码
    HWND hPreview = CreateWindowExW(WS_EX_CLIENTEDGE, WC_STATICW, L"",
        WS_CHILD | WS_VISIBLE | SS_BITMAP | SS_CENTERIMAGE,
        margin, yPos, 400, 400, hDlg, (HMENU)IDC_STATIC_PREVIEW, g_hinstance, NULL);
    
    // 按钮区域 (右侧) - 优化布局
    int btnX = 435;
    int btnY = yPos;
    int btnWidth = 220;
    
    CreateWindowExW(0, WC_BUTTONW, L"保存为 PNG",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        btnX, btnY, btnWidth, 35, hDlg, (HMENU)IDC_BTN_SAVE_PNG, g_hinstance, NULL);
    btnY += 45;
    
    CreateWindowExW(0, WC_BUTTONW, L"保存为 JPG",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        btnX, btnY, btnWidth, 35, hDlg, (HMENU)IDC_BTN_SAVE_JPG, g_hinstance, NULL);
    btnY += 45;
    
    CreateWindowExW(0, WC_BUTTONW, L"复制到剪贴板",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        btnX, btnY, btnWidth, 35, hDlg, (HMENU)IDC_BTN_COPY, g_hinstance, NULL);
    
    // 设置窗口过程 (Subclassing)
    SetWindowLongPtrW(hDlg, GWLP_WNDPROC, (LONG_PTR)QRGenDialogProc);
    
    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_QUIT || !IsWindow(hDlg)) {
            break;
        }
        if (!IsDialogMessage(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}

// QR Generation Dialog Procedure
// 更改：必须是 LRESULT CALLBACK 并调用 DefWindowProcW 才能拖动
LRESULT CALLBACK QRGenDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CLOSE:
            // 清理资源
            if (g_qrGenData.hPreviewBitmap) {
                DeleteObject(g_qrGenData.hPreviewBitmap);
                g_qrGenData.hPreviewBitmap = NULL;
            }
            if (g_qrGenData.pGdiplusBitmap) {
                delete g_qrGenData.pGdiplusBitmap;
                g_qrGenData.pGdiplusBitmap = NULL;
            }
            DestroyWindow(hwndDlg);
            PostQuitMessage(0);
            return 0;
            
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_EDIT_TEXT:
                    // 修复：实现输入内容变化时自动生成
                    if (HIWORD(wParam) == EN_CHANGE) {
                        // 延迟自动生成，避免每次按键都生成
                        static UINT_PTR timerId = 0;
                        if (timerId) {
                            KillTimer(hwndDlg, timerId);
                        }
                        // 500ms 后自动生成
                        timerId = SetTimer(hwndDlg, 1, 500, NULL);
                    }
                    return 0;
                    
                case IDC_BTN_GENERATE:
                    UpdateQRPreview(hwndDlg);
                    return 0;
                    
                case IDC_BTN_SAVE_PNG:
                    SaveQRCodeImage(hwndDlg, true);
                    return 0;
                    
                case IDC_BTN_SAVE_JPG:
                    SaveQRCodeImage(hwndDlg, false);
                    return 0;
                    
                case IDC_BTN_COPY:
                    CopyQRToClipboard(hwndDlg);
                    return 0;
                    
                case IDC_COMBO_SIZE:
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        // 修复：尺寸改变时自动更新预览
                        if (!g_qrGenData.currentText.empty()) {
                            UpdateQRPreview(hwndDlg);
                        }
                    }
                    return 0;
                    
                case IDC_COMBO_ECC:
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        // 纠错级别改变时自动更新预览
                        if (!g_qrGenData.currentText.empty()) {
                            UpdateQRPreview(hwndDlg);
                        }
                    }
                    return 0;
            }
            break;
            
        case WM_TIMER:
            // 修复：定时器触发自动生成
            if (wParam == 1) {
                KillTimer(hwndDlg, 1);
                HWND hEdit = GetDlgItem(hwndDlg, IDC_EDIT_TEXT);
                int textLen = GetWindowTextLengthW(hEdit);
                if (textLen > 0) {
                    UpdateQRPreview(hwndDlg);
                }
            }
            return 0;
    }
    // 更改：调用 DefWindowProcW 以处理拖动等默认行为
    return DefWindowProcW(hwndDlg, uMsg, wParam, lParam);
}

// 更新二维码预览
void UpdateQRPreview(HWND hwndDlg) {
    try {
        // 获取输入文本 (Unicode 版本)
        HWND hEdit = GetDlgItem(hwndDlg, IDC_EDIT_TEXT);
        int textLen = GetWindowTextLengthW(hEdit);
        
        if (textLen == 0) {
            // 静默返回，不显示提示框（自动生成时）
            return;
        }
        
        std::vector<wchar_t> buffer(textLen + 1);
        GetWindowTextW(hEdit, buffer.data(), textLen + 1);
        std::wstring wideText(buffer.data());
        
        // 关键：将 UTF-16 转换为 UTF-8
        g_qrGenData.currentText = WideToUTF8(wideText); 
        
        // 获取纠错级别
        HWND hComboECC = GetDlgItem(hwndDlg, IDC_COMBO_ECC);
        int eccIdx = SendMessageA(hComboECC, CB_GETCURSEL, 0, 0);
        switch (eccIdx) {
            case 0: g_qrGenData.eccLevel = qrcodegen::QrCode::Ecc::LOW; break;
            case 1: g_qrGenData.eccLevel = qrcodegen::QrCode::Ecc::MEDIUM; break;
            case 2: g_qrGenData.eccLevel = qrcodegen::QrCode::Ecc::QUARTILE; break;
            case 3: g_qrGenData.eccLevel = qrcodegen::QrCode::Ecc::HIGH; break;
            default: g_qrGenData.eccLevel = qrcodegen::QrCode::Ecc::MEDIUM; break;
        }
        
        // 检查数据长度上限（根据纠错级别）
        size_t dataLen = g_qrGenData.currentText.length();
        int maxCapacity = 0;
        const wchar_t* eccName = L"";
        
        switch (eccIdx) {
            case 0: // LOW
                maxCapacity = 2953; // Version 40, Low ECC
                eccName = L"低 (L)";
                break;
            case 1: // MEDIUM
                maxCapacity = 2331; // Version 40, Medium ECC
                eccName = L"中 (M)";
                break;
            case 2: // QUARTILE
                maxCapacity = 1663; // Version 40, Quartile ECC
                eccName = L"高 (Q)";
                break;
            case 3: // HIGH
                maxCapacity = 1273; // Version 40, High ECC
                eccName = L"最高 (H)";
                break;
        }
        
        if (dataLen > (size_t)maxCapacity) {
            std::wstring msg = L"输入数据过长！\n\n";
            msg += L"当前长度: " + std::to_wstring(dataLen) + L" 字节\n";
            msg += L"纠错级别: " + std::wstring(eccName) + L"\n";
            msg += L"最大容量: " + std::to_wstring(maxCapacity) + L" 字节\n\n";
            msg += L"建议：\n";
            msg += L"1. 减少输入内容\n";
            msg += L"2. 降低纠错级别（低级别容量更大）";
            MessageBoxW(hwndDlg, msg.c_str(), L"数据超出限制", MB_OK | MB_ICONWARNING);
            return;
        }
        
        // 获取尺寸设置
        HWND hComboSize = GetDlgItem(hwndDlg, IDC_COMBO_SIZE);
        int sizeIdx = SendMessageA(hComboSize, CB_GETCURSEL, 0, 0);
        switch (sizeIdx) {
            case 0: g_qrGenData.scale = 4; break;
            case 1: g_qrGenData.scale = 8; break;
            case 2: g_qrGenData.scale = 12; break;
            case 3: g_qrGenData.scale = 16; break;
            default: g_qrGenData.scale = 8; break;
        }
        
        // 生成二维码
        qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(g_qrGenData.currentText.c_str(), g_qrGenData.eccLevel);
        int size = qr.getSize();
        int border = 4;
        int imageSize = (size + border * 2) * g_qrGenData.scale;
        
        // 清理旧的位图
        if (g_qrGenData.pGdiplusBitmap) {
            delete g_qrGenData.pGdiplusBitmap;
        }
        
        // 创建新的 GDI+ 位图（用于保存）
        g_qrGenData.pGdiplusBitmap = new Gdiplus::Bitmap(imageSize, imageSize, PixelFormat24bppRGB);
        Gdiplus::Graphics graphics(g_qrGenData.pGdiplusBitmap);
        graphics.Clear(Gdiplus::Color(255, 255, 255));
        
        Gdiplus::SolidBrush blackBrush(Gdiplus::Color(0, 0, 0));
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                if (qr.getModule(x, y)) {
                    int rectX = (x + border) * g_qrGenData.scale;
                    int rectY = (y + border) * g_qrGenData.scale;
                    graphics.FillRectangle(&blackBrush, rectX, rectY, g_qrGenData.scale, g_qrGenData.scale);
                }
            }
        }
        
        // 修复：创建固定尺寸的预览位图（400x400）
        const int previewSize = 380; // 预览固定尺寸
        Gdiplus::Bitmap previewBitmap(previewSize, previewSize, PixelFormat24bppRGB);
        Gdiplus::Graphics previewGraphics(&previewBitmap);
        previewGraphics.Clear(Gdiplus::Color(255, 255, 255));
        
        // 计算缩放比例以适应预览区域
        float scale = (float)previewSize / (float)imageSize;
        if (scale > 1.0f) scale = 1.0f; // 不放大，只缩小
        
        int scaledSize = (int)(imageSize * scale);
        int offsetX = (previewSize - scaledSize) / 2;
        int offsetY = (previewSize - scaledSize) / 2;
        
        // 使用高质量插值绘制缩放后的二维码
        previewGraphics.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
        previewGraphics.DrawImage(g_qrGenData.pGdiplusBitmap, 
            offsetX, offsetY, scaledSize, scaledSize);
        
        // 清理旧的预览位图
        if (g_qrGenData.hPreviewBitmap) {
            DeleteObject(g_qrGenData.hPreviewBitmap);
            g_qrGenData.hPreviewBitmap = NULL;
        }
        
        previewBitmap.GetHBITMAP(Gdiplus::Color(255, 255, 255), &g_qrGenData.hPreviewBitmap);
        
        // 更新预览控件
        HWND hPreview = GetDlgItem(hwndDlg, IDC_STATIC_PREVIEW);
        SendMessageA(hPreview, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)g_qrGenData.hPreviewBitmap);
        
        // 强制重绘预览区域
        InvalidateRect(hPreview, NULL, TRUE);
        UpdateWindow(hPreview);
        
    } catch (const std::exception& e) {
        std::string errorMsg = "生成二维码时发生错误: ";
        errorMsg += e.what();
        MessageBoxA(hwndDlg, errorMsg.c_str(), "错误", MB_OK | MB_ICONERROR);
    }
}

// 保存二维码图片
void SaveQRCodeImage(HWND hwndDlg, bool asPNG) {
    if (!g_qrGenData.pGdiplusBitmap) {
        MessageBoxW(hwndDlg, L"请先生成二维码", L"提示", MB_OK | MB_ICONINFORMATION); // 更改：使用 W
        return;
    }
    
    try {
        // 打开保存文件对话框 (Unicode 版本)
        wchar_t wFilename[MAX_PATH] = {0};
        OPENFILENAMEW ofn = {0}; // 更改：使用 W
        ofn.lStructSize = sizeof(OPENFILENAMEW);
        ofn.hwndOwner = hwndDlg;
        ofn.lpstrFilter = asPNG ? L"PNG 图片\0*.png\0所有文件\0*.*\0" : L"JPEG 图片\0*.jpg;*.jpeg\0所有文件\0*.*\0";
        ofn.lpstrFile = wFilename;
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrDefExt = asPNG ? L"png" : L"jpg";
        ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        
        // 生成默认文件名
        std::wstring defaultName = L"qrcode_" + std::to_wstring(GetTickCount()) + (asPNG ? L".png" : L".jpg");
        wcscpy_s(wFilename, defaultName.c_str());
        
        if (GetSaveFileNameW(&ofn)) { // 更改：使用 W
            CLSID encoderClsid;
            const WCHAR* mimeType = asPNG ? L"image/png" : L"image/jpeg";
            
            if (GetEncoderClsid(mimeType, &encoderClsid) >= 0) {
                
                if (g_qrGenData.pGdiplusBitmap->Save(wFilename, &encoderClsid) == Gdiplus::Ok) {
                    std::wstring msg = L"二维码已保存: " + std::wstring(wFilename) + L"\n是否打开它？";
                    if (MessageBoxW(hwndDlg, msg.c_str(), L"保存成功", MB_YESNO | MB_ICONINFORMATION) == IDYES) {
                        ShellExecuteW(NULL, L"open", wFilename, NULL, NULL, SW_SHOWNORMAL); // 更改：使用 W
                    }
                } else {
                    MessageBoxW(hwndDlg, L"保存文件失败", L"错误", MB_OK | MB_ICONERROR); // 更改：使用 W
                }
            } else {
                MessageBoxW(hwndDlg, L"获取图像编码器失败", L"错误", MB_OK | MB_ICONERROR); // 更改：使用 W
            }
        }
    } catch (const std::exception& e) {
        std::string errorMsg = "保存图片时发生错误: ";
        errorMsg += e.what();
        MessageBoxA(hwndDlg, errorMsg.c_str(), "错误", MB_OK | MB_ICONERROR);
    }
}

// 复制二维码到剪贴板
void CopyQRToClipboard(HWND hwndDlg) {
    if (!g_qrGenData.hPreviewBitmap) {
        MessageBoxW(hwndDlg, L"请先生成二维码", L"提示", MB_OK | MB_ICONINFORMATION); // 更改：使用 W
        return;
    }
    
    try {
        CopyBitmapToClipboard(g_qrGenData.hPreviewBitmap);
        MessageBoxW(hwndDlg, L"二维码图片已复制到剪贴板！", L"成功", MB_OK | MB_ICONINFORMATION); // 更改：使用 W
    } catch (const std::exception& e) {
        std::string errorMsg = "复制到剪贴板时发生错误: ";
        errorMsg += e.what();
        MessageBoxA(hwndDlg, errorMsg.c_str(), "错误", MB_OK | MB_ICONERROR);
    }
}

// --- 辅助函数 ---

// 复制 UTF-8 文本到剪贴板 (转换为 UTF-16)
void CopyToClipboard(const std::string& text) {
    
    if (!OpenClipboard(g_hwnd)) {
        return;
    }
    EmptyClipboard();
    
    // 转换 UTF-8 到 UTF-16 (Windows Unicode)
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
    if (wideLen == 0) {
        CloseClipboard();
        return;
    }
    
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, wideLen * sizeof(wchar_t));
    if (!hg) {
        CloseClipboard();
        return;
    }
    
    wchar_t* pchData = (wchar_t*)GlobalLock(hg);
    if (pchData) {
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, pchData, wideLen);
        GlobalUnlock(hg);
        
        if (!SetClipboardData(CF_UNICODETEXT, hg)) {
            GlobalFree(hg);
        } else {
        }
    } else {
        GlobalFree(hg);
    }
    
    CloseClipboard();
}

// 复制位图到剪贴板
void CopyBitmapToClipboard(HBITMAP hBitmap) {
    if (!hBitmap) return;
    
    if (!OpenClipboard(g_hwnd)) {
        return;
    }
    
    EmptyClipboard();
    
    // 获取位图信息
    BITMAP bmp;
    GetObject(hBitmap, sizeof(BITMAP), &bmp);
    
    // 创建 DIB
    BITMAPINFOHEADER bi = {0};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = bmp.bmWidth;
    bi.biHeight = bmp.bmHeight;
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;
    
    HDC hdc = GetDC(NULL);
    
    // 计算图像大小
    int imageSize = ((bmp.bmWidth * 3 + 3) & ~3) * bmp.bmHeight;
    
    // 分配全局内存
    HGLOBAL hDIB = GlobalAlloc(GMEM_MOVEABLE, sizeof(BITMAPINFOHEADER) + imageSize);
    if (hDIB) {
        void* pDIB = GlobalLock(hDIB);
        if (pDIB) {
            memcpy(pDIB, &bi, sizeof(BITMAPINFOHEADER));
            
            // 获取位图数据
            GetDIBits(hdc, hBitmap, 0, bmp.bmHeight, 
                     (BYTE*)pDIB + sizeof(BITMAPINFOHEADER), 
                     (BITMAPINFO*)pDIB, DIB_RGB_COLORS);
            
            GlobalUnlock(hDIB);
            
            // 设置到剪贴板
            if (!SetClipboardData(CF_DIB, hDIB)) {
                GlobalFree(hDIB);
            }
        } else {
            GlobalFree(hDIB);
        }
    }
    
    ReleaseDC(NULL, hdc);
    CloseClipboard();
}

// --- 屏幕截图函数 ---
HBITMAP CaptureScreen() {
    HDC hScreenDC = GetDC(NULL);
    HDC hMemDC = CreateCompatibleDC(hScreenDC);
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, screenWidth, screenHeight);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemDC, hBitmap);
    BitBlt(hMemDC, 0, 0, screenWidth, screenHeight, hScreenDC, 0, 0, SRCCOPY);
    SelectObject(hMemDC, hOldBitmap);
    DeleteDC(hMemDC);
    ReleaseDC(NULL, hScreenDC);
    return hBitmap;
}

HBITMAP CaptureScreenRegion(const RECT& rect) {
    HDC hScreenDC = GetDC(NULL);
    HDC hMemDC = CreateCompatibleDC(hScreenDC);
    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) {
        DeleteDC(hMemDC);
        ReleaseDC(NULL, hScreenDC);
        return NULL;
    }
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, width, height);
    HBITMAP hOldBitmap = (HBITMAP)SelectObject(hMemDC, hBitmap);
    BitBlt(hMemDC, 0, 0, width, height, hScreenDC, rect.left, rect.top, SRCCOPY);
    SelectObject(hMemDC, hOldBitmap);
    DeleteDC(hMemDC);
    ReleaseDC(NULL, hScreenDC);
    return hBitmap;
}

// GDI+ 辅助函数
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    
    Gdiplus::ImageCodecInfo* pImageCodecInfo = (Gdiplus::ImageCodecInfo*)(malloc(size));
    
    if (pImageCodecInfo == NULL) return -1;
    Gdiplus::GetImageEncoders(num, size, pImageCodecInfo);
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pImageCodecInfo[j].MimeType, format) == 0) {
            *pClsid = pImageCodecInfo[j].Clsid;
            free(pImageCodecInfo);
            return j;
        }
    }
    free(pImageCodecInfo);
    return -1;
}

// --- 快捷键配置函数 ---

// 获取键名
std::wstring GetKeyName(UINT vkCode) {
    switch (vkCode) {
        case VK_F1: return L"F1";
        case VK_F2: return L"F2";
        case VK_F3: return L"F3";
        case VK_F4: return L"F4";
        case VK_F5: return L"F5";
        case VK_F6: return L"F6";
        case VK_F7: return L"F7";
        case VK_F8: return L"F8";
        case VK_F9: return L"F9";
        case VK_F10: return L"F10";
        case VK_F11: return L"F11";
        case VK_F12: return L"F12";
        case 'A': case 'B': case 'C': case 'D': case 'E': case 'F': case 'G':
        case 'H': case 'I': case 'J': case 'K': case 'L': case 'M': case 'N':
        case 'O': case 'P': case 'Q': case 'R': case 'S': case 'T': case 'U':
        case 'V': case 'W': case 'X': case 'Y': case 'Z':
            return std::wstring(1, (wchar_t)vkCode);
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            return std::wstring(1, (wchar_t)vkCode);
        case VK_SPACE: return L"Space";
        case VK_RETURN: return L"Enter";
        case VK_TAB: return L"Tab";
        case VK_ESCAPE: return L"Esc";
        default: return L"Unknown";
    }
}

// 注册当前快捷键
bool RegisterCurrentHotkey(HWND hwnd) {
    UnregisterHotKey(hwnd, HOTKEY_ID);
    return RegisterHotKey(hwnd, HOTKEY_ID, g_hotkeyConfig.modifiers, g_hotkeyConfig.vkCode);
}

// 保存快捷键配置到文件
void SaveHotkeyConfig() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring configPath = exePath;
    size_t pos = configPath.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        configPath = configPath.substr(0, pos + 1);
    }
    configPath += L"config.ini";
    
    HANDLE hFile = CreateFileW(configPath.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        char buffer[512];
        sprintf_s(buffer, 
            "[Hotkeys]\n"
            "ScanModifiers=%u\n"
            "ScanKey=%u\n"
            "GenerateModifiers=%u\n"
            "GenerateKey=%u\n"
            "GenerateEnabled=%d\n"
            "\n"
            "[Settings]\n"
            "AutoStart=%d\n",
            g_hotkeyConfig.modifiers, g_hotkeyConfig.vkCode,
            g_hotkeyGenConfig.modifiers, g_hotkeyGenConfig.vkCode,
            g_hotkeyGenEnabled ? 1 : 0,
            g_autoStartEnabled ? 1 : 0);
        DWORD written;
        WriteFile(hFile, buffer, (DWORD)strlen(buffer), &written, NULL);
        CloseHandle(hFile);
    }
}

// 加载快捷键配置
void LoadHotkeyConfig() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring configPath = exePath;
    size_t pos = configPath.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        configPath = configPath.substr(0, pos + 1);
    }
    configPath += L"config.ini";
    
    HANDLE hFile = CreateFileW(configPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        char buffer[1024] = {0};
        DWORD read;
        if (ReadFile(hFile, buffer, sizeof(buffer) - 1, &read, NULL)) {
            // 解析 INI 格式
            char* line = strtok(buffer, "\n");
            while (line != NULL) {
                // 去除行尾的 \r
                size_t len = strlen(line);
                if (len > 0 && line[len - 1] == '\r') {
                    line[len - 1] = '\0';
                }
                
                if (strncmp(line, "ScanModifiers=", 14) == 0) {
                    g_hotkeyConfig.modifiers = atoi(line + 14);
                } else if (strncmp(line, "ScanKey=", 8) == 0) {
                    g_hotkeyConfig.vkCode = atoi(line + 8);
                } else if (strncmp(line, "GenerateModifiers=", 18) == 0) {
                    g_hotkeyGenConfig.modifiers = atoi(line + 18);
                } else if (strncmp(line, "GenerateKey=", 12) == 0) {
                    g_hotkeyGenConfig.vkCode = atoi(line + 12);
                } else if (strncmp(line, "GenerateEnabled=", 16) == 0) {
                    g_hotkeyGenEnabled = (atoi(line + 16) == 1);
                } else if (strncmp(line, "AutoStart=", 10) == 0) {
                    g_autoStartEnabled = (atoi(line + 10) == 1);
                }
                
                line = strtok(NULL, "\n");
            }
        }
        CloseHandle(hFile);
    }
}

// 显示设置窗口
void ShowSettingsWindow(HWND hwnd) {
    int winWidth = 400;
    int winHeight = 300;
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenWidth - winWidth) / 2;
    int y = (screenHeight - winHeight) / 2;

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        WC_STATICW,
        L"快捷键设置",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, winWidth, winHeight,
        hwnd, NULL, g_hinstance, NULL
    );
    
    if (!hDlg) {
        MessageBoxW(hwnd, L"创建设置对话框失败", L"错误", MB_OK | MB_ICONERROR);
        return;
    }
    
    int margin = 20;
    int yPos = margin;
    
    // 标题
    CreateWindowExW(0, WC_STATICW, L"设置截图识别快捷键:",
        WS_CHILD | WS_VISIBLE,
        margin, yPos, 360, 20, hDlg, NULL, g_hinstance, NULL);
    yPos += 35;
    
    // 修饰键复选框
    CreateWindowExW(0, WC_BUTTONW, L"Ctrl",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        margin, yPos, 80, 25, hDlg, (HMENU)IDC_HOTKEY_CTRL, g_hinstance, NULL);
    
    CreateWindowExW(0, WC_BUTTONW, L"Alt",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        margin + 90, yPos, 80, 25, hDlg, (HMENU)IDC_HOTKEY_ALT, g_hinstance, NULL);
    
    CreateWindowExW(0, WC_BUTTONW, L"Shift",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        margin + 180, yPos, 80, 25, hDlg, (HMENU)IDC_HOTKEY_SHIFT, g_hinstance, NULL);
    
    CreateWindowExW(0, WC_BUTTONW, L"Win",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        margin + 270, yPos, 80, 25, hDlg, (HMENU)IDC_HOTKEY_WIN, g_hinstance, NULL);
    yPos += 40;
    
    // 按键选择
    CreateWindowExW(0, WC_STATICW, L"按键:",
        WS_CHILD | WS_VISIBLE,
        margin, yPos, 60, 20, hDlg, NULL, g_hinstance, NULL);
    
    HWND hComboKey = CreateWindowExW(0, WC_COMBOBOXW, L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        margin + 70, yPos - 3, 280, 300, hDlg, (HMENU)IDC_COMBO_KEY, g_hinstance, NULL);
    
    // 添加常用按键
    const wchar_t* keys[] = {
        L"F1", L"F2", L"F3", L"F4", L"F5", L"F6", L"F7", L"F8", L"F9", L"F10", L"F11", L"F12",
        L"A", L"B", L"C", L"D", L"E", L"F", L"G", L"H", L"I", L"J", L"K", L"L", L"M",
        L"N", L"O", L"P", L"Q", L"R", L"S", L"T", L"U", L"V", L"W", L"X", L"Y", L"Z",
        L"0", L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9",
        L"Space", L"Enter", L"Tab"
    };
    
    for (const wchar_t* key : keys) {
        SendMessageW(hComboKey, CB_ADDSTRING, 0, (LPARAM)key);
    }
    
    // 设置当前值
    std::wstring currentKey = GetKeyName(g_hotkeyConfig.vkCode);
    int idx = (int)SendMessageW(hComboKey, CB_FINDSTRINGEXACT, -1, (LPARAM)currentKey.c_str());
    if (idx != CB_ERR) {
        SendMessageW(hComboKey, CB_SETCURSEL, idx, 0);
    }
    
    // 设置修饰键状态
    if (g_hotkeyConfig.modifiers & MOD_CONTROL)
        SendMessageW(GetDlgItem(hDlg, IDC_HOTKEY_CTRL), BM_SETCHECK, BST_CHECKED, 0);
    if (g_hotkeyConfig.modifiers & MOD_ALT)
        SendMessageW(GetDlgItem(hDlg, IDC_HOTKEY_ALT), BM_SETCHECK, BST_CHECKED, 0);
    if (g_hotkeyConfig.modifiers & MOD_SHIFT)
        SendMessageW(GetDlgItem(hDlg, IDC_HOTKEY_SHIFT), BM_SETCHECK, BST_CHECKED, 0);
    if (g_hotkeyConfig.modifiers & MOD_WIN)
        SendMessageW(GetDlgItem(hDlg, IDC_HOTKEY_WIN), BM_SETCHECK, BST_CHECKED, 0);
    
    yPos += 50;
    
    // 当前快捷键显示
    std::wstring currentHotkey = L"当前快捷键: ";
    if (g_hotkeyConfig.modifiers & MOD_CONTROL) currentHotkey += L"Ctrl + ";
    if (g_hotkeyConfig.modifiers & MOD_ALT) currentHotkey += L"Alt + ";
    if (g_hotkeyConfig.modifiers & MOD_SHIFT) currentHotkey += L"Shift + ";
    if (g_hotkeyConfig.modifiers & MOD_WIN) currentHotkey += L"Win + ";
    currentHotkey += GetKeyName(g_hotkeyConfig.vkCode);
    
    CreateWindowExW(0, WC_STATICW, currentHotkey.c_str(),
        WS_CHILD | WS_VISIBLE,
        margin, yPos, 360, 20, hDlg, (HMENU)9999, g_hinstance, NULL);
    yPos += 40;
    
    // 按钮
    CreateWindowExW(0, WC_BUTTONW, L"应用",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        margin, yPos, 100, 30, hDlg, (HMENU)IDC_BTN_APPLY, g_hinstance, NULL);
    
    CreateWindowExW(0, WC_BUTTONW, L"恢复默认",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        margin + 120, yPos, 100, 30, hDlg, (HMENU)IDC_BTN_RESET, g_hinstance, NULL);
    
    CreateWindowExW(0, WC_BUTTONW, L"关闭",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        margin + 240, yPos, 100, 30, hDlg, (HMENU)IDCANCEL, g_hinstance, NULL);
    
    SetWindowLongPtrW(hDlg, GWLP_WNDPROC, (LONG_PTR)SettingsDialogProc);
    
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_QUIT || !IsWindow(hDlg)) {
            break;
        }
        if (!IsDialogMessage(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}

// 设置对话框过程
LRESULT CALLBACK SettingsDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CLOSE:
            DestroyWindow(hwndDlg);
            PostQuitMessage(0);
            return 0;
            
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_BTN_APPLY: {
                    // 获取修饰键
                    UINT modifiers = 0;
                    if (SendMessageW(GetDlgItem(hwndDlg, IDC_HOTKEY_CTRL), BM_GETCHECK, 0, 0) == BST_CHECKED)
                        modifiers |= MOD_CONTROL;
                    if (SendMessageW(GetDlgItem(hwndDlg, IDC_HOTKEY_ALT), BM_GETCHECK, 0, 0) == BST_CHECKED)
                        modifiers |= MOD_ALT;
                    if (SendMessageW(GetDlgItem(hwndDlg, IDC_HOTKEY_SHIFT), BM_GETCHECK, 0, 0) == BST_CHECKED)
                        modifiers |= MOD_SHIFT;
                    if (SendMessageW(GetDlgItem(hwndDlg, IDC_HOTKEY_WIN), BM_GETCHECK, 0, 0) == BST_CHECKED)
                        modifiers |= MOD_WIN;
                    
                    // 获取按键
                    HWND hComboKey = GetDlgItem(hwndDlg, IDC_COMBO_KEY);
                    int idx = (int)SendMessageW(hComboKey, CB_GETCURSEL, 0, 0);
                    if (idx == CB_ERR) {
                        MessageBoxW(hwndDlg, L"请选择一个按键", L"提示", MB_OK | MB_ICONWARNING);
                        return 0;
                    }
                    
                    wchar_t keyText[32];
                    SendMessageW(hComboKey, CB_GETLBTEXT, idx, (LPARAM)keyText);
                    
                    // 转换按键名称为虚拟键码
                    UINT vkCode = 0;
                    if (wcslen(keyText) == 1 && ((keyText[0] >= 'A' && keyText[0] <= 'Z') || (keyText[0] >= '0' && keyText[0] <= '9'))) {
                        vkCode = keyText[0];
                    } else if (wcscmp(keyText, L"Space") == 0) {
                        vkCode = VK_SPACE;
                    } else if (wcscmp(keyText, L"Enter") == 0) {
                        vkCode = VK_RETURN;
                    } else if (wcscmp(keyText, L"Tab") == 0) {
                        vkCode = VK_TAB;
                    } else if (keyText[0] == 'F' && wcslen(keyText) >= 2) {
                        int fNum = _wtoi(keyText + 1);
                        if (fNum >= 1 && fNum <= 12) {
                            vkCode = VK_F1 + fNum - 1;
                        }
                    }
                    
                    if (vkCode == 0) {
                        MessageBoxW(hwndDlg, L"无效的按键", L"错误", MB_OK | MB_ICONERROR);
                        return 0;
                    }
                    
                    // 尝试注册新快捷键
                    HotkeyConfig oldConfig = g_hotkeyConfig;
                    g_hotkeyConfig.modifiers = modifiers;
                    g_hotkeyConfig.vkCode = vkCode;
                    
                    if (RegisterCurrentHotkey(g_hwnd)) {
                        SaveHotkeyConfig();
                        
                        std::wstring msg = L"快捷键已更新为: ";
                        if (modifiers & MOD_CONTROL) msg += L"Ctrl + ";
                        if (modifiers & MOD_ALT) msg += L"Alt + ";
                        if (modifiers & MOD_SHIFT) msg += L"Shift + ";
                        if (modifiers & MOD_WIN) msg += L"Win + ";
                        msg += GetKeyName(vkCode);
                        
                        MessageBoxW(hwndDlg, msg.c_str(), L"成功", MB_OK | MB_ICONINFORMATION);
                        
                        // 更新显示
                        SetWindowTextW(GetDlgItem(hwndDlg, 9999), (L"当前快捷键: " + msg.substr(11)).c_str());
                    } else {
                        g_hotkeyConfig = oldConfig;
                        RegisterCurrentHotkey(g_hwnd);
                        MessageBoxW(hwndDlg, L"注册快捷键失败!\n该快捷键可能已被其他程序占用。", L"错误", MB_OK | MB_ICONERROR);
                    }
                    return 0;
                }
                
                case IDC_BTN_RESET: {
                    g_hotkeyConfig.modifiers = MOD_CONTROL | MOD_ALT;
                    g_hotkeyConfig.vkCode = 'Q';
                    
                    if (RegisterCurrentHotkey(g_hwnd)) {
                        SaveHotkeyConfig();
                        
                        // 更新界面
                        SendMessageW(GetDlgItem(hwndDlg, IDC_HOTKEY_CTRL), BM_SETCHECK, BST_CHECKED, 0);
                        SendMessageW(GetDlgItem(hwndDlg, IDC_HOTKEY_ALT), BM_SETCHECK, BST_CHECKED, 0);
                        SendMessageW(GetDlgItem(hwndDlg, IDC_HOTKEY_SHIFT), BM_SETCHECK, BST_UNCHECKED, 0);
                        SendMessageW(GetDlgItem(hwndDlg, IDC_HOTKEY_WIN), BM_SETCHECK, BST_UNCHECKED, 0);
                        
                        HWND hComboKey = GetDlgItem(hwndDlg, IDC_COMBO_KEY);
                        int idx = (int)SendMessageW(hComboKey, CB_FINDSTRINGEXACT, -1, (LPARAM)L"Q");
                        if (idx != CB_ERR) {
                            SendMessageW(hComboKey, CB_SETCURSEL, idx, 0);
                        }
                        
                        SetWindowTextW(GetDlgItem(hwndDlg, 9999), L"当前快捷键: Ctrl + Alt + Q");
                        MessageBoxW(hwndDlg, L"已恢复默认快捷键: Ctrl+Alt+Q", L"成功", MB_OK | MB_ICONINFORMATION);
                    }
                    return 0;
                }
                
                case IDCANCEL:
                    PostMessage(hwndDlg, WM_CLOSE, 0, 0);
                    return 0;
            }
            break;
    }
    return DefWindowProcW(hwndDlg, uMsg, wParam, lParam);
}
// --- 生成快捷键注册函数 ---
bool RegisterGenerateHotkey(HWND hwnd) {
    UnregisterHotKey(hwnd, HOTKEY_GEN_ID);
    if (g_hotkeyGenEnabled) {
        return RegisterHotKey(hwnd, HOTKEY_GEN_ID, g_hotkeyGenConfig.modifiers, g_hotkeyGenConfig.vkCode);
    }
    return true;
}

// --- 开机自启函数 ---
bool SetAutoStart(bool enable) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    
    HKEY hKey;
    const wchar_t* keyPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* valueName = L"QRTrayApp";
    
    if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS) {
        if (enable) {
            RegSetValueExW(hKey, valueName, 0, REG_SZ, (BYTE*)exePath, (DWORD)((wcslen(exePath) + 1) * sizeof(wchar_t)));
        } else {
            RegDeleteValueW(hKey, valueName);
        }
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

bool IsAutoStartEnabled() {
    HKEY hKey;
    const wchar_t* keyPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    const wchar_t* valueName = L"QRTrayApp";
    
    if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t value[MAX_PATH];
        DWORD size = sizeof(value);
        DWORD type;
        
        if (RegQueryValueExW(hKey, valueName, NULL, &type, (BYTE*)value, &size) == ERROR_SUCCESS) {
            RegCloseKey(hKey);
            return true;
        }
        RegCloseKey(hKey);
    }
    return false;
}

void SaveAutoStartConfig() {
    // 现在使用统一的配置文件
    SaveHotkeyConfig();
}

void LoadAutoStartConfig() {
    // 配置已在 LoadHotkeyConfig 中加载
    // 如果配置文件不存在，检查注册表
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring configPath = exePath;
    size_t pos = configPath.find_last_of(L"\\/");
    if (pos != std::wstring::npos) {
        configPath = configPath.substr(0, pos + 1);
    }
    configPath += L"config.ini";
    
    HANDLE hFile = CreateFileW(configPath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        // 如果配置文件不存在，检查注册表
        g_autoStartEnabled = IsAutoStartEnabled();
    } else {
        CloseHandle(hFile);
    }
}

// --- 扫码快捷键设置窗口 ---
void ShowScanHotkeySettings(HWND hwnd) {
    ShowSettingsWindow(hwnd);
}

// --- 生成快捷键对话框窗口过程 ---
LRESULT CALLBACK GenerateHotkeyDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CLOSE:
            DestroyWindow(hwndDlg);
            PostQuitMessage(0);
            return 0;
            
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
                case IDC_CHECK_GEN_ENABLE:
                    if (HIWORD(wParam) == BN_CLICKED) {
                        HWND hCheck = GetDlgItem(hwndDlg, IDC_CHECK_GEN_ENABLE);
                        bool enabled = (SendMessageW(hCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
                        
                        // 更新状态显示
                        std::wstring statusText = L"当前快捷键: ";
                        if (enabled) {
                            if (g_hotkeyGenConfig.modifiers & MOD_CONTROL) statusText += L"Ctrl + ";
                            if (g_hotkeyGenConfig.modifiers & MOD_ALT) statusText += L"Alt + ";
                            if (g_hotkeyGenConfig.modifiers & MOD_SHIFT) statusText += L"Shift + ";
                            if (g_hotkeyGenConfig.modifiers & MOD_WIN) statusText += L"Win + ";
                            statusText += GetKeyName(g_hotkeyGenConfig.vkCode);
                        } else {
                            statusText += L"未启用";
                        }
                        SetWindowTextW(GetDlgItem(hwndDlg, 9998), statusText.c_str());
                    }
                    return 0;
                    
                case IDC_BTN_GEN_APPLY: {
                    // 获取启用状态
                    HWND hCheck = GetDlgItem(hwndDlg, IDC_CHECK_GEN_ENABLE);
                    bool enabled = (SendMessageW(hCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
                    
                    // 获取修饰键
                    UINT modifiers = 0;
                    if (SendMessageW(GetDlgItem(hwndDlg, IDC_HOTKEY_GEN_CTRL), BM_GETCHECK, 0, 0) == BST_CHECKED)
                        modifiers |= MOD_CONTROL;
                    if (SendMessageW(GetDlgItem(hwndDlg, IDC_HOTKEY_GEN_ALT), BM_GETCHECK, 0, 0) == BST_CHECKED)
                        modifiers |= MOD_ALT;
                    if (SendMessageW(GetDlgItem(hwndDlg, IDC_HOTKEY_GEN_SHIFT), BM_GETCHECK, 0, 0) == BST_CHECKED)
                        modifiers |= MOD_SHIFT;
                    if (SendMessageW(GetDlgItem(hwndDlg, IDC_HOTKEY_GEN_WIN), BM_GETCHECK, 0, 0) == BST_CHECKED)
                        modifiers |= MOD_WIN;
                    
                    // 获取按键
                    HWND hComboKey = GetDlgItem(hwndDlg, IDC_COMBO_GEN_KEY);
                    int idx = (int)SendMessageW(hComboKey, CB_GETCURSEL, 0, 0);
                    if (idx == CB_ERR) {
                        MessageBoxW(hwndDlg, L"请选择一个按键", L"提示", MB_OK | MB_ICONWARNING);
                        return 0;
                    }
                    
                    wchar_t keyText[32];
                    SendMessageW(hComboKey, CB_GETLBTEXT, idx, (LPARAM)keyText);
                    
                    // 转换按键名称为虚拟键码
                    UINT vkCode = 0;
                    if (wcslen(keyText) == 1 && ((keyText[0] >= 'A' && keyText[0] <= 'Z') || (keyText[0] >= '0' && keyText[0] <= '9'))) {
                        vkCode = keyText[0];
                    } else if (keyText[0] == 'F' && wcslen(keyText) >= 2) {
                        int fNum = _wtoi(keyText + 1);
                        if (fNum >= 1 && fNum <= 12) {
                            vkCode = VK_F1 + fNum - 1;
                        }
                    }
                    
                    if (vkCode == 0) {
                        MessageBoxW(hwndDlg, L"无效的按键", L"错误", MB_OK | MB_ICONERROR);
                        return 0;
                    }
                    
                    // 保存旧配置
                    HotkeyConfig oldConfig = g_hotkeyGenConfig;
                    bool oldEnabled = g_hotkeyGenEnabled;
                    
                    g_hotkeyGenConfig.modifiers = modifiers;
                    g_hotkeyGenConfig.vkCode = vkCode;
                    g_hotkeyGenEnabled = enabled;
                    
                    if (RegisterGenerateHotkey(g_hwnd)) {
                        SaveHotkeyConfig();
                        
                        std::wstring msg = L"快捷键已更新！\n";
                        if (enabled) {
                            msg += L"新快捷键: ";
                            if (modifiers & MOD_CONTROL) msg += L"Ctrl + ";
                            if (modifiers & MOD_ALT) msg += L"Alt + ";
                            if (modifiers & MOD_SHIFT) msg += L"Shift + ";
                            if (modifiers & MOD_WIN) msg += L"Win + ";
                            msg += GetKeyName(vkCode);
                        } else {
                            msg += L"快捷键已禁用";
                        }
                        
                        MessageBoxW(hwndDlg, msg.c_str(), L"成功", MB_OK | MB_ICONINFORMATION);
                        
                        // 更新显示
                        std::wstring statusText = L"当前快捷键: ";
                        if (enabled) {
                            if (modifiers & MOD_CONTROL) statusText += L"Ctrl + ";
                            if (modifiers & MOD_ALT) statusText += L"Alt + ";
                            if (modifiers & MOD_SHIFT) statusText += L"Shift + ";
                            if (modifiers & MOD_WIN) statusText += L"Win + ";
                            statusText += GetKeyName(vkCode);
                        } else {
                            statusText += L"未启用";
                        }
                        SetWindowTextW(GetDlgItem(hwndDlg, 9998), statusText.c_str());
                    } else {
                        g_hotkeyGenConfig = oldConfig;
                        g_hotkeyGenEnabled = oldEnabled;
                        RegisterGenerateHotkey(g_hwnd);
                        MessageBoxW(hwndDlg, L"注册快捷键失败!\n该快捷键可能已被其他程序占用。", L"错误", MB_OK | MB_ICONERROR);
                    }
                    return 0;
                }
                
                case IDC_BTN_GEN_RESET: {
                    g_hotkeyGenConfig.modifiers = MOD_CONTROL;
                    g_hotkeyGenConfig.vkCode = 'Q';
                    g_hotkeyGenEnabled = false;
                    
                    if (RegisterGenerateHotkey(g_hwnd)) {
                        SaveHotkeyConfig();
                        
                        // 更新界面
                        SendMessageW(GetDlgItem(hwndDlg, IDC_CHECK_GEN_ENABLE), BM_SETCHECK, BST_UNCHECKED, 0);
                        SendMessageW(GetDlgItem(hwndDlg, IDC_HOTKEY_GEN_CTRL), BM_SETCHECK, BST_CHECKED, 0);
                        SendMessageW(GetDlgItem(hwndDlg, IDC_HOTKEY_GEN_ALT), BM_SETCHECK, BST_UNCHECKED, 0);
                        SendMessageW(GetDlgItem(hwndDlg, IDC_HOTKEY_GEN_SHIFT), BM_SETCHECK, BST_UNCHECKED, 0);
                        SendMessageW(GetDlgItem(hwndDlg, IDC_HOTKEY_GEN_WIN), BM_SETCHECK, BST_UNCHECKED, 0);
                        
                        HWND hComboKey = GetDlgItem(hwndDlg, IDC_COMBO_GEN_KEY);
                        int idx = (int)SendMessageW(hComboKey, CB_FINDSTRINGEXACT, -1, (LPARAM)L"Q");
                        if (idx != CB_ERR) {
                            SendMessageW(hComboKey, CB_SETCURSEL, idx, 0);
                        }
                        
                        SetWindowTextW(GetDlgItem(hwndDlg, 9998), L"当前快捷键: 未启用");
                        MessageBoxW(hwndDlg, L"已恢复默认设置: Ctrl+Q (未启用)", L"成功", MB_OK | MB_ICONINFORMATION);
                    }
                    return 0;
                }
                
                case IDCANCEL:
                    PostMessage(hwndDlg, WM_CLOSE, 0, 0);
                    return 0;
            }
            break;
    }
    return DefWindowProcW(hwndDlg, uMsg, wParam, lParam);
}

// --- 生成快捷键设置窗口 ---
void ShowGenerateHotkeySettings(HWND hwnd) {
    int winWidth = 450;
    int winHeight = 350;
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    int x = (screenWidth - winWidth) / 2;
    int y = (screenHeight - winHeight) / 2;

    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        WC_STATICW,
        L"生成二维码快捷键设置",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, winWidth, winHeight,
        hwnd, NULL, g_hinstance, NULL
    );
    
    if (!hDlg) {
        MessageBoxW(hwnd, L"创建设置对话框失败", L"错误", MB_OK | MB_ICONERROR);
        return;
    }
    
    int margin = 20;
    int yPos = margin;
    
    // 启用复选框
    HWND hCheckEnable = CreateWindowExW(0, WC_BUTTONW, L"启用生成二维码快捷键",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        margin, yPos, 400, 25, hDlg, (HMENU)IDC_CHECK_GEN_ENABLE, g_hinstance, NULL);
    
    if (g_hotkeyGenEnabled) {
        SendMessageW(hCheckEnable, BM_SETCHECK, BST_CHECKED, 0);
    }
    yPos += 40;
    
    // 标题
    CreateWindowExW(0, WC_STATICW, L"设置快捷键组合:",
        WS_CHILD | WS_VISIBLE,
        margin, yPos, 360, 20, hDlg, NULL, g_hinstance, NULL);
    yPos += 35;
    
    // 修饰键复选框
    CreateWindowExW(0, WC_BUTTONW, L"Ctrl",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        margin, yPos, 80, 25, hDlg, (HMENU)IDC_HOTKEY_GEN_CTRL, g_hinstance, NULL);
    
    CreateWindowExW(0, WC_BUTTONW, L"Alt",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        margin + 100, yPos, 80, 25, hDlg, (HMENU)IDC_HOTKEY_GEN_ALT, g_hinstance, NULL);
    
    CreateWindowExW(0, WC_BUTTONW, L"Shift",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        margin + 200, yPos, 80, 25, hDlg, (HMENU)IDC_HOTKEY_GEN_SHIFT, g_hinstance, NULL);
    
    CreateWindowExW(0, WC_BUTTONW, L"Win",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        margin + 300, yPos, 80, 25, hDlg, (HMENU)IDC_HOTKEY_GEN_WIN, g_hinstance, NULL);
    yPos += 40;
    
    // 按键选择
    CreateWindowExW(0, WC_STATICW, L"按键:",
        WS_CHILD | WS_VISIBLE,
        margin, yPos, 60, 20, hDlg, NULL, g_hinstance, NULL);
    
    HWND hComboKey = CreateWindowExW(0, WC_COMBOBOXW, L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        margin + 70, yPos - 3, 310, 300, hDlg, (HMENU)IDC_COMBO_GEN_KEY, g_hinstance, NULL);
    
    // 添加常用按键
    const wchar_t* keys[] = {
        L"F1", L"F2", L"F3", L"F4", L"F5", L"F6", L"F7", L"F8", L"F9", L"F10", L"F11", L"F12",
        L"A", L"B", L"C", L"D", L"E", L"F", L"G", L"H", L"I", L"J", L"K", L"L", L"M",
        L"N", L"O", L"P", L"Q", L"R", L"S", L"T", L"U", L"V", L"W", L"X", L"Y", L"Z",
        L"0", L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9"
    };
    
    for (const wchar_t* key : keys) {
        SendMessageW(hComboKey, CB_ADDSTRING, 0, (LPARAM)key);
    }
    
    // 设置当前值
    std::wstring currentKey = GetKeyName(g_hotkeyGenConfig.vkCode);
    int idx = (int)SendMessageW(hComboKey, CB_FINDSTRINGEXACT, -1, (LPARAM)currentKey.c_str());
    if (idx != CB_ERR) {
        SendMessageW(hComboKey, CB_SETCURSEL, idx, 0);
    }
    
    // 设置修饰键状态
    if (g_hotkeyGenConfig.modifiers & MOD_CONTROL)
        SendMessageW(GetDlgItem(hDlg, IDC_HOTKEY_GEN_CTRL), BM_SETCHECK, BST_CHECKED, 0);
    if (g_hotkeyGenConfig.modifiers & MOD_ALT)
        SendMessageW(GetDlgItem(hDlg, IDC_HOTKEY_GEN_ALT), BM_SETCHECK, BST_CHECKED, 0);
    if (g_hotkeyGenConfig.modifiers & MOD_SHIFT)
        SendMessageW(GetDlgItem(hDlg, IDC_HOTKEY_GEN_SHIFT), BM_SETCHECK, BST_CHECKED, 0);
    if (g_hotkeyGenConfig.modifiers & MOD_WIN)
        SendMessageW(GetDlgItem(hDlg, IDC_HOTKEY_GEN_WIN), BM_SETCHECK, BST_CHECKED, 0);
    
    yPos += 50;
    
    // 当前快捷键显示
    std::wstring currentHotkey = L"当前快捷键: ";
    if (g_hotkeyGenEnabled) {
        if (g_hotkeyGenConfig.modifiers & MOD_CONTROL) currentHotkey += L"Ctrl + ";
        if (g_hotkeyGenConfig.modifiers & MOD_ALT) currentHotkey += L"Alt + ";
        if (g_hotkeyGenConfig.modifiers & MOD_SHIFT) currentHotkey += L"Shift + ";
        if (g_hotkeyGenConfig.modifiers & MOD_WIN) currentHotkey += L"Win + ";
        currentHotkey += GetKeyName(g_hotkeyGenConfig.vkCode);
    } else {
        currentHotkey += L"未启用";
    }
    
    CreateWindowExW(0, WC_STATICW, currentHotkey.c_str(),
        WS_CHILD | WS_VISIBLE,
        margin, yPos, 400, 20, hDlg, (HMENU)9998, g_hinstance, NULL);
    yPos += 40;
    
    // 按钮
    CreateWindowExW(0, WC_BUTTONW, L"应用",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        margin, yPos, 100, 30, hDlg, (HMENU)IDC_BTN_GEN_APPLY, g_hinstance, NULL);
    
    CreateWindowExW(0, WC_BUTTONW, L"恢复默认",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        margin + 120, yPos, 100, 30, hDlg, (HMENU)IDC_BTN_GEN_RESET, g_hinstance, NULL);
    
    CreateWindowExW(0, WC_BUTTONW, L"关闭",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        margin + 240, yPos, 100, 30, hDlg, (HMENU)IDCANCEL, g_hinstance, NULL);
    
    // 设置窗口过程
    SetWindowLongPtrW(hDlg, GWLP_WNDPROC, (LONG_PTR)GenerateHotkeyDialogProc);
    
    // 消息循环
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_QUIT || !IsWindow(hDlg)) {
            break;
        }
        if (!IsDialogMessage(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}
