/* Host-only model/fixture; see check-sde-native-README.md. */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stddef.h>
#include <errno.h>
#define SDE_ERROR(...) ((void)0)
#define MAX_ALLOWED_CRTC_CNT_DURING_SECURE 1
#define container_of(p,t,m) ((t*)((char*)(p)-offsetof(t,m)))
struct list_head {struct list_head*next,*prev;};
#define INIT_LIST_HEAD(h) ((h)->next=(h)->prev=(h))
static void list_add(struct list_head*n,struct list_head*h){n->prev=h;n->next=h->next;h->next->prev=n;h->next=n;}
#define list_for_each_entry(p,h,m) for(p=container_of((h)->next,__typeof__(*p),m);&p->m!=(h);p=container_of(p->m.next,__typeof__(*p),m))
struct drm_crtc_state {bool active;uint32_t ns,sec,direct;};
struct drm_crtc {struct drm_crtc_state*state;struct list_head head;struct {int id;}base;};
struct drm_device {struct {struct list_head crtc_list;}mode_config;};
struct msm_kms {int unused;};struct sde_kms {struct msm_kms base;struct drm_device*dev;};
#define to_sde_kms(k) container_of(k,struct sde_kms,base)
struct drm_atomic_state {struct drm_crtc*state_crtc;struct drm_crtc_state*state_new;int count;};
#define for_each_new_crtc_in_state(st,c,cs,i) for(i=0;i<(st)->count&&((c=(st)->state_crtc),(cs=(st)->state_new),1);i++)
static void sde_crtc_state_find_plane_fb_modes(struct drm_crtc_state*s,uint32_t*n,uint32_t*sec,uint32_t*d){*n=s->ns;*sec=s->sec;*d=s->direct;}
static void sde_crtc_find_plane_fb_modes(struct drm_crtc*c,uint32_t*n,uint32_t*s,uint32_t*d){sde_crtc_state_find_plane_fb_modes(c->state,n,s,d);}
