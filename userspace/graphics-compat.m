// SPDX-License-Identifier: BSD-2-Clause
#import "b2l-runtime-mode.h"
#import <CoreGraphics/CoreGraphics.h>
#import <objc/runtime.h>
#include <stdatomic.h>
#include <string.h>
static BOOL enabled;
static _Atomic unsigned repairs;
static BOOL rgba8(size_t bits,size_t pixelBits,CGBitmapInfo info) {
 unsigned alpha=info&kCGBitmapAlphaInfoMask,order=info&kCGBitmapByteOrderMask;
 return bits==8&&pixelBits==32&&!(info&kCGBitmapFloatComponents)&&
 (alpha==kCGImageAlphaPremultipliedFirst||alpha==kCGImageAlphaPremultipliedLast||
 alpha==kCGImageAlphaFirst||alpha==kCGImageAlphaLast||alpha==kCGImageAlphaNoneSkipFirst||alpha==kCGImageAlphaNoneSkipLast)&&
 (order==kCGBitmapByteOrderDefault||order==kCGBitmapByteOrder32Little||order==kCGBitmapByteOrder32Big)&&!(info&~(kCGBitmapAlphaInfoMask|kCGBitmapByteOrderMask));
}
static CGColorSpaceRef defaultRGB(void) {
 CGColorSpaceRef space=CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
 if(!space)space=CGColorSpaceCreateDeviceRGB();return space;
}
static void repaired(const char *kind,BOOL success) {
 unsigned n=atomic_fetch_add(&repairs,1);
 if(n<16)NSLog(@"B2L CG: unprofiled RGBA8 %s sRGB result=%d",kind,success);
}
static CGImageRef image(size_t w,size_t h,size_t bits,size_t pixelBits,size_t row,CGColorSpaceRef space,CGBitmapInfo info,CGDataProviderRef provider,const CGFloat *decode,bool interpolate,CGColorRenderingIntent intent) {
 CGColorSpaceRef fallback=NULL;
 if(enabled&&!space&&w&&h&&w<=SIZE_MAX/4&&row>=w*4&&rgba8(bits,pixelBits,info))space=fallback=defaultRGB();
 CGImageRef result=CGImageCreate(w,h,bits,pixelBits,row,space,info,provider,decode,interpolate,intent);
 if(fallback){repaired("image",result!=NULL);CGColorSpaceRelease(fallback);}return result;
}
static CGContextRef bitmap(void *data,size_t w,size_t h,size_t bits,size_t row,CGColorSpaceRef space,uint32_t info,CGBitmapContextReleaseDataCallback release,void *user) {
 CGColorSpaceRef fallback=NULL;
 if(enabled&&!space&&w&&h&&w<=SIZE_MAX/4&&(!row||row>=w*4)&&rgba8(bits,32,info))space=fallback=defaultRGB();
 CGContextRef result=CGBitmapContextCreateWithData(data,w,h,bits,row,space,info,release,user);
 if(fallback){repaired("bitmap",result!=NULL);CGColorSpaceRelease(fallback);}return result;
}
static CGContextRef bitmapSimple(void *data,size_t w,size_t h,size_t bits,size_t row,CGColorSpaceRef space,uint32_t info) {
 return bitmap(data,w,h,bits,row,space,info,NULL,NULL);
}
#define INTERPOSE(replacement,original) __attribute__((used,section("__DATA,__interpose"))) static const struct {const void *newp,*oldp;} pair_##replacement={(const void *)&replacement,(const void *)&original};

/* UIKit chooses a status-bar visual provider by matching the screen's native
 * pixel width against known iPhone profiles. A 720 px render width matches
 * none, so UIKit installs _UIStatusBarVisualProvider_Fallback, which draws the
 * internal "rdar:45025538" placeholder into the status bar. The logical width
 * here is 360 pt (720 px / display-scale 2), the same width as the 1080 px
 * @3x profile, so use that provider instead. */
typedef Class (*B2LProviderIMP)(id,SEL,id,id);
static B2LProviderIMP b2l_provider_original;
static Class b2l_status_bar_provider(id self,SEL _cmd,id screen,id info) {
    Class provider=b2l_provider_original?(Class)b2l_provider_original(self,_cmd,screen,info):Nil;
    const char *name=provider!=Nil?class_getName(provider):"";
    if(provider!=Nil&&strstr(name,"Fallback")==NULL)return provider;
    Class replacement=(Class)objc_getClass("_UIStatusBarVisualProvider_Split1080");
    if(replacement==Nil)replacement=(Class)objc_getClass("_UIStatusBarVisualProvider_LegacyPhone");
    if(replacement!=Nil&&replacement!=provider)NSLog(@"B2L SB: status bar provider %s -> %s",name,class_getName(replacement));
    return replacement!=Nil?replacement:provider;
}
static void install_status_bar_provider(void) {
    Class split=(Class)objc_getClass("_UIStatusBarVisualProvider_Split");
    if(split==Nil)return;
    SEL selector=NSSelectorFromString(@"visualProviderSubclassForScreen:visualProviderInfo:");
    Method method=class_getClassMethod(split,selector);
    if(method==NULL)return;
    b2l_provider_original=(B2LProviderIMP)method_getImplementation(method);
    method_setImplementation(method,(IMP)b2l_status_bar_provider);
    NSLog(@"B2L SB: status bar provider override installed");
}

#ifndef B2L_COMPAT_TEST
INTERPOSE(image,CGImageCreate)
INTERPOSE(bitmap,CGBitmapContextCreateWithData)
INTERPOSE(bitmapSimple,CGBitmapContextCreate)
/* Unambiguous load beacon: the kernel's "AMFI" policy stand-in prints this
 call into the boot log, so a missing beacon proves the dylib never ran (dyld
 pruned DYLD_INSERT_LIBRARIES or skipped __interpose) instead of leaving the
 failure inside os_log where the boot ring cannot see it. */
extern int __mac_syscall(const char *policy,int call,void *arg);
__attribute__((constructor)) static void install(void) {@autoreleasepool{(void)__mac_syscall("AMFI",0x5b,NULL);enabled=b2lRuntimeMode();if(enabled){install_status_bar_provider();NSLog(@"B2L CG: bounded unprofiled RGBA8 software compatibility loaded");}}}

#endif
