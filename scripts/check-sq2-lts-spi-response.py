#!/usr/bin/env python3
"""Compile the actual body-completion function with a bounded buffer fixture.

This checks callback routing and the unchanged length guard, not hardware input.
"""
import pathlib, subprocess, sys, tempfile

source=pathlib.Path(sys.argv[1]).read_text()
start=source.index('static void spi_hid_input_body_complete(void *_shid)\n{')
end=source.index('static void spi_hid_input_header_complete(void *_shid)\n{',start)
callback=source[start:end]
p=source.index('static int spi_hid_process_input_report(')
a=source.index('\tif (body.content_length > header.report_length)',p)
b=source.index('\n\tif (body.content_id',a)
guard=source[a:b]
stub=r'''
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
struct device { int unused; };
struct spi { struct device dev; };
struct spi_hid_input_buf { unsigned char header[4],body[4000]; int report_length,content_length; };
struct transfer { void *rx_buf,*tx_buf; int len; };
struct spi_hid { struct spi *spi; int input_lock,powered,pm_suspended,input_stage;
 struct transfer input_transfer[2]; struct { int status; } input_message;
 int bus_error_count,bus_last_error,error_work,power_state,input_transfer_pending;
 struct spi_hid_input_buf input,response; };
#define READ_ONCE(x) (x)
#define SPI_HID_INPUT_STAGE_IDLE 0
#define SPI_HID_POWER_MODE_OFF 0
#define spin_lock_irqsave(x,y) ((void)0)
#define spin_unlock_irqrestore(x,y) ((void)0)
#define trace_spi_hid_input_body_complete(...) ((void)0)
#define dev_warn(...) ((void)0)
#define dev_err(...) ((void)0)
static int resets,selected,result;
static void schedule_work(int *w) { resets++; }
static void spi_hid_input_header_complete(void *p) {}
static int spi_hid_input_async(struct spi_hid *s, void *h, int n, void (*cb)(void *)) {return 0;}
static int validate(struct spi_hid_input_buf *buf) {
 struct { int report_length; } header={buf->report_length};
 struct { int content_length; } body={buf->content_length};
GUARD
 return 0;
}
static int spi_hid_process_input_report(struct spi_hid *s,struct spi_hid_input_buf *buf) {
 selected=(buf==&s->response);result=validate(buf);return result;
}
CALLBACK
static void check(const char *name,int response,int length) {
 struct spi spi={0}; struct spi_hid s={.spi=&spi,.powered=1,.power_state=1,.input_transfer_pending=1};
 s.input.report_length=124;s.input.content_length=response?3499:100;
 s.response.report_length=124;s.response.content_length=length;
 s.input_transfer[1].rx_buf=response?s.response.body:s.input.body;
 resets=0;selected=-1;result=999;
 spi_hid_input_body_complete(&s);
 printf("%s selected=%s result=%d resets=%d\n",name,selected?"response":"input",result,resets);
}
int main(void) {check("valid_response_stale_touch",1,100);check("oversized_response",1,3499);check("normal_touch",0,100);return 0;}
'''.replace('GUARD',guard).replace('CALLBACK',callback)
with tempfile.TemporaryDirectory(prefix='spi-response-check-') as d:
    c=pathlib.Path(d)/'check.c';c.write_text(stub);exe=pathlib.Path(d)/'check'
    subprocess.run(['cc','-std=c11',str(c),'-o',str(exe)],check=True)
    sys.exit(subprocess.run([str(exe)]).returncode)
