idf_component_register(
    SRCS "test_main.c" "test_esp32_compressor.c" "test_esp32_decompressor.c" "test_search_consistency.c"
    INCLUDE_DIRS "."
    REQUIRES tamp unity
)
