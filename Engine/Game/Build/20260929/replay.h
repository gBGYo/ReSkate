#pragma once
#include "Engine/Game/Build/fingerprint.h"

namespace dingosdk::game::build::v20260929::replay {
// Supported SHA-256 fbce74d5e28ef525dbba2cb4adbebc13405bdbd88f31bc940bca45e4ae88b8f9.
// Client ctor 50c1f0 publishes the singleton and creates the session manager
// at +568 with backend +50 (vtable 6084518). 6240e0/6241e0 install a lease
// at manager+8; the lease's +10 points back to that manager.
inline constexpr std::uintptr_t client = 0x71eaba8, manager = 0x568;
inline constexpr std::uintptr_t backend = 0x50, backend_vtable = 0x6084518;
inline constexpr std::uintptr_t lease = 8, lease_manager = 0x10, lease_vtable = 0x60ca700;
// Backend +88 -> 6123b0 -> 41a6960 reads the playhead at 718ffd0 when
// 7726468 is nonzero. Backend +90 -> 6254b0 -> 41a9f40 writes this time.
inline constexpr std::uintptr_t playing = 0x7726468, playhead = 0x718ffd0;
// Backend +78 -> 610710 -> 41a6770 computes recording time from the last
// 40-byte interval: double start + unsigned count(+20) * double step(+10).
// The interval vector is recorder+30/+38. 41a6820 uses the same recorder.
inline constexpr std::uintptr_t recorder = 0x7726450, stream = 0x77264e0;
inline constexpr std::uintptr_t intervals_begin = 0x30, intervals_end = 0x38;
inline constexpr std::size_t interval_stride = 40;
// Video export uses a separate sequence cursor while leaving the editor
// playhead parked. 61bce8 -> 41aa2d0 installs 24-byte (start,end,step)
// segments. 41a7738 reads this cursor; 41a7c10/41a7c54 advance it and
// 41a7c8a restores StateStream at that time without writing the playhead.
// State 1 is exporting; state 2 retains the last frame until 41aa3c0
// acknowledges completion and clears the 32-byte header.
inline constexpr std::uintptr_t export_sequence = 0x77264e8;
inline constexpr std::size_t export_header_size = 32;
inline constexpr std::size_t export_count = 8, export_time = 16, export_index = 24, export_status = 28;
// 48d46f0 constructs a skinned instance with source record at instance+20.
// Live-verified backlink: source+38 points to that instance. Both the live and reconstructed
// source records retain the same 64-bit StateStream key at source+40
// (48b29b0 registration). The instance's +108 allocation index changes in
// replay; it is not the recorded actor's logical handle.
inline constexpr std::uintptr_t skinned_state_vtable = 0x65f27f8;
inline constexpr std::uintptr_t instance_source = 0x20, source_instance = 0x38, source_key = 0x40;
inline constexpr std::size_t skinned_state_stride = 96;
inline constexpr Fingerprint playback_clock{0x41a6960, {
    0xc5,0xfb,0x10,0x15,0x20,0x50,0xf2,0x01,0x33,0xc0,0xc4,0xe1,0xf9,0x6e,0xc8,0x0f,
    0xb6,0x05,0xf2,0xfa,0x57,0x03,0xc4,0xe1,0xf9,0x6e,0xc0,0xc4,0xe2,0x79,0x29,0xd9}};
inline constexpr Fingerprint recording_clock{0x41a6770, {
    0x40,0x53,0x48,0x83,0xec,0x30,0x48,0x8b,0x1d,0xd3,0xfc,0x57,0x03,0x48,0x8b,0xcb,
    0xc5,0xf8,0x29,0x74,0x24,0x20,0xe8,0xf5,0x95,0x3e,0xfd,0x48,0x8b,0x05,0xbe,0xfc}};
inline constexpr Fingerprint export_clock{0x41a7738, {
    0xc5,0x7b,0x10,0x0d,0xb8,0xed,0x57,0x03,0x48,0xc7,0xc6,0xff,0xff,0xff,0xff,0x48,
    0x8b,0xd6,0xf0,0x48,0x0f,0xc1,0x15,0x0d,0xed,0x57,0x03,0x48,0xf7,0xc2,0xfc,0xff}};
inline constexpr Fingerprint export_advance{0x41a7bc4, {
    0x0f,0xb6,0x05,0x39,0xe9,0x57,0x03,0x3c,0x02,0x0f,0x84,0xbc,0x00,0x00,0x00,0x3c,
    0x01,0x0f,0x85,0xb4,0x00,0x00,0x00,0xc5,0xfb,0x10,0x15,0x15,0xe9,0x57,0x03,0xc4}};
}
