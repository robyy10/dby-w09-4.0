#!/bin/bash
# =============================================================
# DBY-W09 Kernel Packaging Script
# Huawei MatePad 11 2021 — Snapdragon 865 — Kernel 4.19.157
# =============================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
IMAGE="$SCRIPT_DIR/arch/arm64/boot/Image.gz"
OUT_DIR="$SCRIPT_DIR/out"
DATE=$(date +%Y%m%d-%H%M)
STOCK_BOOT="${1:-}"

RED='\033[0;31m'; GREEN='\033[0;32m'
YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}  DBY-W09 Kernel Packaging${NC}"
echo -e "${BLUE}  Kernel 4.19.157 / Snapdragon 865${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

# --- Controllo Image.gz ---
if [ ! -f "$IMAGE" ]; then
    echo -e "${RED}ERRORE: Image.gz non trovata in:${NC} $IMAGE"
    echo "Compila prima il kernel con: make ... Image.gz"
    exit 1
fi
echo -e "${GREEN}✓ Image.gz trovata${NC} ($(du -sh "$IMAGE" | cut -f1))"

mkdir -p "$OUT_DIR"

# =============================================
# METODO 1: Crea AnyKernel3 zip (per TWRP)
# =============================================
echo ""
echo -e "${BLUE}[Metodo 1] Creazione AnyKernel3 zip...${NC}"

AK3_DIR="$SCRIPT_DIR/AnyKernel3"
ZIP_NAME="DBY-W09_kernel-4.19.157_${DATE}.zip"

cp "$IMAGE" "$AK3_DIR/Image.gz"
cd "$AK3_DIR"
zip -r9 "$OUT_DIR/$ZIP_NAME" \
    anykernel.sh Image.gz META-INF/ modules/ patch/ ramdisk/ tools/ \
    -x "*.git*" "*.DS_Store*" "*.placeholder" 2>/dev/null
rm -f "$AK3_DIR/Image.gz"
cd "$SCRIPT_DIR"

echo -e "${GREEN}✓ AnyKernel3 zip:${NC} out/$ZIP_NAME ($(du -sh "$OUT_DIR/$ZIP_NAME" | cut -f1))"

# =============================================
# METODO 2: boot.img con ramdisk originale
# =============================================
echo ""
echo -e "${BLUE}[Metodo 2] Creazione boot.img...${NC}"

# Cerca magiskboot (x86_64) nel PATH o nella directory
MAGISKBOOT=""
for candidate in \
    "$(which magiskboot 2>/dev/null)" \
    "$SCRIPT_DIR/tools/magiskboot" \
    "/usr/local/bin/magiskboot"; do
    if [ -x "$candidate" ]; then
        MAGISKBOOT="$candidate"
        break
    fi
done

if [ -z "$MAGISKBOOT" ]; then
    echo -e "${YELLOW}⚠ magiskboot non trovato. Scarico...${NC}"
    mkdir -p "$SCRIPT_DIR/tools"
    # Scarica magiskboot x86_64 da Magisk releases
    MAGISK_VER="v27.0"
    curl -sL "https://github.com/topjohnwu/Magisk/releases/download/${MAGISK_VER}/Magisk-${MAGISK_VER}.apk" \
        -o /tmp/Magisk.apk && \
    unzip -p /tmp/Magisk.apk lib/x86_64/libmagiskboot.so > "$SCRIPT_DIR/tools/magiskboot" && \
    chmod +x "$SCRIPT_DIR/tools/magiskboot" && \
    MAGISKBOOT="$SCRIPT_DIR/tools/magiskboot" || \
    echo -e "${YELLOW}⚠ Download fallito. Segui le istruzioni manuali sotto.${NC}"
fi

if [ -n "$MAGISKBOOT" ] && [ -n "$STOCK_BOOT" ] && [ -f "$STOCK_BOOT" ]; then
    BOOT_OUT="$OUT_DIR/DBY-W09_kernel-4.19.157_${DATE}_boot.img"
    WORK_DIR=$(mktemp -d)
    
    echo "  Decompongo boot.img stock..."
    cp "$STOCK_BOOT" "$WORK_DIR/boot.img"
    cd "$WORK_DIR"
    "$MAGISKBOOT" unpack boot.img
    
    echo "  Sostituisco kernel..."
    cp "$IMAGE" "$WORK_DIR/kernel"
    
    echo "  Rimpacchetto..."
    "$MAGISKBOOT" repack boot.img "$BOOT_OUT"
    cd "$SCRIPT_DIR"
    rm -rf "$WORK_DIR"
    
    echo -e "${GREEN}✓ boot.img:${NC} out/$(basename $BOOT_OUT) ($(du -sh "$BOOT_OUT" | cut -f1))"
else
    if [ -z "$STOCK_BOOT" ]; then
        echo -e "${YELLOW}⚠ Boot.img stock non fornito. Esegui:${NC}"
        echo "  ./package_kernel.sh /path/to/stock_boot.img"
    elif [ ! -f "$STOCK_BOOT" ]; then
        echo -e "${YELLOW}⚠ File non trovato: $STOCK_BOOT${NC}"
    fi
fi

# =============================================
# RIEPILOGO E ISTRUZIONI
# =============================================
echo ""
echo -e "${BLUE}========================================${NC}"
echo -e "${GREEN}  File creati in: out/${NC}"
ls -lh "$OUT_DIR/" 2>/dev/null
echo -e "${BLUE}========================================${NC}"
echo ""
echo -e "${YELLOW}Come estrarre il boot.img stock dal tablet:${NC}"
echo ""
echo -e "${BLUE}  Via ADB (tablet avviato):${NC}"
echo "    adb shell su -c 'dd if=/dev/block/bootdevice/by-name/boot of=/sdcard/boot.img'"
echo "    adb pull /sdcard/boot.img"
echo ""
echo -e "${BLUE}  Poi crea il boot.img con kernel custom:${NC}"
echo "    ./package_kernel.sh boot.img"
echo ""
echo -e "${BLUE}  Flash (temporaneo - NON permanente, torna al riavvio):${NC}"
echo "    adb reboot bootloader"
echo "    fastboot boot out/$(ls out/*.img 2>/dev/null | head -1 | xargs basename 2>/dev/null || echo 'boot.img')"
echo ""
echo -e "${BLUE}  Flash permanente:${NC}"
echo "    fastboot flash boot out/<boot.img>"
echo "    fastboot reboot"
