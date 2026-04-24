// DaveSaveEd.cpp
//
// Copyright (c) 2025 FNGarvin (184324400+FNGarvin@users.noreply.github.com)
// All rights reserved.
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
//
// Disclaimer: This project and its creators are not affiliated with Mintrocket, Nexon,
// or any other entities associated with the game "Dave the Diver." This is an independent
// fan-made tool.
//
// This project uses third-party libraries under their respective licenses:
// - zlib (Zlib License)
// - nlohmann/json (MIT License)
// - SQLite (Public Domain)
// Full license texts can be found in the /dist/zlib, /dist/nlohmann_json, and /dist/sqlite3 directories.
//
#include <windows.h>    // Basic Windows API functions
#include <windowsx.h>   // For GET_WM_COMMAND_ID macro
#include <strsafe.h>    // For StringCbCopyA (safe string copy for font face name)
#include <string>       // For std::string
#include <filesystem>   // For std::filesystem (C++17 for path manipulation) - Kept for std::filesystem::path
#include <string.h>     // For strstr (for parsing command-line arguments)
#include <vector>       // For std::vector (used in zlib decompression)
#include <cctype>       // For std::tolower (used in ingredient search filter)

// Include SQLite3 header
#include "sqlite3.h"

// zlib header for decompression
#include "zlib.h"

// Include the generated embedded SQL string (our compressed reference DB)
#include "embedded_sql.h" // Contains compressed binary SQL data for the reference database.

// Project-specific headers
#include "DaveSaveEd.h"     // Application-wide globals and common definitions.
#include "Logger.h"         // Logging functionality.
#include "SaveGameManager.h" // Manages game save file operations.
#include "resource.h" //icon ID

// --- Global Constants and Control IDs for the Dialog UI ---
#define IDC_MAIN_DIALOG             100

// Control IDs for currency-related UI elements.
#define IDC_STATIC_GOLD_LABEL       101
#define IDC_EDIT_GOLD_VALUE         102
#define IDC_BTN_SET_GOLD            103

#define IDC_STATIC_BEI_LABEL        104
#define IDC_EDIT_BEI_VALUE          105
#define IDC_BTN_SET_BEI             106

#define IDC_STATIC_FLAME_LABEL      107
#define IDC_EDIT_FLAME_VALUE        108
#define IDC_BTN_SET_FLAME           109

#define IDC_STATIC_FOLLOWER_LABEL   114
#define IDC_EDIT_FOLLOWER_VALUE     115
#define IDC_BTN_SET_FOLLOWER        116

// Control IDs for file operation UI elements.
#define IDC_BTN_LOAD_SAVE           112
#define IDC_BTN_WRITE_SAVE          113

#define IDC_COMBO_INGREDIENTS       117
#define IDC_EDIT_ING_AMOUNT         118
#define IDC_BTN_SET_ING_AMOUNT      119
#define IDC_EDIT_ING_SEARCH         120

// --- Global Window Handles ---
HWND g_hDlg = NULL; // Handle to the main dialog window.

HWND g_hComboIngredients = NULL;
HWND g_hEditIngAmount = NULL;
HWND g_hEditIngSearch = NULL;

// Full ingredient list cached at startup for fast combo filtering.
// Each entry is { ingredientID, "ID - TextID" display string }.
std::vector<std::pair<int, std::string>> g_ingredientList;

// Handles to the edit controls that display and accept currency values.
HWND g_hEditGoldValue = NULL;
HWND g_hEditBeiValue = NULL;
HWND g_hEditFlameValue = NULL;
HWND g_hEditFollowerValue = NULL;

// --- Global SQLite Database Handle (for embedded reference DB) ---
// This database stores reference data (e.g., ingredient lists) for the editor.
sqlite3* g_refDb = NULL;

// --- Global Save Game Manager instance ---
// Manages all interactions with the game's save files.
SaveGameManager g_saveGameManager;

// Global brush for painting the dialog background color.
HBRUSH g_hBackgroundBrush = NULL;

// Global fonts — created once in WM_CREATE, destroyed in WM_DESTROY.
HFONT g_hFont        = NULL; // Standard UI font (Segoe UI 9pt)
HFONT g_hFontBold    = NULL; // Bold variant for group-box section headers
HFONT g_hFontSmall   = NULL; // Small font for status bar

// Status bar at the bottom of the window.
HWND g_hStatusBar = NULL;

// Write Save button — kept globally so it can be enabled/disabled with the save state.
HWND g_hBtnWriteSave = NULL;
HWND g_hBtnLoadSave  = NULL;

// Group box handles (needed by DoLayout for repositioning).
HWND g_hGbResources   = NULL;
HWND g_hGbIngredients = NULL;

// Label handles (needed by DoLayout).
HWND g_hLblGold     = NULL;
HWND g_hLblBei      = NULL;
HWND g_hLblFlame    = NULL;
HWND g_hLblFollower = NULL;

// --- Forward Declarations ---
INT_PTR CALLBACK DialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
void UpdateCurrencyDisplay();
void DoLayout(HWND hDlg, int W, int H);

// Helper: apply a font to a single child window (used with EnumChildWindows).
static BOOL CALLBACK SetChildFont(HWND hChild, LPARAM lParam) {
    SendMessage(hChild, WM_SETFONT, (WPARAM)lParam, TRUE);
    return TRUE;
}

// --- UpdateCurrencyDisplay ---
// Pulls values from SaveGameManager into the edit controls and
// enables/disables all editable fields + the Write button accordingly.
void UpdateCurrencyDisplay() {
    bool loaded = g_saveGameManager.IsSaveFileLoaded();

    if (loaded) {
        SetWindowTextA(g_hEditGoldValue,     std::to_string(g_saveGameManager.GetGold()).c_str());
        SetWindowTextA(g_hEditBeiValue,      std::to_string(g_saveGameManager.GetBei()).c_str());
        SetWindowTextA(g_hEditFlameValue,    std::to_string(g_saveGameManager.GetArtisansFlame()).c_str());
        SetWindowTextA(g_hEditFollowerValue, std::to_string(g_saveGameManager.GetFollowerCount()).c_str());
        // Show short filename in status bar.
        std::string path = g_saveGameManager.GetCurrentFilePath();
        size_t slash = path.find_last_of("\\/");
        std::string name = (slash != std::string::npos) ? path.substr(slash + 1) : path;
        std::string status = "  Loaded: " + name;
        if (g_hStatusBar) SetWindowTextA(g_hStatusBar, status.c_str());
        LogMessage(LOG_INFO_LEVEL, "Currency display updated from SaveGameManager values.");
    } else {
        SetWindowTextA(g_hEditGoldValue,     "");
        SetWindowTextA(g_hEditBeiValue,      "");
        SetWindowTextA(g_hEditFlameValue,    "");
        SetWindowTextA(g_hEditFollowerValue, "");
        if (g_hStatusBar) SetWindowTextA(g_hStatusBar, "  No save file loaded.");
        LogMessage(LOG_INFO_LEVEL, "No valid save data loaded. Displaying blank currency values.");
    }

    EnableWindow(g_hEditGoldValue,     loaded ? TRUE : FALSE);
    EnableWindow(g_hEditBeiValue,      loaded ? TRUE : FALSE);
    EnableWindow(g_hEditFlameValue,    loaded ? TRUE : FALSE);
    EnableWindow(g_hEditFollowerValue, loaded ? TRUE : FALSE);
    if (g_hBtnWriteSave) EnableWindow(g_hBtnWriteSave, loaded ? TRUE : FALSE);
}

// --- DoLayout ---
// Repositions and resizes all child controls to fit the given client area (W x H).
// Called from WM_CREATE (initial pass) and WM_SIZE (every resize) for full responsiveness.
void DoLayout(HWND hDlg, int W, int H) {
    const int MARGIN      = 16;       // outer left/right margin
    const int PAD         = 12;       // inner group-box padding
    const int ROW_H       = 26;       // control height
    const int ROW_GAP     = 8;        // vertical gap between rows
    const int SEC_GAP     = 14;       // vertical gap between sections
    const int LBL_W       = 118;      // label column width (left-anchored)
    const int EDIT_W      = 110;      // value edit width (right-anchored)
    const int BTN_W       = 64;       // "Set" button width (right-anchored)
    const int SBAR_H      = 22;       // status bar height
    const int FILE_BTN_H  = ROW_H + 4;
    const int ING_BTN_W   = 64;
    const int ING_EDIT_W  = 70;
    const int ING_GAP     = 8;

    // Horizontal anchors — group box spans full width minus margins.
    int GB_W        = W - MARGIN * 2;
    int GB_INNER_X  = MARGIN + PAD;

    // Resources section: edit + button right-anchored inside group box.
    int GB_BTN_X    = MARGIN + GB_W - PAD - BTN_W;
    int GB_EDIT_X   = GB_BTN_X - 8 - EDIT_W;

    // Ingredients section: combo/search fill left side; amount edit + Set button right-anchored.
    int ing_x           = MARGIN + PAD;
    int ing_right       = MARGIN + GB_W - PAD;
    int ing_set_btn_x   = ing_right - ING_BTN_W;
    int ing_edit_x      = ing_set_btn_x - ING_GAP - ING_EDIT_W;
    int ING_COMBO_W     = ing_edit_x - ING_GAP - ing_x;

    // Vertical layout: fixed-height sections stacked from top; file buttons above status bar.
    int res_gb_inner_h  = PAD + (ROW_H + ROW_GAP) * 4 - ROW_GAP + PAD;
    int ing_gb_inner_h  = PAD + (ROW_H + ROW_GAP) * 2 - ROW_GAP + PAD;
    int res_gb_y        = MARGIN;
    int ing_gb_y        = res_gb_y + res_gb_inner_h + 18 + SEC_GAP;
    int file_y          = H - SBAR_H - SEC_GAP - FILE_BTN_H;
    int half_w          = (GB_W - 8) / 2;

    // Batch all moves to eliminate flicker during resize.
    HDWP hdwp = BeginDeferWindowPos(24);
    if (!hdwp) return;

    auto Move = [&](HWND hw, int x, int y, int w, int h) {
        if (hw) hdwp = DeferWindowPos(hdwp, hw, NULL, x, y, w, h,
                                       SWP_NOZORDER | SWP_NOACTIVATE);
    };

    // --- Resources group box ---
    Move(g_hGbResources, MARGIN, res_gb_y, GB_W, res_gb_inner_h + 18);
    int ry = res_gb_y + 22;
    Move(g_hLblGold,                              GB_INNER_X, ry, LBL_W, ROW_H);
    Move(g_hEditGoldValue,                        GB_EDIT_X,  ry, EDIT_W, ROW_H);
    Move(GetDlgItem(hDlg, IDC_BTN_SET_GOLD),      GB_BTN_X,   ry, BTN_W,  ROW_H);
    ry += ROW_H + ROW_GAP;
    Move(g_hLblBei,                               GB_INNER_X, ry, LBL_W, ROW_H);
    Move(g_hEditBeiValue,                         GB_EDIT_X,  ry, EDIT_W, ROW_H);
    Move(GetDlgItem(hDlg, IDC_BTN_SET_BEI),       GB_BTN_X,   ry, BTN_W,  ROW_H);
    ry += ROW_H + ROW_GAP;
    Move(g_hLblFlame,                             GB_INNER_X, ry, LBL_W, ROW_H);
    Move(g_hEditFlameValue,                       GB_EDIT_X,  ry, EDIT_W, ROW_H);
    Move(GetDlgItem(hDlg, IDC_BTN_SET_FLAME),     GB_BTN_X,   ry, BTN_W,  ROW_H);
    ry += ROW_H + ROW_GAP;
    Move(g_hLblFollower,                          GB_INNER_X, ry, LBL_W, ROW_H);
    Move(g_hEditFollowerValue,                    GB_EDIT_X,  ry, EDIT_W, ROW_H);
    Move(GetDlgItem(hDlg, IDC_BTN_SET_FOLLOWER),  GB_BTN_X,   ry, BTN_W,  ROW_H);

    // --- Ingredients group box ---
    Move(g_hGbIngredients, MARGIN, ing_gb_y, GB_W, ing_gb_inner_h + 18);
    int iy = ing_gb_y + 22;
    Move(g_hEditIngSearch,                              ing_x,        iy, ING_COMBO_W, ROW_H);
    iy += ROW_H + ROW_GAP;
    Move(g_hComboIngredients,                           ing_x,        iy, ING_COMBO_W, ROW_H);
    Move(g_hEditIngAmount,                              ing_edit_x,   iy, ING_EDIT_W,  ROW_H);
    Move(GetDlgItem(hDlg, IDC_BTN_SET_ING_AMOUNT),     ing_set_btn_x,iy, ING_BTN_W,   ROW_H);

    // --- File buttons ---
    Move(g_hBtnLoadSave,  MARGIN,              file_y, half_w, FILE_BTN_H);
    Move(g_hBtnWriteSave, MARGIN + half_w + 8, file_y, half_w, FILE_BTN_H);

    // --- Status bar ---
    Move(g_hStatusBar, 0, H - SBAR_H, W, SBAR_H);

    EndDeferWindowPos(hdwp);
}

// --- Entry Point: WinMain ---
// The main entry point for the Windows application.
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // Suppress unused parameter warnings.
    (void)hPrevInstance;

    // Check for "-log" command line argument to enable file logging.
    bool enableFileLogging = (strstr(lpCmdLine, "-log") != nullptr);
    Logger::Initialize("DaveSaveEd", enableFileLogging, BIN_DIRECTORY); // Initialize the logging system.
    LogMessage(LOG_INFO_LEVEL, "Application started.");

    // Initialize COM (Component Object Model) for functions like SHGetKnownFolderPath.
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr)) {
        LogMessage(LOG_ERROR_LEVEL, "COM Initialization Failed!");
        MessageBox(NULL, "COM Initialization Failed!", "Error", MB_ICONERROR | MB_OK);
        Logger::Shutdown();
        return 1;
    }

    // Register the custom dialog window class.
    WNDCLASSEX wc = {0};
    wc.cbSize        = sizeof(WNDCLASSEX);
    wc.lpfnWndProc   = DialogProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = g_hBackgroundBrush = CreateSolidBrush(GetSysColor(COLOR_BTNFACE)); // Set background color.
    wc.lpszClassName = "DaveSaveEdDialogClass";
    wc.hIcon         = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON)); // NEW: Set application icon
    wc.hIconSm       = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_APPICON)); // NEW: Set small icon for taskbar/title bar

    if (!RegisterClassEx(&wc)) {
        LogMessage(LOG_ERROR_LEVEL, "Window Registration Failed!");
        MessageBox(NULL, "Window Registration Failed!", "Error", MB_ICONERROR | MB_OK);
        CoUninitialize();
        Logger::Shutdown();
        return 1;
    }

    // Create the main dialog window.
    g_hDlg = CreateWindowEx(
        WS_EX_APPWINDOW | WS_EX_WINDOWEDGE, // Extended window styles.
        "DaveSaveEdDialogClass",            // Class name.
        "DaveSaveEd",                       // NEW: Window title changed to "DaveSaveEd".
        WS_OVERLAPPEDWINDOW | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME | WS_MAXIMIZEBOX, // Window styles.
        CW_USEDEFAULT, CW_USEDEFAULT,       // Default position.
        520, 480,                           // Window size — wider for grouped layout with status bar.
        NULL,                               // Parent window.
        NULL,                               // Menu handle.
        hInstance,                          // Application instance.
        NULL                                // Window creation data.
    );

    if (g_hDlg == NULL) {
        LogMessage(LOG_ERROR_LEVEL, "Window Creation Failed!");
        MessageBox(NULL, "Window Creation Failed!", "Error", MB_ICONERROR | MB_OK);
        CoUninitialize();
        Logger::Shutdown();
        return 1;
    }

    // Center the dialog window on the screen.
    RECT rcScreen;
    GetClientRect(GetDesktopWindow(), &rcScreen);
    int screenWidth = rcScreen.right;
    int screenHeight = rcScreen.bottom;

    RECT rcDlg;
    GetWindowRect(g_hDlg, &rcDlg);
    int dlgWidth = rcDlg.right - rcDlg.left;
    int dlgHeight = rcDlg.bottom - rcDlg.top;

    SetWindowPos(g_hDlg, NULL,
                 (screenWidth - dlgWidth) / 2,
                 (screenHeight - dlgHeight) / 2,
                 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER);

    // Display the window and begin the message loop.
    ShowWindow(g_hDlg, nCmdShow);
    UpdateWindow(g_hDlg);

    MSG msg = {0};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    LogMessage(LOG_INFO_LEVEL, "Application exiting.");
    // Clean up resources before exiting.
    if (g_hBackgroundBrush) {
        DeleteObject(g_hBackgroundBrush);
        g_hBackgroundBrush = NULL;
    }
    CoUninitialize(); // Uninitialize COM.
    Logger::Shutdown(); // Shut down the logging system.
    return (int)msg.wParam;
}

// --- Dialog Procedure ---
// Callback function for handling messages sent to the main dialog window.
INT_PTR CALLBACK DialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_CREATE: {
            g_hDlg = hDlg; // Store the dialog handle globally.
            LogMessage(LOG_INFO_LEVEL, "WM_CREATE received. Initializing UI and Reference Database.");

            // Initialize currency edit fields to blank and disabled until a save is loaded.
            // (Handles are set during control creation below.)

            // --- Reference Database Initialization (from embedded_sql.h) ---
            // Opens an in-memory SQLite database and populates it from compressed SQL data.
            int rc = sqlite3_open(":memory:", &g_refDb);
            if (rc != SQLITE_OK) {
                LogMessage(LOG_ERROR_LEVEL, (std::string("Cannot open in-memory reference database: ") + sqlite3_errmsg(g_refDb)).c_str());
                MessageBox(hDlg, "Failed to open reference database! Application might not function correctly.", "Database Error", MB_ICONERROR | MB_OK);
            } else {
                LogMessage(LOG_INFO_LEVEL, "In-memory reference database opened successfully.");

                // Decompress the embedded SQL data using zlib.
                z_stream strm;
                strm.zalloc = Z_NULL;
                strm.zfree = Z_NULL;
                strm.opaque = Z_NULL;
                strm.avail_in = 0;
                strm.next_in = Z_NULL;

                const size_t MAX_UNCOMPRESSED_SIZE = 150000; // Estimated max size for decompressed data.
                std::vector<char> decompressed_buffer(MAX_UNCOMPRESSED_SIZE);

                rc = inflateInit(&strm); // Initialize the zlib decompression stream.
                if (rc != Z_OK) {
                    LogMessage(LOG_ERROR_LEVEL, (std::string("zlib inflateInit failed: ") + std::string(strm.msg ? strm.msg : "Unknown error")).c_str());
                    MessageBox(hDlg, "Failed to initialize decompressor!", "Decompression Error", MB_ICONERROR | MB_OK);
                    inflateEnd(&strm);
                    sqlite3_close(g_refDb);
                    g_refDb = NULL;
                    return 0;
                }

                strm.avail_in = static_cast<uInt>(embedded_sql_compressed_size);
                strm.next_in = (Bytef*)embedded_sql_compressed;
                strm.avail_out = static_cast<uInt>(decompressed_buffer.size());
                strm.next_out = (Bytef*)decompressed_buffer.data();

                rc = inflate(&strm, Z_FINISH); // Perform decompression.
                if (rc != Z_STREAM_END) {
                    LogMessage(LOG_ERROR_LEVEL, (std::string("zlib inflate failed: ") + std::string(strm.msg ? strm.msg : "Unknown error")).c_str());
                    MessageBox(hDlg, "Failed to decompress SQL data!", "Decompression Error", MB_ICONERROR | MB_OK);
                    inflateEnd(&strm);
                    sqlite3_close(g_refDb);
                    g_refDb = NULL;
                    return 0;
                }

                inflateEnd(&strm); // Clean up zlib stream.

                std::string decompressed_sql_str(decompressed_buffer.data(), strm.total_out);
                LogMessage(LOG_INFO_LEVEL, (std::string("SQL data decompressed successfully. Original size: ") + std::to_string(strm.total_out) + " bytes.").c_str());

                // Execute the decompressed SQL statements to populate the in-memory database.
                rc = sqlite3_exec(g_refDb, decompressed_sql_str.c_str(), 0, 0, 0);
                if (rc != SQLITE_OK) {
                    LogMessage(LOG_ERROR_LEVEL, (std::string("Failed to execute embedded SQL dump for reference DB: ") + sqlite3_errmsg(g_refDb)).c_str());
                    MessageBox(hDlg, "Failed to populate reference database from embedded SQL!", "Database Error", MB_ICONERROR | MB_OK);
                } else {
                    LogMessage(LOG_INFO_LEVEL, "Reference database populated from embedded SQL successfully.");
                }
            }

            // --- Create Fonts ---
            LOGFONTA lf = {};
            lf.lfHeight         = -MulDiv(9, GetDeviceCaps(GetDC(hDlg), LOGPIXELSY), 72);
            lf.lfWeight         = FW_NORMAL;
            lf.lfCharSet        = DEFAULT_CHARSET;
            lf.lfQuality        = CLEARTYPE_QUALITY;
            lf.lfPitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
            StringCbCopyA(lf.lfFaceName, sizeof(lf.lfFaceName), "Segoe UI");
            g_hFont = CreateFontIndirectA(&lf);

            lf.lfWeight = FW_SEMIBOLD;
            g_hFontBold = CreateFontIndirectA(&lf);

            lf.lfWeight = FW_NORMAL;
            lf.lfHeight = -MulDiv(8, GetDeviceCaps(GetDC(hDlg), LOGPIXELSY), 72);
            g_hFontSmall = CreateFontIndirectA(&lf);

            // --- Layout constants ---
            RECT cr; GetClientRect(hDlg, &cr);
            int W = cr.right - cr.left;
            int H = cr.bottom - cr.top;

            // --- Section 1: Resources group box ---
            // All controls are created at 0,0,0,0 — DoLayout positions them.
            g_hGbResources = CreateWindowEx(0, "BUTTON", "Resources",
                WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                0, 0, 0, 0,
                hDlg, NULL, GetModuleHandle(NULL), NULL);

            // Gold
            g_hLblGold = CreateWindowEx(WS_EX_TRANSPARENT, "STATIC", "Gold",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                0, 0, 0, 0, hDlg,
                (HMENU)IDC_STATIC_GOLD_LABEL, GetModuleHandle(NULL), NULL);
            g_hEditGoldValue = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "",
                WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
                0, 0, 0, 0, hDlg,
                (HMENU)IDC_EDIT_GOLD_VALUE, GetModuleHandle(NULL), NULL);
            EnableWindow(g_hEditGoldValue, FALSE);
            CreateWindowEx(0, "BUTTON", "Set",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 0, 0, hDlg,
                (HMENU)IDC_BTN_SET_GOLD, GetModuleHandle(NULL), NULL);

            // Bei
            g_hLblBei = CreateWindowEx(WS_EX_TRANSPARENT, "STATIC", "Bei",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                0, 0, 0, 0, hDlg,
                (HMENU)IDC_STATIC_BEI_LABEL, GetModuleHandle(NULL), NULL);
            g_hEditBeiValue = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "",
                WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
                0, 0, 0, 0, hDlg,
                (HMENU)IDC_EDIT_BEI_VALUE, GetModuleHandle(NULL), NULL);
            EnableWindow(g_hEditBeiValue, FALSE);
            CreateWindowEx(0, "BUTTON", "Set",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 0, 0, hDlg,
                (HMENU)IDC_BTN_SET_BEI, GetModuleHandle(NULL), NULL);

            // Artisan's Flame
            g_hLblFlame = CreateWindowEx(WS_EX_TRANSPARENT, "STATIC", "Artisan's Flame",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                0, 0, 0, 0, hDlg,
                (HMENU)IDC_STATIC_FLAME_LABEL, GetModuleHandle(NULL), NULL);
            g_hEditFlameValue = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "",
                WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
                0, 0, 0, 0, hDlg,
                (HMENU)IDC_EDIT_FLAME_VALUE, GetModuleHandle(NULL), NULL);
            EnableWindow(g_hEditFlameValue, FALSE);
            CreateWindowEx(0, "BUTTON", "Set",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 0, 0, hDlg,
                (HMENU)IDC_BTN_SET_FLAME, GetModuleHandle(NULL), NULL);

            // Follower Count
            g_hLblFollower = CreateWindowEx(WS_EX_TRANSPARENT, "STATIC", "Follower Count",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                0, 0, 0, 0, hDlg,
                (HMENU)IDC_STATIC_FOLLOWER_LABEL, GetModuleHandle(NULL), NULL);
            g_hEditFollowerValue = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "",
                WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
                0, 0, 0, 0, hDlg,
                (HMENU)IDC_EDIT_FOLLOWER_VALUE, GetModuleHandle(NULL), NULL);
            EnableWindow(g_hEditFollowerValue, FALSE);
            CreateWindowEx(0, "BUTTON", "Set",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 0, 0, hDlg,
                (HMENU)IDC_BTN_SET_FOLLOWER, GetModuleHandle(NULL), NULL);

            // --- Section 2: Ingredients group box ---
            g_hGbIngredients = CreateWindowEx(0, "BUTTON", "Ingredients",
                WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                0, 0, 0, 0,
                hDlg, NULL, GetModuleHandle(NULL), NULL);

            // Search box — placeholder cleared on EN_SETFOCUS, restored on EN_KILLFOCUS if empty.
            g_hEditIngSearch = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                0, 0, 0, 0,
                hDlg, (HMENU)IDC_EDIT_ING_SEARCH, GetModuleHandle(NULL), NULL);
            SetWindowTextA(g_hEditIngSearch, "Search...");

            // Combo + amount edit + Set button
            g_hComboIngredients = CreateWindowEx(0, "COMBOBOX", "",
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                0, 0, 0, 200,
                hDlg, (HMENU)IDC_COMBO_INGREDIENTS, GetModuleHandle(NULL), NULL);

            g_hEditIngAmount = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "1",
                WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
                0, 0, 0, 0,
                hDlg, (HMENU)IDC_EDIT_ING_AMOUNT, GetModuleHandle(NULL), NULL);

            CreateWindowEx(0, "BUTTON", "Set",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 0, 0,
                hDlg, (HMENU)IDC_BTN_SET_ING_AMOUNT, GetModuleHandle(NULL), NULL);

            // Populate ingredient combo from reference DB.
            if (g_refDb) {
                sqlite3_stmt* pStmt;
                const char* sql = "SELECT I.TID, T.ItemTextID FROM Ingredients I JOIN Items T ON I.TID = T.ItemDataID ORDER BY I.TID ASC";
                if (sqlite3_prepare_v2(g_refDb, sql, -1, &pStmt, NULL) == SQLITE_OK) {
                    while (sqlite3_step(pStmt) == SQLITE_ROW) {
                        int id = sqlite3_column_int(pStmt, 0);
                        const char* rawText = reinterpret_cast<const char*>(sqlite3_column_text(pStmt, 1));
                        std::string textID = rawText ? rawText : "";
                        std::string displayStr = std::to_string(id) + " - " + textID;
                        g_ingredientList.emplace_back(id, displayStr);
                    }
                    sqlite3_finalize(pStmt);
                }
                for (const auto& entry : g_ingredientList) {
                    int index = ComboBox_AddString(g_hComboIngredients, entry.second.c_str());
                    ComboBox_SetItemData(g_hComboIngredients, index, entry.first);
                }
            }

            // --- Section 3: File operation buttons ---
            g_hBtnLoadSave = CreateWindowEx(0, "BUTTON", "Load Save File...",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 0, 0,
                hDlg, (HMENU)IDC_BTN_LOAD_SAVE, GetModuleHandle(NULL), NULL);

            g_hBtnWriteSave = CreateWindowEx(0, "BUTTON", "Write Save File",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                0, 0, 0, 0,
                hDlg, (HMENU)IDC_BTN_WRITE_SAVE, GetModuleHandle(NULL), NULL);
            EnableWindow(g_hBtnWriteSave, FALSE);

            // --- Status bar (bottom strip) ---
            g_hStatusBar = CreateWindowEx(0, "STATIC", "  No save file loaded.",
                WS_CHILD | WS_VISIBLE | SS_LEFT,
                0, 0, 0, 0,
                hDlg, NULL, GetModuleHandle(NULL), NULL);

            // --- Apply fonts to all controls ---
            EnumChildWindows(hDlg, SetChildFont, (LPARAM)g_hFont);
            // Override group box titles and section headers with bold font.
            EnumChildWindows(hDlg, [](HWND hChild, LPARAM lParam) -> BOOL {
                char cls[32] = {};
                GetClassNameA(hChild, cls, sizeof(cls));
                if (lstrcmpiA(cls, "BUTTON") == 0) {
                    LONG style = GetWindowLong(hChild, GWL_STYLE);
                    if ((style & BS_TYPEMASK) == BS_GROUPBOX)
                        SendMessage(hChild, WM_SETFONT, (WPARAM)(HFONT)lParam, TRUE);
                }
                return TRUE;
            }, (LPARAM)g_hFontBold);
            // Small font for status bar.
            if (g_hStatusBar) SendMessage(g_hStatusBar, WM_SETFONT, (WPARAM)g_hFontSmall, TRUE);

            // Initial layout pass — positions all controls based on current client size.
            DoLayout(hDlg, W, H);

            return 0;
        }

        case WM_COMMAND: {
            // Handle button clicks and other command messages.
            WORD controlId = GET_WM_COMMAND_ID(wParam, lParam);

            switch (controlId) {
                case IDC_BTN_SET_GOLD: {
                    LogMessage(LOG_INFO_LEVEL, "Set Gold button clicked.");
                    if (g_saveGameManager.IsSaveFileLoaded()) {
                        char buf[32] = {0};
                        GetWindowTextA(g_hEditGoldValue, buf, sizeof(buf));
                        long long value = _atoi64(buf);
                        g_saveGameManager.SetGold(value); // Clamped to max inside setter.
                        UpdateCurrencyDisplay();           // Refresh to show clamped value.
                    } else {
                        MessageBox(hDlg, "No save file loaded or valid data to modify!", "Error", MB_ICONWARNING | MB_OK);
                    }
                    break;
                }
                case IDC_BTN_SET_BEI: {
                    LogMessage(LOG_INFO_LEVEL, "Set Bei button clicked.");
                    if (g_saveGameManager.IsSaveFileLoaded()) {
                        char buf[32] = {0};
                        GetWindowTextA(g_hEditBeiValue, buf, sizeof(buf));
                        long long value = _atoi64(buf);
                        g_saveGameManager.SetBei(value);
                        UpdateCurrencyDisplay();
                    } else {
                        MessageBox(hDlg, "No save file loaded or valid data to modify!", "Error", MB_ICONWARNING | MB_OK);
                    }
                    break;
                }
                case IDC_BTN_SET_FLAME: {
                    LogMessage(LOG_INFO_LEVEL, "Set Artisan's Flame button clicked.");
                    if (g_saveGameManager.IsSaveFileLoaded()) {
                        char buf[32] = {0};
                        GetWindowTextA(g_hEditFlameValue, buf, sizeof(buf));
                        long long value = _atoi64(buf);
                        g_saveGameManager.SetArtisansFlame(value);
                        UpdateCurrencyDisplay();
                    } else {
                        MessageBox(hDlg, "No save file loaded or valid data to modify!", "Error", MB_ICONWARNING | MB_OK);
                    }
                    break;
                }
                case IDC_BTN_SET_FOLLOWER: {
                    LogMessage(LOG_INFO_LEVEL, "Set Follower Count button clicked.");
                    if (g_saveGameManager.IsSaveFileLoaded()) {
                        char buf[32] = {0};
                        GetWindowTextA(g_hEditFollowerValue, buf, sizeof(buf));
                        long long value = _atoi64(buf);
                        g_saveGameManager.SetFollowerCount(value);
                        UpdateCurrencyDisplay();
                    } else {
                        MessageBox(hDlg, "No save file loaded or valid data to modify!", "Error", MB_ICONWARNING | MB_OK);
                    }
                    break;
                }
                case IDC_EDIT_ING_SEARCH:
                    if (HIWORD(wParam) == EN_SETFOCUS) {
                        // Clear the placeholder text when the user clicks into the box.
                        char buf[4] = {};
                        GetWindowTextA(g_hEditIngSearch, buf, sizeof(buf));
                        if (lstrcmpiA(buf, "Search...") == 0)
                            SetWindowTextA(g_hEditIngSearch, "");
                    } else if (HIWORD(wParam) == EN_KILLFOCUS) {
                        // Restore placeholder if the box is left empty.
                        char buf[4] = {};
                        GetWindowTextA(g_hEditIngSearch, buf, sizeof(buf));
                        if (buf[0] == '\0')
                            SetWindowTextA(g_hEditIngSearch, "Search...");
                    } else if (HIWORD(wParam) == EN_CHANGE) {
                        char searchBuf[128] = {0};
                        GetWindowTextA(g_hEditIngSearch, searchBuf, sizeof(searchBuf));
                        std::string filter(searchBuf);
                        // Don't filter on the placeholder text itself.
                        if (filter == "Search...") filter = "";

                        std::string filterLower = filter;
                        for (char& c : filterLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                        ComboBox_ResetContent(g_hComboIngredients);
                        for (const auto& entry : g_ingredientList) {
                            std::string dispLower = entry.second;
                            for (char& c : dispLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                            if (filterLower.empty() || dispLower.find(filterLower) != std::string::npos) {
                                int index = ComboBox_AddString(g_hComboIngredients, entry.second.c_str());
                                ComboBox_SetItemData(g_hComboIngredients, index, entry.first);
                            }
                        }
                    }
                    break;

                case IDC_COMBO_INGREDIENTS:
                    // When the user picks a different ingredient, pre-fill the amount field
                    // with whatever count is already in the loaded save, or 0 if not present.
                    if (HIWORD(wParam) == CBN_SELCHANGE) {
                        int selIdx = ComboBox_GetCurSel(g_hComboIngredients);
                        if (selIdx != CB_ERR && g_saveGameManager.IsSaveFileLoaded()) {
                            int ingredientID = (int)ComboBox_GetItemData(g_hComboIngredients, selIdx);
                            int currentCount = g_saveGameManager.GetIngredientCount(ingredientID);
                            SetWindowTextA(g_hEditIngAmount, std::to_string(currentCount).c_str());
                        }
                    }
                    break;

                case IDC_BTN_SET_ING_AMOUNT: {
                    if (!g_saveGameManager.IsSaveFileLoaded()) {
                        MessageBox(hDlg, "Load a save file first!", "Error", MB_ICONWARNING | MB_OK);
                        break;
                    }

                    int selIdx = ComboBox_GetCurSel(g_hComboIngredients);
                    if (selIdx == CB_ERR) {
                        MessageBox(hDlg, "Select an ingredient ID first!", "Error", MB_ICONWARNING | MB_OK);
                        break;
                    }

                    // Get ID from combo and Amount from edit box
                    int ingredientID = (int)ComboBox_GetItemData(g_hComboIngredients, selIdx);
                    char buf[16];
                    GetWindowTextA(g_hEditIngAmount, buf, 16);
                    int amount = atoi(buf);

                    // Call the manager to update
                    if (g_saveGameManager.SetSpecificIngredient(ingredientID, amount, g_refDb)) {
                        LogMessage(LOG_INFO_LEVEL, "Manually updated ingredient.");
                        MessageBox(hDlg, "Ingredient updated!", "Success", MB_ICONINFORMATION | MB_OK);
                    } else {
                        MessageBox(hDlg, "Failed to update. Ingredient might not exist in save.", "Error", MB_ICONERROR | MB_OK);
                    }
                    break;
                }
                case IDC_BTN_LOAD_SAVE: {
                    LogMessage(LOG_INFO_LEVEL, "Load Save File button clicked.");
                    std::string latestSavePath;
                    // Use the static method to get the default directory and latest file.
                    std::filesystem::path defaultDir = SaveGameManager::GetDefaultSaveGameDirectoryAndLatestFile(latestSavePath);

                    // Configure the Open File dialog.
                    OPENFILENAMEA ofn;
                    char szFile[MAX_PATH] = {0}; // Buffer for the selected file path.
                    ZeroMemory(&ofn, sizeof(ofn));
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner = hDlg;
                    ofn.lpstrFile = szFile;
                    ofn.nMaxFile = sizeof(szFile);
                    ofn.lpstrFilter = "Dave the Diver Save Files (*.sav)\0*.sav\0Xbox Save Files\0*\0All Files (*.*)\0*.*\0";
                    ofn.nFilterIndex = 1;
                    ofn.lpstrFileTitle = NULL;
                    ofn.nMaxFileTitle = 0;
                    ofn.lpstrInitialDir = defaultDir.string().c_str(); // Set initial directory.
                    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR; // Flags for behavior.

                    // Pre-fill the dialog with the latest save path if found.
                    if (!latestSavePath.empty()) {
                        strncpy_s(szFile, MAX_PATH, latestSavePath.c_str(), _TRUNCATE);
                        // If it's an Xbox save (no .sav extension), switch filter to "Xbox Save Files (*)"
                        if (latestSavePath.find(".sav") == std::string::npos) {
                            ofn.nFilterIndex = 2;
                        }
                    }

                    // Show the Open File dialog.
                    if (GetOpenFileNameA(&ofn) == TRUE) {
                        // Attempt to load the selected save file using the SaveGameManager.
                        if (g_saveGameManager.LoadSaveFile(ofn.lpstrFile)) {
                            //MessageBox(hDlg, "Save file loaded successfully!", "Success", MB_ICONINFORMATION | MB_OK);
                        } else {
                            MessageBox(hDlg, "Failed to load or parse save file!", "Load Error", MB_ICONERROR | MB_OK);
                        }
                        UpdateCurrencyDisplay(); // Always update UI after load attempt.
                    } else {
                        LogMessage(LOG_INFO_LEVEL, "File selection cancelled.");
                        UpdateCurrencyDisplay(); // Update UI to reflect no file loaded (e.g., clear fields).
                    }
                    break;
                }
                case IDC_BTN_WRITE_SAVE:
                    LogMessage(LOG_INFO_LEVEL, "Write Save File button clicked.");
                    std::string backup_path;
                    if (g_saveGameManager.WriteSaveFile(backup_path)) {
                        std::string outro = "Save file updated and backed up to " + backup_path += "!"; 
                        MessageBox(hDlg, outro.c_str(), "DaveSaveEd", MB_ICONINFORMATION | MB_OK);
                        PostQuitMessage(0);
                    } else {
                        MessageBox(hDlg, "Failed to write save file!", "Save Error", MB_ICONERROR | MB_OK);
                    }
                    break;
            }
            return 0;
        }
        
        case WM_SIZE: {
            int newW = LOWORD(lParam);
            int newH = HIWORD(lParam);
            if (wParam != SIZE_MINIMIZED && newW > 0 && newH > 0)
                DoLayout(hDlg, newW, newH);
            return 0;
        }

        case WM_GETMINMAXINFO: {
            // Enforce a minimum window size so controls never get crushed.
            MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            mmi->ptMinTrackSize.x = 420;
            mmi->ptMinTrackSize.y = 380;
            return 0;
        }

        case WM_CLOSE:
            LogMessage(LOG_INFO_LEVEL, "WM_CLOSE received. Destroying window.");
            DestroyWindow(hDlg); // Destroy the window.
            return 0;

        case WM_DESTROY:
            LogMessage(LOG_INFO_LEVEL, "WM_DESTROY received. Posting quit message.");
            if (g_refDb) {
                sqlite3_close(g_refDb);
                g_refDb = NULL;
                LogMessage(LOG_INFO_LEVEL, "Reference database closed.");
            }
            if (g_hFont)      { DeleteObject(g_hFont);      g_hFont      = NULL; }
            if (g_hFontBold)  { DeleteObject(g_hFontBold);  g_hFontBold  = NULL; }
            if (g_hFontSmall) { DeleteObject(g_hFontSmall); g_hFontSmall = NULL; }
            PostQuitMessage(0);
            return 0;

        case WM_CTLCOLORSTATIC: {
            HDC hdcStatic = (HDC)wParam;
            HWND hCtrl    = (HWND)lParam;
            SetBkMode(hdcStatic, TRANSPARENT);
            // Status bar gets a slightly darker tint to visually separate it.
            if (hCtrl == g_hStatusBar) {
                SetTextColor(hdcStatic, RGB(90, 90, 90));
                static HBRUSH hSbarBrush = CreateSolidBrush(RGB(225, 225, 225));
                return (INT_PTR)hSbarBrush;
            }
            return (INT_PTR)g_hBackgroundBrush;
        }

        case WM_CTLCOLOREDIT: {
            // Keep edit controls white regardless of dialog background.
            HDC hdcEdit = (HDC)wParam;
            SetBkColor(hdcEdit, RGB(255, 255, 255));
            SetTextColor(hdcEdit, RGB(30, 30, 30));
            static HBRUSH hWhiteBrush = (HBRUSH)GetStockObject(WHITE_BRUSH);
            return (INT_PTR)hWhiteBrush;
        }

        default:
            // Let the default window procedure handle any unhandled messages.
            return DefWindowProc(hDlg, message, wParam, lParam);
    }
}