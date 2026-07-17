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

/* One scoreboard whose per-vcpu element is the whole counter array: nslots
 * per-halfword buckets for [base, base+size) followed by a single "other"
 * bucket. Counting happens inline in generated code (no helper call). */
static struct qemu_plugin_scoreboard *score;
static uint64_t nslots; /* number of per-halfword buckets; slot nslots is "other" */
static uint64_t base = 0x0;
static uint64_t size = 0x100000; /* 1 MiB default code region */
static char out_path[4096] = "insncount.out";

/* qemu_plugin_u64 addressing the counter at byte offset `slot * 8` in the element. */
static qemu_plugin_u64 slot_entry(uint64_t slot) {
    qemu_plugin_u64 entry = {score, (size_t)(slot * sizeof(uint64_t))};
    return entry;
}

static void vcpu_tb_trans(qemu_plugin_id_t id, struct qemu_plugin_tb *tb) {
    (void)id;
    size_t n = qemu_plugin_tb_n_insns(tb);
    for (size_t i = 0; i < n; i++) {
        struct qemu_plugin_insn *insn = qemu_plugin_tb_get_insn(tb, i);
        uint64_t vaddr = qemu_plugin_insn_vaddr(insn);
        uint64_t slot;
        if (vaddr >= base && vaddr < base + size) {
            slot = (vaddr - base) >> 1;
        } else {
            slot = nslots; /* "other" bucket */
        }
        qemu_plugin_register_vcpu_insn_exec_inline_per_vcpu(insn, QEMU_PLUGIN_INLINE_ADD_U64, slot_entry(slot), 1);
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
    uint64_t other_count = qemu_plugin_u64_sum(slot_entry(nslots));
    uint64_t total = other_count;
    for (uint64_t i = 0; i < nslots; i++) total += qemu_plugin_u64_sum(slot_entry(i));
    fprintf(f, "# total %" PRIu64 "\n", total);
    fprintf(f, "# other %" PRIu64 "\n", other_count);
    for (uint64_t i = 0; i < nslots; i++) {
        uint64_t count = qemu_plugin_u64_sum(slot_entry(i));
        if (count) {
            fprintf(f, "%" PRIx64 " %" PRIu64 "\n", base + (i << 1), count);
        }
    }
    fclose(f);
    qemu_plugin_scoreboard_free(score);
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
    nslots = size >> 1;
    /* One element per vcpu, sized to hold every PC bucket plus the "other" bucket. */
    score = qemu_plugin_scoreboard_new((nslots + 1) * sizeof(uint64_t));
    if (!score) {
        fprintf(stderr, "insncount: OOM allocating scoreboard\n");
        return -1;
    }
    qemu_plugin_register_vcpu_tb_trans_cb(id, vcpu_tb_trans);
    qemu_plugin_register_atexit_cb(id, plugin_exit, NULL);
    return 0;
}
