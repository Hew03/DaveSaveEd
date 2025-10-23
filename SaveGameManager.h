// SaveGameManager.h
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
#include <vector>
#include <filesystem>
#include "json.hpp"         // Include nlohmann/json for the JSON data
#include "zlib.h"           // For zlib compression/decompression
#include "sqlite3.h"        // For SQLite database operations

class SaveGameManager {
public:
    SaveGameManager();
    ~SaveGameManager();

    // Core Save File Operations
    bool LoadSaveFile(const std::string& filepath);
    bool WriteSaveFile(std::string& out_backup_filepath);

    // Player Stats Getters
    long long GetGold() const;
    long long GetBei() const;
    long long GetArtisansFlame() const;
    long long GetFollowerCount() const;
    bool IsSaveFileLoaded() const { return m_isSaveFileLoaded; }

    // Player Stats Setters
    void SetGold(long long value);
    void SetBei(long long value);
    void SetArtisansFlame(long long value);
    void SetFollowerCount(long long value);

    // Ingredient Modification Functions
    void MaxOwnIngredients(sqlite3* db);
    void MaxAllIngredients(sqlite3* db);

    // Static helper to find save directory
    static std::filesystem::path GetDefaultSaveGameDirectoryAndLatestFile(std::string& latestSaveFileName);

private:
    // --- Member Variables ---
    nlohmann::json m_saveData;
    std::string m_currentSaveFilePath;
    bool m_isSaveFileLoaded;

    // --- Private Helper Methods ---
    // New robust encode/decode functions based on bypass-and-resync logic.
    std::string DecodeAndBypass(const std::vector<unsigned char>& encrypted_data);
    std::vector<unsigned char> EncodeWithBypass(const std::string& utf8_json_string);

    // Helper for DecodeAndBypass: Finds problematic field length and next key index.
    bool FindFieldDetails(const std::vector<unsigned char>& encrypted_bytes, size_t start_pos,
                          size_t& out_field_len, size_t& out_resync_key_idx);

    // Helper for FindFieldDetails: Simple repeating-key XOR on a vector slice.
    std::vector<unsigned char> XorBytes(const std::vector<unsigned char>& data_bytes, size_t key_start_index);
    
    // Helper for EncodeWithBypass: Converts byte vector to hex string.
    std::string BytesToHex(const std::vector<unsigned char>& bytes);

    // Helper for EncodeWithBypass: Converts hex string to byte vector.
    std::vector<unsigned char> HexToBytes(const std::string& hex);

    // Zlib compression/decompression helpers
    std::string decompressZlib(const std::vector<unsigned char>& compressed_bytes);
    std::vector<unsigned char> compressZlib(const std::string& uncompressed_json);

    // Constant for the XOR key
    const std::string XOR_KEY = "GameData";
};

//END OF SaveGameManager.h
