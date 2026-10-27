#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define ASHMEM_GET_NAME _IOR(0x77, 2, char[256])
#define ASHMEM_GET_SIZE _IO(0x77, 4)
#define ASHMEM_SET_PROT_MASK _IOW(0x77, 5, unsigned long)
#define ASHMEM_GET_PROT_MASK _IO(0x77, 6)
#define ASHMEM_GET_FILE_ID _IOR(0x77, 11, unsigned long)
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL line %d: %s (errno=%d)\n", __LINE__, #x, errno); return 1; } } while (0)

int main(void)
{
    int fd = memfd_create("sq2-compat-test", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    CHECK(fd >= 0);
    CHECK(ftruncate(fd, 4096) == 0);
    CHECK(ioctl(fd, ASHMEM_GET_SIZE, 0) == 4096);
    char name[256] = {0};
    CHECK(ioctl(fd, ASHMEM_GET_NAME, name) == 0);
    CHECK(strcmp(name, "sq2-compat-test") == 0);
    unsigned long id = 0, id2 = 0;
    CHECK(ioctl(fd, ASHMEM_GET_FILE_ID, &id) == 0 && id != 0);
    int copy = dup(fd);
    CHECK(copy >= 0);
    CHECK(ioctl(copy, ASHMEM_GET_FILE_ID, &id2) == 0 && id2 == id);
    char *data = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    CHECK(data != MAP_FAILED);
    data[0] = 42;
    CHECK(ioctl(fd, ASHMEM_SET_PROT_MASK, PROT_READ) == 0);
    CHECK(!(ioctl(fd, ASHMEM_GET_PROT_MASK, 0) & PROT_WRITE));
    CHECK(fcntl(fd, F_GET_SEALS) & F_SEAL_FUTURE_WRITE);
    CHECK(mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) == MAP_FAILED);
    data[0] = 43; /* An existing writable mapping must remain usable. */
    CHECK(data[0] == 43);
    CHECK(ioctl(fd, ASHMEM_SET_PROT_MASK, PROT_READ | PROT_WRITE) == -1);
    CHECK(ioctl(fd, _IO(0x77, 99), 0) == -1 && errno == ENOTTY);
    int plain = open("/tmp/sq2-plain-tmpfs", O_RDWR | O_CREAT | O_EXCL, 0600);
    CHECK(plain >= 0);
    CHECK(ioctl(plain, ASHMEM_GET_SIZE, 0) == -1 && errno == ENOTTY);
    close(plain);
    unlink("/tmp/sq2-plain-tmpfs");
    munmap(data, 4096);
    close(copy);
    close(fd);
    puts("PASS: memfd ashmem size/name/identity/seals; ordinary tmpfs rejects ioctls");
    return 0;
}
