#include "XchgUserClient.hpp"
#include <libkern/libkern.h>
OSDefineMetaClassAndStructors(XchgUserClient, IOUserClient)

bool XchgUserClient::init() { return super::init(); }
void XchgUserClient::free() { super::free(); }

kern_return_t XchgUserClient::Start(IOService* provider) {
    auto kr = super::Start(provider);
    if (kr != kIOReturnSuccess) return kr;
    driver_ = OSDynamicCast(XchgDriver, provider);
    if (!driver_) return kIOReturnBadArgument;
    IOLockLock(driver_->lock_);
    if (!driver_->opened_[0]) { driver_->opened_[0] = true; role_ = 0; }
    else if (!driver_->opened_[1]) { driver_->opened_[1] = true; role_ = 1; }
    else { IOLockUnlock(driver_->lock_); return kIOReturnExclusiveAccess; }
    IOLockUnlock(driver_->lock_);
    return kIOReturnSuccess;
}

kern_return_t XchgUserClient::Stop(IOService* provider) {
    if (driver_) {
        IOLockLock(driver_->lock_);
        driver_->opened_[role_] = false;
        IOLockUnlock(driver_->lock_);
    }
    return super::Stop(provider);
}

kern_return_t XchgUserClient::ExternalMethod(uint64_t selector, IOUserClientMethodArguments* args,
                                             const IOUserClientMethodDispatch* dispatch, OSObject* target,
                                             void* reference) {
    (void)dispatch; (void)target; (void)reference;
    switch (selector) {
        case kXchgMethodSetConfig: return methodSetConfig(args);
        case kXchgMethodGetStatus: return methodGetStatus(args);
        case kXchgMethodReadData:  return methodReadData(args);
        case kXchgMethodWriteData: return methodWriteData(args);
        case kXchgMethodReset:     return methodReset(args);
        default: return kIOReturnUnsupported;
    }
}

kern_return_t XchgUserClient::methodSetConfig(IOUserClientMethodArguments* args) {
    if (!args || args->structureInputSize < sizeof(xchg_config_t)) return kIOReturnBadArgument;
    auto* cfg = (const xchg_config_t*)args->structureInput;
    if (cfg->exponent < XCHG_MIN_EXPONENT || cfg->exponent > XCHG_MAX_EXPONENT) return kIOReturnBadArgument;
    IOLockLock(driver_->lock_);
    auto kr = driver_->allocRegion(driver_->ch0to1_, cfg->send_region_size);
    if (kr == kIOReturnSuccess) kr = driver_->allocRegion(driver_->ch1to0_, cfg->recv_region_size);
    if (kr == kIOReturnSuccess) driver_->cfg_ = *cfg;
    IOLockUnlock(driver_->lock_);
    return kr;
}

kern_return_t XchgUserClient::methodGetStatus(IOUserClientMethodArguments* args) {
    if (!args || args->structureOutputSize < sizeof(xchg_status_t)) return kIOReturnBadArgument;
    auto* st = (xchg_status_t*)args->structureOutput;
    bzero(st, sizeof(*st));
    IOLockLock(driver_->lock_);
    st->mantissa = driver_->cfg_.mantissa;
    st->exponent = driver_->cfg_.exponent;
    st->send_region_size = driver_->cfg_.send_region_size;
    st->recv_region_size = driver_->cfg_.recv_region_size;
    st->tx_payload_size = (role_ == 0) ? driver_->ch0to1_.payloadSize : driver_->ch1to0_.payloadSize;
    st->rx_payload_size = (role_ == 0) ? driver_->ch1to0_.payloadSize : driver_->ch0to1_.payloadSize;
    st->epoch_id = driver_->epochId_;
    IOLockUnlock(driver_->lock_);
    args->structureOutputSize = sizeof(*st);
    return kIOReturnSuccess;
}

kern_return_t XchgUserClient::methodReadData(IOUserClientMethodArguments* args) {
    if (!args || !args->structureOutput) return kIOReturnBadArgument;
    XchgDriver::Region& r = (role_ == 0) ? driver_->ch1to0_ : driver_->ch0to1_;
    IOLockLock(driver_->lock_);
    if (r.payloadSize == 0 || r.readPos[role_] >= r.payloadSize) {
        args->structureOutputSize = 0;
        IOLockUnlock(driver_->lock_);
        return kIOReturnSuccess;
    }
    uint32_t avail = r.payloadSize - r.readPos[role_];
    uint32_t toCopy = (args->structureOutputSize < avail) ? (uint32_t)args->structureOutputSize : avail;
    memcpy(args->structureOutput, r.buf + sizeof(uint32_t) + r.readPos[role_], toCopy);
    r.readPos[role_] += toCopy;
    args->structureOutputSize = toCopy;
    IOLockUnlock(driver_->lock_);
    return kIOReturnSuccess;
}

kern_return_t XchgUserClient::methodWriteData(IOUserClientMethodArguments* args) {
    if (!args || !args->structureInput) return kIOReturnBadArgument;
    XchgDriver::Region& r = (role_ == 0) ? driver_->ch0to1_ : driver_->ch1to0_;
    uint32_t peer = role_ == 0 ? 1 : 0;
    IOLockLock(driver_->lock_);
    uint32_t cap = r.regionSize - sizeof(uint32_t);
    uint32_t toCopy = (args->structureInputSize < cap) ? (uint32_t)args->structureInputSize : cap;
    memcpy(r.buf + sizeof(uint32_t), args->structureInput, toCopy);
    r.payloadSize = toCopy;
    driver_->publishSize(r);
    r.readPos[peer] = 0;
    IOLockUnlock(driver_->lock_);
    return kIOReturnSuccess;
}

kern_return_t XchgUserClient::methodReset(IOUserClientMethodArguments* args) {
    (void)args;
    IOLockLock(driver_->lock_);
    driver_->epochId_ = 0;
    driver_->ch0to1_.payloadSize = 0;
    driver_->ch1to0_.payloadSize = 0;
    driver_->publishSize(driver_->ch0to1_);
    driver_->publishSize(driver_->ch1to0_);
    driver_->resetEpoch();
    IOLockUnlock(driver_->lock_);
    return kIOReturnSuccess;
}
