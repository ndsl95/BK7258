#!/bin/bash
# 编译 WB5E 工程并导出固件到 H:\BK7258\firmware
# 用法: bash build.sh [wb5e_lvgl|wb5e_demo]   (默认 wb5e_lvgl)
set -euo pipefail
PROJ=${1:-wb5e_lvgl}
TASK_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)
SDK_ROOT=${BK7258_SDK_ROOT:-/root/bk7258}
# The source under code/ is authoritative. Only replace changed files so
# incremental builds remain valid and local SDK-only files are preserved.
sync_file() {
    local source_file=$1 target_file=$2
    if ! cmp -s "$source_file" "$target_file"; then
        mkdir -p -- "$(dirname -- "$target_file")"
        cp -- "$source_file" "$target_file"
    fi
}
if [[ "$PROJ" == wb5e_lvgl ]]; then
    while IFS= read -r -d '' source_file; do
        relative_file=${source_file#"$TASK_ROOT/code/projects/$PROJ/"}
        sync_file "$source_file" "$SDK_ROOT/projects/$PROJ/$relative_file"
    done < <(find "$TASK_ROOT/code/projects/$PROJ" -type f -print0)
    sync_file "$TASK_ROOT/code/patches/lcd_qspi_st77916.c" "$SDK_ROOT/ap/components/bk_peripheral/src/lcd/qspi/lcd_qspi_st77916.c"
    sync_file "$TASK_ROOT/code/patches/lv_port_disp_v9.c" "$SDK_ROOT/ap/components/lvgl/lvgl_v9/porting/lv_port_disp.c"
    sync_file "$TASK_ROOT/code/patches/lv_conf_v9.h" "$SDK_ROOT/ap/components/lvgl/lvgl_v9/lv_conf.h"
    sync_file "$TASK_ROOT/code/patches/bk_display_qspi_ctlr.c" "$SDK_ROOT/ap/components/bk_display/src/qspi/bk_display_qspi_ctlr.c"
fi
cd -- "$SDK_ROOT"
echo ">>> building PROJECT=$PROJ"
make bk7258 PROJECT=$PROJ
mkdir -p -- "$TASK_ROOT/firmware"
cp "build/bk7258/$PROJ/package/all-app.bin" "$TASK_ROOT/firmware/all-app_$PROJ.bin"
echo ">>> OK: H:/BK7258/firmware/all-app_$PROJ.bin"
