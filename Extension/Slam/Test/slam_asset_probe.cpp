// Read-only investigation tool for the supported game's skeleton/physics assets.
#include "Engine/Vfs/game_bundles.h"
#include "Engine/Resource/ebx_document.h"
#include "Engine/Core/Json/json.h"
#include "Extension/Slam/slam_mesh.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <type_traits>

namespace {
namespace ebx = dingosdk::frostbite::ebx;
using dingosdk::Json;
Json describe(const ebx::Value& value, unsigned depth);
Json describe(const ebx::Object& object, unsigned depth) {
    Json out = Json::object();
    if (depth > 6) return "depth limit";
    for (const auto& field : object.fields) out[field.name] = describe(field.value, depth + 1);
    return out;
}
Json describe(const ebx::Value& value, unsigned depth) {
    return std::visit([depth](const auto& v) -> Json {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_arithmetic_v<T> || std::is_same_v<T, std::string>) return v;
        else if constexpr (std::is_same_v<T, ebx::Value::Array>) {
            Json out = Json::array();
            if (depth > 6) return "depth limit";
            for (const auto& item : v) out.push_back(describe(item, depth + 1));
            return out;
        } else if constexpr (std::is_same_v<T, std::shared_ptr<ebx::Object>>) {
            return v ? describe(*v, depth + 1) : Json();
        } else if constexpr (std::is_same_v<T, ebx::ResourceReference>) return Json{{"resourceId", v.id}};
        else if constexpr (std::is_same_v<T, ebx::PointerReference>) return Json{{"kind", static_cast<int>(v.kind)}, {"index", v.index}};
        else if constexpr (std::is_same_v<T, dingosdk::frostbite::Guid>) return v.string();
        else return Json();
    }, value.data);
}
}
int main(int argc, char** argv) {
    if (argc < 3) { std::cerr << "usage: slam_asset_probe <Skate folder> <output folder> [asset filter|list:bundle filter] [toc] [bundle]\n"; return 1; }
    try {
        if (argc>3 && std::string_view(argv[3])=="verify-mesh") {
            const auto mesh=dingosdk::slam::load_dembones_mesh(argv[1]);
            std::cout << "Dem Bones: " << mesh.vertices.size() << " vertices, " << mesh.indices.size()/3
                << " triangles, " << std::count(mesh.required.begin(),mesh.required.end(),true) << " required bones.\n";
            std::array<std::size_t,dingosdk::slam::injury_bone_count> injury_vertices{};
            for (const auto& vertex : mesh.vertices) {
                if (vertex.part>=injury_vertices.size())
                    throw std::runtime_error("Mesh vertex has no valid anatomical injury mapping");
                ++injury_vertices[static_cast<std::size_t>(vertex.part)];
            }
            if (!injury_vertices[101] || !injury_vertices[103] || injury_vertices[102] ||
                mesh.rig.injury_parts[101]!=101 || mesh.rig.injury_parts[102]!=101 || mesh.rig.injury_parts[103]!=103)
                throw std::runtime_error("Head and neck anatomical injury zones are not distinct");
            Json mapping=Json::object();
            for (std::size_t i=0;i<injury_vertices.size();++i)
                if (injury_vertices[i]) mapping[std::to_string(i)]=injury_vertices[i];
            std::filesystem::create_directories(argv[2]);
            std::ofstream(std::filesystem::path(argv[2])/"dembones-injury-vertices.json") << mapping.dump(2);
            // A reviewable, local-only mesh preview; the runtime loads the
            // installed assets and does not depend on this exported evidence.
            std::ofstream file(std::filesystem::path(argv[2])/"dembones-preview.obj");
            for (const auto& vertex : mesh.vertices) file << "v " << vertex.position[0] << ' ' << vertex.position[1] << ' ' << vertex.position[2] << '\n';
            for (std::size_t i=0;i<mesh.indices.size();i+=3) file << "f " << mesh.indices[i]+1 << ' ' << mesh.indices[i+1]+1 << ' ' << mesh.indices[i+2]+1 << '\n';
            if (!file) throw std::runtime_error("Cannot save preview evidence");
            return 0;
        }
        const dingosdk::vfs::GameData data(argv[1]);
        const auto toc = data.read_toc(argc > 4 ? argv[4] : "Win32/levels/game/bam_levelroot/bam_levelroot.toc");
        std::filesystem::create_directories(argv[2]);
        const std::string filter = argc > 3 ? argv[3] : "";
        if (filter.starts_with("chunk:")) {
            const auto found = std::find_if(toc.chunks.begin(), toc.chunks.end(),
                [&](const auto& chunk) { return chunk.guid.string() == filter.substr(6) && !chunk.removed; });
            if (found == toc.chunks.end()) throw std::runtime_error("Requested TOC chunk is missing");
            const auto bytes = data.read({found->location,found->offset,found->size});
            const auto path = std::filesystem::path(argv[2]) / (found->guid.string()+".chunk");
            std::ofstream file(path,std::ios::binary);
            file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
            if (!file) throw std::runtime_error("Cannot save chunk evidence");
            std::cout << path.string() << ": " << bytes.size() << " bytes\n";
            return 0;
        }
        if (filter.starts_with("list:")) {
            Json names = Json::array();
            for (const auto& entry : toc.bundles) {
                names.push_back(entry.name);
                if (entry.name.find(filter.substr(5)) != std::string::npos) std::cout << entry.name << '\n';
            }
            std::ofstream(std::filesystem::path(argv[2]) / "bundles.json") << names.dump(2);
            std::cout << toc.bundles.size() << " bundle names inspected.\n";
            return 0;
        }
        const auto bundle = data.read_bundle(toc, argc > 5 ? argv[5] : "win32/levels/game/bam_levelroot/bam_levelroot");
        if (!bundle) throw std::runtime_error("Requested bundle is missing");
        Json names = Json::array();
        Json resources = Json::array();
        if (argc > 6 && std::string_view(argv[6]) == "--chunks") {
            Json chunks = Json::array();
            for (std::size_t i=0; i<bundle->manifest.chunks.size(); ++i) {
                const auto& chunk = bundle->manifest.chunks[i];
                const auto* payload = bundle->payload(dingosdk::frostbite::AssetKind::chunk,i);
                if (!payload) throw std::runtime_error("Chunk payload is missing");
                const auto bytes = data.read(*payload);
                const auto filename = chunk.guid.string()+".chunk";
                std::ofstream file(std::filesystem::path(argv[2])/filename,std::ios::binary);
                file.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
                if (!file) throw std::runtime_error("Cannot save chunk evidence");
                chunks.push_back(Json{{"id",chunk.guid.string()},{"bytes",bytes.size()},{"file",filename}});
            }
            std::ofstream(std::filesystem::path(argv[2])/"chunks.json") << chunks.dump(2);
        }
        for (std::size_t i = 0; i < bundle->manifest.resources.size(); ++i) {
            const auto& resource = bundle->manifest.resources[i];
            if (resource.name.find(filter.empty() ? "animbase_default" : filter) == std::string::npos) continue;
            resources.push_back(Json{{"name",resource.name},{"id",resource.resourceId},{"type",resource.resourceType},{"bytes",resource.originalSize}});
            const auto* payload = bundle->payload(dingosdk::frostbite::AssetKind::resource, i);
            if (!payload) continue;
            const auto bytes = data.read(*payload);
            auto name = resource.name;
            std::replace(name.begin(), name.end(), '/', '_');
            std::ofstream file(std::filesystem::path(argv[2]) / (name + ".res"), std::ios::binary);
            file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }
        std::ofstream(std::filesystem::path(argv[2]) / "resources.json") << resources.dump(2);
        for (std::size_t i = 0; i < bundle->manifest.ebx.size(); ++i) {
            const auto& asset = bundle->manifest.ebx[i];
            auto name = asset.name;
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (filter.empty() && name.find("skeleton") == std::string::npos && name.find("rig") == std::string::npos &&
                name.find("physics") == std::string::npos && name.find("skater") == std::string::npos) continue;
            names.push_back(asset.name);
            if (argc > 3 && name.find(argv[3]) == std::string::npos) continue;
            const auto* payload = bundle->payload(dingosdk::frostbite::AssetKind::ebx, i);
            if (!payload) continue;
            const auto bytes = data.read(*payload);
            const auto doc = ebx::read_document(bytes);
            Json instances = Json::array();
            for (const auto& instance : doc.instances) {
                if (!instance.object) continue;
                instances.push_back(Json{{"type", doc.types.at(static_cast<std::size_t>(instance.descriptor)).name},
                    {"fields", describe(*instance.object, 0)}});
            }
            std::replace(name.begin(), name.end(), '/', '_');
            std::ofstream(std::filesystem::path(argv[2]) / (name + ".json")) << instances.dump(2);
            Json imports = Json::array();
            for (const auto& ref : doc.imports) imports.push_back(Json{{"file",ref.fileGuid.string()},{"instance",ref.classGuid.string()}});
            std::ofstream(std::filesystem::path(argv[2]) / (name + ".imports.json")) << imports.dump(2);
            if (asset.name.find("Skeleton") != std::string::npos || asset.name.find("skeleton") != std::string::npos)
                std::cout << asset.name << '\n';
        }
        std::ofstream(std::filesystem::path(argv[2]) / "assets.json") << names.dump(2);
        std::cout << "Inspected " << names.size() << " skeleton/skater/physics assets.\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
