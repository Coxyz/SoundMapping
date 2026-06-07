#include "MixEngine.h"
#include "../audio/AudioSessionManager.h"

#include <windows.h>
#include <fstream>
#include <sstream>
#include <cstdlib>

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

MixEngine::MixEngine() {
    const wchar_t* defaults[kChannels] = { L"Game", L"Chat", L"Media", L"Aux" };
    for (int i = 0; i < kChannels; ++i) chans_[i].name = defaults[i];
}

MixEngine::~MixEngine() { Stop(); }

void MixEngine::Start()  { if (running_) return; running_ = true; Refresh(); }
void MixEngine::Stop()   { running_ = false; active_.clear(); }

void MixEngine::SetFallback(const std::wstring& outputId) {
    fallback_ = outputId;
    if (running_) Refresh();
}

void MixEngine::SetOutput(int ch, const std::wstring& outputId) {
    chans_[ch].outputId = outputId;
    if (running_) Refresh();
}

void MixEngine::SetChanVolume(int ch, float v) {
    chans_[ch].settings.volume = v;
    for (auto& kv : active_) if (kv.second.channel == ch) kv.second.router->SetChanVolume(v);
}
void MixEngine::SetChanBassDb(int ch, double db) {
    chans_[ch].settings.bassDb = db;
    for (auto& kv : active_) if (kv.second.channel == ch) kv.second.router->SetChanBassDb(db);
}
void MixEngine::SetChanTrebleDb(int ch, double db) {
    chans_[ch].settings.trebleDb = db;
    for (auto& kv : active_) if (kv.second.channel == ch) kv.second.router->SetChanTrebleDb(db);
}

int MixEngine::ChannelOfApp(const std::wstring& app) const {
    for (int i = 0; i < kChannels; ++i)
        if (chans_[i].apps.count(app)) return i;
    return -1;
}

void MixEngine::AssignApp(const std::wstring& app, int ch) {
    for (auto& c : chans_) c.apps.erase(app);
    if (ch >= 0 && ch < kChannels) chans_[ch].apps.insert(app);
    if (running_) Refresh();
}

MixEngine::Settings MixEngine::GetApp(const std::wstring& app) const {
    auto it = appSettings_.find(app);
    return (it == appSettings_.end()) ? Settings{} : it->second;
}
void MixEngine::SetAppVolume(const std::wstring& app, float v) {
    appSettings_[app].volume = v;
    for (auto& kv : active_) if (kv.second.app == app) kv.second.router->SetAppVolume(v);
}
void MixEngine::SetAppBassDb(const std::wstring& app, double db) {
    appSettings_[app].bassDb = db;
    for (auto& kv : active_) if (kv.second.app == app) kv.second.router->SetAppBassDb(db);
}
void MixEngine::SetAppTrebleDb(const std::wstring& app, double db) {
    appSettings_[app].trebleDb = db;
    for (auto& kv : active_) if (kv.second.app == app) kv.second.router->SetAppTrebleDb(db);
}

void MixEngine::StartApp(DWORD pid, const std::wstring& app,
                         const std::wstring& out, int channel) {
    auto router = std::make_unique<AppAudioRouter>();
    if (!router->Start(pid, out)) return;

    const Settings a = GetApp(app);                 // etage appli
    router->SetAppVolume(a.volume);
    router->SetAppBassDb(a.bassDb);
    router->SetAppTrebleDb(a.trebleDb);

    Settings c;                                      // etage canal (flat si fallback)
    if (channel >= 0) c = chans_[channel].settings;
    router->SetChanVolume(c.volume);
    router->SetChanBassDb(c.bassDb);
    router->SetChanTrebleDb(c.trebleDb);

    active_[pid] = Active{ std::move(router), channel, out, app };
}

void MixEngine::Refresh() {
    if (!running_) return;
    const DWORD self = GetCurrentProcessId();

    struct Want { int channel; std::wstring out; std::wstring app; };
    std::map<DWORD, Want> want;
    for (auto& s : ListSessions()) {
        if (s.Pid() == 0 || s.Pid() == self) continue;
        const int ch = ChannelOfApp(s.Name());
        Want w;
        if (ch >= 0 && !chans_[ch].outputId.empty()) { w.channel = ch; w.out = chans_[ch].outputId; }
        else                                         { w.channel = -1; w.out = fallback_; }
        if (w.out.empty()) continue;
        w.app = s.Name();
        want[s.Pid()] = w;
    }

    for (auto it = active_.begin(); it != active_.end(); ) {
        auto w = want.find(it->first);
        if (w == want.end() || w->second.out != it->second.outputId ||
            w->second.channel != it->second.channel)
            it = active_.erase(it);
        else
            ++it;
    }
    for (auto& kv : want)
        if (active_.find(kv.first) == active_.end())
            StartApp(kv.first, kv.second.app, kv.second.out, kv.second.channel);
}

void MixEngine::Save() const {
    const std::wstring path = ConfigPath();
    if (path.empty()) return;
    std::ofstream f(path.c_str());
    if (!f) return;

    for (int i = 0; i < kChannels; ++i) {
        const Channel& c = chans_[i];
        f << "CH\t" << i << "\t" << ToUtf8(c.name) << "\t" << ToUtf8(c.outputId) << "\t"
          << c.settings.volume << "\t" << c.settings.bassDb << "\t" << c.settings.trebleDb << "\n";
        for (const auto& app : c.apps)
            f << "APP\t" << i << "\t" << ToUtf8(app) << "\n";
    }
    for (const auto& kv : appSettings_) {
        f << "AV\t" << ToUtf8(kv.first) << "\t"
          << kv.second.volume << "\t" << kv.second.bassDb << "\t" << kv.second.trebleDb << "\n";
    }
}

void MixEngine::Load() {
    const std::wstring path = ConfigPath();
    if (path.empty()) return;
    std::ifstream f(path.c_str());
    if (!f) return;

    std::string line;
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string tag;
        if (!std::getline(ss, tag, '\t')) continue;

        if (tag == "CH") {
            std::string idx, name, out, vol, bass, treb;
            std::getline(ss, idx, '\t');
            const int i = std::atoi(idx.c_str());
            if (i < 0 || i >= kChannels) continue;
            std::getline(ss, name, '\t');
            std::getline(ss, out,  '\t');
            std::getline(ss, vol,  '\t');
            std::getline(ss, bass, '\t');
            std::getline(ss, treb, '\t');
            if (!name.empty()) chans_[i].name = FromUtf8(name);
            chans_[i].outputId          = FromUtf8(out);
            chans_[i].settings.volume   = vol.empty()  ? 1.0f : (float)std::atof(vol.c_str());
            chans_[i].settings.bassDb   = bass.empty() ? 0.0  : std::atof(bass.c_str());
            chans_[i].settings.trebleDb = treb.empty() ? 0.0  : std::atof(treb.c_str());
        } else if (tag == "APP") {
            std::string idx, app;
            std::getline(ss, idx, '\t');
            const int i = std::atoi(idx.c_str());
            std::getline(ss, app, '\t');
            if (i >= 0 && i < kChannels && !app.empty())
                chans_[i].apps.insert(FromUtf8(app));
        } else if (tag == "AV") {
            std::string name, vol, bass, treb;
            std::getline(ss, name, '\t');
            std::getline(ss, vol,  '\t');
            std::getline(ss, bass, '\t');
            std::getline(ss, treb, '\t');
            if (!name.empty()) {
                Settings s;
                s.volume   = vol.empty()  ? 1.0f : (float)std::atof(vol.c_str());
                s.bassDb   = bass.empty() ? 0.0  : std::atof(bass.c_str());
                s.trebleDb = treb.empty() ? 0.0  : std::atof(treb.c_str());
                appSettings_[FromUtf8(name)] = s;
            }
        }
    }
}
