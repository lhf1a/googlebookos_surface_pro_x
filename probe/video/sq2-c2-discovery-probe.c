// Read-only service-manager discovery; never starts or waits for a service.
#include <android/binder_ibinder.h>
#include <stdbool.h>
// Platform NDK APIs, absent from the public NDK header package. Signatures
// checked against frameworks/native libs/binder/ndk/include_platform/android/binder_manager.h.
extern AIBinder *AServiceManager_checkService(const char *instance);
extern bool AServiceManager_isDeclared(const char *instance);
extern void AServiceManager_forEachDeclaredInstance(const char *interface,
 void *context, void (*callback)(const char *, void *));
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
struct found { int count; int experimental; };
static void instance(const char *name, void *opaque) {
 struct found *f = opaque;
 ++f->count;
 if (!strcmp(name, "default1")) f->experimental = 1;
 printf("DECLARED_INSTANCE=%s\n", name);
}
int main(void) {
 const char *descriptor = "android.hardware.media.c2.IComponentStore";
 const char *names[] = {
  "android.hardware.media.c2.IComponentStore/default",
  "android.hardware.media.c2.IComponentStore/software",
  "android.hardware.media.c2.IComponentStore/default1"
 };
 struct found f = {0};
 AServiceManager_forEachDeclaredInstance(descriptor, &f, instance);
 for (unsigned i = 0; i < 3; ++i) {
  AIBinder *b = AServiceManager_checkService(names[i]);
  printf("SERVICE=%s declared=%d registered=%d\n", names[i],
         AServiceManager_isDeclared(names[i]), b != NULL);
  if (b) AIBinder_decStrong(b);
 }
 printf("DISCOVERY declared_count=%d experimental_declared=%d\n", f.count, f.experimental);
 FILE *maps = fopen("/proc/self/maps", "r");
 char line[1024];
 if (maps) {
  while (fgets(line, sizeof(line), maps))
   if (strstr(line, "/libbinder.so") || strstr(line, "/libbinder_ndk.so"))
    printf("BINDER_MAP %s", line);
  fclose(maps);
 }
 DIR *fds = opendir("/proc/self/fd");
 struct dirent *entry;
 if (fds) {
  while ((entry = readdir(fds))) {
   char path[512], dest[512];
   snprintf(path, sizeof(path), "/proc/self/fd/%s", entry->d_name);
   ssize_t n = readlink(path, dest, sizeof(dest)-1);
   if (n < 0) continue;
   dest[n] = 0;
   if (strstr(dest, "binder")) printf("BINDER_FD %s=%s\n", entry->d_name, dest);
  }
  closedir(fds);
 }
 return 0;
}
