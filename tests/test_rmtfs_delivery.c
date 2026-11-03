/* Offline synthetic filesystem/call fixture. No device/NV data is used. */
#define _GNU_SOURCE
#define RMTFS_DELIVERY_TEST 1
#include "../probe/lte/rmtfs-delivery-observe.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysmacros.h>
#include <pthread.h>

#define SIZE (2u * 1024u * 1024u)
#define EXE_SIZE 69360u
#define EXE_SHA "7265b767d9c5de77f74369d267514a1963456d924177f9ba8f7562c1f73fcb43"
#define INPUT_SHA "51da329c207e4946b3c6fc2fbfa070966f4800b0ee531e556d2d7e9cddd2d9b3"
enum { F_EXE=20,F_PHYS,F_SIZE,F_CLIENT,F_DEV,F_MEM,F_LOG,F_SELF_MEM };
static unsigned char input[SIZE], memory[SIZE];
static char logs[65536];
static size_t positions[64], log_len;
static unsigned calls, reads, opens, log_writes;
static int enabled, bad_exe, bad_dev, bad_size, wrong_type, log_open_error;
static int fail_read, short_read, bad_read, fail_write, short_write, fail_log;
static int read_error_code, short_read_value, preserve_real_errno;
static int log_reused_char, log_reused_inode, read_reused_inode, stderr_char;
static int recursive, recurse_once, threaded, first_waiting, release_first;
static pthread_mutex_t gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static const char *fsg_hash;
static unsigned char packet[14];
static struct rdo_qrtr_address destination;
static _Atomic unsigned send_calls;
static unsigned metadata_reads;
static int send_error, send_short, socket_domain, socket_type, socket_bad_name, socket_bad_fd;
static int mem_reused_inode, metadata_error, metadata_short, send_recursive, send_recursed, send_threaded;
static const void *expected_send_buf;
static const struct sockaddr *expected_send_addr;
static size_t expected_send_length;
static socklen_t expected_send_addrlen;
static int expected_send_fd, expected_send_flags;
static unsigned recv_calls;
static int recv_error, recv_probe_lock;
static ssize_t recv_value;
static int expected_recv_fd, expected_recv_flags;
static void *expected_recv_buf;
static size_t expected_recv_length;
static struct sockaddr *expected_recv_addr;
static socklen_t *expected_recv_addrlen;
static long ticks;

static int mock_open(const char *path,int flags,mode_t mode)
{
    int fd=-1; opens++;
    if (!strcmp(path,"/proc/self/exe")) fd=F_EXE;
    else if (!strcmp(path,"/proc/self/mem")){ assert((flags&O_ACCMODE)==O_RDONLY);fd=F_SELF_MEM; }
    else if (!strcmp(path,"/sys/class/rmtfs/qcom_rmtfs_mem1/phys_addr")) fd=F_PHYS;
    else if (!strcmp(path,"/sys/class/rmtfs/qcom_rmtfs_mem1/size")) fd=F_SIZE;
    else if (!strcmp(path,"/sys/class/rmtfs/qcom_rmtfs_mem1/client_id")) fd=F_CLIENT;
    else if (!strcmp(path,"/sys/class/rmtfs/qcom_rmtfs_mem1/dev")) fd=F_DEV;
    else if (!strcmp(path,"/dev/qcom_rmtfs_mem1")) {
        assert((flags & O_ACCMODE)==O_RDONLY); fd=F_MEM;
    } else if (!strcmp(path,"/logs/lte-rmtfs-delivery-v1.txt")) {
        assert((flags&O_EXCL) && (flags&O_NOFOLLOW)); assert(mode==0600);
        if(log_open_error) {errno=EEXIST;return -1;} fd=F_LOG;
    }
    if(fd<0) {errno=ENOENT;return -1;}
    positions[fd]=0;return fd;
}
static ssize_t mock_read(int fd,void *buf,size_t count)
{
    const char *s=NULL;size_t n;
    if(fd==F_EXE) {
        n=EXE_SIZE-positions[fd];if(n>count)n=count;
        memset(buf,90,n);if(bad_exe && positions[fd]==0 && n)((unsigned char*)buf)[0]^=1;
        positions[fd]+=n;return (ssize_t)n;
    }
    if(fd==F_PHYS)s="0x0000000085500000\n";
    if(fd==F_SIZE)s=bad_size?"0x0000000000100000\n":"0x0000000000200000\n";
    if(fd==F_CLIENT)s="1\n";
    if(fd==F_DEV)s=bad_dev?"243:2\n":"243:1\n";
    if(!s){errno=EBADF;return -1;}
    n=strlen(s)-positions[fd];if(n>count)n=count;
    memcpy(buf,s+positions[fd],n);positions[fd]+=n;return (ssize_t)n;
}
static ssize_t mock_pread(int fd,void *buf,size_t count,off_t off)
{
    if(fd==F_SELF_MEM) {
        metadata_reads++;
        if(metadata_error){errno=metadata_error;return -1;}
        uintptr_t p=(uintptr_t)off, a=(uintptr_t)packet, b=(uintptr_t)&destination;
        if(!((p>=a && p-a<=sizeof(packet) && count<=sizeof(packet)-(p-a)) ||
             (p>=b && p-b<=sizeof(destination) && count<=sizeof(destination)-(p-b)))) {
            errno=EFAULT;return -1;
        }
        size_t n=metadata_short?count-1:count;
        memcpy(buf,(const void*)p,n);errno=EAGAIN;return (ssize_t)n;
    }
    assert(fd==F_MEM);assert(off>=0 && (size_t)off+count<=SIZE);
    reads++;assert(calls>=reads); /* original write must already have happened */
    if(fail_read && (unsigned)fail_read==reads){errno=read_error_code;return -1;}
    size_t n=count;
    if(short_read && (unsigned)short_read==reads)n=(size_t)short_read_value;
    memcpy(buf,memory+off,n);
    if(bad_read && (unsigned)bad_read==reads && n>7)((unsigned char*)buf)[7]^=1;
    errno=EAGAIN;return (ssize_t)n;
}
static ssize_t mock_pwrite(int fd,const void *buf,size_t count,off_t off)
{
    assert(errno==EDOM || (preserve_real_errno && errno==123));
    int entry_errno=errno;
    calls++;
    if(threaded && calls==1) {
        pthread_mutex_lock(&gate);first_waiting=1;pthread_cond_signal(&cv);
        while(!release_first) { pthread_cond_wait(&cv,&gate); }
        pthread_mutex_unlock(&gate);
    }
    if(recursive && !recurse_once) {
        recurse_once=1;
        assert(rdo_test_pwrite(99,buf,count,off)==(ssize_t)count);
    }
    if(fail_write && (unsigned)fail_write==calls){errno=ENOSPC;return -1;}
    size_t n=count;
    if(short_write && (unsigned)short_write==calls)n=128;
    if(fd==3 && off>=0 && (size_t)off+n<=SIZE)memcpy(memory+off,buf,n);
    errno=preserve_real_errno?entry_errno:EBUSY;return (ssize_t)n;
}
static ssize_t mock_write(int fd,const void *buf,size_t count)
{
    assert(fd==F_LOG || fd==2);
    assert(!(fd==F_LOG && (log_reused_char || log_reused_inode)));
    assert(!(fd==2 && stderr_char));log_writes++;
    if(fail_log){errno=ENOSPC;return -1;}
    assert(log_len+count<sizeof(logs));memcpy(logs+log_len,buf,count);
    log_len+=count;logs[log_len]=0;errno=EAGAIN;return (ssize_t)count;
}
static int mock_close(int fd){assert(!(fd==F_LOG && (log_reused_char || log_reused_inode)));assert(!(fd==F_MEM && read_reused_inode));assert(!(fd==F_SELF_MEM && mem_reused_inode));return 0;}
static int mock_fstat(int fd,struct stat *st)
{
    memset(st,0,sizeof(*st));st->st_nlink=1;st->st_uid=0;st->st_gid=0;
    st->st_mode=S_IFREG|0600;st->st_dev=makedev(0,2);st->st_ino=(ino_t)fd;
    if(fd==F_EXE)st->st_size=EXE_SIZE;
    if(fd==F_MEM || fd==3) {
        st->st_mode=(wrong_type?S_IFREG:S_IFCHR)|0600;
        st->st_rdev=makedev(243,1);st->st_ino=100;
    }
    if(fd==F_LOG && log_reused_char){st->st_mode=S_IFCHR|0600;st->st_rdev=makedev(243,1);}
    if(fd==F_LOG && log_reused_inode)st->st_ino++;
    if(fd==F_MEM && read_reused_inode)st->st_ino++;
    if(fd==2 && stderr_char)st->st_mode=S_IFCHR|0600;
    if(fd==4 && !socket_bad_fd){st->st_mode=S_IFSOCK|0600;st->st_ino=200;}
    if(fd==F_SELF_MEM && mem_reused_inode)st->st_ino++;
    return 0;
}
static int mock_clock(clockid_t id,struct timespec *ts)
{(void)id;ticks+=1000;ts->tv_sec=ticks/1000000000L;ts->tv_nsec=ticks%1000000000L;return 0;}
static char *mock_getenv(const char *name)
{
    if(!strcmp(name,"RMTFS_DELIVERY_OBSERVE"))return enabled?"1":NULL;
    if(!strcmp(name,"RMTFS_DELIVERY_FSG_SHA256"))return (char*)fsg_hash;
    return NULL;
}
static ssize_t mock_sendto(int fd,const void *buf,size_t n,int flags,const struct sockaddr *addr,socklen_t len)
{
    assert(errno==123);assert(fd==expected_send_fd && buf==expected_send_buf && n==expected_send_length);
    assert(flags==expected_send_flags && addr==expected_send_addr && len==expected_send_addrlen);
    send_calls++;
    if(send_threaded && send_calls==1) {
        pthread_mutex_lock(&gate);first_waiting=1;pthread_cond_broadcast(&cv);
        while(!release_first){pthread_cond_wait(&cv,&gate);}pthread_mutex_unlock(&gate);
    }
    if(send_recursive && !send_recursed){send_recursed=1;errno=123;
        assert(rdo_test_sendto(fd,buf,n,flags,addr,len)==(ssize_t)n);}
    if(send_error){errno=send_error;return -1;}
    errno=123;return send_short?(ssize_t)send_short:(ssize_t)n;
}
static int mock_getsockopt(int fd,int level,int option,void *value,socklen_t *len)
{
    assert(fd==4 && level==SOL_SOCKET && *len==sizeof(int));
    int n=option==SO_DOMAIN?socket_domain:option==SO_TYPE?socket_type:-1;
    assert(n>=0);memcpy(value,&n,sizeof(n));*len=sizeof(n);errno=EAGAIN;return 0;
}
static int mock_getsockname(int fd,struct sockaddr *addr,socklen_t *len)
{
    assert(fd==4 && *len==sizeof(struct rdo_qrtr_address));
    struct rdo_qrtr_address a={.family=socket_bad_name?AF_UNIX:42,.node=1,.port=1234};
    memcpy(addr,&a,sizeof(a));*len=sizeof(a);errno=EAGAIN;return 0;
}
static ssize_t mock_recvfrom(int fd,void *buf,size_t n,int flags,struct sockaddr *addr,socklen_t *len)
{
    assert(errno==123);assert(fd==expected_recv_fd && buf==expected_recv_buf && n==expected_recv_length);
    assert(flags==expected_recv_flags && addr==expected_recv_addr && len==expected_recv_addrlen);
    recv_calls++;
    if(recv_probe_lock) {int held=atomic_flag_test_and_set(&rdo_busy);if(!held)atomic_flag_clear(&rdo_busy);assert(!held);}
    if(recv_error){errno=recv_error;return -1;}
    errno=123;return recv_value;
}
static const struct rdo_io io={
    .open=mock_open,.read=mock_read,.pread=mock_pread,.pwrite=mock_pwrite,
    .write=mock_write,.close=mock_close,.fstat=mock_fstat,
    .clock_gettime=mock_clock,.getenv=mock_getenv,
    .sendto=mock_sendto,.getsockopt=mock_getsockopt,.getsockname=mock_getsockname,.recvfrom=mock_recvfrom
};
static void reset(void)
{
    memset(memory,0,sizeof(memory));memset(logs,0,sizeof(logs));memset(positions,0,sizeof(positions));
    log_len=0;calls=reads=opens=log_writes=0;ticks=0;enabled=1;
    recv_calls=0;recv_error=recv_probe_lock=0;recv_value=14;
    send_calls=0;metadata_reads=0;send_error=send_short=socket_bad_name=socket_bad_fd=0;
    mem_reused_inode=metadata_error=metadata_short=send_recursive=send_recursed=send_threaded=0;
    socket_domain=42;socket_type=SOCK_DGRAM;
    static const unsigned char response[14]={2,0x56,0x34,3,0,7,0,2,4,0,0,0,0,0};
    memcpy(packet,response,sizeof(packet));
    memset(&destination,0,sizeof(destination));destination.family=42;destination.node=0;destination.port=50;
    bad_exe=bad_dev=bad_size=wrong_type=log_open_error=0;
    fail_read=short_read=bad_read=fail_write=short_write=fail_log=0;
    read_error_code=EIO;short_read_value=511;preserve_real_errno=0;
    log_reused_char=log_reused_inode=read_reused_inode=stderr_char=0;
    recursive=recurse_once=threaded=first_waiting=release_first=0;fsg_hash=INPUT_SHA;
}
static void init(void){errno=EDOM;rdo_test_init(&io,EXE_SHA);assert(errno==EDOM);}
static ssize_t invoke(int fd,size_t index,size_t count,off_t off)
{
    errno=EDOM;unsigned before=calls;
    ssize_t n=rdo_test_pwrite(fd,input+index,count,off);
    assert(calls==before+1);assert(errno==(n<0?ENOSPC:EBUSY));return n;
}
static void sequence(void)
{
    for(size_t i=0;i<SIZE;i+=512)assert(invoke(3,i,512,(off_t)i)==512);
}
static void no_complete(void){rdo_test_finish();assert(!strstr(logs,"coverage=1"));}
static ssize_t send_packet(int fd,const void *buf,size_t n,int flags,const struct sockaddr *addr,socklen_t len)
{
    expected_send_fd=fd;expected_send_buf=buf;expected_send_length=n;expected_send_flags=flags;
    expected_send_addr=addr;expected_send_addrlen=len;
    unsigned before=send_calls;errno=123;
    ssize_t result=rdo_test_sendto(fd,buf,n,flags,addr,len);
    assert(send_calls==before+1);assert(errno==(send_error?send_error:123));return result;
}
static ssize_t normal_send(void)
{return send_packet(4,packet,sizeof(packet),0,(const struct sockaddr*)&destination,sizeof(destination));}
static ssize_t recv_packet(int fd,void *buf,size_t n,int flags,struct sockaddr *addr,socklen_t *len)
{
    expected_recv_fd=fd;expected_recv_buf=buf;expected_recv_length=n;expected_recv_flags=flags;
    expected_recv_addr=addr;expected_recv_addrlen=len;unsigned before=recv_calls;errno=123;
    ssize_t result=rdo_test_recvfrom(fd,buf,n,flags,addr,len);
    assert(recv_calls==before+1 && errno==(recv_error?recv_error:123));return result;
}
static const char *response_record(void)
{const char *p=strstr(logs,"RMTFS_RESPONSE");return p?p:"";}
static void reject_transport(void)
{assert(!strstr(logs,"transport_accepted=1"));rdo_test_finish();}
static void *other_thread_send(void *unused)
{(void)unused;assert(normal_send()==14);return NULL;}
static void *concurrent_sender(void *unused)
{
    (void)unused;pthread_mutex_lock(&gate);while(!first_waiting){pthread_cond_wait(&cv,&gate);}pthread_mutex_unlock(&gate);
    errno=123;assert(rdo_test_sendto(4,packet,14,0,(const struct sockaddr*)&destination,12)==14);assert(errno==123);
    pthread_mutex_lock(&gate);release_first=1;pthread_cond_signal(&cv);pthread_mutex_unlock(&gate);return NULL;
}
static void pass(const char *name){printf("PASS %s\n",name);}
static void *thread_call(void *unused)
{
    (void)unused;errno=EDOM;assert(rdo_test_pwrite(3,input,512,0)==512);assert(errno==EBUSY);return NULL;
}
int main(void)
{
    for(size_t i=0;i<SIZE;i++)input[i]=(unsigned char)(i*17+(i>>8));
    reset();enabled=0;init();assert(invoke(3,0,512,0)==512);rdo_test_finish();
    assert(opens==0 && reads==0 && log_len==0);pass("disabled_passthrough");
    reset();init();sequence();rdo_test_finish();
    assert(calls==4096 && reads==4096);assert(!memcmp(memory,input,SIZE));
    assert(strstr(logs,"coverage=1") && strstr(logs,"fsg=1"));
    assert(strstr(logs, "input_sha256=" INPUT_SHA) && strstr(logs, "readback_sha256=" INPUT_SHA));
    assert(strstr(logs,"differences=0 first_difference=-1"));pass("complete_exact_fsg_and_readback");
    reset();fsg_hash="0000000000000000000000000000000000000000000000000000000000000000";
    init();sequence();rdo_test_finish();assert(strstr(logs,"coverage=1"));assert(!strstr(logs,"fsg=1"));pass("input_hash_not_fsg");
    reset();init();bad_read=3;sequence();rdo_test_finish();
    assert(strstr(logs,"coverage=1"));assert(strstr(logs,"differences=1 first_difference=1031"));
    assert(!strstr(logs,"readback_sha256=" INPUT_SHA));pass("readback_mismatch");
    reset();init();assert(invoke(99,0,512,0)==512);no_complete();assert(reads==0);pass("non_target_fd");
    reset();bad_dev=1;init();assert(invoke(3,0,512,0)==512);no_complete();assert(reads==0);pass("sysfs_node_mismatch");
    reset();wrong_type=1;init();assert(invoke(3,0,512,0)==512);no_complete();assert(reads==0);pass("non_char_node");
    reset();bad_size=1;init();assert(invoke(3,0,512,0)==512);no_complete();assert(reads==0);pass("wrong_window_size");
    reset();bad_exe=1;init();assert(invoke(3,0,512,0)==512);no_complete();assert(reads==0);pass("wrong_original_executable");
    reset();fsg_hash="not-a-sha256";init();assert(invoke(3,0,512,0)==512);no_complete();assert(reads==0);pass("malformed_expected_hash");
    reset();log_open_error=1;init();assert(invoke(3,0,512,0)==512);no_complete();assert(reads==0);pass("exclusive_log_failure");
    reset();init();short_write=1;assert(invoke(3,0,512,0)==128);no_complete();assert(reads==0);pass("real_short_write_preserved");
    reset();init();fail_write=1;assert(invoke(3,0,512,0)==-1);no_complete();assert(reads==0);pass("real_error_errno_preserved");
    reset();init();preserve_real_errno=1;errno=123;assert(rdo_test_pwrite(3,input,512,0)==512);
    assert(errno==123);no_complete();pass("successful_original_errno_preserved");
    reset();init();fail_read=1;assert(invoke(3,0,512,0)==512);no_complete();assert(reads==1);assert(strstr(logs,"read_return=-1 read_errno=5"));pass("readback_error_not_write_error");
    reset();init();fail_read=1;read_error_code=EINTR;assert(invoke(3,0,512,0)==512);
    no_complete();assert(reads==1);assert(strstr(logs,"read_return=-1 read_errno=4"));pass("readback_eintr_no_retry");
    reset();init();short_read=1;short_read_value=1;assert(invoke(3,0,512,0)==512);
    no_complete();assert(reads==1);assert(strstr(logs,"read_return=1 read_errno=11"));pass("readback_one_byte_no_retry");
    reset();init();short_read=1;assert(invoke(3,0,512,0)==512);no_complete();assert(reads==1);assert(strstr(logs,"read_return=511 read_errno=11"));pass("readback_short_not_write_error");
    reset();init();assert(invoke(3,0,512,0)==512);assert(invoke(3,1024,512,1024)==512);no_complete();pass("out_of_order_gap");
    reset();init();assert(invoke(3,0,512,0)==512);assert(invoke(3,512,512,0)==512);no_complete();pass("overlap_incomplete");
    reset();init();assert(invoke(3,0,512,-512)==512);assert(invoke(3,0,512,SIZE)==512);no_complete();assert(reads==0);pass("out_of_bounds_passthrough");
    reset();init();assert(invoke(3,0,511,0)==511);no_complete();assert(reads==0);pass("non_sector_write_passthrough");
    reset();init();fail_log=1;sequence();rdo_test_finish();assert(calls==4096);pass("log_failure_preserves_original");
    reset();init();recursive=1;errno=EDOM;assert(rdo_test_pwrite(3,input,512,0)==512);assert(errno==EBUSY);assert(calls==2);no_complete();pass("recursive_observation_is_bypassed");
    reset();init();for(unsigned i=0;i<40000;i++)assert(invoke(99,0,512,0)==512);
    sequence();rdo_test_finish();assert(reads==0);assert(log_len<16384);pass("bounded_call_budget");
    reset();init();for(unsigned i=0;i<5;i++)sequence();rdo_test_finish();
    assert(calls==5*4096 && reads==4*4096 && log_len<16384);pass("bounded_sequence_byte_budget");
    reset();init();threaded=1;pthread_t t;assert(!pthread_create(&t,NULL,thread_call,NULL));
    pthread_mutex_lock(&gate);while(!first_waiting)pthread_cond_wait(&cv,&gate);pthread_mutex_unlock(&gate);
    assert(invoke(99,0,512,0)==512);
    pthread_mutex_lock(&gate);release_first=1;pthread_cond_signal(&cv);pthread_mutex_unlock(&gate);
    assert(!pthread_join(t,NULL));no_complete();assert(calls==2 && reads==0);
    assert(strstr(logs,"reason=concurrent"));pass("concurrent_original_calls_preserved_observation_invalidated");
    reset();init();log_reused_char=1;assert(invoke(3,0,512,0)==512);no_complete();
    assert(strstr(logs,"reason=log_changed"));pass("reused_log_char_not_written_or_closed");
    reset();init();log_reused_inode=1;assert(invoke(3,0,512,0)==512);no_complete();
    assert(strstr(logs,"reason=log_changed"));pass("reused_log_regular_inode_not_written_or_closed");
    reset();init();read_reused_inode=1;assert(invoke(3,0,512,0)==512);no_complete();
    assert(reads==0 && strstr(logs,"reason=readfd_changed"));pass("reused_read_inode_not_read_or_closed");
    reset();log_open_error=1;stderr_char=1;init();assert(invoke(3,0,512,0)==512);no_complete();
    assert(log_writes==0 && reads==0);pass("fallback_never_writes_character_stderr");
    reset();enabled=0;init();assert(normal_send()==14);assert(!log_len && !metadata_reads);rdo_test_finish();pass("send_disabled_exact_passthrough");
    reset();init();assert(normal_send()==14);assert(!*response_record() && !metadata_reads);rdo_test_finish();pass("send_no_fsg_no_association");
    reset();init();sequence();assert(normal_send()==14);
    assert(strstr(response_record(),"reason=accepted") && strstr(response_record(),"transport_accepted=1"));
    assert(strstr(response_record(),"request_transaction_verified=0") && strstr(response_record(),"association=next_send_same_thread"));
    assert(strstr(response_record(),"transaction=13398 message_id=3 payload_length=7"));
    assert(strstr(response_record(),"qmi_result=0 qmi_error=0") && metadata_reads==2);
    size_t after_log=log_len;assert(normal_send()==14);assert(log_len==after_log);rdo_test_finish();pass("send_full_fsg_next_send_metadata_not_request_proof");
    reset();init();sequence();packet[1]=0xff;packet[2]=0xff;assert(normal_send()==14);
    assert(strstr(response_record(),"transaction=65535") && strstr(response_record(),"request_transaction_verified=0"));
    rdo_test_finish();pass("send_records_arbitrary_transaction_does_not_verify_request");
    for(unsigned i=0;i<6;i++) {
        reset();init();sequence();const unsigned offsets[]={0,3,5,7,8,10};packet[offsets[i]]=99;
        assert(normal_send()==14);assert(strstr(response_record(),"packet_ok=0"));reject_transport();
    }pass("send_rejects_type_msgid_payload_tlv_length_result");
    reset();init();sequence();packet[10]=1;packet[12]=52;assert(normal_send()==14);
    assert(strstr(response_record(),"reason=qmi_error") && strstr(response_record(),"qmi_result=1 qmi_error=52"));
    assert(strstr(response_record(),"transport_accepted=1"));rdo_test_finish();pass("send_qmi_error_not_modem_success");
    reset();init();sequence();socket_domain=AF_UNIX;assert(normal_send()==14);assert(strstr(response_record(),"socket_ok=0"));reject_transport();pass("send_wrong_socket_domain");
    reset();init();sequence();socket_type=SOCK_STREAM;assert(normal_send()==14);reject_transport();pass("send_wrong_socket_type");
    reset();init();sequence();socket_bad_name=1;assert(normal_send()==14);reject_transport();pass("send_wrong_local_family");
    reset();init();sequence();socket_bad_fd=1;assert(normal_send()==14);assert(!metadata_reads);reject_transport();pass("send_non_socket_fd");
    for(unsigned i=0;i<5;i++) {
        reset();init();sequence();if(i==0)destination.family=AF_UNIX;else if(i==1)destination.node=UINT32_MAX;
        else destination.port=i==2?0:i==3?UINT32_MAX:UINT32_C(0xfffffffe);
        assert(normal_send()==14);assert(strstr(response_record(),"destination_ok=0"));reject_transport();
    }pass("send_rejects_nonqrtr_broadcast_control_destination");
    reset();init();sequence();assert(send_packet(4,packet,14,0,(const struct sockaddr*)&destination,8)==14);reject_transport();pass("send_wrong_address_length");
    reset();init();sequence();assert(send_packet(4,packet,7,0,(const struct sockaddr*)&destination,12)==7);reject_transport();pass("send_wrong_packet_length");
    reset();init();sequence();assert(send_packet(4,packet,14,MSG_DONTWAIT,(const struct sockaddr*)&destination,12)==14);reject_transport();pass("send_wrong_flags_preserved");
    reset();init();sequence();send_short=7;assert(normal_send()==7);assert(strstr(response_record(),"reason=send_short"));reject_transport();pass("send_short_return_no_retry");
    const int errors[]={EIO,EPERM,EINTR};
    for(unsigned i=0;i<sizeof(errors)/sizeof(errors[0]);i++) {
        reset();init();sequence();send_error=errors[i];assert(normal_send()==-1);
        char expected[64];snprintf(expected,sizeof(expected),"send_return=-1 send_errno=%d",errors[i]);
        assert(strstr(response_record(),expected));reject_transport();
    }pass("send_eio_eperm_eintr_return_errno_no_retry");
    reset();init();sequence();send_error=EFAULT;
    assert(send_packet(4,(void*)1,14,0,(const struct sockaddr*)1,12)==-1);
    assert(strstr(response_record(),"copy_ok=0") && strstr(response_record(),"send_errno=14"));reject_transport();pass("send_efault_bad_buffer_and_address_never_dereferenced");
    reset();init();sequence();send_error=EFAULT;
    assert(send_packet(4,(void*)1,14,0,(const struct sockaddr*)&destination,12)==-1);
    assert(metadata_reads==2 && strstr(response_record(),"copy_ok=0"));reject_transport();pass("send_bad_buffer_safe_copy_only");
    reset();init();sequence();metadata_short=1;assert(normal_send()==14);reject_transport();pass("send_metadata_short_read_inconclusive");
    reset();init();sequence();mem_reused_inode=1;assert(normal_send()==14);assert(!metadata_reads);reject_transport();pass("send_memfd_reuse_not_read_or_closed");
    reset();init();sequence();assert(invoke(99,0,512,0)==512);assert(normal_send()==14);
    assert(strstr(response_record(),"reason=intervening_pwrite") && !metadata_reads);reject_transport();pass("send_intervening_pwrite_clears_pending");
    reset();init();sequence();pthread_t sender;assert(!pthread_create(&sender,NULL,other_thread_send,NULL));assert(!pthread_join(sender,NULL));
    assert(!*response_record());assert(normal_send()==14);assert(strstr(response_record(),"reason=intervening_io"));reject_transport();pass("send_other_thread_cannot_consume_same_thread_pending");
    reset();init();sequence();socket_domain=AF_UNIX;assert(normal_send()==14);after_log=log_len;socket_domain=42;assert(normal_send()==14);
    assert(log_len==after_log);reject_transport();pass("send_invalid_first_send_consumes_pending");
    reset();init();sequence();send_recursive=1;expected_send_fd=4;expected_send_buf=packet;expected_send_length=14;expected_send_flags=0;
    expected_send_addr=(const struct sockaddr*)&destination;expected_send_addrlen=12;errno=123;
    assert(rdo_test_sendto(4,packet,14,0,(const struct sockaddr*)&destination,12)==14 && errno==123 && send_calls==2);
    reject_transport();pass("send_recursive_real_calls_preserved_no_false_acceptance");
    reset();init();for(unsigned i=0;i<40000;i++)assert(normal_send()==14);sequence();assert(normal_send()==14);
    assert(!metadata_reads);reject_transport();pass("send_total_call_budget_preserves_calls");
    reset();init();for(unsigned i=0;i<5;i++){sequence();assert(normal_send()==14);}
    assert(metadata_reads==8 && rdo.metadata_bytes==104);rdo_test_finish();pass("send_metadata_budget_four_sequences");
    reset();init();sequence();fail_log=1;assert(normal_send()==14);reject_transport();pass("send_log_failure_not_ack_loss_claim");
    reset();init();sequence();rdo_test_finish();assert(strstr(response_record(),"attempted=0 reason=process_exit"));
    assert(strstr(response_record(),"modem_acceptance=unknown"));pass("send_absence_is_inconclusive_not_ack_loss");
    reset();enabled=0;init();assert(recv_packet(4,packet,14,0,NULL,NULL)==14);
    assert(!log_len && !metadata_reads);rdo_test_finish();pass("recv_disabled_exact_passthrough");
    reset();init();assert(recv_packet(4,packet,14,0,NULL,NULL)==14);
    assert(!*response_record() && !metadata_reads);rdo_test_finish();pass("recv_no_pending_does_not_parse_request");
    reset();init();sequence();recv_probe_lock=1;
    assert(recv_packet(4,packet,14,0,NULL,NULL)==14);assert(normal_send()==14);
    assert(strstr(response_record(),"attempted=0 reason=receive_boundary"));reject_transport();
    assert(!metadata_reads);pass("recv_next_request_boundary_prevents_stale_send_association");
    reset();init();sequence();recv_error=EFAULT;recv_probe_lock=1;
    assert(recv_packet(4,(void*)1,14,MSG_DONTWAIT,(struct sockaddr*)1,(socklen_t*)1)==-1);
    assert(normal_send()==14);assert(!metadata_reads);reject_transport();pass("recv_efault_invalid_parameters_forwarded_once_unread");
    reset();init();sequence();recv_error=EINTR;
    assert(recv_packet(4,packet,14,0,NULL,NULL)==-1);assert(normal_send()==14);reject_transport();pass("recv_eintr_no_retry_and_pending_invalidated");
    reset();init();sequence();recv_value=3;recv_probe_lock=1;
    assert(recv_packet(4,packet,14,0,NULL,NULL)==3);assert(normal_send()==14);reject_transport();pass("recv_short_return_preserved_no_observer_lock_during_receive");
    reset();init();sequence();send_threaded=1;
    expected_send_fd=4;expected_send_buf=packet;expected_send_length=14;expected_send_flags=0;
    expected_send_addr=(const struct sockaddr*)&destination;expected_send_addrlen=12;
    assert(!pthread_create(&sender,NULL,concurrent_sender,NULL));errno=123;
    assert(rdo_test_sendto(4,packet,14,0,(const struct sockaddr*)&destination,12)==14 && errno==123);
    assert(!pthread_join(sender,NULL));assert(send_calls==2);reject_transport();
    assert(strstr(logs,"reason=concurrent"));pass("send_concurrent_real_calls_not_serialized_or_replayed");
    reset();init();sequence();send_short=15;assert(normal_send()==15);reject_transport();pass("send_impossible_oversize_return_not_complete");
    puts("ALL_TESTS_PASS offline synthetic fixture; no device or RF acceptance");return 0;
}
