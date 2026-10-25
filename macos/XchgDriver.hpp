#pragma once
#include <DriverKit/DriverKit.h>
#include "../include/xchg_proto.h"

class XchgDriver : public IOService {
    OSDeclareDefaultStructors(XchgDriver)
public:
    struct Region {
        uint8_t*  buf{nullptr};
        uint32_t  regionSize{0};
        uint32_t  payloadSize{0};
        uint32_t  readPos[2]{0,0};
    };

    virtual bool init() override;
    virtual void free() override;
    virtual kern_return_t Start(IOService* provider) override;
    virtual kern_return_t Stop(IOService* provider) override;

    IOLock*      lock_{nullptr};
    xchg_config_t cfg_{};
    Region       ch0to1_{};
    Region       ch1to0_{};
    bool         opened_[2]{false,false};
    uint64_t     epochId_{0};

    kern_return_t allocRegion(Region& r, uint32_t size);
    void publishSize(Region& r);
    void resetEpoch();
};
