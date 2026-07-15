idf_component_register(
    SRCS "main.c" "../../common/tamp_bench.c"
    INCLUDE_DIRS ""
    PRIV_INCLUDE_DIRS "../../common"
    EMBED_FILES
        data/enwik8-100kb
        data/enwik8-100kb.tamp
        data/vectors.bin
)
