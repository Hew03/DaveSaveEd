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
#include <iomanip>       // For std::put_time
#include <sstream>       // For std::stringstream
#include <algorithm>     // For std::all_of, and std::min/max
#include "sqlite3.h"     // Required for sqlite3* parameter in MaxAllIngredients
#include "Logger.h"      // For LogMessage
#include "json.hpp"      // Corrected include for nlohmann/json
#include <vector>        // Required for std::vector
#include <map>           // Required for std::map
#include <string>        // Required for std::string
#include <stdexcept>     // Required for std::runtime_error
#include <filesystem>    // Required for std::filesystem::path, create_directories, copy, last_write_time

// --- Global Constants for SaveGameManager ---
// Note: XOR_KEY is defined in SaveGameManager.h
// Use long long for currency to avoid overflow
const long long SAVE_MAX_CURRENCY = 999999999LL;

// Constructor: Initializes the SaveGameManager instance.
SaveGameManager::SaveGameManager() : m_isSaveFileLoaded(false) {
    LogMessage(LOG_INFO_LEVEL, "SaveGameManager initialized.");
}

// Destructor: Cleans up resources used by the SaveGameManager.
SaveGameManager::~SaveGameManager() {
    LogMessage(LOG_INFO_LEVEL, "SaveGameManager shutting down.");
}

// --- HYBRID XOR FUNCTION (Updated with Triage Logic and vector<unsigned char>) ---
std::vector<unsigned char> SaveGameManager::XORHybrid(const std::vector<unsigned char>& data, const std::string& key) {
    std::vector<unsigned char> output;
    output.reserve(data.size());

    size_t data_idx = 0;
    size_t key_idx = 0;
    size_t key_len = key.length();

    while (data_idx < data.size()) {
        unsigned char peek_key_byte = key[key_idx % key_len];
        unsigned char decrypted_peek = data[data_idx] ^ peek_key_byte;

        if (decrypted_peek >= 128) {
            // --- Special Block Case ---
            // Check the next 4 bytes to determine the bug pattern
            size_t block_len_check = 4;
            size_t block_end_check = std::min(data_idx + block_len_check, data.size());
            
            int high_bit_count = 0;
            for (size_t i = 0; i < (block_end_check - data_idx); ++i) {
                unsigned char check_key_byte = key[(key_idx + i) % key_len];
                unsigned char decrypted_check = data[data_idx + i] ^ check_key_byte;
                if (decrypted_check >= 128) {
                    ++high_bit_count;
                }
            }

            // Determine the correct data block length and key advancement based on the pattern
            size_t block_len_process;
            size_t key_advancement;

            if (high_bit_count == 4) {
                // This is the "UTF-8 sequence" bug. Process 6 data bytes, advance key by 4.
                block_len_process = 6;
                key_advancement = 4;
            } else {
                // This is the "legacy codepage" bug. Process 4 data bytes, advance key by 3.
                block_len_process = 4;
                key_advancement = 3;
            }

            LogMessage(LOG_INFO_LEVEL, ("High-bit block detected (" + std::to_string(high_bit_count) + "/4). Processing " + std::to_string(block_len_process) + " bytes, advancing key by " + std::to_string(key_advancement) + ".").c_str());
            
            // Process the determined number of bytes
            size_t block_end_process = std::min(data_idx + block_len_process, data.size());
            for (size_t i = 0; i < (block_end_process - data_idx); ++i) {
                unsigned char current_key_byte = key[(key_idx + i) % key_len];
                output.push_back(data[data_idx + i] ^ current_key_byte);
            }

            data_idx += (block_end_process - data_idx);
            key_idx += key_advancement;

        } else {
            // --- Standard XOR Case ---
            output.push_back(decrypted_peek);
            data_idx += 1;
            key_idx += 1;
        }
    }
    return output;
}

// --- ENCODING HELPERS ---
std::string SaveGameManager::Latin1ToUTF8(const std::vector<unsigned char>& latin1_bytes) {
    std::string utf8_str;
    utf8_str.reserve(latin1_bytes.size());

    for (unsigned char c : latin1_bytes) {
        if (c < 0x80) {
            utf8_str += c;
        } else {
            utf8_str += (0xC0 | (c >> 6));
            utf8_str += (0x80 | (c & 0x3F));
        }
    }
    return utf8_str;
}

std::vector<unsigned char> SaveGameManager::UTF8ToLatin1(const std::string& utf8_str) {
    std::vector<unsigned char> latin1_bytes;
    latin1_bytes.reserve(utf8_str.length());

    for (size_t i = 0; i < utf8_str.length(); ) {
        unsigned char c = utf8_str[i];
        if (c < 0x80) {
            latin1_bytes.push_back(c);
            i++;
        } else if ((c & 0xE0) == 0xC0) {
            if (i + 1 < utf8_str.length()) {
                unsigned char c2 = utf8_str[i + 1];
                unsigned char original_char = ((c & 0x1F) << 6) | (c2 & 0x3F);
                latin1_bytes.push_back(original_char);
                i += 2;
            } else {
                latin1_bytes.push_back('?');
                i++;
            }
        } else {
            latin1_bytes.push_back('?');
            i++;
        }
    }
    return latin1_bytes;
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

        std::vector<unsigned char> json_bytes_latin1 = XORHybrid(xor_encrypted_bytes, XOR_KEY);
        LogMessage(LOG_INFO_LEVEL, "XOR decrypted save file.");

        std::string json_str_utf8 = Latin1ToUTF8(json_bytes_latin1);
        LogMessage(LOG_INFO_LEVEL, "Converted byte stream to UTF-8 for parsing.");

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
        
        std::vector<unsigned char> json_bytes_latin1 = UTF8ToLatin1(json_to_write_utf8);
        LogMessage(LOG_INFO_LEVEL, "Converted UTF-8 string to Latin-1 bytes for encryption.");

        std::vector<unsigned char> final_bytes = XORHybrid(json_bytes_latin1, XOR_KEY);
        LogMessage(LOG_INFO_LEVEL, "XOR encrypted JSON data.");

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

// --- MaxAllIngredients Implementation ---
void SaveGameManager::MaxAllIngredients(sqlite3* db) {
    if (!m_isSaveFileLoaded) {
        LogMessage(LOG_WARNING_LEVEL, "No save file loaded for MaxAllIngredients.");
        return;
    }
    if (!db) {
        LogMessage(LOG_ERROR_LEVEL, "Database handle (g_refDb) is null for MaxAllIngredients.");
        return;
    }

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
    std::string sql_query = R"(
        SELECT
            I.TID AS ingredientsID_for_save_file_key,
            T.TID AS parentID,
            T.MaxCount
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

    for (const auto& db_ingredient : all_db_ingredients) {
        if (!db_ingredient.count("ingredientsID_for_save_file_key") ||
            !db_ingredient.count("parentID") ||
            !db_ingredient.count("MaxCount")) {
            LogMessage(LOG_WARNING_LEVEL, "Skipping database ingredient entry due to missing required fields.");
            skipped_count++;
            continue;
        }

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
    LogMessage(LOG_INFO_LEVEL, ("MaxAllIngredients: Updated " + std::to_string(updated_count) + " existing, added " + std::to_string(added_count) + " new, skipped " + std::to_string(skipped_count) + " ingredients.").c_str());
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

