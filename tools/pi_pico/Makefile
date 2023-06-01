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

clone-heatshrink:
	if [ ! -d heatshrink ]; then \
		git clone https://github.com/atomicobject/heatshrink.git; \
	fi

clean-heatshrink:
	cd heatshrink && make clean

build-heatshrink: clone-heatshrink
	@cd heatshrink \
		&& export CC=arm-none-eabi-gcc \
		&& make libheatshrink_static.a \
		&& arm-none-eabi-size libheatshrink_static.a

clone-zlib:
	if [ ! -d zlib ]; then \
		git clone -b rp2xx https://github.com/kripton/zlib.git; \
	fi


size-zlib:
	@arm-none-eabi-size build/libzlib.a

clone-uzlib:
	if [ ! -d zlib ]; then \
		git clone https://github.com/pfalcon/uzlib.git; \
	fi
