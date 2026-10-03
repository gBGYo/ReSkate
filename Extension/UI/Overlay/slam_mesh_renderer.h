#pragma once
#include <d3d12.h>
#include <cstddef>
#include <string_view>
namespace dingosdk::overlay::detail {
// Called on the serialized presentation thread, after the current frame's
// GPU fence has completed. Own depth/upload resources; no game depth changes.
void render_slam_mesh(ID3D12Device* device,ID3D12GraphicsCommandList* commands,
    D3D12_CPU_DESCRIPTOR_HANDLE target,DXGI_FORMAT format,UINT width,UINT height,
    std::size_t frame,std::size_t frame_count) noexcept;
void clear_slam_mesh_renderer() noexcept;
std::string_view slam_mesh_renderer_status() noexcept;
}
