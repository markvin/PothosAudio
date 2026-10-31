// Copyright (c) 2026 Pothos contributors
// SPDX-License-Identifier: BSL-1.0

#include "AudioHelpers.hpp"
#include <Pothos/Testing.hpp>
#include <Pothos/Exception.hpp>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

using namespace PothosAudio;

//set or remove an environment variable for the time of a test
struct ScopedEnv
{
    ScopedEnv(const char *name, const char *value):
        name(name)
    {
        if (const char *old = std::getenv(name)) previous = old;
        set(value);
    }
    ~ScopedEnv(void)
    {
        set(previous? previous->c_str() : nullptr);
    }
    void set(const char *value)
    {
        #ifdef _WIN32
        _putenv_s(name, (value == nullptr)? "" : value);
        #else
        if (value == nullptr) unsetenv(name);
        else setenv(name, value, 1);
        #endif
    }
    const char *name;
    std::optional<std::string> previous;
};

POTHOS_TEST_BLOCK("/audio/tests", test_audio_int8_conversion)
{
    POTHOS_TEST_EQUAL(int(int8ToU8(-128)), 0x00);
    POTHOS_TEST_EQUAL(int(int8ToU8(0)), 0x80);
    POTHOS_TEST_EQUAL(int(int8ToU8(127)), 0xFF);
    for (int i = -128; i <= 127; i++)
    {
        POTHOS_TEST_EQUAL(int(u8ToInt8(int8ToU8(std::int8_t(i)))), i);
    }
}

POTHOS_TEST_BLOCK("/audio/tests", test_audio_format_from_dtype)
{
    POTHOS_TEST_TRUE(formatFromDType(Pothos::DType("float32")) == ma_format_f32);
    POTHOS_TEST_TRUE(formatFromDType(Pothos::DType("int32")) == ma_format_s32);
    POTHOS_TEST_TRUE(formatFromDType(Pothos::DType("int16")) == ma_format_s16);
    POTHOS_TEST_TRUE(formatFromDType(Pothos::DType("int8")) == ma_format_u8);
    POTHOS_TEST_TRUE(formatFromDType(Pothos::DType("uint8")) == ma_format_u8);
    POTHOS_TEST_THROWS(formatFromDType(Pothos::DType("complex_float32")), Pothos::InvalidArgumentException);
}

POTHOS_TEST_BLOCK("/audio/tests", test_audio_backend_names)
{
    POTHOS_TEST_TRUE(backendFromName("null") == ma_backend_null);
    try
    {
        backendFromName("nosuchbackend");
        POTHOS_TEST_TRUE(false);
    }
    catch (const Pothos::InvalidArgumentException &ex)
    {
        POTHOS_TEST_TRUE(ex.displayText().find("nosuchbackend") != std::string::npos);
    }

    {
        ScopedEnv env("POTHOS_AUDIO_BACKEND", nullptr);
        const auto backends = contextBackends();
        POTHOS_TEST_TRUE(not backends.empty());
        POTHOS_TEST_TRUE(std::find(backends.begin(), backends.end(), ma_backend_null) == backends.end());
    }
    {
        ScopedEnv env("POTHOS_AUDIO_BACKEND", "null");
        const auto backends = contextBackends();
        POTHOS_TEST_EQUAL(backends.size(), 1u);
        POTHOS_TEST_TRUE(backends.at(0) == ma_backend_null);
    }
    {
        ScopedEnv env("POTHOS_AUDIO_BACKEND", "nosuchbackend");
        POTHOS_TEST_THROWS(contextBackends(), Pothos::InvalidArgumentException);
    }
}

POTHOS_TEST_BLOCK("/audio/tests", test_audio_interleave)
{
    const std::vector<std::int16_t> left{1, 2, 3}, right{-1, -2, -3};
    std::vector<std::int16_t> interleaved(6);
    interleave({left.data(), right.data()}, interleaved.data(), 3, 2, sizeof(std::int16_t));
    const std::vector<std::int16_t> expected{1, -1, 2, -2, 3, -3};
    POTHOS_TEST_EQUALV(interleaved, expected);

    std::vector<std::int16_t> left2(3), right2(3);
    deinterleave(interleaved.data(), {left2.data(), right2.data()}, 3, 2, sizeof(std::int16_t));
    POTHOS_TEST_EQUALV(left2, left);
    POTHOS_TEST_EQUALV(right2, right);
}

POTHOS_TEST_BLOCK("/audio/tests", test_audio_native_sample_rates)
{
    ma_device_info info{};
    const ma_uint32 rates[] = {48000, 44100, 0, 48000, 96000};
    for (const auto rate : rates)
    {
        info.nativeDataFormats[info.nativeDataFormatCount].format = ma_format_f32;
        info.nativeDataFormats[info.nativeDataFormatCount].channels = 2;
        info.nativeDataFormats[info.nativeDataFormatCount].sampleRate = rate;
        info.nativeDataFormatCount++;
    }
    const std::vector<ma_uint32> expected{44100, 48000, 96000};
    POTHOS_TEST_TRUE(PothosAudio::nativeSampleRates(info) == expected);
    POTHOS_TEST_TRUE(PothosAudio::nativeSampleRates(ma_device_info{}).empty());
}
