#!/usr/bin/env python3
#convert save files for Dave the Diver between xor encoded and json encoded
#
# Copyright (c) 2025 FNGarvin (184324400+FNGarvin@users.noreply.github.com)
# All rights reserved.
#
# This software is provided 'as-is', without any express or implied
# warranty. In no event will the authors be held liable for any damages
# arising from the use of this software.
#
# Permission is granted to anyone to use this software for any purpose,
# including commercial applications, and to alter it and redistribute it
# freely, subject to the following restrictions:
#
# 1. The origin of this software must not be misrepresented; you must not
#    claim that you wrote the original software. If you use this software
#    in a product, an acknowledgment in the product documentation would be
#    appreciated but is not required.
# 2. Altered source versions must be plainly marked as such, and must not be
#    misrepresented as being the original software.
# 3. This notice may not be removed or altered from any source distribution.

import sys
import os
import json
import binascii
import filecmp

# The XOR key used for encryption and decryption.
# It's defined as a bytes literal for direct use in XOR operations.
XOR_KEY = b"GameData"

def xor_bytes_hybrid(data_bytes, key_bytes):
    """
    Performs a repeating-key XOR using a hybrid algorithm that can handle
    multiple distinct bugs found in the game's save files.

    It detects a high-bit character, then inspects the following bytes to
    determine which bug has been encountered, and applies the correct
    key synchronization fix for that specific case.
    """
    key_len = len(key_bytes)
    output_bytes = bytearray()
    
    data_idx = 0
    key_idx = 0

    while data_idx < len(data_bytes):
        # Peek ahead by decrypting the current byte to check its value.
        peek_key_byte = key_bytes[key_idx % key_len]
        decrypted_peek = data_bytes[data_idx] ^ peek_key_byte

        # Check if the decrypted byte would have the high bit set.
        if decrypted_peek >= 128:
            # --- Special Block Case ---
            # A high-bit character was detected. Now we need to determine
            # which bug we've encountered by counting consecutive high-bit bytes.
            block_len_check = 4
            block_end_check = min(data_idx + block_len_check, len(data_bytes))
            
            high_bit_count = 0
            for i in range(block_end_check - data_idx):
                check_key_byte = key_bytes[(key_idx + i) % key_len]
                decrypted_check = data_bytes[data_idx + i] ^ check_key_byte
                if decrypted_check >= 128:
                    high_bit_count += 1

            # Apply the correct processing rules based on the bug pattern.
            if high_bit_count == 4:
                # This is the "UTF-8 sequence" bug from brit.sav.
                # Per user instruction, process 6 bytes and advance key by 4.
                block_len_process = 6
                key_advancement = 4
            else:
                # Default to the "legacy codepage" bug fix from orig.sav.
                # Process 4 data bytes, advance key by 3 (slip of 1).
                block_len_process = 4
                key_advancement = 3

            # Process the determined block length by XORing each byte.
            block_end_process = min(data_idx + block_len_process, len(data_bytes))
            for i in range(block_end_process - data_idx):
                current_key_byte = key_bytes[(key_idx + i) % key_len]
                output_bytes.append(data_bytes[data_idx + i] ^ current_key_byte)
            
            # Advance the data pointer by the number of bytes we processed.
            data_idx += (block_end_process - data_idx)
            # Advance the key index by the amount we determined was correct for this bug.
            key_idx += key_advancement
        else:
            # --- Standard XOR Case ---
            # The decrypted byte is standard ASCII, so we append it.
            output_bytes.append(decrypted_peek)
            data_idx += 1
            key_idx += 1
            
    return bytes(output_bytes)


def encode_json_to_sav(json_filepath, is_test=False):
    """
    Reads a JSON file, strips whitespace, XORs it with the hybrid algorithm,
    and saves the result as a raw binary .sav file.
    """
    try:
        # 1. Read the JSON file using a permissive encoding.
        with open(json_filepath, 'r', encoding='latin-1') as f:
            # For the test, the file is already compact. For normal use, parse and dump.
            if is_test:
                compact_json_string = f.read()
            else:
                json_data = json.load(f)
                compact_json_string = json.dumps(json_data, separators=(',', ':'))
        
        # 3. Encode the compact JSON string to bytes using the same encoding.
        json_bytes = compact_json_string.encode('latin-1')
        
        # 4. Perform the XOR hash operation using the new hybrid logic.
        hashed_bytes = xor_bytes_hybrid(json_bytes, XOR_KEY)
        
        # 5. Determine the output filename.
        base_name = os.path.splitext(json_filepath)[0]
        output_filepath = base_name + ".resave" if is_test else base_name + ".sav"
        
        # 6. Prompt for overwrite if not in test mode.
        if not is_test and os.path.exists(output_filepath):
            response = input(f"'{output_filepath}' already exists. Overwrite? (y/N): ").lower()
            if response != 'y':
                print(f"Skipping encoding for '{json_filepath}'.")
                return

        # 7. Write the raw binary hashed data to the output file.
        with open(output_filepath, 'wb') as f:
            f.write(hashed_bytes)
        
        if not is_test:
            print(f"Successfully encoded '{json_filepath}' to '{output_filepath}'.")

    except FileNotFoundError:
        print(f"Error: Input file not found at '{json_filepath}'.", file=sys.stderr)
    except json.JSONDecodeError:
        print(f"Error: Invalid JSON format in '{json_filepath}'. Please ensure it's valid JSON.", file=sys.stderr)
    except Exception as e:
        print(f"An unexpected error occurred during encoding '{json_filepath}': {e}", file=sys.stderr)

def decode_sav_to_json(sav_filepath, pretty_print=True):
    """
    Reads a .sav file, decodes it, and saves the result as a .json file.
    """
    try:
        # 1. Read the .sav file as raw binary data.
        with open(sav_filepath, 'rb') as f:
            hashed_bytes = f.read()
        
        # 2. Perform the XOR decode operation using the new hybrid logic.
        decoded_bytes = xor_bytes_hybrid(hashed_bytes, XOR_KEY)
        
        # 3. Decode the bytes back to a string using a permissive encoding.
        decoded_json_string = decoded_bytes.decode('latin-1')
        
        # 4. Determine the output filename.
        base_name = os.path.splitext(sav_filepath)[0]
        output_filepath = base_name + ".json"
        
        # 5. Prompt for overwrite if not pretty-printing for a test.
        if pretty_print and os.path.exists(output_filepath):
            response = input(f"'{output_filepath}' already exists. Overwrite? (y/N): ").lower()
            if response != 'y':
                print(f"Skipping decoding for '{sav_filepath}'.")
                return

        # 6. Save the output, pretty-printing if requested.
        if pretty_print:
            try:
                parsed_json = json.loads(decoded_json_string)
                with open(output_filepath, 'w', encoding='latin-1') as f:
                    json.dump(parsed_json, f, indent=4, ensure_ascii=False)
                print(f"Successfully decoded '{sav_filepath}' to '{output_filepath}' (pretty-printed).")
            except json.JSONDecodeError:
                print(f"Warning: Decoded data from '{sav_filepath}' is not valid JSON. Saving as raw text.", file=sys.stderr)
                with open(output_filepath, 'w', encoding='latin-1') as f:
                    f.write(decoded_json_string)
                print(f"Successfully decoded '{sav_filepath}' to '{output_filepath}' (raw text).")
        else:
            # Save the raw, non-pretty-printed JSON for the test.
            with open(output_filepath, 'w', encoding='latin-1') as f:
                f.write(decoded_json_string)

    except FileNotFoundError:
        print(f"Error: Input file not found at '{sav_filepath}'.", file=sys.stderr)
    except Exception as e:
        print(f"An unexpected error occurred during decoding '{sav_filepath}': {e}", file=sys.stderr)

def test_reversibility(sav_filepath):
    """
    Performs a round-trip test (sav -> json -> resave) and compares the result.
    """
    print(f"--- Running reversibility test on '{sav_filepath}' ---")
    base_name = os.path.splitext(sav_filepath)[0]
    json_filepath = base_name + ".json"
    resave_filepath = base_name + ".resave"

    # 1. Decode sav to raw json (no pretty-printing)
    print("Step 1: Decoding .sav to raw .json...")
    decode_sav_to_json(sav_filepath, pretty_print=False)

    # 2. Re-encode raw json to .resave
    print("Step 2: Re-encoding raw .json to .resave...")
    encode_json_to_sav(json_filepath, is_test=True)

    # 3. Compare original .sav with .resave
    print("Step 3: Comparing original .sav with the new .resave file...")
    are_identical = filecmp.cmp(sav_filepath, resave_filepath, shallow=False)

    if are_identical:
        print("\nSUCCESS: The files are identical. The algorithm is reversible.")
    else:
        print("\nFAILURE: The files are NOT identical. The algorithm is not perfectly reversible.")

    # 4. Clean up intermediate files
    print(f"Cleaning up temporary file (.resave)... The decoded file '{json_filepath}' has been kept for inspection.")
    # os.remove(json_filepath) # Keep the json file as requested
    os.remove(resave_filepath)
    print("--- Test complete ---")


def display_usage():
    """
    Displays the script's usage message.
    """
    script_name = os.path.basename(sys.argv[0])
    print(f"Usage: {script_name} <file1.json | file1.sav> [file2 ...]")
    print(f"   or: {script_name} --test <file.sav>")
    print("\nThis script encodes/decodes Dave the Diver save files.")
    print("The --test flag performs a round-trip conversion to check for perfect reversibility.")

def main():
    """
    Main function to parse command-line arguments and call appropriate functions.
    """
    if len(sys.argv) < 2:
        display_usage()
        sys.exit(1)

    # Check for test mode
    if sys.argv[1] == '--test':
        if len(sys.argv) != 3 or not sys.argv[2].lower().endswith('.sav'):
            print("\nError: --test flag requires a single .sav file as an argument.", file=sys.stderr)
            display_usage()
            sys.exit(1)
        test_reversibility(sys.argv[2])
        return

    # Normal encode/decode mode
    for filepath in sys.argv[1:]:
        if not os.path.exists(filepath):
            print(f"Warning: File not found: '{filepath}'. Skipping.", file=sys.stderr)
            continue

        filename, ext = os.path.splitext(filepath)
        ext = ext.lower()

        if ext == '.json':
            encode_json_to_sav(filepath)
        elif ext == '.sav':
            decode_sav_to_json(filepath)
        else:
            print(f"Warning: Unsupported file extension '{ext}' for '{filepath}'. Skipping.", file=sys.stderr)

# Entry point of the script.
if __name__ == "__main__":
    main()

# END OF encdec.py

