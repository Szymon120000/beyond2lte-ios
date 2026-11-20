#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
"""Exercise publish/replace of owned bootstrap payloads over a tiny FS model."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'xnu-overlay/bsd/kern/b2l_userspace_seed.h').read_text()
a=s.index('static int b2l_apps_remove');b=s.index('#include "b2l_profile_cache.h"',a)
actual=s[a:b]
pre=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <stdio.h>
typedef char *caddr_t;typedef int vfs_context_t;
enum vtype{VREG,VDIR};
struct vnode_attr{unsigned va_uid,va_mode,va_nlink;off_t va_data_size;};
#define MAX_ENTRIES 8
struct entry{char path[160];unsigned char data[12000];off_t bytes;unsigned mode,links;int type;int exists;};
typedef struct entry *vnode_t;
static struct entry entries[MAX_ENTRIES];
static bool root_type(vnode_t vp,enum vtype t){return vp&&vp->type==(int)t;}
#define VATTR_INIT(p) memset(p,0,sizeof(*(p)))
#define VATTR_WANTED(p,x) ((void)0)
#define VATTR_IS_SUPPORTED(p,x) 1
#define UIO_READ 0
#define UIO_WRITE 1
#define UIO_SYSSPACE 0
#define IO_SYNC 1
#define MNT_WAIT 1
#define FREAD 1
#define FWRITE 2
#define O_CREAT 0200
#define VNODE_LOOKUP_NOFOLLOW_ANY 0
#define NOFOLLOW_ANY 0
#define LOCKPARENT 1
#define LOCKLEAF 2
#define NAMEI_NOFOLLOW_ANY 4
#define DELETE 2
#define CREATE 1
#define OP_UNLINK 6
#define OP_LINK 5
#ifdef CAST_USER_ADDR_T
#undef CAST_USER_ADDR_T
#endif
#define CAST_USER_ADDR_T(x) ((void *)(uintptr_t)(x))
static const unsigned char b2l_apps_executable[1]={0},b2l_hfs_compat_dylib[1]={0},b2l_graphics_compat_dylib[1]={0};
static bool b2l_apps_payload_equal(const unsigned char*s,size_t bytes,size_t off,const unsigned char*a,size_t n,bool native){
 assert(!native&&off<=bytes&&n<=bytes-off);return !memcmp(s+off,a,n);
}
static int writes,syncs;
static int vnode_getattr(vnode_t p,struct vnode_attr *v,int ctx){(void)ctx;*v=(struct vnode_attr){0,p->mode,p->links,p->bytes};return 0;}
static int kauth_cred_get(void){return 0;}static int current_proc(void){return 0;}
static int vn_rdwr(int op,vnode_t p,caddr_t data,int n,off_t at,int space,int flags,int cred,int *resid,int proc){
 (void)space;(void)flags;(void)cred;(void)proc;assert(n>=0&&at>=0&&(size_t)(at+n)<=sizeof(p->data));*resid=0;
 if(op==UIO_READ){assert(at+n<=p->bytes);memcpy(data,p->data+at,n);return 0;}
 writes++;memcpy(p->data+at,data,n);p->bytes=at+n;return 0;
}
static int VNOP_FSYNC(vnode_t p,int flags,int ctx){(void)p;(void)flags;(void)ctx;syncs++;return 0;}
struct componentname{const char *cn_nameptr;};
struct nameidata{vnode_t ni_vp,ni_dvp;struct componentname ni_cnd;int ni_flag,op;};
#define NDINIT(ndp,operation,pop,flags,segflg,namep,ctx) do{ \
 (ndp)->op=(operation);(ndp)->ni_cnd.cn_nameptr=(const char *)(namep); \
 (ndp)->ni_vp=NULL;(ndp)->ni_dvp=NULL;(ndp)->ni_flag=0;}while(0)
static struct entry *find(const char *p){for(unsigned i=0;i<MAX_ENTRIES;i++)if(entries[i].exists&&!strcmp(entries[i].path,p))return &entries[i];return NULL;}
static struct entry *add(const char *p){for(unsigned i=0;i<MAX_ENTRIES;i++)if(!entries[i].exists){memset(&entries[i],0,sizeof(entries[i]));strncpy(entries[i].path,p,sizeof(entries[i].path)-1);entries[i].exists=1;entries[i].type=VREG;entries[i].links=1;return &entries[i];}assert(0);return NULL;}
static struct entry dirnode={.type=VDIR};
static int vnode_put(vnode_t vp){(void)vp;return 0;}
static int vnode_lookup(const char *p,int flags,vnode_t *vpp,int ctx){(void)flags;(void)ctx;vnode_t e=find(p);if(!e)return ENOENT;*vpp=e;return 0;}
static int vnode_open(const char *p,int fmode,int cmode,int flags,vnode_t *vpp,int ctx){(void)flags;(void)ctx;vnode_t e=find(p);if(!e){if(!(fmode&O_CREAT))return ENOENT;e=add(p);e->mode=cmode;}*vpp=e;return 0;}
static int vnode_close(vnode_t vp,int fmode,int ctx){(void)vp;(void)fmode;(void)ctx;return 0;}
static int namei(struct nameidata *nd){vnode_t e=find(nd->ni_cnd.cn_nameptr);if(nd->op==CREATE){nd->ni_vp=e;nd->ni_dvp=&dirnode;return 0;}if(nd->op==DELETE){if(!e)return ENOENT;nd->ni_vp=e;nd->ni_dvp=&dirnode;return 0;}assert(0);return EINVAL;}
static int VNOP_LINK(vnode_t vp,vnode_t dvp,struct componentname *cnp,int ctx){(void)dvp;(void)ctx;if(find(cnp->cn_nameptr))return EEXIST;vnode_t e=add(cnp->cn_nameptr);memcpy(e->data,vp->data,sizeof(e->data));e->bytes=vp->bytes;e->mode=vp->mode;e->links=2;vp->links=2;return 0;}
static int VNOP_REMOVE(vnode_t dvp,vnode_t vp,struct componentname *cnp,int flags,int ctx){(void)dvp;(void)flags;(void)ctx;assert(find(cnp->cn_nameptr)==vp);vp->exists=0;if(vp->links)vp->links--;return 0;}
static void lookup_done(struct nameidata *nd){(void)nd;}
'''
post=r'''
static unsigned char payload[10013];
static void reset(void){memset(entries,0,sizeof(entries));writes=syncs=0;}
int main(void){
 for(size_t i=0;i<sizeof(payload);i++)payload[i]=(unsigned char)(i*29+7);
 const char *final="/usr/local/libexec/x",*staging="/usr/local/libexec/.x";
 /* first publication and an idempotent replay */
 reset();assert(!b2l_apps_publish(final,staging,payload,sizeof(payload),0755,0));
 vnode_t f=find(final);assert(f&&f->bytes==(off_t)sizeof(payload)&&f->mode==0755&&!memcmp(f->data,payload,sizeof(payload)));
 int w=writes;assert(!b2l_apps_publish(final,staging,payload,sizeof(payload),0755,0));assert(writes==w);
 /* a stale copy from another build is replaced, not fatal */
 f->data[777]^=1;int bad=f->data[777];assert(!b2l_apps_publish(final,staging,payload,sizeof(payload),0755,0));
 f=find(final);assert(f&&f->data[777]!=bad&&!memcmp(f->data,payload,sizeof(payload)));
 /* a stale mode is replaced */
 f->mode=0644;assert(!b2l_apps_publish(final,staging,payload,sizeof(payload),0755,0));assert(find(final)->mode==0755);
 /* a stale staging prefix is discarded and rewritten */
 reset();vnode_t s=add(staging);memcpy(s->data,payload,64);s->bytes=64;s->data[3]^=1;
 assert(!b2l_apps_publish(final,staging,payload,sizeof(payload),0755,0));
 f=find(final);assert(f&&f->bytes==(off_t)sizeof(payload)&&!memcmp(f->data,payload,sizeof(payload)));
 /* a directory at the owned path is refused rather than deleted */
 reset();vnode_t d=add(final);d->type=VDIR;assert(b2l_apps_publish(final,staging,payload,sizeof(payload),0755,0)==EPERM);
 puts("owned payload publish: idempotent/stale-replace/prefix-resume/directory-refusal PASS");
}
'''
with tempfile.TemporaryDirectory(prefix='b2l-publish-test-') as td:
 p=Path(td);(p/'t.c').write_text(pre+actual+post)
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'t.c'),'-o',str(p/'t')],check=True)
 subprocess.run([str(p/'t')],check=True)
