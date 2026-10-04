#pragma once
#include <array>
#include <cstdint>

// The replay editor's video export (analysis/replay-editor.md).
namespace dingosdk::game::build::v20260929::replay_export {
// The movie encoder consumes a mapped capture-ring frame synchronously. The
// pixels belong to an earlier scene submission, not the latest raster pose.
inline constexpr std::uintptr_t submit_frame = 0x34f4610;
inline constexpr std::array<unsigned char, 24> submit_frame_prefix{
    0x40,0x57,0x48,0x81,0xec,0x50,0x08,0x00,0x00,0x48,0x8b,0x05,0xa0,0xfd,0xcc,0x03,
    0x48,0x33,0xc4,0x48,0x89,0x84,0x24,0x20};
// The encoder the replay editor exports with; the engine has others.
inline constexpr std::uintptr_t replay_encoder_vtable = 0x608c0b0;
// Encoder byte: a frame may be encoded. A world update sets it and an encoded
// frame clears it; submit_frame drops the frame it is called with while clear.
inline constexpr std::uintptr_t frame_permit = 0x90;
// 34f3540 copies configuration height (+8) and width (+10) into the
// encoder at +10/+18. 34f5350 maps those into codec width(+4)/height(+8);
// 34ed530/34ebcd0 consume them in that order. 34f4600 forwards mapped
// pixels and byte row pitch.
// 34e0a60's YUV conversion reads B,G,R at +0,+1,+2 with a four-byte stride.
inline constexpr std::uintptr_t frame_width = 0x18, frame_height = 0x10;
// 34f6290 queues a GPU texture copy only after checking the capture lease,
// encoder state, frame acceptance and matching source/target dimensions.
// 40c7840 records that copy command (commands, destination, source).
inline constexpr std::uintptr_t copy_texture=0x40c7840, capture_copy_return=0x34f63e8;
inline constexpr std::array<unsigned char,24> copy_texture_prefix{
    0x40,0x53,0x55,0x56,0x57,0x48,0x83,0xec,0x28,0x48,0x8b,0x41,
    0x18,0x48,0x8d,0x59,0x08,0x49,0x8b,0xf8,0x48,0x8b,0xf2,0xbd};
// 34f4360 advances the ring and drains +ac. The callback receives a copied
// slot header: byte +0 is frame acceptance, bytes +4..+51 are slot metadata.
inline constexpr std::uintptr_t mapped_frame=0x34f4600;
inline constexpr std::array<unsigned char,14> mapped_frame_prefix{
    0x49,0x8b,0x10,0x48,0x8b,0x01,0x45,0x8b,0x40,0x08,0x48,0xff,0x60,0x50};
// A new encoder lease starts a new capture generation, even when allocator
// addresses or the replay session are reused by a restarted export.
inline constexpr std::uintptr_t begin_capture=0x34f5e00;
inline constexpr std::array<unsigned char,24> begin_capture_prefix{
    0x40,0x53,0x48,0x83,0xec,0x30,0x48,0x8b,0x02,0x48,0x8b,0xda,
    0x48,0x89,0x44,0x24,0x20,0x48,0x83,0xc1,0x68,0x48,0x8b,0x42};
inline constexpr std::uintptr_t capture_manager=0x7631998;
inline constexpr std::uintptr_t capture_ring=0x40, capture_lease=0x68;
inline constexpr std::uintptr_t capture_slots=0xa4, capture_write=0xa8, capture_read=0xac;
inline constexpr std::uintptr_t capture_slot_stride=0x40, capture_metadata=0xc;
}
