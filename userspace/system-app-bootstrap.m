// SPDX-License-Identifier: BSD-2-Clause
// Explicit B2L development bootstrap for the shipped 23G90 migration plugin.
// Never overrides its capability, eligibility, consent or trust-cache decisions.
#import <Foundation/Foundation.h>
#import <objc/message.h>
#include <dlfcn.h>
#include <sys/sysctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <string.h>

static BOOL bootOption(const char *args,const char *option) {
    size_t n=strlen(option);
    for(const char *p=args;*p;) {
        while(*p==' ')p++;
        const char *end=strchr(p,' ');if(!end)end=p+strlen(p);
        if((size_t)(end-p)==n && !memcmp(p,option,n))return YES;
        p=end;
    }
    return NO;
}
static id call0(id object, NSString *name) {
    SEL selector=NSSelectorFromString(name);
    return [object respondsToSelector:selector] ? ((id(*)(id,SEL))objc_msgSend)(object,selector) : nil;
}
static NSUInteger installedCount(void) {
    NSArray *roots=@[@"/private/var/containers/Bundle/Application",@"/private/var/mobile/Containers/Bundle/Application"];
    NSUInteger count=0;NSFileManager *fm=NSFileManager.defaultManager;
    for(NSString *root in roots) {
        for(NSString *container in [fm contentsOfDirectoryAtPath:root error:NULL]) {
            NSString *base=[root stringByAppendingPathComponent:container];
            for(NSString *name in [fm contentsOfDirectoryAtPath:base error:NULL]) {
                if(![name.pathExtension isEqualToString:@"app"])continue;
                NSDictionary *info=[NSDictionary dictionaryWithContentsOfFile:[[base stringByAppendingPathComponent:name] stringByAppendingPathComponent:@"Info.plist"]];
                if([info[@"CFBundleIdentifier"] isKindOfClass:NSString.class] && [info[@"CFBundleExecutable"] isKindOfClass:NSString.class])count++;
            }
        }
    }
    return count;
}
// The iOS 26 Control Center gallery is seeded from this plain plist rather
// than the legacy per-module preferences. The shipped seed only lists six
// modules, so the Appearance (light/dark) control never appears. Add it once,
// keeping every existing entry and clearing a stale disable. The file belongs
// to mobile; this helper runs as root before user apps start.
static void ensureAppearanceModule(void) {
    NSString *path=@"/private/var/mobile/Library/ControlCenter/ModuleConfiguration.plist";
    NSString *identifier=@"com.apple.control-center.AppearanceModule";
    NSDictionary *existing=[NSDictionary dictionaryWithContentsOfFile:path];
    NSMutableDictionary *config=existing?[existing mutableCopy]:[NSMutableDictionary dictionary];
    NSArray *identifiers=[config[@"module-identifiers"] isKindOfClass:NSArray.class]?config[@"module-identifiers"]:@[];
    NSArray *disabled=[config[@"disabled-module-identifiers"] isKindOfClass:NSArray.class]?config[@"disabled-module-identifiers"]:@[];
    BOOL listed=[identifiers containsObject:identifier];
    BOOL blocked=[disabled containsObject:identifier];
    if(listed && !blocked)return;
    NSMutableArray *next=[identifiers mutableCopy];
    if(!listed)[next addObject:identifier];
    config[@"module-identifiers"]=next;
    if(blocked){NSMutableArray *allowed=[disabled mutableCopy];[allowed removeObject:identifier];config[@"disabled-module-identifiers"]=allowed;}
    if(!config[@"version"])config[@"version"]=@3;
    if(!config[@"userenabled-fixed-module-identifiers"])config[@"userenabled-fixed-module-identifiers"]=@[];
    NSFileManager *fm=NSFileManager.defaultManager;
    if(![fm createDirectoryAtPath:[path stringByDeletingLastPathComponent] withIntermediateDirectories:YES attributes:nil error:NULL] ||
       ![config writeToFile:path atomically:YES]) {
        NSLog(@"B2L APPS: cannot publish Control Center Appearance configuration");
        return;
    }
    if(chown(path.fileSystemRepresentation,501,501) || chmod(path.fileSystemRepresentation,0644))
        NSLog(@"B2L APPS: Control Center configuration ownership not set");
    NSLog(@"B2L APPS: Appearance control exposed in Control Center (listed=%d blocked=%d)",(int)listed,(int)blocked);
}
int main(void) {
 @autoreleasepool {
    char args[4096]={0};size_t argsLength=sizeof(args);
    if(sysctlbyname("kern.bootargs",args,&argsLength,NULL,0) || argsLength>sizeof(args) || !memchr(args,0,sizeof(args)) ||
       !bootOption(args,"b2l_userspace_bootstrap=1") || !bootOption(args,"b2l_arm64e_compat=1") || !bootOption(args,"cs_enforcement_disable=1")) {
        NSLog(@"B2L APPS: explicit development bootstrap mode absent");return 63;
    }
    char model[64]={0};size_t length=sizeof(model);
    NSDictionary *version=[NSDictionary dictionaryWithContentsOfFile:@"/System/Library/CoreServices/SystemVersion.plist"];
    NSString *build=version[@"ProductBuildVersion"];
    if(getuid()!=0 || sysctlbyname("hw.machine",model,&length,NULL,0) || strcmp(model,"iPhone17,2") || !([build isEqual:@"23G90"] || [build isEqual:@"25F71"])) {
        NSLog(@"B2L APPS: unsupported owner/model/build; native migration not invoked");return 64;
    }
    ensureAppearanceModule();
    // Serialize accidental duplicate launches without blocking another startup.
    int lock=open("/private/var/run/b2l-system-app-bootstrap-v1.lock",O_RDWR|O_CREAT|O_NOFOLLOW,0600);
    if(lock<0 || flock(lock,LOCK_EX|LOCK_NB))return 65;
    NSString *marker=@"/private/var/db/b2l-system-app-bootstrap-hfs-v1.plist";
    NSDictionary *previous=[NSDictionary dictionaryWithContentsOfFile:marker];
    NSUInteger before=installedCount();
    if([previous[@"build"] isEqual:build] && [previous[@"complete"] boolValue] && before>0) {
        NSLog(@"B2L APPS: existing completed native migration preserved; installed=%lu",(unsigned long)before);return 0;
    }
    // A failed trial remains explicit. Do not retry migration on every reboot.
    if([previous[@"attempted"] boolValue]) {
        NSLog(@"B2L APPS: previous attempt incomplete; preserving receipt for inspection");return 66;
    }
    // Native client setup can wait for its Mach services. Bound the entire
    // one-shot process as well; launchd does not respawn it on failure.
    alarm(600);
    if(!dlopen("/System/Library/PrivateFrameworks/DataMigration.framework/DataMigration",RTLD_NOW|RTLD_GLOBAL)) {
        NSLog(@"B2L APPS: DataMigration unavailable: %s",dlerror());return 67;
    }
    NSBundle *bundle=[NSBundle bundleWithPath:@"/System/Library/DataClassMigrators/SystemAppMigrator.migrator"];
    NSError *error=nil;
    if(![bundle loadAndReturnError:&error]) {NSLog(@"B2L APPS: plugin load failed: %@",error);return 68;}
    Class principal=bundle.principalClass;
    if(!principal || ![NSStringFromClass(principal) isEqual:@"MISystemAppMigrator"])return 69;
    id migrator=[[principal alloc] init];
    SEL perform=NSSelectorFromString(@"performMigration");
    NSMethodSignature *signature=[migrator methodSignatureForSelector:perform];
    if(!migrator || !signature || signature.numberOfArguments!=2 || signature.methodReturnLength!=sizeof(BOOL)) {
        NSLog(@"B2L APPS: migration ABI mismatch");return 70;
    }
    id items=call0(migrator,@"itemsToInstall");
    if(![items isKindOfClass:NSDictionary.class] || [items count]==0) {
        NSLog(@"B2L APPS: plugin found no staged apps; installed=%lu",(unsigned long)before);return 71;
    }
    NSDictionary *attempt=@{@"build":build,@"attempted":@YES,@"complete":@NO,@"installedBefore":@(before),@"stagedCandidates":@([items count])};
    if(![attempt writeToFile:marker atomically:YES]) {NSLog(@"B2L APPS: cannot persist attempt receipt");return 72;}
    NSLog(@"B2L APPS: invoking shipped SystemAppMigrator; staged=%lu installedBefore=%lu",(unsigned long)[items count],(unsigned long)before);
    BOOL result=NO;
    @try {result=((BOOL(*)(id,SEL))objc_msgSend)(migrator,perform);}
    @catch(NSException *exception) {NSLog(@"B2L APPS: migration exception %@: %@",exception.name,exception.reason);return 73;}
    NSUInteger after=installedCount();
    // Success requires actual installed bundles, not only a native return value.
    BOOL complete=result && after>before;
    NSMutableDictionary *receipt=[attempt mutableCopy];receipt[@"nativeResult"]=@(result);receipt[@"installedAfter"]=@(after);receipt[@"complete"]=@(complete);
    BOOL saved=[receipt writeToFile:marker atomically:YES];
    NSLog(@"B2L APPS: nativeResult=%d installedAfter=%lu completed=%d receiptSaved=%d",result,(unsigned long)after,complete,saved);
    return complete && saved?0:74;
 }
}
