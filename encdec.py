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
#    misrepresented as being the original software#
# 3. This notice may not be removed or altered from any source distribution.

import sys
import os
import json
import re
import filecmp

XOR_KEY = b"GameData"
TROUBLESOME_TRIGGERS = [
    b'"FarmAnimal":[{"FarmAnimalID":11090001,"Name":"',
]
BYPASS_PREFIX = "BYPASSED_HEX::"

def xor_bytes(data_bytes, key_bytes, key_start_index=0):
    """Performs a repeating-key XOR on a byte sequence."""
    key_len = len(key_bytes)
    return bytes([byte ^ key_bytes[(key_start_index + i) % key_len] for i, byte in enumerate(data_bytes)])

def find_field_details(encrypted_bytes, start_pos):
    """
    Uses a two-pass check to find the length of a problematic field and the
    correct re-sync key index for the data that follows.
    """
    print("\n--- ENTERING find_field_details ---")
    print(f"[DEBUG] Starting analysis from byte position: {start_pos}")

    field_len = None
    
    # --- Pass 1: Find the length of the problematic field ---
    print("\n[DEBUG] Pass 1: Searching for the end-of-field marker '\"}],'... ")
    slice_for_len_check = encrypted_bytes[start_pos:]
    for offset_pass1 in range(len(XOR_KEY)):
        temp_key_idx = (start_pos + offset_pass1) % len(XOR_KEY)
        decrypted_slice = xor_bytes(slice_for_len_check, XOR_KEY, key_start_index=temp_key_idx)
        print(f"[DEBUG]   Offset {offset_pass1} | Preview: {decrypted_slice[:120]!r}")
        try:
            end_marker_pos = decrypted_slice.index(b'"}],')
            field_len = end_marker_pos
            print(f"[DEBUG]   SUCCESS (Pass 1): Found marker with offset {offset_pass1}. Field length: {field_len}")
            break
        except ValueError:
            continue
    if field_len is None:
        print("[DEBUG] Pass 1 FAILED: Could not find end marker with any offset.")
        print("--- EXITING find_field_details (Failure) ---\n")
        return None, None

    # --- Pass 2: Find the correct re-sync key offset for the rest of the file ---
    resync_pos = start_pos + field_len
    print(f"\n[DEBUG] Pass 2: Searching for a valid key offset at re-sync position {resync_pos}...")
    slice_for_offset_check = encrypted_bytes[resync_pos : resync_pos + 50]
    final_resync_key_idx = None
    for offset_pass2 in range(len(XOR_KEY)):
        temp_key_idx = (resync_pos + offset_pass2) % len(XOR_KEY)
        decrypted_slice = xor_bytes(slice_for_offset_check, XOR_KEY, key_start_index=temp_key_idx)
        print(f"[DEBUG]   Offset {offset_pass2} | Preview: {decrypted_slice!r}")
        if decrypted_slice.startswith(b'"}],'):
            final_resync_key_idx = temp_key_idx
            print(f"[DEBUG]   SUCCESS (Pass 2): Found valid re-sync key index: {final_resync_key_idx}")
            print("--- EXITING find_field_details (Success) ---\n")
            return field_len, final_resync_key_idx
            
    print("[DEBUG] Pass 2 FAILED: Could not find a valid re-sync offset.")
    print("--- EXITING find_field_details (Failure) ---\n")
    return field_len, None

def decode_sav_to_json(sav_filepath, pretty_print=True):
    """
    Reads and decodes a .sav file, proactively finding and bypassing problematic fields.
    """
    print(f"[INFO] Decoding '{sav_filepath}'...")
    try:
        with open(sav_filepath, 'rb') as f:
            encrypted_bytes = f.read()

        output_buffer = bytearray()
        data_idx = 0
        key_idx = 0

        while data_idx < len(encrypted_bytes):
            decrypted_byte = encrypted_bytes[data_idx] ^ XOR_KEY[key_idx % len(XOR_KEY)]
            output_buffer.append(decrypted_byte)

            trigger_found = False
            for trigger in TROUBLESOME_TRIGGERS:
                if output_buffer.endswith(trigger):
                    field_start_pos = data_idx + 1
                    length, new_key_idx = find_field_details(encrypted_bytes, field_start_pos)
                    
                    if length is not None and new_key_idx is not None:
                        field_bytes = encrypted_bytes[field_start_pos : field_start_pos + length]
                        
                        output_buffer = output_buffer[:-len(trigger)]
                        output_buffer.extend(trigger)
                        bypass_string = f'{BYPASS_PREFIX}{field_bytes.hex()}:{new_key_idx}'
                        output_buffer.extend(bypass_string.encode('ascii'))
                        
                        data_idx = field_start_pos + length
                        key_idx = new_key_idx
                        trigger_found = True
                    else:
                        print(f"[WARNING] Trigger found at byte {data_idx}, but could not determine field details. Aborting.", file=sys.stderr)
                        return
            
            if not trigger_found:
                data_idx += 1
                key_idx += 1
        
        try:
            final_json_string = output_buffer.decode('utf-8')
            parsed_json = json.loads(final_json_string)
        except (UnicodeDecodeError, json.JSONDecodeError) as e:
            print(f"\n[ERROR] Final repaired data could not be parsed as valid JSON.", file=sys.stderr)
            print(f"  - Reason: {e}", file=sys.stderr)
            error_filepath = os.path.splitext(sav_filepath)[0] + ".failed_decode.bin"
            print(f"  - Saving the raw decrypted data to '{error_filepath}' for inspection.")
            with open(error_filepath, 'wb') as f:
                f.write(output_buffer)
            return

        output_filepath = os.path.splitext(sav_filepath)[0] + ".json"
        
        with open(output_filepath, 'w', encoding='utf-8') as f:
            if pretty_print:
                json.dump(parsed_json, f, indent=4, ensure_ascii=False)
            else:
                json.dump(parsed_json, f, separators=(',', ':'), ensure_ascii=False)
        
        if pretty_print:
            print(f"Successfully decoded and saved to '{output_filepath}'.")

    except Exception as e:
        print(f"An unexpected error occurred during decode: {e}", file=sys.stderr)

def encode_json_to_sav(json_filepath, is_test=False):
    """
    Reads a JSON file, re-encoding it and splicing back in any bypassed fields.
    """
    if not is_test: print(f"[INFO] Encoding '{json_filepath}'...")
    try:
        with open(json_filepath, 'r', encoding='utf-8') as f:
            text_data = f.read()

        if is_test:
            compact_json_string = text_data
        else:
            compact_json_string = json.dumps(json.loads(text_data), separators=(',', ':'), ensure_ascii=False)

        pattern = re.compile(rf'{BYPASS_PREFIX}([a-fA-F0-9]+):(\d+)')
        output_bytes = bytearray()
        last_end = 0
        key_idx = 0

        for match in pattern.finditer(compact_json_string):
            start, end = match.span()
            
            clean_part_str = compact_json_string[last_end:start]
            clean_part_bytes = clean_part_str.encode('utf-8')
            output_bytes.extend(xor_bytes(clean_part_bytes, XOR_KEY, key_start_index=key_idx))
            key_idx = (key_idx + len(clean_part_bytes)) % len(XOR_KEY)
            
            hex_data = match.group(1)
            new_key_idx = int(match.group(2))
            
            raw_field_bytes = bytes.fromhex(hex_data)
            output_bytes.extend(raw_field_bytes)
            key_idx = new_key_idx
            
            last_end = end

        remaining_part_str = compact_json_string[last_end:]
        remaining_part_bytes = remaining_part_str.encode('utf-8')
        output_bytes.extend(xor_bytes(remaining_part_bytes, XOR_KEY, key_start_index=key_idx))

        base_name = os.path.splitext(json_filepath)[0]
        output_filepath = base_name + ".resave" if is_test else base_name + ".sav"
        
        if not is_test and os.path.exists(output_filepath):
             response = input(f"'{output_filepath}' already exists. Overwrite? (y/N): ").lower()
             if response != 'y': return

        with open(output_filepath, 'wb') as f:
            f.write(output_bytes)
        
        if not is_test:
            print(f"Successfully encoded to '{output_filepath}'.")

    except Exception as e:
        print(f"An unexpected error occurred during encode: {e}", file=sys.stderr)

def test_roundtrip(sav_filepath):
    """
    Performs a decode/encode cycle and compares the result to the original file.
    """
    print(f"--- Running round-trip test on '{sav_filepath}' ---")
    base_name = os.path.splitext(sav_filepath)[0]
    json_filepath = base_name + ".json"
    resave_filepath = base_name + ".resave"
    are_identical = False

    try:
        print("\nStep 1: Decoding .sav to raw .json...")
        decode_sav_to_json(sav_filepath, pretty_print=False)

        if not os.path.exists(json_filepath):
            print("\nFAILURE: Decode step failed to produce a .json file.")
            return

        print("\nStep 2: Re-encoding raw .json to .resave...")
        encode_json_to_sav(json_filepath, is_test=True)

        print("\nStep 3: Comparing original .sav with the new .resave file...")
        are_identical = filecmp.cmp(sav_filepath, resave_filepath, shallow=False)

        if are_identical:
            print("\nSUCCESS: The files are identical. The process is perfectly reversible.")
        else:
            print("\nFAILURE: The files are NOT identical. The process is not reversible.")

    finally:
        if are_identical:
            print("\nStep 4: Cleaning up intermediate files...")
            if os.path.exists(json_filepath): os.remove(json_filepath)
            if os.path.exists(resave_filepath): os.remove(resave_filepath)
        print("--- Test complete ---")

def display_usage():
    """Displays the script's usage message."""
    script_name = os.path.basename(sys.argv[0])
    print(f"Usage: {script_name} <file1.json | file1.sav> [file2 ...]")
    print(f"   or: {script_name} --test <file.sav>")
    print("\nconvert save files for dave the Diver between .sav and .json")

def main():
    """Main function to parse command-line arguments."""
    if len(sys.argv) < 2:
        display_usage()
        sys.exit(1)

    if sys.argv[1] == '--test':
        if len(sys.argv) != 3 or not sys.argv[2].lower().endswith('.sav'):
            print("\nError: --test flag requires a single .sav file as an argument.", file=sys.stderr)
            display_usage()
            sys.exit(1)
        test_roundtrip(sys.argv[2])
        return

    for filepath in sys.argv[1:]:
        if not os.path.exists(filepath):
            print(f"Warning: File not found: '{filepath}'. Skipping.", file=sys.stderr)
            continue
        ext = os.path.splitext(filepath)[1].lower()
        if ext == '.json':
            encode_json_to_sav(filepath)
        elif ext == '.sav':
            decode_sav_to_json(filepath)
        else:
            print(f"Warning: Unsupported file extension '{ext}' for '{filepath}'. Skipping.", file=sys.stderr)

if __name__ == "__main__":
    main()

# END OF encdec.py
