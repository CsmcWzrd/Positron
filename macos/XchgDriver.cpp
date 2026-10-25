#include "XchgDriver.hpp"
#include <stdlib.h>
OSDefineMetaClassAndStructors(XchgDriver, IOService)

bool XchgDriver::init() {
    if (!super::init()) return false;
    cfg_.mantissa = 1; cfg_.exponent = 0; cfg_.send_region_size = 4096; cfg_.recv_region_size = 4096;
    lock_ = IOLockAlloc();
    return lock_ != nullptr;
}

void XchgDriver::free() {
    if (ch0to1_.buf) IOFree(ch0to1_.buf, ch0to1_.regionSize);
    if (ch1to0_.buf) IOFree(ch1to0_.buf, ch1to0_.regionSize);
    if (lock_) IOLockFree(lock_);
    super::free();
}

kern_return_t XchgDriver::Start(IOService* provider) {
    kern_return_t kr = super::Start(provider);
    if (kr != kIOReturnSuccess) return kr;
    kr = allocRegion(ch0to1_, cfg_.send_region_size);
    if (kr != kIOReturnSuccess) return kr;
    kr = allocRegion(ch1to0_, cfg_.recv_region_size);
    if (kr != kIOReturnSuccess) return kr;
    RegisterService();
    return kIOReturnSuccess;
}

kern_return_t XchgDriver::Stop(IOService* provider) {
    return super::Stop(provider);
}

kern_return_t XchgDriver::allocRegion(Region& r, uint32_t size) {
    if (size < 4) return kIOReturnBadArgument;
    uint8_t* p = (uint8_t*)IOMalloc(size);
    if (!p) return kIOReturnNoMemory;
    bzero(p, size);
    if (r.buf) IOFree(r.buf, r.regionSize);
    r.buf = p; r.regionSize = size; r.payloadSize = 0; r.readPos[0]=r.readPos[1]=0;
    publishSize(r);
    return kIOReturnSuccess;
}

void XchgDriver::publishSize(Region& r) {
    if (r.buf && r.regionSize >= 4) *(uint32_t*)r.buf = r.payloadSize;
}

void XchgDriver::resetEpoch() {
    epochId_++;
    ch0to1_.readPos[0]=ch0to1_.readPos[1]=0;
    ch1to0_.readPos[0]=ch1to0_.readPos[1]=0;
}
