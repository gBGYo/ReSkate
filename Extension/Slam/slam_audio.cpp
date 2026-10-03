#include "slam_audio.h"
#include "Extension/UI/Overlay/overlay.h"
#include "Engine/Core/Log/logging.h"
#include <Windows.h>
#include <xaudio2.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <future>
#include <memory>
#include <stdexcept>

namespace dingosdk::slam {
namespace {
struct Audio {
    Microsoft::WRL::ComPtr<IXAudio2> engine;
    IXAudio2MasteringVoice* master{};
    std::array<IXAudio2SourceVoice*,4> voices{};
    std::vector<std::int16_t> thud=make_impact_sound(false),crunch=make_impact_sound(true);
    ~Audio() {
        for (auto* voice : voices) if (voice) voice->DestroyVoice();
        if (master) master->DestroyVoice();
    }
    void stop() {
        for (auto* voice : voices) {
            voice->Stop(); voice->FlushSourceBuffers();
        }
    }
};
std::shared_ptr<Audio> prepare() {
    auto audio=std::make_shared<Audio>();
    if (FAILED(XAudio2Create(&audio->engine)) ||
        FAILED(audio->engine->CreateMasteringVoice(&audio->master,2,48000,0,nullptr,nullptr,AudioCategory_GameEffects)))
        throw std::runtime_error("Impact audio device is unavailable.");
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
    std::uint64_t observed_ms{}, played_ms{}, retry_ms{};
    bool stopped=true;
};
State& state() {static auto* value=new State; return *value;}
bool focused() {
    DWORD process{};
    GetWindowThreadProcessId(GetForegroundWindow(),&process);
    return process==GetCurrentProcessId() && overlay::keyboard_shortcuts_allowed();
}
}
std::string update_impact_audio(const VisualOptions& options,const VisualEvents& events,bool active) noexcept {
    auto& s=state();
    try {
        const auto now=GetTickCount64();
        const bool enabled=active && options.impact_sound && options.sound_volume>0 && focused();
        const bool new_hit=events.latest_impact_ms && events.latest_impact_ms!=s.observed_ms;
        s.observed_ms=events.latest_impact_ms;
        if (!enabled) {
            if (s.audio && !s.stopped) {s.audio->stop(); s.stopped=true;}
            return options.impact_sound ? s.status : "Impact sound off.";
        }
        if (!s.audio && !s.loading.valid() && now>=s.retry_ms) {
            s.status="Preparing impact sound...";
            s.loading=std::async(std::launch::async,prepare);
        }
        if (s.loading.valid() && s.loading.wait_for(std::chrono::milliseconds(0))==std::future_status::ready) {
            s.audio=s.loading.get(); s.status="Impact sound ready.";
            logging::write(logging::Level::info,logging::Channel::skater,s.status);
        }
        if (!s.audio || !new_hit || !events.latest_severity || now<events.latest_impact_ms ||
            now-events.latest_impact_ms>150 || (s.played_ms && now-s.played_ms<120)) return s.status;
        // One layered sound per contact batch, preferring a newly fractured
        // part. Bounded voices/cooldown prevent a pile-up during a ragdoll.
        for (auto* voice : s.audio->voices) {
            XAUDIO2_VOICE_STATE voice_state{};
            voice->GetState(&voice_state,XAUDIO2_VOICE_NOSAMPLESPLAYED);
            if (voice_state.BuffersQueued) continue;
            const auto& samples=events.latest_fracture ? s.audio->crunch : s.audio->thud;
            const auto strength=std::clamp(std::sqrt(events.latest_severity)/24.f,.25f,1.f);
            const auto ratio=events.latest_fracture ? .92f : 1.05f;
            if (FAILED(voice->SetVolume(options.sound_volume*strength)) || FAILED(voice->SetFrequencyRatio(ratio))) continue;
            XAUDIO2_BUFFER buffer{};
            buffer.Flags=XAUDIO2_END_OF_STREAM;
            buffer.AudioBytes=static_cast<UINT32>(samples.size()*sizeof(samples.front()));
            buffer.pAudioData=reinterpret_cast<const BYTE*>(samples.data());
            if (SUCCEEDED(voice->SubmitSourceBuffer(&buffer)) && SUCCEEDED(voice->Start())) {
                s.played_ms=now; s.stopped=false;
                break;
            }
            voice->Stop(); voice->FlushSourceBuffers();
        }
        return s.status;
    } catch (const std::exception& error) {
        if (s.audio) s.audio->stop();
        s.stopped=true; s.audio.reset(); s.retry_ms=GetTickCount64()+10000;
        s.status=error.what();
        logging::write(logging::Level::warning,logging::Channel::skater,s.status);
        return s.status;
    } catch (...) {return "Impact audio unavailable.";}
}
}
