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

// --- Forward Declarations ---
// Main dialog procedure callback function.
INT_PTR CALLBACK DialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
// Function to update the currency display on the UI from the loaded save data.
void UpdateCurrencyDisplay();

// --- Function to update currency edit fields ---
// Retrieves currency values from the SaveGameManager and updates the corresponding UI controls.
void UpdateCurrencyDisplay() {
    if (g_saveGameManager.IsSaveFileLoaded()) {
        SetWindowTextA(g_hEditGoldValue,     std::to_string(g_saveGameManager.GetGold()).c_str());
        SetWindowTextA(g_hEditBeiValue,      std::to_string(g_saveGameManager.GetBei()).c_str());
        SetWindowTextA(g_hEditFlameValue,    std::to_string(g_saveGameManager.GetArtisansFlame()).c_str());
        SetWindowTextA(g_hEditFollowerValue, std::to_string(g_saveGameManager.GetFollowerCount()).c_str());
        // Enable editing now that a save is loaded.
        EnableWindow(g_hEditGoldValue,     TRUE);
        EnableWindow(g_hEditBeiValue,      TRUE);
        EnableWindow(g_hEditFlameValue,    TRUE);
        EnableWindow(g_hEditFollowerValue, TRUE);
        LogMessage(LOG_INFO_LEVEL, "Currency display updated from SaveGameManager values.");
    } else {
        SetWindowTextA(g_hEditGoldValue,     "");
        SetWindowTextA(g_hEditBeiValue,      "");
        SetWindowTextA(g_hEditFlameValue,    "");
        SetWindowTextA(g_hEditFollowerValue, "");
        // Disable editing until a save is loaded.
        EnableWindow(g_hEditGoldValue,     FALSE);
        EnableWindow(g_hEditBeiValue,      FALSE);
        EnableWindow(g_hEditFlameValue,    FALSE);
        EnableWindow(g_hEditFollowerValue, FALSE);
        LogMessage(LOG_INFO_LEVEL, "No valid save data loaded. Displaying blank currency values.");
    }
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
        WS_OVERLAPPEDWINDOW | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, // Window styles.
        CW_USEDEFAULT, CW_USEDEFAULT,       // Default position.
        450, 360,                           // Initial size (height reduced after removing max-ingredient buttons).
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

            // --- Create UI Elements (Centered Layout) ---
            // Defines dimensions and spacing for UI controls to achieve a centered layout.
            int control_height = 24;
            int spacing_y = 10;
            int section_spacing_y = 15;

            int label_width = 110;
            int value_width = 100;
            int currency_button_width = 120;
            int spacing_x_currency_row = 10;

            int ing_btn_width = 170;
            int ing_btn_spacing = 20;

            int file_btn_width = 150;
            int file_btn_spacing = 20;

            // Calculate total widths for horizontal centering.
            int currency_row_total_width = label_width + spacing_x_currency_row + value_width + spacing_x_currency_row + currency_button_width;
            int ingredient_row_total_width = ing_btn_width * 2 + ing_btn_spacing;
            int file_row_total_width = file_btn_width * 2 + file_btn_spacing;

            RECT client_rect;
            GetClientRect(hDlg, &client_rect);
            int dialog_client_width = client_rect.right - client_rect.left;
            int dialog_client_height = client_rect.bottom - client_rect.top;

            // Calculate total height needed for all UI blocks.
            int total_currency_block_height = (control_height * 4) + (spacing_y * 3);
            // No separate max-ingredient button row any more.
            int total_ingredient_search_height = control_height; // Search filter box above the combo.
            int total_ingredient_scan_height = control_height;   // Combo/edit/set row for individual editing.
            int total_file_block_height = control_height + 5;    // +5 for slight extra spacing.

            int total_ui_elements_height = total_currency_block_height + section_spacing_y +
                                           total_ingredient_search_height + spacing_y +
                                           total_ingredient_scan_height + section_spacing_y +
                                           total_file_block_height;

            // Calculate initial Y position to vertically center the UI elements.
            int y_pos_start = (dialog_client_height - total_ui_elements_height) / 2;
            if (y_pos_start < 10) y_pos_start = 10; // Ensure a minimum top margin.

            int y_pos = y_pos_start; // Current Y position for placing controls.

            // Calculate X start positions for horizontal centering of each row type.
            int currency_x_start = (dialog_client_width - currency_row_total_width) / 2;
            int current_label_x = currency_x_start;
            int current_value_x = currency_x_start + label_width + spacing_x_currency_row;
            int current_button_x = current_value_x + value_width + spacing_x_currency_row;

            // Create Currency UI Elements.
            // Each row: label | editable value (ES_NUMBER) | "Set" button
            CreateWindowEx(WS_EX_TRANSPARENT, "STATIC", "Gold:", WS_CHILD | WS_VISIBLE,
                current_label_x, y_pos, label_width, control_height, hDlg, (HMENU)IDC_STATIC_GOLD_LABEL, GetModuleHandle(NULL), NULL);
            g_hEditGoldValue = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
                current_value_x, y_pos, value_width, control_height, hDlg, (HMENU)IDC_EDIT_GOLD_VALUE, GetModuleHandle(NULL), NULL);
            EnableWindow(g_hEditGoldValue, FALSE);
            CreateWindowEx(0, "BUTTON", "Set", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                current_button_x, y_pos, currency_button_width, control_height, hDlg, (HMENU)IDC_BTN_SET_GOLD, GetModuleHandle(NULL), NULL);
            y_pos += control_height + spacing_y;

            CreateWindowEx(WS_EX_TRANSPARENT, "STATIC", "Bei:", WS_CHILD | WS_VISIBLE,
                current_label_x, y_pos, label_width, control_height, hDlg, (HMENU)IDC_STATIC_BEI_LABEL, GetModuleHandle(NULL), NULL);
            g_hEditBeiValue = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
                current_value_x, y_pos, value_width, control_height, hDlg, (HMENU)IDC_EDIT_BEI_VALUE, GetModuleHandle(NULL), NULL);
            EnableWindow(g_hEditBeiValue, FALSE);
            CreateWindowEx(0, "BUTTON", "Set", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                current_button_x, y_pos, currency_button_width, control_height, hDlg, (HMENU)IDC_BTN_SET_BEI, GetModuleHandle(NULL), NULL);
            y_pos += control_height + spacing_y;

            CreateWindowEx(WS_EX_TRANSPARENT, "STATIC", "Artisan's Flame:", WS_CHILD | WS_VISIBLE,
                current_label_x, y_pos, label_width, control_height, hDlg, (HMENU)IDC_STATIC_FLAME_LABEL, GetModuleHandle(NULL), NULL);
            g_hEditFlameValue = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
                current_value_x, y_pos, value_width, control_height, hDlg, (HMENU)IDC_EDIT_FLAME_VALUE, GetModuleHandle(NULL), NULL);
            EnableWindow(g_hEditFlameValue, FALSE);
            CreateWindowEx(0, "BUTTON", "Set", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                current_button_x, y_pos, currency_button_width, control_height, hDlg, (HMENU)IDC_BTN_SET_FLAME, GetModuleHandle(NULL), NULL);
            y_pos += control_height + spacing_y;

            CreateWindowEx(WS_EX_TRANSPARENT, "STATIC", "Follower Count:", WS_CHILD | WS_VISIBLE,
                current_label_x, y_pos, label_width, control_height, hDlg, (HMENU)IDC_STATIC_FOLLOWER_LABEL, GetModuleHandle(NULL), NULL);
            g_hEditFollowerValue = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "", WS_CHILD | WS_VISIBLE | ES_NUMBER | ES_AUTOHSCROLL,
                current_value_x, y_pos, value_width, control_height, hDlg, (HMENU)IDC_EDIT_FOLLOWER_VALUE, GetModuleHandle(NULL), NULL);
            EnableWindow(g_hEditFollowerValue, FALSE);
            CreateWindowEx(0, "BUTTON", "Set", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                current_button_x, y_pos, currency_button_width, control_height, hDlg, (HMENU)IDC_BTN_SET_FOLLOWER, GetModuleHandle(NULL), NULL);
            y_pos += control_height + section_spacing_y;

            // Create Ingredient UI Elements.
            int ing_x_start = (dialog_client_width - ingredient_row_total_width) / 2;
            // (Max Own/All ingredient buttons removed – use the per-ingredient editor below.)

            int combo_width = 180;
            int amount_width = 60;
            int set_btn_width = 80;
            int manual_row_total = combo_width + amount_width + set_btn_width + 20;
            int manual_x_start = (dialog_client_width - manual_row_total) / 2;

            // Search filter box — user types here to narrow the ingredient combo below.
            g_hEditIngSearch = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "Search...",
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                manual_x_start, y_pos, combo_width, control_height, hDlg, (HMENU)IDC_EDIT_ING_SEARCH, GetModuleHandle(NULL), NULL);
            y_pos += control_height + spacing_y; // Advance past the search box.

            g_hComboIngredients = CreateWindowEx(0, "COMBOBOX", "",
                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                manual_x_start, y_pos, combo_width, 200, hDlg, (HMENU)IDC_COMBO_INGREDIENTS, GetModuleHandle(NULL), NULL);

            g_hEditIngAmount = CreateWindowEx(WS_EX_CLIENTEDGE, "EDIT", "1",
                WS_CHILD | WS_VISIBLE | ES_NUMBER,
                manual_x_start + combo_width + 10, y_pos, amount_width, control_height, hDlg, (HMENU)IDC_EDIT_ING_AMOUNT, GetModuleHandle(NULL), NULL);

            CreateWindowEx(0, "BUTTON", "Set",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                manual_x_start + combo_width + amount_width + 20, y_pos, set_btn_width, control_height, hDlg, (HMENU)IDC_BTN_SET_ING_AMOUNT, GetModuleHandle(NULL), NULL);

            // Populate g_ingredientList from the Reference DB (cached for fast filtering).
            // Display string uses ItemTextID as the readable name: "ID - ItemTextID".
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
                // Load full unfiltered list into the combo on startup.
                for (const auto& entry : g_ingredientList) {
                    int index = ComboBox_AddString(g_hComboIngredients, entry.second.c_str());
                    ComboBox_SetItemData(g_hComboIngredients, index, entry.first);
                }
            }

            // Create File Operation UI Elements.
            y_pos += control_height + section_spacing_y; // Advance past the ingredient scan row.
            int file_x_start = (dialog_client_width - file_row_total_width) / 2;

            CreateWindowEx(0, "BUTTON", "Load Save File...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                file_x_start, y_pos, file_btn_width, control_height + 5, hDlg, (HMENU)IDC_BTN_LOAD_SAVE, GetModuleHandle(NULL), NULL);
            CreateWindowEx(0, "BUTTON", "Write Save File", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                file_x_start + file_btn_width + file_btn_spacing, y_pos, file_btn_width, control_height + 5, hDlg, (HMENU)IDC_BTN_WRITE_SAVE, GetModuleHandle(NULL), NULL);

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
                    // Re-populate the combo with entries that contain the search text.
                    if (HIWORD(wParam) == EN_CHANGE) {
                        char searchBuf[128] = {0};
                        GetWindowTextA(g_hEditIngSearch, searchBuf, sizeof(searchBuf));
                        std::string filter(searchBuf);

                        // Build lowercase version of the filter for case-insensitive matching.
                        std::string filterLower = filter;
                        for (char& c : filterLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

                        // Rebuild the combo contents to match the current filter.
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
        
        case WM_CLOSE:
            LogMessage(LOG_INFO_LEVEL, "WM_CLOSE received. Destroying window.");
            DestroyWindow(hDlg); // Destroy the window.
            return 0;

        case WM_DESTROY:
            LogMessage(LOG_INFO_LEVEL, "WM_DESTROY received. Posting quit message.");
            // Close the reference database if it's open.
            if (g_refDb) {
                sqlite3_close(g_refDb);
                g_refDb = NULL;
                LogMessage(LOG_INFO_LEVEL, "Reference database closed.");
            }
            PostQuitMessage(0); // Signal the application to exit the message loop.
            return 0;

        case WM_CTLCOLORSTATIC: {
            // Custom drawing for static controls to ensure transparent background with the dialog's brush.
            HDC hdcStatic = (HDC)wParam;
            SetBkMode(hdcStatic, TRANSPARENT); // Set background mode to transparent.
            return (INT_PTR)g_hBackgroundBrush; // Return the brush for the dialog background.
        }

        default:
            // Let the default window procedure handle any unhandled messages.
            return DefWindowProc(hDlg, message, wParam, lParam);
    }
}