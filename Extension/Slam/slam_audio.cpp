#include "slam_audio.h"
#include "Extension/UI/Overlay/overlay.h"
#include "Engine/Core/Log/logging.h"
#include <Windows.h>
#include <xaudio2.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <future>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace dingosdk::slam {
namespace {
struct ComApartment {
    ComApartment() {
        const auto result=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
        if (FAILED(result)) throw std::runtime_error(std::format("Impact audio COM initialization failed (0x{:08X}).",
            static_cast<std::uint32_t>(result)));
    }
    ~ComApartment() {CoUninitialize();}
};
struct Audio {
    Microsoft::WRL::ComPtr<IXAudio2> engine;
    IXAudio2MasteringVoice* master{};
    std::array<IXAudio2SourceVoice*,4> voices{};
    std::vector<std::int16_t> thud=make_impact_sound(false);
    std::array<std::vector<std::int16_t>,4> cracks{make_impact_sound(true,0),make_impact_sound(true,1),
        make_impact_sound(true,2),make_impact_sound(true,3)};
    std::vector<std::int16_t> custom_crack;
    std::string status="Impact and bone-crack sounds ready.";
    ~Audio() {
        for (auto* voice : voices) if (voice) voice->DestroyVoice();
        if (master) master->DestroyVoice();
    }
    void stop() {
        for (auto* voice : voices) {
            if (voice) {voice->Stop(); voice->FlushSourceBuffers();}
        }
    }
    bool playing() const {
        for (auto* voice : voices) {
            XAUDIO2_VOICE_STATE state{};
            voice->GetState(&state,XAUDIO2_VOICE_NOSAMPLESPLAYED);
            if (state.BuffersQueued) return true;
        }
        return false;
    }
    bool play(const std::vector<std::int16_t>& samples,float volume,float ratio) {
        for (auto* voice : voices) {
            XAUDIO2_VOICE_STATE state{};
            voice->GetState(&state,XAUDIO2_VOICE_NOSAMPLESPLAYED);
            if (state.BuffersQueued) continue;
            if (FAILED(voice->SetVolume(volume)) || FAILED(voice->SetFrequencyRatio(ratio))) continue;
            XAUDIO2_BUFFER buffer{};
            buffer.Flags=XAUDIO2_END_OF_STREAM;
            buffer.AudioBytes=static_cast<UINT32>(samples.size()*sizeof(samples.front()));
            buffer.pAudioData=reinterpret_cast<const BYTE*>(samples.data());
            if (SUCCEEDED(voice->SubmitSourceBuffer(&buffer)) && SUCCEEDED(voice->Start())) return true;
            voice->Stop(); voice->FlushSourceBuffers();
        }
        return false;
    }
};
std::shared_ptr<Audio> prepare(const std::string& path) {
    // std::async uses a fresh worker: COM initialization on the game thread
    // does not initialize the audio preparation thread.
    const ComApartment apartment;
    auto audio=std::make_shared<Audio>();
    if (!path.empty()) {
        std::string_view error="Could not read the WAV file.";
        try {
            std::ifstream file(std::filesystem::path(std::u8string(path.begin(),path.end())),std::ios::binary|std::ios::ate);
            const auto size=file ? file.tellg() : std::streampos(-1);
            if (!file) error="Could not open the WAV file. Check its path.";
            else if (size<44) error="WAV file is truncated.";
            else if (size>static_cast<std::streamoff>(max_impact_wav_bytes)) error="WAV file exceeds the 1 MiB limit.";
            else {
                std::vector<std::byte> bytes(static_cast<std::size_t>(size));
                file.seekg(0);
                if (file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()))) {
                    if (auto pcm=decode_impact_wav(bytes,&error)) audio->custom_crack=std::move(*pcm);
                }
            }
        } catch (const std::exception&) {audio->custom_crack.clear();}
        audio->status=audio->custom_crack.empty() ?
            "Custom crack unavailable: "+std::string(error)+" Using built-in cracks." :
            "Custom bone-crack sound ready.";
    }
    const auto engine_result=XAudio2Create(&audio->engine);
    if (FAILED(engine_result)) throw std::runtime_error(std::format("Impact audio engine is unavailable (0x{:08X}).",
        static_cast<std::uint32_t>(engine_result)));
    const auto device_result=audio->engine->CreateMasteringVoice(&audio->master,2,48000,0,nullptr,nullptr,AudioCategory_GameEffects);
    if (FAILED(device_result)) throw std::runtime_error(std::format("Impact audio device is unavailable (0x{:08X}).",
        static_cast<std::uint32_t>(device_result)));
    const WAVEFORMATEX format{WAVE_FORMAT_PCM,1,48000,96000,2,16,0};
    for (auto*& voice : audio->voices)
        if (FAILED(audio->engine->CreateSourceVoice(&voice,&format,0,1.2f)))
            throw std::runtime_error("Impact sound voice is unavailable.");
    return audio;
}
struct State {
    std::shared_ptr<Audio> audio;
    std::future<std::shared_ptr<Audio>> loading;
    std::string status="Impact sound has not been prepared.";
    std::string loaded_path,loading_path;
    std::uint64_t observed_ms{}, played_ms{}, retry_ms{};
    std::uint64_t preview_until{};
    std::string preview_path;
    bool preview_playing{};
    bool stopped=true;
};
State& state() {static auto* value=new State; return *value;}
bool window_focused() {
    DWORD process{};
    GetWindowThreadProcessId(GetForegroundWindow(),&process);
    return process==GetCurrentProcessId();
}
}
std::string update_impact_audio(const VisualOptions& options,const VisualEvents& events,bool active,bool prepare_allowed,
    bool preview_requested) noexcept {
    auto& s=state();
    try {
        const auto now=GetTickCount64();
        const bool foreground=window_focused();
        if (preview_requested && foreground && valid_visual_options(options) && options.fracture_sound && options.fracture_volume>0) {
            s.preview_until=now+5000; s.preview_path=options.fracture_sound_path; s.retry_ms=0;
        }
        if (!foreground || !valid_visual_options(options) || !options.fracture_sound || options.fracture_volume<=0 ||
            s.preview_path!=options.fracture_sound_path) {
            s.preview_until=0;
            if (s.preview_playing && s.audio) {s.audio->stop(); s.stopped=true;}
            if (s.preview_playing && s.audio) s.status=s.audio->status;
            s.preview_playing=false;
        }
        if (s.preview_until && now>=s.preview_until) {
            s.preview_until=0; s.status="Sound preview timed out. Try again.";
        }
        if (s.preview_playing && s.audio && !s.audio->playing()) {
            s.preview_playing=false; s.stopped=true; s.status=s.audio->status;
        }
        const bool preview_pending=s.preview_until!=0;
        const bool device_wanted=(((active || prepare_allowed) && overlay::keyboard_shortcuts_allowed()) || preview_pending) &&
            valid_visual_options(options) && foreground &&
            ((options.impact_sound && options.sound_volume>0) || (options.fracture_sound && options.fracture_volume>0));
        const bool enabled=active && device_wanted && overlay::keyboard_shortcuts_allowed();
        const bool new_hit=events.latest_impact_ms && events.latest_impact_ms!=s.observed_ms;
        s.observed_ms=events.latest_impact_ms;
        if (device_wanted && (!s.audio || s.loaded_path!=options.fracture_sound_path ||
            (preview_requested && !options.fracture_sound_path.empty() && s.audio->custom_crack.empty())) &&
            !s.loading.valid() && now>=s.retry_ms) {
            s.status="Preparing impact sound...";
            s.loading_path=options.fracture_sound_path;
            s.loading=std::async(std::launch::async,prepare,s.loading_path);
        }
        if (s.loading.valid() && s.loading.wait_for(std::chrono::milliseconds(0))==std::future_status::ready) {
            auto prepared=s.loading.get();
            if (s.loading_path==options.fracture_sound_path) {
                // Stop queued buffers before releasing the sample storage.
                if (s.audio) s.audio->stop();
                s.audio=std::move(prepared); s.stopped=true; s.preview_playing=false;
                s.loaded_path=s.loading_path; s.status=s.audio->status;
                logging::write(logging::Level::info,logging::Channel::skater,s.status);
            }
        }
        if (preview_pending && s.audio && !s.loading.valid() && s.loaded_path==s.preview_path) {
            s.preview_until=0;
            // A rejected custom file must not sound like a successful preview.
            if (!s.preview_path.empty() && s.audio->custom_crack.empty()) {s.status=s.audio->status; return s.status;}
            s.audio->stop();
            const auto& samples=s.audio->custom_crack.empty() ? s.audio->cracks[0] : s.audio->custom_crack;
            s.preview_playing=s.audio->play(samples,options.fracture_volume,1.f);
            s.stopped=!s.preview_playing;
            s.status=s.preview_playing ? "Playing bone-crack preview." : "Could not play the bone-crack preview.";
            return s.status;
        }
        if (s.preview_playing) return s.status;
        if (preview_pending) return s.status;
        if (!enabled) {
            if (s.audio && !s.stopped) {s.audio->stop(); s.stopped=true;}
            return options.impact_sound || options.fracture_sound ? s.status : "Impact and bone-crack sounds off.";
        }
        const bool crack=events.latest_fracture && options.fracture_sound && options.fracture_volume>0;
        if (!s.audio || s.loaded_path!=options.fracture_sound_path || !new_hit || !events.latest_severity || now<events.latest_impact_ms ||
            now-events.latest_impact_ms>150 || (s.played_ms && now-s.played_ms<(crack ? 40u : 120u)) ||
            (!crack && (!options.impact_sound || options.sound_volume<=0))) return s.status;
        // One layered sound per contact batch, preferring a newly fractured
        // part. Bounded voices/cooldown prevent a pile-up during a ragdoll.
        const auto variant=static_cast<unsigned>((events.latest_impact_ms^(events.latest_bone*0x9e3779b9u))%4);
        const auto& samples=crack ? (s.audio->custom_crack.empty() ? s.audio->cracks[variant] : s.audio->custom_crack) : s.audio->thud;
        const auto strength=std::clamp(std::sqrt(events.latest_severity)/24.f,.25f,1.f);
        const auto ratio=crack && !s.audio->custom_crack.empty() ? 1.f : .92f+static_cast<float>(variant)*.045f;
        if (s.audio->play(samples,(crack ? options.fracture_volume : options.sound_volume)*strength,ratio)) {
            s.played_ms=now; s.stopped=false;
        }
        return s.status;
    } catch (const std::exception& error) {
        if (s.audio) s.audio->stop();
        s.stopped=true; s.preview_until=0; s.preview_playing=false;
        s.audio.reset(); s.retry_ms=GetTickCount64()+10000;
        s.status=error.what();
        logging::write(logging::Level::warning,logging::Channel::skater,s.status);
        return s.status;
    } catch (...) {return "Impact audio unavailable.";}
}
}
