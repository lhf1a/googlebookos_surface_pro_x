#include "qti-msm-vmbind-bridge.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    assert(qti_vm_app_uid(10252) && qti_vm_app_uid(110252));
    assert(qti_vm_app_uid(10000) && qti_vm_app_uid(19999));
    assert(!qti_vm_app_uid(0) && !qti_vm_app_uid(1000));
    assert(!qti_vm_app_uid(20000) && !qti_vm_app_uid(99001));
    puts("PASS all regular application UIDs, including secondary users");
    return 0;
}
