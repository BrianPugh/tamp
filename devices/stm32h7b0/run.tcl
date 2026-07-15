# Flash the benchmark firmware, load the data blob into AXI SRAM, and run with
# semihosting console output. Invoke from the repo root:
#   openocd -f interface/stlink.cfg -f target/stm32h7x.cfg -f devices/stm32h7b0/run.tcl
# OpenOCD stays attached to service semihosting; tools/device-runner.py --exec
# terminates it after the TAMP-DEVICE-RESULT sentinel.
program devices/stm32h7b0/build/tamp_benchmark.elf verify
reset halt
load_image build/stm32h7b0-bench-data.bin 0x24000000 bin
arm semihosting enable
resume
