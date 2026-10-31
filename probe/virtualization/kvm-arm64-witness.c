/* SPDX-License-Identifier: MIT
 * Minimal ARM64 KVM guest witness. No disks, network, firmware or external input.
 * Run only after an isolated EL2 kernel has initialized /dev/kvm.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/kvm.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

static volatile sig_atomic_t expired;
static void timeout_handler(int sig) { (void)sig; expired = 1; }
static void fail(const char *what) { perror(what); exit(1); }
static void core_reg(int cpu, unsigned long offset, uint64_t value)
{
    struct kvm_one_reg r = {
        .id = KVM_REG_ARM64 | KVM_REG_SIZE_U64 | KVM_REG_ARM_CORE | offset,
        .addr = (uint64_t)(uintptr_t)&value,
    };
    if (ioctl(cpu, KVM_SET_ONE_REG, &r) < 0) fail("KVM_SET_ONE_REG");
}
int main(void)
{
    int kvm = open("/dev/kvm", O_RDWR | O_CLOEXEC);
    if (kvm < 0) fail("open /dev/kvm");
    int api = ioctl(kvm, KVM_GET_API_VERSION, 0);
    if (api != KVM_API_VERSION) {
        fprintf(stderr, "Unexpected KVM API %d\n", api); return 1;
    }
    int vm = ioctl(kvm, KVM_CREATE_VM, 0);
    if (vm < 0) fail("KVM_CREATE_VM");
    long page = sysconf(_SC_PAGESIZE);
    if (page < 4096) { fprintf(stderr, "Invalid host page size\n"); return 1; }
    void *mem = mmap(NULL, (size_t)page, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) fail("guest mmap");
    /* mov x0,#42; mov x1,#0x100000; str x0,[x1]; b . */
    const uint32_t code[] = {0xd2800540, 0xd2a00201, 0xf9000020, 0x14000000};
    memcpy(mem, code, sizeof(code));
    struct kvm_userspace_memory_region slot = {
        .slot = 0, .guest_phys_addr = 0x40000000,
        .memory_size = (uint64_t)page, .userspace_addr = (uintptr_t)mem,
    };
    if (ioctl(vm, KVM_SET_USER_MEMORY_REGION, &slot) < 0) fail("KVM_SET_USER_MEMORY_REGION");
    int cpu = ioctl(vm, KVM_CREATE_VCPU, 0);
    if (cpu < 0) fail("KVM_CREATE_VCPU");
    struct kvm_vcpu_init init = {0};
    if (ioctl(vm, KVM_ARM_PREFERRED_TARGET, &init) < 0) fail("KVM_ARM_PREFERRED_TARGET");
    if (ioctl(cpu, KVM_ARM_VCPU_INIT, &init) < 0) fail("KVM_ARM_VCPU_INIT");
    core_reg(cpu, offsetof(struct kvm_regs, regs.pc) / sizeof(uint32_t), 0x40000000);
    core_reg(cpu, offsetof(struct kvm_regs, regs.pstate) / sizeof(uint32_t), 0x3c5);
    int size = ioctl(kvm, KVM_GET_VCPU_MMAP_SIZE, 0);
    if (size < (int)sizeof(struct kvm_run)) { fprintf(stderr, "Invalid run size\n"); return 1; }
    struct kvm_run *run = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED, cpu, 0);
    if (run == MAP_FAILED) fail("vcpu mmap");
    struct sigaction sa = {.sa_handler = timeout_handler};
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGALRM, &sa, NULL)) fail("sigaction");
    alarm(5);
    int rc = ioctl(cpu, KVM_RUN, 0);
    alarm(0);
    if (rc < 0) {
        if (expired) fprintf(stderr, "Guest execution timed out\n");
        else perror("KVM_RUN");
        return 1;
    }
    uint64_t value = 0;
    memcpy(&value, run->mmio.data, sizeof(value));
    if (run->exit_reason != KVM_EXIT_MMIO || !run->mmio.is_write ||
        run->mmio.phys_addr != 0x100000 || run->mmio.len != 8 || value != 42) {
        fprintf(stderr, "Unexpected guest exit=%u; MMIO witness missing\n", run->exit_reason);
        return 1;
    }
    puts("PASS: ARM64 KVM guest executed and wrote MMIO witness 42 (one vCPU only)");
    munmap(run, (size_t)size); close(cpu); close(vm);
    munmap(mem, (size_t)page); close(kvm);
    return 0;
}
