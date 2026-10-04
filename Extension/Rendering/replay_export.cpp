#include "replay_export.h"
#include "replay_capture_queue.h"
#include "Engine/Core/Hooks/hooks.h"
#include "Engine/Core/Platform/memory.h"
#include "Engine/Game/Build/addresses.h"
#include "Engine/Game/Build/20260929/replay_export.h"
#include "Engine/Core/Log/logging.h"
#include "Extension/Slam/slam_runtime.h"
#include "Extension/UI/Overlay/replay_capture.h"
#include <array>
#include <atomic>
#include <cstring>
#include <intrin.h>
#include <mutex>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace dingosdk::replay_export {
namespace {
namespace native = addr::replay_export;
// The encoder passes its arguments straight on to the codec, so all four
// register arguments are forwarded as they came.
using Submit = void (*)(std::uintptr_t encoder, std::uintptr_t pixels, std::uintptr_t pitch, std::uintptr_t unused);
std::atomic<Submit> original{};
using CopyTexture=void(*)(std::uintptr_t,std::uintptr_t,std::uintptr_t);
using MappedFrame=void(*)(std::uintptr_t,std::uintptr_t,std::uintptr_t,std::uintptr_t);
using BeginCapture=void(*)(std::uintptr_t,std::uintptr_t);
std::atomic<CopyTexture> original_copy{};
std::atomic<MappedFrame> original_mapped{};
std::atomic<BeginCapture> original_begin{};
std::atomic<std::uintptr_t> image{};
std::atomic<std::uint64_t> diagnostic_at{};
std::mutex frames_mutex;
CaptureQueue<slam::Snapshot> frames;
thread_local const slam::Snapshot* encoding_scene{};

std::optional<CaptureKey> capture_key(bool draining) {
    CaptureKey key;
    std::uint32_t count{};
    std::uintptr_t vtable{};
    const auto base=image.load();
    if (!base || !memory::peek(base+native::capture_manager,key.manager) || !key.manager ||
        !memory::peek(key.manager+native::capture_lease,key.lease) || !key.lease ||
        !memory::peek(key.lease,key.encoder) || !key.encoder ||
        !memory::peek(key.encoder,vtable) || vtable!=base+native::replay_encoder_vtable ||
        !memory::peek(key.manager+native::capture_ring,key.ring) || !key.ring ||
        !memory::peek(key.manager+native::capture_slots,count) || !count || count>64 ||
        !memory::peek(key.manager+(draining ? native::capture_read : native::capture_write),key.slot) || key.slot>=count)
        return {};
    const auto slot=key.ring+key.slot*native::capture_slot_stride;
    if (!memory::peek(slot,key.resource) || !key.resource ||
        !memory::peek_bytes(slot+native::capture_metadata,key.metadata.data(),key.metadata.size())) return {};
    return key;
}

void copy_texture(std::uintptr_t commands,std::uintptr_t destination,std::uintptr_t source) {
    const auto incoming_error=GetLastError();
    // This return address is the verified native capture copy, after all its
    // acceptance/dimension checks. Other texture copies cannot publish frames.
    if (reinterpret_cast<std::uintptr_t>(_ReturnAddress())==image.load()+native::capture_copy_return) {
        try {
            if (const auto key=capture_key(false)) {
                auto scene=slam::presentation_snapshot();
                std::lock_guard lock(frames_mutex);
                frames.record(*key,std::move(scene));
            }
        } catch (...) {}
    }
    SetLastError(incoming_error);
    original_copy.load()(commands,destination,source);
}

void mapped_frame(std::uintptr_t encoder,std::uintptr_t metadata,std::uintptr_t pixels,std::uintptr_t unused) {
    const auto incoming_error=GetLastError();
    std::optional<slam::Snapshot> scene;
    try {
        if (const auto key=capture_key(true); key && key->encoder==encoder) {
            std::array<std::byte,48> submitted{};
            const bool matches=memory::peek_bytes(metadata+4,submitted.data(),submitted.size()) && submitted==key->metadata;
            std::lock_guard lock(frames_mutex);
            // Consume even a mismatched callback. Never substitute the latest
            // scene or leave an old snapshot attached to a recycled slot.
            scene=frames.take(*key);
            if (!matches) scene.reset();
        }
    } catch (...) {}
    struct RestoreScene {
        const slam::Snapshot* previous=encoding_scene;
        ~RestoreScene() {encoding_scene=previous;}
    } restore;
    encoding_scene=scene ? &*scene : nullptr;
    SetLastError(incoming_error);
    // The native wrapper tail-calls submit_frame on this same thread.
    original_mapped.load()(encoder,metadata,pixels,unused);
}

void begin_capture(std::uintptr_t manager,std::uintptr_t lease) {
    const auto incoming_error=GetLastError();
    {
        std::lock_guard lock(frames_mutex);
        frames.clear();
    }
    SetLastError(incoming_error);
    original_begin.load()(manager,lease);
}

void submit(std::uintptr_t encoder, std::uintptr_t pixels, std::uintptr_t pitch, std::uintptr_t unused) {
    const auto incoming_error=GetLastError();
    std::vector<std::uint8_t> composited;
    // Encoding is done on this thread before the call returns, so letting every
    // frame through also holds the renderer to the pace the encoder keeps.
    std::uintptr_t vtable{};
    if (memory::peek(encoder, vtable) && vtable == image.load() + native::replay_encoder_vtable) {
        *reinterpret_cast<std::uint8_t*>(encoder + native::frame_permit) = 1;
        if (pixels) {
            auto result=overlay::ReplayCaptureResult::unavailable;
            try {
                std::uint32_t width{},height{};
                if (encoding_scene && memory::peek(encoder+native::frame_width,width) && memory::peek(encoder+native::frame_height,height)) {
                    if (const auto frame=bgra_frame(width,height,pitch)) {
                        composited.resize(frame->bytes);
                        if (memory::peek_bytes(pixels,composited.data(),composited.size())) {
                            result=overlay::capture_slam_frame(composited,*frame,*encoding_scene);
                            if (result==overlay::ReplayCaptureResult::included)
                                pixels=reinterpret_cast<std::uintptr_t>(composited.data());
                        }
                    }
                }
            } catch (...) {}
            const auto now=GetTickCount64();
            if (result==overlay::ReplayCaptureResult::unavailable && now>=diagnostic_at.load()) {
                diagnostic_at.store(now+5000);
                logging::write(logging::Level::warning,logging::Channel::graphics,
                    "Replay export frame has no X-ray: a matching capture-slot pose, pixels or GPU resources are unavailable.");
            }
        }
    }
    SetLastError(incoming_error);
    // The codec consumes pixels synchronously. Keep the composited copy alive
    // until it returns; the game's readback buffer is never modified.
    original.load()(encoder, pixels, pitch, unused);
}
} // namespace

bool start(std::uintptr_t base) noexcept {
    std::array<void*,4> targets{};
    std::size_t prepared{};
    try {
        const auto matches=[base](std::uintptr_t rva,const auto& prefix) {
            std::array<unsigned char,std::tuple_size_v<std::decay_t<decltype(prefix)>>> bytes{};
            return memory::read_bytes(base+rva,bytes.data(),bytes.size()) && bytes==prefix;
        };
        // All capture contracts must match before any frame hook is enabled.
        if (!base || !matches(native::submit_frame,native::submit_frame_prefix) ||
            !matches(native::copy_texture,native::copy_texture_prefix) ||
            !matches(native::mapped_frame,native::mapped_frame_prefix) ||
            !matches(native::begin_capture,native::begin_capture_prefix)) return false;
        std::array<unsigned char,5> copy_call{};
        std::int32_t displacement{};
        if (!memory::read_bytes(base+native::capture_copy_return-5,copy_call.data(),copy_call.size()) || copy_call[0]!=0xe8)
            return false;
        std::memcpy(&displacement,copy_call.data()+1,sizeof displacement);
        if (base+native::capture_copy_return+displacement!=base+native::copy_texture) return false;
        image = base;
        targets={reinterpret_cast<void*>(base+native::submit_frame),reinterpret_cast<void*>(base+native::copy_texture),
            reinterpret_cast<void*>(base+native::mapped_frame),reinterpret_cast<void*>(base+native::begin_capture)};
        const std::array<void*,4> detours{reinterpret_cast<void*>(&submit),reinterpret_cast<void*>(&copy_texture),
            reinterpret_cast<void*>(&mapped_frame),reinterpret_cast<void*>(&begin_capture)};
        std::array<void*,4> previous{};
        for (std::size_t i=0;i<targets.size();++i) {
            if (hook_prepare(targets[i],detours[i],&previous[i])!=HookOk) throw std::runtime_error("Capture hook prepare failed");
            ++prepared;
        }
        original=reinterpret_cast<Submit>(previous[0]); original_copy=reinterpret_cast<CopyTexture>(previous[1]);
        original_mapped=reinterpret_cast<MappedFrame>(previous[2]); original_begin=reinterpret_cast<BeginCapture>(previous[3]);
        for (auto* target : targets)
            if (hook_queue_enable(target)!=HookOk) throw std::runtime_error("Capture hook queue failed");
        if (hook_apply_queued()!=HookOk) throw std::runtime_error("Capture hook attach failed");
        return true;
    } catch (...) {
        while (prepared) hook_remove(targets[--prepared]);
        return false;
    }
}
} // namespace dingosdk::replay_export
