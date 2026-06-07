#include "SerialReader.h"

#include <cstdlib>
#include <string>

SerialReader::~SerialReader() { Stop(); }

bool SerialReader::Start(HWND hwnd, UINT msg, const std::wstring& port, int baud) {
    if (running_.load()) return false;
    if (thread_.joinable()) thread_.join();

    // Prefixe \\.\ : indispensable pour COM10 et au-dela, sans effet avant.
    const std::wstring path = L"\\\\.\\" + port;
    handle_ = CreateFileW(path.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) return false;

    DCB dcb = {};
    dcb.DCBlength = sizeof(dcb);
    GetCommState(handle_, &dcb);
    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    SetCommState(handle_, &dcb);

    COMMTIMEOUTS to = {};
    to.ReadIntervalTimeout        = 50;
    to.ReadTotalTimeoutConstant   = 50;
    to.ReadTotalTimeoutMultiplier = 10;
    SetCommTimeouts(handle_, &to);

    hwnd_ = hwnd;
    msg_  = msg;
    running_.store(true);
    thread_ = std::thread(&SerialReader::Run, this);
    return true;
}

void SerialReader::Stop() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
    if (handle_ != INVALID_HANDLE_VALUE) { CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE; }
}

void SerialReader::Run() {
    std::string line;
    char buf[256];
    while (running_.load()) {
        DWORD n = 0;
        if (!ReadFile(handle_, buf, sizeof(buf), &n, nullptr)) break;   // erreur -> on arrete
        for (DWORD i = 0; i < n; ++i) {
            const char c = buf[i];
            if (c == '\n' || c == '\r') {
                // Format attendu : sld<num>:<val>
                if (line.size() >= 5 && line.compare(0, 3, "sld") == 0) {
                    const size_t colon = line.find(':', 3);
                    if (colon != std::string::npos) {
                        int slider = std::atoi(line.substr(3, colon - 3).c_str());
                        int val    = std::atoi(line.substr(colon + 1).c_str());
                        if (val < 0) val = 0;
                        if (val > 100) val = 100;
                        if (hwnd_) PostMessageW(hwnd_, msg_, (WPARAM)slider, (LPARAM)val);
                    }
                }
                line.clear();
            } else if (line.size() < 64) {
                line.push_back(c);
            }
        }
    }
}
