/* SPDX-License-Identifier: BSD-2-Clause
 * Opt-in publication of the owned native migration launcher. Existing owned
 * files are compared first; a stale copy (another build or an uncertified
 * native rewrite) is replaced instead of blocking every later boot. A synced
 * executable is published before its launch job.
 */
#include <pexpert/pexpert.h>
#include "b2l_userspace_payload.h"
#include "b2l_apps_payload_compare.h"
static int b2l_apps_cache_ready;
static int b2l_compat_ready;
int b2l_apps_cache_is_ready(void){return b2l_apps_cache_ready;}
/* The runtime compatibility libraries only need their own published files.
 * Keep them usable when the separate migration job/cache publication fails,
 * so a stale bootstrap cannot also disable rendering compatibility. */
int b2l_compat_is_ready(void){return b2l_compat_ready;}
static int b2l_apps_directory(const char *path,vfs_context_t ctx)
{
    struct nameidata nd;struct vnode_attr va;vnode_t vp=NULL;
    NDINIT(&nd,CREATE,OP_MKDIR,LOCKPARENT,UIO_SYSSPACE,CAST_USER_ADDR_T(path),ctx);
    nd.ni_flag|=NAMEI_NOFOLLOW_ANY;nd.ni_cnd.cn_flags|=WILLBEDIR;
    int error=namei(&nd);if(error)return error;
    if(!root_type(nd.ni_dvp,VDIR)){error=EXDEV;goto out;}
    if(nd.ni_vp){
        VATTR_INIT(&va);VATTR_WANTED(&va,va_uid);VATTR_WANTED(&va,va_mode);
        error=root_type(nd.ni_vp,VDIR)?vnode_getattr(nd.ni_vp,&va,ctx):ENOTDIR;
        if(!error&&(!VATTR_IS_SUPPORTED(&va,va_uid)||!VATTR_IS_SUPPORTED(&va,va_mode)||va.va_uid||(va.va_mode&0022)))error=EPERM;
        goto out;
    }
    VATTR_INIT(&va);VATTR_SET(&va,va_type,VDIR);VATTR_SET(&va,va_mode,0755);
    VATTR_SET(&va,va_uid,0);VATTR_SET(&va,va_gid,0);
    error=vn_mkdir(nd.ni_dvp,&vp,&nd,&va,ctx);
    if(!error){
        vnode_update_identity(vp,nd.ni_dvp,nd.ni_cnd.cn_nameptr,nd.ni_cnd.cn_namelen,nd.ni_cnd.cn_hash,VNODE_UPDATE_NAME|VNODE_UPDATE_PARENT);
        error=VNOP_FSYNC(vp,MNT_WAIT,ctx);if(!error)error=VNOP_FSYNC(nd.ni_dvp,MNT_WAIT,ctx);
    }
    if(vp)vnode_put(vp);
out:lookup_done(&nd);return error;
}
static int b2l_apps_remove(const char *path,vfs_context_t ctx)
{
    struct nameidata nd;
    NDINIT(&nd,DELETE,OP_UNLINK,LOCKPARENT|LOCKLEAF,UIO_SYSSPACE,CAST_USER_ADDR_T(path),ctx);
    nd.ni_flag|=NAMEI_NOFOLLOW_ANY;
    int error=namei(&nd);if(error)return error==ENOENT?0:error;
    if(!nd.ni_vp)error=ENOENT;
    else if(root_type(nd.ni_vp,VDIR))error=EPERM;
    else {
        error=VNOP_REMOVE(nd.ni_dvp,nd.ni_vp,&nd.ni_cnd,ctx);
        if(!error)error=VNOP_FSYNC(nd.ni_dvp,MNT_WAIT,ctx);
    }
    lookup_done(&nd);return error;
}
static int b2l_apps_sync_payload(vnode_t vp,const unsigned char *data,size_t bytes,
    unsigned mode,bool writing,vfs_context_t ctx)
{
    if(!bytes||bytes>4*1024*1024||!root_type(vp,VREG))return EINVAL;
    struct vnode_attr va;VATTR_INIT(&va);
    VATTR_WANTED(&va,va_uid);VATTR_WANTED(&va,va_mode);VATTR_WANTED(&va,va_nlink);VATTR_WANTED(&va,va_data_size);
    int error=vnode_getattr(vp,&va,ctx);if(error)return error;
    if(!VATTR_IS_SUPPORTED(&va,va_uid)||!VATTR_IS_SUPPORTED(&va,va_mode)||!VATTR_IS_SUPPORTED(&va,va_nlink)||!VATTR_IS_SUPPORTED(&va,va_data_size)||
       va.va_uid||(va.va_mode&0777)!=mode||va.va_nlink<1||va.va_nlink>(writing?1:2)||va.va_data_size<0||
       (uint64_t)va.va_data_size>bytes||(!writing&&(uint64_t)va.va_data_size!=bytes))return EINVAL;
    unsigned char check[256];size_t size=(size_t)va.va_data_size;
    for(size_t offset=0;offset<size;){
        size_t n=size-offset;if(n>sizeof(check))n=sizeof(check);int resid=0;
        error=vn_rdwr(UIO_READ,vp,(caddr_t)check,(int)n,(off_t)offset,UIO_SYSSPACE,0,kauth_cred_get(),&resid,current_proc());
        if(error||resid||!b2l_apps_payload_equal(data,bytes,offset,check,n,
            !writing&&mode==0755&&(data==b2l_apps_executable||data==b2l_hfs_compat_dylib||data==b2l_graphics_compat_dylib)))return error?error:EIO;
        offset+=n;
    }
    if(!writing)return 0;
    for(size_t offset=size;offset<bytes;){
        size_t n=bytes-offset;if(n>4096)n=4096;int resid=0;
        error=vn_rdwr(UIO_WRITE,vp,(caddr_t)data+offset,(int)n,(off_t)offset,UIO_SYSSPACE,IO_SYNC,kauth_cred_get(),&resid,current_proc());
        if(error||resid)return error?error:EIO;offset+=n;
    }
    error=VNOP_FSYNC(vp,MNT_WAIT,ctx);if(error)return error;
    return b2l_apps_sync_payload(vp,data,bytes,mode,false,ctx);
}
static int b2l_apps_publish(const char *final,const char *staging,const unsigned char *data,size_t bytes,unsigned mode,vfs_context_t ctx)
{
    vnode_t vp=NULL;int error=vnode_lookup(final,VNODE_LOOKUP_NOFOLLOW_ANY,&vp,ctx);
    if(!error){
        error=b2l_apps_sync_payload(vp,data,bytes,mode,false,ctx);vnode_put(vp);
        if(!error)return 0;
        /* A previously published owned copy can be stale: it may come from a
         * different build (UUID/size) or carry a native-cache rewrite we cannot
         * certify. Keeping it would wedge compatibility publication on every
         * boot, so replace the owned file instead of failing forever. */
        int removed=b2l_apps_remove(final,ctx);
        if(removed)return removed;
        removed=b2l_apps_remove(staging,ctx);
        if(removed)return removed;
        error=ENOENT;
    }
    if(error!=ENOENT)return error;
    for(unsigned attempt=0;attempt<2;attempt++){
        error=vnode_open(staging,FREAD|FWRITE|O_CREAT,mode,VNODE_LOOKUP_NOFOLLOW_ANY,&vp,ctx);
        if(error)return error;
        error=b2l_apps_sync_payload(vp,data,bytes,mode,true,ctx);
        if(!error)break;
        vnode_close(vp,FREAD|FWRITE,ctx);vp=NULL;
        int removed=b2l_apps_remove(staging,ctx);
        if(removed)return removed;
        if(attempt)return error;
    }
    if(!error){
        struct nameidata nd;NDINIT(&nd,CREATE,OP_LINK,LOCKPARENT,UIO_SYSSPACE,CAST_USER_ADDR_T(final),ctx);nd.ni_flag|=NAMEI_NOFOLLOW_ANY;
        error=namei(&nd);
        if(!error){
            if(nd.ni_vp)error=EEXIST;
            else if(!root_type(nd.ni_dvp,VDIR))error=EXDEV;
            else {error=VNOP_LINK(vp,nd.ni_dvp,&nd.ni_cnd,ctx);if(!error)error=VNOP_FSYNC(nd.ni_dvp,MNT_WAIT,ctx);}
            lookup_done(&nd);
        }
    }
    int close_error=vnode_close(vp,FREAD|FWRITE,ctx);return error?error:close_error;
}
#include "b2l_profile_cache.h"
static void b2l_userspace_before_init(void)
{
    int requested=0;PE_parse_boot_argn("b2l_userspace_bootstrap",&requested,sizeof(requested));
    if(requested!=1)return;
    if(!b2l_amfi_active()){printf("B2L APPS: bootstrap skipped: AMFI inactive\n");return;}
    if(!rootvnode||vfs_isrdonly(vnode_mount(rootvnode))||strcmp(vfs_statfs(vnode_mount(rootvnode))->f_fstypename,"hfs")){
        printf("B2L APPS: bootstrap skipped: root is not writable HFS\n");return;
    }
    vfs_context_t ctx=vfs_context_current();int error=b2l_profile_cache_refresh(ctx);
    printf("B2L PROFILE: MobileGestalt refresh error=%d\n",error);
    error=0;
    static const char *const dirs[]={"/usr/local","/usr/local/libexec","/System/Library/LaunchDaemons","/private/var/db"};
    for(unsigned i=0;i<sizeof(dirs)/sizeof(dirs[0]);i++){
        if((error=b2l_apps_directory(dirs[i],ctx))){printf("B2L APPS: directory %s error=%d\n",dirs[i],error);goto out;}
    }
#define B2L_PUBLISH(label,final,staging,data,mode) do{ \
    error=b2l_apps_publish(final,staging,data,sizeof(data),mode,ctx); \
    if(error){printf("B2L APPS: publish %s error=%d\n",label,error);goto out;} \
}while(0)
    B2L_PUBLISH("executable","/usr/local/libexec/b2l-system-app-bootstrap-v3","/usr/local/libexec/.b2l-system-app-bootstrap-v3",b2l_apps_executable,0755);
    B2L_PUBLISH("hfs-compat","/usr/local/libexec/b2l-hfs-install-compat-v1.dylib","/usr/local/libexec/.b2l-hfs-install-compat-v1.dylib",b2l_hfs_compat_dylib,0755);
    B2L_PUBLISH("graphics-compat","/usr/local/libexec/b2l-graphics-compat-v1.dylib","/usr/local/libexec/.b2l-graphics-compat-v1.dylib",b2l_graphics_compat_dylib,0755);
    /* Rendering compatibility is usable now; migration publication below is
     * independent and must not disable it if the launch cache is rejected. */
    b2l_compat_ready=1;
    printf("B2L APPS: runtime compatibility libraries published\n");
    B2L_PUBLISH("job","/System/Library/LaunchDaemons/dev.b2l.system-app-bootstrap-v3.plist","/System/Library/LaunchDaemons/.b2l-system-app-bootstrap-v3.plist",b2l_apps_job,0644);
    B2L_PUBLISH("launch-cache","/private/var/db/b2l-launchd-bootstrap-v4.plist","/private/var/db/.b2l-launchd-bootstrap-v4",b2l_apps_launch_cache,0644);
#undef B2L_PUBLISH
    if(!error)error=VFS_SYNC(vnode_mount(rootvnode),MNT_WAIT,ctx);
    if(!error)b2l_apps_cache_ready=1;
out:printf("B2L APPS: native migration bootstrap publication error=%d\n",error);
}
