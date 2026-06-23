#!/bin/bash
# ============================================================
# build_backend_in_container.sh
#
# Compila il backend (discovery service) DENTRO un container Debian
# bookworm, eseguendo il Makefile esistente del progetto. Garantisce
# compatibilita' glibc con l'immagine Kathara (uafx-node).
#
# Usa il Makefile in ~/tesi-uafx/experiments/framework/backend cosi'
# com'e': stesso build della macchina di sviluppo, solo con la glibc
# del container.
#
# Uso:
#   ./build_backend_in_container.sh
# Output:
#   backend  (in ~/tesi-uafx/experiments/framework/backend)
# ============================================================
set -e

# --- Percorsi sul filesystem HOST ---
ROOT="$HOME/tesi-uafx"
BACKEND_DIR="$ROOT/experiments/framework/backend"

if [ ! -f "$BACKEND_DIR/Makefile" ]; then
    echo "ERRORE: Makefile non trovato in $BACKEND_DIR"
    exit 1
fi

echo "==> Build backend dentro container debian:bookworm"
echo "    BACKEND_DIR = $BACKEND_DIR"
echo ""

# Montiamo l'intera home tesi-uafx (il Makefile usa percorsi relativi
# ../../../lib/... che devono risolvere correttamente).
docker run --rm \
    -v "$ROOT":"$ROOT" \
    -w "$BACKEND_DIR" \
    debian:bookworm-slim \
    bash -c '
        set -e
        echo "==> Installo toolchain nel container..."
        apt-get update -qq && apt-get install -y -qq gcc make libc6-dev libssl-dev > /dev/null
        echo "==> make clean..."
        make clean || true
        echo "==> make..."
        make
        echo "==> Build OK"
    '

echo ""
echo "==> backend compilato (bookworm-compatibile):"
ls -lh "$BACKEND_DIR/backend"
echo ""
echo "Verifica glibc richiesta dal binario:"
objdump -T "$BACKEND_DIR/backend" 2>/dev/null | grep -o 'GLIBC_[0-9.]*' | sort -u | tail -5 || true
echo ""
echo "Per copiarlo nel lab:"
echo "  cp $BACKEND_DIR/backend ~/uafx-kathara/lab2/shared/"
