// DaveSaveEd.h
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
#pragma once

#include <string>
#include <windows.h> // For HWND, etc. (if needed by public functions)

// Defines the default binary output directory, used for logging and other file operations.
const std::string BIN_DIRECTORY = "bin";

// Defines logging levels for the application's logging system.
enum LogLevel {
    LOG_INFO_LEVEL,
    LOG_ERROR_LEVEL,
    LOG_WARNING_LEVEL
};

// Function prototype for the global logging function.
void LogMessage(LogLevel level, const char* message, int sqlite_err_code = -1);

// External declaration for the main dialog window handle, used by various parts of the application.
extern HWND g_hDlg;