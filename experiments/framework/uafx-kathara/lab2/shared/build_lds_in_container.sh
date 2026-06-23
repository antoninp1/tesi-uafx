#!/bin/bash
# ============================================================
# build_lds_in_container.sh
#
# Compila lds_server DENTRO un container Debian bookworm, per
# compatibilita' glibc con l'immagine Kathara (uafx-node).
#
# L'LDS dipende solo da open62541.c (niente types UAFX, niente cJSON),
# quindi il build e' molto piu' semplice del temp_server.
#
# Uso:
#   (dalla cartella dove hai salvato lds_server.c)
#   ./build_lds_in_container.sh
# Output:
#   lds_server  (nella cartella corrente, compatibile col container)
# ============================================================
set -e

# --- Percorsi sul filesystem HOST ---
ROOT="$HOME/tesi-uafx"
LIB_DIR="$ROOT/lib"
SRC_DIR="$(pwd)"                  # cartella dove sta lds_server.c
OUT_DIR="$(pwd)"                  # dove finisce lds_server

# Verifica presenza del sorgente
if [ ! -f "$SRC_DIR/lds_patched.c" ]; then
    echo "ERRORE: lds_server.c non trovato in $SRC_DIR"
    echo "Salva lds_server.c in questa cartella e rilancia."
    exit 1
fi
if [ ! -f "$LIB_DIR/open62541.c" ]; then
    echo "ERRORE: open62541.c non trovato in $LIB_DIR"
    exit 1
fi

echo "==> Build LDS dentro container debian:bookworm"
echo "    LIB_DIR = $LIB_DIR"
echo "    SRC_DIR = $SRC_DIR"
echo ""

docker run --rm \
    -v "$ROOT":"$ROOT" \
    -v "$SRC_DIR":"$SRC_DIR" \
    -w "$SRC_DIR" \
    debian:bookworm-slim \
    bash -c '
        set -e
        echo "==> Installo toolchain nel container..."
        apt-get update -qq && apt-get install -y -qq gcc libc6-dev libssl-dev > /dev/null
        echo "==> Compilo..."
        gcc -o '"$OUT_DIR"'/lds_server \
            '"$SRC_DIR"'/lds_patched.c \
            '"$LIB_DIR"'/open62541.c \
            -I'"$LIB_DIR"' \
            -pthread \
            -Wno-unused-parameter
        echo "==> Build OK"
    '

echo ""
echo "==> lds_server compilato (bookworm-compatibile):"
ls -lh "$OUT_DIR/lds_server"
echo ""
echo "Verifica glibc richiesta dal binario:"
objdump -T "$OUT_DIR/lds_server" 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sort -u | tail -5 || true
