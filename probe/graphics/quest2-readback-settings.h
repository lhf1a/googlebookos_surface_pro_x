/* Private, read-only diagnostics for Quest 2 v77 GLES only.
 * Original SHA256: a0d4d099a67d4a0db54ee6b5cd69e15e1787484128667120c7a90d6ae333b316
 * The launcher's SHA256SUMS check remains required: patchelf changes the linked
 * file's hash. Build ID plus instruction checks below identify its audited ABI;
 * they are not a replacement for whole-file integrity verification.
 * Call on the thread with the EGL context current, before glReadPixels.
 * No GL calls, memory writes, or unverified pointer dereferences are performed.
 */
#ifndef QUEST2_READBACK_SETTINGS_H
#define QUEST2_READBACK_SETTINGS_H

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int q2rb_map_has(uint64_t address, size_t size, int executable)
{
    if (!address || !size || address > INT64_MAX || size > INT64_MAX - address)
        return 0;
    FILE *maps = fopen("/proc/self/maps", "re");
    if (!maps) return 0;
    char line[4096], perms[5];
    unsigned long long start, end;
    int found = 0;
    while (fgets(line, sizeof(line), maps)) {
        if (sscanf(line, "%llx-%llx %4s", &start, &end, perms) != 3) continue;
        if (address >= start && address + size <= end && perms[0] == 'r' &&
            (!executable || perms[2] == 'x')) {
            found = 1;
            break;
        }
    }
    fclose(maps);
    return found;
}

static int q2rb_read(int mem, uint64_t address, void *data, size_t size)
{
    if (!q2rb_map_has(address, size, 0)) return 0;
    size_t done = 0;
    while (done < size) {
        ssize_t n = pread(mem, (char *)data + done, size - done,
                          (off_t)(address + done));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return 0;
        done += (size_t)n;
    }
    return 1;
}

static int q2rb_pointer_field(int mem, uint64_t object, uint64_t offset,
                             uint64_t *value)
{
    if (!object || object > INT64_MAX - offset) return 0;
    if (!q2rb_read(mem, object + offset, value, sizeof(*value))) return 0;
    /* Bionic heap pointers can carry an ARM64 top-byte tag. proc/self/maps
     * and pread offsets use the untagged VA; subsequent reads still require
     * a real readable mapping. Preserve the donor's -1 TLS sentinel. */
    if (*value != UINT64_MAX) *value &= UINT64_C(0x00ffffffffffffff);
    return 1;
}

static int q2rb_find_named_base(uint64_t *base, const char *basename)
{
    FILE *maps = fopen("/proc/self/maps", "re");
    if (!maps) return 0;
    char line[4096], perms[5];
    unsigned long long start, end, offset;
    unsigned matches = 0;
    while (fgets(line, sizeof(line), maps)) {
        int used = 0;
        if (sscanf(line, "%llx-%llx %4s %llx %*s %*u %n",
                   &start, &end, perms, &offset, &used) != 4 || !used)
            continue;
        char *path = line + used;
        path[strcspn(path, "\r\n")] = '\0';
        const char *name = strrchr(path, '/');
        if (!name || strcmp(name + 1, basename) || offset ||
            perms[0] != 'r' || start >= end)
            continue;
        *base = (uint64_t)start;
        matches++;
    }
    fclose(maps);
    return matches == 1;
}
static int q2rb_find_base(uint64_t *base)
{
    return q2rb_find_named_base(base, "libGLESv2_adreno.so");
}

/* Bounded traversal of the fixed v77 UMD merged-sync layout. This is a
 * read-only diagnostic, never an ownership inference permitting destruction. */
static void q2rb_sync_tree(int mem, uint64_t root)
{
    uint64_t gsl = 0, stack[512], seen[512];
    unsigned todo = 0, visited = 0, merged = 0, leaves = 0;
    int valid = q2rb_find_named_base(&gsl, "libgsl.so");
    if (root) stack[todo++] = root;
    while (valid && todo) {
        uint64_t node = stack[--todo] & UINT64_C(0x00ffffffffffffff), words[5];
        unsigned duplicate = 0;
        for (unsigned i = 0; i < visited; ++i) if (seen[i] == node) duplicate = 1;
        if (duplicate) continue;
        if (visited == 512 || !q2rb_read(mem, node, words, sizeof(words))) { valid = 0; break; }
        seen[visited++] = node;
        if ((words[1] & UINT64_C(0x00ffffffffffffff)) == gsl + 0x215088) {
            uint64_t count = words[3], array = words[2] & UINT64_C(0x00ffffffffffffff);
            if (!count || count > 512 - todo || !q2rb_read(mem, array, stack + todo, (size_t)count * 8)) {
                valid = 0; break;
            }
            todo += (unsigned)count; ++merged;
        } else if ((words[1] & UINT64_C(0x00ffffffffffffff)) == gsl + 0x213878) ++leaves;
        else valid = 0;
    }
    printf("QUEST2_PENDING_TREE valid=%d visited=%u merged=%u drm_leaves=%u\n", valid, visited, merged, leaves);
}

static int q2rb_build_id(int mem, uint64_t base)
{
    static const unsigned char expected[16] = {
        0xaf, 0x16, 0xb8, 0xea, 0x19, 0xe7, 0x2e, 0xa9,
        0xd2, 0x6d, 0x9b, 0xb4, 0x77, 0x00, 0x3d, 0xb3
    };
    Elf64_Ehdr elf;
    if (!q2rb_read(mem, base, &elf, sizeof(elf)) ||
        memcmp(elf.e_ident, ELFMAG, SELFMAG) ||
        elf.e_ident[EI_CLASS] != ELFCLASS64 ||
        elf.e_ident[EI_DATA] != ELFDATA2LSB ||
        elf.e_machine != EM_AARCH64 || elf.e_type != ET_DYN ||
        elf.e_phentsize != sizeof(Elf64_Phdr) ||
        !elf.e_phnum || elf.e_phnum > 64 || elf.e_phoff > 65536 ||
        base > INT64_MAX - 0x1000000)
        return 0;
    int zero_load = 0, match = 0;
    for (unsigned i = 0; i < elf.e_phnum; i++) {
        Elf64_Phdr ph;
        if (!q2rb_read(mem, base + elf.e_phoff + i * sizeof(ph), &ph, sizeof(ph)))
            return 0;
        if (ph.p_type == PT_LOAD && ph.p_offset == 0 && ph.p_vaddr == 0)
            zero_load = 1;
        if (ph.p_type != PT_NOTE || !ph.p_memsz) continue;
        if (ph.p_memsz > 4096 || ph.p_vaddr > 0x1000000 - ph.p_memsz)
            return 0;
        unsigned char notes[4096];
        if (!q2rb_read(mem, base + ph.p_vaddr, notes, (size_t)ph.p_memsz))
            return 0;
        size_t at = 0, length = (size_t)ph.p_memsz;
        while (length - at >= sizeof(Elf64_Nhdr)) {
            Elf64_Nhdr n;
            memcpy(&n, notes + at, sizeof(n));
            at += sizeof(n);
            if (n.n_namesz > length - at || n.n_descsz > length - at) return 0;
            size_t namesz = ((size_t)n.n_namesz + 3) & ~(size_t)3;
            size_t descsz = ((size_t)n.n_descsz + 3) & ~(size_t)3;
            if (namesz > length - at || descsz > length - at - namesz) return 0;
            if (n.n_type == NT_GNU_BUILD_ID && n.n_namesz == 4 &&
                !memcmp(notes + at, "GNU\0", 4)) {
                if (n.n_descsz != sizeof(expected) ||
                    memcmp(notes + at + namesz, expected, sizeof(expected)))
                    return 0;
                match = 1;
            }
            at += namesz + descsz;
        }
    }
    return zero_load && match;
}

static int q2rb_words(int mem, uint64_t address, const uint32_t *expected,
                     size_t count)
{
    uint32_t words[8];
    if (!count || count > sizeof(words) / sizeof(words[0]) ||
        !q2rb_map_has(address, count * sizeof(words[0]), 1) ||
        !q2rb_read(mem, address, words, count * sizeof(words[0])))
        return 0;
    return !memcmp(words, expected, count * sizeof(words[0]));
}

static int q2rb_audited_thunk(int mem, uint64_t base, uint64_t target)
{
    static const uint32_t direct[] = {0xf9400400, 0x17feb637};
    static const uint32_t validated[] = {0xaa0003f8, 0xf9400400};
    static const uint32_t validated_tail[] = {0x17fe4955};
    static const uint32_t traced[] = {0xf9400740};
    static const uint32_t traced_call[] = {0x97fd6109};
    if (target < base || !q2rb_map_has(target, 4, 1)) return 0;
    switch (target - base) {
    case 0x1d1db8:
        return q2rb_words(mem, target, direct, 2);
    case 0x1ed098:
        return q2rb_words(mem, base + 0x1ed0b0, validated, 2) &&
               q2rb_words(mem, base + 0x1ed144, validated_tail, 1);
    case 0x2271d0:
        return q2rb_words(mem, base + 0x227254, traced, 1) &&
               q2rb_words(mem, base + 0x227274, traced_call, 1);
    default:
        return 0;
    }
}

static void quest2_log_readback_settings(void)
{
    int saved_errno = errno;
    int mem = -1;
    const char *failure = "unique_donor_mapping";
    uint64_t base = 0, dispatch = 0, target = 0, real = 0, settings = 0;
    uint32_t limits[2] = {0, 0};
    uint32_t context_flags = 0;
    const char *source = "global";
    static const uint32_t global_setup[] = {0x90001849, 0xaa0603f3, 0x912b4129};
    static const uint32_t global_load[] = {0xf9413528};
    static const uint32_t tls_key[] = {0xb9402d20, 0x940bae63, 0xaa0003e8};
    static const uint32_t slot_load[] = {0xf9417909};
    static const uint32_t settings_load[] = {0xf9405668, 0xb9657d0f};
    static const uint32_t flags_load[] = {0xb9408a7a};
    if (sizeof(void *) != 8 || !q2rb_find_base(&base)) goto done;
    failure = "self_mem_open";
    mem = open("/proc/self/mem", O_RDONLY | O_CLOEXEC);
    if (mem < 0) goto done;
    failure = "donor_build_id_or_elf";
    if (!q2rb_build_id(mem, base)) goto done;
    failure = "audited_instruction_bytes";
    if (!q2rb_words(mem, base + 0xfdea0, global_setup, 3) ||
        !q2rb_words(mem, base + 0xfdec0, global_load, 1) ||
        !q2rb_words(mem, base + 0xfded0, tls_key, 3) ||
        !q2rb_words(mem, base + 0xfdedc, slot_load, 1) ||
        !q2rb_words(mem, base + 0x1bb880, settings_load, 2) ||
        !q2rb_words(mem, base + 0x1bb718, flags_load, 1)) goto done;
    failure = "global_context_read";
    if (!q2rb_pointer_field(mem, base + 0x405ad0, 616, &dispatch)) goto done;
    if (dispatch == UINT64_MAX) {
        pthread_key_t key;
        failure = "tls_key_read";
        if (sizeof(key) != 4 ||
            !q2rb_read(mem, base + 0x405ad0 + 44, &key, sizeof(key))) goto done;
        /* The donor uses this exact key with pthread_getspecific. The returned
         * pointer is only converted to an integer; reads still use pread. */
        dispatch = (uint64_t)(uintptr_t)pthread_getspecific(key) & UINT64_C(0x00ffffffffffffff);
        source = "pthread_tls";
    }
    failure = "dispatch_slot_read";
    if (!q2rb_pointer_field(mem, dispatch, 752, &target)) goto done;
    failure = "unrecognized_dispatch_thunk";
    if (!q2rb_audited_thunk(mem, base, target)) goto done;
    failure = "real_context_read";
    if (!q2rb_pointer_field(mem, dispatch, 8, &real)) goto done;
    failure = "settings_pointer_read";
    if (!q2rb_pointer_field(mem, real, 168, &settings)) goto done;
    failure = "context_flags_read";
    if (!real || real > INT64_MAX - 140 ||
        !q2rb_read(mem, real + 136, &context_flags, sizeof(context_flags))) goto done;
    failure = "settings_values_read";
    if (!settings || settings > INT64_MAX - 9604 ||
        !q2rb_read(mem, settings + 9596, limits, sizeof(limits))) goto done;
    printf("QUEST2_READBACK_SETTINGS status=ok source=%s thunk_va=0x%" PRIx64
           " byte_threshold=%" PRIu32 " percent_threshold=%" PRIu32
           " context_flags_136=0x%08" PRIx32 "\n",
           source, target - base, limits[0], limits[1], context_flags);
    failure = NULL;
    /* The same fixed real-context ABI carries the pending server-wait sync.
     * Read only: this must never release or overwrite a donor-owned object. */
    if (getenv("SQ2_COMPOSITION_FD_DIAGNOSTIC")) {
        uint64_t pending = 0, words[5] = {0, 0, 0, 0, 0};
        int readable = q2rb_pointer_field(mem, real, 16296, &pending) &&
            (!pending || q2rb_read(mem, pending, words, sizeof(words)));
        printf("QUEST2_PENDING_SYNC readable=%d object=0x%" PRIx64
               " words=%" PRIx64 ",%" PRIx64 ",%" PRIx64 ",%" PRIx64 ",%" PRIx64 "\n",
               readable, pending, words[0], words[1], words[2], words[3], words[4]);
        if (readable) q2rb_sync_tree(mem, pending);
    }
done:
    if (failure) {
        printf("QUEST2_READBACK_SETTINGS status=unavailable reason=%s", failure);
        if (base && target >= base && target - base < 0x1000000)
            printf(" thunk_va=0x%" PRIx64, target - base);
        putchar('\n');
    }
    if (mem >= 0) close(mem);
    errno = saved_errno;
}

#endif
