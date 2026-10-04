#include <Windows.h>
#include <xaudio2.h>
#include "Extension/UI/Overlay/overlay.h"
#include "Engine/Core/Log/logging.h"
#include <iostream>
#include <thread>
#include <fstream>
#include <filesystem>

static bool test_focused=true,shortcuts=false;
static std::uint64_t test_now=1000;
static HWND test_foreground() {return reinterpret_cast<HWND>(1);}
static DWORD test_window_process(HWND,DWORD* process) {*process=test_focused ? GetCurrentProcessId() : 0; return 1;}
static ULONGLONG test_clock() {return test_now;}
static HRESULT test_create_audio(IXAudio2** audio,UINT32 flags=0,XAUDIO2_PROCESSOR processor=XAUDIO2_DEFAULT_PROCESSOR) {
    const auto result=XAudio2Create(audio,flags,processor);
    // Exercise real voices and submitted buffers silently. Buffer completion
    // is controlled below so assertions do not depend on device timing.
    if (SUCCEEDED(result)) (*audio)->StopEngine();
    return result;
}
namespace dingosdk::overlay {bool keyboard_shortcuts_allowed() noexcept {return shortcuts;}}
namespace dingosdk::logging {void write(Level,Channel,std::string_view) noexcept {}}
#define GetForegroundWindow test_foreground
#define GetWindowThreadProcessId test_window_process
#define GetTickCount64 test_clock
#define XAudio2Create test_create_audio
// Compile the production playback code with controlled focus and time, while
// exercising real XAudio2 buffer submission. No game hooks or audible output.
#include "Extension/Slam/slam_audio.cpp"
#undef GetForegroundWindow
#undef GetWindowThreadProcessId
#undef GetTickCount64
#undef XAudio2Create

int failures{};
void check(bool value,const char* message) {
    if (!value) {++failures; std::cerr<<"FAIL: "<<message<<'\n';}
}
int main() {
    using namespace dingosdk::slam;
    struct Fixture {
        std::filesystem::path path=std::filesystem::temp_directory_path()/
            std::format("reskate-slam-audio-{}.wav",GetCurrentProcessId());
        ~Fixture() {std::error_code error; std::filesystem::remove(path,error);}
    } fixture;
    std::vector<std::byte> wav(44+2205*2);
    const auto put=[&](std::size_t at,unsigned value,unsigned count) {
        for (unsigned i=0;i<count;++i) wav[at+i]=static_cast<std::byte>((value>>(i*8))&255);
    };
    std::memcpy(wav.data(),"RIFF",4); put(4,static_cast<unsigned>(wav.size()-8),4);
    std::memcpy(wav.data()+8,"WAVEfmt ",8); put(16,16,4);
    put(20,1,2); put(22,1,2); put(24,11025,4); put(28,22050,4); put(32,2,2); put(34,16,2);
    std::memcpy(wav.data()+36,"data",4); put(40,4410,4);
    for (std::size_t i=44;i<wav.size();i+=2) put(i,1000,2);
    const auto save=[&]() {
        std::ofstream file(fixture.path,std::ios::binary|std::ios::trunc);
        file.write(reinterpret_cast<const char*>(wav.data()),static_cast<std::streamsize>(wav.size()));
        check(file.good(),"The temporary sound fixture can be written");
    };
    save();
    VisualOptions options;
    const auto utf8_path=fixture.path.u8string();
    options.fracture_sound_path.assign(utf8_path.begin(),utf8_path.end());
    VisualEvents events;
    auto update=[&](bool request=false,bool active=false) {
        return update_impact_audio(options,events,active,active,request);
    };
    auto wait=[&]() {
        auto status=update();
        for (unsigned i=0;i<1000 && (state().loading.valid() || state().preview_until);++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5)); status=update();
        }
        return status;
    };
    check(update()=="Impact sound has not been prepared.","Opening a menu does not start unsolicited playback");
    update(true);
    auto status=wait();
    if (status.starts_with("Impact audio engine is unavailable") || status.starts_with("Impact audio device is unavailable")) {
        std::cout<<"SKIP: no XAudio2 output device: "<<status<<'\n';
        return 77;
    }
    check(status=="Playing bone-crack preview.","An explicit preview loads and starts with the menu open");
    auto& s=state();
    if (!s.audio) {std::cerr<<status<<'\n'; return 1;}
    check(s.audio->custom_crack.size()==9600,"Preview uses the 11.025 kHz clip converted to 48 kHz");
    check(s.preview_playing && s.audio->playing(),"Preview submits a real XAudio2 buffer");
    float ratio{},volume{};
    s.audio->voices[0]->GetFrequencyRatio(&ratio); s.audio->voices[0]->GetVolume(&volume);
    check(ratio==1 && volume==options.fracture_volume,"Preview uses original pitch and the configured fracture volume");
    update(); check(s.preview_playing && s.audio->playing(),"Menu suppression does not stop an explicit preview");
    update(true); check(s.preview_playing && s.audio->playing(),"Repeated clicks restart the preview instead of piling up buffers");
    unsigned queued{};
    for (auto* voice : s.audio->voices) {XAUDIO2_VOICE_STATE vs{}; voice->GetState(&vs); queued+=vs.BuffersQueued;}
    check(queued==1,"Repeated previews have only one queued sound");
    test_focused=false; update(); check(!s.preview_playing && !s.audio->playing(),"Losing focus stops the preview");
    test_focused=true; update(); check(!s.preview_playing && !s.audio->playing(),"Regaining focus cannot replay the preview");
    update(true); options.fracture_volume=0; update();
    check(!s.preview_playing && !s.audio->playing(),"Muting fracture volume cancels a playing preview");
    options.fracture_volume=.65f;
    options.fracture_sound_path+=".missing";
    update(true); status=wait();
    check(status.find("Could not open the WAV file")!=std::string::npos && !s.audio->playing(),
        "A missing custom file reports the error and does not preview fallback audio");
    options.fracture_sound_path.assign(utf8_path.begin(),utf8_path.end());
    put(22,2,2); save(); update(true); status=wait();
    check(status.find("Use a mono WAV file")!=std::string::npos && !s.audio->playing(),
        "An unsupported custom format reports the decoder error without previewing fallback audio");
    put(22,1,2); save(); update(true); status=wait();
    check(status=="Playing bone-crack preview." && !s.audio->custom_crack.empty(),
        "Preview retries a previously rejected file after it is repaired at the same path");
    options.fracture_sound_path.clear(); update(true); status=wait();
    check(status=="Playing bone-crack preview." && s.audio->custom_crack.empty(),"An empty path previews the built-in crack");
    s.audio->stop(); update(); check(!s.preview_playing,"Completed playback clears preview state");
    shortcuts=true; test_now=2000;
    events.latest_impact_ms=test_now; events.latest_fracture=true; events.latest_severity=400;
    update(false,true); check(s.audio->playing() && !s.preview_playing,"Gameplay fracture playback still works");
    shortcuts=false; update(false,true); check(!s.audio->playing(),"The menu still suppresses gameplay fracture playback");
    test_focused=false; update(true); check(!s.preview_until,"An out-of-focus preview request is discarded");
    s.audio->stop(); s.audio.reset();
    if (failures) return 1;
    std::cout<<"Audio preview regression checks passed using real XAudio2 voices (silent engine).\n";
}
