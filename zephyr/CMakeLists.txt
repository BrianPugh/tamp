# Zephyr module build for the tamp C library.
# Enable with CONFIG_TAMP=y; see zephyr/Kconfig.

if(CONFIG_TAMP)
  set(TAMP_C_SRC ${CMAKE_CURRENT_LIST_DIR}/../tamp/_c_src)

  zephyr_include_directories(${TAMP_C_SRC})

  zephyr_library_named(tamp)
  zephyr_library_sources(${TAMP_C_SRC}/tamp/common.c)
  zephyr_library_sources_ifdef(CONFIG_TAMP_COMPRESSOR ${TAMP_C_SRC}/tamp/compressor.c)
  zephyr_library_sources_ifdef(CONFIG_TAMP_DECOMPRESSOR ${TAMP_C_SRC}/tamp/decompressor.c)
endif()
