/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_BLIT_H
#define B2L_BLIT_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/* The source and destination extents are real mapped allocations. Never derive
 * memory bounds solely from userspace width/height. No YUV fallback. */
static inline int b2l_blit(void *dst,uint64_t dbytes,uint32_t dw,uint32_t dh,uint32_t ds,
    const void *src,uint64_t sbytes,uint32_t sw,uint32_t sh,uint32_t ss,uint32_t soff,
    int32_t sx,int32_t sy,int32_t dx,int32_t dy,int32_t width,int32_t height)
{
    if(!dst||!src||!dw||!dh||!sw||!sh||dw>16384||dh>16384||sw>16384||sh>16384||
       ds<dw*4||ss<sw*4||ds>65536||ss>65536||sx<0||sy<0||dx<0||dy<0||
       (uint32_t)sx>=sw||(uint32_t)sy>=sh||(uint32_t)dx>=dw||(uint32_t)dy>=dh||
       width<=0||height<=0)return 0;
    uint32_t w=(uint32_t)width,h=(uint32_t)height;
    if(w>sw-(uint32_t)sx)w=sw-(uint32_t)sx;
    if(w>dw-(uint32_t)dx)w=dw-(uint32_t)dx;
    if(h>sh-(uint32_t)sy)h=sh-(uint32_t)sy;
    if(h>dh-(uint32_t)dy)h=dh-(uint32_t)dy;
    uint64_t end_s=(uint64_t)soff+((uint64_t)sy+h-1)*ss+((uint64_t)sx+w)*4;
    uint64_t end_d=((uint64_t)dy+h-1)*ds+((uint64_t)dx+w)*4;
    if(end_s>sbytes||end_d>dbytes)return 0;
    /* memmove tolerates the default scanout surface being re-presented. */
    if(sx==0&&dx==0&&(uint64_t)w*4==ds&&ds==ss){
        memmove((uint8_t*)dst+(uint64_t)dy*ds,
                (const uint8_t*)src+soff+(uint64_t)sy*ss,(size_t)h*ds);
        return 1;
    }
    for(uint32_t y=0;y<h;++y)
        memmove((uint8_t*)dst+((uint64_t)dy+y)*ds+(uint64_t)dx*4,
                (const uint8_t*)src+soff+((uint64_t)sy+y)*ss+(uint64_t)sx*4,(size_t)w*4);
    return 1;
}

/* Four-byte iOSurface formats, named by the logical channel order of the
 * 32-bit word. Stored little-endian the channel order in memory differs from
 * the panel's BGRA scanout, so each one needs its own byte permutation. */
#define B2L_BLIT_FMT_BGRA 0x42475241u
#define B2L_BLIT_FMT_ARGB 0x41524742u
#define B2L_BLIT_FMT_ABGR 0x41424752u
#define B2L_BLIT_FMT_RGBA 0x52474241u

/* 0 = already BGRA, 1/2/3 = permutation, -1 = format not handled. */
static inline int b2l_blit_swizzle(uint32_t fmt)
{
    switch(fmt){
    case B2L_BLIT_FMT_BGRA:return 0;
    case B2L_BLIT_FMT_ARGB:return 1;
    case B2L_BLIT_FMT_ABGR:return 2;
    case B2L_BLIT_FMT_RGBA:return 3;
    default:return -1;
    }
}

static inline uint32_t b2l_blit_reorder(uint32_t v,int swz)
{
    switch(swz){
    case 1:return (v>>24)|(v<<24)|((v>>8)&0x0000ff00u)|((v<<8)&0x00ff0000u);
    case 2:return (v>>8)|(v<<24);
    case 3:return (v&0xff00ff00u)|((v&0x000000ffu)<<16)|((v>>16)&0x000000ffu);
    default:return v;
    }
}

/* Layer present: copies the source sub-rectangle (sx,sy,swr,shr) into the
 * destination rectangle (dx,dy,width,height), scaling with nearest-neighbour
 * when the two differ and permuting the channel order when the surface is not
 * BGRA. Same fail-closed bounds discipline as b2l_blit(): every extent is
 * validated against the mapped source and the physical scanout before the
 * first byte is read or written. */
static inline int b2l_blit_layer(void *dst,uint64_t dbytes,uint32_t dw,uint32_t dh,uint32_t ds,
    const void *src,uint64_t sbytes,uint32_t sw,uint32_t sh,uint32_t ss,uint32_t soff,
    int32_t sx,int32_t sy,int32_t swr,int32_t shr,
    int32_t dx,int32_t dy,int32_t width,int32_t height,int swz)
{
    if(!dst||!src||!dw||!dh||!sw||!sh||dw>16384||dh>16384||sw>16384||sh>16384||
       ds<dw*4||ss<sw*4||ds>65536||ss>65536||sx<0||sy<0||dx<0||dy<0||
       swr<-1||shr<-1||swz<0||swz>3||
       (uint32_t)sx>=sw||(uint32_t)sy>=sh||(uint32_t)dx>=dw||(uint32_t)dy>=dh||
       width<=0||height<=0)return 0;
    uint32_t w0=(uint32_t)width,h0=(uint32_t)height,w=w0,h=h0;
    /* An absent source rectangle means "the whole visible destination". */
    uint32_t rw=swr>0?(uint32_t)swr:w,rx=(uint32_t)sx;
    uint32_t rh=shr>0?(uint32_t)shr:h,ry=(uint32_t)sy;
    /* A source rectangle outside the surface is a broken record: refuse it
     * rather than silently resampling a clipped area. */
    if(!rw||!rh||rw>sw-rx||rh>sh-ry)return 0;
    if(w>dw-(uint32_t)dx)w=dw-(uint32_t)dx;
    if(h>dh-(uint32_t)dy)h=dh-(uint32_t)dy;
    if(w0>16384u||h0>16384u)return 0;
    uint64_t end_s=(uint64_t)soff+((uint64_t)ry+rh-1)*ss+((uint64_t)rx+rw)*4;
    uint64_t end_d=((uint64_t)dy+h-1)*ds+((uint64_t)dx+w)*4;
    if(end_s>sbytes||end_d>dbytes)return 0;

    const uint8_t *sbase=(const uint8_t*)src+(uint64_t)soff;
    uint8_t *dbase=(uint8_t*)dst+(uint64_t)dy*ds+(uint64_t)dx*4;

    if(rw==w0&&rh==h0){
        /* Unscaled: memmove tolerates re-presenting the default surface. */
        if(!swz){
            for(uint32_t y=0;y<h;++y)
                memmove(dbase+(uint64_t)y*ds,sbase+(uint64_t)(ry+y)*ss+(uint64_t)rx*4,(size_t)w*4);
        }else{
            for(uint32_t y=0;y<h;++y){
                const uint8_t *srow=sbase+(uint64_t)(ry+y)*ss+(uint64_t)rx*4;
                uint8_t *drow=dbase+(uint64_t)y*ds;
                for(uint32_t x=0;x<w;++x){
                    uint32_t v;
                    memcpy(&v,srow+(uint64_t)x*4,4);
                    v=b2l_blit_reorder(v,swz);
                    memcpy(drow+(uint64_t)x*4,&v,4);
                }
            }
        }
        return 1;
    }

    /* Nearest-neighbour scale. 16.16 stepping keeps division out of the loop;
     * overflow is impossible because both factors are <= 16384. */
    uint64_t stepx=((uint64_t)rw<<16)/w0,stepy=((uint64_t)rh<<16)/h0;
    uint64_t syf=0;
    for(uint32_t y=0;y<h;++y){
        const uint8_t *srow=sbase+(uint64_t)(ry+(uint32_t)(syf>>16))*ss;
        uint8_t *drow=dbase+(uint64_t)y*ds;
        uint64_t sxf=0;
        for(uint32_t x=0;x<w;++x){
            uint32_t v;
            memcpy(&v,srow+(uint64_t)(rx+(uint32_t)(sxf>>16))*4,4);
            if(swz)v=b2l_blit_reorder(v,swz);
            memcpy(drow+(uint64_t)x*4,&v,4);
            sxf+=stepx;
        }
        syf+=stepy;
    }
    return 1;
}
#endif
