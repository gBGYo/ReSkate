#include "Engine/Vfs/game_bundles.h"
#include "Engine/Resource/ebx_writer.h"
#include "Engine/Resource/cas_codec.h"
#include "Engine/Resource/texture.h"
#include "Engine/Vfs/mod_catalog.h"
#include "Engine/Core/Json/json.h"
#include "Extension/Blood/blood_native.h"
#include "Extension/Blood/blood_response.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <cstring>
#include <cmath>
#include <set>

namespace fs=std::filesystem;
namespace fb=dingosdk::frostbite;
namespace ebx=fb::ebx;
using dingosdk::Json;
namespace {
constexpr auto donor_bundle="win32/effects/gestures/effectblueprints/ebp_gesture_applause_bundle";
constexpr auto donor_graph="effects/_global/emittergraphs/eg_commonemitter_01";
constexpr auto donor_effect="effects/gestures/effectblueprints/ebp_gesture_applause";
// BAM_CoreGameAssets is unloaded on travel to stadiums, Grom and other maps.
// Keep the complete blood dependency set in the common level root, alongside
// the shader lookup tables it uses, so host-map travel cannot strand it.
constexpr auto target="win32/levels/game/dingolevel_root/dingolevel_root";
constexpr std::uint64_t stock_material=7655350407671934533ULL;
constexpr std::uint64_t blood_material=0x5ba7a219be420019ULL;
template<class T> T get(const std::vector<std::byte>& b,std::size_t p) {
    T v; if(p+sizeof(v)>b.size()) throw std::runtime_error("Truncated particle resource");
    std::memcpy(&v,b.data()+p,sizeof(v)); return v;
}
template<class T> void put(std::vector<std::byte>& b,std::size_t p,T v) {
    if(p+sizeof(v)>b.size()) throw std::runtime_error("Truncated particle resource");
    std::memcpy(b.data()+p,&v,sizeof(v));
}
ebx::Value& field(ebx::Object& o,std::string_view name) {
    for (auto& f:o.fields) if (f.name==name) return f.value;
    throw std::runtime_error("Missing donor field: "+std::string(name));
}
ebx::Object& object(ebx::Value& v) {return *std::get<std::shared_ptr<ebx::Object>>(v.data);}
ebx::Object& root(ebx::Document& d) {return *d.instances.at(d.root()-d.instances.data()).object;}
// Field descriptors belong to a document. Copy values by name into the
// blueprint's own parameter type rather than transplanting graph descriptors.
ebx::Object parameter_copy(const ebx::Object& prototype,const ebx::Object& source) {
    auto out=prototype;
    for (auto& destination:out.fields) {
        const auto from=std::find_if(source.fields.begin(),source.fields.end(),[&](const auto& f){return f.name==destination.name;});
        if (from==source.fields.end()) throw std::runtime_error("Incompatible GPU parameter schema");
        const auto nested=std::get_if<std::shared_ptr<ebx::Object>>(&destination.value.data);
        if (nested && *nested) {
            const auto input=std::get_if<std::shared_ptr<ebx::Object>>(&from->value.data);
            if (!input || !*input) throw std::runtime_error("Incompatible GPU vector schema");
            destination.value.data=std::make_shared<ebx::Object>(parameter_copy(**nested,**input));
        } else destination.value.data=from->value.data;
    }
    return out;
}
std::uint32_t hash(std::string_view s) {std::uint32_t h=5381; for (unsigned char c:s) h=h*33^c; return h;}
void vector(ebx::Object& o,std::initializer_list<double> values) {
    auto i=values.begin(); for (const auto name:{"x","y","z","w"}) if (i!=values.end()) field(o,name).data=*i++;
}
void quality(ebx::Object& o,std::string_view name,double value,bool integer=false) {
    auto& q=object(field(o,name));
    for (const auto level:{"Low","Medium","High","Ultra"}) {
        if (integer) field(q,level).data=static_cast<std::int64_t>(value);
        else field(q,level).data=value;
    }
}
void parameter(ebx::Object& graph,std::string_view name,std::initializer_list<double> values,bool integer=false) {
    auto& params=std::get<ebx::Value::Array>(field(graph,"EmitterGraphParams").data);
    for (auto& p:params) {
        auto& o=object(p); const auto id=std::get<std::int64_t>(field(o,"PropertyId").data);
        if (static_cast<std::uint32_t>(id)!=hash(name)) continue;
        if (integer) field(o,"IntValue").data=static_cast<std::int64_t>(*values.begin());
        else vector(object(field(o,"Value")),values);
        return;
    }
    throw std::runtime_error("Verified GPU parameter is missing: "+std::string(name));
}
fb::Guid private_guid(unsigned kind,unsigned slot) {
    fb::Guid g;
    constexpr std::array<unsigned char,16> prefix{0x67,0x9e,0x4b,0x91,0x5f,0x28,0xcd,0x43,0xa1,0x7c,0xbc,0x18,0x7f,0x00,0x00,0x00};
    for (unsigned i=0;i<16;++i) g.bytes[i]=std::byte(prefix[i]);
    g.bytes[13]=std::byte(kind); g.bytes[14]=std::byte(slot&255); g.bytes[15]=std::byte(slot>>8); return g;
}
void identity(ebx::Document& d,unsigned kind,unsigned slot,std::string_view name) {
    d.fileGuid=private_guid(kind,slot);
    for (unsigned i=0;i<d.instances.size();++i) if (d.instances[i].exported) d.instances[i].instanceGuid=private_guid(kind,slot+1+i);
    field(root(d),"Name").data=std::string(name);
}
std::string hex(std::span<const std::byte> b) {std::string s;for (auto c:b) {const auto v=std::to_integer<unsigned>(c);s+="0123456789abcdef"[v>>4];s+="0123456789abcdef"[v&15];}return s;}
void write(const fs::path& p,std::span<const std::byte> b) {std::ofstream f(p,std::ios::binary);f.write(reinterpret_cast<const char*>(b.data()),b.size());if(!f)throw std::runtime_error("Could not write blood asset");}
void alias_material(fb::BundleAsset& a,std::vector<std::byte>& raw,std::uint64_t original_material,std::uint64_t private_material) {
    std::vector<std::byte> alias;
    if (a.resourceType==0xe2e6955b) {
        const auto start=get<std::uint32_t>(a.resourceMeta,0)+get<std::uint32_t>(a.resourceMeta,4);
        const auto n=get<std::uint32_t>(raw,start); std::size_t p=start+4;
        for (unsigned row=0;row<n;++row) {
            const auto length=12+std::size_t(get<std::uint32_t>(raw,p+8))*4;
            if (p>raw.size() || length>raw.size()-p) throw std::runtime_error("Invalid particle program row");
            if (get<std::uint64_t>(raw,p)==original_material) alias.assign(raw.begin()+p,raw.begin()+p+length);
            p+=length;
        }
        if (p!=raw.size() || alias.empty()) throw std::runtime_error("Missing particle compiled program");
        put(alias,0,private_material); p=start+4;
        for (unsigned row=0;row<n && get<std::uint64_t>(raw,p)<private_material;++row)
            p+=12+std::size_t(get<std::uint32_t>(raw,p+8))*4;
        raw.insert(raw.begin()+p,alias.begin(),alias.end()); put(raw,start,n+1);
        put(a.resourceMeta,8,get<std::uint32_t>(a.resourceMeta,8)+std::uint32_t(alias.size()));
    } else {
        for (std::size_t p=0;p+16<=raw.size();p+=16)
            if (get<std::uint64_t>(raw,p)==original_material) {alias.assign(raw.begin()+p,raw.begin()+p+16);break;}
        if (alias.empty()) throw std::runtime_error("Missing particle texture lookup");
        put(alias,0,private_material); std::size_t p{};
        while (p<raw.size() && get<std::uint64_t>(raw,p)<private_material) p+=16;
        raw.insert(raw.begin()+p,alias.begin(),alias.end());
    }
}


}
#include "blood_ground_asset.h"

int main(int argc,char** argv) {
    try {
        if (argc==4 && std::string_view(argv[3])=="--verify-merge") {
            auto catalog=dingosdk::mods::load_catalog(argv[1],{},false);
            if (!catalog.issue.empty()) throw std::runtime_error(catalog.issue);
            // Verify an upgrade against the other installed mods, rather than
            // merging two versions of our blood assets together. Only this
            // temporary catalog changes; the installed mod and backup stay put.
            std::erase_if(catalog.mods,[](const auto& existing) {
                return existing.name=="ReSkate_HallOfMeat" || existing.name=="HallOfMeatBlood" || existing.name=="ReSkate_Blood";
            });
            const fs::path mod=fs::absolute(argv[2]);
            catalog.root=mod.parent_path()/".blood-merge-verification";
            dingosdk::mods::Mod addition; addition.name="ReSkate_Blood"; addition.directory=mod; addition.provides_layout=true;
            catalog.mods.insert(catalog.mods.begin(),addition);
            const auto report=dingosdk::mods::merge_mods(catalog);
            fs::create_directories(catalog.root);
            Json problems=Json::object(); for (const auto& [name,list]:report.problems) problems[name]=list;
            std::ofstream(catalog.root/"report.json")<<Json{{"built",report.built},{"issue",report.issue},{"problems",problems},{"notes",report.notes}}.dump(2);
            if (!report.built || !report.issue.empty() || !report.problems.empty()) throw std::runtime_error("Blood/map merge failed; see .blood-merge-verification/report.json");
            // A conflict-free merge is insufficient: a city-only bundle passes
            // that check but disappears when multiplayer selects a stadium.
            const auto merged=catalog.root/dingosdk::mods::generated_folder;
            const auto root_toc=merged/"Win32/levels/game/dingolevel_root/dingolevel_root.toc";
            std::ifstream input(root_toc,std::ios::binary|std::ios::ate);
            if (!input) throw std::runtime_error("Merged blood package has no common level root TOC");
            std::vector<std::byte> bytes(static_cast<std::size_t>(input.tellg()));
            input.seekg(0); input.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
            if (!input) throw std::runtime_error("Could not read merged common level root TOC");
            const auto toc=fb::read_toc(bytes);
            const auto root=std::find_if(toc.bundles.begin(),toc.bundles.end(),[](const auto& b){return b.name==target;});
            if (root==toc.bundles.end()) throw std::runtime_error("Merged blood package has no common level root bundle");
            const auto region=fb::read_bundle_region(root->region);
            if (region.files.empty()) throw std::runtime_error("Merged common level root has no manifest");
            const auto layout=dingosdk::vfs::read_layout(merged/"layout.toc");
            dingosdk::vfs::GameArchives archives(merged,layout.root);
            const auto& file=region.files.front();
            const auto manifest=archives.read_manifest(merged,file.location,file.offset,file.size,catalog.data_root);
            const auto require_asset=[&](const std::string& name) {
                if (std::none_of(manifest.ebx.begin(),manifest.ebx.end(),[&](const auto& a){return a.name==name;}))
                    throw std::runtime_error("Blood asset missing from the common level root: "+name);
            };
            for (unsigned color=0;color<dingosdk::blood::blood_color_names.size();++color) {
                const auto selected=static_cast<dingosdk::blood::BloodColor>(color);
                for (const auto* name:dingosdk::blood::blood_asset_names)
                    require_asset(dingosdk::blood::blood_colored_asset(name,selected));
                for (const auto* name:dingosdk::blood::blood_ground_materials)
                    require_asset(dingosdk::blood::blood_colored_asset(name,selected));
            }
            std::cout<<"Blood package merged without conflicts; all 36 effects and 16 materials are in the common level root.\n"; return 0;
        }
        if (argc!=3) throw std::runtime_error("usage: blood_asset_author <supported Skate folder> <output folder>");
        dingosdk::vfs::GameData data(argv[1]);
        const auto toc=data.read_toc("Win32/customization.toc");
        const auto bundle=data.read_bundle(toc,donor_bundle);
        if (!bundle) throw std::runtime_error("Supported particle donor bundle is missing");
        const fs::path out=argv[2]; fs::create_directories(out); Json rows=Json::array();
        std::set<std::pair<fb::AssetKind,std::string>> emitted;
        auto emit=[&](fb::AssetKind kind,fb::BundleAsset a,const std::vector<std::byte>& raw,bool added=false) {
            if (!emitted.emplace(kind,kind==fb::AssetKind::chunk?a.guid.string():a.name).second) return;
            const auto file=std::to_string(rows.size())+".cas";
            const auto encoded=fb::encode_cas(raw,{argv[1]});
            if (fb::decode_cas(encoded,{argv[1]})!=raw) throw std::runtime_error("Particle CAS round-trip failed");
            write(out/file,encoded);
            rows.push_back(Json{{"kind",kind==fb::AssetKind::ebx?1:kind==fb::AssetKind::resource?2:3},
                {"name",kind==fb::AssetKind::chunk?a.guid.string():a.name},{"file",file},{"size",raw.size()},
                {"type",a.resourceType},{"rid",a.resourceId},{"meta",hex(a.resourceMeta)},
                {"logical_offset",a.logicalOffset},{"logical_size",kind==fb::AssetKind::chunk?raw.size():a.logicalSize},{"target",target},{"added",added}});
        };
        auto bytes=[&](const auto& source,const char* name) {
            std::size_t index{}; if (!source->find(fb::AssetKind::ebx,name,&index)) throw std::runtime_error("Missing EBX donor");
            return data.read(*source->payload(fb::AssetKind::ebx,index));
        };
        const auto graph_bytes=bytes(bundle,donor_graph),effect_bytes=bytes(bundle,donor_effect);
        // Keep stock streaming texture records; copying a bundle's partial mip
        // slice as a complete replacement corrupts its texture payload.
        for (const auto kind:{fb::AssetKind::ebx,fb::AssetKind::resource}) {
            const auto& assets=kind==fb::AssetKind::ebx?bundle->manifest.ebx:bundle->manifest.resources;
            for (std::size_t i=0;i<assets.size();++i) {
                auto asset=assets[i];
                if (kind==fb::AssetKind::ebx && asset.name!=donor_graph && asset.name.find("textures/")==std::string::npos) continue;
                // Material permutations, binding sets and streamable texture
                // sets are renderer dependencies, alongside shader bytecode.
                // Preserve the compact donor's complete resource set.
                emit(kind,asset,data.read(*bundle->payload(kind,i)));
            }
        }
        for (unsigned color_index=0;color_index<dingosdk::blood::blood_color_names.size();++color_index) {
        const auto color=static_cast<dingosdk::blood::BloodColor>(color_index);
        const auto color_suffix=dingosdk::blood::blood_color_suffixes[color_index];
        const auto material_key=blood_material+color_index*0x100000ULL;
        constexpr auto texture_donor="effects/_global/textures/defaults/t_fx_placeholdertex";
        const auto texture_name=dingosdk::blood::blood_colored_asset("effects/reskate/hallofmeat/t_blood_droplet",color);
        const std::uint64_t texture_rid=0x5ba7a219be420021ULL+color_index*0x100000ULL;
        auto texture_doc=ebx::read_document(bytes(bundle,texture_donor));
        const auto old_file=texture_doc.fileGuid,old_class=texture_doc.root()->instanceGuid;
        identity(texture_doc,3+color_index*16,0x300,texture_name);
        field(root(texture_doc),"Resource").data=ebx::ResourceReference{texture_rid};
        field(root(texture_doc),"AuthoredWidth").data=std::uint64_t{64};
        field(root(texture_doc),"AuthoredHeight").data=std::uint64_t{64};
        auto& crop=object(field(root(texture_doc),"CropInfo")); field(crop,"Z").data=64.; field(crop,"W").data=64.;
        fb::BundleAsset texture_asset; texture_asset.name=texture_name;
        emit(fb::AssetKind::ebx,texture_asset,ebx::write_document(texture_doc),true);
        std::size_t texture_index{};
        const auto* original_texture=bundle->find(fb::AssetKind::resource,texture_donor,&texture_index);
        if (!original_texture) throw std::runtime_error("Missing particle texture header");
        texture_asset=*original_texture;
        auto header=data.read(*bundle->payload(fb::AssetKind::resource,texture_index));
        if (header.size()!=180 || get<std::uint32_t>(texture_asset.resourceMeta,0)!=12) throw std::runtime_error("Unexpected particle texture format");
        const auto pixel_guid=blood_particle_chunk(color_index);
        put(header,12,std::uint32_t{20}); put(header,20,std::uint16_t{1});
        put(header,22,std::uint16_t{64}); put(header,24,std::uint16_t{64}); put(header,31,std::uint8_t{0});
        put(texture_asset.resourceMeta,4,std::uint32_t{0});
        std::copy(pixel_guid.bytes.begin(),pixel_guid.bytes.end(),header.begin()+40);
        std::fill(header.begin()+56,header.begin()+116,std::byte{});
        fb::Image mip{64,64,std::vector<std::uint8_t>(64*64*4)};
        // Compact round globules have no long leaf silhouette or bright rim.
        // Their stream shape comes from ballistic motion and spawn spacing.
        for (unsigned y=0;y<64;++y) for (unsigned x=0;x<64;++x) {
            const float dx=(x+.5f-32)/24,dy=(y+.5f-32)/24;
            const float radius=std::sqrt(dx*dx+dy*dy);
            const float coverage=std::clamp((1-radius)*16,0.f,1.f);
            const float shade=.65f+.35f*std::sqrt(std::max(0.f,1-radius*radius));
            const auto at=4*(y*64+x);
            mip.rgba[at]=static_cast<std::uint8_t>(160*shade);
            const auto rgb=dingosdk::blood::blood_pixel(color,mip.rgba[at],2,6);
            std::copy(rgb.begin(),rgb.end(),mip.rgba.begin()+at);
            mip.rgba[at+3]=static_cast<std::uint8_t>(coverage*255);
        }
        const auto expected_pixels=mip.rgba;
        std::vector<std::byte> pixels; std::uint8_t mips{};
        for (;;) {
            put(header,56+4*mips,std::uint32_t(mip.rgba.size())); ++mips;
            const auto raw=std::as_bytes(std::span(mip.rgba)); pixels.insert(pixels.end(),raw.begin(),raw.end());
            if (mip.width==1 && mip.height==1) break;
            mip=fb::resize(mip,std::max(1u,mip.width/2),std::max(1u,mip.height/2));
        }
        put(header,30,mips); put(header,116,std::uint32_t(pixels.size()));
        for (const auto offset:{0u,4u,32u,36u}) put(header,offset,std::uint32_t{0});
        if (fb::decode_texture(fb::read_texture_header(header,texture_asset.resourceMeta),pixels,64).rgba!=expected_pixels)
            throw std::runtime_error("Blood drop pixel round-trip failed");
        texture_asset.name=texture_name; texture_asset.resourceId=texture_rid;
        emit(fb::AssetKind::resource,texture_asset,header,true);
        fb::BundleAsset pixel_asset; pixel_asset.guid=pixel_guid; emit(fb::AssetKind::chunk,pixel_asset,pixels,true);
        const auto depot_it=std::find_if(bundle->manifest.resources.begin(),bundle->manifest.resources.end(),[](const auto& a){return a.resourceType==0x73312045;});
        if (depot_it==bundle->manifest.resources.end()) throw std::runtime_error("Missing particle shader depot");
        auto depot_asset=*depot_it;
        auto depot=data.read(*bundle->payload(fb::AssetKind::resource,depot_it-bundle->manifest.resources.begin()));
        const auto table=get<std::uint64_t>(depot,0); std::uint64_t block{};
        for (unsigned i=0;i<get<std::uint32_t>(depot_asset.resourceMeta,12);++i)
            if (get<std::uint64_t>(depot,table+16*i)==stock_material) block=get<std::uint64_t>(depot,table+16*i+8);
        if (!block) throw std::runtime_error("Missing particle material scope");
        put(depot,table,material_key); put(depot,table+8,block); put(depot_asset.resourceMeta,12,std::uint32_t{1});
        const auto scope=get<std::uint64_t>(depot,block),scope_size=std::uint64_t(get<std::uint32_t>(depot,block+16));
        if (scope>depot.size() || scope_size>depot.size()-scope) throw std::runtime_error("Invalid particle texture scope");
        std::array<std::byte,32> before{},after{};
        std::copy(old_class.bytes.begin(),old_class.bytes.end(),before.begin()); std::copy(old_file.bytes.begin(),old_file.bytes.end(),before.begin()+16);
        std::copy(texture_doc.root()->instanceGuid.bytes.begin(),texture_doc.root()->instanceGuid.bytes.end(),after.begin());
        std::copy(texture_doc.fileGuid.bytes.begin(),texture_doc.fileGuid.bytes.end(),after.begin()+16);
        const auto end=depot.begin()+scope+scope_size;
        const auto binding=std::search(depot.begin()+scope,end,before.begin(),before.end());
        if (binding==end) throw std::runtime_error("Particle texture binding missing");
        std::copy(after.begin(),after.end(),binding);
        std::uint64_t scope_hash=14695981039346656037ULL;
        for (auto at=scope;at<scope+scope_size;++at) scope_hash=(scope_hash^std::to_integer<unsigned char>(depot[at]))*1099511628211ULL;
        put(depot,block+8,scope_hash);
        depot_asset.name=std::string("effects/reskate/hallofmeat/shaderblockdepot_blood")+color_suffix;
        depot_asset.resourceId=0x5ba7a219be420029ULL+color_index*0x100000ULL;
        emit(fb::AssetKind::resource,depot_asset,depot,true);
        for (unsigned asset=0;asset<dingosdk::blood::blood_asset_names.size();++asset) {
            const unsigned kind=asset%3;
            const bool light=asset>=3 && asset<6,heavy=asset>=6;
            const double burst_count=light ? 3 : heavy ? 40 : 12;
            const double spawn_rate=kind==0 ? 0 : kind==1 ? (light ? 4 : heavy ? 24 : 10) : (light ? 60 : heavy ? 300 : 160);
            const double particle_limit=kind==0 ? burst_count : kind==1 ? (heavy ? 24 : 12) : 100;
            const double particle_life=kind==0 ? (heavy ? .45 : .25) : kind==1 ? .45 : .25;
            const double spread=light ? .45 : heavy ? 3.5 : 1.4;
            auto graph=ebx::read_document(graph_bytes),effect=ebx::read_document(effect_bytes);
            const auto graph_name=std::string("effects/reskate/hallofmeat/eg_blood_")+(kind==0?"spray":kind==1?"droplets":"trail")+(light?"_light":heavy?"_heavy":"")+color_suffix;
            identity(graph,asset+color_index*16,0x100,graph_name);
            const auto effect_name=dingosdk::blood::blood_colored_asset(dingosdk::blood::blood_asset_names[asset],color);
            identity(effect,asset+color_index*16,0x200,effect_name);
            auto& g=root(graph);
            // The proven sprite renderer emits round independent drops.
            // A dense point jet and a looser spray use the same liquid mask.
            parameter(g,"SpawnColor",{1,1,1,1}); parameter(g,"SpawnAlpha",{.95});
            parameter(g,"SpawnScaleMinMax",kind==0 ? (heavy ? std::initializer_list<double>{.01,.02} : std::initializer_list<double>{.0072,.0144}) :
                kind==1?std::initializer_list<double>{.0084,.0168}:std::initializer_list<double>{.012,.0216});
            parameter(g,"StretchAmount",{0}); parameter(g,"SphereScale",{.003,.003,.003});
            // Shrinking only SphereScale leaves the stock cube/cone spawn
            // distributions active around the character instead of at the cut.
            parameter(g,"CubeDistributionSize",{.003,.003,.003});
            parameter(g,"SphereDistributionRadius",{.003});
            parameter(g,"ConeDistributionAngle",{kind==0 ? (light ? 12. : heavy ? 55. : 30.) : 2.});
            parameter(g,"SpawnVelocityMin",kind==0?std::initializer_list<double>{-spread,heavy?2.5:1.8,-spread}:kind==1?std::initializer_list<double>{-.4,2,-.4}:std::initializer_list<double>{-.04,2.5,-.04});
            parameter(g,"SpawnVelocityMax",kind==0?std::initializer_list<double>{spread,heavy?5.5:3.5,spread}:kind==1?std::initializer_list<double>{.4,3,.4}:std::initializer_list<double>{.04,3.5,.04});
            parameter(g,"UseGravity",{1},true); parameter(g,"Gravity",{-9.807});
            field(g,"KillOnStop").data=false; field(g,"CastSunShadow").data=false;
            field(g,"IgnoreScreenAreaCulling").data=true;
            quality(g,"ParticleLifeSpan",.8);
            for (auto& instance:graph.instances)
                if (graph.types.at(instance.descriptor).name=="SpawnModeContinuous")
                    quality(*instance.object,"MaxSpawnRate",300);
            auto& bp=root(effect); field(bp,"TimeDeltaType").data=std::int64_t{2};
            unsigned emitters{};
            for (auto& instance:effect.instances) {
                if (effect.types.at(instance.descriptor).name!="EmitterGraphEntityData") continue;
                ++emitters; auto& e=*instance.object;
                const auto reference=std::get<ebx::PointerReference>(field(e,"EmitterGraph").data);
                if (reference.kind!=ebx::PointerKind::external) throw std::runtime_error("Unexpected particle graph binding");
                auto& import=effect.imports.at(reference.index); import.fileGuid=graph.fileGuid; import.classGuid=graph.root()->instanceGuid;
                auto& inputs=std::get<ebx::Value::Array>(field(e,"EmitterGraphParams").data);
                const auto prototype=object(inputs.at(0));
                inputs.clear();
                for (auto& p:std::get<ebx::Value::Array>(field(g,"EmitterGraphParams").data)) {
                    auto input=parameter_copy(prototype,object(p));
                    field(input,"DataOffset").data=std::int64_t{0};
                    ebx::Value value; value.data=std::make_shared<ebx::Object>(std::move(input)); inputs.push_back(std::move(value));
                }
                // All three phases use the compatible particle vertex layout.
                auto& keys=std::get<ebx::Value::Array>(field(e,"ExpressionShaderInstanceKeys").data);
                if (keys.size()!=1) throw std::runtime_error("Unexpected particle material count");
                keys[0].data=static_cast<std::int64_t>(material_key);
                field(e,"DrawCameraOffset").data=0.; quality(e,"Enable",1,true);
                auto& pose=object(field(e,"Transform")); vector(object(field(pose,"trans")),{0,0,0});
                auto& overrides=object(field(e,"EmitterGraphOverrides"));
                quality(overrides,"ParticleMaxCount",particle_limit,true);
                quality(overrides,"InitialParticleCount",kind==0?burst_count:2,true);
                quality(overrides,"SpawnRate",spawn_rate);
                quality(overrides,"ParticleLifeSpan",particle_life);
                // Runtime stops light wounds early; allow its full heavy-hit
                // emission window instead of silently capping every hit.
                quality(overrides,"EmitterLifeSpan",kind==0?.12:(kind==1?.5:.7)*dingosdk::blood::blood_max_emission_scale);
                for (const auto flag:{"IsEmitterLifeSpanOverrideSet","IsParticleMaxCountOverrideSet","IsInitialParticleCountOverrideSet","IsParticleLifeSpanOverrideSet","IsSpawnRateOverrideSet"})
                    field(overrides,flag).data=true;
            }
            if (emitters!=1) throw std::runtime_error("Donor must contain one emitter and no audio or light entities");
            const auto graph_raw=ebx::write_document(graph),effect_raw=ebx::write_document(effect);
            auto verified_graph=ebx::read_document(graph_raw),verified_effect=ebx::read_document(effect_raw);
            auto& authored_inputs=std::get<ebx::Value::Array>(field(root(verified_graph),"EmitterGraphParams").data);
            auto original_for_check=ebx::read_document(graph_bytes);
            auto& stock_inputs=std::get<ebx::Value::Array>(field(root(original_for_check),"EmitterGraphParams").data);
            if (authored_inputs.size()!=stock_inputs.size()) throw std::runtime_error("Particle graph GPU layout changed");
            for (std::size_t i=0;i<stock_inputs.size();++i)
                for (const auto slot:{"PropertyId","DataOffset","ExposableType"})
                    if (std::get<std::int64_t>(field(object(authored_inputs[i]),slot).data)!=std::get<std::int64_t>(field(object(stock_inputs[i]),slot).data))
                        throw std::runtime_error("Particle graph GPU parameter slot changed");
            if (verified_graph.fileGuid!=graph.fileGuid || verified_effect.fileGuid!=effect.fileGuid || verified_effect.rootType!="EffectBlueprint")
                throw std::runtime_error("Private particle EBX round-trip failed");
            for (auto& instance:verified_effect.instances) if (verified_effect.types.at(instance.descriptor).name=="EmitterGraphEntityData") {
                auto& overrides=object(field(*instance.object,"EmitterGraphOverrides"));
                for (const auto level:{"Low","Medium","High","Ultra"}) {
                    if (std::get<std::int64_t>(field(object(field(overrides,"InitialParticleCount")),level).data)!=(kind==0?burst_count:2) ||
                        std::get<std::int64_t>(field(object(field(overrides,"ParticleMaxCount")),level).data)!=particle_limit ||
                        std::abs(std::get<double>(field(object(field(overrides,"SpawnRate")),level).data)-spawn_rate)>.001)
                        throw std::runtime_error("Blood intensity emission overrides failed verification");
                }
            }
            std::cout<<effect_name<<": initial="<<(kind==0?burst_count:2)
                <<", rate="<<spawn_rate<<", limit="<<particle_limit<<", particle life="<<particle_life<<"\n";
            fb::BundleAsset a; a.name=graph_name; emit(fb::AssetKind::ebx,a,graph_raw,true);
            a.name=effect_name; emit(fb::AssetKind::ebx,a,effect_raw,true);
        }
        }
        author_blood_ground(data,emit);
        // The private particle scope reuses its stock compiled program. Alias the
        // material key in the root tables, preserving every existing row.
        const auto root_toc=data.read_toc("Win32/levels/game/dingolevel_root/dingolevel_root.toc");
        const auto root_bundle=data.read_bundle(root_toc,"win32/levels/game/dingolevel_root/dingolevel_root");
        if (!root_bundle) throw std::runtime_error("Missing root shader lookup bundle");
        unsigned lookups{};
        for (std::size_t i=0;i<root_bundle->manifest.resources.size();++i) {
            auto a=root_bundle->manifest.resources[i];
            if (a.resourceType!=0xe2e6955b && a.resourceType!=0x2d254a89) continue;
            ++lookups; auto raw=data.read(*root_bundle->payload(fb::AssetKind::resource,i));
            for (unsigned color=0;color<dingosdk::blood::blood_color_names.size();++color)
                alias_material(a,raw,stock_material,blood_material+color*0x100000ULL);
            for (unsigned variant=0;variant<ground_variant_count;++variant)
                alias_material(a,raw,ground_stock_material,ground_key(variant));
            emit(fb::AssetKind::resource,a,raw);
            rows[rows.size()-1]["target"]="win32/levels/game/dingolevel_root/dingolevel_root";
        }
        if (lookups!=2) throw std::runtime_error("Expected both root particle lookup tables");
        std::ofstream(out/"resources.json")<<Json{{"schema",1},{"resources",rows},{"donor",donor_bundle},{"target",target}}.dump(2);
        std::cout<<"Authored 36 private blood effects, 16 ground blood materials and "<<rows.size()<<" verified payloads.\n";
    } catch (const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
