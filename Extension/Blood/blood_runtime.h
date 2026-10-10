#pragma once
#include "blood_options.h"
#include "blood_telemetry.h"
namespace dingosdk::blood {
struct Snapshot {
    Options options;
    bool ready{}, available{};
    std::size_t sources{}, marks{};
    std::string status="Waiting for a local skater.", save_status;
};
Snapshot snapshot();
bool set_options(const Options&) noexcept;
void tick(std::uintptr_t base,std::uintptr_t client,std::uintptr_t entity,bool ready,bool noclip,bool editor,std::string_view map) noexcept;
void before_level_transition(unsigned next) noexcept;
void observe_selection(std::uintptr_t selector,std::uint32_t next) noexcept;
struct PhysicsContacts {
    std::uintptr_t rig{},entity{},world{};
    std::array<std::uint8_t,24> touching{};
    std::array<std::optional<ContactDetail>,24> details;
    bool valid{};
};
PhysicsContacts capture_contacts(std::uintptr_t rig) noexcept;
void observe_skeleton(std::uintptr_t rig,float seconds,bool wipeout,const PhysicsContacts&) noexcept;
}
