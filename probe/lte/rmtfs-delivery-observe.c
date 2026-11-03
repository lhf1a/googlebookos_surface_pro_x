/* SPDX-License-Identifier: MIT
 * Opt-in, bounded observer for the pinned glibc RMTFS pwrite path.
 * Never install globally: preload only the audited rmtfs invocation. The caller
 * still performs exactly one original pwrite. Readback is CPU-visible evidence,
 * not an atomic snapshot, a cache fix, or proof that the modem accepted NV.
 *
 * Enable: RMTFS_DELIVERY_OBSERVE=1 RMTFS_DELIVERY_FSG_SHA256=<64 hex digits>
 * Log: /logs/lte-rmtfs-delivery-v1.txt (new file only, 0600, no symlink).
 * No NV bytes are logged. No production path/identity override is provided.
 * Accept only newline-terminated records ending in record_end=1. Descriptor
 * identity checks detect observed reuse, not cross-thread close/reopen TOCTOU;
 * the audited program must not close this library's private descriptors.
 * Test builds inject POSIX operations and an expected fixture executable hash;
 * they retain the same size, sysfs, character-device and logging checks.
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdatomic.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef RMTFS_DELIVERY_TEST
#include <dlfcn.h>
#include <sys/syscall.h>
#endif

#define RDO_WINDOW UINT64_C(2097152)
#define RDO_SECTOR 512u
#define RDO_SECTORS 4096u
#define RDO_MAX_SEQUENCES 4u
#define RDO_MAX_CALLS 32768u
#define RDO_MAX_BYTES (RDO_WINDOW * RDO_MAX_SEQUENCES)
#define RDO_EXE_SIZE 69360u
#define RDO_LOG "/logs/lte-rmtfs-delivery-v1.txt"
#define RDO_NODE "/dev/qcom_rmtfs_mem1"
#define RDO_SYSFS "/sys/class/rmtfs/qcom_rmtfs_mem1/"
#define RDO_EXE_SHA "856681222851149a54eead466804cb4bc51e5aece26a0b0afb41ae231403a62b"
#define RDO_MAX_METADATA_BYTES (RDO_MAX_SEQUENCES * 26u)
#define RDO_AF_QRTR 42
struct rdo_qrtr_address { sa_family_t family; uint32_t node, port; };
_Static_assert(sizeof(struct rdo_qrtr_address)==12, "audited sockaddr_qrtr ABI");
_Static_assert(sizeof(off_t) == 8, "the audited pwrite ABI has 64-bit off_t");

struct rdo_io {
    int (*open)(const char *, int, mode_t);
    ssize_t (*read)(int, void *, size_t);
    ssize_t (*pread)(int, void *, size_t, off_t);
    ssize_t (*pwrite)(int, const void *, size_t, off_t);
    ssize_t (*write)(int, const void *, size_t);
    int (*close)(int);
    int (*fstat)(int, struct stat *);
    int (*clock_gettime)(clockid_t, struct timespec *);
    char *(*getenv)(const char *);
    ssize_t (*sendto)(int, const void *, size_t, int, const struct sockaddr *, socklen_t);
    int (*getsockopt)(int, int, int, void *, socklen_t *);
    int (*getsockname)(int, struct sockaddr *, socklen_t *);
    ssize_t (*recvfrom)(int, void *, size_t, int, struct sockaddr *, socklen_t *);
};

/* FIPS 180-4 SHA-256, fixed storage; no libcrypto, allocation or hidden I/O. */
struct rdo_sha256 {
    uint32_t h[8];
    uint64_t bytes;
    unsigned used;
    unsigned char block[64];
};
static uint32_t rdo_ror(uint32_t v, unsigned n)
{
    return (v >> n) | (v << (32u - n));
}
static void rdo_sha_block(struct rdo_sha256 *s, const unsigned char *p)
{
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
    };
    uint32_t w[64], a=s->h[0], b=s->h[1], c=s->h[2], d=s->h[3];
    uint32_t e=s->h[4], f=s->h[5], g=s->h[6], h=s->h[7];
    unsigned i;
    for (i=0; i<16; ++i)
        w[i]=((uint32_t)p[i*4]<<24)|((uint32_t)p[i*4+1]<<16)|((uint32_t)p[i*4+2]<<8)|p[i*4+3];
    for (i=16; i<64; ++i) {
        uint32_t x=w[i-15], y=w[i-2];
        w[i]=w[i-16]+(rdo_ror(x,7)^rdo_ror(x,18)^(x>>3))+w[i-7]+
             (rdo_ror(y,17)^rdo_ror(y,19)^(y>>10));
    }
    for (i=0; i<64; ++i) {
        uint32_t t1=h+(rdo_ror(e,6)^rdo_ror(e,11)^rdo_ror(e,25))+
                    ((e&f)^(~e&g))+k[i]+w[i];
        uint32_t t2=(rdo_ror(a,2)^rdo_ror(a,13)^rdo_ror(a,22))+((a&b)^(a&c)^(b&c));
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    s->h[0]+=a; s->h[1]+=b; s->h[2]+=c; s->h[3]+=d;
    s->h[4]+=e; s->h[5]+=f; s->h[6]+=g; s->h[7]+=h;
}
static void rdo_sha_init(struct rdo_sha256 *s)
{
    static const uint32_t iv[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
                               0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memcpy(s->h,iv,sizeof(iv)); s->bytes=0; s->used=0;
}
static void rdo_sha_update(struct rdo_sha256 *s, const void *data, size_t n)
{
    const unsigned char *p=data;
    s->bytes+=(uint64_t)n;
    while (n) {
        size_t take=64u-s->used;
        if (take>n) take=n;
        memcpy(s->block+s->used,p,take);
        s->used+=(unsigned)take; p+=take; n-=take;
        if (s->used==64) { rdo_sha_block(s,s->block); s->used=0; }
    }
}
static void rdo_sha_final(struct rdo_sha256 *s, unsigned char out[32])
{
    uint64_t bits=s->bytes*8u;
    unsigned i;
    s->block[s->used++]=0x80;
    if (s->used>56) {
        memset(s->block+s->used,0,64u-s->used);
        rdo_sha_block(s,s->block); s->used=0;
    }
    memset(s->block+s->used,0,56u-s->used);
    for (i=0; i<8; ++i) s->block[63-i]=(unsigned char)(bits>>(i*8));
    rdo_sha_block(s,s->block);
    for (i=0; i<32; ++i) out[i]=(unsigned char)(s->h[i/4]>>(24u-8u*(i%4)));
}
static void rdo_hex(const unsigned char in[32], char out[65])
{
    static const char digits[]="0123456789abcdef";
    unsigned i;
    for (i=0; i<32; ++i) { out[i*2]=digits[in[i]>>4]; out[i*2+1]=digits[in[i]&15]; }
    out[64]=0;
}
static int rdo_parse_hash(const char *text, unsigned char out[32])
{
    unsigned i;
    if (!text || strnlen(text,65)!=64) return 0;
    for (i=0; i<64; ++i) {
        unsigned char c=(unsigned char)text[i];
        unsigned v;
        if (c>='0' && c<='9') v=c-'0';
        else if (c>='a' && c<='f') v=c-'a'+10u;
        else if (c>='A' && c<='F') v=c-'A'+10u;
        else return 0;
        if (!(i&1)) out[i/2]=(unsigned char)(v<<4);
        else out[i/2]|=(unsigned char)v;
    }
    return 1;
}

static struct {
    struct rdo_io io;
    int logfd, readfd, memfd;
    dev_t log_dev;
    ino_t log_ino;
    dev_t read_dev;
    ino_t read_ino;
    dev_t mem_dev;
    ino_t mem_ino;
    dev_t device;
    unsigned calls, attempts;
    uint64_t bytes;
    unsigned metadata_bytes;
    unsigned char expected[32];
    struct {
        int active;
        unsigned sectors, reads;
        uint64_t start_ns, read_bytes, differences;
        int64_t first_difference;
        ssize_t read_return;
        int read_errno;
        struct rdo_sha256 input, output;
    } seq;
} rdo;
static atomic_int rdo_enabled;
static atomic_int rdo_contended;
static atomic_flag rdo_busy=ATOMIC_FLAG_INIT;
static atomic_uint_fast64_t rdo_events;
static _Thread_local struct {
    unsigned sequence;
    uint64_t event, completed_ns;
} rdo_pending;

static int rdo_now(uint64_t *value)
{
    struct timespec t;
    if (rdo.io.clock_gettime(CLOCK_MONOTONIC,&t)<0 || t.tv_sec<0 ||
        t.tv_nsec<0 || t.tv_nsec>=1000000000L) return 0;
    *value=(uint64_t)t.tv_sec*UINT64_C(1000000000)+(uint64_t)t.tv_nsec;
    return 1;
}
/* A bounded direct write. No stdio buffering, retry loop or device descriptor. */
static void rdo_marker(const char *reason, int error);
static void rdo_log(const char *line, size_t length)
{
    struct stat st;
    if (rdo.logfd>=0 && (rdo.io.fstat(rdo.logfd,&st)<0 || !S_ISREG(st.st_mode) ||
        st.st_dev!=rdo.log_dev || st.st_ino!=rdo.log_ino || st.st_nlink!=1 ||
        st.st_uid!=0 || (st.st_mode&0777)!=0600)) {
        /* Do not write to or close a descriptor which might have been reused. */
        atomic_store(&rdo_enabled,0); rdo.logfd=-1;
        rdo_marker("log_changed",0);
        return;
    }
    if (rdo.logfd>=0 && rdo.io.write(rdo.logfd,line,length)!=(ssize_t)length) {
        int error=errno;
        atomic_store(&rdo_enabled,0);
        if (rdo.io.fstat(rdo.logfd,&st)==0 && S_ISREG(st.st_mode) &&
            st.st_dev==rdo.log_dev && st.st_ino==rdo.log_ino) (void)rdo.io.close(rdo.logfd);
        rdo.logfd=-1;
        /* No further observation without a usable evidence log. */
        rdo_marker("log_short_or_error",error);
    }
}
static void rdo_marker(const char *reason, int error)
{
    char line[256];
    struct stat st;
    int n=snprintf(line,sizeof(line),"RMTFS_DELIVERY disabled reason=%s errno=%d coverage=0 inconclusive=1 record_end=1\n",reason,error);
    if (n<=0 || (size_t)n>=sizeof(line)) return;
    if (rdo.logfd>=0) rdo_log(line,(size_t)n);
    else if (rdo.io.fstat(STDERR_FILENO,&st)==0 && S_ISREG(st.st_mode))
        (void)rdo.io.write(STDERR_FILENO,line,(size_t)n);
    /* An unavailable log never authorizes writing a console/device on fd 2. */
}
static void rdo_invalidate_pending(const char *reason)
{
    char line[320];
    int n;
    if (!rdo_pending.sequence) return;
    n=snprintf(line,sizeof(line),
        "RMTFS_RESPONSE sequence=%u attempted=0 reason=%s association=next_send_same_thread "
        "request_transaction_verified=0 transport_accepted=0 modem_acceptance=unknown record_end=1\n",
        rdo_pending.sequence,reason);
    rdo_pending.sequence=0;
    if (n>0 && (size_t)n<sizeof(line)) rdo_log(line,(size_t)n);
}
static void rdo_finish_sequence(const char *reason, int complete, ssize_t wr, int error)
{
    unsigned char ih[32], oh[32];
    char input[65], output[65], line[1024];
    uint64_t now=0, elapsed=0;
    int clock_ok, coverage, fsg, n;
    if (!rdo.seq.active) return;
    clock_ok=rdo_now(&now) && now>=rdo.seq.start_ns;
    if (clock_ok) elapsed=now-rdo.seq.start_ns;
    rdo_sha_final(&rdo.seq.input,ih); rdo_hex(ih,input);
    rdo_sha_final(&rdo.seq.output,oh); rdo_hex(oh,output);
    coverage=complete && rdo.seq.sectors==RDO_SECTORS &&
             rdo.seq.reads==RDO_SECTORS && rdo.seq.read_bytes==RDO_WINDOW &&
             !atomic_load(&rdo_contended);
    fsg=coverage && !memcmp(ih,rdo.expected,32);
    n=snprintf(line,sizeof(line),
        "RMTFS_DELIVERY sequence=%u reason=%s coverage=%d fsg=%d sectors=%u reads=%u "
        "input_bytes=%" PRIu64 " read_bytes=%" PRIu64 " input_sha256=%s readback_sha256=%s "
        "differences=%" PRIu64 " first_difference=%" PRId64 " real_return=%zd real_errno=%d "
        "read_return=%zd read_errno=%d "
        "elapsed_ns=%" PRIu64 " clock_ok=%d calls=%u total_bytes=%" PRIu64
        " atomic_snapshot=0 modem_acceptance=unknown record_end=1\n",
        rdo.attempts,reason,coverage,fsg,rdo.seq.sectors,rdo.seq.reads,
        (uint64_t)rdo.seq.sectors*RDO_SECTOR,rdo.seq.read_bytes,input,output,
        rdo.seq.differences,rdo.seq.first_difference,wr,error,rdo.seq.read_return,
        rdo.seq.read_errno,elapsed,clock_ok,rdo.calls,rdo.bytes);
    rdo.seq.active=0;
    if (n>0 && (size_t)n<sizeof(line)) rdo_log(line,(size_t)n);
    if (fsg && atomic_load(&rdo_enabled)) {
        rdo_pending.sequence=rdo.attempts;
        rdo_pending.event=atomic_load(&rdo_events);
        rdo_pending.completed_ns=now;
    }
}
static void rdo_stop(const char *reason, int error, ssize_t wr)
{
    rdo_finish_sequence(reason,0,wr,error);
    rdo_invalidate_pending(reason);
    atomic_store(&rdo_enabled,0);
    rdo_marker(reason,error);
}
static int rdo_small(const char *path, char out[128])
{
    int fd=rdo.io.open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW,0), saved;
    ssize_t n;
    if (fd<0) return 0;
    n=rdo.io.read(fd,out,127); saved=errno;
    (void)rdo.io.close(fd); errno=saved;
    if (n<=0 || n>=127) return 0;
    out[n]=0;
    return 1;
}
static int rdo_number(const char *text, unsigned base, uint64_t *out)
{
    uint64_t value=0;
    unsigned count=0;
    if (base==16 && text[0]=='0' && (text[1]=='x' || text[1]=='X')) text+=2;
    while (*text && *text!='\n') {
        unsigned char c=(unsigned char)*text++;
        unsigned digit;
        if (c>='0' && c<='9') digit=c-'0';
        else if (base==16 && c>='a' && c<='f') digit=c-'a'+10u;
        else if (base==16 && c>='A' && c<='F') digit=c-'A'+10u;
        else return 0;
        if (digit>=base || value>(UINT64_MAX-digit)/base) return 0;
        value=value*base+digit; ++count;
    }
    if (!count || (*text=='\n' && text[1])) return 0;
    *out=value; return 1;
}
static int rdo_identity(void)
{
    char text[128], *colon;
    uint64_t a,b,value;
    if (!rdo_small(RDO_SYSFS "client_id",text) || !rdo_number(text,10,&value) || value!=1) return 0;
    if (!rdo_small(RDO_SYSFS "phys_addr",text) || !rdo_number(text,16,&value) || value!=UINT64_C(0x85500000)) return 0;
    if (!rdo_small(RDO_SYSFS "size",text) || !rdo_number(text,16,&value) || value!=RDO_WINDOW) return 0;
    if (!rdo_small(RDO_SYSFS "dev",text) || !(colon=strchr(text,':'))) return 0;
    *colon=0;
    if (!rdo_number(text,10,&a) || !rdo_number(colon+1,10,&b) || a>UINT32_MAX || b>UINT32_MAX) return 0;
    rdo.device=makedev((unsigned)a,(unsigned)b);
    return major(rdo.device)==a && minor(rdo.device)==b;
}
static int rdo_executable(const char *expected_hex)
{
    unsigned char expected[32], digest[32], buffer[1024];
    struct rdo_sha256 sha;
    struct stat st;
    size_t total=0;
    int fd, ok=0;
    if (!rdo_parse_hash(expected_hex,expected)) return 0;
    /* /proc/self/exe is intentionally followed; it is the kernel's own link. */
    fd=rdo.io.open("/proc/self/exe",O_RDONLY|O_CLOEXEC,0);
    if (fd<0) return 0;
    if (rdo.io.fstat(fd,&st)<0 || !S_ISREG(st.st_mode) || st.st_size!=RDO_EXE_SIZE) goto done;
    rdo_sha_init(&sha);
    while (total<RDO_EXE_SIZE) {
        size_t want=RDO_EXE_SIZE-total;
        ssize_t n;
        if (want>sizeof(buffer)) want=sizeof(buffer);
        n=rdo.io.read(fd,buffer,want);
        /* Bound read calls too: short/EINTR reads make admission inconclusive. */
        if (n!=(ssize_t)want) goto done;
        rdo_sha_update(&sha,buffer,(size_t)n); total+=(size_t)n;
    }
    if (rdo.io.read(fd,buffer,1)!=0) goto done;
    rdo_sha_final(&sha,digest); ok=!memcmp(digest,expected,32);
done:
    (void)rdo.io.close(fd);
    memset(buffer,0,sizeof(buffer));
    return ok;
}
static void rdo_initialize(const struct rdo_io *io, const char *exe_hash)
{
    struct stat st;
    char *opt;
    int saved=errno;
    static const char admitted[]=
        "RMTFS_DELIVERY enabled=1 schema=2 sector_bytes=512 window_bytes=2097152 "
        "max_sequences=4 max_calls=32768 max_bytes=8388608 "
        "response_max_metadata_bytes=104 association=next_send_same_thread request_transaction_verified=0 "
        "phase=after_real_pwrite_before_return atomic_snapshot=0 modem_acceptance=unknown record_end=1\n";
    memset(&rdo,0,sizeof(rdo)); rdo.io=*io; rdo.logfd=-1; rdo.readfd=-1; rdo.memfd=-1;
    memset(&rdo_pending,0,sizeof(rdo_pending)); atomic_store(&rdo_events,0);
    atomic_store(&rdo_enabled,0); atomic_store(&rdo_contended,0); atomic_flag_clear(&rdo_busy);
    opt=rdo.io.getenv("RMTFS_DELIVERY_OBSERVE");
    if (!opt || strcmp(opt,"1")) goto done;
    rdo.logfd=rdo.io.open(RDO_LOG,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if (rdo.logfd<0) { rdo_marker("log_open",errno); goto done; }
    if (rdo.io.fstat(rdo.logfd,&st)<0 || !S_ISREG(st.st_mode) ||
        (st.st_mode&0777)!=0600 || st.st_nlink!=1 || st.st_uid!=0) {
        (void)rdo.io.close(rdo.logfd); rdo.logfd=-1; rdo_marker("log_identity",0); goto done;
    }
    rdo.log_dev=st.st_dev; rdo.log_ino=st.st_ino;
    if (!rdo_parse_hash(rdo.io.getenv("RMTFS_DELIVERY_FSG_SHA256"),rdo.expected)) {
        rdo_marker("fsg_hash",0); goto done;
    }
    if (!rdo.io.pwrite || !rdo.io.pread || !rdo.io.sendto || !rdo.io.recvfrom ||
        !rdo.io.getsockopt || !rdo.io.getsockname) { rdo_marker("symbol",0); goto done; }
    if (!rdo_executable(exe_hash)) { rdo_marker("exe_identity",0); goto done; }
    if (!rdo_identity()) { rdo_marker("sysfs_identity",0); goto done; }
    rdo.readfd=rdo.io.open(RDO_NODE,O_RDONLY|O_NOFOLLOW|O_CLOEXEC,0);
    if (rdo.readfd<0) { rdo_marker("readfd_open",errno); goto done; }
    if (rdo.io.fstat(rdo.readfd,&st)<0 || !S_ISCHR(st.st_mode) || st.st_rdev!=rdo.device) {
        (void)rdo.io.close(rdo.readfd); rdo.readfd=-1; rdo_marker("readfd_identity",0); goto done;
    }
    rdo.read_dev=st.st_dev; rdo.read_ino=st.st_ino;
    /* A private read-only descriptor allows bounded copies even after EFAULT.
     * Only the immediate sendto's two pointers (12 + 14 bytes) are admitted. */
    rdo.memfd=rdo.io.open("/proc/self/mem",O_RDONLY|O_NOFOLLOW|O_CLOEXEC,0);
    if (rdo.memfd<0) { rdo_marker("self_mem_open",errno); goto done; }
    if (rdo.io.fstat(rdo.memfd,&st)<0 || !S_ISREG(st.st_mode)) {
        (void)rdo.io.close(rdo.memfd); rdo.memfd=-1; rdo_marker("self_mem_identity",0); goto done;
    }
    rdo.mem_dev=st.st_dev; rdo.mem_ino=st.st_ino;
    atomic_store(&rdo_enabled,1);
    rdo_log(admitted,sizeof(admitted)-1);
done:
    errno=saved;
}

static void rdo_observe(int fd, const void *buf, size_t count, off_t offset, ssize_t result, int error)
{
    struct stat st;
    unsigned char input[RDO_SECTOR], output[RDO_SECTOR];
    uint64_t now;
    ssize_t n;
    unsigned i;
    if (atomic_load(&rdo_contended)) { rdo_stop("concurrent",error,result); return; }
    if (rdo.calls>=RDO_MAX_CALLS) { rdo_stop("call_limit",error,result); return; }
    ++rdo.calls;
    if (rdo.io.fstat(fd,&st)<0 || !S_ISCHR(st.st_mode) || st.st_rdev!=rdo.device) return;
    /* Recheck the independently held read descriptor; no reopening a reused fd. */
    if (rdo.io.fstat(rdo.readfd,&st)<0 || !S_ISCHR(st.st_mode) || st.st_rdev!=rdo.device ||
        st.st_dev!=rdo.read_dev || st.st_ino!=rdo.read_ino) {
        rdo.readfd=-1; /* Do not close an fd which now belongs to somebody else. */
        rdo_stop("readfd_changed",error,result); return;
    }
    if (count!=RDO_SECTOR || offset<0 || (uint64_t)offset>=RDO_WINDOW || (offset%RDO_SECTOR)) {
        rdo_finish_sequence("shape",0,result,error); return;
    }
    if (rdo.seq.active && (uint64_t)offset!=(uint64_t)rdo.seq.sectors*RDO_SECTOR)
        rdo_finish_sequence("order",0,result,error);
    if (!atomic_load(&rdo_enabled)) return;
    if (!rdo.seq.active) {
        if (offset!=0) return;
        if (rdo.attempts>=RDO_MAX_SEQUENCES) { rdo_stop("sequence_limit",error,result); return; }
        if (!rdo_now(&now)) { rdo_stop("clock",0,result); return; }
        memset(&rdo.seq,0,sizeof(rdo.seq)); rdo.seq.active=1;
        rdo.seq.start_ns=now; rdo.seq.first_difference=-1;
        rdo_sha_init(&rdo.seq.input); rdo_sha_init(&rdo.seq.output); ++rdo.attempts;
    }
    if (result!=RDO_SECTOR) { rdo_finish_sequence("real_short_or_error",0,result,error); return; }
    if (rdo.bytes>RDO_MAX_BYTES-RDO_SECTOR) { rdo_stop("byte_limit",error,result); return; }
    /* Read buf only after a successful original call; EFAULT is never dereferenced. */
    memcpy(input,buf,sizeof(input));
    rdo_sha_update(&rdo.seq.input,input,sizeof(input)); ++rdo.seq.sectors;
    rdo.bytes+=RDO_SECTOR;
    n=rdo.io.pread(rdo.readfd,output,sizeof(output),offset);
    rdo.seq.read_return=n; rdo.seq.read_errno=errno;
    if (n>0 && n<=RDO_SECTOR) {
        rdo_sha_update(&rdo.seq.output,output,(size_t)n);
        rdo.seq.read_bytes+=(uint64_t)n;
        for (i=0; i<(unsigned)n; ++i) if (input[i]!=output[i]) {
            ++rdo.seq.differences;
            if (rdo.seq.first_difference<0) rdo.seq.first_difference=(int64_t)offset+i;
        }
    }
    memset(input,0,sizeof(input)); memset(output,0,sizeof(output));
    if (n!=RDO_SECTOR) { rdo_finish_sequence("read_short_or_error",0,result,error); return; }
    ++rdo.seq.reads;
    if (atomic_load(&rdo_contended)) { rdo_stop("concurrent",error,result); return; }
    if (rdo.seq.sectors==RDO_SECTORS) rdo_finish_sequence("complete",1,result,error);
}

static int rdo_copy_parameter(void *dst, const void *src, size_t size, int *copy_errno)
{
    struct stat st;
    uintptr_t address=(uintptr_t)src;
    ssize_t n;
    if ((size!=12 && size!=14) || !address || address>(uintptr_t)INT64_MAX-size ||
        rdo.metadata_bytes>RDO_MAX_METADATA_BYTES-size) {
        *copy_errno=EINVAL; return 0;
    }
    if (rdo.memfd<0 || rdo.io.fstat(rdo.memfd,&st)<0 || !S_ISREG(st.st_mode) ||
        st.st_dev!=rdo.mem_dev || st.st_ino!=rdo.mem_ino) {
        rdo.memfd=-1; *copy_errno=EBADF; atomic_store(&rdo_enabled,0); return 0;
    }
    rdo.metadata_bytes+=(unsigned)size;
    n=rdo.io.pread(rdo.memfd,dst,size,(off_t)address);
    *copy_errno=errno;
    return n==(ssize_t)size;
}
static unsigned rdo_le16(const unsigned char *p)
{
    return (unsigned)p[0]|((unsigned)p[1]<<8);
}
static int rdo_qrtr_socket(int fd)
{
    struct stat before, after;
    struct rdo_qrtr_address local;
    socklen_t length;
    int value;
    if (rdo.io.fstat(fd,&before)<0 || !S_ISSOCK(before.st_mode)) return 0;
    length=sizeof(value); value=0;
    if (rdo.io.getsockopt(fd,SOL_SOCKET,SO_DOMAIN,&value,&length)<0 ||
        length!=sizeof(value) || value!=RDO_AF_QRTR) return 0;
    length=sizeof(value); value=0;
    if (rdo.io.getsockopt(fd,SOL_SOCKET,SO_TYPE,&value,&length)<0 ||
        length!=sizeof(value) || value!=SOCK_DGRAM) return 0;
    memset(&local,0,sizeof(local)); length=sizeof(local);
    if (rdo.io.getsockname(fd,(struct sockaddr *)&local,&length)<0 ||
        length!=sizeof(local) || local.family!=RDO_AF_QRTR) return 0;
    if (rdo.io.fstat(fd,&after)<0 || !S_ISSOCK(after.st_mode) ||
        after.st_dev!=before.st_dev || after.st_ino!=before.st_ino) return 0;
    return 1;
}
static void rdo_response(int fd, const void *buf, size_t length, int flags,
                         const struct sockaddr *addr, socklen_t addrlen,
                         ssize_t result, int error, unsigned sequence,
                         uint64_t expected_event, uint64_t event, uint64_t completed_ns)
{
    struct rdo_qrtr_address destination;
    unsigned char packet[14];
    const char *reason="intervening_io";
    char line[1024];
    int socket_ok=0, destination_ok=0, packet_ok=0, copy_ok=0, copy_errno=0;
    int type=-1, transaction=-1, message=-1, payload=-1, qmi_result=-1, qmi_error=-1;
    int accepted=0, n, clock_ok;
    int64_t node=-1, port=-1;
    uint64_t now=0, elapsed=0;
    if (event!=expected_event+1 || atomic_load(&rdo_contended)) goto record;
    reason="socket_identity";
    if (!(socket_ok=rdo_qrtr_socket(fd))) goto record;
    reason="address_length";
    if (addrlen!=sizeof(destination)) goto record;
    reason="copy_address";
    if (!rdo_copy_parameter(&destination,addr,sizeof(destination),&copy_errno)) goto record;
    reason="destination";
    if (destination.family!=RDO_AF_QRTR || destination.node==UINT32_MAX ||
        !destination.port || destination.port>=UINT32_C(0xfffffffe)) goto record;
    destination_ok=1; node=destination.node; port=destination.port;
    reason="packet_length_or_flags";
    if (length!=sizeof(packet) || flags!=0) goto record;
    reason="copy_packet";
    if (!rdo_copy_parameter(packet,buf,sizeof(packet),&copy_errno)) goto record;
    copy_ok=1;
    reason="packet";
    if (packet[0]!=2 || rdo_le16(packet+3)!=3 || rdo_le16(packet+5)!=7 ||
        packet[7]!=2 || rdo_le16(packet+8)!=4 || rdo_le16(packet+10)>1) goto record;
    packet_ok=1; type=packet[0]; transaction=(int)rdo_le16(packet+1);
    message=(int)rdo_le16(packet+3); payload=(int)rdo_le16(packet+5);
    qmi_result=(int)rdo_le16(packet+10); qmi_error=(int)rdo_le16(packet+12);
    reason=result<0 ? "send_error" : result!=(ssize_t)length ? "send_short" :
           (qmi_result || qmi_error) ? "qmi_error" : "accepted";
    accepted=result==(ssize_t)length;
record:
    /* Another observed send/pwrite invalidates this weak temporal association. */
    if (atomic_load(&rdo_events)!=event || atomic_load(&rdo_contended)) {
        reason="concurrent"; accepted=0;
    }
    clock_ok=rdo_now(&now) && now>=completed_ns;
    if (clock_ok) elapsed=now-completed_ns;
    n=snprintf(line,sizeof(line),
        "RMTFS_RESPONSE sequence=%u attempted=1 reason=%s association=next_send_same_thread "
        "request_transaction_verified=0 socket_ok=%d destination_ok=%d packet_ok=%d copy_ok=%d copy_errno=%d "
        "node=%" PRId64 " port=%" PRId64 " type=%d transaction=%d message_id=%d payload_length=%d "
        "qmi_result=%d qmi_error=%d send_length=%zu send_flags=%d address_length=%u "
        "send_return=%zd send_errno=%d transport_accepted=%d elapsed_ns=%" PRIu64
        " clock_ok=%d metadata_bytes=%u modem_acceptance=unknown record_end=1\n",
        sequence,reason,socket_ok,destination_ok,packet_ok,copy_ok,copy_errno,node,port,
        type,transaction,message,payload,qmi_result,qmi_error,length,flags,(unsigned)addrlen,
        result,error,accepted,elapsed,clock_ok,rdo.metadata_bytes);
    if (n>0 && (size_t)n<sizeof(line)) rdo_log(line,(size_t)n);
}

static ssize_t rdo_forward(int fd, const void *buf, size_t count, off_t offset)
{
    int entry=errno, result_errno, watching=atomic_load(&rdo_enabled), owner=0;
    ssize_t result;
    if (watching) {
        (void)atomic_fetch_add(&rdo_events,1);
        owner=!atomic_flag_test_and_set(&rdo_busy);
        if (!owner) atomic_store(&rdo_contended,1);
    }
    errno=entry;
    result=rdo.io.pwrite(fd,buf,count,offset); /* Exactly once; no short-write retry. */
    result_errno=errno;
    if (owner) {
        rdo_invalidate_pending("intervening_pwrite");
        if (atomic_load(&rdo_enabled)) rdo_observe(fd,buf,count,offset,result,result_errno);
        atomic_flag_clear(&rdo_busy);
    }
    errno=result_errno;
    return result;
}
static ssize_t rdo_send_forward(int fd, const void *buf, size_t length, int flags,
                                const struct sockaddr *addr, socklen_t addrlen)
{
    int entry=errno, result_errno, watching=atomic_load(&rdo_enabled), owner=0;
    unsigned pending=rdo_pending.sequence;
    uint64_t expected=rdo_pending.event, completed=rdo_pending.completed_ns, event=0;
    ssize_t result;
    rdo_pending.sequence=0; /* The very first send consumes it, including failures. */
    if (watching) {
        event=atomic_fetch_add(&rdo_events,1)+1;
        owner=!atomic_flag_test_and_set(&rdo_busy);
        if (!owner) atomic_store(&rdo_contended,1);
    }
    errno=entry;
    result=rdo.io.sendto(fd,buf,length,flags,addr,addrlen);
    result_errno=errno;
    if (owner) {
        if (rdo.calls>=RDO_MAX_CALLS) rdo_stop("call_limit",result_errno,result);
        else {
            ++rdo.calls;
            if (pending && atomic_load(&rdo_enabled))
                rdo_response(fd,buf,length,flags,addr,addrlen,result,result_errno,
                             pending,expected,event,completed);
            if (atomic_load(&rdo_contended)) rdo_stop("concurrent",result_errno,result);
        }
        atomic_flag_clear(&rdo_busy);
    }
    errno=result_errno;
    return result;
}
static ssize_t rdo_recv_forward(int fd, void *buf, size_t length, int flags,
                                struct sockaddr *addr, socklen_t *addrlen)
{
    int entry=errno;
    if (atomic_load(&rdo_enabled)) {
        (void)atomic_fetch_add(&rdo_events,1);
        if (atomic_flag_test_and_set(&rdo_busy)) {
            atomic_store(&rdo_contended,1);
            rdo_pending.sequence=0;
        } else {
            rdo_invalidate_pending("receive_boundary");
            if (rdo.calls>=RDO_MAX_CALLS) rdo_stop("call_limit",entry,0);
            else ++rdo.calls;
            atomic_flag_clear(&rdo_busy);
        }
    }
    /* No inspection of buf/addr/addrlen and no observer lock held while the
     * original call may block. This is an event boundary, not a request parser. */
    errno=entry;
    return rdo.io.recvfrom(fd,buf,length,flags,addr,addrlen);
}
static void rdo_finish(void)
{
    int saved=errno;
    struct stat st;
    if (!atomic_flag_test_and_set(&rdo_busy)) {
        rdo_finish_sequence("process_exit",0,0,0);
        rdo_invalidate_pending("process_exit");
        atomic_store(&rdo_enabled,0);
        if (rdo.logfd>=0 && rdo.io.fstat(rdo.logfd,&st)==0 && S_ISREG(st.st_mode) &&
            st.st_dev==rdo.log_dev && st.st_ino==rdo.log_ino) (void)rdo.io.close(rdo.logfd);
        if (rdo.readfd>=0 && rdo.io.fstat(rdo.readfd,&st)==0 && S_ISCHR(st.st_mode) &&
            st.st_dev==rdo.read_dev && st.st_ino==rdo.read_ino && st.st_rdev==rdo.device)
            (void)rdo.io.close(rdo.readfd);
        if (rdo.memfd>=0 && rdo.io.fstat(rdo.memfd,&st)==0 && S_ISREG(st.st_mode) &&
            st.st_dev==rdo.mem_dev && st.st_ino==rdo.mem_ino) (void)rdo.io.close(rdo.memfd);
        rdo.logfd=-1; rdo.readfd=-1; rdo.memfd=-1;
        atomic_flag_clear(&rdo_busy);
    }
    errno=saved;
}

#ifdef RMTFS_DELIVERY_TEST
/* Compile-time injection only. Never present in the production shared library. */
void rdo_test_init(const struct rdo_io *io, const char *fixture_exe_sha256)
{
    rdo_initialize(io,fixture_exe_sha256);
}
ssize_t rdo_test_pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    return rdo_forward(fd,buf,count,offset);
}
ssize_t rdo_test_sendto(int fd, const void *buf, size_t length, int flags,
                       const struct sockaddr *addr, socklen_t addrlen)
{
    return rdo_send_forward(fd,buf,length,flags,addr,addrlen);
}
ssize_t rdo_test_recvfrom(int fd, void *buf, size_t length, int flags,
                         struct sockaddr *addr, socklen_t *addrlen)
{
    return rdo_recv_forward(fd,buf,length,flags,addr,addrlen);
}
void rdo_test_finish(void) { rdo_finish(); }
#else
static int rdo_open(const char *path, int flags, mode_t mode) { return open(path,flags,mode); }
static ssize_t (*rdo_original_pwrite)(int,const void *,size_t,off_t);
static ssize_t (*rdo_original_pread)(int,void *,size_t,off_t);
static ssize_t (*rdo_original_sendto)(int,const void *,size_t,int,const struct sockaddr *,socklen_t);
static ssize_t (*rdo_original_recvfrom)(int,void *,size_t,int,struct sockaddr *,socklen_t *);
static atomic_int rdo_ready;
__attribute__((constructor)) static void rdo_load(void)
{
    int saved=errno;
    void *p;
    struct rdo_io io={rdo_open,read,NULL,NULL,write,close,fstat,clock_gettime,getenv,
                      NULL,getsockopt,getsockname,NULL};
    _Static_assert(sizeof(p)==sizeof(rdo_original_pwrite),"POSIX dlsym function-pointer ABI");
    /* Once during loader initialization; no lazy resolution or pthread locks. */
    p=dlsym(RTLD_NEXT,"pwrite"); memcpy(&rdo_original_pwrite,&p,sizeof(p));
    p=dlsym(RTLD_NEXT,"pread"); memcpy(&rdo_original_pread,&p,sizeof(p));
    p=dlsym(RTLD_NEXT,"sendto"); memcpy(&rdo_original_sendto,&p,sizeof(p));
    p=dlsym(RTLD_NEXT,"recvfrom"); memcpy(&rdo_original_recvfrom,&p,sizeof(p));
    io.pwrite=rdo_original_pwrite; io.pread=rdo_original_pread; io.sendto=rdo_original_sendto;
    io.recvfrom=rdo_original_recvfrom;
    rdo_initialize(&io,RDO_EXE_SHA);
    atomic_store(&rdo_ready,1);
    errno=saved;
}
ssize_t pwrite(int fd, const void *buf, size_t count, off_t offset)
{
    /* Bootstrap/resolution failure only: pass through once, never observe.
     * The normal admitted path always calls the resolved original libc symbol.
     * This emergency syscall does not reproduce libc cancellation bookkeeping. */
    if (!rdo_original_pwrite) return (ssize_t)syscall(SYS_pwrite64,fd,buf,count,offset);
    if (!atomic_load(&rdo_ready)) return rdo_original_pwrite(fd,buf,count,offset);
    return rdo_forward(fd,buf,count,offset);
}
ssize_t sendto(int fd, const void *buf, size_t length, int flags,
               const struct sockaddr *addr, socklen_t addrlen)
{
    if (!rdo_original_sendto) return (ssize_t)syscall(SYS_sendto,fd,buf,length,flags,addr,addrlen);
    if (!atomic_load(&rdo_ready)) return rdo_original_sendto(fd,buf,length,flags,addr,addrlen);
    return rdo_send_forward(fd,buf,length,flags,addr,addrlen);
}
ssize_t recvfrom(int fd, void *buf, size_t length, int flags,
                 struct sockaddr *addr, socklen_t *addrlen)
{
    if (!rdo_original_recvfrom) return (ssize_t)syscall(SYS_recvfrom,fd,buf,length,flags,addr,addrlen);
    if (!atomic_load(&rdo_ready)) return rdo_original_recvfrom(fd,buf,length,flags,addr,addrlen);
    return rdo_recv_forward(fd,buf,length,flags,addr,addrlen);
}
__attribute__((destructor)) static void rdo_unload(void) { rdo_finish(); }
#endif
