.DEFAULT_GOAL := size

configure:
	@cmake -B build -S .

build: configure
	# Build the library / produces libtamp.a.
	@make -C build

size: build
	arm-none-eabi-size build/libtamp.a

fsize: build  # Function sizes
	arm-none-eabi-nm --print-size --size-sort --radix=d build/libtamp.a

clean:
	@rm -rf build
