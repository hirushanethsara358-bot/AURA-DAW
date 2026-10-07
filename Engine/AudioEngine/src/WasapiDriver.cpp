/// @file WasapiDriver.cpp
/// @brief Shared-mode, event-driven WASAPI output for the Windows MVP path.

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <audioclient.h>
#include <avrt.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>
#include <windows.h>

#include "WasapiDriver.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <exception>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace Aura::Audio {
namespace {

template <typename Interface> class ComPtr final {
  public:
    ComPtr() = default;
    ~ComPtr() { reset(); }

    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;

    [[nodiscard]] Interface* Get() const noexcept { return pointer_; }
    [[nodiscard]] Interface** GetAddressOf() noexcept { return &pointer_; }
    Interface** operator&() noexcept {
        reset();
        return &pointer_;
    }
    Interface* operator->() const noexcept { return pointer_; }

  private:
    void reset() noexcept {
        if (pointer_ != nullptr) {
            pointer_->Release();
            pointer_ = nullptr;
        }
    }

    Interface* pointer_ = nullptr;
};

constexpr DWORD kThreadWaitMs = 100;
constexpr UINT32 kMaxDeviceBufferFrames = 262144;
constexpr UINT16 kMaxDeviceChannels = 32;
constexpr GUID kPcmSubformat{
    0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
constexpr GUID kIeeeFloatSubformat{
    0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
constexpr PROPERTYKEY kDeviceFriendlyNameKey{
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};

class ComApartment final {
  public:
    ComApartment() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComApartment() {
        if (SUCCEEDED(result_)) {
            CoUninitialize();
        }
    }
    [[nodiscard]] HRESULT result() const { return result_; }
    [[nodiscard]] bool usable() const {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

  private:
    HRESULT result_;
};

struct CoTaskMemDeleter {
    void operator()(void* pointer) const noexcept { CoTaskMemFree(pointer); }
};

struct EventCloser {
    void operator()(void* handle) const noexcept {
        if (handle != nullptr) {
            CloseHandle(static_cast<HANDLE>(handle));
        }
    }
};

using UniqueEvent = std::unique_ptr<void, EventCloser>;
using UniqueWaveFormat = std::unique_ptr<WAVEFORMATEX, CoTaskMemDeleter>;

std::string hresultMessage(const char* operation, HRESULT result) {
    std::ostringstream message;
    message << operation << " failed (HRESULT 0x" << std::hex << std::uppercase
            << static_cast<unsigned long>(result) << ").";
    return message.str();
}

std::string wideToUtf8(const wchar_t* value) {
    if (value == nullptr || *value == L'\0') {
        return {};
    }
    const int sourceLength = static_cast<int>(wcslen(value));
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, sourceLength,
                                             nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(required), '\0');
    const int written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, sourceLength,
                                            result.data(), required, nullptr, nullptr);
    return written == required ? result : std::string{};
}

std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                             static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    const int written =
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(), required);
    return written == required ? result : std::wstring{};
}

enum class DeviceSampleKind { Float32, PcmInteger };

struct DeviceFormat {
    UINT16 channels = 0;
    UINT32 sampleRate = 0;
    UINT16 containerBits = 0;
    UINT16 validBits = 0;
    UINT16 blockAlign = 0;
    DeviceSampleKind kind = DeviceSampleKind::Float32;
};

bool describeDeviceFormat(const WAVEFORMATEX& waveFormat, DeviceFormat& format,
                          std::string& error) {
    if (waveFormat.nChannels == 0 || waveFormat.nChannels > kMaxDeviceChannels ||
        waveFormat.nSamplesPerSec < 8000 || waveFormat.nSamplesPerSec > 384000 ||
        waveFormat.nBlockAlign == 0) {
        error = "WASAPI returned an unsupported device channel count, rate, or block alignment.";
        return false;
    }

    GUID subtype = GUID_NULL;
    UINT16 validBits = waveFormat.wBitsPerSample;
    if (waveFormat.wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* extensible = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(&waveFormat);
        if (waveFormat.cbSize < sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX)) {
            error = "WASAPI returned a truncated WAVEFORMATEXTENSIBLE description.";
            return false;
        }
        subtype = extensible->SubFormat;
        validBits = extensible->Samples.wValidBitsPerSample == 0
                        ? waveFormat.wBitsPerSample
                        : extensible->Samples.wValidBitsPerSample;
    } else if (waveFormat.wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        subtype = kIeeeFloatSubformat;
    } else if (waveFormat.wFormatTag == WAVE_FORMAT_PCM) {
        subtype = kPcmSubformat;
    } else {
        error = "WASAPI shared-mode device format is neither PCM nor IEEE float.";
        return false;
    }

    DeviceSampleKind sampleKind;
    if (IsEqualGUID(subtype, kIeeeFloatSubformat) && waveFormat.wBitsPerSample == 32 &&
        validBits == 32) {
        sampleKind = DeviceSampleKind::Float32;
    } else if (IsEqualGUID(subtype, kPcmSubformat) &&
               (waveFormat.wBitsPerSample == 16 || waveFormat.wBitsPerSample == 24 ||
                waveFormat.wBitsPerSample == 32) &&
               validBits > 0 && validBits <= waveFormat.wBitsPerSample) {
        sampleKind = DeviceSampleKind::PcmInteger;
    } else {
        error = "WASAPI shared-mode device format uses an unsupported sample representation.";
        return false;
    }

    const UINT16 bytesPerSample = static_cast<UINT16>((waveFormat.wBitsPerSample + 7U) / 8U);
    if (bytesPerSample == 0 ||
        static_cast<UINT32>(bytesPerSample) * waveFormat.nChannels != waveFormat.nBlockAlign) {
        error = "WASAPI device format has an inconsistent frame size.";
        return false;
    }

    format.channels = waveFormat.nChannels;
    format.sampleRate = waveFormat.nSamplesPerSec;
    format.containerBits = waveFormat.wBitsPerSample;
    format.validBits = validBits;
    format.blockAlign = waveFormat.nBlockAlign;
    format.kind = sampleKind;
    return true;
}

HRESULT getEndpointFriendlyName(IMMDevice* device, std::string& name) {
    ComPtr<IPropertyStore> propertyStore;
    HRESULT result = device->OpenPropertyStore(STGM_READ, &propertyStore);
    if (FAILED(result)) {
        return result;
    }

    PROPVARIANT friendlyName;
    PropVariantInit(&friendlyName);
    result = propertyStore->GetValue(kDeviceFriendlyNameKey, &friendlyName);
    if (SUCCEEDED(result) && friendlyName.vt == VT_LPWSTR && friendlyName.pwszVal != nullptr) {
        name = wideToUtf8(friendlyName.pwszVal);
    }
    PropVariantClear(&friendlyName);
    return result;
}

HRESULT getMixFormat(IMMDevice* device, UniqueWaveFormat& ownedFormat) {
    ComPtr<IAudioClient> audioClient;
    HRESULT result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                      reinterpret_cast<void**>(audioClient.GetAddressOf()));
    if (FAILED(result)) {
        return result;
    }
    WAVEFORMATEX* format = nullptr;
    result = audioClient->GetMixFormat(&format);
    if (SUCCEEDED(result)) {
        ownedFormat.reset(format);
    }
    return result;
}

HRESULT getDefaultRenderId(IMMDeviceEnumerator* enumerator, std::string& id) {
    ComPtr<IMMDevice> defaultDevice;
    HRESULT result = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &defaultDevice);
    if (FAILED(result)) {
        return result;
    }
    LPWSTR wideId = nullptr;
    result = defaultDevice->GetId(&wideId);
    if (SUCCEEDED(result)) {
        id = wideToUtf8(wideId);
    }
    CoTaskMemFree(wideId);
    return result;
}

bool getEnumerator(ComPtr<IMMDeviceEnumerator>& enumerator, std::string& error) {
    const HRESULT result = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                            CLSCTX_INPROC_SERVER, __uuidof(IMMDeviceEnumerator),
                                            reinterpret_cast<void**>(enumerator.GetAddressOf()));
    if (FAILED(result)) {
        error = hresultMessage("Creating the WASAPI device enumerator", result);
        return false;
    }
    return true;
}

void writeDeviceSample(std::uint8_t* destination, const DeviceFormat& format, double sample) {
    sample = std::clamp(sample, -1.0, 1.0);
    if (format.kind == DeviceSampleKind::Float32) {
        const float value = static_cast<float>(sample);
        std::memcpy(destination, &value, sizeof(value));
        return;
    }

    const int shift = static_cast<int>(format.containerBits - format.validBits);
    const std::int64_t negativeLimit = -(std::int64_t{1} << (format.validBits - 1U));
    const std::int64_t positiveLimit = (std::int64_t{1} << (format.validBits - 1U)) - 1;
    const std::int64_t quantized =
        sample <= -1.0 ? negativeLimit
                       : static_cast<std::int64_t>(std::llround(sample * positiveLimit));
    const std::int64_t aligned = quantized * (std::int64_t{1} << shift);

    if (format.containerBits == 16) {
        const auto value = static_cast<std::int16_t>(aligned);
        std::memcpy(destination, &value, sizeof(value));
    } else if (format.containerBits == 24) {
        const std::uint32_t value = static_cast<std::uint32_t>(static_cast<std::int32_t>(aligned));
        destination[0] = static_cast<std::uint8_t>(value & 0xffU);
        destination[1] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
        destination[2] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    } else {
        const auto value = static_cast<std::int32_t>(aligned);
        std::memcpy(destination, &value, sizeof(value));
    }
}

void convertPlanarToDevice(const std::vector<double>& callbackBuffer, int callbackChannels,
                           UINT32 callbackCapacity, UINT32 frames, const DeviceFormat& deviceFormat,
                           BYTE* deviceBuffer) {
    const std::size_t bytesPerSample = deviceFormat.containerBits / 8U;
    for (UINT32 frame = 0; frame < frames; ++frame) {
        const double left = callbackBuffer[frame];
        const double right =
            callbackChannels > 1
                ? callbackBuffer[static_cast<std::size_t>(callbackCapacity) + frame]
                : left;
        for (UINT16 channel = 0; channel < deviceFormat.channels; ++channel) {
            double sample = 0.0;
            if (deviceFormat.channels == 1) {
                sample = callbackChannels > 1 ? (left + right) * 0.5 : left;
            } else if (channel == 0) {
                sample = left;
            } else if (channel == 1) {
                sample = right;
            }
            if (!std::isfinite(sample)) {
                sample = 0.0;
            }
            const std::size_t offset = static_cast<std::size_t>(frame) * deviceFormat.blockAlign +
                                       static_cast<std::size_t>(channel) * bytesPerSample;
            writeDeviceSample(deviceBuffer + offset, deviceFormat, sample);
        }
    }
}

} // namespace

struct WasapiDriver::State {
    mutable std::mutex mutex;
    std::condition_variable startupCondition;
    std::thread worker;
    HANDLE stopEvent = nullptr;
    std::atomic<bool> stopRequested{false};
    std::atomic<bool> running{false};
    std::atomic<int> bufferFrames{0};
    bool startupComplete = false;
    bool startupSucceeded = false;
    std::string startupError;
    std::string lastError;
};

void WasapiDriver::publishStartupResult(State& state, bool succeeded, std::string error) {
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.startupComplete = true;
        state.startupSucceeded = succeeded;
        state.startupError = error;
        if (!succeeded) {
            state.lastError = std::move(error);
        }
    }
    state.startupCondition.notify_all();
}

void WasapiDriver::publishRuntimeError(State& state, std::string error) {
    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.lastError = std::move(error);
    }
    state.running.store(false, std::memory_order_release);
}

std::string WasapiDriver::initializeAndRender(State& state, const AudioDeviceConfig& config,
                                              IAudioCallback& callback) {
    ComApartment apartment;
    if (!apartment.usable()) {
        return hresultMessage("Initializing the WASAPI COM apartment", apartment.result());
    }

    ComPtr<IMMDeviceEnumerator> enumerator;
    std::string error;
    if (!getEnumerator(enumerator, error)) {
        return error;
    }

    ComPtr<IMMDevice> device;
    if (config.outputDeviceId.empty()) {
        const HRESULT result = enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia, &device);
        if (FAILED(result)) {
            return hresultMessage("Opening the default render endpoint", result);
        }
    } else {
        const std::wstring requestedId = utf8ToWide(config.outputDeviceId);
        if (requestedId.empty()) {
            return "The configured output device identifier is not valid UTF-8.";
        }
        const HRESULT result = enumerator->GetDevice(requestedId.c_str(), &device);
        if (FAILED(result)) {
            return hresultMessage("Opening the configured render endpoint", result);
        }
    }

    ComPtr<IAudioClient> audioClient;
    HRESULT result = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                      reinterpret_cast<void**>(audioClient.GetAddressOf()));
    if (FAILED(result)) {
        return hresultMessage("Activating the WASAPI audio client", result);
    }

    WAVEFORMATEX* rawMixFormat = nullptr;
    result = audioClient->GetMixFormat(&rawMixFormat);
    if (FAILED(result)) {
        return hresultMessage("Querying the shared-mode mix format", result);
    }
    UniqueWaveFormat mixFormat(rawMixFormat);
    DeviceFormat deviceFormat;
    if (!describeDeviceFormat(*mixFormat, deviceFormat, error)) {
        return error;
    }
    if (config.enableInputs || config.numInputChannels > 0) {
        return "WASAPI capture is not part of the current 0.1 playback path.";
    }
    if (config.numOutputChannels > 2) {
        return "The 0.1 WASAPI renderer currently supports one or two internal output channels.";
    }

    UniqueEvent audioEvent(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!audioEvent) {
        return "Creating the WASAPI render event failed.";
    }
    result = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, 0,
                                     0, mixFormat.get(), nullptr);
    if (FAILED(result)) {
        return hresultMessage("Initializing the shared-mode WASAPI stream", result);
    }
    result = audioClient->SetEventHandle(static_cast<HANDLE>(audioEvent.get()));
    if (FAILED(result)) {
        return hresultMessage("Registering the WASAPI render event", result);
    }

    UINT32 bufferSize = 0;
    result = audioClient->GetBufferSize(&bufferSize);
    if (FAILED(result) || bufferSize == 0 || bufferSize > kMaxDeviceBufferFrames) {
        return FAILED(result) ? hresultMessage("Querying the WASAPI endpoint buffer", result)
                              : "WASAPI returned an invalid or excessively large endpoint buffer.";
    }
    state.bufferFrames.store(static_cast<int>(bufferSize), std::memory_order_relaxed);

    ComPtr<IAudioRenderClient> renderClient;
    result = audioClient->GetService(__uuidof(IAudioRenderClient),
                                     reinterpret_cast<void**>(renderClient.GetAddressOf()));
    if (FAILED(result)) {
        return hresultMessage("Obtaining the WASAPI render client", result);
    }

    const int callbackChannels = config.numOutputChannels;
    std::vector<double> callbackBuffer(static_cast<std::size_t>(bufferSize) * callbackChannels,
                                       0.0);
    std::vector<double*> callbackOutputs(static_cast<std::size_t>(callbackChannels));
    for (int channel = 0; channel < callbackChannels; ++channel) {
        callbackOutputs[static_cast<std::size_t>(channel)] =
            callbackBuffer.data() + static_cast<std::size_t>(channel) * bufferSize;
    }

    if (const std::string callbackError =
            callback.configureDeviceFormat(deviceFormat.sampleRate, 0, callbackChannels);
        !callbackError.empty()) {
        return callbackError;
    }

    DWORD mmcssTaskIndex = 0;
    HANDLE mmcssHandle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &mmcssTaskIndex);
    result = audioClient->Start();
    if (FAILED(result)) {
        if (mmcssHandle != nullptr) {
            AvRevertMmThreadCharacteristics(mmcssHandle);
        }
        return hresultMessage("Starting the shared-mode WASAPI stream", result);
    }

    state.running.store(true, std::memory_order_release);
    publishStartupResult(state, true);

    HANDLE waitHandles[] = {state.stopEvent, static_cast<HANDLE>(audioEvent.get())};
    bool failedDuringRender = false;
    while (!state.stopRequested.load(std::memory_order_acquire)) {
        const DWORD waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, kThreadWaitMs);
        if (waitResult == WAIT_OBJECT_0) {
            break;
        }
        if (waitResult == WAIT_FAILED) {
            publishRuntimeError(state, "Waiting for the WASAPI render event failed.");
            failedDuringRender = true;
            break;
        }
        if (waitResult == WAIT_TIMEOUT) {
            UINT32 padding = 0;
            result = audioClient->GetCurrentPadding(&padding);
            if (FAILED(result)) {
                publishRuntimeError(state,
                                    hresultMessage("Checking WASAPI endpoint padding", result));
                failedDuringRender = true;
                break;
            }
            continue;
        }
        if (waitResult != WAIT_OBJECT_0 + 1) {
            publishRuntimeError(state, "WASAPI returned an unexpected render wait status.");
            failedDuringRender = true;
            break;
        }

        UINT32 padding = 0;
        result = audioClient->GetCurrentPadding(&padding);
        if (FAILED(result)) {
            publishRuntimeError(state, hresultMessage("Reading WASAPI endpoint padding", result));
            failedDuringRender = true;
            break;
        }
        if (padding > bufferSize) {
            publishRuntimeError(state, "WASAPI reported endpoint padding larger than its buffer.");
            failedDuringRender = true;
            break;
        }
        const UINT32 availableFrames = bufferSize - padding;
        if (availableFrames == 0) {
            continue;
        }

        BYTE* deviceBuffer = nullptr;
        result = renderClient->GetBuffer(availableFrames, &deviceBuffer);
        if (FAILED(result)) {
            publishRuntimeError(state, hresultMessage("Acquiring a WASAPI render packet", result));
            failedDuringRender = true;
            break;
        }
        callback.processBlock(nullptr, callbackOutputs.data(), 0, callbackChannels,
                              static_cast<int>(availableFrames));
        convertPlanarToDevice(callbackBuffer, callbackChannels, bufferSize, availableFrames,
                              deviceFormat, deviceBuffer);
        result = renderClient->ReleaseBuffer(availableFrames, 0);
        if (FAILED(result)) {
            publishRuntimeError(state, hresultMessage("Submitting a WASAPI render packet", result));
            failedDuringRender = true;
            break;
        }
    }

    if (mmcssHandle != nullptr) {
        AvRevertMmThreadCharacteristics(mmcssHandle);
    }
    const HRESULT stopResult = audioClient->Stop();
    if (FAILED(stopResult) && !failedDuringRender &&
        !state.stopRequested.load(std::memory_order_acquire)) {
        publishRuntimeError(state, hresultMessage("Stopping the WASAPI stream", stopResult));
        failedDuringRender = true;
    }
    state.running.store(false, std::memory_order_release);
    return {};
}

WasapiDriver::WasapiDriver() : state_(std::make_unique<State>()) {}

WasapiDriver::~WasapiDriver() {
    stop();
}

std::vector<AudioDeviceInfo> WasapiDriver::enumerateDevices() {
    std::vector<AudioDeviceInfo> devices;
    ComApartment apartment;
    if (!apartment.usable()) {
        return devices;
    }

    ComPtr<IMMDeviceEnumerator> enumerator;
    std::string error;
    if (!getEnumerator(enumerator, error)) {
        return devices;
    }

    std::string defaultId;
    (void)getDefaultRenderId(enumerator.Get(), defaultId);

    ComPtr<IMMDeviceCollection> collection;
    if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection))) {
        return devices;
    }
    UINT deviceCount = 0;
    if (FAILED(collection->GetCount(&deviceCount))) {
        return devices;
    }

    for (UINT index = 0; index < deviceCount; ++index) {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(index, &device))) {
            continue;
        }

        LPWSTR rawId = nullptr;
        if (FAILED(device->GetId(&rawId))) {
            continue;
        }
        AudioDeviceInfo info;
        info.id = wideToUtf8(rawId);
        CoTaskMemFree(rawId);
        if (info.id.empty()) {
            continue;
        }
        info.driver = DriverType::WASAPI;
        info.maxInputChannels = 0;
        info.isDefaultOutput = info.id == defaultId;
        if (FAILED(getEndpointFriendlyName(device.Get(), info.name)) || info.name.empty()) {
            info.name = "WASAPI output";
        }

        UniqueWaveFormat mixFormat;
        if (SUCCEEDED(getMixFormat(device.Get(), mixFormat))) {
            DeviceFormat description;
            std::string ignored;
            if (describeDeviceFormat(*mixFormat, description, ignored)) {
                info.maxOutputChannels = description.channels;
                info.supportedSampleRates.push_back(static_cast<double>(description.sampleRate));
            }
        }
        if (info.maxOutputChannels > 0 && !info.supportedSampleRates.empty()) {
            devices.push_back(std::move(info));
        }
    }
    return devices;
}

std::string WasapiDriver::start(const AudioDeviceConfig& config, IAudioCallback& callback) {
    if (const std::string error = config.validate(); !error.empty()) {
        return error;
    }
    if (config.enableInputs || config.numInputChannels > 0) {
        return "WASAPI capture is not part of the current 0.1 playback path.";
    }
    if (config.numOutputChannels > 2) {
        return "The 0.1 WASAPI renderer currently supports one or two internal output channels.";
    }
    stop();
    std::unique_lock<std::mutex> lock(state_->mutex);
    state_->stopRequested.store(false, std::memory_order_release);
    state_->running.store(false, std::memory_order_release);
    state_->startupComplete = false;
    state_->startupSucceeded = false;
    state_->startupError.clear();
    state_->lastError.clear();
    state_->bufferFrames.store(0, std::memory_order_relaxed);

    state_->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (state_->stopEvent == nullptr) {
        state_->lastError = "Creating the WASAPI stop event failed.";
        return state_->lastError;
    }

    state_->worker = std::thread([this, config, &callback] {
        std::string error;
        try {
            error = initializeAndRender(*state_, config, callback);
        } catch (const std::exception& exception) {
            error = std::string("WASAPI worker failed: ") + exception.what();
        } catch (...) {
            error = "WASAPI worker failed with an unknown exception.";
        }

        bool startupComplete = false;
        {
            std::lock_guard<std::mutex> workerLock(state_->mutex);
            startupComplete = state_->startupComplete;
        }
        if (!startupComplete) {
            publishStartupResult(*state_, false,
                                 error.empty() ? "WASAPI initialization failed." : error);
        } else if (!error.empty()) {
            publishRuntimeError(*state_, std::move(error));
        }
        state_->running.store(false, std::memory_order_release);
    });

    state_->startupCondition.wait(lock, [this] { return state_->startupComplete; });
    if (state_->startupSucceeded) {
        return {};
    }

    const std::string startError = state_->startupError;
    state_->stopRequested.store(true, std::memory_order_release);
    if (state_->stopEvent != nullptr) {
        SetEvent(state_->stopEvent);
    }
    std::thread failedWorker = std::move(state_->worker);
    HANDLE failedStopEvent = state_->stopEvent;
    state_->stopEvent = nullptr;
    lock.unlock();
    if (failedWorker.joinable()) {
        failedWorker.join();
    }
    if (failedStopEvent != nullptr) {
        CloseHandle(failedStopEvent);
    }
    return startError;
}

void WasapiDriver::stop() {
    std::thread worker;
    HANDLE stopEvent = nullptr;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        state_->stopRequested.store(true, std::memory_order_release);
        stopEvent = state_->stopEvent;
        if (stopEvent != nullptr) {
            SetEvent(stopEvent);
        }
        if (state_->worker.joinable()) {
            worker = std::move(state_->worker);
        }
    }
    if (worker.joinable()) {
        worker.join();
    }
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        if (state_->stopEvent == stopEvent) {
            state_->stopEvent = nullptr;
        }
        state_->running.store(false, std::memory_order_release);
        state_->stopRequested.store(false, std::memory_order_release);
    }
    if (stopEvent != nullptr) {
        CloseHandle(stopEvent);
    }
}

bool WasapiDriver::isRunning() const {
    return state_->running.load(std::memory_order_acquire);
}

int WasapiDriver::latencySamples() const {
    return state_->bufferFrames.load(std::memory_order_relaxed);
}

std::string WasapiDriver::lastError() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->lastError;
}

} // namespace Aura::Audio

#endif // defined(_WIN32)
