/* SPDX-License-Identifier: BSD-2-Clause
 * MAX77705 power path.
 *
 * Charger at USI hsi2c_45 (0x15D60000), 7-bit address 0x66; fuel gauge at
 * 0x36. Register layout and field scaling come from the Samsung
 * max77705 charger/fuel-gauge drivers; the transfer sequence is the proven
 * USI-I2C-v2 routine from B2LInput (same IP, different instance).
 *
 * The phone has no Apple PMU, so booting iOS does not restore whatever
 * charger configuration Android's bootloader/driver left behind. This
 * service selects buck+charger mode, raises the USB input limit to a value
 * the charger can regulate, kicks the charger watchdog and reports gauge
 * progress to the persistent kernel log. Reads are validated before any
 * register is written, so an unclocked USI block can never be programmed
 * blind.
 */
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <kern/clock.h>

#define B2L_CHG_ADDR       0x66
#define B2L_FG_ADDR        0x36

#define CHG_INT_OK         0xB2
#define CHG_DETAILS_00     0xB3
#define CHG_CNFG_00        0xB7
#define CHG_CNFG_02        0xB9
#define CHG_CNFG_06        0xBD
#define CHG_CNFG_09        0xC0

#define FG_STATUS          0x00
#define FG_SOC             0x06
#define FG_VCELL           0x09
#define FG_CURRENT         0x0A

/* CHG_CNFG_00 MODE[3:0]: 5/6/7 keep the buck on and the charger enabled. */
#define B2L_CHG_MODE_BUCK_CHG 0x05
/* CHG_CNFG_09 bit 7 enables charging; bits[6:0] are CHGIN_LIM in 25 mA steps
 * (value+1 steps). */
#define B2L_CHG_ILIM_STEP_MA 25u
#define B2L_CHG_EN           0x80u
#define B2L_CHG_ILIM_USB_MA  1500u
#define B2L_CHG_ILIM_SAFE_MA 500u
/* Initial fast-charge current. The charger's own input regulation and the
 * thermal guard below keep this from being sustained when it is not safe. */
#define B2L_CHG_CC_MA        1500u
/* CHG_CNFG_06 WDTCLR[1:0] = 1 kicks the charger watchdog. */
#define B2L_CHG_WDT_KICK     0x01u
/* CHG_CNFG_06 DIS_AICL: when set the input current is not reduced
 * automatically, so never ask for more than a plain USB port provides. */
#define B2L_CHG_DIS_AICL     0x10u

class B2LPower : public IOService {
    OSDeclareDefaultStructors(B2LPower);
    IOMemoryDescriptor *i2cDesc;
    IOMemoryMap *i2cMap;
    IOWorkLoop *loop;
    IOTimerEventSource *timer;
    volatile uint8_t *i2c;
    bool chargerReady, gaugeReady;
    unsigned timeoutUS, transferFailures, ticks, probeAttempts;
    unsigned socTenths, vcellMv;
    int currentMa, tempTenths;
    uint16_t fgStatus, fgTemp;
    static uint64_t uptimeUS(){
        uint64_t ns=0;
        absolutetime_to_nanoseconds(mach_absolute_time(),&ns);
        return ns/1000;
    }
    uint32_t r(unsigned off){
        uint32_t v=*(volatile uint32_t*)(i2c+off);
        __asm__ volatile("dmb oshld":::"memory");
        return v;
    }
    void w(unsigned off,uint32_t v){
        *(volatile uint32_t*)(i2c+off)=v;
        __asm__ volatile("dsb oshst":::"memory");
    }
    void quiesce(){
        w(0x20,0);w(0x24,r(0x24));
        w(0x44,r(0x44)&~(1u<<31));
        w(0,r(0)&~0xc0u);
    }
    void resetController(){
        w(0,r(0)|(1u<<31));w(0,r(0)&~(1u<<31));
        w(0,8);w(8,0x00ffffff);
        w(0x40,r(0x40)|(1u<<31));
    }
    bool message(uint8_t addr,uint8_t *buf,unsigned n,bool read,bool stop);
    bool readRegs(uint8_t addr,uint8_t reg,uint8_t *buf,unsigned n);
    bool readRegsCombined(uint8_t addr,uint8_t reg,uint8_t *buf,unsigned n);
    bool writeReg(uint8_t addr,uint8_t reg,uint8_t value);
    bool readWord(uint8_t addr,uint8_t reg,uint16_t *value);
    void probe();
    void configure(uint8_t cnfg00,uint8_t cnfg02,uint8_t cnfg06,uint8_t cnfg09);
    void setChargeCurrent(unsigned ma);
    void thermalGuard();
    void sample();
    void publish();
    void kickWatchdog();
    static void tick(OSObject *owner,IOTimerEventSource *sender);
public:
    bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
    void stop(IOService *provider) APPLE_KEXT_OVERRIDE;
    void free() APPLE_KEXT_OVERRIDE;
};
OSDefineMetaClassAndStructors(B2LPower,IOService);

/* Same bounded USI-I2C-v2 sequence as B2LInput::message(). The slave
 * address lives in I2C_ADDR[19:10] of register 0x70; unlike the touch
 * driver we have two slaves, so the field must be cleared before it is set.
 * stop=false leaves the bus held so a repeated start can follow. */
bool B2LPower::message(uint8_t addr,uint8_t *buf,unsigned n,bool read,bool stop){
    if(!buf||!n||n>16)return false;
    const unsigned budgetUS=timeoutUS;
    const uint64_t begin=uptimeUS();
    w(0x48,r(0x48)&~(1u<<31));
    unsigned level=n<8?n:8;
    w(4,3u|(level<<4)|(level<<16));
    w(0,(r(0)&~0xc0u)|8u|(read?0x40u:0x80u));
    w(0x40,r(0x40)|(1u<<31));
    /* I2C_ADDR[19:10] holds the 7-bit slave address (i2c-exynos5.c clears
     * the whole 0x3ff field). Clear it completely so no leftover address
     * bits from the bootloader survive between the two slaves. */
    w(0x70,(r(0x70)&~0x000ffc00u)|(((uint32_t)addr&0x7fu)<<10));
    w(0x24,r(0x24));w(0x20,0x80);
    w(0x44,(r(0x44)&~0x8003ffffu)|0x80000000u|(stop?0x20000u:0)|(read?0x10000u:0)|n);
    unsigned at=0;
    while(uptimeUS()-begin<budgetUS){
        unsigned before=at;
        while(at<n && uptimeUS()-begin<budgetUS){
            uint32_t fifo=r(0x30);
            if(read){
                if(fifo&(1u<<24))break;
                buf[at++]=(uint8_t)r(0x38);
            }else{
                if((fifo&0x7f)>=16)break;
                w(0x34,buf[at++]);
            }
        }
        if(uptimeUS()-begin>=budgetUS)break;
        if(at==n && (r(0x24)&0x80) && (read||(r(0x30)&0x100))){
            quiesce();return true;
        }
        if(at==before || at==n)IODelay(1);
    }
    if(transferFailures++<8)
        IOLog("B2L POWER: i2c timeout addr=0x%02x read=%d bytes=%u done=%u ctrl=0x%x int=0x%x cnf=0x%x\n",
            addr,read,n,at,r(0),r(0x24),r(0x2c));
    resetController();quiesce();
    return false;
}

/* Register read: latched register address write followed by the data
 * transfer, exactly like B2LInput::command(). The MAX PMICs also accept the
 * combined form (register address without STOP, repeated start into the
 * read); probe() tries that when the split form returns a dead-bus pattern. */
bool B2LPower::readRegs(uint8_t addr,uint8_t reg,uint8_t *buf,unsigned n){
    resetController();
    if(!message(addr,&reg,1,false,true))return false;
    return message(addr,buf,n,true,true);
}
bool B2LPower::readRegsCombined(uint8_t addr,uint8_t reg,uint8_t *buf,unsigned n){
    resetController();
    if(!message(addr,&reg,1,false,false))return false;
    return message(addr,buf,n,true,true);
}
bool B2LPower::writeReg(uint8_t addr,uint8_t reg,uint8_t value){
    uint8_t pair[2]={reg,value};
    resetController();
    return message(addr,pair,2,false,true);
}
bool B2LPower::readWord(uint8_t addr,uint8_t reg,uint16_t *value){
    uint8_t d[2]={0,0};
    if(!readRegs(addr,reg,d,2))return false;
    *value=(uint16_t)(d[0]|(d[1]<<8));
    return true;
}

void B2LPower::probe(){
    ++probeAttempts;
    uint8_t cnfg00=0,cnfg02=0,cnfg06=0,cnfg09=0,intok=0,details=0;
    bool ok00=false,ok02=false,ok06=false,ok09=false,okb2=false,okb3=false;
    bool combined=false;
    for(unsigned pass=0;pass<2;++pass){
        combined=(pass==1);
        cnfg00=cnfg02=cnfg06=cnfg09=intok=details=0;
        if(combined){
            ok00=readRegsCombined(B2L_CHG_ADDR,CHG_CNFG_00,&cnfg00,1);
            ok02=readRegsCombined(B2L_CHG_ADDR,CHG_CNFG_02,&cnfg02,1);
            ok06=readRegsCombined(B2L_CHG_ADDR,CHG_CNFG_06,&cnfg06,1);
            ok09=readRegsCombined(B2L_CHG_ADDR,CHG_CNFG_09,&cnfg09,1);
            okb2=readRegsCombined(B2L_CHG_ADDR,CHG_INT_OK,&intok,1);
            okb3=readRegsCombined(B2L_CHG_ADDR,CHG_DETAILS_00,&details,1);
        }else{
            ok00=readRegs(B2L_CHG_ADDR,CHG_CNFG_00,&cnfg00,1);
            ok02=readRegs(B2L_CHG_ADDR,CHG_CNFG_02,&cnfg02,1);
            ok06=readRegs(B2L_CHG_ADDR,CHG_CNFG_06,&cnfg06,1);
            ok09=readRegs(B2L_CHG_ADDR,CHG_CNFG_09,&cnfg09,1);
            okb2=readRegs(B2L_CHG_ADDR,CHG_INT_OK,&intok,1);
            okb3=readRegs(B2L_CHG_ADDR,CHG_DETAILS_00,&details,1);
        }
        chargerReady=ok00&&ok02&&ok06&&ok09;
        /* A dead bus reads back one repeated value. Never program a charger
         * whose configuration registers cannot be distinguished. */
        if(chargerReady && cnfg00==cnfg02 && cnfg02==cnfg06 && cnfg06==cnfg09 &&
           (cnfg00==0x00||cnfg00==0xFF))
            chargerReady=false;
        if(chargerReady)break;
    }
    IOLog("B2L POWER PROBE: attempt=%u path=%s ok=%d%d%d%d%d%d cnfg00=0x%02x cnfg02=0x%02x cnfg06=0x%02x cnfg09=0x%02x int_ok=0x%02x details=0x%02x\n",
        probeAttempts,combined?"combined":"split",ok00,ok02,ok06,ok09,okb2,okb3,
        cnfg00,cnfg02,cnfg06,cnfg09,intok,details);
    if(chargerReady)configure(cnfg00,cnfg02,cnfg06,cnfg09);
    sample();
}

void B2LPower::configure(uint8_t cnfg00,uint8_t cnfg02,uint8_t cnfg06,uint8_t cnfg09){
    unsigned mode=cnfg00&0x0f;
    unsigned ccMa=(cnfg02&0x3f)<=2?100u:(unsigned)(cnfg02&0x3f)*50u;
    if(mode<B2L_CHG_MODE_BUCK_CHG||mode>0x07){
        /* Preserve WDTEN and every reserved bit above the mode field. */
        uint8_t want=(uint8_t)((cnfg00&0xf0)|B2L_CHG_MODE_BUCK_CHG);
        if(writeReg(B2L_CHG_ADDR,CHG_CNFG_00,want)){
            IOLog("B2L POWER: charger mode 0x%x -> 0x%x\n",mode,want&0x0f);
            mode=want&0x0f;
        }
    }
    unsigned desired=(cnfg06&B2L_CHG_DIS_AICL)?B2L_CHG_ILIM_SAFE_MA:B2L_CHG_ILIM_USB_MA;
    unsigned setting=desired/B2L_CHG_ILIM_STEP_MA-1u;
    unsigned currentSetting=cnfg09&0x7f;
    if(!(cnfg09&B2L_CHG_EN)||currentSetting<setting){
        /* CHGIN_LIM[6:0] is the limit and bit 7 is CHG_EN. Only ever raise
         * the limit; AICL may have reduced it below what we would ask for. */
        uint8_t want=(uint8_t)(B2L_CHG_EN|(currentSetting<setting?setting:currentSetting));
        if(writeReg(B2L_CHG_ADDR,CHG_CNFG_09,want)){
            IOLog("B2L POWER: CHGIN_LIM %u mA -> %u mA%s\n",
                (currentSetting+1)*B2L_CHG_ILIM_STEP_MA,
                ((want&0x7f)+1)*B2L_CHG_ILIM_STEP_MA,
                (cnfg09&B2L_CHG_EN)?"":" (CHG_EN reasserted)");
            currentSetting=want&0x7f;
        }
    }
    kickWatchdog();
    /* Android/LK usually leave the charger at a conservative USB charge
     * current. Raise it once, then let the thermal guard in thermalGuard()
     * pull it back if the pack warms up. */
    if(ccMa<1000){setChargeCurrent(B2L_CHG_CC_MA);ccMa=B2L_CHG_CC_MA;}
    IOLog("B2L POWER: ready mode=0x%x chgin_lim=%u mA chg_en=%d charge_current=%u mA aicl=%s wdt=%s\n",
        mode,(currentSetting+1)*B2L_CHG_ILIM_STEP_MA,(cnfg09&B2L_CHG_EN)?1:0,ccMa,
        (cnfg06&B2L_CHG_DIS_AICL)?"off":"on",
        (cnfg00&0x10)?"on":"off");
}

/* CHG_CNFG_02[5:0] is the fast-charge current in 50 mA steps (0..2 = 100 mA);
 * [7:6] belong to OTG and must be preserved. */
void B2LPower::setChargeCurrent(unsigned ma){
    uint8_t cnfg02=0;
    if(!readRegs(B2L_CHG_ADDR,CHG_CNFG_02,&cnfg02,1))return;
    unsigned reg=ma/50u;
    if(reg<3)reg=3;
    if(reg>0x3fu)reg=0x3fu;
    if((cnfg02&0x3f)==reg)return;
    unsigned was=(cnfg02&0x3f)<=2?100u:(unsigned)(cnfg02&0x3f)*50u;
    if(writeReg(B2L_CHG_ADDR,CHG_CNFG_02,(uint8_t)((cnfg02&0xc0)|reg)))
        IOLog("B2L POWER: charge current %u mA -> %u mA\n",was,reg*50u);
}

/* There is no Apple battery driver doing JEITA, so the throttle lives here:
 * above 45 C drop to a trickle-safe current, above 55 C to the minimum. */
void B2LPower::thermalGuard(){
    if(tempTenths>550)setChargeCurrent(100);
    else if(tempTenths>450)setChargeCurrent(500);
}

void B2LPower::kickWatchdog(){
    uint8_t cnfg06=0;
    if(!readRegs(B2L_CHG_ADDR,CHG_CNFG_06,&cnfg06,1))return;
    writeReg(B2L_CHG_ADDR,CHG_CNFG_06,
        (uint8_t)((cnfg06&~0x03u)|B2L_CHG_WDT_KICK));
}

/* TEMPERATURE_REG is 16-bit fixed point: high byte whole degrees Celsius,
 * low byte 39/10 m°C units. Return whole deci-degrees, matching the
 * Samsung driver's sign handling. */
static int b2l_fg_temp_tenths(uint16_t raw){
    uint8_t lo=(uint8_t)(raw&0xff),hi=(uint8_t)(raw>>8);
    int t;
    if(hi&0x80){
        t=((~hi)&0xff)+1;
        t*=(-1000);
        t-=(-(int)lo)*39/10;
    }else{
        t=(hi&0x7f);
        t*=1000;
        t+=lo*39/10;
    }
    return t/100;
}

void B2LPower::sample(){
    uint16_t soc=0,vcell=0,current=0,temp=0,status=0;
    bool okSoc=readWord(B2L_FG_ADDR,FG_SOC,&soc);
    bool okCell=readWord(B2L_FG_ADDR,FG_VCELL,&vcell);
    bool okCurrent=readWord(B2L_FG_ADDR,FG_CURRENT,&current);
    readWord(B2L_FG_ADDR,FG_TEMP,&temp);
    readWord(B2L_FG_ADDR,FG_STATUS,&status);
    gaugeReady=okSoc&&okCell&&okCurrent;
    if(!gaugeReady)return;
    /* Scale factors: SOC 1/256 %, VCELL 78.125 uV, CURRENT 156.25 uA with
     * the 5 mOhm sense resistor (fg_resistor=2). */
    unsigned socT=(unsigned)(((uint32_t)soc*10u)/256u);
    if(socT>1000)socT=1000;
    unsigned mv=(unsigned)(((uint32_t)vcell*5u)/64u);
    int ma=((int)(int16_t)current)*5/16;
    /* Reject implausible frames instead of reporting garbage as battery
     * state: a present pack is between ~2.5 V and 4.5 V. */
    if(mv<2500||mv>4500){gaugeReady=false;return;}
    socTenths=socT;vcellMv=mv;currentMa=ma;
    tempTenths=b2l_fg_temp_tenths(temp);
    fgStatus=status;fgTemp=temp;
}

void B2LPower::publish(){
    if(!gaugeReady)return;
    setProperty("B2LBatterySOCPercent",(unsigned long long)(socTenths/10),32);
    setProperty("B2LBatteryVoltage_mV",(unsigned long long)vcellMv,32);
    setProperty("B2LBatteryCurrent_mA",(long long)currentMa,32);
    setProperty("B2LChargerReady",chargerReady);
    setProperty("B2LGaugeReady",gaugeReady);
}

void B2LPower::tick(OSObject *owner,IOTimerEventSource *sender){
    B2LPower *s=(B2LPower*)owner;
    ++s->ticks;
    if(!s->chargerReady&&!s->gaugeReady){
        if(s->ticks%12==0&&s->probeAttempts<10)s->probe();
        return;
    }
    /* The charger watchdog expires after roughly 30 s; 10 s keeps it fed
     * without touching the bus on every tick. */
    if(s->ticks%2==0)s->kickWatchdog();
    s->sample();
    s->thermalGuard();
    s->publish();
    if(s->ticks%12==0){
        int tt=s->tempTenths,at=tt<0?-tt:tt;
        IOLog("B2L POWER: soc=%u.%u%% vcell=%u mV current=%d mA temp=%s%d.%d C status=0x%04x chg_ok=%d fg_ok=%d\n",
            s->socTenths/10,s->socTenths%10,s->vcellMv,s->currentMa,
            tt<0?"-":"",at/10,at%10,s->fgStatus,
            s->chargerReady,s->gaugeReady);
    }
}

bool B2LPower::start(IOService *provider){
    if(!IOService::start(provider))return false;
    i2cDesc=IOMemoryDescriptor::withPhysicalAddress(0x15D60000,4096,kIODirectionInOut);
    if(i2cDesc)i2cMap=i2cDesc->map(kIOMapInhibitCache);
    if(!i2cMap)return false;
    i2c=(volatile uint8_t*)i2cMap->getVirtualAddress();
    timeoutUS=50000;
    probe();
    loop=IOWorkLoop::workLoop();
    if(!loop)return false;
    timer=IOTimerEventSource::timerEventSource(this,&B2LPower::tick);
    if(!timer||loop->addEventSource(timer)!=kIOReturnSuccess)return false;
    timer->setTimeoutMS(5000);
    registerService();
    publish();
    IOLog("B2L POWER: MAX77705 service registered charger=%d gauge=%d\n",
        chargerReady,gaugeReady);
    return true;
}

void B2LPower::stop(IOService *provider){
    if(timer){timer->cancelTimeout();timer->disable();}
    if(loop&&timer)loop->removeEventSource(timer);
    if(timer){timer->release();timer=nullptr;}
    if(i2cMap){i2cMap->release();i2cMap=nullptr;}
    if(i2cDesc){i2cDesc->release();i2cDesc=nullptr;}
    IOService::stop(provider);
}
void B2LPower::free(){
    if(timer){timer->release();timer=nullptr;}
    if(i2cMap){i2cMap->release();i2cMap=nullptr;}
    if(i2cDesc){i2cDesc->release();i2cDesc=nullptr;}
    IOService::free();
}
