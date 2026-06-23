#!/bin/bash
# ============================================================
# build_temp_server.sh
#
# Compila il server UAFX (versione LLDP live) con l'amalgamation
# open62541, raccogliendo i sorgenti dalle loro posizioni reali nel
# progetto tesi-uafx. Verifica la presenza dei file prima di compilare.
#
# Layout assunto:
#   /tesi-uafx/lib                          -> open62541.{c,h}, cJSON.{c,h}
#   /tesi-uafx/experiments/framework/server -> uafx_temperature_server.c,
#                                              establish_connection{,2}.{c,h}
#   $GEN_DIR (vedi sotto)                   -> types_*_generated.*, my_uafx_model.*
#
# Uso:
#   ./build_temp_server.sh
# Output:
#   ./temp_server  (nella cartella corrente)
# ============================================================

set -e

# --- Percorsi (modifica GEN_DIR se i types stanno altrove) ---
ROOT="$HOME/tesi-uafx"
LIB_DIR="$ROOT/lib"
SRV_DIR="$ROOT/experiments/framework/server"
GEN_DIR="$(pwd)"          # cartella dei types_*_generated e my_uafx_model
                          # (default: cartella corrente; cambiala se serve)

OUT="temp_server"
MAIN_SRC="$SRV_DIR/uafx_temperature_server.c"

# --- Elenco sorgenti (.c) con percorso completo ---
SOURCES=(
    "$GEN_DIR/my_uafx_model.c"
    "$GEN_DIR/types_di_generated.c"
    "$GEN_DIR/types_uafx_data_generated.c"
    "$GEN_DIR/types_uafx_ac_generated.c"
    "$SRV_DIR/establish_connection.c"
    "$LIB_DIR/cJSON.c"
    "$LIB_DIR/open62541.c"
)

# --- Header richiesti (verifica presenza) ---
HEADERS=(
    "$LIB_DIR/open62541.h"
    "$LIB_DIR/cJSON.h"
    "$SRV_DIR/establish_connection.h"
    "$GEN_DIR/my_uafx_model.h"
    "$GEN_DIR/types_di_generated.h"
    "$GEN_DIR/types_di_generated_handling.h"
    "$GEN_DIR/types_uafx_data_generated.h"
    "$GEN_DIR/types_uafx_data_generated_handling.h"
    "$GEN_DIR/types_uafx_ac_generated.h"
    "$GEN_DIR/types_uafx_ac_generated_handling.h"
)

# --- Cartelle da passare come -I (dove stanno gli header) ---
INCLUDES=(
    "-I$LIB_DIR"
    "-I$SRV_DIR"
    "-I$GEN_DIR"
)

# --- Verifica presenza file ---
echo "==> Percorsi:"
echo "    LIB_DIR = $LIB_DIR"
echo "    SRV_DIR = $SRV_DIR"
echo "    GEN_DIR = $GEN_DIR"
echo ""
echo "==> Verifica file richiesti..."
MISSING=0

check_file() {
    if [ ! -f "$1" ]; then
        echo "   [MANCA]   $1"
        MISSING=$((MISSING + 1))
    else
        echo "   [ok]      $1"
    fi
}

echo "-- Main:"
check_file "$MAIN_SRC"
echo "-- Sorgenti (.c):"
for f in "${SOURCES[@]}"; do check_file "$f"; done
echo "-- Header (.h):"
for h in "${HEADERS[@]}"; do check_file "$h"; done

if [ "$MISSING" -gt 0 ]; then
    echo ""
    echo "ERRORE: mancano $MISSING file. Correggi i percorsi in cima allo"
    echo "script (ROOT / GEN_DIR) o copia i file mancanti, poi riprova."
    exit 1
fi

# --- Compilazione ---
echo ""
echo "==> Compilazione di $OUT ..."
gcc -o "$OUT" \
    -include "$LIB_DIR/open62541.h" \
    "$MAIN_SRC" \
    "${SOURCES[@]}" \
    "${INCLUDES[@]}" \
    -pthread \
    -Wno-unused-parameter

echo ""
echo "==> Build completata: ./$OUT"
ls -lh "$OUT"
