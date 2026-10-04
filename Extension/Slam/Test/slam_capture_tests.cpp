#include "Extension/UI/Overlay/slam_mesh_renderer.h"
#include "Extension/Rendering/replay_capture_queue.h"
#include "Extension/Slam/slam_runtime.h"
#include <Windows.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <iostream>
#include <vector>

// Capture renders the explicitly supplied, immutable frame. These presentation
// entry points intentionally supply no scene, detecting accidental resampling.
namespace dingosdk::slam {
Snapshot presentation_snapshot() {return {};}
bool visuals_visible() noexcept {return true;}
}
namespace {
using Microsoft::WRL::ComPtr;
using namespace dingosdk;
int failures{};
void check(bool value,const char* message) {
    if (!value) {++failures; std::cerr<<"FAIL: "<<message<<'\n';}
}
void pixels() {
    using namespace replay_export;
    check(!bgra_frame(0,2,8) && !bgra_frame(8193,2,32772) && !bgra_frame(2,2,7) &&
        !bgra_frame(2,2,UINTPTR_MAX),"Invalid capture dimensions and pitches are rejected");
    const auto frame=*bgra_frame(2,2,12);
    std::vector<std::uint8_t> video(24,17),layer(32,0);
    for (unsigned at : {0u,4u,12u,16u}) {
        video[at]=10; video[at+1]=20; video[at+2]=30; video[at+3]=201;
    }
    const auto before=video;
    layer[4+2]=128; layer[4+3]=128; // half-opacity red, premultiplied
    layer[16]=255; layer[16+3]=255; // opaque blue on row two
    check(composite_bgra(video,frame,layer,16),"Padded video and layer rows can be composited");
    check(video[0]==10 && video[1]==20 && video[2]==30,"Transparent pixels preserve captured video");
    check(video[4]==5 && video[5]==10 && video[6]==143,"Partial alpha is applied once with BGRA channel order");
    check(video[12]==255 && video[13]==0 && video[14]==0,"Opaque pixels replace RGB on the correct row");
    check(video[3]==201 && video[7]==201 && video[15]==201 && video[19]==201,"Video alpha remains unchanged");
    check(video[8]==17 && video[11]==17 && video[20]==17 && video[23]==17,"Row padding remains unchanged");
    video=before;
    check(!composite_bgra(video,frame,{layer.data(),10},16) && video==before,"Truncated layers cannot partially modify a video frame");
}
slam::Snapshot scene() {
    slam::Snapshot value;
    auto mesh=std::make_shared<slam::SkeletonMesh>();
    mesh->rig.inverse_bind.fill(slam::identity_pose);
    mesh->rig.parents.fill(-1);
    for (const slam::Vec3 position : {slam::Vec3{-.6f,-.6f,-2},slam::Vec3{0,.6f,-2},slam::Vec3{.6f,-.6f,-2}}) {
        slam::MeshVertex vertex; vertex.position=position; vertex.normal={0,0,1}; vertex.weights[0]=255;
        mesh->vertices.push_back(vertex);
    }
    mesh->indices={0,1,2}; value.mesh=mesh;
    auto pose=std::make_shared<slam::MeshPose>(); pose->skin.fill(slam::identity_pose);
    pose->render_camera=slam::RenderCamera{slam::identity_pose,60};
    // GPU/shader preparation may outlast the normal presentation lease. A
    // frame already validated by the export caller must remain unchanged.
    pose->at_ms=GetTickCount64()-1000; value.mesh_pose=pose;
    value.replay_active=value.replay_available=true; value.visuals.replay=true;
    value.visuals.opacity=.5f; value.visuals.reduced_effects=true;
    value.replay_result.phase=slam::Phase::bailed; value.replay_time_ms=1001;
    return value;
}
std::vector<std::uint8_t> background(const replay_export::BgraFrame& frame) {
    std::vector<std::uint8_t> result(frame.bytes,17);
    for (std::size_t y=0;y<frame.height;++y)
        for (std::size_t x=0;x<frame.width;++x) {
            const auto at=y*frame.pitch+x*4;
            result[at]=24; result[at+1]=40; result[at+2]=60; result[at+3]=255;
        }
    return result;
}
replay_export::CaptureKey key(std::uint32_t slot) {
    replay_export::CaptureKey result{0x10000,0x20000,0x30000,0x40000,0x50000+slot,slot};
    result.metadata[0]=static_cast<std::byte>(slot);
    return result;
}
void delayed_frames(ID3D12Device* device,const slam::Snapshot& reference) {
    auto storage=std::make_unique<replay_export::CaptureQueue<slam::Snapshot>>();
    auto& queue=*storage;
    const auto frame=*replay_export::bgra_frame(128,128,512);
    auto early=reference,latest=reference;
    early.replay_time_ms=100; latest.replay_time_ms=900;
    early.replay_fracture_seeds[0]=12; latest.replay_fracture_seeds[0]=99;
    auto moved=std::make_shared<slam::MeshPose>(*latest.mesh_pose);
    moved->skin[0][12]=.8f;
    moved->render_camera->world[12]=-.4f;
    latest.mesh_pose=moved;
    // The game queues several frames while older readbacks reach the encoder.
    // This includes camera movement and a discontinuity in the replay clock.
    queue.record(key(0),early);
    for (std::uint32_t slot=1;slot<6;++slot) queue.record(key(slot),latest);
    auto encoded=queue.take(key(0));
    check(encoded && encoded->mesh_pose==early.mesh_pose && encoded->replay_time_ms==100 &&
        encoded->replay_fracture_seeds[0]==12,"Delayed readback retains its original pose, camera and injury time");
    auto expected=background(frame),actual=expected,newer=expected;
    check(overlay::detail::capture_slam_mesh(device,early,expected,frame),"Reference capture renders its original scene");
    check(encoded && overlay::detail::capture_slam_mesh(device,*encoded,actual,frame) && actual==expected,
        "Queued video frame uses the matching skeleton pixels after player and camera move");
    check(overlay::detail::capture_slam_mesh(device,latest,newer,frame) && newer!=actual,
        "Using the newest pose would produce visibly different, displaced skeleton pixels");
    check(!queue.take(key(0)),"A submitted slot cannot reuse an old scene twice");
    auto next=queue.take(key(5));
    check(next && next->replay_time_ms==900 && next->mesh_pose==latest.mesh_pose,
        "Different capture slots retain their own frames independently of drain order");
    queue.record(key(0),early); queue.record(key(0),latest);
    next=queue.take(key(0));
    check(next && next->replay_time_ms==900,"Recycled slots replace the previous pose");
    for (unsigned field=0;field<7;++field) {
        queue.record(key(0),early);
        auto mismatch=key(0);
        switch (field) {
        case 0: ++mismatch.manager; break;
        case 1: ++mismatch.encoder; break;
        case 2: ++mismatch.lease; break;
        case 3: ++mismatch.ring; break;
        case 4: ++mismatch.resource; break;
        case 5: mismatch.metadata[0]=std::byte{42}; break;
        case 6: mismatch.slot=64; break;
        }
        check(!queue.take(mismatch),"Unmatched buffers, encoders and metadata never borrow a different frame's scene");
        queue.clear();
    }
    queue.record(key(0),early);
    queue.clear();
    check(!queue.take(key(0)),"Restarting export clears frames even when native addresses are reused");
}
void gpu() {
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<ID3D12Device> device;
    const bool ready=SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) &&
        SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter))) &&
        SUCCEEDED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device)));
    check(ready,"D3D12 software adapter initializes for capture regression");
    if (!ready) return;
    auto value=scene();
    delayed_frames(device.Get(),value);
    for (const auto frame : {*replay_export::bgra_frame(128,128,544),*replay_export::bgra_frame(96,64,400)}) {
        auto video=background(frame); const auto before=video;
        check(overlay::detail::capture_slam_mesh(device.Get(),value,video,frame),"X-ray is rendered and copied into the encoder frame before returning");
        const auto center=static_cast<std::size_t>(frame.height/2)*frame.pitch+frame.width/2*4;
        check(video[center]>before[center] && video[center+1]>before[center+1] && video[center+2]>before[center+2],
            "Encoded pixels contain the skeleton at the capture resolution");
        check(video[0]==before[0] && video[1]==before[1] && video[2]==before[2] && video[center+3]==255,
            "Capture preserves the background and alpha outside the X-ray");
        check(video[frame.width*4]==17,"GPU readback composition preserves padded native rows");
        value.visuals.only_fractured=true;
        video=before;
        check(overlay::detail::capture_slam_mesh(device.Get(),value,video,frame) && video==before,
            "Replay bone filters also apply to encoded video");
        value.visuals.only_fractured=false;
        value.visuals.replay=false;
        check(!overlay::detail::capture_slam_mesh(device.Get(),value,video,frame) && video==before,
            "Disabling replay X-rays leaves encoded video unchanged");
        value.visuals.replay=true;
    }
    ComPtr<ID3D12InfoQueue> info;
    if (SUCCEEDED(device.As(&info))) {
        for (UINT64 i=0;i<info->GetNumStoredMessagesAllowedByRetrievalFilter();++i) {
            SIZE_T bytes{}; info->GetMessage(i,nullptr,&bytes);
            std::vector<std::byte> storage(bytes);
            auto* message=reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            if (SUCCEEDED(info->GetMessage(i,message,&bytes)))
                check(message->Severity!=D3D12_MESSAGE_SEVERITY_ERROR && message->Severity!=D3D12_MESSAGE_SEVERITY_CORRUPTION,
                    message->pDescription);
        }
    }
}
}
int main() {
    pixels(); gpu();
    if (!failures) std::cout<<"BGRA composition and GPU X-ray encoder-frame capture passed.\n";
    return failures ? 1 : 0;
}
