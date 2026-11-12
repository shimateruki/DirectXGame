#include "VideoSpritePlayer.h"
#include "engine/graphics/core/TextureManager.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl.h>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {
constexpr size_t kRowBytes = VideoSpritePlayer::kWidth * 4;
constexpr size_t kFrameBytes = kRowBytes * VideoSpritePlayer::kHeight;

struct VideoFrame {
    double seconds = 0.0; // ループ先頭からの時刻。
    bool startsLoop = false;
    std::vector<uint8_t> pixels;
};

bool CopySample(IMFSample& sample, LONG stride, std::vector<uint8_t>& pixels) {
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(sample.ConvertToContiguousBuffer(&buffer)))
        return false;
    pixels.resize(kFrameBytes);
    ComPtr<IMF2DBuffer> buffer2D;
    BYTE* firstRow = nullptr;
    DWORD length = 0;
    const bool use2D = SUCCEEDED(buffer.As(&buffer2D));
    HRESULT hr = use2D ? buffer2D->Lock2D(&firstRow, &stride) : buffer->Lock(&firstRow, nullptr, &length);
    if (FAILED(hr))
        return false;
    const auto pitch = std::abs(static_cast<int64_t>(stride));
    const auto required = pitch * (VideoSpritePlayer::kHeight - 1) + kRowBytes;
    const bool valid = firstRow && pitch >= kRowBytes && (use2D || required <= length);
    if (valid) {
        if (!use2D && stride < 0)
            firstRow += pitch * (VideoSpritePlayer::kHeight - 1);
        for (uint32_t y = 0; y < VideoSpritePlayer::kHeight; ++y) {
            auto* destination = pixels.data() + y * kRowBytes;
            std::memcpy(destination, firstRow + static_cast<ptrdiff_t>(y) * stride, kRowBytes);
            // RGB32の未定義バイトを不透明なアルファへ揃える。
            for (size_t x = 3; x < kRowBytes; x += 4)
                destination[x] = 255;
        }
    }
    if (use2D)
        buffer2D->Unlock2D();
    else
        buffer->Unlock();
    return valid;
}

bool ReadFormat(IMFSourceReader& reader, LONG& stride) {
    ComPtr<IMFMediaType> actual;
    UINT32 width = 0, height = 0, pitch = 0;
    GUID subtype{};
    if (FAILED(reader.GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actual)) ||
        FAILED(actual->GetGUID(MF_MT_SUBTYPE, &subtype)) || subtype != MFVideoFormat_RGB32 ||
        FAILED(MFGetAttributeSize(actual.Get(), MF_MT_FRAME_SIZE, &width, &height)) ||
        width != VideoSpritePlayer::kWidth || height != VideoSpritePlayer::kHeight)
        return false;
    if (SUCCEEDED(actual->GetUINT32(MF_MT_DEFAULT_STRIDE, &pitch)))
        stride = static_cast<LONG>(pitch);
    else if (FAILED(MFGetStrideForBitmapInfoHeader(MFVideoFormat_RGB32.Data1, width, &stride)))
        return false;
    return true;
}

bool OpenReader(const std::filesystem::path& path, ComPtr<IMFSourceReader>& reader, LONG& stride) {
    ComPtr<IMFAttributes> attributes;
    ComPtr<IMFMediaType> output;
    return SUCCEEDED(MFCreateAttributes(&attributes, 1)) &&
        SUCCEEDED(attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE)) &&
        SUCCEEDED(MFCreateSourceReaderFromURL(path.c_str(), attributes.Get(), &reader)) &&
        SUCCEEDED(reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE)) &&
        SUCCEEDED(reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE)) &&
        SUCCEEDED(MFCreateMediaType(&output)) &&
        SUCCEEDED(output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video)) &&
        SUCCEEDED(output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32)) &&
        SUCCEEDED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, output.Get())) &&
        ReadFormat(*reader.Get(), stride);
}
}

struct VideoSpritePlayer::Playback {
    std::mutex mutex;
    std::condition_variable changed;
    std::filesystem::path path;
    std::deque<VideoFrame> queue;
    uint64_t request = 0; // ファイル切替前のデコード結果を識別する。
    bool stop = false;
    bool error = false;
    std::thread worker;

    VideoFrame selected;
    double seconds = 0.0;
    bool pending = false;
    bool hasImage = false;

    void Read() {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool mediaStarted = SUCCEEDED(com) && SUCCEEDED(MFStartup(MF_VERSION));
        uint64_t previous = 0;
        for (;;) {
            std::filesystem::path file;
            uint64_t id;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, [&] { return stop || previous != request; });
                if (stop)
                    break;
                id = previous = request;
                file = path;
            }
            if (file.empty())
                continue;
            if (!mediaStarted || !Decode(file, id)) {
                std::lock_guard lock(mutex);
                if (request == id)
                    error = true;
            }
        }
        if (mediaStarted)
            MFShutdown();
        if (SUCCEEDED(com))
            CoUninitialize();
    }

    bool Decode(const std::filesystem::path& file, uint64_t id) {
        ComPtr<IMFSourceReader> reader;
        LONG stride = 0;
        if (!OpenReader(file, reader, stride))
            return false;
        bool first = true;
        LONGLONG origin = 0;
        for (;;) {
            {
                std::unique_lock lock(mutex);
                changed.wait(lock, [&] { return stop || request != id || queue.size() < 3; });
                if (stop || request != id)
                    return true;
            }
            ComPtr<IMFSample> sample;
            DWORD flags = 0;
            LONGLONG timestamp = 0;
            if (FAILED(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr,
                                         &flags, &timestamp, &sample)) ||
                (flags & MF_SOURCE_READERF_ERROR))
                return false;
            // 初回デコード時にも形式確定の通知が来る。対応寸法・形式なら読み続ける。
            if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) && !ReadFormat(*reader.Get(), stride))
                return false;
            if (sample) {
                if (first)
                    origin = timestamp;
                VideoFrame frame;
                frame.startsLoop = first;
                frame.seconds = static_cast<double>(timestamp - origin) / 10000000.0;
                if (!CopySample(*sample.Get(), stride, frame.pixels))
                    return false;
                std::lock_guard lock(mutex);
                if (stop || request != id)
                    return true;
                queue.push_back(std::move(frame));
                first = false;
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                if (first)
                    return false;
                PROPVARIANT position{};
                position.vt = VT_I8;
                position.hVal.QuadPart = 0;
                if (FAILED(reader->SetCurrentPosition(GUID_NULL, position)))
                    return false;
                first = true;
            }
        }
    }
};

VideoSpritePlayer::VideoSpritePlayer() : playback_(std::make_unique<Playback>()) {}

VideoSpritePlayer::~VideoSpritePlayer() {
    Close();
    auto& state = *playback_;
    {
        std::lock_guard lock(state.mutex);
        state.stop = true;
    }
    state.changed.notify_one();
    if (state.worker.joinable())
        state.worker.join();
}

void VideoSpritePlayer::RequestOpen(const std::filesystem::path& path) {
    Close();
    auto& state = *playback_;
    if (path.empty())
        return;
    {
        std::lock_guard lock(state.mutex);
        state.path = path;
        ++state.request;
    }
    if (!state.worker.joinable())
        state.worker = std::thread([&state] { state.Read(); });
    state.changed.notify_one();
}

void VideoSpritePlayer::Close() {
    auto& state = *playback_;
    {
        std::lock_guard lock(state.mutex);
        state.path.clear();
        ++state.request;
        state.queue.clear();
        state.error = false;
        state.seconds = 0.0;
        state.selected = {};
        state.pending = state.hasImage = false;
    }
    state.changed.notify_one();
    TextureManager::GetInstance()->ReleaseVideoSurface(this);
}

void VideoSpritePlayer::Update(double deltaSeconds) {
    auto& state = *playback_;
    std::lock_guard lock(state.mutex);
    if (state.error)
        return;
    if ((state.pending || state.hasImage) && std::isfinite(deltaSeconds))
        state.seconds += std::clamp(deltaSeconds, 0.0, 0.1);
    while (!state.queue.empty()) {
        auto& next = state.queue.front();
        if (next.startsLoop) {
            // 最終フレームにも通常の表示時間を与えてから先頭に戻す。
            if ((state.pending || state.hasImage) && state.seconds < state.selected.seconds + 1.0 / 30.0)
                break;
            state.seconds = 0.0;
        } else if (next.seconds > state.seconds) {
            break;
        }
        state.selected = std::move(next);
        state.pending = true;
        state.queue.pop_front();
    }
    state.changed.notify_one();
}

std::optional<uint32_t> VideoSpritePlayer::PrepareTextureForDraw(ID3D12GraphicsCommandList& commands) {
    auto& state = *playback_;
    if (HasError() || (!state.pending && !state.hasImage))
        return std::nullopt;
    auto* textures = TextureManager::GetInstance();
    const auto handle = textures->AcquireVideoSurface(this);
    if (!handle)
        return std::nullopt;
    if (state.pending && textures->UploadVideoSurface(this, commands, state.selected.pixels)) {
        state.pending = false;
        state.hasImage = true;
    }
    return state.hasImage ? handle : std::nullopt;
}

bool VideoSpritePlayer::HasError() const {
    std::lock_guard lock(playback_->mutex);
    return playback_->error;
}
