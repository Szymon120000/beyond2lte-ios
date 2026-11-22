/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef B2L_BLIT_H
#define B2L_BLIT_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/* The source and destination extents are real mapped allocations. Never derive
 * memory bounds solely from userspace width/height. No resize or YUV fallback. */
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

/* Nearest-neighbour upscales used by the CPU scanout bridge. The destination
 * rectangle is expressed in render coordinates and is mapped into the panel;
 * each panel pixel reads one source pixel. num/den is the panel/render ratio
 * (4/3 for 1080->1440, 2/1 for 720->1440). */
static inline int b2l_blit_scale_common(void *dst,uint64_t dbytes,uint32_t dw,uint32_t dh,uint32_t ds,
    const void *src,uint64_t sbytes,uint32_t sw,uint32_t sh,uint32_t ss,uint32_t soff,
    int32_t sx,int32_t sy,int32_t dx,int32_t dy,int32_t width,int32_t height,
    uint32_t num,uint32_t den)
{
    if(!dst||!src||!dw||!dh||!sw||!sh||dw>16384||dh>16384||sw>16384||sh>16384||
       ds<dw*4||ss<sw*4||ds>65536||ss>65536||num<den||den==0||
       sx<0||sy<0||dx<0||dy<0||width<=0||height<=0||
       (uint32_t)sx>=sw||(uint32_t)sy>=sh||(uint32_t)dx>=dw||(uint32_t)dy>=dh)return 0;
    uint64_t end_s=(uint64_t)soff+((uint64_t)sy+height-1)*ss+((uint64_t)sx+width)*4;
    if(end_s>sbytes)return 0;
    uint64_t pdx=(uint64_t)dx*num/den, pdy=(uint64_t)dy*num/den;
    uint64_t px2=((uint64_t)dx+(uint64_t)width)*num/den;
    uint64_t py2=((uint64_t)dy+(uint64_t)height)*num/den;
    if(px2>dw)px2=dw;
    if(py2>dh)py2=dh;
    if(pdx>=px2||pdy>=py2)return 0;
    if((uint64_t)pdy*ds+px2*4>dbytes)return 0;
    uint32_t sxend=(uint32_t)sx+(uint32_t)width;
    uint32_t syend=(uint32_t)sy+(uint32_t)height;
    if(sxend>sw)sxend=sw;
    if(syend>sh)syend=sh;
    uint32_t npx=(uint32_t)(px2-pdx);
    uint32_t sxi=(uint32_t)sx, syi=(uint32_t)sy;
    uint32_t accy=0;
    for(uint64_t py=pdy;py<py2;++py){
        if(syi>=syend)break;
        const uint32_t *srow=(const uint32_t *)((const uint8_t*)src+soff+(uint64_t)syi*ss);
        uint32_t *drow=(uint32_t*)((uint8_t*)dst+py*ds)+pdx;
        uint32_t sxx=sxi, accx=0;
        for(uint32_t i=0;i<npx;++i){
            if(sxx>=sxend)break;
            drow[i]=srow[sxx];
            accx+=den;
            if(accx>=num){accx-=num;++sxx;}
        }
        accy+=den;
        if(accy>=num){accy-=num;++syi;}
    }
    return 1;
}
static inline int b2l_blit_43(void *dst,uint64_t dbytes,uint32_t dw,uint32_t dh,uint32_t ds,
    const void *src,uint64_t sbytes,uint32_t sw,uint32_t sh,uint32_t ss,uint32_t soff,
    int32_t sx,int32_t sy,int32_t dx,int32_t dy,int32_t width,int32_t height)
{
    return b2l_blit_scale_common(dst,dbytes,dw,dh,ds,src,sbytes,sw,sh,ss,soff,
        sx,sy,dx,dy,width,height,4,3);
}
/* 2:1 is the common case (720x1520 -> 1440x3040). Each source pixel becomes
 * a horizontal pair of destination pixels, so the inner loop runs once per
 * pair instead of once per destination pixel. */
static inline int b2l_blit_21(void *dst,uint64_t dbytes,uint32_t dw,uint32_t dh,uint32_t ds,
    const void *src,uint64_t sbytes,uint32_t sw,uint32_t sh,uint32_t ss,uint32_t soff,
    int32_t sx,int32_t sy,int32_t dx,int32_t dy,int32_t width,int32_t height)
{
    if(!dst||!src||!dw||!dh||!sw||!sh||dw>16384||dh>16384||sw>16384||sh>16384||
       ds<dw*4||ss<sw*4||ds>65536||ss>65536||
       sx<0||sy<0||dx<0||dy<0||width<=0||height<=0||
       (uint32_t)sx>=sw||(uint32_t)sy>=sh||(uint32_t)dx>=dw||(uint32_t)dy>=dh)return 0;
    if(width>(int32_t)(sw-(uint32_t)sx))width=(int32_t)(sw-(uint32_t)sx);
    if(height>(int32_t)(sh-(uint32_t)sy))height=(int32_t)(sh-(uint32_t)sy);
    if(width<=0||height<=0)return 0;
    uint64_t end_s=(uint64_t)soff+((uint64_t)sy+height-1)*ss+((uint64_t)sx+width)*4;
    if(end_s>sbytes)return 0;
    uint64_t pdx=(uint64_t)dx*2,pdy=(uint64_t)dy*2;
    uint64_t px2=((uint64_t)dx+(uint64_t)width)*2;
    uint64_t py2=((uint64_t)dy+(uint64_t)height)*2;
    if(px2>dw)px2=dw;
    if(py2>dh)py2=dh;
    if(pdx>=px2||pdy>=py2)return 0;
    if((uint64_t)pdy*ds+px2*4>dbytes)return 0;
    uint32_t sxend=(uint32_t)sx+(uint32_t)width;
    uint32_t syend=(uint32_t)sy+(uint32_t)height;
    if(sxend>sw)sxend=sw;
    if(syend>sh)syend=sh;
    uint32_t npx=(uint32_t)(px2-pdx);
    uint32_t syi=(uint32_t)sy;
    uint32_t accy=0;
    for(uint64_t py=pdy;py<py2;++py){
        if(syi>=syend)break;
        const uint32_t *srow=(const uint32_t *)((const uint8_t*)src+soff+(uint64_t)syi*ss);
        uint32_t *drow=(uint32_t*)((uint8_t*)dst+py*ds)+pdx;
        uint32_t sxx=(uint32_t)sx;
        uint32_t i=0;
        for(;i+1<npx;i+=2){
            if(sxx>=sxend)break;
            uint32_t v=srow[sxx++];
            drow[i]=v;
            drow[i+1]=v;
        }
        if((npx&1)&&i<npx&&sxx<sxend)drow[i]=srow[sxx];
        accy+=1;
        if(accy>=2){accy-=2;++syi;}
    }
    return 1;
}
#endif
