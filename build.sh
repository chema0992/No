#!/bin/bash

gcc -Wall -Wextra -Werror -Os -s \
    -ffunction-sections -fdata-sections \
    -fno-ident -fno-asynchronous-unwind-tables \
    -Wl,--gc-sections -static main.c -o no
