/* SPDX-License-Identifier: BSD-2-Clause
 * Minimal non-Apple device-tree platform for the Exynos 9820 first boot.
 */
#include <pexpert/arm64/board_config.h>
#if defined(B2L)
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <libkern/c++/OSData.h>
#include <pexpert/pexpert.h>
#include <kern/clock.h>
#include <mach/mach_time.h>
#include "b2l_rtc.h"

class B2LPlatformExpert : public IODTPlatformExpert {
	OSDeclareDefaultStructors(B2LPlatformExpert);
public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void processTopLevel(IORegistryEntry *root) APPLE_KEXT_OVERRIDE;
	const char *deleteList(void) APPLE_KEXT_OVERRIDE;
	const char *excludeList(void) APPLE_KEXT_OVERRIDE;
	/* XNU seeds the calendar from this once IOKit is up, so SpringBoard and
	 * every userspace process see the real date/time instead of 1970. */
	long getGMTTimeOfDay(void) APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(B2LPlatformExpert, IODTPlatformExpert);

static IOMemoryMap *b2l_rtc_map;
static uint64_t b2l_rtc_epoch;
static uint64_t b2l_rtc_anchor;
static bool b2l_rtc_checked;

static uint64_t
b2l_rtc_read(void)
{
	if (b2l_rtc_map == nullptr) {
		IOMemoryDescriptor *desc = IOMemoryDescriptor::withPhysicalAddress(
		    B2L_RTC_BASE, B2L_RTC_BYTES, kIODirectionIn);
		if (desc != nullptr) {
			b2l_rtc_map = desc->map(kIOMapInhibitCache);
			desc->release();
		}
	}
	if (b2l_rtc_map == nullptr) {
		IOLog("B2L RTC: register mapping failed; calendar left unset\n");
		return 0;
	}
	volatile uint8_t *b = (volatile uint8_t *)b2l_rtc_map->getVirtualAddress();
	if (b == nullptr) {
		IOLog("B2L RTC: no virtual address; calendar left unset\n");
		return 0;
	}
	uint8_t raw[7] = {
		b[B2L_RTC_REG_SEC], b[B2L_RTC_REG_MIN], b[B2L_RTC_REG_HOUR],
		b[B2L_RTC_REG_DATE], b[B2L_RTC_REG_MON], b[B2L_RTC_REG_YEAR],
		b[B2L_RTC_REG_YEAR + 1]
	};
	uint8_t con = b[B2L_RTC_REG_CON];
	uint64_t epoch = b2l_rtc_decode(raw);
	IOLog("B2L RTC: rtcen=%d raw=%02x %02x %02x %02x %02x %02x%02x epoch=%llu\n",
	    (con & B2L_RTC_CON_EN) ? 1 : 0, raw[0], raw[1], raw[2], raw[3], raw[4],
	    raw[6], raw[5], epoch);
	if (epoch == 0) {
		IOLog("B2L RTC: no plausible date; calendar will stay at the platform default\n");
	}
	return epoch;
}

long
B2LPlatformExpert::getGMTTimeOfDay(void)
{
	int seed = 0;
	uint64_t now, ns = 0;

	if (!b2l_rtc_checked) {
		b2l_rtc_checked = true;
		/* Explicit boot argument wins, for tests and for a dead RTC. */
		if (PE_parse_boot_argn("b2l_utc", &seed, sizeof(seed)) && seed > 0) {
			b2l_rtc_epoch = (uint32_t)seed;
			IOLog("B2L RTC: using b2l_utc=%d from the boot arguments\n", seed);
		} else {
			b2l_rtc_epoch = b2l_rtc_read();
		}
		if (b2l_rtc_epoch != 0) b2l_rtc_anchor = mach_absolute_time();
	}
	if (b2l_rtc_epoch == 0) return 0;
	now = mach_absolute_time();
	if (now > b2l_rtc_anchor) {
		absolutetime_to_nanoseconds(now - b2l_rtc_anchor, &ns);
	}
	return (long)(b2l_rtc_epoch + ns / 1000000000ULL);
}

bool
B2LPlatformExpert::start(IOService *provider)
{
	IOLog("B2L: starting device-tree platform expert\n");
	if (!IODTPlatformExpert::start(provider)) {
		return false;
	}
    IORegistryEntry *chosen=IORegistryEntry::fromPath("/chosen",gIODTPlane);
    OSData *wifi=chosen?OSDynamicCast(OSData,chosen->getProperty("mac-address-wifi0")):nullptr;
    IOLog("B2L: WiFi MAC metadata %s (address redacted)\n",wifi&&wifi->getLength()==6?"present, 6 bytes":"absent");
    if(chosen)chosen->release();
    uint32_t sleepDisabled=0;
    if(PE_get_default("sleep-disabled",&sleepDisabled,sizeof(sleepDisabled)) && sleepDisabled)
        IOLog("B2L PM: system sleep disabled until CPU suspend/resume support is available\n");
	registerService();
	IOLog("B2L: platform expert registered; software product iPhone17,2, Samsung B2L hardware\n");
	return true;
}

const char *B2LPlatformExpert::deleteList(void) { return "()"; }
const char *B2LPlatformExpert::excludeList(void)
{
	return "('chosen', 'defaults', 'cpus', 'options')";
}

void
B2LPlatformExpert::processTopLevel(IORegistryEntry *root)
{
	/* There is no Apple NVRAM /options service.  Publish the real boot CPU
	 * without fabricating a firmware NVRAM device. */
	IORegistryEntry *cpus = root->childFromPath("cpus", gIODTPlane);
	if (cpus != nullptr) {
		createNubs(this, IODTFindMatchingEntries(cpus, kIODTExclusive, nullptr));
		cpus->release();
	}
	createNubs(this, IODTFindMatchingEntries(root, kIODTExclusive, excludeList()));
    IORegistryEntry *armio=root->childFromPath("arm-io",gIODTPlane);
    if(armio){createNubs(this,IODTFindMatchingEntries(armio,kIODTExclusive,"('gic')"));armio->release();}

}
#endif
