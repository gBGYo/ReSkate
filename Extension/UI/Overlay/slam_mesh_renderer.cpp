#include "slam_mesh_renderer.h"
#include "Extension/Slam/slam_runtime.h"
#include "Engine/Game/UI/live_game_view.h"
#include "Engine/Core/Log/logging.h"
#include <Windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstring>
#include <future>
#include <limits>
#include <stdexcept>
#include <vector>

namespace dingosdk::overlay::detail {
namespace {
using Microsoft::WRL::ComPtr;
constexpr char shader[]=R"(
cbuffer Scene : register(b0) {
    row_major float4x4 skin[386];
    row_major float4x4 camera;
    float4 projection; // focal x/y, near, far
    float4 colors[395];
    float4 damage[395];
    float4 fracturePlanes[395];
};
struct Vertex {
    float3 position : POSITION; float3 normal : NORMAL;
    uint4 bones0 : BLENDINDICES0; uint4 bones1 : BLENDINDICES1;
    float4 weights0 : BLENDWEIGHT0; float4 weights1 : BLENDWEIGHT1;
    uint region : REGION;
    uint part : PART;
};
struct Pixel { float4 position : SV_POSITION; float3 normal : NORMAL; float3 bindPosition : TEXCOORD0; nointerpolation uint part : PART; };
Pixel vs(Vertex input) {
    float4 world=0; float3 normal=0;
    [unroll] for (uint i=0;i<4;++i) {
        world+=mul(float4(input.position,1),skin[input.bones0[i]])*input.weights0[i];
        world+=mul(float4(input.position,1),skin[input.bones1[i]])*input.weights1[i];
        normal+=mul(float4(input.normal,0),skin[input.bones0[i]]).xyz*input.weights0[i];
        normal+=mul(float4(input.normal,0),skin[input.bones1[i]]).xyz*input.weights1[i];
    }
    float4 view=mul(world,camera);
    float depth=-view.z;
    Pixel output;
    output.position=float4(view.x*projection.x,view.y*projection.y,
        (depth*projection.w-projection.z*projection.w)/(projection.w-projection.z),depth);
    output.normal=mul(float4(normal,0),camera).xyz;
    output.bindPosition=input.position;
    output.part=min(input.part,394); return output;
}
float4 ps(Pixel input) : SV_TARGET {
    float3 normal=normalize(input.normal);
    float diffuse=abs(dot(normal,normalize(float3(-.4,.7,1))));
    float rim=pow(1-abs(normal.z),2);
    float4 color=colors[input.part];
    clip(color.a-.001);
    float seed=float(input.part)*2.31;
    // Stable bind-space patches preserve the ivory bone surface while
    // concentrating the injury color rather than coating the entire bone.
    float broad=.5+.5*sin(dot(input.bindPosition,float3(17,23,19))+seed);
    float detail=.5+.5*sin(dot(input.bindPosition,float3(53,41,61))-seed);
    float patch=smoothstep(.22,.8,broad*.7+detail*.3);
    float tint=(.08+.78*patch)*damage[input.part].z;
    float3 bone=lerp(float3(.96,.94,.87),color.rgb,tint);
    float3 surface=bone*(.4+.58*diffuse)+float3(.12,.16,.18)*rim;
    // Each fracture's stable jagged plane in body bind space follows the
    // same skinning as its bone. It never displaces the synchronized pose.
    float jagged=.0025*sin(dot(input.bindPosition,float3(113,167,137))+seed)
        +.0015*sin(dot(input.bindPosition,float3(251,179,223))-seed);
    float seam=abs(dot(float4(input.bindPosition,1),fracturePlanes[input.part])+jagged);
    float width=max(fwidth(seam)*1.2,.0015);
    // A visible split in the X-ray surface opens once at the fracture,
    // then settles. Reduced effects keeps the steady crack instead.
    float opening=(.0015+.005*damage[input.part].w)*damage[input.part].x;
    if (opening>0) clip(seam-opening);
    float fractureDistance=max(0,seam-opening);
    float crack=(1-smoothstep(width,width*2.5,fractureDistance))*damage[input.part].x;
    float edge=(1-smoothstep(width*2.5,width*5,fractureDistance))*damage[input.part].x;
    surface=lerp(surface,float3(.035,.008,.012),crack*.95);
    surface+=float3(.32,.06,.025)*max(0,edge-crack);
    surface+=float3(1,.92,.78)*damage[input.part].y;
    return float4(surface,color.a);
}
)";
struct Constants {
    std::array<slam::PoseMatrix,slam::render_bone_count> skin;
    slam::PoseMatrix camera;
    std::array<float,4> projection;
    std::array<std::array<float,4>,slam::injury_bone_count> colors;
    std::array<std::array<float,4>,slam::injury_bone_count> damage, fracture_planes;
};
struct FrameResources {
    ComPtr<ID3D12Resource> constants,depth;
    UINT width{},height{};
};
struct Renderer {
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline,depth_pipeline;
    ComPtr<ID3D12Resource> vertices,indices;
    ComPtr<ID3D12DescriptorHeap> depths;
    std::vector<FrameResources> frames;
    D3D12_VERTEX_BUFFER_VIEW vertex_view{};
    D3D12_INDEX_BUFFER_VIEW index_view{};
    std::shared_ptr<const slam::SkeletonMesh> mesh;
    std::array<std::array<float,4>,slam::injury_bone_count> fracture_planes{};
    std::array<slam::Vec3,slam::injury_bone_count> fracture_low{},fracture_high{};
    std::array<bool,slam::injury_bone_count> fracture_used{};
    std::array<std::uint64_t,slam::injury_bone_count> fracture_cached_at{};
    std::future<std::shared_ptr<Renderer>> preparation;
    std::string status;
    bool failed{},drawn{};
    std::uint64_t diagnostic_at{};
};
Renderer& renderer() {static auto* value=new Renderer; return *value;}
void check(HRESULT result,const char* message) {if (FAILED(result)) throw std::runtime_error(message);}
ComPtr<ID3DBlob> compile(const char* entry,const char* target) {
    ComPtr<ID3DBlob> result,error;
    const auto hr=D3DCompile(shader,sizeof(shader)-1,"Slam X-ray",nullptr,nullptr,entry,target,
        D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&result,&error);
    if (FAILED(hr)) throw std::runtime_error(error ? std::string(static_cast<const char*>(error->GetBufferPointer()),error->GetBufferSize()) : "Cannot compile X-ray shader");
    return result;
}
ComPtr<ID3D12Resource> upload(ID3D12Device* device,std::size_t bytes,const void* data=nullptr) {
    D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width=bytes; desc.Height=1;
    desc.DepthOrArraySize=1; desc.MipLevels=1; desc.SampleDesc.Count=1; desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> result;
    check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&result)),"Cannot allocate X-ray upload buffer");
    if (data) {
        void* destination{}; const D3D12_RANGE empty{};
        check(result->Map(0,&empty,&destination),"Cannot map X-ray upload buffer");
        std::memcpy(destination,data,bytes); result->Unmap(0,nullptr);
    }
    return result;
}
void setup(Renderer& r,ID3D12Device* device,DXGI_FORMAT format,std::size_t frames,std::shared_ptr<const slam::SkeletonMesh> mesh) {
    D3D12_ROOT_PARAMETER parameter{}; parameter.ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV;
    parameter.Descriptor.ShaderRegister=0; parameter.ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC root{}; root.NumParameters=1; root.pParameters=&parameter;
    root.Flags=D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> signature,error;
    check(D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&error),"Cannot serialize X-ray root signature");
    check(device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&r.root)),"Cannot create X-ray root signature");
    const auto vs=compile("vs","vs_5_1"),ps=compile("ps","ps_5_1");
    const D3D12_INPUT_ELEMENT_DESC input[]={
        {"POSITION",0,DXGI_FORMAT_R32G32B32_FLOAT,0,0,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"NORMAL",0,DXGI_FORMAT_R32G32B32_FLOAT,0,12,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"BLENDINDICES",0,DXGI_FORMAT_R16G16B16A16_UINT,0,24,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"BLENDINDICES",1,DXGI_FORMAT_R16G16B16A16_UINT,0,32,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"BLENDWEIGHT",0,DXGI_FORMAT_R8G8B8A8_UNORM,0,40,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"BLENDWEIGHT",1,DXGI_FORMAT_R8G8B8A8_UNORM,0,44,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"REGION",0,DXGI_FORMAT_R32_UINT,0,48,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0},
        {"PART",0,DXGI_FORMAT_R32_UINT,0,52,D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,0}};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pipeline{};
    pipeline.pRootSignature=r.root.Get(); pipeline.VS={vs->GetBufferPointer(),vs->GetBufferSize()};
    pipeline.PS={ps->GetBufferPointer(),ps->GetBufferSize()}; pipeline.InputLayout={input,static_cast<UINT>(std::size(input))};
    pipeline.SampleMask=UINT_MAX; pipeline.SampleDesc.Count=1; pipeline.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pipeline.NumRenderTargets=1; pipeline.RTVFormats[0]=format; pipeline.DSVFormat=DXGI_FORMAT_D32_FLOAT;
    pipeline.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID; pipeline.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;
    pipeline.RasterizerState.DepthClipEnable=TRUE;
    auto& blend=pipeline.BlendState.RenderTarget[0]; blend.BlendEnable=TRUE;
    blend.SrcBlend=D3D12_BLEND_SRC_ALPHA; blend.DestBlend=D3D12_BLEND_INV_SRC_ALPHA; blend.BlendOp=D3D12_BLEND_OP_ADD;
    blend.SrcBlendAlpha=D3D12_BLEND_ONE; blend.DestBlendAlpha=D3D12_BLEND_INV_SRC_ALPHA; blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;
    blend.LogicOp=D3D12_LOGIC_OP_NOOP; blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    pipeline.DepthStencilState.DepthEnable=TRUE; pipeline.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ALL;
    pipeline.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_LESS;
    pipeline.DepthStencilState.FrontFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};
    pipeline.DepthStencilState.BackFace=pipeline.DepthStencilState.FrontFace;
    // Establish the nearest skeleton surface first, then blend that surface
    // once. Transparency must not depend on which body section was drawn first.
    blend.BlendEnable=FALSE; blend.RenderTargetWriteMask=0;
    check(device->CreateGraphicsPipelineState(&pipeline,IID_PPV_ARGS(&r.depth_pipeline)),"Cannot create X-ray depth pipeline");
    blend.BlendEnable=TRUE; blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
    pipeline.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;
    pipeline.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_EQUAL;
    check(device->CreateGraphicsPipelineState(&pipeline,IID_PPV_ARGS(&r.pipeline)),"Cannot create X-ray color pipeline");
    const auto vertex_bytes=mesh->vertices.size()*sizeof(slam::MeshVertex),index_bytes=mesh->indices.size()*sizeof(std::uint32_t);
    r.vertices=upload(device,vertex_bytes,mesh->vertices.data()); r.indices=upload(device,index_bytes,mesh->indices.data());
    r.vertex_view={r.vertices->GetGPUVirtualAddress(),static_cast<UINT>(vertex_bytes),sizeof(slam::MeshVertex)};
    r.index_view={r.indices->GetGPUVirtualAddress(),static_cast<UINT>(index_bytes),DXGI_FORMAT_R32_UINT};
    D3D12_DESCRIPTOR_HEAP_DESC depths{}; depths.Type=D3D12_DESCRIPTOR_HEAP_TYPE_DSV; depths.NumDescriptors=static_cast<UINT>(frames);
    check(device->CreateDescriptorHeap(&depths,IID_PPV_ARGS(&r.depths)),"Cannot create X-ray depth descriptors");
    r.frames.resize(frames);
    for (auto& frame : r.frames) frame.constants=upload(device,(sizeof(Constants)+255)&~std::size_t{255});
    // Cache actual bone extents once. Each fracture then chooses a stable
    // interior location/angle within its own geometry, independent of pose.
    std::array<slam::Vec3,slam::injury_bone_count> low{},high{};
    std::array<bool,slam::injury_bone_count> used{};
    for (auto& point : low) point.fill(std::numeric_limits<float>::max());
    for (auto& point : high) point.fill(std::numeric_limits<float>::lowest());
    for (const auto& vertex : mesh->vertices) {
        const auto part=vertex.part;
        if (part>=slam::render_bone_count) continue;
        const auto local=slam::model_to_world(mesh->rig.inverse_bind[part],vertex.position);
        used[part]=true;
        for (std::size_t axis=0;axis<3;++axis) {
            low[part][axis]=std::min(low[part][axis],local[axis]);
            high[part][axis]=std::max(high[part][axis],local[axis]);
        }
    }
    r.fracture_planes.fill({0,0,0,1});
    for (std::size_t part=0;part<slam::render_bone_count;++part) {
        if (!used[part]) continue;
        const auto plane=slam::make_fracture_plane(mesh->rig.inverse_bind[part],low[part],high[part],{});
        if (plane) r.fracture_planes[part]=*plane;
    }
    r.fracture_low=low; r.fracture_high=high; r.fracture_used=used;
    r.mesh=std::move(mesh); r.status="3D X-ray ready.";
    logging::write(logging::Level::info,logging::Channel::graphics,"Slam X-ray: 3D pipeline ready.");
}
slam::PoseMatrix camera_matrix(const slam::PoseMatrix& camera) {
    slam::PoseMatrix inverse=slam::identity_pose;
    for (std::size_t row=0;row<3;++row)
        for (std::size_t axis=0;axis<3;++axis) inverse[row*4+axis]=camera[axis*4+row];
    for (std::size_t axis=0;axis<3;++axis)
        inverse[12+axis]=-(camera[12]*inverse[axis]+camera[13]*inverse[4+axis]+camera[14]*inverse[8+axis]);
    return inverse;
}
}
std::string_view slam_mesh_renderer_status() noexcept {return renderer().status;}
void clear_slam_mesh_renderer() noexcept {renderer()=Renderer{};}
void render_slam_mesh(ID3D12Device* device,ID3D12GraphicsCommandList* commands,D3D12_CPU_DESCRIPTOR_HANDLE target,
    DXGI_FORMAT format,UINT width,UINT height,std::size_t frame_index,std::size_t frame_count) noexcept {
    auto& r=renderer();
    if (r.failed || !slam::visuals_visible() || !width || !height || frame_index>=frame_count) return;
    try {
        auto value=slam::presentation_snapshot();
        if (!value.mesh) return;
        // Shader/PSO preparation may take seconds on the first run. Keep it
        // off Present and start as soon as the mesh is loaded, before a fall.
        // These immutable D3D12 resources have never been submitted to a GPU.
        if (!r.pipeline) {
            if (!r.preparation.valid()) {
                r.status="Preparing 3D X-ray...";
                ComPtr<ID3D12Device> owned_device=device;
                r.preparation=std::async(std::launch::async,[owned_device,format,frame_count,mesh=value.mesh] {
                    auto prepared=std::make_shared<Renderer>();
                    setup(*prepared,owned_device.Get(),format,frame_count,mesh);
                    return prepared;
                });
            }
            if (r.preparation.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready) return;
            auto prepared=r.preparation.get();
            r=std::move(*prepared);
            // Preparation must not cause the first mesh draw to use the pose
            // copied before compilation started.
            value=slam::presentation_snapshot();
        }
        const bool normal_play=value.visuals.normal_play && value.normal_xray_available;
        if ((!normal_play && (!value.visible || !value.xray_context_valid || value.result.cancelled)) || !value.mesh || !value.mesh_pose ||
            GetTickCount64()-value.mesh_pose->at_ms>=250) return;
        const auto appearance=slam::visual_appearance(value.visuals,
            normal_play ? value.normal_xray_result : value.result,
            normal_play ? value.normal_xray_events : value.visual_events,GetTickCount64(),value.first_person,normal_play);
        if (appearance.opacity<=.001f) return;
        slam::RenderCamera view;
        if (value.mesh_pose->render_camera) {
            // The main raster submission captured both values together.
            // Reading a newer CPU camera here breaks that pairing in motion.
            view=*value.mesh_pose->render_camera;
        } else {
            auto fallback=latest_game_view();
            if (!fallback || !refresh_game_view(value.image_base,*fallback)) return;
            view.world=fallback->world; view.vertical_fov=fallback->vertical_fov;
        }
        if (r.mesh!=value.mesh || r.frames.size()!=frame_count) return;
        auto& frame=r.frames[frame_index];
        auto depth=r.depths->GetCPUDescriptorHandleForHeapStart();
        depth.ptr+=frame_index*device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        if (!frame.depth || frame.width!=width || frame.height!=height) {
            D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_DEFAULT;
            D3D12_RESOURCE_DESC desc{}; desc.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width=width; desc.Height=height; desc.DepthOrArraySize=1; desc.MipLevels=1;
            desc.Format=DXGI_FORMAT_D32_FLOAT; desc.SampleDesc.Count=1; desc.Flags=D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            D3D12_CLEAR_VALUE clear{}; clear.Format=DXGI_FORMAT_D32_FLOAT; clear.DepthStencil.Depth=1;
            frame.depth.Reset();
            check(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_DEPTH_WRITE,&clear,IID_PPV_ARGS(&frame.depth)),"Cannot allocate X-ray depth target");
            device->CreateDepthStencilView(frame.depth.Get(),nullptr,depth); frame.width=width; frame.height=height;
        }
        Constants constants;
        constants.skin=value.mesh_pose->skin; constants.camera=camera_matrix(view.world);
        const auto focal=1/std::tan(view.vertical_fov*3.14159265f/360);
        constants.projection={focal*static_cast<float>(height)/static_cast<float>(width),focal,.05f,1000};
        constants.colors=appearance.colors;
        const auto& fractures=normal_play ? value.normal_xray_events.fractures_at_ms : value.visual_events.fractures_at_ms;
        for (std::size_t part=0;part<slam::render_bone_count;++part) {
            if (!r.fracture_used[part] || !fractures[part] || fractures[part]==r.fracture_cached_at[part]) continue;
            const auto plane=slam::make_fracture_plane(r.mesh->rig.inverse_bind[part],r.fracture_low[part],r.fracture_high[part],
                slam::varied_fracture_location(static_cast<unsigned>(part),fractures[part]));
            if (plane) {r.fracture_planes[part]=*plane; r.fracture_cached_at[part]=fractures[part];}
        }
        constants.damage=appearance.damage; constants.fracture_planes=r.fracture_planes;
        void* destination{}; const D3D12_RANGE empty{};
        check(frame.constants->Map(0,&empty,&destination),"Cannot update X-ray pose");
        std::memcpy(destination,&constants,sizeof(constants)); frame.constants->Unmap(0,nullptr);
        commands->SetPipelineState(r.depth_pipeline.Get()); commands->SetGraphicsRootSignature(r.root.Get());
        commands->SetGraphicsRootConstantBufferView(0,frame.constants->GetGPUVirtualAddress());
        const D3D12_VIEWPORT viewport{0,0,static_cast<float>(width),static_cast<float>(height),0,1};
        const D3D12_RECT scissor{0,0,static_cast<LONG>(width),static_cast<LONG>(height)};
        commands->RSSetViewports(1,&viewport); commands->RSSetScissorRects(1,&scissor);
        commands->OMSetRenderTargets(1,&target,FALSE,&depth);
        commands->ClearDepthStencilView(depth,D3D12_CLEAR_FLAG_DEPTH,1,0,0,nullptr);
        commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        commands->IASetVertexBuffers(0,1,&r.vertex_view); commands->IASetIndexBuffer(&r.index_view);
        commands->DrawIndexedInstanced(static_cast<UINT>(r.mesh->indices.size()),1,0,0,0);
        commands->SetPipelineState(r.pipeline.Get());
        commands->DrawIndexedInstanced(static_cast<UINT>(r.mesh->indices.size()),1,0,0,0);
        commands->OMSetRenderTargets(1,&target,FALSE,nullptr);
        if (!r.drawn) {
            r.drawn=true; r.status="Dem Bones 3D X-ray rendering.";
            logging::write(logging::Level::info,logging::Channel::graphics,"Slam X-ray: first skinned mesh draw submitted.");
        }
        const auto now=GetTickCount64();
        if (now>=r.diagnostic_at) {
            r.diagnostic_at=now+5000;
            logging::log(logging::Level::info,logging::Channel::graphics,
                "X-ray pose timing: {}ms old, sequence {}, source {}, exports {}, evaluations {}, superseded {}, rejected {}.",
                now-value.mesh_pose->at_ms,value.mesh_pose->sequence,value.mesh_pose->render_camera ? "raster pose and camera" : value.mesh_pose->presentation_read ? "native live buffer" : value.mesh_pose->native_draw ? "native draw" :
                    value.mesh_pose->render_export ? "animation export" : "animation evaluation",
                value.export_poses,value.animation_poses,value.superseded_poses,value.rejected_poses);
        }
    } catch (const std::exception& error) {
        r.failed=true; r.status=std::string("3D X-ray unavailable: ")+error.what();
        logging::write(logging::Level::warning,logging::Channel::graphics,r.status);
        commands->OMSetRenderTargets(1,&target,FALSE,nullptr);
    } catch (...) {r.failed=true; r.status="3D X-ray unavailable.";}
}
}
