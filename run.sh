#!/bin/sh
TERM=xterm qemu-system-x86_64 -m 64 -curses -kernel kernel/kernel.bin -net nic,model=rtl8139 -net user
