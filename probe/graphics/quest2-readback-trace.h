/* PRIVATE PROBE ONLY: Quest 2 v77 CPU-transfer descriptor trace.
 *
 * Include in ONE probe translation unit. After making the donor EGL context
 * current, and BEFORE submitting any work, call:
 *     quest2_install_readback_trace(6);
 * Installation failure terminates this probe with exit status 125. At the end,
 * quest2_report_readback_trace() distinguishes no calls from a successful trace.
 * The hook lasts only until this process exits; do not dlclose the donor.
 *
 * PRECONDITION: no concurrent donor calls on any thread during installation.
 * This is not a live-patching API for an application or SurfaceFlinger. The
 * private donor text page is briefly RW (not executable), then restored RX.
 * No on-disk file, other process, GPU identity, or descriptor is modified.
 * Logs contain raw private-process memory; keep them under ignored out/.
 *
 * Audited donor: original GLES SHA256 a0d4d099a67d4a0db54ee6b5cd69e15e1787484128667120c7a90d6ae333b316
 * Build ID: af16b8ea19e72ea9d26d9bb477003db3.
 * Target VA 0x296390, original function span [0x296390,0x2971e8).
 * Function SHA256: 984f8991d6b4235e693e6264693a165530d67a63c8efaa8f4c08e8916d7fed60
 * The launcher MUST also verify its candidate's whole-file SHA256SUMS. The
 * Build ID / exact prologue / full-function FNV fingerprint below detect ABI
 * drift; FNV is not a cryptographic integrity or authenticity check.
 *
 * ABI audit: callers including 0x11b3f4 and 0x11c274 prepare x0/x1 and consume
 * w0. Entry copies those to x20/x19; observed other argument registers are
 * written before use. Since the complete internal C type is unavailable, the
 * stub does not call it through an assumed int(void *,void *) prototype: it
 * preserves x0-x17, x29/x30, q0-q31, NZCV, FPCR and FPSR around logging, restores
 * the original SP (including stack arguments), and tail-branches. x16 is the
 * AAPCS64 IP0 scratch used by both absolute jumps, not an argument. Original
 * return registers and return address remain under the donor's control.
 */
#ifndef QUEST2_READBACK_TRACE_H
#define QUEST2_READBACK_TRACE_H

#include "quest2-readback-settings.h"
#include <stdlib.h>
#include <sys/mman.h>

#if defined(__aarch64__)

enum { Q2RT_TARGET_VA = 0x296390, Q2RT_CODE_SIZE = 0xe58,
       Q2RT_SNAPSHOT_SIZE = 256, Q2RT_MAX_RECORDS = 32 };

static uintptr_t q2rt_trampoline __asm__("q2rt_trampoline")
    __attribute__((used));
static uint64_t q2rt_base;
static int q2rt_mem = -1;
static unsigned q2rt_limit, q2rt_calls;
static int q2rt_installed;

static __attribute__((noreturn)) void q2rt_fail(const char *reason)
{
    int error = errno;
    fprintf(stderr, "QUEST2_READBACK_TRACE status=install_failed reason=%s errno=%d\n",
            reason, error);
    fflush(stderr);
    _exit(125);
}

static uint64_t q2rt_untag(uint64_t address)
{
    return address & UINT64_C(0x00ffffffffffffff);
}

static uint64_t q2rt_code_fingerprint(const unsigned char *code, size_t size)
{
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (size_t i = 0; i < size; i++)
        hash = (hash ^ code[i]) * UINT64_C(0x100000001b3);
    return hash;
}

/* Verify the WHOLE page being made writable, including MAP_PRIVATE and the
 * exact donor filename. q2rb_read() separately checks every readable range. */
static int q2rt_private_rx_page(uint64_t page, size_t length)
{
    if (!page || !length || page > INT64_MAX - length) return 0;
    FILE *maps = fopen("/proc/self/maps", "re");
    if (!maps) return 0;
    char line[4096], perms[5];
    unsigned long long start, end, offset;
    int found = 0;
    while (fgets(line, sizeof(line), maps)) {
        int used = 0;
        if (sscanf(line, "%llx-%llx %4s %llx %*s %*u %n",
                   &start, &end, perms, &offset, &used) != 4 || !used)
            continue;
        char *path = line + used;
        path[strcspn(path, "\r\n")] = '\0';
        const char *name = strrchr(path, '/');
        if (!name || strcmp(name + 1, "libGLESv2_adreno.so") ||
            strcmp(perms, "r-xp") || page < start || page + length > end)
            continue;
        /* The audited text LOAD has p_offset == p_vaddr. */
        if (page < q2rt_base || page - start > UINT64_MAX - offset ||
            offset + page - start != page - q2rt_base)
            continue;
        found = 1;
        break;
    }
    fclose(maps);
    return found;
}

static int q2rt_context_current(void)
{
    uint64_t dispatch = 0, target = 0, real = 0;
    if (!q2rb_pointer_field(q2rt_mem, q2rt_base + 0x405ad0, 616, &dispatch))
        return 0;
    if (dispatch == UINT64_MAX) {
        pthread_key_t key;
        if (sizeof(key) != 4 ||
            !q2rb_read(q2rt_mem, q2rt_base + 0x405ad0 + 44, &key, sizeof(key)))
            return 0;
        dispatch = q2rt_untag((uint64_t)(uintptr_t)pthread_getspecific(key));
    }
    return q2rb_pointer_field(q2rt_mem, dispatch, 752, &target) &&
           q2rb_audited_thunk(q2rt_mem, q2rt_base, target) &&
           q2rb_pointer_field(q2rt_mem, dispatch, 8, &real) &&
           q2rb_map_has(real, 176, 0);
}

static void q2rt_dump(unsigned sequence, const char *kind,
                      const unsigned char bytes[Q2RT_SNAPSHOT_SIZE])
{
    fprintf(stderr, "QUEST2_READBACK_TRACE seq=%u kind=%s bytes256=", sequence, kind);
    for (size_t i = 0; i < Q2RT_SNAPSHOT_SIZE; i++)
        fprintf(stderr, "%02x", bytes[i]);
    fputc('\n', stderr);
    fprintf(stderr, "QUEST2_READBACK_TRACE seq=%u kind=%s fields", sequence, kind);
    for (size_t offset = 200; offset <= 236; offset += 4) {
        uint32_t value;
        memcpy(&value, bytes + offset, sizeof(value));
        fprintf(stderr, " u32_%zu=%" PRIu32 "(0x%08" PRIx32 ")",
                offset, value, value);
    }
    fputc('\n', stderr);
}

/* Optional bounded snapshot of the already CPU-mapped source, for OFFLINE
 * address-formula comparison. Never write pixels or descriptor fields. */
static void q2rt_save_source(unsigned sequence, const unsigned char *image)
{
    const char *enabled = getenv("SQ2_READBACK_SAVE_SOURCE");
    if (!enabled || strcmp(enabled, "1")) return;
    uint64_t address, length, pitch;
    uint32_t format, layout, height, samples;
    memcpy(&address, image + 16, 8);
    memcpy(&pitch, image + 24, 8);
    memcpy(&length, image + 32, 8);
    memcpy(&height, image + 208, 4);
    memcpy(&samples, image + 216, 4);
    memcpy(&format, image + 224, 4);
    memcpy(&layout, image + 232, 4);
    address = q2rt_untag(address);
    if (format != 28 || layout != 2 || samples != 1 || !height || !pitch ||
        length > 16 * 1024 * 1024 || !length || pitch > length / height ||
        pitch * height != length ||
        !q2rb_map_has(address, (size_t)length, 0)) {
        fprintf(stderr, "QUEST2_READBACK_SOURCE seq=%u status=rejected_layout_or_bounds\n", sequence);
        return;
    }
    char path[160];
    snprintf(path, sizeof(path), "/data/local/tmp/sq2-closed/readpixels/capture-%u.bin", sequence);
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    size_t done = 0;
    int closed = -1;
    if (fd >= 0) {
        while (done < length) {
            /* proc/self/mem rejects some DRM PFN mappings. write performs a
             * bounded kernel copy_from_user on this readable mapping instead;
             * an invalid source returns EFAULT. No C pointer dereference. The
             * descriptor's original CPU copy follows on this same thread. */
            ssize_t written = write(fd, (const void *)(uintptr_t)(address + done),
                                    (size_t)length - done);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) break;
            done += (size_t)written;
        }
        closed = close(fd);
    }
    fprintf(stderr, "QUEST2_READBACK_SOURCE seq=%u status=%s bytes=%zu pitch=%" PRIu64
            " padded_height=%" PRIu32 "\n", sequence,
            fd >= 0 && done == length && !closed ? "saved" : "file_error", done, pitch, height);
}

static __attribute__((used, noinline)) void q2rt_log_snapshot(
    uint64_t context, uint64_t descriptor, uint64_t caller)
    __asm__("q2rt_log_snapshot");

static __attribute__((used, noinline)) void q2rt_log_snapshot(
    uint64_t context, uint64_t descriptor, uint64_t caller)
{
    int saved_errno = errno;
    unsigned sequence = __atomic_fetch_add(&q2rt_calls, 1u, __ATOMIC_RELAXED) + 1u;
    if (sequence > q2rt_limit || !sequence) {
        errno = saved_errno;
        return;
    }
    unsigned char transfer[Q2RT_SNAPSHOT_SIZE], image[Q2RT_SNAPSHOT_SIZE];
    descriptor = q2rt_untag(descriptor);
    caller = q2rt_untag(caller);
    flockfile(stderr);
    fprintf(stderr, "QUEST2_READBACK_TRACE seq=%u phase=enter", sequence);
    if (caller >= q2rt_base && caller - q2rt_base < 0x1000000)
        fprintf(stderr, " caller_va=0x%" PRIx64, caller - q2rt_base);
    else
        fputs(" caller_va=outside_donor", stderr);
    fputc('\n', stderr);
    context = q2rt_untag(context);
    if (q2rb_read(q2rt_mem, context, image, sizeof(image))) {
        q2rt_dump(sequence, "copy_context", image);
        uint32_t layout_words[13];
        if (context <= INT64_MAX - 340 &&
            q2rb_read(q2rt_mem, context + 288, layout_words, sizeof(layout_words))) {
            fprintf(stderr, "QUEST2_READBACK_TRACE seq=%u kind=copy_context_layout fields",
                    sequence);
            for (unsigned i = 0; i < 13; ++i)
                fprintf(stderr, " u32_%u=%" PRIu32 "(0x%08" PRIx32 ")",
                        288 + i * 4, layout_words[i], layout_words[i]);
            fputc('\n', stderr);
        }
        uint64_t vtable = 0;
        if (q2rb_pointer_field(q2rt_mem, context, 0, &vtable)) {
            for (unsigned offset = 824; offset <= 832; offset += 8) {
                uint64_t function = 0;
                fprintf(stderr, "QUEST2_READBACK_TRACE seq=%u vtable_slot=%u",
                        sequence, offset);
                if (q2rb_pointer_field(q2rt_mem, vtable, offset, &function) &&
                    function >= q2rt_base && function - q2rt_base < 0x1000000 &&
                    q2rb_map_has(function, 4, 1))
                    fprintf(stderr, " function_va=0x%" PRIx64, function - q2rt_base);
                else
                    fputs(" function_va=unavailable_or_outside_donor", stderr);
                fputc('\n', stderr);
            }
        }
    } else {
        fprintf(stderr, "QUEST2_READBACK_TRACE seq=%u kind=copy_context status=unreadable\n",
                sequence);
    }
    if (!q2rb_read(q2rt_mem, descriptor, transfer, sizeof(transfer))) {
        fprintf(stderr, "QUEST2_READBACK_TRACE seq=%u kind=transfer status=unreadable\n",
                sequence);
    } else {
        q2rt_dump(sequence, "transfer", transfer);
        for (unsigned offset = 56; offset <= 64; offset += 8) {
            uint64_t address;
            const char *kind = offset == 56 ? "image56" : "image64";
            memcpy(&address, transfer + offset, sizeof(address));
            address = q2rt_untag(address);
            if (q2rb_read(q2rt_mem, address, image, sizeof(image))) {
                q2rt_dump(sequence, kind, image);
                if (offset == 56) q2rt_save_source(sequence, image);
            } else
                fprintf(stderr, "QUEST2_READBACK_TRACE seq=%u kind=%s status=unreadable\n",
                        sequence, kind);
        }
    }
    fflush(stderr);
    funlockfile(stderr);
    errno = saved_errno;
}

/* Naked, tail-call entry: no compiler prologue or assumed donor return type.
 * 704 bytes is 16-byte aligned. x18 is Android's reserved platform register;
 * x19-x28 remain preserved by the C logger's ABI. The original prologue later
 * saves the original caller frame/link registers, not this logger's frame. */
static __attribute__((naked, used)) void q2rt_entry(void)
{
    __asm__ volatile(
        ".inst 0xd503245f\n" /* bti c; a hint on hardware without BTI */
        "sub sp, sp, #704\n"
        "stp x0, x1, [sp, #0]\n"
        "stp x2, x3, [sp, #16]\n"
        "stp x4, x5, [sp, #32]\n"
        "stp x6, x7, [sp, #48]\n"
        "stp x8, x9, [sp, #64]\n"
        "stp x10, x11, [sp, #80]\n"
        "stp x12, x13, [sp, #96]\n"
        "stp x14, x15, [sp, #112]\n"
        "stp x16, x17, [sp, #128]\n"
        "stp x29, x30, [sp, #144]\n"
        "mrs x9, nzcv\n"
        "mrs x10, fpcr\n"
        "mrs x11, fpsr\n"
        "stp x9, x10, [sp, #160]\n"
        "str x11, [sp, #176]\n"
        "stp q0, q1, [sp, #192]\n"
        "stp q2, q3, [sp, #224]\n"
        "stp q4, q5, [sp, #256]\n"
        "stp q6, q7, [sp, #288]\n"
        "stp q8, q9, [sp, #320]\n"
        "stp q10, q11, [sp, #352]\n"
        "stp q12, q13, [sp, #384]\n"
        "stp q14, q15, [sp, #416]\n"
        "stp q16, q17, [sp, #448]\n"
        "stp q18, q19, [sp, #480]\n"
        "stp q20, q21, [sp, #512]\n"
        "stp q22, q23, [sp, #544]\n"
        "stp q24, q25, [sp, #576]\n"
        "stp q26, q27, [sp, #608]\n"
        "stp q28, q29, [sp, #640]\n"
        "stp q30, q31, [sp, #672]\n"
        "mov x2, x30\n"
        "bl q2rt_log_snapshot\n"
        "ldp q0, q1, [sp, #192]\n"
        "ldp q2, q3, [sp, #224]\n"
        "ldp q4, q5, [sp, #256]\n"
        "ldp q6, q7, [sp, #288]\n"
        "ldp q8, q9, [sp, #320]\n"
        "ldp q10, q11, [sp, #352]\n"
        "ldp q12, q13, [sp, #384]\n"
        "ldp q14, q15, [sp, #416]\n"
        "ldp q16, q17, [sp, #448]\n"
        "ldp q18, q19, [sp, #480]\n"
        "ldp q20, q21, [sp, #512]\n"
        "ldp q22, q23, [sp, #544]\n"
        "ldp q24, q25, [sp, #576]\n"
        "ldp q26, q27, [sp, #608]\n"
        "ldp q28, q29, [sp, #640]\n"
        "ldp q30, q31, [sp, #672]\n"
        "ldp x9, x10, [sp, #160]\n"
        "ldr x11, [sp, #176]\n"
        "msr nzcv, x9\n"
        "msr fpcr, x10\n"
        "msr fpsr, x11\n"
        "ldp x0, x1, [sp, #0]\n"
        "ldp x2, x3, [sp, #16]\n"
        "ldp x4, x5, [sp, #32]\n"
        "ldp x6, x7, [sp, #48]\n"
        "ldp x8, x9, [sp, #64]\n"
        "ldp x10, x11, [sp, #80]\n"
        "ldp x12, x13, [sp, #96]\n"
        "ldp x14, x15, [sp, #112]\n"
        "ldp x16, x17, [sp, #128]\n"
        "ldp x29, x30, [sp, #144]\n"
        "adrp x16, q2rt_trampoline\n"
        "ldr x16, [x16, :lo12:q2rt_trampoline]\n"
        "add sp, sp, #704\n"
        "br x16\n");
}

static void q2rt_absolute_jump(unsigned char bytes[16], uint64_t destination)
{
    const uint32_t instructions[2] = {0x58000050, 0xd61f0200};
    memcpy(bytes, instructions, sizeof(instructions));
    memcpy(bytes + 8, &destination, sizeof(destination));
}

static inline void quest2_install_readback_trace(unsigned max_records)
{
    int saved_errno = errno;
    static const uint32_t prologue[4] = {
        0xd10743ff, 0xa9177bfd, 0x9105c3fd, 0xa9186ffc
    };
    unsigned char original[Q2RT_CODE_SIZE], jump[16], verify[16];
    if (q2rt_installed) q2rt_fail("already_installed");
    if (!max_records || max_records > Q2RT_MAX_RECORDS)
        q2rt_fail("record_limit_must_be_1_to_32");
    if (sizeof(void *) != 8 || !q2rb_find_base(&q2rt_base))
        q2rt_fail("unique_donor_mapping");
    q2rt_mem = open("/proc/self/mem", O_RDONLY | O_CLOEXEC);
    if (q2rt_mem < 0) q2rt_fail("self_mem_open");
    if (!q2rb_build_id(q2rt_mem, q2rt_base))
        q2rt_fail("donor_build_id_or_elf");
    uint64_t target = q2rt_base + Q2RT_TARGET_VA;
    if (!q2rb_words(q2rt_mem, target, prologue, 4) ||
        !q2rb_read(q2rt_mem, target, original, sizeof(original)) ||
        q2rt_code_fingerprint(original, sizeof(original)) != UINT64_C(0x2182b6ed3b456476))
        q2rt_fail("audited_function_bytes");
    if (!q2rt_context_current()) q2rt_fail("audited_current_context");
    /* This also keeps the settings header's optional static API in use. */
    quest2_log_readback_settings();
    long page_value = sysconf(_SC_PAGESIZE);
    if (page_value < 4096 || page_value > 65536 ||
        ((unsigned long)page_value & ((unsigned long)page_value - 1)))
        q2rt_fail("unsupported_page_size");
    size_t page_size = (size_t)page_value;
    uint64_t page = target & ~((uint64_t)page_size - 1);
    if (target + sizeof(jump) > page + page_size ||
        !q2rt_private_rx_page(page, page_size))
        q2rt_fail("target_page_not_private_rx_donor");
    unsigned char *trampoline = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
                                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (trampoline == MAP_FAILED) q2rt_fail("trampoline_allocate");
    /* All four relocated instructions are position-independent stack setup. */
    memcpy(trampoline, original, 16);
    q2rt_absolute_jump(trampoline + 16, target + 16);
    if (mprotect(trampoline, page_size, PROT_READ | PROT_EXEC))
        q2rt_fail("trampoline_rx");
    __builtin___clear_cache((char *)trampoline, (char *)trampoline + 32);
    q2rt_trampoline = (uintptr_t)trampoline;
    q2rt_limit = max_records;
    __atomic_store_n(&q2rt_calls, 0u, __ATOMIC_RELAXED);
    q2rt_absolute_jump(jump, (uint64_t)(uintptr_t)&q2rt_entry);
    /* Single-threaded quiescent installation is required. These sixteen bytes
     * are NOT an atomic hot-patch. A failure after RW still kills only this
     * private probe; it must never continue with a non-executable donor page. */
    if (mprotect((void *)(uintptr_t)page, page_size, PROT_READ | PROT_WRITE))
        q2rt_fail("donor_page_rw");
    memcpy((void *)(uintptr_t)target, jump, sizeof(jump));
    __builtin___clear_cache((char *)(uintptr_t)target,
                            (char *)(uintptr_t)target + sizeof(jump));
    if (mprotect((void *)(uintptr_t)page, page_size, PROT_READ | PROT_EXEC))
        q2rt_fail("donor_page_restore_rx");
    if (!q2rb_read(q2rt_mem, target, verify, sizeof(verify)) ||
        memcmp(verify, jump, sizeof(jump)) ||
        !q2rt_private_rx_page(page, page_size))
        q2rt_fail("installed_patch_or_rx_verification");
    q2rt_installed = 1;
    fprintf(stderr, "QUEST2_READBACK_TRACE status=installed target_va=0x296390"
            " max_records=%u page_size=%zu mode=process_private\n", max_records, page_size);
    fflush(stderr);
    errno = saved_errno;
}

static inline void quest2_report_readback_trace(void)
{
    int saved_errno = errno;
    unsigned count = __atomic_load_n(&q2rt_calls, __ATOMIC_RELAXED);
    fprintf(stderr, "QUEST2_READBACK_TRACE status=summary installed=%d calls=%u records=%u\n",
            q2rt_installed, count, count < q2rt_limit ? count : q2rt_limit);
    fflush(stderr);
    errno = saved_errno;
}

#else

static inline void quest2_install_readback_trace(unsigned max_records)
{
    (void)max_records;
    /* Reference the shared diagnostic API without invoking it on this host. */
    (void)quest2_log_readback_settings;
    fputs("QUEST2_READBACK_TRACE status=install_failed reason=aarch64_required\n", stderr);
    fflush(stderr);
    _exit(125);
}

static inline void quest2_report_readback_trace(void)
{
    fputs("QUEST2_READBACK_TRACE status=summary installed=0 calls=0 records=0\n", stderr);
}

#endif
#endif
