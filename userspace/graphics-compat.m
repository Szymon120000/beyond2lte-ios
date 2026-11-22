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

#ifndef B2L_COMPAT_TEST
/* UIKit chooses a status-bar visual provider by matching the screen against
 * known iPhone profiles. No profile has a 720 px native width, so UIKit's
 * lookup fails and it installs the Fallback provider, which paints the
 * internal "rdar:45025538" placeholder into the status bar. A 720 px render at
 * display-scale 2 is 360 pt wide, exactly the Split1080 profile (1080 px @3x),
 * so substitute that provider. SpringBoard carries its own STUIStatusBar*
 * subclasses which override the lookup; swizzling only UIKit's _UI copies
 * leaves SpringBoard unaffected, so patch every class that implements the
 * selector itself. */
typedef Class (*B2LProviderIMP)(id,SEL,id,id);
static struct {Class cls;B2LProviderIMP original;} b2l_providers[8];
static unsigned b2l_provider_count;
static Class b2l_status_bar_class(Class from) {
    BOOL springboard=strncmp(class_getName(from),"STUI",4)==0;
    Class replacement=springboard?(Class)objc_getClass("STUIStatusBarVisualProvider_Split1080")
                                  :(Class)objc_getClass("_UIStatusBarVisualProvider_Split1080");
    if(replacement==Nil)replacement=springboard?(Class)objc_getClass("STUIStatusBarVisualProvider_Split")
                                                :(Class)objc_getClass("_UIStatusBarVisualProvider_Split");
    return replacement;
}
static Class b2l_status_bar_provider(id self,SEL _cmd,id screen,id info) {
    Class cls=(Class)self;
    B2LProviderIMP original=NULL;
    for(unsigned i=0;i<b2l_provider_count;i++)if(b2l_providers[i].cls==cls)original=b2l_providers[i].original;
    Class provider=original!=NULL?(Class)original(self,_cmd,screen,info):Nil;
    const char *name=provider!=Nil?class_getName(provider):"";
    /* 1080 px @3x is the 360 pt profile, which is exactly this panel's logical
     * width, so keep it if the lookup already found it. */
    if(provider!=Nil&&strstr(name,"Split1080")!=NULL)return provider;
    Class replacement=b2l_status_bar_class(cls);
    if(replacement!=Nil&&replacement!=provider)NSLog(@"B2L SB: +[%s] provider %s -> %s",class_getName(cls),name,class_getName(replacement));
    return replacement!=Nil?replacement:provider;
}
static void install_status_bar_provider(void) {
    static const char *names[]={"STUIStatusBarVisualProvider_Split","STUIStatusBarVisualProvider_Phone",
     "STUIStatusBarVisualProvider_iOS","STUIStatusBarVisualProvider_DynamicSplit",
     "_UIStatusBarVisualProvider_Split","_UIStatusBarVisualProvider_Phone",
     "_UIStatusBarVisualProvider_iOS","_UIStatusBarVisualProvider_DynamicSplit"};
    SEL selector=NSSelectorFromString(@"visualProviderSubclassForScreen:visualProviderInfo:");
    for(unsigned n=0;n<sizeof(names)/sizeof(names[0]);n++){
        Class cls=(Class)objc_getClass(names[n]);
        Class meta=cls!=Nil?object_getClass(cls):Nil;
        unsigned count=0,direct=0;
        Method *methods=meta!=Nil?class_copyMethodList(meta,&count):NULL;
        for(unsigned i=0;i<count;i++)if(method_getName(methods[i])==selector)direct=1;
        free(methods);
        Method method=direct?class_getClassMethod(cls,selector):NULL;
        if(method==NULL||b2l_provider_count>=sizeof(b2l_providers)/sizeof(b2l_providers[0]))continue;
        IMP current=method_getImplementation(method);
        if(current==(IMP)b2l_status_bar_provider)continue;
        b2l_providers[b2l_provider_count].cls=cls;
        b2l_providers[b2l_provider_count].original=(B2LProviderIMP)current;
        b2l_provider_count++;
        method_setImplementation(method,(IMP)b2l_status_bar_provider);
        NSLog(@"B2L SB: status bar provider override installed on %s",names[n]);
    }
}
/* The placeholder can also reach the status bar as breadcrumb text, which is
 * unrelated to provider selection. Drop only titles that advertise a radar
 * number so real breadcrumbs keep working. */
typedef id (*B2LTitleIMP)(id,SEL);
typedef void (*B2LTextIMP)(id,SEL,NSString *);
static B2LTitleIMP b2l_breadcrumb_original;
static B2LTextIMP b2l_destination_original;
static BOOL b2l_radar_text(id text) {
    return [text isKindOfClass:[NSString class]]&&
     [(NSString *)text rangeOfString:@"rdar" options:NSCaseInsensitiveSearch].location!=NSNotFound;
}
static id b2l_breadcrumb_title(id self,SEL _cmd) {
    id title=b2l_breadcrumb_original!=NULL?b2l_breadcrumb_original(self,_cmd):nil;
    return b2l_radar_text(title)?nil:title;
}
static void b2l_destination_text(id self,SEL _cmd,NSString *text) {
    if(b2l_destination_original!=NULL)b2l_destination_original(self,_cmd,b2l_radar_text(text)?nil:text);
}
static void swizzle_instance(const char *name,const char *selector,IMP replacement,IMP *original) {
    Class cls=(Class)objc_getClass(name);
    if(cls==Nil)return;
    Method method=class_getInstanceMethod(cls,NSSelectorFromString([NSString stringWithUTF8String:selector]));
    if(method==NULL)return;
    *original=method_getImplementation(method);
    method_setImplementation(method,replacement);
    NSLog(@"B2L SB: suppressed radar placeholder on -[%s %s]",name,selector);
}
static void install_breadcrumb_filter(void) {
    swizzle_instance("SBDeviceApplicationSceneStatusBarBreadcrumbProvider","breadcrumbTitle",(IMP)b2l_breadcrumb_title,(IMP *)&b2l_breadcrumb_original);
    swizzle_instance("UIStatusBarBreadcrumbItemView","setDestinationText:",(IMP)b2l_destination_text,(IMP *)&b2l_destination_original);
}

INTERPOSE(image,CGImageCreate)
INTERPOSE(bitmap,CGBitmapContextCreateWithData)
INTERPOSE(bitmapSimple,CGBitmapContextCreate)
/* Unambiguous load beacon: the kernel's "AMFI" policy stand-in prints this
 call into the boot log, so a missing beacon proves the dylib never ran (dyld
 pruned DYLD_INSERT_LIBRARIES or skipped __interpose) instead of leaving the
 failure inside os_log where the boot ring cannot see it. */
extern int __mac_syscall(const char *policy,int call,void *arg);
__attribute__((constructor)) static void install(void) {@autoreleasepool{(void)__mac_syscall("AMFI",0x5b,NULL);enabled=b2lRuntimeMode();if(enabled){install_status_bar_provider();install_breadcrumb_filter();(void)__mac_syscall("AMFI",0x5b,NULL);NSLog(@"B2L CG: bounded unprofiled RGBA8 software compatibility loaded");}}}

#endif
