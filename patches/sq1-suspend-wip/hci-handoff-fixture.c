#include <sys/socket.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
static int downs,binds;
static void report(void){printf("downs=%d binds=%d\n",downs,binds);}
int __wrap_socket(int d,int t,int p){(void)d;(void)t;(void)p;atexit(report);return 123;}
int __wrap_ioctl(int f,unsigned long req,...){(void)f;if(req==_IOW('H',202,int))downs++;return 0;}
int __wrap_bind(int f,const struct sockaddr *a,socklen_t n){(void)f;(void)a;(void)n;binds++; const char *mode=getenv("CASE");
if(downs){errno=ETIMEDOUT;return -1;}
if(mode&&!strcmp(mode,"denied")){errno=EACCES;return -1;}
if(mode&&!strcmp(mode,"busy")){errno=EBUSY;return -1;}
if(binds<3){errno=EBUSY;return -1;}return 0;}
unsigned int __wrap_sleep(unsigned int s){(void)s;return 0;}
int __wrap_posix_openpt(int f){(void)f;exit(0);}
