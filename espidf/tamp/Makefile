# Core C source files needed for ESP-IDF component (excludes desktop-only files)
TAMP_C_FILES := \
	common.c \
	common.h \
	compressor.c \
	compressor.h \
	decompressor.c \
	decompressor.h

component:
	cp ../../README.md .
	mkdir -p tamp
	$(foreach f,$(TAMP_C_FILES),cp ../../tamp/_c_src/tamp/$(f) tamp/;)
	compote component pack --name=tamp --version=0.0.0
