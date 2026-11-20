/* SPDX-License-Identifier: MIT */
#include "b2l_apfs_omap.h"
static uint32_t r32(const uint8_t *p) {
 return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static uint64_t r64(const uint8_t *p) {
 return (uint64_t)r32(p)|((uint64_t)r32(p+4)<<32);
}
static uint64_t fletcher(const uint8_t *p,size_t n) {
 const uint64_t m=0xffffffffULL; uint64_t a=0,b=0,lo,hi; size_t i;
 for(i=8;i<n;i+=4) { a=(a+r32(p+i))%m; b=(b+a)%m; }
 lo=0xffffffffULL-((a+b)%m); hi=0xffffffffULL-((a+lo)%m);
 return (hi<<32)|lo;
}
int b2l_apfs_decode_omap(const void *data,size_t n,struct b2l_apfs_omap_info *o) {
 const uint8_t *p=(const uint8_t *)data; uint32_t t;
 if(!p||!o||n<88||(n&3)) return B2L_APFS_OMAP_EINVAL;
 t=r32(p+24);
 if((t&0xffffu)!=0x0bu||(t&0xc0000000u)!=0x40000000u)
  return B2L_APFS_OMAP_ETYPE;
 if(r64(p)!=fletcher(p,n)) return B2L_APFS_OMAP_ECHECKSUM;
 o->oid=r64(p+8); o->xid=r64(p+16); o->flags=r32(p+32);
 o->snapshot_count=r32(p+36); o->tree_type=r32(p+40);
 o->snapshot_tree_type=r32(p+44); o->tree_oid=r64(p+48);
 o->snapshot_tree_oid=r64(p+56); o->most_recent_snapshot=r64(p+64);
 if(o->oid==0||o->tree_oid==0||o->tree_type==0)
  return B2L_APFS_OMAP_EBOUNDS;
 return B2L_APFS_OMAP_OK;
}
int b2l_apfs_decode_omap_key(const void *data,size_t n,struct b2l_apfs_omap_key *o) {
 const uint8_t *p=(const uint8_t *)data;
 if(!p||!o||n<16) return B2L_APFS_OMAP_EINVAL;
 o->oid=r64(p); o->xid=r64(p+8);
 if(!o->oid||!o->xid) return B2L_APFS_OMAP_EBOUNDS;
 return B2L_APFS_OMAP_OK;
}
int b2l_apfs_decode_omap_value(const void *data,size_t n,struct b2l_apfs_omap_value *o) {
 const uint8_t *p=(const uint8_t *)data;
 if(!p||!o||n<16) return B2L_APFS_OMAP_EINVAL;
 o->flags=r32(p); o->size=r32(p+4); o->paddr=r64(p+8);
 if(o->flags&1u) return B2L_APFS_OMAP_EDELETED;
 if(!o->size||!o->paddr) return B2L_APFS_OMAP_EBOUNDS;
 return B2L_APFS_OMAP_OK;
}
