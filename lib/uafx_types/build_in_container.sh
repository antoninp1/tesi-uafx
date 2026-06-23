#!/bin/bash
# ============================================================
# build_in_container.sh
#
# Compila temp_server DENTRO un container Debian bookworm, cosi' il
# binario e' linkato contro la stessa glibc/libssl dell'immagine Kathara
# (uafx-node) ed e' garantito compatibile.
#
# Risolve l'errore: "GLIBC_2.xx not found" quando il binario e' compilato
# su una distro piu' recente (es. Ubuntu 24.04, glibc 2.39) ma gira in un
# container bookworm (glibc 2.36).
#
# Uso:
#   ./build_in_container.sh
# Output:
#   temp_server  (nella cartella corrente, compatibile col container)
# ============================================================

set -e

# --- Percorsi sul filesystem HOST ---
ROOT="$HOME/tesi-uafx"
LIB_DIR="$ROOT/lib"
SRV_DIR="$ROOT/experiments/framework/server"
GEN_DIR="$(pwd)"                  # cartella dei types_*_generated / my_uafx_model
OUT_DIR="$(pwd)"                  # dove finisce temp_server

echo "==> Build dentro container debian:bookworm"
echo "    ROOT    = $ROOT"
echo "    GEN_DIR = $GEN_DIR"
echo ""

# Montiamo l'intera home tesi-uafx (cosi' tutti i path assoluti combaciano)
# e la cartella corrente (per i generati e l'output).
# gcc viene installato al volo nel container.

docker run --rm \
    -v "$ROOT":"$ROOT" \
    -v "$GEN_DIR":"$GEN_DIR" \
    -w "$GEN_DIR" \
    debian:bookworm-slim \
    bash -c '
        set -e
        echo "==> Installo toolchain nel container..."
        apt-get update -qq && apt-get install -y -qq gcc libc6-dev libssl-dev > /dev/null
        echo "==> Compilo..."
        gcc -o '"$OUT_DIR"'/temp_server \
            -include '"$LIB_DIR"'/open62541.h \
            '"$SRV_DIR"'/uafx_temperature_server.c \
            '"$GEN_DIR"'/my_uafx_model.c \
            '"$GEN_DIR"'/types_di_generated.c \
            '"$GEN_DIR"'/types_uafx_data_generated.c \
            '"$GEN_DIR"'/types_uafx_ac_generated.c \
            '"$SRV_DIR"'/establish_connection.c \
            '"$LIB_DIR"'/cJSON.c \
            '"$LIB_DIR"'/open62541.c \
            -I'"$LIB_DIR"' \
            -I'"$SRV_DIR"' \
            -I'"$GEN_DIR"' \
            -pthread \
            -Wno-unused-parameter
        echo "==> Build OK"
    '

echo ""
echo "==> temp_server compilato (bookworm-compatibile):"
ls -lh "$OUT_DIR/temp_server"
echo ""
echo "Verifica glibc richiesta dal binario:"
objdump -T "$OUT_DIR/temp_server" 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sort -u | tail -5 || true
