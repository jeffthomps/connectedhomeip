/*
 *
 *    Copyright (c) 2020 Project CHIP Authors
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

#include <platform/CHIPDeviceLayer.h>
#include <platform/PlatformManager.h>

#include <app/clusters/ota-provider/ota-provider-delegate.h>
#include <app/clusters/ota-provider/ota-provider.h>
#include <app/server/Server.h>
#include <app/util/util.h>
#include <credentials/DeviceAttestationCredsProvider.h>
#include <credentials/examples/DeviceAttestationCredsExample.h>
#include <json/json.h>
#include <lib/core/CHIPError.h>
#include <lib/support/CHIPArgParser.hpp>
#include <lib/support/CHIPMem.h>
#include <lib/support/logging/CHIPLogging.h>
#include <ota-provider-common/BdxOtaSender.h>
#include <ota-provider-common/DefaultUserConsentProvider.h>
#include <ota-provider-common/OTAProviderExample.h>

#include <fstream>
#include <iostream>
#include <unistd.h>

using chip::BitFlags;
using chip::app::Clusters::OTAProviderDelegate;
using chip::ArgParser::HelpOptions;
using chip::ArgParser::OptionDef;
using chip::ArgParser::OptionSet;
using chip::ArgParser::PrintArgError;
using chip::bdx::TransferControlFlags;
using chip::Messaging::ExchangeManager;

// TODO: this should probably be done dynamically
constexpr chip::EndpointId kOtaProviderEndpoint = 0;

constexpr uint16_t kOptionFilepath             = 'f';
constexpr uint16_t kOptionOtaImageList         = 'o';
constexpr uint16_t kOptionQueryImageBehavior   = 'q';
constexpr uint16_t kOptionDelayedActionTimeSec = 'd';
constexpr uint16_t kOptionUserConsent          = 'u';

// Global variables used for passing the CLI arguments to the OTAProviderExample object
static OTAProviderExample::QueryImageBehaviorType gQueryImageBehavior = OTAProviderExample::kRespondWithUnknown;
static uint32_t gDelayedActionTimeSec                                 = 0;
static const char * gOtaFilepath                                      = nullptr;
static const char * gOtaImageListFilepath                             = nullptr;
static chip::ota::UserConsentState gUserConsentState                  = chip::ota::UserConsentState::kUnknown;

// Parses the JSON filepath and extracts DeviceSoftwareVersionModel parameters
static bool ParseJsonFileAndPopulateCandidates(const char * filepath,
                                               std::vector<OTAProviderExample::DeviceSoftwareVersionModel> & candidates)
{
    bool ret = false;
    Json::Value root;
    Json::CharReaderBuilder builder;
    JSONCPP_STRING errs;
    std::ifstream ifs;

    builder["collectComments"] = true; // allow C/C++ type comments in JSON file
    ifs.open(filepath);

    if (!ifs.good())
    {
        ChipLogError(SoftwareUpdate, "Error opening ifstream with file: \"%s\"", filepath);
        return ret;
    }

    if (!parseFromStream(builder, ifs, &root, &errs))
    {
        ChipLogError(SoftwareUpdate, "Error parsing JSON from file: \"%s\"", filepath);
        return ret;
    }

    const Json::Value devSofVerModValue = root["deviceSoftwareVersionModel"];
    if (!devSofVerModValue || !devSofVerModValue.isArray())
    {
        ChipLogError(SoftwareUpdate, "Error: Key deviceSoftwareVersionModel not found or its value is not of type Array");
    }
    else
    {
        for (auto iter : devSofVerModValue)
        {
            OTAProviderExample::DeviceSoftwareVersionModel candidate;
            candidate.vendorId        = static_cast<chip::VendorId>(iter.get("vendorId", 1).asUInt());
            candidate.productId       = static_cast<uint16_t>(iter.get("productId", 1).asUInt());
            candidate.softwareVersion = static_cast<uint32_t>(iter.get("softwareVersion", 10).asUInt64());
            strncpy(candidate.softwareVersionString, iter.get("softwareVersionString", "1.0.0").asCString(),
                    OTAProviderExample::SW_VER_STR_MAX_LEN);
            candidate.cDVersionNumber              = static_cast<uint16_t>(iter.get("cDVersionNumber", 0).asUInt());
            candidate.softwareVersionValid         = iter.get("softwareVersionValid", true).asBool() ? true : false;
            candidate.minApplicableSoftwareVersion = static_cast<uint32_t>(iter.get("minApplicableSoftwareVersion", 0).asUInt64());
            candidate.maxApplicableSoftwareVersion =
                static_cast<uint32_t>(iter.get("maxApplicableSoftwareVersion", 1000).asUInt64());
            strncpy(candidate.otaURL, iter.get("otaURL", "https://test.com").asCString(), OTAProviderExample::OTA_URL_MAX_LEN);
            candidates.push_back(candidate);
            ret = true;
        }
    }
    return ret;
}

bool HandleOptions(const char * aProgram, OptionSet * aOptions, int aIdentifier, const char * aName, const char * aValue)
{
    bool retval = true;
    static bool kOptionFilepathSelected;
    static bool kOptionOtaImageListSelected;

    switch (aIdentifier)
    {
    case kOptionFilepath:
        kOptionFilepathSelected = true;
        if (0 != access(aValue, R_OK))
        {
            PrintArgError("%s: not permitted to read %s\n", aProgram, aValue);
            retval = false;
        }
        else if (kOptionOtaImageListSelected)
        {
            PrintArgError("%s: Cannot have both OptionOtaImageList and kOptionOtaFilepath \n", aProgram);
            retval = false;
        }
        else
        {
            gOtaFilepath = aValue;
        }
        break;
    case kOptionOtaImageList:
        kOptionOtaImageListSelected = true;
        if (0 != access(aValue, R_OK))
        {
            PrintArgError("%s: not permitted to read %s\n", aProgram, aValue);
            retval = false;
        }
        else if (kOptionFilepathSelected)
        {
            PrintArgError("%s: Cannot have both OptionOtaImageList and kOptionOtaFilepath \n", aProgram);
            retval = false;
        }
        else
        {
            gOtaImageListFilepath = aValue;
        }
        break;
    case kOptionQueryImageBehavior:
        if (aValue == NULL)
        {
            PrintArgError("%s: ERROR: NULL QueryImageBehavior parameter\n", aProgram);
            retval = false;
        }
        else if (strcmp(aValue, "UpdateAvailable") == 0)
        {
            gQueryImageBehavior = OTAProviderExample::kRespondWithUpdateAvailable;
        }
        else if (strcmp(aValue, "Busy") == 0)
        {
            gQueryImageBehavior = OTAProviderExample::kRespondWithBusy;
        }
        else if (strcmp(aValue, "UpdateNotAvailable") == 0)
        {
            gQueryImageBehavior = OTAProviderExample::kRespondWithNotAvailable;
        }
        else
        {
            PrintArgError("%s: ERROR: Invalid QueryImageBehavior parameter:  %s\n", aProgram, aValue);
            retval = false;
        }
        break;
    case kOptionDelayedActionTimeSec:
        gDelayedActionTimeSec = static_cast<uint32_t>(strtol(aValue, NULL, 0));
        break;
    case kOptionUserConsent:
        if (aValue == NULL)
        {
            PrintArgError("%s: ERROR: NULL UserConsent parameter\n", aProgram);
            retval = false;
        }
        else if (strcmp(aValue, "granted") == 0)
        {
            gUserConsentState = chip::ota::UserConsentState::kGranted;
        }
        else if (strcmp(aValue, "denied") == 0)
        {
            gUserConsentState = chip::ota::UserConsentState::kDenied;
        }
        else if (strcmp(aValue, "deferred") == 0)
        {
            gUserConsentState = chip::ota::UserConsentState::kObtaining;
        }
        else
        {
            PrintArgError("%s: ERROR: Invalid UserConsent parameter:  %s\n", aProgram, aValue);
            retval = false;
        }
        break;
    default:
        PrintArgError("%s: INTERNAL ERROR: Unhandled option: %s\n", aProgram, aName);
        retval = false;
        break;
    }

    return (retval);
}

OptionDef cmdLineOptionsDef[] = {
    { "filepath", chip::ArgParser::kArgumentRequired, kOptionFilepath },
    { "otaImageList", chip::ArgParser::kArgumentRequired, kOptionOtaImageList },
    { "QueryImageBehavior", chip::ArgParser::kArgumentRequired, kOptionQueryImageBehavior },
    { "DelayedActionTimeSec", chip::ArgParser::kArgumentRequired, kOptionDelayedActionTimeSec },
    { "UserConsent", chip::ArgParser::kArgumentRequired, kOptionUserConsent },
    {},
};

OptionSet cmdLineOptions = { HandleOptions, cmdLineOptionsDef, "PROGRAM OPTIONS",
                             "  -f/--filepath <file>\n"
                             "        Path to a file containing an OTA image.\n"
                             "  -o/--otaImageList <file>\n"
                             "        Path to a file containing a list of OTA images.\n"
                             "  -q/--QueryImageBehavior <UpdateAvailable | Busy | UpdateNotAvailable>\n"
                             "        Status value in the Query Image Response\n"
                             "  -d/--DelayedActionTimeSec <time>\n"
                             "        Value in seconds for the DelayedActionTime in the Query Image Response\n"
                             "        and Apply Update Response\n"
                             "  -u/--UserConsent <granted | denied | deferred>\n"
                             "        granted: Status value in QueryImageResponse is set to UpdateAvailable\n"
                             "        denied: Status value in QueryImageResponse is set to UpdateNotAvailable\n"
                             "        deferred: Status value in QueryImageResponse is set to Busy\n"
                             "        -q/--QueryImageBehavior overrides this option\n" };

HelpOptions helpOptions("ota-provider-app", "Usage: ota-provider-app [options]", "1.0");

OptionSet * allOptions[] = { &cmdLineOptions, &helpOptions, nullptr };

int main(int argc, char * argv[])
{
    CHIP_ERROR err = CHIP_NO_ERROR;
    OTAProviderExample otaProvider;
    chip::ota::DefaultUserConsentProvider userConsentProvider;

    if (chip::Platform::MemoryInit() != CHIP_NO_ERROR)
    {
        fprintf(stderr, "FAILED to initialize memory\n");
        return 1;
    }

    if (chip::DeviceLayer::PlatformMgr().InitChipStack() != CHIP_NO_ERROR)
    {
        fprintf(stderr, "FAILED to initialize chip stack\n");
        return 1;
    }

    if (!chip::ArgParser::ParseArgs(argv[0], argc, argv, allOptions))
    {
        return 1;
    }

    chip::DeviceLayer::ConfigurationMgr().LogDeviceConfig();
    chip::Server::GetInstance().Init();

    // Initialize device attestation config
    SetDeviceAttestationCredentialsProvider(chip::Credentials::Examples::GetExampleDACProvider());

    BdxOtaSender * bdxOtaSender = otaProvider.GetBdxOtaSender();
    VerifyOrReturnError(bdxOtaSender != nullptr, 1);
    err = chip::Server::GetInstance().GetExchangeManager().RegisterUnsolicitedMessageHandlerForProtocol(chip::Protocols::BDX::Id,
                                                                                                        bdxOtaSender);
    if (err != CHIP_NO_ERROR)
    {
        ChipLogDetail(SoftwareUpdate, "RegisterUnsolicitedMessageHandler failed: %s", chip::ErrorStr(err));
        return 1;
    }

    ChipLogDetail(SoftwareUpdate, "using OTA file: %s", gOtaFilepath ? gOtaFilepath : "(none)");

    if (gOtaFilepath != nullptr)
    {
        otaProvider.SetOTAFilePath(gOtaFilepath);
    }

    otaProvider.SetQueryImageBehavior(gQueryImageBehavior);
    otaProvider.SetDelayedActionTimeSec(gDelayedActionTimeSec);

    if (gUserConsentState != chip::ota::UserConsentState::kUnknown)
    {
        userConsentProvider.SetGlobalUserConsentState(gUserConsentState);
        otaProvider.SetUserConsentDelegate(&userConsentProvider);
    }

    ChipLogDetail(SoftwareUpdate, "Using ImageList file: %s", gOtaImageListFilepath ? gOtaImageListFilepath : "(none)");

    if (gOtaImageListFilepath != nullptr)
    {
        // Parse JSON file and load the ota candidates
        std::vector<OTAProviderExample::DeviceSoftwareVersionModel> candidates;
        ParseJsonFileAndPopulateCandidates(gOtaImageListFilepath, candidates);
        otaProvider.SetOTACandidates(candidates);
    }

    chip::app::Clusters::OTAProvider::SetDelegate(kOtaProviderEndpoint, &otaProvider);

    chip::DeviceLayer::PlatformMgr().RunEventLoop();

    return 0;
}
