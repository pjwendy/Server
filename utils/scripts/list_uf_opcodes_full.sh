#!/bin/bash

UF_OPS_H="/home/eqemu/code/common/patches/uf_ops.h"
UF_CPP="/home/eqemu/code/common/patches/uf.cpp"
UF_STRUCTS_H="/home/eqemu/code/common/patches/uf_structs.h"
UF_CONF="/home/eqemu/code/utils/patches/patch_UF.conf"
OUTFILE="/home/eqemu/code/utils/scripts/uf_opcode_structs.md"

extract_block() {
    local file="$1"
    local keyword="$2"
    local opcode="$3"
    awk -v kw="$keyword" -v op="$opcode" '
    BEGIN {
        debug = (op == "OP_CharInventory") ? 1 : 0;
        in_comment = 0;
        if (debug) print "[extract_block] Searching for " kw "(" op ")" > "/dev/stderr";
    }
    # Track block comments
    /\/\*/ { in_comment = 1 }
    /\*\// { in_comment = 0 }
    {
        # Only match macro at start of line, not in comments or strings
        if (!in_comment && $0 !~ /^[[:space:]]*\/\// && $0 ~ "^[[:space:]]*" kw "\\(" op "\\)") {
            if (debug) print "[extract_block] Found macro line: " $0 > "/dev/stderr";
            inblock=1
            brace=0
            started=0
        }
    }
    inblock {
        if (debug) print "[extract_block] Line: " $0 > "/dev/stderr";
        bopen = gsub(/\{/, "{")
        bclose = gsub(/\}/, "}")
        if (debug) print "[bopen] : " bopen > "/dev/stderr";
        if (debug) print "[bclose] : " bclose > "/dev/stderr";
        if (debug) print "[brace] : " brace > "/dev/stderr";
        brace += bopen
        if (debug) print "[brace] : " brace > "/dev/stderr";
        brace -= bclose
        if (debug) print "[brace] : " brace > "/dev/stderr";
        if (bopen > 0) started=1
        if (debug) print "[extract_block] Brace count: " brace ", started: " started > "/dev/stderr";
        print
        if (started && brace == 0) {
            if (debug) print "[extract_block] Block end reached." > "/dev/stderr";
            inblock=0
            exit
        }
    }
    ' "$file"
}

echo "# Underfoot Client Opcodes and Structures" > "$OUTFILE"
echo "" >> "$OUTFILE"
echo "This file lists all opcodes, their direction, hex value, associated structure, the full structure definition, and the full ENCODE or DECODE section for the Underfoot client protocol." >> "$OUTFILE"
echo "" >> "$OUTFILE"

grep -E 'E\(OP_|D\(OP_' "$UF_OPS_H" | \
    sed -E 's/E\(OP_([A-Za-z0-9_]+)\)/OP_\1|outgoing/; s/D\(OP_([A-Za-z0-9_]+)\)/OP_\1|incoming/' | \
    while IFS='|' read OPCODE DIRECTION; do
        # Find opcode hex value in patch_UF.conf
        OPCODE_HEX=$(grep -E "^$OPCODE[[:space:]]*=" "$UF_CONF" | head -1 | sed -E 's/.*=0x([0-9a-fA-F]+).*/0x\1/')
        if [ -z "$OPCODE_HEX" ]; then
            OPCODE_HEX="(unknown)"
        fi
        # Find structure name in uf.cpp (look for structs:: or direct struct usage)
        STRUCT_NAME=$(awk "/ENCODE\($OPCODE\)|DECODE\($OPCODE\)/,/\}/" "$UF_CPP" | \
            grep -Eo 'structs::[A-Za-z0-9_]+' | head -1 | sed 's/structs:://')
        if [ -z "$STRUCT_NAME" ]; then
            STRUCT_NAME=$(awk "/ENCODE\($OPCODE\)|DECODE\($OPCODE\)/,/\}/" "$UF_CPP" | \
                grep -Eo '[A-Za-z0-9_]+_Struct' | head -1)
        fi
        if [ -z "$STRUCT_NAME" ]; then
            STRUCT_NAME="(unknown)"
        fi

        # Extract full structure definition from uf_structs.h (handles { on next line)
        STRUCT_DEF="Structure definition not found."
        if [ "$STRUCT_NAME" != "(unknown)" ]; then
            START_LINE=$(grep -n -E "struct[[:space:]]+$STRUCT_NAME([[:space:]]*|$)" "$UF_STRUCTS_H" | head -1 | cut -d: -f1)
            if [ -n "$START_LINE" ]; then
                BRACE_LINE=$START_LINE
                if ! grep -E "struct[[:space:]]+$STRUCT_NAME[[:space:]]*\{" "$UF_STRUCTS_H" | head -1 >/dev/null; then
                    BRACE_LINE=$(awk "NR>$START_LINE{if(match(\$0,/^\s*\{/)){print NR; exit}}" "$UF_STRUCTS_H")
                fi
                END_LINE=$(awk "NR>$BRACE_LINE{if(match(\$0,/^\s*\};/)){print NR; exit}}" "$UF_STRUCTS_H")
                if [ -n "$BRACE_LINE" ] && [ -n "$END_LINE" ]; then
                    STRUCT_DEF=$(sed -n "${START_LINE},${END_LINE}p" "$UF_STRUCTS_H")
                fi
            fi
        fi

        # Extract full ENCODE or DECODE section from uf.cpp using brace counting
        CODE_SECTION="Section not found."
        if [ "$DIRECTION" = "outgoing" ]; then
            CODE_SECTION=$(extract_block "$UF_CPP" "ENCODE" "$OPCODE")
        else
            CODE_SECTION=$(extract_block "$UF_CPP" "DECODE" "$OPCODE")
        fi
        if [ -z "$CODE_SECTION" ]; then
            CODE_SECTION="Section not found."
        fi

        # Write to Markdown file
        echo "## $OPCODE ($OPCODE_HEX)" >> "$OUTFILE"
        echo "- **Direction:** $DIRECTION" >> "$OUTFILE"
        echo "- **Structure:** $STRUCT_NAME" >> "$OUTFILE"
        echo "" >> "$OUTFILE"
        echo "**Structure Definition:**" >> "$OUTFILE"
        echo '```cpp' >> "$OUTFILE"
        echo "$STRUCT_DEF" >> "$OUTFILE"
        echo '```' >> "$OUTFILE"
        echo "" >> "$OUTFILE"
        echo "**Full $DIRECTION Section:**" >> "$OUTFILE"
        echo '```cpp' >> "$OUTFILE"
        echo "$CODE_SECTION" >> "$OUTFILE"
        echo '```' >> "$OUTFILE"
        echo "" >> "$OUTFILE"
        echo "---" >> "$OUTFILE"
        echo "" >> "$OUTFILE"
    done