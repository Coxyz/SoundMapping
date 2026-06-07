#include "MixEngine.h"
#include "../audio/AudioSessionManager.h"

#include <windows.h>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <utility>

namespace {

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}
std::wstring FromUtf8(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}
std::wstring ConfigPath() {
    const wchar_t* appdata = _wgetenv(L"APPDATA");
    if (!appdata) return L"";
    std::wstring dir = std::wstring(appdata) + L"\\SoundMapping";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\settings.cfg";
}

} // namespace

MixEngine::Eq MixEngine::DefaultEq() {
    Eq e;
    for (int b = 0; b < ChannelEq::kBands; ++b) e[b] = ChannelEq::Default(b);
    return e;
}

MixEngine::MixEngine() {
    const wchar_t* names[] = { L"Game", L"Chat", L"Media", L"Aux" };
    for (auto n : names) chans_.push_back(Channel{ n, L"", 1.0f, DefaultEq() });
}

MixEngine::~MixEngine() { Stop(); }

void MixEngine::Start() { if (running_) return; running_ = true; Refresh(); }
void MixEngine::Stop()  { running_ = false; active_.clear(); }
void MixEngine::SetFallback(const std::wstring& o) { fallback_ = o; if (running_) Refresh(); }

// --- Canaux ---
std::wstring MixEngine::ChannelName(int ch)   const { return (ch >= 0 && ch < (int)chans_.size()) ? chans_[ch].name : L""; }
std::wstring MixEngine::ChannelOutput(int ch) const { return (ch >= 0 && ch < (int)chans_.size()) ? chans_[ch].outputId : L""; }
float        MixEngine::ChannelVolume(int ch) const { return (ch >= 0 && ch < (int)chans_.size()) ? chans_[ch].volume : 1.0f; }
EqBand MixEngine::ChannelBand(int ch, int band) const {
    if (ch >= 0 && ch < (int)chans_.size() && band >= 0 && band < ChannelEq::kBands) return chans_[ch].eq[band];
    return EqBand{};
}

int MixEngine::AddChannel() {
    Channel c;
    c.name = L"Canal " + std::to_wstring(chans_.size() + 1);
    c.volume = 1.0f;
    c.eq = DefaultEq();
    chans_.push_back(std::move(c));
    return (int)chans_.size() - 1;
}

void MixEngine::RemoveChannel(int ch) {
    if (ch < 0 || ch >= (int)chans_.size()) return;
    chans_.erase(chans_.begin() + ch);
    for (auto& kv : apps_) {                    // recale les cibles des applis
        if (kv.second.channel == ch)      kv.second.channel = -1;
        else if (kv.second.channel > ch)  kv.second.channel -= 1;
    }
    if (running_) Refresh();
}

void MixEngine::SetChannelName(int ch, const std::wstring& n) {
    if (ch >= 0 && ch < (int)chans_.size()) chans_[ch].name = n;
}
void MixEngine::SetChannelOutput(int ch, const std::wstring& o) {
    if (ch < 0 || ch >= (int)chans_.size()) return;
    chans_[ch].outputId = o;
    if (running_) Refresh();
}
void MixEngine::SetChannelVolume(int ch, float v) {
    if (ch < 0 || ch >= (int)chans_.size()) return;
    chans_[ch].volume = v;
    for (auto& kv : active_) if (kv.second.channel == ch) kv.second.router->SetChanVolume(v);
}
void MixEngine::SetChannelBand(int ch, int band, const EqBand& b) {
    if (ch < 0 || ch >= (int)chans_.size() || band < 0 || band >= ChannelEq::kBands) return;
    chans_[ch].eq[band] = b;
    for (auto& kv : active_) if (kv.second.channel == ch) kv.second.router->SetChanBand(band, b);
}

// --- Applications ---
MixEngine::AppConfig& MixEngine::AppRef(const std::wstring& app) {
    auto it = apps_.find(app);
    if (it == apps_.end()) { AppConfig c; c.eq = DefaultEq(); it = apps_.emplace(app, std::move(c)).first; }
    return it->second;
}
MixEngine::AppConfig MixEngine::GetApp(const std::wstring& app) const {
    auto it = apps_.find(app);
    if (it != apps_.end()) return it->second;
    AppConfig c; c.eq = DefaultEq(); return c;
}
void MixEngine::SetAppTargetChannel(const std::wstring& app, int ch) {
    AppConfig& c = AppRef(app); c.channel = ch; c.deviceId.clear(); if (running_) Refresh();
}
void MixEngine::SetAppTargetDevice(const std::wstring& app, const std::wstring& dev) {
    AppConfig& c = AppRef(app); c.channel = -1; c.deviceId = dev; if (running_) Refresh();
}
void MixEngine::ClearAppTarget(const std::wstring& app) {
    AppConfig& c = AppRef(app); c.channel = -1; c.deviceId.clear(); if (running_) Refresh();
}
void MixEngine::SetAppVolume(const std::wstring& app, float v) {
    AppRef(app).volume = v;
    for (auto& kv : active_) if (kv.second.app == app) kv.second.router->SetAppVolume(v);
}
void MixEngine::SetAppBand(const std::wstring& app, int band, const EqBand& b) {
    if (band < 0 || band >= ChannelEq::kBands) return;
    AppRef(app).eq[band] = b;
    // L'EQ "appli" n'agit que si l'appli va en direct (pas dans un canal).
    for (auto& kv : active_)
        if (kv.second.app == app && kv.second.channel < 0) kv.second.router->SetAppBand(band, b);
}

// --- Sliders materiels ---
MixEngine::SliderTarget MixEngine::GetSlider(int i) const {
    return (i >= 0 && i < kSliders) ? sliders_[i] : SliderTarget{};
}
void MixEngine::SetSliderChannel(int i, int ch)              { if (i >= 0 && i < kSliders) sliders_[i] = SliderTarget{ true,  ch, L"" }; }
void MixEngine::SetSliderApp(int i, const std::wstring& app) { if (i >= 0 && i < kSliders) sliders_[i] = SliderTarget{ false, -1, app }; }
void MixEngine::ClearSlider(int i)                           { if (i >= 0 && i < kSliders) sliders_[i] = SliderTarget{}; }

void MixEngine::StartApp(DWORD pid, const std::wstring& app, const std::wstring& out, int channel) {
    auto r = std::make_unique<AppAudioRouter>();
    if (!r->Start(pid, out)) return;

    const AppConfig cfg = GetApp(app);
    r->SetAppVolume(cfg.volume);

    Eq appEq, chanEq;   // par defaut : bandes a gain 0 => transparentes
    if (channel >= 0 && channel < (int)chans_.size()) {
        chanEq = chans_[channel].eq;             // appli dans un canal -> EQ canal
        r->SetChanVolume(chans_[channel].volume);
    } else {
        appEq = cfg.eq;                          // appli en direct -> EQ appli
        r->SetChanVolume(1.0f);
    }
    for (int b = 0; b < ChannelEq::kBands; ++b) { r->SetAppBand(b, appEq[b]); r->SetChanBand(b, chanEq[b]); }

    active_[pid] = Active{ std::move(r), app, channel, out };
}

void MixEngine::Refresh() {
    if (!running_) return;
    const DWORD self = GetCurrentProcessId();

    struct Want { std::wstring app; int channel; std::wstring out; };
    std::map<DWORD, Want> want;
    for (auto& s : ListSessions()) {
        if (s.Pid() == self) continue;   // jamais nous-memes (les sons systeme sont gardes)
        const AppConfig cfg = GetApp(s.Name());
        int channel = -1;
        std::wstring out;
        if (cfg.channel >= 0 && cfg.channel < (int)chans_.size() && !chans_[cfg.channel].outputId.empty()) {
            channel = cfg.channel; out = chans_[cfg.channel].outputId;
        } else if (!cfg.deviceId.empty()) {
            out = cfg.deviceId;
        } else {
            out = fallback_;
        }
        if (out.empty()) continue;
        want[s.Pid()] = Want{ s.Name(), channel, out };
    }

    for (auto it = active_.begin(); it != active_.end(); ) {
        auto w = want.find(it->first);
        if (w == want.end() || w->second.out != it->second.outputId || w->second.channel != it->second.channel)
            it = active_.erase(it);
        else
            ++it;
    }
    for (auto& kv : want)
        if (active_.find(kv.first) == active_.end())
            StartApp(kv.first, kv.second.app, kv.second.out, kv.second.channel);
}

static void WriteEq(std::ostream& f, const MixEngine::Eq& eq) {
    for (int b = 0; b < ChannelEq::kBands; ++b)
        f << "\t" << (eq[b].enabled ? 1 : 0) << "\t" << eq[b].freq
          << "\t" << eq[b].gainDb << "\t" << eq[b].Q;
}
static void ReadEq(std::istringstream& ss, MixEngine::Eq& eq) {
    for (int b = 0; b < ChannelEq::kBands; ++b) {
        std::string en, fr, gn, q;
        std::getline(ss, en, '\t'); std::getline(ss, fr, '\t');
        std::getline(ss, gn, '\t'); std::getline(ss, q,  '\t');
        EqBand band = ChannelEq::Default(b);
        if (!en.empty()) band.enabled = (std::atoi(en.c_str()) != 0);
        if (!fr.empty()) band.freq    = std::atof(fr.c_str());
        if (!gn.empty()) band.gainDb  = std::atof(gn.c_str());
        if (!q.empty())  band.Q       = std::atof(q.c_str());
        eq[b] = band;
    }
}

void MixEngine::Save() const {
    const std::wstring path = ConfigPath();
    if (path.empty()) return;
    std::ofstream f(path.c_str());
    if (!f) return;
    for (const auto& c : chans_) {
        f << "CHAN\t" << ToUtf8(c.name) << "\t" << ToUtf8(c.outputId) << "\t" << c.volume;
        WriteEq(f, c.eq);
        f << "\n";
    }
    for (const auto& kv : apps_) {
        const AppConfig& a = kv.second;
        f << "APPC\t" << ToUtf8(kv.first) << "\t" << a.channel << "\t" << ToUtf8(a.deviceId) << "\t" << a.volume;
        WriteEq(f, a.eq);
        f << "\n";
    }
    for (int i = 0; i < kSliders; ++i) {
        const SliderTarget& t = sliders_[i];
        f << "SLD\t" << i << "\t" << (t.isChannel ? 1 : 0) << "\t" << t.channel << "\t" << ToUtf8(t.app) << "\n";
    }
    f << "SERIAL\t" << ToUtf8(comPort_) << "\t" << baud_ << "\n";
}

void MixEngine::Load() {
    const std::wstring path = ConfigPath();
    if (path.empty()) return;
    std::ifstream f(path.c_str());
    if (!f) return;

    std::vector<Channel>            tmpCh;
    std::map<std::wstring, AppConfig> tmpApps;
    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string tag;
        if (!std::getline(ss, tag, '\t')) continue;

        if (tag == "CHAN") {
            std::string name, out, vol;
            std::getline(ss, name, '\t'); std::getline(ss, out, '\t'); std::getline(ss, vol, '\t');
            Channel c;
            c.name = FromUtf8(name);
            c.outputId = FromUtf8(out);
            c.volume = vol.empty() ? 1.0f : (float)std::atof(vol.c_str());
            c.eq = DefaultEq();
            ReadEq(ss, c.eq);
            tmpCh.push_back(std::move(c));
        } else if (tag == "APPC") {
            std::string name, chs, dev, vol;
            std::getline(ss, name, '\t'); std::getline(ss, chs, '\t');
            std::getline(ss, dev, '\t');  std::getline(ss, vol, '\t');
            AppConfig a;
            a.channel  = chs.empty() ? -1 : std::atoi(chs.c_str());
            a.deviceId = FromUtf8(dev);
            a.volume   = vol.empty() ? 1.0f : (float)std::atof(vol.c_str());
            a.eq = DefaultEq();
            ReadEq(ss, a.eq);
            tmpApps[FromUtf8(name)] = a;
        } else if (tag == "SLD") {
            std::string idx, isch, ch, app;
            std::getline(ss, idx, '\t'); std::getline(ss, isch, '\t');
            std::getline(ss, ch, '\t');  std::getline(ss, app, '\t');
            const int i = std::atoi(idx.c_str());
            if (i >= 0 && i < kSliders) {
                sliders_[i].isChannel = (std::atoi(isch.c_str()) != 0);
                sliders_[i].channel   = ch.empty() ? -1 : std::atoi(ch.c_str());
                sliders_[i].app       = FromUtf8(app);
            }
        } else if (tag == "SERIAL") {
            std::string port, baud;
            std::getline(ss, port, '\t'); std::getline(ss, baud, '\t');
            if (!port.empty()) comPort_ = FromUtf8(port);
            if (!baud.empty()) baud_ = std::atoi(baud.c_str());
        }
    }
    if (!tmpCh.empty()) chans_ = std::move(tmpCh);
    apps_ = std::move(tmpApps);
}
