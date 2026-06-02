#!/usr/bin/env python3
import os
import re
import sys

# Define known proper nouns, abbreviations, and acronyms that must be protected with curly braces in titles/booktitles/journals
PROPER_NOUNS = [
    r'Rust', r'C\+\+', r'Cyclone', r'CLU', r'Toka', r'Coq', r'System\s+F', 
    r'Typed\s+Assembly\s+Language', r'ML', r'ALGOL', r'BCPL', r'CHERI', 
    r'RISC', r'ISCA', r'POPL', r'PLDI', r'OOPSLA', r'ESOP', r'TOPLAS', 
    r'LICS', r'HILT', r'PACMPL', r'Datalog', r'Vale', r'Swift', r'NLL',
    r'Separation\s+Logic', r'Alias\s+Types', r'Ownership\s+Types', 
    r'Flexible\s+Alias\s+Protection', r'System\s+Programming'
]

# Compile regular expressions to find unprotected proper nouns
# An unprotected word is not enclosed by { }
# E.g. we want to detect "Rust" but not "{Rust}" or "{{Rust}}"
def get_unprotected_regexes():
    regexes = []
    for word in PROPER_NOUNS:
        # Match the word if it is not immediately preceded by { and followed by }
        # Or more simply, match the word where it is surrounded by word boundaries, 
        # and checking if it is inside braces is handled by a parser or character scan.
        # Let's use a simpler heuristic: if the word is found, check if it's enclosed.
        # We will compile a pattern to match the word as a whole word.
        pattern = re.compile(r'\b' + word + r'\b')
        regexes.append((word, pattern))
    return regexes

def check_brackets_balance(text):
    balance = 0
    for char in text:
        if char == '{':
            balance += 1
        elif char == '}':
            balance -= 1
            if balance < 0:
                return False
    return balance == 0

def is_word_protected(value, start_idx, end_idx):
    # Check if the word at value[start_idx:end_idx] is enclosed in braces
    # A simple way is to check if there is an opening brace somewhere before it 
    # and a corresponding closing brace after it, at the same nesting level.
    # More practically: check if the character immediately before is '{' and after is '}'
    # or if the entire field value is double-braced, or if the word is inside { ... }
    
    # We can track the brace nesting level at each character index
    nesting = 0
    nesting_levels = []
    for char in value:
        if char == '{':
            nesting += 1
        elif char == '}':
            nesting -= 1
        nesting_levels.append(nesting)
        
    # If the nesting level at the word's start/end index is > 0 (or if we are inside a brace),
    # it is protected. However, if the opening brace is at the very beginning of the field:
    # e.g., title={The Rust Language}, the nesting level is 1 everywhere inside, 
    # but "Rust" is NOT protected from being lowercased by BibTeX style because the whole field brace
    # doesn't count as inner protection unless it is double-braced or the word itself is braced.
    # BibTeX downcases everything in titles except the first letter and words enclosed in an INNER pair of braces.
    # So the nesting level relative to the outermost braces must be >= 1.
    # Specifically, if the value starts with '{' and ends with '}', the base nesting level inside is 1.
    # Any word needs to be at a nesting level strictly greater than the base level to be protected.
    
    # Let's determine the base nesting level of the field.
    # If it is defined as title={...}, the outer braces are parsed.
    # Let's count braces from the start.
    outer_braces = 0
    if value.startswith('{') and value.endswith('}'):
        # Check if they are the matching outer braces
        temp = 0
        is_outer = True
        for i, char in enumerate(value):
            if char == '{':
                temp += 1
            elif char == '}':
                temp -= 1
                if temp == 0 and i < len(value) - 1:
                    is_outer = False
                    break
        if is_outer:
            outer_braces = 1
            
    # Now check the nesting level of the word
    temp = 0
    for i in range(start_idx):
        if value[i] == '{':
            temp += 1
        elif value[i] == '}':
            temp -= 1
            
    # If the nesting level of the word is greater than outer_braces, it's protected!
    return temp > outer_braces

def analyze_bib_file(filepath):
    print(f"\n========================================\nAnalyzing: {filepath}\n========================================")
    if not os.path.exists(filepath):
        print(f"Error: File {filepath} does not exist.")
        return False

    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    # Parse entries manually or using regex
    # Match: @type{key, fields...}
    # We will find all entries
    entry_pattern = re.compile(r'@(\w+)\s*\{\s*([^,]+),')
    entries = []
    
    # Let's extract entries by matching matching braces
    idx = 0
    errors_found = 0
    warnings_found = 0
    keys = set()
    
    while True:
        match = entry_pattern.search(content, idx)
        if not match:
            break
            
        entry_type = match.group(1).lower()
        entry_key = match.group(2).strip()
        start_pos = match.start()
        
        # Find matching closing brace for this entry
        brace_count = 0
        end_pos = -1
        for i in range(start_pos, len(content)):
            if content[i] == '{':
                brace_count += 1
            elif content[i] == '}':
                brace_count -= 1
                if brace_count == 0:
                    end_pos = i + 1
                    break
                    
        if end_pos == -1:
            print(f"Error: Unbalanced braces starting at line {content.count(os.linesep, 0, start_pos) + 1} for key {entry_key}")
            errors_found += 1
            idx = match.end()
            continue
            
        entry_body = content[start_pos:end_pos]
        idx = end_pos
        
        # Check duplicate keys
        if entry_key in keys:
            print(f"Error: Duplicate citation key '{entry_key}' found.")
            errors_found += 1
        keys.add(entry_key)
        
        # Parse fields inside this entry
        # Fields are generally: name = {value} or name = "value"
        # We can extract them using a regex
        fields = {}
        # We want to match field names and their braced or quoted values
        # Let's search inside the entry body (excluding the type and key header)
        header_len = match.end() - start_pos
        body_fields_part = entry_body[header_len:-1].strip()
        
        # Parse fields by scanning
        field_idx = 0
        while field_idx < len(body_fields_part):
            # Find next '='
            eq_idx = body_fields_part.find('=', field_idx)
            if eq_idx == -1:
                break
            field_name = body_fields_part[field_idx:eq_idx].strip().lower()
            
            # Find the value starting after '='
            val_start = eq_idx + 1
            while val_start < len(body_fields_part) and body_fields_part[val_start].isspace():
                val_start += 1
                
            if val_start >= len(body_fields_part):
                break
                
            # Value can be enclosed in { } or " " or be numeric
            val_end = -1
            if body_fields_part[val_start] == '{':
                # Find matching }
                b_count = 1
                for j in range(val_start + 1, len(body_fields_part)):
                    if body_fields_part[j] == '{':
                        b_count += 1
                    elif body_fields_part[j] == '}':
                        b_count -= 1
                        if b_count == 0:
                            val_end = j + 1
                            break
            elif body_fields_part[val_start] == '"':
                # Find matching "
                for j in range(val_start + 1, len(body_fields_part)):
                    if body_fields_part[j] == '"' and body_fields_part[j-1] != '\\':
                        val_end = j + 1
                        break
            else:
                # Numeric or unbraced identifier, find next comma or end
                comma_idx = body_fields_part.find(',', val_start)
                if comma_idx == -1:
                    val_end = len(body_fields_part)
                else:
                    val_end = comma_idx
                    
            if val_end == -1:
                print(f"Error: Unbalanced braces/quotes in field '{field_name}' of entry '{entry_key}'")
                errors_found += 1
                break
                
            field_val = body_fields_part[val_start:val_end].strip()
            fields[field_name] = field_val
            field_idx = val_end
            # Skip comma if any
            while field_idx < len(body_fields_part) and (body_fields_part[field_idx].isspace() or body_fields_part[field_idx] == ','):
                field_idx += 1
                
        # Validate fields
        # 1. Missing fields validation
        missing_fields = []
        if entry_type == 'article':
            for req in ['author', 'title', 'journal', 'year']:
                if req not in fields:
                    missing_fields.append(req)
        elif entry_type == 'inproceedings':
            for req in ['author', 'title', 'booktitle', 'year']:
                if req not in fields:
                    missing_fields.append(req)
        elif entry_type == 'book':
            if 'author' not in fields and 'editor' not in fields:
                missing_fields.append('author/editor')
            for req in ['title', 'publisher', 'year']:
                if req not in fields:
                    missing_fields.append(req)
                    
        if missing_fields:
            print(f"Warning: Entry '{entry_key}' ({entry_type}) is missing required field(s): {', '.join(missing_fields)}")
            warnings_found += 1
            
        # 2. Casing protection validation
        unprotected_regexes = get_unprotected_regexes()
        for field_name in ['title', 'booktitle', 'journal', 'series']:
            if field_name in fields:
                val = fields[field_name]
                for name, regex in unprotected_regexes:
                    for m in regex.finditer(val):
                        start_idx = m.start()
                        end_idx = m.end()
                        # Check if this occurrence is protected
                        if not is_word_protected(val, start_idx, end_idx):
                            # Special check: is it the first word of the title?
                            # If it's the very first word of the field (and it's not preceded by other letters),
                            # standard BibTeX styles capitalize the first letter anyway, but protecting it is still safer.
                            # We will warn about it.
                            is_first_word = (start_idx == 0 or (start_idx == 1 and val[0] == '{'))
                            if not is_first_word:
                                print(f"Warning: In entry '{entry_key}', field '{field_name}' contains unprotected proper noun/acronym '{m.group(0)}' at index {start_idx} (value: {val})")
                                warnings_found += 1
                                
        # 3. URL and howpublished sanity checks
        if 'howpublished' in fields:
            val = fields['howpublished']
            if '\\url' in val:
                print(f"Note: In entry '{entry_key}', 'howpublished' contains a LaTeX '\\url' macro. For Typst compatibility, consider using a clean 'url' field directly instead of '\\url' in 'howpublished'.")
                warnings_found += 1

    print(f"Analysis complete: {errors_found} errors, {warnings_found} warnings found.")
    return errors_found == 0

def main():
    workspace = '/home/zhyi/GitDP/tokalang'
    bib_files = [
        os.path.join(workspace, 'tokac/paper_hsmodel/bib/references.bib'),
        os.path.join(workspace, 'tokac/paper/bib/references.bib')
    ]
    
    success = True
    for bf in bib_files:
        if not analyze_bib_file(bf):
            success = False
            
    if not success:
        sys.exit(1)
    else:
        sys.exit(0)

if __name__ == '__main__':
    main()
