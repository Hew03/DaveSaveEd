// SaveGameManager.cpp
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
#define NOMINMAX // Prevent Windows.h from defining min/max macros

#include "SaveGameManager.h"
#include <fstream>       // For file input/output streams
#include <shlobj.h>      // For SHGetKnownFolderPath (includes windows.h)
#include <chrono>        // For timestamping (std::chrono)
#include <iomanip>       // For std::put_time, std::hex, std::setw, std::setfill
#include <sstream>       // For std::stringstream
#include <algorithm>     // For std::all_of, std::min/max, std::search, std::equal
#include "sqlite3.h"     // Required for sqlite3* parameter in MaxAllIngredients
#include "Logger.h"      // For LogMessage
#include "json.hpp"      // Corrected include for nlohmann/json
#include <vector>        // Required for std::vector
#include <map>           // Required for std::map
#include <set>           // Required for std::set (for DLC check)
#include <string>        // Required for std::string
#include <stdexcept>     // Required for std::runtime_error
#include <filesystem>    // Required for std::filesystem::path, create_directories, copy, last_write_time
#include <regex>         // For regex matching in EncodeWithBypass

// --- Global Constants for SaveGameManager ---
// Note: XOR_KEY is defined in SaveGameManager.h as a member const
// Use long long for currency to avoid overflow
const long long SAVE_MAX_CURRENCY = 999999999LL;

// --- Constants for Bypass Logic ---
static const std::string BYPASS_PREFIX = "BYPASSED_HEX::";
static const std::vector<unsigned char> TRIGGER_FARMANIMAL = {
    '"', 'F', 'a', 'r', 'm', 'A', 'n', 'i', 'm', 'a', 'l', '"', ':',
    '[', '{', '"', 'F', 'a', 'r', 'm', 'A', 'n', 'i', 'm', 'a', 'l',
    'I', 'D', '"', ':', '1', '1', '0', '9', '0', '0', '0', '1', ',',
    '"', 'N', 'a', 'm', 'e', '"', ':', '"'
};
// This can be a vector of vectors if more triggers are found
static const std::vector<std::vector<unsigned char>> TROUBLESOME_TRIGGERS = {
    TRIGGER_FARMANIMAL
};
static const std::vector<unsigned char> END_MARKER = { '"', '}', ']', ',' };


// Constructor: Initializes the SaveGameManager instance.
SaveGameManager::SaveGameManager() : m_isSaveFileLoaded(false) {
    LogMessage(LOG_INFO_LEVEL, "SaveGameManager initialized.");
}

// Destructor: Cleans up resources used by the SaveGameManager.
SaveGameManager::~SaveGameManager() {
    LogMessage(LOG_INFO_LEVEL, "SaveGameManager shutting down.");
}

// --- Hex/Byte Helpers ---

// Helper for EncodeWithBypass: Converts byte vector to hex string.
std::string SaveGameManager::BytesToHex(const std::vector<unsigned char>& bytes) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (unsigned char byte : bytes) {
        ss << std::setw(2) << static_cast<int>(byte);
    }
    return ss.str();
}

// Helper for EncodeWithBypass: Converts hex string to byte vector.
std::vector<unsigned char> SaveGameManager::HexToBytes(const std::string& hex) {
    std::vector<unsigned char> bytes;
    if (hex.length() % 2 != 0) {
        LogMessage(LOG_WARNING_LEVEL, "HexToBytes: Input string has odd length. Returning empty vector.");
        return bytes;
    }
    bytes.reserve(hex.length() / 2);
    for (size_t i = 0; i < hex.length(); i += 2) {
        try {
            std::string byteString = hex.substr(i, 2);
            unsigned char byte = static_cast<unsigned char>(std::stoul(byteString, nullptr, 16));
            bytes.push_back(byte);
        } catch (const std::exception& e) {
            LogMessage(LOG_ERROR_LEVEL, (std::string("HexToBytes: Error parsing hex string: ") + e.what()).c_str());
            bytes.clear();
            return bytes;
        }
    }
    return bytes;
}


// --- BYPASS-AND-RESYNC XOR FUNCTIONS (NEW) ---

// Helper for FindFieldDetails: Simple repeating-key XOR on a vector slice.
std::vector<unsigned char> SaveGameManager::XorBytes(const std::vector<unsigned char>& data_bytes, size_t key_start_index) {
    std::vector<unsigned char> output;
    output.reserve(data_bytes.size());
    size_t key_len = XOR_KEY.length();
    if (key_len == 0) return data_bytes; // Avoid divide by zero

    for (size_t i = 0; i < data_bytes.size(); ++i) {
        output.push_back(data_bytes[i] ^ XOR_KEY[(key_start_index + i) % key_len]);
    }
    return output;
}

// Helper for DecodeAndBypass: Finds problematic field length and next key index.
bool SaveGameManager::FindFieldDetails(const std::vector<unsigned char>& encrypted_bytes, size_t start_pos,
                                  size_t& out_field_len, size_t& out_resync_key_idx)
{
    LogMessage(LOG_INFO_LEVEL, ("--- ENTERING FindFieldDetails: Starting analysis from byte " + std::to_string(start_pos)).c_str());
    size_t key_len = XOR_KEY.length();
    bool length_found = false;
    out_field_len = 0;

    if (start_pos >= encrypted_bytes.size()) {
        LogMessage(LOG_WARNING_LEVEL, "[DEBUG] FindFieldDetails FAILED: start_pos is at or beyond end of data.");
        return false;
    }

    // --- Pass 1: Find the length of the problematic field ---
    LogMessage(LOG_INFO_LEVEL, "[DEBUG] Pass 1: Searching for end-of-field marker...");
    std::vector<unsigned char> slice_for_len_check(encrypted_bytes.begin() + start_pos, encrypted_bytes.end());

    for (size_t offset_pass1 = 0; offset_pass1 < key_len; ++offset_pass1) {
        size_t temp_key_idx = (start_pos + offset_pass1) % key_len;
        std::vector<unsigned char> decrypted_slice = XorBytes(slice_for_len_check, temp_key_idx);

        // Search for END_MARKER in decrypted_slice
        auto it = std::search(decrypted_slice.begin(), decrypted_slice.end(), END_MARKER.begin(), END_MARKER.end());

        if (it != decrypted_slice.end()) {
            out_field_len = std::distance(decrypted_slice.begin(), it);
            length_found = true;
            LogMessage(LOG_INFO_LEVEL, ("[DEBUG]   SUCCESS (Pass 1): Found marker with offset " + std::to_string(offset_pass1) + ". Field length: " + std::to_string(out_field_len)).c_str());
            break;
        }
    }

    if (!length_found) {
        LogMessage(LOG_WARNING_LEVEL, "[DEBUG] Pass 1 FAILED: Could not find end marker.");
        return false;
    }

    // --- Pass 2: Find the correct re-sync key offset ---
    size_t resync_pos = start_pos + out_field_len;
    LogMessage(LOG_INFO_LEVEL, ("[DEBUG] Pass 2: Searching for valid key offset at re-sync position " + std::to_string(resync_pos)).c_str());

    if (resync_pos >= encrypted_bytes.size()) {
         LogMessage(LOG_WARNING_LEVEL, "[DEBUG] Pass 2 FAILED: Re-sync position is at or beyond end of data.");
         return false;
    }

    size_t slice_len = std::min(static_cast<size_t>(50), encrypted_bytes.size() - resync_pos);
    if (slice_len < END_MARKER.size()) { // Need at least enough data to check for the marker
         LogMessage(LOG_WARNING_LEVEL, "[DEBUG] Pass 2 FAILED: Not enough data for re-sync check.");
         return false;
    }
    std::vector<unsigned char> slice_for_offset_check(encrypted_bytes.begin() + resync_pos, encrypted_bytes.begin() + resync_pos + slice_len);

    for (size_t offset_pass2 = 0; offset_pass2 < key_len; ++offset_pass2) {
        size_t temp_key_idx = (resync_pos + offset_pass2) % key_len;
        std::vector<unsigned char> decrypted_slice = XorBytes(slice_for_offset_check, temp_key_idx);

        // Check if decrypted_slice starts with END_MARKER
        if (decrypted_slice.size() >= END_MARKER.size() &&
            std::equal(END_MARKER.begin(), END_MARKER.end(), decrypted_slice.begin()))
        {
            out_resync_key_idx = temp_key_idx;
            LogMessage(LOG_INFO_LEVEL, ("[DEBUG]   SUCCESS (Pass 2): Found valid re-sync key index: " + std::to_string(out_resync_key_idx)).c_str());
            return true;
        }
    }

    LogMessage(LOG_WARNING_LEVEL, "[DEBUG] Pass 2 FAILED: Could not find a valid re-sync offset.");
    return false;
}

// New robust decode function
std::string SaveGameManager::DecodeAndBypass(const std::vector<unsigned char>& encrypted_bytes) {
    std::vector<unsigned char> output_buffer;
    output_buffer.reserve(encrypted_bytes.size());
    size_t data_idx = 0;
    size_t key_idx = 0;
    size_t key_len = XOR_KEY.length();

    if (key_len == 0) {
        LogMessage(LOG_ERROR_LEVEL, "XOR_KEY length is zero. Aborting decode.");
        return "";
    }

    while (data_idx < encrypted_bytes.size()) {
        unsigned char decrypted_byte = encrypted_bytes[data_idx] ^ XOR_KEY[key_idx % key_len];
        output_buffer.push_back(decrypted_byte);

        bool trigger_found = false;
        for (const auto& trigger : TROUBLESOME_TRIGGERS) {
            if (output_buffer.size() >= trigger.size()) {
                // Check if output_buffer ends with trigger
                if (std::equal(trigger.begin(), trigger.end(), output_buffer.end() - trigger.size())) {
                    size_t field_start_pos = data_idx + 1;
                    size_t field_len = 0;
                    size_t new_key_idx = 0;

                    LogMessage(LOG_INFO_LEVEL, ("[DEBUG] Trigger found at byte " + std::to_string(data_idx) + ". Starting field analysis.").c_str());

                    if (FindFieldDetails(encrypted_bytes, field_start_pos, field_len, new_key_idx)) {
                        if (field_start_pos + field_len > encrypted_bytes.size()) {
                            LogMessage(LOG_WARNING_LEVEL, "Trigger found, but calculated field length exceeds data size. Aborting bypass.");
                            // This is bad, we should probably fail hard.
                            throw std::runtime_error("Trigger found, but field length calculation was invalid. Aborting.");
                        }

                        std::vector<unsigned char> field_bytes(encrypted_bytes.begin() + field_start_pos,
                                                               encrypted_bytes.begin() + field_start_pos + field_len);

                        // Rewind output buffer (remove trigger)
                        output_buffer.resize(output_buffer.size() - trigger.size());
                        // Re-add trigger
                        output_buffer.insert(output_buffer.end(), trigger.begin(), trigger.end());

                        // Add bypass string
                        std::string bypass_string = BYPASS_PREFIX + BytesToHex(field_bytes) + ":" + std::to_string(new_key_idx);
                        output_buffer.insert(output_buffer.end(), bypass_string.begin(), bypass_string.end());

                        // Jump data index and set new key index
                        data_idx = field_start_pos + field_len;
                        key_idx = new_key_idx;
                        trigger_found = true;
                    } else {
                        LogMessage(LOG_WARNING_LEVEL, "Trigger found, but could not determine field details. Aborting decode.");
                        throw std::runtime_error("Trigger found, but FindFieldDetails failed. Aborting.");
                    }
                    break; // Exit trigger loop
                }
            }
        } // end for triggers

        if (!trigger_found) {
            data_idx += 1;
            key_idx += 1;
        }
    } // end while

    // Convert final buffer to UTF-8 string
    return std::string(output_buffer.begin(), output_buffer.end());
}


// New robust encode function
std::vector<unsigned char> SaveGameManager::EncodeWithBypass(const std::string& utf8_json_string) {
    std::vector<unsigned char> output_bytes;
    output_bytes.reserve(utf8_json_string.length()); // Initial guess
    size_t key_len = XOR_KEY.length();
    size_t key_idx = 0;

    if (key_len == 0) {
        LogMessage(LOG_ERROR_LEVEL, "XOR_KEY length is zero. Aborting encode.");
        return output_bytes;
    }

    try {
        // Regex: BYPASSED_HEX::([a-fA-F0-9]+):(\d+)
        std::regex pattern(BYPASS_PREFIX + "([a-fA-F0-9]+):(\\d+)");

        auto it_begin = std::sregex_iterator(utf8_json_string.begin(), utf8_json_string.end(), pattern);
        auto it_end = std::sregex_iterator();

        std::string::const_iterator last_match_end = utf8_json_string.begin();

        for (std::sregex_iterator i = it_begin; i != it_end; ++i) {
            std::smatch match = *i;
            std::string::const_iterator match_start = utf8_json_string.begin() + match.position(0);

            // 1. Get the clean part *before* this match
            std::string clean_part_str(last_match_end, match_start);
            std::vector<unsigned char> clean_part_bytes(clean_part_str.begin(), clean_part_str.end());

            // 2. Encrypt and append the clean part
            std::vector<unsigned char> encrypted_clean_part = XorBytes(clean_part_bytes, key_idx);
            output_bytes.insert(output_bytes.end(), encrypted_clean_part.begin(), encrypted_clean_part.end());

            // 3. Update key_idx
            key_idx = (key_idx + clean_part_bytes.size()) % key_len;

            // 4. Get the bypassed data
            std::string hex_data = match.str(1);
            size_t new_key_idx = std::stoul(match.str(2));

            // 5. Append the raw bypassed bytes (un-encrypted)
            std::vector<unsigned char> raw_field_bytes = HexToBytes(hex_data);
            output_bytes.insert(output_bytes.end(), raw_field_bytes.begin(), raw_field_bytes.end());

            // 6. Set the new key_idx
            key_idx = new_key_idx;

            // 7. Update pointer for the next "clean part"
            last_match_end = utf8_json_string.begin() + match.position(0) + match.length(0);
        }

        // 8. Encrypt and append any remaining part after the last match
        std::string remaining_part_str(last_match_end, utf8_json_string.end());
        std::vector<unsigned char> remaining_part_bytes(remaining_part_str.begin(), remaining_part_str.end());

        std::vector<unsigned char> encrypted_remaining_part = XorBytes(remaining_part_bytes, key_idx);
        output_bytes.insert(output_bytes.end(), encrypted_remaining_part.begin(), encrypted_remaining_part.end());

    } catch (const std::regex_error& e) {
         LogMessage(LOG_ERROR_LEVEL, (std::string("Regex error during encode: ") + e.what()).c_str());
         throw; // Re-throw
    } catch (const std::exception& e) {
         LogMessage(LOG_ERROR_LEVEL, (std::string("Error during encode: ") + e.what()).c_str());
         throw; // Re-throw
    }
    
    return output_bytes;
}


// --- Zlib Decompression Implementation ---
std::string SaveGameManager::decompressZlib(const std::vector<unsigned char>& compressed_bytes) {
    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = static_cast<uInt>(compressed_bytes.size());
    strm.next_in = const_cast<Bytef*>(compressed_bytes.data());

    if (inflateInit(&strm) != Z_OK) {
        throw std::runtime_error("zlib inflateInit failed.");
    }

    std::string decompressed_str;
    const size_t CHUNK = 16384;
    std::vector<unsigned char> buffer(CHUNK);

    int ret;
    do {
        strm.avail_out = CHUNK;
        strm.next_out = buffer.data();
        ret = inflate(&strm, Z_NO_FLUSH);
        if (ret < 0 && ret != Z_BUF_ERROR) {
             inflateEnd(&strm);
             throw std::runtime_error("zlib inflate error: " + std::string(strm.msg ? strm.msg : "Unknown error"));
        }
        decompressed_str.append(reinterpret_cast<char*>(buffer.data()), CHUNK - strm.avail_out);
    } while (strm.avail_out == 0);

    inflateEnd(&strm);
    if (ret != Z_STREAM_END) {
        throw std::runtime_error("zlib inflate did not reach end of stream correctly.");
    }
    return decompressed_str;
}

// --- Zlib Compression Implementation ---
std::vector<unsigned char> SaveGameManager::compressZlib(const std::string& uncompressed_data) {
    z_stream strm;
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    strm.avail_in = static_cast<uInt>(uncompressed_data.size());
    strm.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(uncompressed_data.data()));

    if (deflateInit(&strm, Z_DEFAULT_COMPRESSION) != Z_OK) {
        throw std::runtime_error("zlib deflateInit failed.");
    }

    std::vector<unsigned char> compressed_bytes;
    const size_t CHUNK = 16384;
    std::vector<unsigned char> buffer(CHUNK);

    int ret;
    do {
        strm.avail_out = CHUNK;
        strm.next_out = buffer.data();
        ret = deflate(&strm, Z_FINISH);
        compressed_bytes.insert(compressed_bytes.end(), buffer.begin(), buffer.begin() + (CHUNK - strm.avail_out));
    } while (ret == Z_OK);

    deflateEnd(&strm);
    if (ret != Z_STREAM_END) {
        throw std::runtime_error("zlib deflate did not finish stream correctly: " + std::string(strm.msg ? strm.msg : "Unknown error"));
    }
    return compressed_bytes;
}


// --- LoadSaveFile Implementation (Updated) ---
bool SaveGameManager::LoadSaveFile(const std::string& filepath) {
    LogMessage(LOG_INFO_LEVEL, ("Attempting to load save file: " + filepath).c_str());
    m_isSaveFileLoaded = false;
    m_currentSaveFilePath = "";
    m_saveData = nlohmann::json();

    try {
        std::ifstream input_file(filepath, std::ios::binary);
        if (!input_file) {
            LogMessage(LOG_ERROR_LEVEL, ("Could not open save file for reading: " + filepath).c_str());
            return false;
        }
        std::vector<unsigned char> xor_encrypted_bytes((std::istreambuf_iterator<char>(input_file)), std::istreambuf_iterator<char>());
        input_file.close();
        LogMessage(LOG_INFO_LEVEL, ("Read " + std::to_string(xor_encrypted_bytes.size()) + " bytes from file.").c_str());

        // New logic:
        std::string json_str_utf8 = DecodeAndBypass(xor_encrypted_bytes);
        LogMessage(LOG_INFO_LEVEL, "Decoded save file with bypass logic.");

        m_saveData = nlohmann::json::parse(json_str_utf8);
        m_currentSaveFilePath = filepath;
        m_isSaveFileLoaded = true;
        LogMessage(LOG_INFO_LEVEL, "Save file JSON parsed successfully.");
        return true;

    } catch (const nlohmann::json::parse_error& e) {
        std::stringstream ss;
        ss << "0x" << std::hex << e.byte;
        LogMessage(LOG_ERROR_LEVEL, ("JSON parse error during load: " + std::string(e.what()) + " at offset " + ss.str()).c_str());
    } catch (const std::runtime_error& e) {
        LogMessage(LOG_ERROR_LEVEL, ("Runtime error during load: " + std::string(e.what())).c_str());
    } catch (const std::exception& e) {
        LogMessage(LOG_ERROR_LEVEL, ("An unknown error occurred during load: " + std::string(e.what())).c_str());
    }
    return false;
}

// --- WriteSaveFile Implementation (Updated) ---
bool SaveGameManager::WriteSaveFile(std::string& out_backup_filepath) {
    out_backup_filepath.clear();

    if (!m_isSaveFileLoaded || m_currentSaveFilePath.empty()) {
        LogMessage(LOG_WARNING_LEVEL, "Attempted to write save file, but no file is loaded or path is empty.");
        return false;
    }

    LogMessage(LOG_INFO_LEVEL, ("Attempting to write save file: " + m_currentSaveFilePath).c_str());
    try {
        std::filesystem::path original_path(m_currentSaveFilePath);
        std::filesystem::path backup_dir;

        WCHAR tempPathBuffer[MAX_PATH];
        DWORD length = GetTempPathW(MAX_PATH, tempPathBuffer);
        if (length == 0 || length > MAX_PATH) {
            LogMessage(LOG_ERROR_LEVEL, "Failed to get system temporary path. Falling back to save directory backup.");
            backup_dir = original_path.parent_path() / "backups";
        } else {
            std::filesystem::path base_temp_path = tempPathBuffer;
            backup_dir = base_temp_path / "DaveSaveEd_Backups";
        }
        std::filesystem::create_directories(backup_dir);

        auto now = std::chrono::system_clock::now();
        std::time_t now_c = std::chrono::system_clock::to_time_t(now);
        std::tm tm_buf;
        localtime_s(&tm_buf, &now_c);

        std::stringstream ss;
        ss << std::put_time(&tm_buf, "%Y%m%d_%H%M%S");
        std::string timestamp = ss.str();

        std::string backup_filename = original_path.stem().string() + "_" + timestamp + original_path.extension().string();
        std::filesystem::path backup_path = backup_dir / backup_filename;

        std::filesystem::copy(original_path, backup_path, std::filesystem::copy_options::overwrite_existing);
        LogMessage(LOG_INFO_LEVEL, ("Original save file backed up to: " + backup_path.string()).c_str());

        std::string json_to_write_utf8 = m_saveData.dump();
        LogMessage(LOG_INFO_LEVEL, "Serialized JSON data to UTF-8 string.");
        
        // New logic:
        std::vector<unsigned char> final_bytes = EncodeWithBypass(json_to_write_utf8);
        LogMessage(LOG_INFO_LEVEL, "Encoded save file with bypass logic.");

        std::ofstream output_file(m_currentSaveFilePath, std::ios::binary | std::ios::trunc);
        if (!output_file) {
            LogMessage(LOG_ERROR_LEVEL, ("Could not open save file for writing: " + m_currentSaveFilePath).c_str());
            return false;
        }
        output_file.write(reinterpret_cast<const char*>(final_bytes.data()), final_bytes.size());
        output_file.close();
        
        out_backup_filepath = backup_path.string();
        LogMessage(LOG_INFO_LEVEL, ("Modified save file written successfully to: " + m_currentSaveFilePath).c_str());
        return true;

    } catch (const std::exception& e) {
        LogMessage(LOG_ERROR_LEVEL, ("Error writing save file: " + std::string(e.what())).c_str());
        return false;
    }
}


// --- Player Stats Getters ---
long long SaveGameManager::GetGold() const {
    if (m_isSaveFileLoaded && m_saveData.contains("PlayerInfo") && m_saveData["PlayerInfo"].is_object() && m_saveData["PlayerInfo"].contains("m_Gold")) {
        return m_saveData["PlayerInfo"]["m_Gold"].get<long long>();
    }
    return 0;
}

long long SaveGameManager::GetBei() const {
    if (m_isSaveFileLoaded && m_saveData.contains("PlayerInfo") && m_saveData["PlayerInfo"].is_object() && m_saveData["PlayerInfo"].contains("m_Bei")) {
        return m_saveData["PlayerInfo"]["m_Bei"].get<long long>();
    }
    return 0;
}

long long SaveGameManager::GetArtisansFlame() const {
    if (m_isSaveFileLoaded && m_saveData.contains("PlayerInfo") && m_saveData["PlayerInfo"].is_object() && m_saveData["PlayerInfo"].contains("m_ChefFlame")) {
        return m_saveData["PlayerInfo"]["m_ChefFlame"].get<long long>();
    }
    return 0;
}

long long SaveGameManager::GetFollowerCount() const {
    if (m_isSaveFileLoaded && m_saveData.contains("SNSInfo") && m_saveData["SNSInfo"].is_object() && m_saveData["SNSInfo"].contains("m_Follow_Count")) {
        return m_saveData["SNSInfo"]["m_Follow_Count"].get<long long>();
    }
    return 0;
}


// --- Player Stats Setters ---
void SaveGameManager::SetGold(long long value) {
    if (m_isSaveFileLoaded && m_saveData.contains("PlayerInfo") && m_saveData["PlayerInfo"].is_object()) {
        m_saveData["PlayerInfo"]["m_Gold"] = std::min(value, SAVE_MAX_CURRENCY);
        LogMessage(LOG_INFO_LEVEL, ("Gold set to: " + std::to_string(m_saveData["PlayerInfo"]["m_Gold"].get<long long>())).c_str());
    } else {
        LogMessage(LOG_WARNING_LEVEL, "Attempted to set gold, but PlayerInfo section not found or invalid.");
    }
}

void SaveGameManager::SetBei(long long value) {
    if (m_isSaveFileLoaded && m_saveData.contains("PlayerInfo") && m_saveData["PlayerInfo"].is_object()) {
        m_saveData["PlayerInfo"]["m_Bei"] = std::min(value, SAVE_MAX_CURRENCY);
        LogMessage(LOG_INFO_LEVEL, ("Bei set to: " + std::to_string(m_saveData["PlayerInfo"]["m_Bei"].get<long long>())).c_str());
    } else {
        LogMessage(LOG_WARNING_LEVEL, "Attempted to set bei, but PlayerInfo section not found or invalid.");
    }
}

void SaveGameManager::SetArtisansFlame(long long value) {
    if (m_isSaveFileLoaded && m_saveData.contains("PlayerInfo") && m_saveData["PlayerInfo"].is_object()) {
        m_saveData["PlayerInfo"]["m_ChefFlame"] = std::min(value, SAVE_MAX_CURRENCY);
        LogMessage(LOG_INFO_LEVEL, ("Artisan's Flame set to: " + std::to_string(m_saveData["PlayerInfo"]["m_ChefFlame"].get<long long>())).c_str());
    } else {
        LogMessage(LOG_WARNING_LEVEL, "Attempted to set artisan's flame, but PlayerInfo section not found or invalid.");
    }
}

void SaveGameManager::SetFollowerCount(long long value) {
    if (m_isSaveFileLoaded && m_saveData.contains("SNSInfo") && m_saveData["SNSInfo"].is_object()) {
        m_saveData["SNSInfo"]["m_Follow_Count"] = value;
        LogMessage(LOG_INFO_LEVEL, ("Follower count set to: " + std::to_string(m_saveData["SNSInfo"]["m_Follow_Count"].get<long long>())).c_str());
    } else {
        LogMessage(LOG_WARNING_LEVEL, "Attempted to set follower count, but SNSInfo section not found or invalid.");
    }
}


// Helper function to determine the target count based on the item's MaxCount from DB
static int GetDesiredMaxCountForTier(int item_db_max_count) {
    if (item_db_max_count == 1) {
        return 0;
    } else if (item_db_max_count == 99) {
        return 66;
    } else if (item_db_max_count == 999) {
        return 666;
    } else if (item_db_max_count >= 9999) {
        return 6666;
    } else {
        LogMessage(LOG_WARNING_LEVEL, ("Unhandled MaxCount tier encountered: " + std::to_string(item_db_max_count) + ". Skipping item.").c_str());
        return 0;
    }
}

// --- MaxOwnIngredients Implementation ---
void SaveGameManager::MaxOwnIngredients(sqlite3* db) {
    if (!m_isSaveFileLoaded || !m_saveData.contains("Ingredients") || !m_saveData["Ingredients"].is_object()) {
        LogMessage(LOG_WARNING_LEVEL, "No save file loaded or 'Ingredients' section not found/invalid for MaxOwnIngredients.");
        return;
    }
    if (!db) {
        LogMessage(LOG_ERROR_LEVEL, "Database handle (g_refDb) is null for MaxOwnIngredients.");
        return;
    }

    nlohmann::json& ingredients_json_map = m_saveData["Ingredients"];
    int updated_count = 0;
    int skipped_count = 0;

    sqlite3_stmt *stmt = nullptr;
    std::string sql = "SELECT MaxCount FROM Items WHERE ItemDataID = ?;";
    int rc_prepare = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL);
    if (rc_prepare != SQLITE_OK) {
        LogMessage(LOG_ERROR_LEVEL, ("SQL prepare failed for MaxOwnIngredients: " + std::string(sqlite3_errmsg(db))).c_str());
        return;
    }

    for (auto it = ingredients_json_map.begin(); it != ingredients_json_map.end(); ++it) {
        if (it.value().contains("ingredientsID") && it.value()["ingredientsID"].is_number_integer()) {
            int ingredients_id = it.value()["ingredientsID"].get<int>();

            sqlite3_reset(stmt);
            sqlite3_bind_int(stmt, 1, ingredients_id);

            int max_count_from_db = 0;
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                max_count_from_db = sqlite3_column_int(stmt, 0);
            } else {
                LogMessage(LOG_WARNING_LEVEL, ("MaxCount not found for existing ingredient ID: " + std::to_string(ingredients_id) + " in Items table. Skipping update.").c_str());
                skipped_count++;
                continue;
            }

            int target_count = GetDesiredMaxCountForTier(max_count_from_db);

            if (target_count > 0) {
                it.value()["count"] = target_count;
                updated_count++;
            } else {
                LogMessage(LOG_INFO_LEVEL, ("Skipping owned ingredient ID " + std::to_string(ingredients_id) + " with MaxCount " + std::to_string(max_count_from_db) + " as per tier rules.").c_str());
                skipped_count++;
            }
        } else {
            LogMessage(LOG_WARNING_LEVEL, ("Skipping ingredient entry without valid 'ingredientsID': " + it.key() + ". Malformed entry.").c_str());
            skipped_count++;
        }
    }

    sqlite3_finalize(stmt);
    LogMessage(LOG_INFO_LEVEL, ("MaxOwnIngredients: Updated " + std::to_string(updated_count) + " owned ingredients. Skipped " + std::to_string(skipped_count) + " ingredients.").c_str());
}

// --- SQLite Callback for batch querying ingredients (for MaxAllIngredients) ---
static int callbackGetAllIngredients(void *data, int argc, char **argv, char **azColName){
    std::vector<std::map<std::string, int>>* results = static_cast<std::vector<std::map<std::string, int>>*>(data);
    std::map<std::string, int> row;
    for(int i = 0; i < argc; i++){
        row[azColName[i]] = argv[i] ? std::stoi(argv[i]) : 0;
    }
    results->push_back(row);
    return 0;
}

// --- MaxAllIngredients Implementation (Updated for DLC Awareness) ---
void SaveGameManager::MaxAllIngredients(sqlite3* db) {
    if (!m_isSaveFileLoaded) {
        LogMessage(LOG_WARNING_LEVEL, "No save file loaded for MaxAllIngredients.");
        return;
    }
    if (!db) {
        LogMessage(LOG_ERROR_LEVEL, "Database handle (g_refDb) is null for MaxAllIngredients.");
        return;
    }

    // --- DLC Check Setup ---
    // 1. Define the mapping from DLCType in DB to DLC ID in save file
    const std::map<int, int> dlcTypeToIdMap = {
        {1, 14252001}, // Dredge
        {3, 14252201}, // Godzilla
        {5, 14252401}  // Ichiban
    };

    // 2. Get the set of installed DLC IDs from the save file
    std::set<int> installedDlcIds;
    if (m_saveData.contains("GameInfo") && m_saveData["GameInfo"].is_object() && m_saveData["GameInfo"].contains("installedDLCs") && m_saveData["GameInfo"]["installedDLCs"].is_array()) {
        for (const auto& dlc_id : m_saveData["GameInfo"]["installedDLCs"]) {
            if (dlc_id.is_number_integer()) {
                installedDlcIds.insert(dlc_id.get<int>());
            }
        }
        LogMessage(LOG_INFO_LEVEL, ("Found " + std::to_string(installedDlcIds.size()) + " installed DLCs in save file.").c_str());
    } else {
        LogMessage(LOG_WARNING_LEVEL, "Could not find 'installedDLCs' array in save file. Assuming no DLCs are owned.");
    }
    // --- End DLC Check Setup ---


    if (!m_saveData.contains("Ingredients") || !m_saveData["Ingredients"].is_object()) {
        LogMessage(LOG_INFO_LEVEL, "Creating empty 'Ingredients' section in save data.");
        m_saveData["Ingredients"] = nlohmann::json::object();
    }

    nlohmann::json& ingredients_json_map = m_saveData["Ingredients"];

    std::string default_lastGainTime = "04/01/2025 12:34:56";
    std::string default_lastGainGameTime = "10/03/2022 08:30:52";

    if (!ingredients_json_map.empty()) {
        auto& first_item_value = ingredients_json_map.begin().value();
        if (first_item_value.contains("lastGainTime") && first_item_value["lastGainTime"].is_string()) {
            default_lastGainTime = first_item_value["lastGainTime"].get<std::string>();
        }
        if (first_item_value.contains("lastGainGameTime") && first_item_value["lastGainGameTime"].is_string()) {
            default_lastGainGameTime = first_item_value["lastGainGameTime"].get<std::string>();
        }
    }
    LogMessage(LOG_INFO_LEVEL, ("Using timestamps '" + default_lastGainTime + "' / '" + default_lastGainGameTime + "' for new ingredients.").c_str());

    std::vector<std::map<std::string, int>> all_db_ingredients;
    // 3. Modify SQL query to fetch DLCType
    std::string sql_query = R"(
        SELECT
            I.TID AS ingredientsID_for_save_file_key,
            T.TID AS parentID,
            T.MaxCount,
            T.DLCType
        FROM
            Ingredients AS I
        JOIN
            Items AS T
        ON
            I.TID = T.ItemDataID;
    )";

    char* zErrMsg = nullptr;
    int rc = sqlite3_exec(db, sql_query.c_str(), callbackGetAllIngredients, &all_db_ingredients, &zErrMsg);
    if (rc != SQLITE_OK) {
        LogMessage(LOG_ERROR_LEVEL, ("SQL error getting all ingredients: " + std::string(zErrMsg)).c_str());
        sqlite3_free(zErrMsg);
        return;
    }
    LogMessage(LOG_INFO_LEVEL, ("Retrieved " + std::to_string(all_db_ingredients.size()) + " potential ingredients from database.").c_str());

    int updated_count = 0;
    int added_count = 0;
    int skipped_count = 0;
    int skipped_dlc_count = 0; // New counter for unowned DLC items

    // 4. Update processing loop with DLC check
    for (const auto& db_ingredient : all_db_ingredients) {
        if (!db_ingredient.count("ingredientsID_for_save_file_key") ||
            !db_ingredient.count("parentID") ||
            !db_ingredient.count("MaxCount") ||
            !db_ingredient.count("DLCType")) { // Check for new field
            LogMessage(LOG_WARNING_LEVEL, "Skipping database ingredient entry due to missing required fields.");
            skipped_count++;
            continue;
        }

        // --- DLC Ownership Check ---
        int dlc_type_from_db = db_ingredient.at("DLCType");
        auto it_dlc_map = dlcTypeToIdMap.find(dlc_type_from_db);

        if (it_dlc_map != dlcTypeToIdMap.end()) {
            // This item is part of a known DLC. Check if the user has it installed.
            int required_dlc_id = it_dlc_map->second;
            if (installedDlcIds.find(required_dlc_id) == installedDlcIds.end()) {
                // User does not own this DLC, so skip the item.
                LogMessage(LOG_INFO_LEVEL, ("Skipping ingredient ID " + std::to_string(db_ingredient.at("ingredientsID_for_save_file_key")) + " from unowned DLC Type " + std::to_string(dlc_type_from_db)).c_str());
                skipped_dlc_count++;
                continue;
            }
        }
        // If we reach here, it's either a base game item (not in map) or a DLC item the user owns.
        // --- End DLC Ownership Check ---

        int ingredients_id_from_db = db_ingredient.at("ingredientsID_for_save_file_key");
        int parent_id_from_db = db_ingredient.at("parentID");
        int max_count_from_db = db_ingredient.at("MaxCount");

        int target_count = GetDesiredMaxCountForTier(max_count_from_db);

        if (target_count == 0) {
            LogMessage(LOG_INFO_LEVEL, ("Skipping ingredient ID " + std::to_string(ingredients_id_from_db) + " with MaxCount " + std::to_string(max_count_from_db) + " from database as per tier rules.").c_str());
            skipped_count++;
            continue;
        }

        std::string ingredient_key = std::to_string(ingredients_id_from_db);

        if (ingredients_json_map.contains(ingredient_key)) {
            ingredients_json_map[ingredient_key]["count"] = target_count;
            updated_count++;
        } else {
            nlohmann::json new_ingredient_entry;
            new_ingredient_entry["ingredientsID"] = ingredients_id_from_db;
            new_ingredient_entry["level"] = 1;
            new_ingredient_entry["parentID"] = parent_id_from_db;
            new_ingredient_entry["count"] = target_count;
            new_ingredient_entry["branchCount"] = 0;
            new_ingredient_entry["lastGainTime"] = default_lastGainTime;
            new_ingredient_entry["lastGainGameTime"] = default_lastGainGameTime;
            new_ingredient_entry["isNew"] = true;
            new_ingredient_entry["placeTagMask"] = 1;

            ingredients_json_map[ingredient_key] = new_ingredient_entry;
            added_count++;
        }
    }
    
    // 5. Update final log message
    std::stringstream log_summary;
    log_summary << "MaxAllIngredients: Updated " << updated_count
                << " existing, added " << added_count
                << " new, skipped " << skipped_count
                << " (malformed/rules), skipped " << skipped_dlc_count
                << " (unowned DLC) ingredients.";
    LogMessage(LOG_INFO_LEVEL, log_summary.str().c_str());
}

// --- Static Helper: GetDefaultSaveGameDirectoryAndLatestFile Implementation ---
std::filesystem::path SaveGameManager::GetDefaultSaveGameDirectoryAndLatestFile(std::string& latestSaveFileName) {
    PWSTR pszPath = NULL;
    std::filesystem::path baseSavePath;

    HRESULT hr = SHGetKnownFolderPath(FOLDERID_LocalAppDataLow, 0, NULL, &pszPath);

    if (SUCCEEDED(hr)) {
        baseSavePath = pszPath;
        CoTaskMemFree(pszPath);

        baseSavePath /= L"nexon";
        baseSavePath /= L"DAVE THE DIVER";
        baseSavePath /= L"SteamSData";
    } else {
        LogMessage(LOG_ERROR_LEVEL, "Failed to get AppData LocalLow path using SHGetKnownFolderPath.");
        const char* localAppDataEnv = getenv("LOCALAPPDATA");
        if (localAppDataEnv) {
            baseSavePath = localAppDataEnv;
            baseSavePath = baseSavePath.parent_path() / "LocalLow";
            baseSavePath /= L"nexon";
            baseSavePath /= L"DAVE THE DIVER";
            baseSavePath /= L"SteamSData";
        } else {
            LogMessage(LOG_ERROR_LEVEL, "LOCALAPPDATA environment variable not found either.");
            return {};
        }
    }

    std::filesystem::path steamIDPath;

    if (std::filesystem::exists(baseSavePath) && std::filesystem::is_directory(baseSavePath)) {
        for (const auto& entry : std::filesystem::directory_iterator(baseSavePath)) {
            if (entry.is_directory()) {
                std::string folderName = entry.path().filename().string();
                if (!folderName.empty() && std::all_of(folderName.begin(), folderName.end(), ::isdigit)) {
                    steamIDPath = entry.path();
                    LogMessage(LOG_INFO_LEVEL, (std::string("Found SteamID folder: ") + steamIDPath.string()).c_str());
                    break;
                }
            }
        }
    }

    if (steamIDPath.empty()) {
        LogMessage(LOG_ERROR_LEVEL, (std::string("Could not find a SteamID folder under: ") + baseSavePath.string()).c_str());
        steamIDPath = baseSavePath;
    }

    std::filesystem::file_time_type lastWriteTime;
    std::filesystem::path mostRecentSaveFile;

    if (std::filesystem::exists(steamIDPath) && std::filesystem::is_directory(steamIDPath)) {
        for (const auto& entry : std::filesystem::directory_iterator(steamIDPath)) {
            if (entry.is_regular_file()) {
                std::string fileName = entry.path().filename().string();
                
                bool is_autosave = (fileName.length() > 10 && fileName.substr(0, 8) == "GameSave" && fileName.substr(fileName.length() - 7) == "_GD.sav");
                bool is_manual_save = (fileName.length() > 2 && fileName.substr(0, 2) == "m_" && fileName.substr(fileName.length() - 4) == ".sav");

                if (is_autosave || is_manual_save) {
                    try {
                        auto fileTime = std::filesystem::last_write_time(entry.path());
                        if (mostRecentSaveFile.empty() || fileTime > lastWriteTime) {
                            lastWriteTime = fileTime;
                            mostRecentSaveFile = entry.path();
                        }
                    } catch (const std::filesystem::filesystem_error& e) {
                        LogMessage(LOG_ERROR_LEVEL, (std::string("Error getting write time for ") + fileName + ": " + e.what()).c_str());
                    }
                }
            }
        }
    }

    if (!mostRecentSaveFile.empty()) {
        latestSaveFileName = mostRecentSaveFile.string();
        LogMessage(LOG_INFO_LEVEL, (std::string("Identified most recent save file: ") + latestSaveFileName).c_str());
    } else {
        LogMessage(LOG_INFO_LEVEL, (std::string("No valid save files found in " + steamIDPath.string())).c_str());
        latestSaveFileName.clear();
    }

    return steamIDPath;
}
//END OF SaveGameManager.cpp

