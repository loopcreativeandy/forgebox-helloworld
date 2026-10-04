#!/bin/sh
# Build the UI host harness (real helloworld_task.c + real LVGL, ASan/UBSan).
set -e
cd "$(dirname "$0")"
R=../..
mkdir -p obj
SRCS="$(find $R/external/lvgl/src -name '*.c') $R/src/tasks/helloworld_task.c $R/src/ui/openSansEnText.c $R/src/ui/openSansEnTitle.c $R/src/ui/imgDevLogo.c harness.c"
gcc -g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer -rdynamic \
    -DLV_CONF_INCLUDE_SIMPLE -Istubs -I$R/external -I$R/external/lvgl -I$R/external/lvgl/src -I$R/src/tasks -I$R/src/usb -I$R/src/wallet \
    -I$R/src/utils -I$R/src -I$R/src/driver/_none_ -w $SRCS -o ui_host -lm
echo built ui_host
