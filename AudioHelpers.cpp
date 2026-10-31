// Copyright (c) 2026 Pothos contributors
// SPDX-License-Identifier: BSL-1.0

#include "AudioHelpers.hpp"
#include <Pothos/Exception.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cstring>

//lower case letters and digits only: "Core Audio" -> "coreaudio", "OpenSL|ES" -> "opensles"
static std::string normalizeBackendName(const std::string &name)
{
    std::string out;
    for (const unsigned char c : name)
    {
        if (std::isalnum(c)) out.push_back(char(std::tolower(c)));
    }
    return out;
}

ma_backend PothosAudio::backendFromName(const std::string &name)
{
    //the display names of miniaudio ("PulseAudio", "Core Audio", "Null", ...), any case
    const auto key = normalizeBackendName(name);
    for (int i = 0; i <= int(ma_backend_null); i++)
    {
        const auto backend = ma_backend(i);
        if (not key.empty() and key == normalizeBackendName(ma_get_backend_name(backend))) return backend;
    }
    throw Pothos::InvalidArgumentException("PothosAudio::backendFromName("+name+")", "unknown miniaudio backend");
}

std::vector<ma_backend> PothosAudio::contextBackends(void)
{
    const char *name = std::getenv("POTHOS_AUDIO_BACKEND");
    if (name != nullptr and name[0] != '\0') return {backendFromName(name)};

    //the enabled backends of this build, in the default order of miniaudio
    std::array<ma_backend, ma_backend_null+1> enabled{};
    size_t count = 0;
    if (ma_get_enabled_backends(enabled.data(), enabled.size(), &count) != MA_SUCCESS) count = 0;
    std::vector<ma_backend> backends;
    for (size_t i = 0; i < count; i++)
    {
        //no silent virtual device on a computer without an audio system
        if (enabled[i] == ma_backend_null or enabled[i] == ma_backend_custom) continue;
        backends.push_back(enabled[i]);
    }
    return backends;
}

ma_format PothosAudio::formatFromDType(const Pothos::DType &dtype)
{
    if (dtype == Pothos::DType("float32")) return ma_format_f32;
    if (dtype == Pothos::DType("int32")) return ma_format_s32;
    if (dtype == Pothos::DType("int16")) return ma_format_s16;
    if (dtype == Pothos::DType("int8")) return ma_format_u8;
    if (dtype == Pothos::DType("uint8")) return ma_format_u8;
    throw Pothos::InvalidArgumentException("PothosAudio::formatFromDType("+dtype.name()+")", "unsupported data type");
}

void PothosAudio::deinterleave(const void *in, const std::vector<void *> &outs, size_t numFrames, size_t numChans, size_t sampleSize)
{
    const auto src = static_cast<const char *>(in);
    for (size_t ch = 0; ch < numChans; ch++)
    {
        auto dst = static_cast<char *>(outs.at(ch));
        for (size_t i = 0; i < numFrames; i++)
        {
            std::memcpy(dst + i*sampleSize, src + (i*numChans + ch)*sampleSize, sampleSize);
        }
    }
}

void PothosAudio::interleave(const std::vector<const void *> &ins, void *out, size_t numFrames, size_t numChans, size_t sampleSize)
{
    const auto dst = static_cast<char *>(out);
    for (size_t ch = 0; ch < numChans; ch++)
    {
        const auto src = static_cast<const char *>(ins.at(ch));
        for (size_t i = 0; i < numFrames; i++)
        {
            std::memcpy(dst + (i*numChans + ch)*sampleSize, src + i*sampleSize, sampleSize);
        }
    }
}

std::optional<ma_device_id> PothosAudio::findDevice(ma_context &ctx, const ma_device_type type, const std::string &name, std::string &foundName)
{
    const std::string what("PothosAudio::findDevice("+name+")");
    ma_device_info *playback = nullptr, *capture = nullptr;
    ma_uint32 numPlayback = 0, numCapture = 0;
    const ma_result result = ma_context_get_devices(&ctx, &playback, &numPlayback, &capture, &numCapture);
    if (result != MA_SUCCESS) throw Pothos::Exception(what, ma_result_description(result));
    const bool isPlayback = (type == ma_device_type_playback);
    const ma_device_info *devices = isPlayback? playback : capture;
    const size_t numDevices = isPlayback? numPlayback : numCapture;
    if (numDevices == 0) throw Pothos::NotFoundException(what, "No devices available");

    //empty name: the default device
    if (name.empty())
    {
        foundName = devices[0].name;
        for (size_t i = 0; i < numDevices; i++)
        {
            if (devices[i].isDefault) foundName = devices[i].name;
        }
        return std::nullopt;
    }

    //numeric name: the index in the list
    if (std::all_of(name.begin(), name.end(), [](const unsigned char c){return std::isdigit(c) != 0;}))
    {
        const auto index = std::stoul(name);
        if (index >= numDevices) throw Pothos::RangeException(what, "Device index out of range");
        foundName = devices[index].name;
        return devices[index].id;
    }

    //the device with the name
    for (size_t i = 0; i < numDevices; i++)
    {
        if (name == devices[i].name)
        {
            foundName = devices[i].name;
            return devices[i].id;
        }
    }
    throw Pothos::NotFoundException(what, "No matching device: "+name);
}

std::vector<ma_uint32> PothosAudio::nativeSampleRates(const ma_device_info &info)
{
    std::vector<ma_uint32> rates;
    for (ma_uint32 i = 0; i < info.nativeDataFormatCount; i++)
    {
        const auto rate = info.nativeDataFormats[i].sampleRate;
        if (rate != 0 and std::find(rates.begin(), rates.end(), rate) == rates.end()) rates.push_back(rate);
    }
    std::sort(rates.begin(), rates.end());
    return rates;
}
