#!/bin/bash
# Script to debug STM32F407 via SWD using ST-LINK GDB Server

echo "Starting ST-LINK GDB Server for STM32F407..."
echo "Target: STM32F407xx"
echo "Debugger: ST-LINK/V2"
echo "Port: 61234"
echo ""

# Start ST-LINK GDB Server
# -d: Enable SWD debug mode
# -p: TCP port number for GDB client
# -v: Verbose mode
# --halt: Halt core during reset
/home/xiucong/ToolChains/STM32Clt/STLink-gdb-server/bin/ST-LINK_gdbserver \
    -d \
    -p 61234 \
    -v \
    --halt \
    -cp /home/xiucong/ToolChains/STM32Clt/STM32CubeProgrammer/bin
