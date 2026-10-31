// Copyright (c) 2014-2017 Josh Blum
//                    2026 Pothos contributors
// SPDX-License-Identifier: BSL-1.0

#include "AudioHelpers.hpp"
#include <Pothos/Plugin.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <map>

using json = nlohmann::json;

//the largest channel count and the sample rates of the native formats
static void addNativeFormats(ma_context &context, const ma_device_type type, const ma_device_info &device, json &infoObject)
{
    ma_device_info info{};
    if (ma_context_get_device_info(&context, type, &device.id, &info) != MA_SUCCESS) return;
    ma_uint32 maxChannels = 0;
    for (ma_uint32 i = 0; i < info.nativeDataFormatCount; i++)
    {
        maxChannels = std::max(maxChannels, info.nativeDataFormats[i].channels);
    }
    //0 in a native format means "any": the value is not known
    const char *key = (type == ma_device_type_capture)? "Max Input Channels" : "Max Output Channels";
    if (maxChannels != 0) infoObject[key] = maxChannels;
    //miniaudio has no default rate: the rates of the native formats (none: any rate)
    const auto rates = PothosAudio::nativeSampleRates(info);
    if (not rates.empty() and not infoObject.contains("Native Sample Rates")) infoObject["Native Sample Rates"] = rates;
}

static std::string enumerateAudioDevices(void)
{
    json topObject;
    topObject["miniaudio Version"] = ma_version_string();
    json devicesArray = json::array();

    const auto backends = PothosAudio::contextBackends();
    ma_context context;
    const ma_result result = ma_context_init(backends.data(), ma_uint32(backends.size()), nullptr, &context);
    if (result != MA_SUCCESS)
    {
        topObject["Error"] = std::string("ma_context_init: ") + ma_result_description(result);
        topObject["Audio Device"] = devicesArray;
        return topObject.dump();
    }

    ma_device_info *playback = nullptr, *capture = nullptr;
    ma_uint32 numPlayback = 0, numCapture = 0;
    if (ma_context_get_devices(&context, &playback, &numPlayback, &capture, &numCapture) == MA_SUCCESS)
    {
        //one entry for each device name, with its input and output side
        std::map<std::string, json> byName;
        std::vector<std::string> order;
        const auto addDevices = [&](const ma_device_info *devices, const ma_uint32 count, const ma_device_type type)
        {
            for (ma_uint32 i = 0; i < count; i++)
            {
                const std::string name(devices[i].name);
                if (byName.count(name) == 0)
                {
                    order.push_back(name);
                    byName[name]["Device Name"] = name;
                    byName[name]["Backend Name"] = ma_get_backend_name(context.backend);
                }
                addNativeFormats(context, type, devices[i], byName[name]);
            }
        };
        addDevices(capture, numCapture, ma_device_type_capture);
        addDevices(playback, numPlayback, ma_device_type_playback);
        for (const auto &name : order) devicesArray.push_back(byName[name]);
    }
    ma_context_uninit(&context);

    topObject["Audio Device"] = devicesArray;
    return topObject.dump();
}

pothos_static_block(registerAudioInfo)
{
    Pothos::PluginRegistry::addCall(
        "/devices/audio/info", &enumerateAudioDevices);
}
