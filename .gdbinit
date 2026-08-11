# GDB initialization script for STM32F407 debugging via ST-LINK
target extended-remote :61234

# Load the ELF file
file build/Debug/AIAssistant_Proj.elf

# Load program to target
load

# Enable semihosting (optional)
monitor arm semihosting enable

# Reset and halt
monitor reset halt

# Set breakpoint at main
break main

# Continue to main
continue

# Show current location
list
