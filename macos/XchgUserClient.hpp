#pragma once
#include <DriverKit/DriverKit.h>
#include "XchgDriver.hpp"

enum {
    kXchgMethodSetConfig = 0,
    kXchgMethodGetStatus = 1,
    kXchgMethodReadData  = 2,
    kXchgMethodWriteData = 3,
    kXchgMethodReset     = 4,
};

class XchgUserClient : public IOUserClient {
    OSDeclareDefaultStructors(XchgUserClient)
public:
    virtual bool init() override;
    virtual void free() override;
    virtual kern_return_t Start(IOService* provider) override;
    virtual kern_return_t Stop(IOService* provider) override;
    virtual kern_return_t ExternalMethod(uint64_t selector, IOUserClientMethodArguments* args,
                                         const IOUserClientMethodDispatch* dispatch, OSObject* target,
                                         void* reference) override;
private:
    XchgDriver* driver_{nullptr};
    uint32_t role_{0};
    kern_return_t methodSetConfig(IOUserClientMethodArguments* args);
    kern_return_t methodGetStatus(IOUserClientMethodArguments* args);
    kern_return_t methodReadData(IOUserClientMethodArguments* args);
    kern_return_t methodWriteData(IOUserClientMethodArguments* args);
    kern_return_t methodReset(IOUserClientMethodArguments* args);
};
