#include "slam_mesh.h"
#include "Engine/Vfs/game_bundles.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace dingosdk::slam {
namespace {
namespace ebx = frostbite::ebx;
void require(bool valid, const char* message) { if (!valid) throw std::runtime_error(message); }
struct Bytes {
    std::span<const std::byte> data;
    void check(std::size_t offset, std::size_t size) const {
        require(offset <= data.size() && size <= data.size()-offset, "Dem Bones mesh is truncated");
    }
    template<class T> T at(std::size_t offset) const {
        check(offset,sizeof(T)); T value{}; std::memcpy(&value,data.data()+offset,sizeof(value)); return value;
    }
    std::size_t pointer(std::size_t offset) const {
        const auto value=at<std::uint64_t>(offset);
        require(value && value <= data.size() && data.size()-static_cast<std::size_t>(value) >= 16,
            "Dem Bones mesh pointer is invalid");
        return static_cast<std::size_t>(value)+16;
    }
};
const ebx::Value& field(const ebx::Object& object, std::string_view name) {
    const auto* value=object.find(name);
    if (!value) throw std::runtime_error("Skeleton field is missing: "+std::string(name));
    return value->value;
}
template<class T> const T& as(const ebx::Value& value) {
    const auto* result=std::get_if<T>(&value.data);
    require(result != nullptr, "Skeleton field has an unsupported type"); return *result;
}
const ebx::Object& object(const ebx::Value& value) {
    const auto& result=as<std::shared_ptr<ebx::Object>>(value);
    require(result != nullptr,"Skeleton object is missing"); return *result;
}
float real(const ebx::Object& value, std::string_view name) {
    const auto result=static_cast<float>(as<double>(field(value,name)));
    require(std::isfinite(result) && std::abs(result)<1000,"Skeleton bind pose is invalid"); return result;
}
PoseMatrix matrix(const ebx::Value& value) {
    const auto& transform=object(value);
    PoseMatrix result=identity_pose;
    std::size_t row{};
    for (const auto name : {"right","up","forward","trans"}) {
        const auto& axis=object(field(transform,name));
        result[row*4]=real(axis,"x"); result[row*4+1]=real(axis,"y"); result[row*4+2]=real(axis,"z"); ++row;
    }
    return result;
}
std::vector<std::byte> asset(const vfs::GameData& data,const vfs::GameBundle& bundle,
    frostbite::AssetKind kind,std::string_view name) {
    std::size_t index{};
    const auto* entry=bundle.find(kind,name,&index);
    require(entry && entry->originalSize <= 8*1024*1024,"Required skeleton asset is missing or too large");
    const auto* payload=bundle.payload(kind,index);
    require(payload && payload->size <= 8*1024*1024,"Required skeleton payload is invalid");
    return data.read(*payload);
}
struct Attribute { unsigned format{}, offset{}, stride{}; std::size_t base{}; bool present{}; };
}
bool decode_render_camera(std::span<const std::byte> input, RenderCamera& output) noexcept {
    if (input.size()!=320) return false;
    RenderCamera next;
    std::memcpy(next.world.data(),input.data()+0x40,sizeof(next.world));
    float radians{};
    std::memcpy(&radians,input.data()+0x104,sizeof(radians));
    next.vertical_fov=radians*180/3.14159265358979323846f;
    if (!std::isfinite(next.vertical_fov) || next.vertical_fov<=1 || next.vertical_fov>=175) return false;
    for (std::size_t row=0;row<4;++row) {
        for (std::size_t axis=0;axis<3;++axis) {
            const auto value=next.world[row*4+axis];
            if (!std::isfinite(value) || std::abs(value)>1e6f) return false;
        }
        next.world[row*4+3]=row==3 ? 1.0f : 0.0f;
    }
    for (std::size_t row=0;row<3;++row) {
        float norm{};
        for (std::size_t axis=0;axis<3;++axis) norm+=next.world[row*4+axis]*next.world[row*4+axis];
        if (std::abs(norm-1)>.05f) return false;
        for (std::size_t other=row+1;other<3;++other) {
            float dot{};
            for (std::size_t axis=0;axis<3;++axis) dot+=next.world[row*4+axis]*next.world[other*4+axis];
            if (std::abs(dot)>.05f) return false;
        }
    }
    output=next;
    return true;
}
bool offset_render_camera(std::span<std::byte> input,const Vec3& translation,float roll,float fov_scale,RenderCamera& output) noexcept {
    RenderCamera original;
    if (!decode_render_camera(input,original) || !std::isfinite(roll) || std::abs(roll)>.03f ||
        !std::isfinite(fov_scale) || fov_scale<.8f || fov_scale>1 ||
        !std::all_of(translation.begin(),translation.end(),[](float v) {return std::isfinite(v) && std::abs(v)<=.2f;})) return false;
    std::array<std::byte,320> candidate;
    std::copy(input.begin(),input.end(),candidate.begin());
    auto world=original.world;
    const auto cosine=std::cos(roll),sine=std::sin(roll);
    for (std::size_t axis=0;axis<3;++axis) {
        world[axis]=original.world[axis]*cosine+original.world[4+axis]*sine;
        world[4+axis]=original.world[4+axis]*cosine-original.world[axis]*sine;
        world[12+axis]+=translation[0]*original.world[axis]+translation[1]*original.world[4+axis]+translation[2]*original.world[8+axis];
    }
    for (std::size_t row=0;row<4;++row)
        std::memcpy(candidate.data()+0x40+row*16,world.data()+row*4,12);
    const float radians=original.vertical_fov*fov_scale*3.14159265358979323846f/180;
    std::memcpy(candidate.data()+0x104,&radians,4);
    RenderCamera next;
    if (!decode_render_camera(candidate,next)) return false;
    std::copy(candidate.begin(),candidate.end(),input.begin()); output=next;
    return true;
}
FractureLocation varied_fracture_location(unsigned bone,std::uint64_t fracture_ms) noexcept {
    std::uint32_t seed=static_cast<std::uint32_t>(fracture_ms)^static_cast<std::uint32_t>(fracture_ms>>32)^((bone+1)*0x9e3779b9u);
    const auto random=[&]() {
        seed+=0x9e3779b9u;
        auto value=seed;
        value=(value^(value>>16))*0x85ebca6bu;
        value=(value^(value>>13))*0xc2b2ae35u;
        value^=value>>16;
        return static_cast<float>(value>>8)/16777215.f;
    };
    const float fraction=.1f+.8f*random();
    const float first=(random()*2-1)*.18f,second=(random()*2-1)*.18f;
    return {fraction,first,second};
}
std::optional<std::array<float,4>> make_fracture_plane(const PoseMatrix& inverse_bind,
    const Vec3& low,const Vec3& high,const FractureLocation& location) noexcept {
    if (!std::isfinite(location.fraction) || location.fraction<.1f || location.fraction>.9f ||
        !std::isfinite(location.tilt_a) || std::abs(location.tilt_a)>.18f ||
        !std::isfinite(location.tilt_b) || std::abs(location.tilt_b)>.18f ||
        !std::all_of(inverse_bind.begin(),inverse_bind.end(),[](float v) {return std::isfinite(v);})) return {};
    std::size_t axis{};
    Vec3 anchor{};
    for (std::size_t i=0;i<3;++i) {
        if (!std::isfinite(low[i]) || !std::isfinite(high[i]) || high[i]<low[i]) return {};
        anchor[i]=(low[i]+high[i])*.5f;
        if (high[i]-low[i]>high[axis]-low[axis]) axis=i;
    }
    if (high[axis]-low[axis]<.00001f) return {};
    anchor[axis]=low[axis]+(high[axis]-low[axis])*location.fraction;
    Vec3 normal{}; normal[axis]=1;
    normal[(axis+1)%3]=location.tilt_a; normal[(axis+2)%3]=location.tilt_b;
    const float length=std::sqrt(1+location.tilt_a*location.tilt_a+location.tilt_b*location.tilt_b);
    for (auto& value : normal) value/=length;
    std::array<float,4> plane{};
    for (std::size_t row=0;row<4;++row)
        for (std::size_t i=0;i<3;++i) plane[row]+=inverse_bind[row*4+i]*normal[i];
    for (std::size_t i=0;i<3;++i) plane[3]-=anchor[i]*normal[i];
    return plane;
}
PoseMatrix compose_matrices(const PoseMatrix& local, const PoseMatrix& parent) noexcept {
    PoseMatrix result{};
    for (std::size_t row=0;row<3;++row)
        for (std::size_t axis=0;axis<3;++axis)
            result[row*4+axis]=local[row*4]*parent[axis]+local[row*4+1]*parent[4+axis]+local[row*4+2]*parent[8+axis];
    const auto position=model_to_world(parent,{local[12],local[13],local[14]});
    std::copy(position.begin(),position.end(),result.begin()+12); result[15]=1;
    return result;
}
SkeletonRig read_skeleton_rig(const ebx::Document& document) {
    const auto* root=document.root();
    require(root && root->object,"Player render skeleton is missing");
    const auto& hierarchy=as<ebx::Value::Array>(field(*root->object,"Hierarchy"));
    const auto& inverse=as<ebx::Value::Array>(field(*root->object,"InverseModelPose"));
    const auto& model=as<ebx::Value::Array>(field(*root->object,"ModelPose"));
    const auto& names=as<ebx::Value::Array>(field(*root->object,"BoneNames"));
    require(hierarchy.size()==render_bone_count && inverse.size()==render_bone_count &&
        model.size()==render_bone_count && names.size()==render_bone_count,"Unsupported player render rig");
    SkeletonRig result;
    for (std::size_t i=0;i<render_bone_count;++i) {
        const auto parent=as<std::int64_t>(hierarchy[i]);
        require(parent>=-1 && parent<static_cast<std::int64_t>(i),"Player rig hierarchy is invalid");
        require((i==0)==(parent==-1),"Player render rig has an unexpected root");
        result.parents[i]=static_cast<int>(parent);
        result.inverse_bind[i]=matrix(inverse[i]);
        const auto restored=compose_matrices(result.inverse_bind[i],matrix(model[i]));
        for (std::size_t j=0;j<16;++j)
            require(std::abs(restored[j]-identity_pose[j])<.003f,"Player inverse bind pose is inconsistent");
        const auto known=region_for_joint(static_cast<int>(i));
        result.regions[i]=known ? *known : result.regions[static_cast<std::size_t>(parent)];
    }
    for (const auto& [index,name] : std::array<std::pair<std::size_t,std::string_view>,8>{{
        {0,"Reference"},{1,"AITrajectory"},{7,"Hips"},{103,"Head"},{49,"RightHand"},{278,"LeftHand"},
        {10,"RightFoot"},{343,"LeftFoot"}}})
        require(as<std::string>(names[index])==name,"Player render rig bone names changed");
    return result;
}
MeshGeometry read_mesh_geometry(std::span<const std::byte> resource) {
    const Bytes r{resource};
    require(resource.size()<=8*1024*1024 && r.at<std::uint32_t>(0)==208 && r.at<std::uint32_t>(4)==188 &&
        r.at<std::uint32_t>(8)==384 && r.at<std::uint32_t>(12)==0 && r.at<std::uint8_t>(124)==1,
        "Unsupported Dem Bones skinned MeshSet layout");
    const auto lods=r.at<std::uint16_t>(180);
    require(lods>0 && lods<=7,"Dem Bones LOD count is invalid");
    MeshGeometry result;
    result.lod=r.pointer(48); r.check(result.lod,188);
    require(r.at<std::uint32_t>(result.lod)==1 && r.at<std::uint32_t>(result.lod+84)==33,
        "Unsupported Dem Bones LOD kind or index format");
    result.section_count=r.at<unsigned>(result.lod+8);
    require(result.section_count==5,"Dem Bones body sections changed");
    result.sections=r.pointer(result.lod+12); r.check(result.sections,result.section_count*384);
    result.vertex_bytes=r.at<std::uint32_t>(result.lod+92);
    result.index_bytes=r.at<std::uint32_t>(result.lod+88);
    require(result.vertex_bytes>0 && result.index_bytes>0 && result.vertex_bytes<=8*1024*1024 &&
        result.index_bytes<=4*1024*1024,"Dem Bones geometry size is invalid");
    result.chunk=r.at<frostbite::Guid>(result.lod+116);
    require(result.chunk!=frostbite::Guid{},"Dem Bones geometry chunk is missing");
    return result;
}
SkeletonMesh read_skinned_mesh(std::span<const std::byte> resource,std::span<const std::byte> geometry,SkeletonRig rig) {
    const auto layout=read_mesh_geometry(resource);
    const Bytes r{resource},g{geometry};
    require(geometry.size()>=std::size_t{layout.vertex_bytes}+layout.index_bytes && geometry.size()<=16*1024*1024,
        "Dem Bones geometry chunk is truncated or too large");
    SkeletonMesh result; result.rig=std::move(rig);
    for (std::size_t i=0;i<render_bone_count;++i) {
        require(result.rig.parents[i]>=-1 && result.rig.parents[i]<static_cast<int>(i),"Dem Bones rig hierarchy is invalid");
        require(static_cast<std::size_t>(result.rig.regions[i])<region_count,"Dem Bones rig injury region is invalid");
        // Native contacts cover the main body bones, rather than fingers or
        // corrective joints. Their mesh descendants share the same injury.
        // Head geometry follows Neck1's physical head/neck body.
        const auto known=region_for_joint(static_cast<int>(i));
        result.rig.physics_parts[i]=i==103 ? 102u : known && i!=0 && i!=1 && i!=380 ? static_cast<unsigned>(i) :
            result.rig.parents[i]>=0 ? result.rig.physics_parts[static_cast<std::size_t>(result.rig.parents[i])] :
            static_cast<unsigned>(injury_bone_count);
    }
    for (unsigned section=0;section<layout.section_count;++section) {
        const auto p=layout.sections+section*384;
        require(r.at<std::uint8_t>(p+31)==3,"Dem Bones section is not a triangle list");
        const auto count=r.at<std::uint32_t>(p+44), triangles=r.at<std::uint32_t>(p+32);
        const auto vertex_offset=r.at<std::uint32_t>(p+40), start=r.at<std::uint32_t>(p+36);
        require(count>0 && count<=65536 && triangles>0 && triangles<=100000,"Dem Bones section size is invalid");
        const auto palette=r.pointer(p+16); const auto palette_count=r.at<std::uint16_t>(p+24);
        require(palette_count>0 && palette_count<=render_bone_count,"Dem Bones bone palette is invalid");
        r.check(palette,palette_count*2);
        const auto declaration=p+112;
        const auto elements=r.at<std::uint8_t>(declaration+96), streams=r.at<std::uint8_t>(declaration+97);
        require(elements>0 && elements<=16 && streams>0 && streams<=16,"Dem Bones vertex declaration is invalid");
        std::array<std::size_t,16> bases{}; std::array<unsigned,16> strides{};
        std::size_t end=vertex_offset;
        for (unsigned i=0;i<streams;++i) {
            strides[i]=r.at<std::uint8_t>(declaration+64+i*2); bases[i]=end;
            require(strides[i]>0 && r.at<std::uint8_t>(declaration+65+i*2)==0,"Dem Bones vertex stream is unsupported");
            end+=std::size_t{strides[i]}*count;
        }
        require(end<=layout.vertex_bytes,"Dem Bones vertex streams exceed the buffer");
        std::array<Attribute,6> attributes{};
        for (unsigned i=0;i<elements;++i) {
            const auto usage=r.at<std::uint8_t>(declaration+i*4);
            if (usage<1 || usage>5) continue;
            const auto stream=r.at<std::uint8_t>(declaration+i*4+3);
            require(stream<streams && !attributes[usage].present,"Dem Bones vertex attribute is invalid");
            attributes[usage]={r.at<std::uint8_t>(declaration+i*4+1),r.at<std::uint8_t>(declaration+i*4+2),
                strides[stream],bases[stream],true};
        }
        for (unsigned usage=1;usage<=5;++usage) {
            const auto& a=attributes[usage]; const unsigned size=usage==1 ? 12 : usage<=3 ? 8 : 4;
            require(a.present && a.format==(usage==1 ? 3u : usage<=3 ? 23u : 13u) &&
                a.offset<=a.stride && size<=a.stride-a.offset,"Unsupported Dem Bones skinning declaration");
        }
        const auto first=result.vertices.size();
        for (std::size_t v=0;v<count;++v) {
            MeshVertex vertex;
            const auto address=[&](unsigned usage) {const auto& a=attributes[usage]; return a.base+v*a.stride+a.offset;};
            vertex.position=g.at<Vec3>(address(1));
            for (float coordinate : vertex.position) require(std::isfinite(coordinate) && std::abs(coordinate)<3,
                "Dem Bones vertex position is invalid");
            unsigned total{},dominant{};
            for (unsigned influence=0;influence<8;++influence) {
                const auto weight=g.at<std::uint8_t>(address(influence<4 ? 4 : 5)+influence%4);
                vertex.weights[influence]=weight; total+=weight;
                if (!weight) continue; // Unused bone slots need not be initialized.
                const auto index=g.at<std::uint16_t>(address(influence<4 ? 2 : 3)+(influence%4)*2);
                require(index<palette_count,"Dem Bones skinning index exceeds its palette");
                const auto bone=r.at<std::uint16_t>(palette+index*2);
                require(bone<render_bone_count,"Dem Bones skinning bone exceeds the player rig");
                vertex.bones[influence]=bone; result.required[bone]=true;
                if (weight>vertex.weights[dominant]) dominant=influence;
            }
            require(total==255,"Dem Bones skinning weights are not normalized");
            vertex.region=static_cast<std::uint32_t>(result.rig.regions[vertex.bones[dominant]]);
            vertex.part=result.rig.physics_parts[vertex.bones[dominant]];
            result.vertices.push_back(vertex);
        }
        const auto index_offset=std::size_t{start}*2, index_bytes=std::size_t{triangles}*6;
        require(index_offset<=layout.index_bytes && index_bytes<=layout.index_bytes-index_offset,
            "Dem Bones section indices exceed the buffer");
        for (std::size_t i=0;i<std::size_t{triangles}*3;++i) {
            const auto index=g.at<std::uint16_t>(layout.vertex_bytes+index_offset+i*2);
            require(index<count,"Dem Bones triangle has an invalid vertex");
            result.indices.push_back(static_cast<std::uint32_t>(first+index));
        }
    }
    // Area-weighted smooth normals avoid relying on the game's packed tangent
    // encoding, and suit the untextured X-ray material.
    for (std::size_t i=0;i<result.indices.size();i+=3) {
        const auto& a=result.vertices[result.indices[i]].position;
        const auto& b=result.vertices[result.indices[i+1]].position;
        const auto& c=result.vertices[result.indices[i+2]].position;
        const Vec3 u{b[0]-a[0],b[1]-a[1],b[2]-a[2]},v{c[0]-a[0],c[1]-a[1],c[2]-a[2]};
        const Vec3 normal{u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};
        for (std::size_t j=0;j<3;++j)
            for (std::size_t axis=0;axis<3;++axis) result.vertices[result.indices[i+j]].normal[axis]+=normal[axis];
    }
    for (auto& vertex : result.vertices) {
        const auto& n=vertex.normal; const auto length=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
        if (length>1e-8f) for (auto& axis : vertex.normal) axis/=length;
        else vertex.normal={0,1,0};
    }
    for (std::size_t i=render_bone_count;i-- >0;)
        if (result.required[i] && result.rig.parents[i]>=0) result.required[static_cast<std::size_t>(result.rig.parents[i])]=true;
    return result;
}
SkeletonMesh load_dembones_mesh(const std::filesystem::path& game_root) {
    const vfs::GameData data(game_root);
    const auto root_toc=data.read_toc("Win32/levels/game/bam_levelroot/bam_levelroot.toc");
    const auto root=data.read_bundle(root_toc,"win32/levels/game/bam_levelroot/bam_levelroot");
    require(root.has_value(),"Player rig bundle is missing");
    auto rig=read_skeleton_rig(ebx::read_document(asset(data,*root,frostbite::AssetKind::ebx,
        "characters/maincharacters/common/animbase/animbase_default_renderskeleton")));
    const auto toc=data.read_toc("Win32/items.toc");
    const auto costume=data.read_bundle(toc,"win32/characters/maincharacters/generic/cas/clothing/unlicensed/generic/apparel/fullbody/costume/dembones/2026/gen_costume_dembones_complex_dmpreset_cas_main_bundlereftable");
    require(costume.has_value(),"Dem Bones costume bundle is missing");
    const auto resource=asset(data,*costume,frostbite::AssetKind::resource,
        "characters/maincharacters/generic/cas/clothing/unlicensed/generic/apparel/fullbody/costume/dembones/2026/gen_costume_dembones_base_mesh");
    const auto layout=read_mesh_geometry(resource);
    std::vector<std::byte> geometry;
    std::size_t index{};
    if (costume->find_chunk(layout.chunk,&index)) geometry=data.read(*costume->payload(frostbite::AssetKind::chunk,index));
    else {
        const auto found=std::find_if(toc.chunks.begin(),toc.chunks.end(),[&](const auto& chunk) {
            return chunk.guid==layout.chunk && !chunk.removed;
        });
        require(found!=toc.chunks.end() && found->size<=16*1024*1024,"Dem Bones geometry chunk is unavailable");
        geometry=data.read({found->location,found->offset,found->size});
    }
    return read_skinned_mesh(resource,geometry,std::move(rig));
}
bool skin_render_pose(const SkeletonMesh& mesh,std::span<const std::array<float,12>> local,MeshPose& output) noexcept {
    if (local.size()<render_bone_count) return false;
    std::array<PoseMatrix,render_bone_count> world{};
    MeshPose next{};
    for (std::size_t i=0;i<render_bone_count;++i) {
        if (!mesh.required[i]) { next.skin[i]=identity_pose; continue; }
        const auto parent=mesh.rig.parents[i];
        if (parent>=static_cast<int>(i) || parent< -1 || (parent>=0 && !mesh.required[static_cast<std::size_t>(parent)])) return false;
        const auto composed=append_pose(parent<0 ? identity_pose : world[static_cast<std::size_t>(parent)],local[i]);
        if (!composed) return false;
        world[i]=*composed;
        // Correct row-vector order: bind vertex -> inverse bind -> bone world.
        next.skin[i]=compose_matrices(mesh.rig.inverse_bind[i],world[i]);
    }
    output=next;
    return true;
}
bool place_render_skin(const SkeletonMesh& mesh,std::span<const PoseMatrix> skin,
    const PoseMatrix& placement,MeshPose& output) noexcept {
    if (skin.size()<render_bone_count) return false;
    for (const auto value : placement) if (!std::isfinite(value) || std::abs(value)>1000000) return false;
    MeshPose next{};
    for (std::size_t i=0;i<render_bone_count;++i) {
        if (!mesh.required[i]) {next.skin[i]=identity_pose; continue;}
        const auto& m=skin[i];
        // Native SIMD rows carry metadata in their fourth component, including
        // NaN sentinels. Only the twelve affine components participate in skinning.
        for (std::size_t row=0;row<4;++row)
            for (std::size_t axis=0;axis<3;++axis) {
                const auto value=m[row*4+axis];
                if (!std::isfinite(value) || std::abs(value)>1000000) return false;
            }
        next.skin[i]=compose_matrices(m,placement);
    }
    output=next;
    return true;
}
bool place_packed_render_skin(const SkeletonMesh& mesh,std::span<const std::array<float,12>> skin,
    const PoseMatrix& placement,MeshPose& output) noexcept {
    if (skin.size()<render_bone_count) return false;
    std::array<PoseMatrix,render_bone_count> matrices{};
    for (std::size_t i=0;i<render_bone_count;++i) {
        const auto& m=skin[i];
        matrices[i]={m[0],m[4],m[8],0, m[1],m[5],m[9],0,
            m[2],m[6],m[10],0, m[3],m[7],m[11],1};
    }
    return place_render_skin(mesh,matrices,placement,output);
}
bool rebase_render_root(const MeshPose& source,const Vec3& root,MeshPose& output) noexcept {
    Vec3 delta{}; float distance_squared{};
    for (std::size_t axis=0;axis<3;++axis) {
        const auto previous=source.skin[1][12+axis];
        if (!std::isfinite(root[axis]) || std::abs(root[axis])>1000000 || !std::isfinite(previous)) return false;
        const auto separation=root[axis]-previous;
        distance_squared+=separation*separation;
        // The collider origin steps vertically by 20 cm even at rest. Its
        // height is not the animation trajectory height; keep the rendered Y.
        delta[axis]=axis==1 ? 0 : separation;
    }
    if (!std::isfinite(distance_squared) || distance_squared>64) return false;
    auto next=source;
    for (auto& bone : next.skin)
        for (std::size_t axis=0;axis<3;++axis) {
            bone[12+axis]+=delta[axis];
            if (!std::isfinite(bone[12+axis]) || std::abs(bone[12+axis])>1000000) return false;
        }
    output=next;
    return true;
}
}
