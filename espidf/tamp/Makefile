# Core C source files needed for ESP-IDF component (excludes desktop-only
# files). The *_fast/_compact/_embedded variants are #include'd by their
# parent .c, not compiled separately; the embedded match finder is required
# for TAMP_ESP32=0 builds (e.g. the noopt benchmark variant).
TAMP_C_FILES := \
	common.c \
	common.h \
	common_window_copy_compact.c \
	common_window_copy_fast.c \
	compressor.c \
	compressor.h \
	compressor_find_match_embedded.c \
	decompressor.c \
	decompressor.h \
	decompressor_refill_compact.c \
	decompressor_refill_fast.c

stage:
	cp ../../README.md .
	mkdir -p tamp
	$(foreach f,$(TAMP_C_FILES),cp ../../tamp/_c_src/tamp/$(f) tamp/;)

component: stage
	compote component pack --name=tamp --version=0.0.0
