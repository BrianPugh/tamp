/*
 * QEMU TCG plugin: per-PC executed-instruction counters.
 *
 * Counts every executed guest instruction. Instructions whose PC falls inside
 * [base, base+size) get an individual counter (2-byte granularity, Thumb);
 * everything else lands in a single "other" bucket. On exit the histogram is
 * dumped to `out` as "<hex-vaddr> <count>" lines, preceded by totals.
 *
 * Deterministic across runs (single vCPU, no interrupts in the harness), so
 * count deltas between two builds are exact.
 *
 * Build (macOS):
 *   cc -O2 -dynamiclib -undefined dynamic_lookup \
 *      $(pkg-config --cflags glib-2.0) -I<qemu-include> \
 *      -o insncount.dylib insncount.c
 */
#include <glib.h>
#include <inttypes.h>
#include <qemu-plugin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

QEMU_PLUGIN_EXPORT int qemu_plugin_version = QEMU_PLUGIN_VERSION;

static uint64_t *counters; /* one per halfword in [base, base+size) */
static uint64_t other_count;
static uint64_t base = 0x0;
static uint64_t size = 0x100000; /* 1 MiB default code region */
static char out_path[4096] = "insncount.out";

static void vcpu_insn_exec(unsigned int vcpu_index, void *udata) {
    (void)vcpu_index;
    ++*(uint64_t *)udata;
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb) {
    (void)id;
    size_t n = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t vaddr = qemu_plugin_insn_vaddr(insn);
        uint64_t *slot;
        if (vaddr >= base && vaddr < base + size) {
            slot = &counters[(vaddr - base) >> 1];
        } else {
            slot = &other_count;
        }
        qemu_plugin_register_vcpu_insn_exec_cb(insn, vcpu_insn_exec, QEMU_PLUGIN_CB_NO_REGS, slot);
    }
}

static void plugin_exit(qemu_plugin_id_t id, void *p) {
    (void)id;
    (void)p;
    FILE *f = fopen(out_path, "w");
    if (!f) {
        qemu_plugin_outs("insncount: failed to open output file\n");
        return;
    }
    uint64_t total = other_count;
    uint64_t nslots = size >> 1;
    for (uint64_t i = 0; i < nslots; i++) total += counters[i];
    fprintf(f, "# total %" PRIu64 "\n", total);
    fprintf(f, "# other %" PRIu64 "\n", other_count);
    for (uint64_t i = 0; i < nslots; i++) {
        if (counters[i]) {
            fprintf(f, "%" PRIx64 " %" PRIu64 "\n", base + (i << 1), counters[i]);
        }
    }
    fclose(f);
}

QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id, const qemu_info_t *info, int argc, char **argv) {
    (void)info;
    for (int i = 0; i < argc; i++) {
        char *opt = argv[i];
        if (g_str_has_prefix(opt, "out=")) {
            snprintf(out_path, sizeof(out_path), "%s", opt + 4);
        } else if (g_str_has_prefix(opt, "base=")) {
            base = g_ascii_strtoull(opt + 5, NULL, 0);
        } else if (g_str_has_prefix(opt, "size=")) {
            size = g_ascii_strtoull(opt + 5, NULL, 0);
        } else {
            fprintf(stderr, "insncount: unknown option: %s\n", opt);
            return -1;
        }
    }
    counters = calloc(size >> 1, sizeof(uint64_t));
    if (!counters) {
        fprintf(stderr, "insncount: OOM allocating counters\n");
        return -1;
    }
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
