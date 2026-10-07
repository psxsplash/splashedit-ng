#include "editor/serial.hh"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace editor {

namespace {

#ifdef _WIN32

std::string lastError() {
    DWORD code = GetLastError();
    char buf[256] = {};
    FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, code, 0, buf, sizeof buf, nullptr);
    std::string s = buf;
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s.empty() ? "error " + std::to_string(code) : s;
}

class SerialLink : public Link {
  public:
    explicit SerialLink(HANDLE h) : h_(h) {}
    ~SerialLink() override { CloseHandle(h_); }
    bool write(const void* data, size_t size) override {
        const char* p = static_cast<const char*>(data);
        while (size) {
            DWORD done = 0;
            if (!WriteFile(h_, p, DWORD(std::min<size_t>(size, 1 << 20)), &done, nullptr)) return false;
            p += done;
            size -= done;
        }
        return true;
    }
    int read(void* data, size_t size, int timeoutMs) override {
        COMMTIMEOUTS t = {};
        // Return as soon as anything arrives, or after timeoutMs with nothing.
        t.ReadIntervalTimeout = MAXDWORD;
        t.ReadTotalTimeoutMultiplier = MAXDWORD;
        t.ReadTotalTimeoutConstant = DWORD(std::max(1, timeoutMs));
        t.WriteTotalTimeoutConstant = 5000;
        if (timeoutMs != lastTimeout_) {
            if (!SetCommTimeouts(h_, &t)) return -1;
            lastTimeout_ = timeoutMs;
        }
        DWORD got = 0;
        if (!ReadFile(h_, data, DWORD(size), &got, nullptr)) return -1;
        return int(got);
    }

  private:
    HANDLE h_;
    int lastTimeout_ = -2;
};

std::unique_ptr<Link> openSerial(const std::string& name, int baud, std::string* err) {
    std::string path = name.rfind("\\\\.\\", 0) == 0 ? name : "\\\\.\\" + name;
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (err) *err = "Cannot open " + name + ": " + lastError();
        return nullptr;
    }
    DCB dcb = {};
    dcb.DCBlength = sizeof dcb;
    GetCommState(h, &dcb);
    dcb.BaudRate = DWORD(baud);
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = TWOSTOPBITS;
    dcb.fBinary = TRUE;
    dcb.fParity = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fOutxDsrFlow = FALSE;
    dcb.fDsrSensitivity = FALSE;
    dcb.fOutX = FALSE;
    dcb.fInX = FALSE;
    dcb.fNull = FALSE;
    dcb.fAbortOnError = FALSE;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;
    if (!SetCommState(h, &dcb)) {
        if (err) *err = "Cannot set up " + name + ": " + lastError();
        CloseHandle(h);
        return nullptr;
    }
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return std::make_unique<SerialLink>(h);
}

class SocketLink : public Link {
  public:
    explicit SocketLink(SOCKET s) : s_(s) {}
    ~SocketLink() override {
        closesocket(s_);
        WSACleanup();
    }
    bool write(const void* data, size_t size) override {
        const char* p = static_cast<const char*>(data);
        while (size) {
            int n = send(s_, p, int(std::min<size_t>(size, 1 << 20)), 0);
            if (n <= 0) return false;
            p += n;
            size -= size_t(n);
        }
        return true;
    }
    int read(void* data, size_t size, int timeoutMs) override {
        WSAPOLLFD pfd = {};
        pfd.fd = s_;
        pfd.events = POLLRDNORM;
        int r = WSAPoll(&pfd, 1, timeoutMs);
        if (r < 0) return -1;
        if (r == 0) return 0;
        int n = recv(s_, static_cast<char*>(data), int(size), 0);
        return n > 0 ? n : -1;
    }

  private:
    SOCKET s_;
};

#else

std::string lastError() { return std::strerror(errno); }

#ifdef MSG_NOSIGNAL
constexpr int kSendFlags = MSG_NOSIGNAL;
#else
constexpr int kSendFlags = 0;  // macOS: SO_NOSIGPIPE is set on the socket instead
#endif

// Waits for fd to be readable; 1 ready, 0 timeout, -1 error.
int waitReadable(int fd, int timeoutMs) {
    pollfd pfd = {fd, POLLIN, 0};
    for (;;) {
        int r = poll(&pfd, 1, timeoutMs);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return r;
        return 1;
    }
}

class FdLink : public Link {
  public:
    FdLink(int fd, bool socket) : fd_(fd), socket_(socket) {}
    ~FdLink() override { close(fd_); }
    bool write(const void* data, size_t size) override {
        const char* p = static_cast<const char*>(data);
        while (size) {
            ssize_t n = socket_ ? send(fd_, p, size, kSendFlags) : ::write(fd_, p, size);
            if (n < 0 && (errno == EINTR || errno == EAGAIN)) {
                if (errno == EAGAIN) {
                    pollfd pfd = {fd_, POLLOUT, 0};
                    poll(&pfd, 1, 100);
                }
                continue;
            }
            if (n <= 0) return false;
            p += n;
            size -= size_t(n);
        }
        if (!socket_) tcdrain(fd_);
        return true;
    }
    int read(void* data, size_t size, int timeoutMs) override {
        int r = waitReadable(fd_, timeoutMs);
        if (r <= 0) return r;
        ssize_t n = ::read(fd_, data, size);
        if (n < 0 && (errno == EINTR || errno == EAGAIN)) return 0;
        return n > 0 ? int(n) : -1;
    }

  private:
    int fd_;
    bool socket_;
};

speed_t speedFor(int baud) {
    switch (baud) {
        case 9600: return B9600;
        case 19200: return B19200;
        case 38400: return B38400;
        case 57600: return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        default: return 0;
    }
}

std::unique_ptr<Link> openSerial(const std::string& name, int baud, std::string* err) {
    speed_t speed = speedFor(baud);
    if (!speed) {
        if (err) *err = "Unsupported baud rate " + std::to_string(baud);
        return nullptr;
    }
    int fd = open(name.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        if (err) *err = "Cannot open " + name + ": " + lastError();
        return nullptr;
    }
    termios tio = {};
    if (tcgetattr(fd, &tio) != 0) {
        if (err) *err = "Cannot set up " + name + ": " + lastError();
        close(fd);
        return nullptr;
    }
    cfmakeraw(&tio);
    cfsetispeed(&tio, speed);
    cfsetospeed(&tio, speed);
    tio.c_cflag &= ~(CSIZE | PARENB);
    tio.c_cflag |= CS8 | CSTOPB | CLOCAL | CREAD;
#ifdef CRTSCTS
    tio.c_cflag &= ~CRTSCTS;
#endif
    tio.c_iflag &= ~(IXON | IXOFF | IXANY);
    tio.c_cc[VMIN] = 0;
    tio.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSANOW, &tio) != 0) {
        if (err) *err = "Cannot set up " + name + ": " + lastError();
        close(fd);
        return nullptr;
    }
    int lines = TIOCM_DTR | TIOCM_RTS;
    ioctl(fd, TIOCMBIS, &lines);
    tcflush(fd, TCIOFLUSH);
    return std::make_unique<FdLink>(fd, false);
}

#endif

std::unique_ptr<Link> openTcp(const std::string& hostPort, std::string* err) {
    size_t colon = hostPort.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 == hostPort.size()) {
        if (err) *err = "Expected tcp:HOST:PORT, got tcp:" + hostPort;
        return nullptr;
    }
    std::string host = hostPort.substr(0, colon), port = hostPort.substr(colon + 1);
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        if (err) *err = "Winsock did not start";
        return nullptr;
    }
#endif
    addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res) {
        if (err) *err = "Cannot resolve " + host;
#ifdef _WIN32
        WSACleanup();
#endif
        return nullptr;
    }
    std::string why = "no address";
    for (addrinfo* a = res; a; a = a->ai_next) {
#ifdef _WIN32
        SOCKET s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == INVALID_SOCKET) continue;
        if (connect(s, a->ai_addr, int(a->ai_addrlen)) == 0) {
            BOOL one = TRUE;
            setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&one), sizeof one);
            freeaddrinfo(res);
            return std::make_unique<SocketLink>(s);
        }
        why = lastError();
        closesocket(s);
#else
        int s = socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s < 0) continue;
        if (connect(s, a->ai_addr, a->ai_addrlen) == 0) {
            int one = 1;
            setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
#ifdef SO_NOSIGPIPE
            setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
            freeaddrinfo(res);
            return std::make_unique<FdLink>(s, true);
        }
        why = lastError();
        close(s);
#endif
    }
    freeaddrinfo(res);
#ifdef _WIN32
    WSACleanup();
#endif
    if (err) *err = "Cannot connect to " + hostPort + ": " + why;
    return nullptr;
}

}  // namespace

std::unique_ptr<Link> openLink(const std::string& spec, int baud, std::string* err) {
    if (spec.empty()) {
        if (err) *err = "No serial port set";
        return nullptr;
    }
    if (spec.rfind("tcp:", 0) == 0) return openTcp(spec.substr(4), err);
    return openSerial(spec, baud, err);
}

std::vector<std::string> listSerialPorts() {
    std::vector<std::string> out;
#ifdef _WIN32
    char target[512];
    for (int i = 1; i <= 64; ++i) {
        std::string name = "COM" + std::to_string(i);
        if (QueryDosDeviceA(name.c_str(), target, sizeof target)) out.push_back(name);
    }
#else
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator("/dev", ec)) {
        std::string n = e.path().filename().string();
#ifdef __APPLE__
        if (n.rfind("cu.", 0) == 0 && n != "cu.Bluetooth-Incoming-Port") out.push_back(e.path().string());
#else
        if (n.rfind("ttyUSB", 0) == 0 || n.rfind("ttyACM", 0) == 0) out.push_back(e.path().string());
#endif
    }
    std::sort(out.begin(), out.end());
#endif
    return out;
}

}  // namespace editor
