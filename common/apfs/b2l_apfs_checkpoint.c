/* SPDX-License-Identifier: MIT */
#include "b2l_apfs_checkpoint.h"
#include <string.h>
static uint32_t r32(const uint8_t*p){return(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint64_t r64(const uint8_t*p){return(uint64_t)r32(p)|((uint64_t)r32(p+4)<<32);}
static uint64_t fletcher(const uint8_t*p,size_t n){const uint64_t m=0xffffffffULL;uint64_t a=0,b=0,l,h;size_t i;for(i=8;i<n;i+=4){a=(a+r32(p+i))%m;b=(b+a)%m;}l=0xffffffffULL-((a+b)%m);h=0xffffffffULL-((a+l)%m);return(h<<32)|l;}
static int checksum_ok(const uint8_t*p,size_t n){return n>=32&&r64(p)==fletcher(p,n);}
static int read_block(b2l_apfs_read_fn fn,void*ctx,uint64_t pa,uint32_t bs,uint64_t blocks,uint8_t*buf){uint64_t off;if(pa>=blocks||pa>UINT64_MAX/bs)return-1;off=pa*(uint64_t)bs;if(off>UINT64_MAX-bs)return-1;return fn(ctx,off,buf,bs);}
int b2l_apfs_validate_checkpoint_map(const void*d,size_t bs,uint64_t xid,uint32_t*flags,uint32_t*count){
 const uint8_t*p=(const uint8_t*)d;uint32_t t,n,f;uint64_t i;
 if(!p||bs<4096||!flags||!count)return B2L_APFS_CKPT_EINVAL;
 t=r32(p+24);if((t&0xffffu)!=12u||(t&0xc0000000u)!=0x40000000u||r32(p+28)!=0)return B2L_APFS_CKPT_EFORMAT;
 if(!checksum_ok(p,bs))return B2L_APFS_CKPT_ECHECKSUM;
 if(r64(p+16)!=xid)return B2L_APFS_CKPT_EFORMAT;
 f=r32(p+32);n=r32(p+36);
 if(n>(bs-40u)/40u||n>101u||(f&~B2L_APFS_CHECKPOINT_MAP_LAST)!=0)return B2L_APFS_CKPT_EFORMAT;
 for(i=0;i<n;i++){const uint8_t*e=p+40+(size_t)i*40;uint32_t et=r32(e),sz=r32(e+8);uint64_t oid=r64(e+24),pa=r64(e+32);
  if((et&0xffffu)==0||sz==0||oid==0||pa==0)return B2L_APFS_CKPT_EFORMAT;}
 *flags=f;*count=n;return B2L_APFS_CKPT_OK;
}
int b2l_apfs_discover_checkpoint(b2l_apfs_read_fn fn,void*ctx,uint64_t devsz,const void*nxdata,size_t nxsz,void*scratch,size_t scratchsz,struct b2l_apfs_checkpoint_info*out){
 const uint8_t*nx=(const uint8_t*)nxdata;uint8_t*buf=(uint8_t*)scratch;
 uint32_t bs,ring,idx,used,i,cur_maps=0,cur_entries=0,cur_flags=0;uint64_t blocks,base,cur_xid=0;
 uint64_t best_xid=0,best_pa=0,best_omap=0,best_spaceman=0,best_reaper=0,best_vols[100];uint32_t best_maps=0,best_entries=0,best_flags=0,best_vol_count=0;int complete=0,found=0;
 if(!fn||!nx||!buf||!out||nxsz<4096)return B2L_APFS_CKPT_EINVAL;
 bs=r32(nx+36);blocks=r64(nx+40);if(bs<4096||bs>65536||(bs&(bs-1))||scratchsz<bs||!blocks)return B2L_APFS_CKPT_EINVAL;
 if(blocks>devsz/bs)return B2L_APFS_CKPT_EBOUNDS;
 {uint32_t raw=r32(nx+104);ring=raw&B2L_APFS_DESC_BLOCKS_MASK;if(raw&B2L_APFS_DESC_BLOCKS_FRAGMENTED)return B2L_APFS_CKPT_EUNSUPPORTED;}
 base=r64(nx+112);idx=r32(nx+136);used=r32(nx+140);
 if(!ring||ring>blocks||!used||used>ring||idx>=ring||base>blocks-ring)return B2L_APFS_CKPT_EBOUNDS;
 if(base>UINT64_MAX/bs||base*(uint64_t)bs>devsz||(uint64_t)ring*bs>devsz-base*(uint64_t)bs)return B2L_APFS_CKPT_EBOUNDS;
 for(i=0;i<used;i++){
  uint32_t slot=(idx+i)%ring,typ,fl=0,n=0;uint64_t pa=base+slot,xid;
  if(read_block(fn,ctx,pa,bs,blocks,buf)!=0)return B2L_APFS_CKPT_EIO;
  if(!checksum_ok(buf,bs))continue;
  typ=r32(buf+24)&0xffffu;xid=r64(buf+16);
  if(xid!=cur_xid){cur_xid=xid;cur_maps=0;cur_entries=0;cur_flags=0;complete=0;}
  if(typ==12u){
   if(b2l_apfs_validate_checkpoint_map(buf,bs,xid,&fl,&n)!=0)continue;
   if(complete || cur_entries>UINT32_MAX-n)return B2L_APFS_CKPT_EFORMAT;
   cur_maps++;cur_entries+=n;cur_flags=fl;
   complete=(fl&B2L_APFS_CHECKPOINT_MAP_LAST)!=0;
  }else if(typ==1u&&memcmp(buf+32,"NXSB",4)==0&&complete&&cur_maps&&xid>best_xid){
   uint32_t maxfs=r32(buf+180),j;
   if(r32(buf+36)!=bs||r64(buf+40)!=blocks||memcmp(buf+72,nx+72,16)!=0||maxfs>100)
    continue;
   best_xid=xid;best_pa=pa;best_maps=cur_maps;best_entries=cur_entries;best_flags=cur_flags;
   best_omap=r64(buf+160);best_spaceman=r64(buf+152);best_reaper=r64(buf+168);
   best_vol_count=0;
   for(j=0;j<maxfs;j++){uint64_t vo=r64(buf+184+(size_t)j*8);if(vo)best_vols[best_vol_count++]=vo;}
   found=1;
  }
 }
 if(!found)return B2L_APFS_CKPT_ENOTFOUND;
 memset(out,0,sizeof(*out));out->xid=best_xid;out->nxsb_paddr=best_pa;
 out->descriptor_blocks=used;out->checkpoint_map_blocks=best_maps;out->mapping_entries=best_entries;out->flags=best_flags;
 out->container_omap_paddr=best_omap;out->spaceman_oid=best_spaceman;out->reaper_oid=best_reaper;
 out->volume_count=best_vol_count;memcpy(out->volume_oids,best_vols,best_vol_count*sizeof(uint64_t));
 return B2L_APFS_CKPT_OK;
}
