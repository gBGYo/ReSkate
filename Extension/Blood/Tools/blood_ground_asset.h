// Private surface material adapted from feature/decals' verified graffiti author.
// Included after the shared EBX/texture helpers in blood_asset_author.cpp.
namespace {
constexpr std::uint64_t ground_stock_material=0x9b3233728e841bc0ULL;
constexpr std::uint64_t ground_blood_material=0x5ba7a219be430019ULL;
constexpr unsigned ground_variant_count=16;
constexpr std::array ground_suffixes={"","_broken","_clotted","_drop"};
constexpr std::uint64_t ground_key(unsigned variant) {return ground_blood_material+variant*0x100;}
fb::Guid ground_guid(unsigned slot,unsigned variant=0) {
    if (variant) {
        // SplitMix64 gives independent identities, including native TOC hash bits.
        std::uint64_t state=0xc7ac4ef59b81d023ULL+variant*0x100+slot;
        fb::Guid id;
        for (unsigned block=0;block<2;++block) {
            auto n=(state+=0x9e3779b97f4a7c15ULL);
            n=(n^(n>>30))*0xbf58476d1ce4e5b9ULL;
            n=(n^(n>>27))*0x94d049bb133111ebULL; n^=n>>31;
            for (unsigned i=0;i<8;++i) id.bytes[block*8+i]=std::byte(n>>(i*8));
        }
        return id;
    }
    // Independent deterministic IDs avoid low-bit-correlated native TOC hash buckets.
    std::array<unsigned char,16> bytes{};
    switch (slot) {
    case 0x10: bytes={0xfe,0x17,0x48,0xbd,0xef,0xdc,0x1e,0x5a,0x8a,0xf4,0xff,0x7c,0x85,0x89,0x64,0x31}; break;
    case 0x11: bytes={0x7d,0x6d,0xd9,0x0c,0x33,0xde,0xab,0x50,0xa3,0x00,0x8a,0xdd,0x7d,0xf2,0x9f,0x22}; break;
    case 0x12: bytes={0x64,0xdf,0xad,0x4e,0x08,0xd8,0x6b,0x59,0x97,0xa8,0x2e,0x8d,0x2e,0x90,0x1a,0xfa}; break;
    case 0x20: bytes={0x82,0x75,0x75,0x6d,0x5a,0x37,0xec,0x5c,0xb5,0x07,0x9e,0xa6,0xd5,0x9f,0x40,0xc2}; break;
    case 0x21: bytes={0x63,0x30,0xf1,0x62,0x46,0xa6,0x95,0x54,0x9f,0x18,0x23,0x22,0x51,0xb4,0x6a,0x48}; break;
    case 0x22: bytes={0x7d,0x17,0xf6,0xe4,0x23,0xfb,0x6e,0x54,0x8d,0x92,0x57,0x1b,0xa1,0x8c,0x9f,0x8d}; break;
    case 0x30: bytes={0x8b,0xa6,0xb1,0xb7,0xb2,0xa8,0xca,0x5c,0x85,0x6e,0x95,0x4c,0x21,0x18,0x20,0x01}; break;
    case 0x31: bytes={0x2e,0x12,0xe2,0x0f,0x7e,0xc6,0x60,0x58,0x81,0x7d,0x40,0xf4,0x35,0x22,0xae,0xe4}; break;
    case 0x32: bytes={0xe3,0xcf,0xc9,0x6f,0xe3,0x75,0x5b,0x5a,0xa9,0xed,0x76,0x15,0xe3,0xea,0x5b,0xfd}; break;
    case 0x40: bytes={0xa5,0x30,0x7d,0xbd,0x21,0xf0,0x2a,0x55,0x8a,0xf2,0xd6,0x43,0x08,0x1d,0xd7,0x1d}; break;
    case 0x41: bytes={0x5d,0x4a,0xe2,0x8c,0xb1,0x0e,0xd3,0x57,0xaf,0x16,0x2c,0xf2,0xe2,0x3b,0xea,0x96}; break;
    default: throw std::runtime_error("Unknown blood ground identity");
    }
    fb::Guid id; for (unsigned i=0;i<16;++i) id.bytes[i]=std::byte(bytes[i]); return id;
}
fb::Guid blood_particle_chunk(unsigned color) {
    return color ? ground_guid(0x12,ground_variant_count+color) : private_guid(3,0x302);
}
ebx::InstanceRecord& ground_instance(ebx::Document& d) {return d.instances.at(d.root()-d.instances.data());}
fb::Image blood_smear_image(unsigned variant) {
    fb::Image image{256,128,std::vector<std::uint8_t>(256*128*4)};
    std::uint32_t seed=0xa91f321u+variant*7919;
    auto random=[&]() {seed^=seed<<13; seed^=seed>>17; seed^=seed<<5; return float(seed>>8)/16777216;};
    struct Blot {float u,v,rx,ry,alpha;};
    std::vector<Blot> blots;
    if (variant==3) {
        blots.push_back({0,0,.57f,.59f,.97f});
        for (unsigned i=0;i<6;++i) {
            const float angle=random()*6.283185f,radius=.65f+.12f*random(),size=.03f+.06f*random();
            blots.push_back({std::cos(angle)*radius,std::sin(angle)*radius,size,size,.7f+.25f*random()});
        }
    } else {
        // Unequal overlapping lobes and dragged fingers, with exposed receiver
        // between them. No full rectangular/oval base that repeated stamps fill.
        for (unsigned i=0;i<24;++i) {
            const float u=-.7f+1.4f*random(),v=(random()-.5f)*.85f;
            const float rx=variant==2 ? .06f+.16f*random() : .13f+.24f*random();
            const float ry=variant==1 ? .012f+.065f*random() : .025f+.16f*random();
            blots.push_back({u,v,rx,ry,.38f+.57f*random()});
        }
    }
    for (unsigned y=0;y<image.height;++y) for (unsigned x=0;x<image.width;++x) {
        const float u=(x+.5f)/128-1,v=(y+.5f)/64-1;
        const float rough=.055f*std::sin(u*67+v*43)+.035f*std::sin(u*137-v*79);
        float opacity{};
        for (const auto& blot:blots) {
            const float du=(u-blot.u)/blot.rx,dv=(v-blot.v)/blot.ry;
            opacity=std::max(opacity,std::clamp((1-du*du-dv*dv+rough)*9,0.f,1.f)*blot.alpha);
        }
        if (variant!=3) {
            const float scrape=std::sin(v*191+std::sin(u*13)*.9f);
            opacity*=scrape>.75f ? .12f : .78f+.22f*std::sin(u*31+v*97)*std::sin(v*53);
        }
        opacity*=std::clamp((1-std::max(std::abs(u),std::abs(v)))*20,0.f,1.f);
        const auto at=4*(y*image.width+x);
        // Keep the dark pigment, but give it enough red and opacity to read
        // against asphalt and concrete without adding more decal stamps.
        const float red=72+12*std::sin(u*17+v*11)*std::sin(v*29);
        image.rgba[at]=static_cast<std::uint8_t>(red);
        image.rgba[at+1]=3; image.rgba[at+2]=6;
        image.rgba[at+3]=static_cast<std::uint8_t>(std::clamp(opacity*(variant==3 ? 1.1f : 1.5f),0.f,1.f)*245);
    }
    return image;
}
void verify_blood_chunk_hashes() {
    std::vector<fb::Guid> ids;
    for (unsigned color=0;color<4;++color) ids.push_back(blood_particle_chunk(color));
    for (unsigned variant=0;variant<ground_variant_count;++variant)
        for (unsigned slot:{0x12u,0x22u,0x32u}) ids.push_back(ground_guid(slot,variant));
    std::vector<std::vector<fb::Guid>> buckets(ids.size());
    for (const auto& id:ids) buckets[fb::toc_hash(id.bytes)%ids.size()].push_back(id);
    std::stable_sort(buckets.begin(),buckets.end(),[](const auto& a,const auto& b){return a.size()>b.size();});
    std::vector<bool> used(ids.size());
    for (const auto& bucket:buckets) {
        if (bucket.size()<2) break;
        bool found{};
        for (std::uint32_t seed=1;seed<10000 && !found;++seed) {
            auto occupied=used; bool collision{};
            for (const auto& id:bucket) {
                const auto index=fb::toc_hash(id.bytes,seed)%ids.size();
                if (occupied[index]) {collision=true; break;}
                occupied[index]=true;
            }
            if (!collision) {used=occupied; found=true;}
        }
        if (!found) throw std::runtime_error("Private blood chunks cannot form a bounded native TOC hash");
    }
}
template<class Emit> void author_blood_ground(dingosdk::vfs::GameData& data,Emit&& output) {
    verify_blood_chunk_hashes();
    constexpr auto donor="win32/levels/game/bam_levelroot/wproot/wp/s9600_x0z0/s200_x500z1100/s100_x450z1150/s50_x425z1175_sublevel";
    constexpr auto stem="world/materials/decal/graffiti_large_01_4x2m/decal_graffiti_large_01_4x2m";

    const auto toc=data.read_toc("Win32/levels/game/bam_levelroot/bam_levelroot.toc");
    const auto bundle=data.read_bundle(toc,donor);
    if (!bundle) throw std::runtime_error("Missing surface decal donor bundle");
    auto emit=[&](fb::AssetKind kind,fb::BundleAsset a,const std::vector<std::byte>& bytes) {
        if (kind==fb::AssetKind::resource && !(a.resourceId&1)) throw std::runtime_error("Decal resource IDs must be odd");
        output(kind,a,bytes,true);
    };
    auto read=[&](fb::AssetKind kind,const std::string& name,fb::BundleAsset& a) {
        std::size_t index{}; const auto* found=bundle->find(kind,name,&index);
        if (!found) throw std::runtime_error("Missing decal material donor: "+name);
        a=*found; return data.read(*bundle->payload(kind,index));
    };
    for (unsigned variant=0;variant<ground_variant_count;++variant) {
    const auto output_stem=std::string("world/materials/decal/reskate_blood/reskate_blood")+ground_suffixes[variant%4]+std::string(dingosdk::blood::blood_color_suffixes[variant/4]);
    auto image=blood_smear_image(variant%4);
    for (std::size_t p=0;p<image.rgba.size();p+=4) {
        const auto rgb=dingosdk::blood::blood_pixel(static_cast<dingosdk::blood::BloodColor>(variant/4),image.rgba[p],image.rgba[p+1],image.rgba[p+2]);
        std::copy(rgb.begin(),rgb.end(),image.rgba.begin()+p);
    }

  const auto color_file=ground_guid(0x10,variant);
  const auto color_class=ground_guid(0x11,variant);
  const auto normal_file=ground_guid(0x20,variant);
  const auto normal_class=ground_guid(0x21,variant);
  const auto opacity_file=ground_guid(0x30,variant);
  const auto opacity_class=ground_guid(0x31,variant);
  const auto material_key=ground_key(variant);
  struct Binding {fb::Guid old_file,old_class,new_file,new_class;};
  std::vector<Binding> bindings;
  for(const unsigned kind:{0u,1u,2u}) {
   const bool normal=kind==1,opacity=kind==2;
   const std::string suffix=normal?"_ny":opacity?"_osm":"_c";
   fb::BundleAsset ebx_asset,res_asset;
   auto doc=ebx::read_document(read(fb::AssetKind::ebx,std::string(stem)+suffix,ebx_asset));
   auto& instance=ground_instance(doc);
   const auto file=normal?normal_file:opacity?opacity_file:color_file,klass=normal?normal_class:opacity?opacity_class:color_class;
   bindings.push_back({doc.fileGuid,instance.instanceGuid,file,klass});
   doc.fileGuid=file;instance.instanceGuid=klass;
   const auto name=std::string(output_stem)+suffix;
   field(*instance.object,"Name").data=name;
   const std::uint64_t rid=(normal?0x5ba7a219be430023ULL:opacity?0x5ba7a219be430025ULL:0x5ba7a219be430021ULL)+variant*0x100;
   field(*instance.object,"Resource").data=ebx::ResourceReference{rid};
   const std::uint32_t width=normal?4:image.width,height=normal?4:image.height;
   field(*instance.object,"AuthoredWidth").data=std::uint64_t(width);
   field(*instance.object,"AuthoredHeight").data=std::uint64_t(height);
   auto crop=std::get<std::shared_ptr<ebx::Object>>(field(*instance.object,"CropInfo").data);
   field(*crop,"Z").data=double(width);field(*crop,"W").data=double(height);
   const auto authored_ebx=ebx::write_document(doc);
   const auto checked_ebx=ebx::read_document(authored_ebx);
   if(checked_ebx.fileGuid!=file || checked_ebx.root()->instanceGuid!=klass ||
      std::get<ebx::ResourceReference>(checked_ebx.root()->object->find("Resource")->value.data).id!=rid)
       throw std::runtime_error("Texture EBX round-trip failed");
   ebx_asset.name=name;emit(fb::AssetKind::ebx,ebx_asset,authored_ebx);
   auto texture=read(fb::AssetKind::resource,std::string(stem)+suffix,res_asset);
   if(texture.size()!=180 || get<std::uint32_t>(res_asset.resourceMeta,0)!=12)throw std::runtime_error("Unexpected texture format");
   const auto chunk=normal?ground_guid(0x22,variant):opacity?ground_guid(0x32,variant):ground_guid(0x12,variant);
   put(texture,12,std::uint32_t(normal || opacity?18:20)); // Linear packed data / sRGB color.
   // Retain the donor's native usage flags. Streaming residency is carried
   // separately in RES metadata and the authored mip data.
   put(texture,20,std::uint16_t(1));
   put(res_asset.resourceMeta,4,std::uint32_t(0));
   put(texture,22,std::uint16_t(width));put(texture,24,std::uint16_t(height));
   put(texture,31,std::uint8_t(0));
   std::copy(chunk.bytes.begin(),chunk.bytes.end(),texture.begin()+40);
   std::fill(texture.begin()+56,texture.begin()+116,std::byte{});
   fb::Image mip=image;
   if(normal) {mip={4,4,std::vector<std::uint8_t>(64)};for(std::size_t p=0;p<64;p+=4){mip.rgba[p]=128;mip.rgba[p+1]=128;mip.rgba[p+2]=255;mip.rgba[p+3]=255;}}
   // The stock OSM's red channel is the graffiti opacity mask. Color alpha
   // alone does not supply it. Use a rough surface with no metalness to avoid bright glossy ribbons.
   if(opacity)for(std::size_t p=0;p<mip.rgba.size();p+=4) {mip.rgba[p]=image.rgba[p+3];mip.rgba[p+1]=185;mip.rgba[p+2]=0;mip.rgba[p+3]=255;}
   const auto expected_image=mip;
   std::vector<std::byte> pixels;std::uint8_t count{};
   for(;;) {
    put(texture,56+4*count,std::uint32_t(mip.rgba.size()));++count;
    const auto b=std::as_bytes(std::span(mip.rgba));pixels.insert(pixels.end(),b.begin(),b.end());
    if(mip.width==1 && mip.height==1)break;
    mip=fb::resize(mip,std::max(1u,mip.width/2),std::max(1u,mip.height/2));
   }
   put(texture,30,count);put(texture,116,std::uint32_t(pixels.size()));
   put(texture,120,rid);
   // Texture's first two words and streaming layout describe old mip slices.
   put(texture,0,std::uint32_t(0));put(texture,4,std::uint32_t(0));
   put(texture,32,std::uint32_t(0));put(texture,36,std::uint32_t(0));
   const auto header=fb::read_texture_header(texture,res_asset.resourceMeta);
   const auto decoded=fb::decode_texture(header,pixels,std::min(width,height));
   if(decoded.width!=width || decoded.height!=height || decoded.rgba!=expected_image.rgba)throw std::runtime_error("Texture round-trip failed");
   res_asset.name=name;res_asset.resourceId=rid;emit(fb::AssetKind::resource,res_asset,texture);
   fb::BundleAsset ca;ca.name=chunk.string();ca.guid=chunk;emit(fb::AssetKind::chunk,ca,pixels);
  }
  fb::BundleAsset decal_asset;
  auto decal=ebx::read_document(read(fb::AssetKind::ebx,std::string(stem)+"_dv",decal_asset));
  decal.fileGuid=ground_guid(0x40,variant);
  auto& instance=ground_instance(decal);instance.instanceGuid=ground_guid(0x41,variant);
  field(*instance.object,"Name").data=std::string(output_stem)+"_dv";
  field(*instance.object,"ShaderInstanceKey").data=std::int64_t(material_key);
  const auto authored_decal=ebx::write_document(decal);
  const auto checked_decal=ebx::read_document(authored_decal);
  if(checked_decal.fileGuid!=decal.fileGuid || checked_decal.root()->instanceGuid!=instance.instanceGuid ||
     std::get<std::int64_t>(checked_decal.root()->object->find("ShaderInstanceKey")->value.data)!=std::int64_t(material_key))
      throw std::runtime_error("Decal EBX round-trip failed");
  decal_asset.name=std::string(output_stem)+"_dv";emit(fb::AssetKind::ebx,decal_asset,authored_decal);
  const auto depot_it=std::find_if(bundle->manifest.resources.begin(),bundle->manifest.resources.end(),[](const auto& a){return a.resourceId==2515853284392278695ULL;});
  if(depot_it==bundle->manifest.resources.end())throw std::runtime_error("Missing shader depot");
  auto depot_asset=*depot_it;
  auto depot=data.read(*bundle->payload(fb::AssetKind::resource,depot_it-bundle->manifest.resources.begin()));
  const auto table=get<std::uint64_t>(depot,0);
  const auto count=get<std::uint32_t>(depot_asset.resourceMeta,12);
  std::uint64_t block{};
  for(std::uint32_t i=0;i<count;++i)if(get<std::uint64_t>(depot,table+16*i)==0x9b3233728e841bc0ULL)block=get<std::uint64_t>(depot,table+16*i+8);
  if(!block)throw std::runtime_error("Missing textured shader entry");
  // Publish one new material key. Retain internal offsets and relocation data;
  // the other donor entries are inaccessible from this private depot.
  put(depot,table,material_key);put(depot,table+8,block);
  put(depot_asset.resourceMeta,12,std::uint32_t(1));
  const auto params=get<std::uint64_t>(depot,block);
  const auto params_size=get<std::uint32_t>(depot,block+16);
  if(params+params_size>depot.size())throw std::runtime_error("Invalid persistent parameter block");
  for(const auto& binding:bindings) {
   std::array<std::byte,32> old{},replacement{};
   std::copy(binding.old_class.bytes.begin(),binding.old_class.bytes.end(),old.begin());
   std::copy(binding.old_file.bytes.begin(),binding.old_file.bytes.end(),old.begin()+16);
   std::copy(binding.new_class.bytes.begin(),binding.new_class.bytes.end(),replacement.begin());
   std::copy(binding.new_file.bytes.begin(),binding.new_file.bytes.end(),replacement.begin()+16);
   const auto end=depot.begin()+params+params_size;
   auto pos=std::search(depot.begin()+params,end,old.begin(),old.end());
   if(pos==end)throw std::runtime_error("Texture binding missing from selected material");
   std::copy(replacement.begin(),replacement.end(),pos);
  }
  // Restore white tint in the selected persistent parameter block.
  for(std::size_t p=params;p+32<=params+params_size;++p)
   if(get<std::uint64_t>(depot,p)==0x8c46cd354b18a9e2ULL && get<std::uint32_t>(depot,p+8)==0x885eff59) {
    if(get<float>(depot,p+20)!=.5f || get<float>(depot,p+24)!=.5f || get<float>(depot,p+28)!=.5f)throw std::runtime_error("Unexpected tint layout");
    put(depot,p+20,1.f);put(depot,p+24,1.f);put(depot,p+28,1.f);
   }
  // The engine caches parsed scopes globally by this hash. Modified texture
  // bindings must not reuse the donor's cache identity.
  std::uint64_t params_hash=14695981039346656037ULL;
  for(std::size_t p=params;p<params+params_size;++p)
   params_hash=(params_hash^std::to_integer<unsigned char>(depot[p]))*1099511628211ULL;
  if(params_hash==get<std::uint64_t>(depot,block+8))throw std::runtime_error("Parameter cache identity did not change");
  put(depot,block+8,params_hash);
  depot_asset.name=std::string("world/materials/decal/reskate_blood/shaderblockdepot_blood_ground")+ground_suffixes[variant%4]+std::string(dingosdk::blood::blood_color_suffixes[variant/4]);
  depot_asset.resourceId=0x5ba7a219be430029ULL+variant*0x100;emit(fb::AssetKind::resource,depot_asset,depot);
}
}
}
