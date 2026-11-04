"""Validate DMS packets and rollback decisions with a mocked modem transport."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'probe/test2/sq2-lte-online-test.c'
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    harness = tmp / 'online.c'
    # Replace only syscalls; exercise production main including timeout rollback.
    text = SOURCE.read_text()
    start = text.index('static int exchange(')
    end = text.index('\nint main(', start)
    text = text[:start] + '''
static int exchanges, current=2, scenario;
static int exchange(const struct sockaddr_qrtr *dst, int set_mode, int *mode) {
    (void)dst;
    exchanges++;
    if(set_mode==0) {
        if(scenario==1) { current=0; return 1; } /* applied, response lost */
        if(scenario==2) return 1; /* rejected */
        current=0;
    } else if(set_mode==2) { current=2; }
    *mode=current;
    return 0;
}
''' + text[end:]
    text = text.replace('int main(', 'int online_main(').replace('sleep(5);', '(void)0;')
    text += '''
#include <assert.h>
int main(void) {
    uint8_t req[16];
    assert(request(req,1,-1)==7 && req[3]==0x2d && req[5]==0);
    assert(request(req,2,0)==11 && req[3]==0x2e && req[5]==4 && req[10]==0);
    assert(request(req,3,2)==11 && req[10]==2);
    uint8_t reply[]={2,1,0,0x2d,0,11,0,2,4,0,0,0,0,0,1,1,0,2};
    int mode=-1;
    assert(response(reply,sizeof(reply),1,0x2d,&mode)==0 && mode==2);
    reply[16]=1; assert(response(reply,sizeof(reply),1,0x2d,&mode)!=0);
    reply[16]=0; reply[10]=1;
    assert(response(reply,sizeof(reply),1,0x2d,&mode)!=0);
    reply[10]=0;
    assert(response(reply,sizeof(reply)-1,1,0x2d,&mode)!=0);
    uint32_t value;
    assert(endpoint("0",&value)==0 && value==0);
    assert(endpoint("-1",&value)!=0 && endpoint("4294967296",&value)!=0);
    assert(now_ms()>=0);
    char *args[]={"online","0","62","online-for-30s",NULL};
    for(scenario=0;scenario<3;scenario++) {
        current=2; exchanges=0;
        assert(online_main(4,args)==(scenario!=0));
        assert(current==2 && exchanges>=4);
    }
    current=3; exchanges=0;
    assert(online_main(4,args)!=0 && current==3 && exchanges==1);
    current=2; interrupted=1; exchanges=0;
    assert(online_main(4,args)!=0 && current==2 && exchanges==1);
    assert(online_main(3,args)==2);
    return 0;
}
'''
    harness.write_text(text)
    binary = tmp / 'online'
    subprocess.run(['cc', '-O2', '-Wall', '-Wextra', '-Werror',
                    str(harness), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=5)
print('PASS: online/restore wire format, successful observation, lost reply, rejection, wrong initial mode, interruption')
