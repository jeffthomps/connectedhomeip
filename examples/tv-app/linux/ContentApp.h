/*
 *
 *    Copyright (c) 2021 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 * @brief Manages Content Apps
 */

#pragma once

#include <app-common/zap-generated/enums.h>
#include <app/util/attribute-storage.h>

#include <stdbool.h>
#include <stdint.h>

#include <functional>

namespace chip {
namespace AppPlatform {

class DLL_EXPORT ApplicationBasic
{
public:
    virtual ~ApplicationBasic() = default;

    virtual const char * GetVendorName()                                               = 0;
    virtual uint16_t GetVendorId()                                                     = 0;
    virtual const char * GetApplicationName()                                          = 0;
    virtual uint16_t GetProductId()                                                    = 0;
    virtual EmberAfApplicationBasicStatus GetApplicationStatus()                       = 0;
    virtual const char * GetApplicationVersion()                                       = 0;
    virtual void SetApplicationStatus(EmberAfApplicationBasicStatus applicationStatus) = 0;

    EmberAfStatus HandleReadAttribute(chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength);
    EmberAfStatus HandleWriteAttribute(chip::AttributeId attributeId, uint8_t * buffer);
};

class DLL_EXPORT AccountLogin
{
public:
    virtual ~AccountLogin() = default;

    virtual void SetSetupPIN(uint32_t setupPIN)                       = 0;
    virtual uint32_t GetSetupPIN(const char * tempAccountId)          = 0;
    virtual bool Login(const char * tempAccountId, uint32_t setupPin) = 0;

    EmberAfStatus HandleReadAttribute(chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength);
    EmberAfStatus HandleWriteAttribute(chip::AttributeId attributeId, uint8_t * buffer);
};

class DLL_EXPORT ContentApp
{
public:
    virtual ~ContentApp() = default;

    inline void SetEndpointId(chip::EndpointId id) { mEndpointId = id; };
    inline chip::EndpointId GetEndpointId() { return mEndpointId; };

    virtual ApplicationBasic * GetApplicationBasic() = 0;
    virtual AccountLogin * GetAccountLogin()         = 0;

    EmberAfStatus HandleReadAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer, uint16_t maxReadLength);
    EmberAfStatus HandleWriteAttribute(ClusterId clusterId, chip::AttributeId attributeId, uint8_t * buffer);

protected:
    chip::EndpointId mEndpointId = 0;
};

} // namespace AppPlatform
} // namespace chip
