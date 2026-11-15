#include "editor/jumplist.hh"

#include <cstring>
#include <iterator>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#include <objbase.h>
#include <objectarray.h>
#include <propidl.h>
#include <propsys.h>
#include <shobjidl.h>
#endif

namespace editor {

namespace fs = std::filesystem;

namespace {

std::string utf8(const fs::path& p) {
    const std::u8string s = p.u8string();
    return std::string(s.begin(), s.end());
}

}  // namespace

std::string quoteArgument(const std::string& arg) {
    std::string out = "\"";
    size_t slashes = 0;
    for (char c : arg) {
        if (c == '\\') {
            ++slashes;
            continue;
        }
        // Backslashes count only before a quote: there each one is doubled.
        out.append(c == '"' ? slashes * 2 + 1 : slashes, '\\');
        out += c;
        slashes = 0;
    }
    out.append(slashes * 2, '\\');
    return out + "\"";
}

std::vector<JumpListEntry> jumpListEntries(const std::vector<fs::path>& projects) {
    std::vector<JumpListEntry> out;
    for (const fs::path& p : projects) {
        if (out.size() == kJumpListMax) break;
        std::error_code ec;
        if (p.empty() || !fs::is_directory(p, ec)) continue;  // --project wants a folder
        JumpListEntry e;
        e.description = utf8(p);
        e.title = p.has_filename() ? utf8(p.filename()) : e.description;
        e.arguments = "--project " + quoteArgument(e.description);
        out.push_back(std::move(e));
    }
    return out;
}

#ifdef _WIN32

namespace {

constexpr wchar_t kAppId[] = L"psxsplash.SplashEdit";

std::wstring widen(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())).wstring(); }

std::wstring ownExe() {
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return {};
        if (n < buf.size()) return buf.substr(0, n);
        buf.resize(buf.size() * 2);
    }
}

// System.Title, spelled out rather than taken from propsys's PKEY_Title.
constexpr PROPERTYKEY kTitleKey = {{0xF29F85E0, 0x4FF9, 0x1068, {0xAB, 0x91, 0x08, 0x00, 0x2B, 0x27, 0xB3, 0xD9}}, 2};

template <class T>
void release(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

// A shell link that runs `exe` with the entry's arguments, or null.
IShellLinkW* makeLink(const std::wstring& exe, const JumpListEntry& e) {
    IShellLinkW* link = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void**>(&link))))
        return nullptr;
    const std::wstring args = widen(e.arguments), desc = widen(e.description), title = widen(e.title);
    bool ok = SUCCEEDED(link->SetPath(exe.c_str())) && SUCCEEDED(link->SetArguments(args.c_str()));
    if (ok) link->SetDescription(desc.c_str());  // a tooltip only: too long a path is no reason to drop it
    if (ok) link->SetIconLocation(exe.c_str(), 0);
    IPropertyStore* props = nullptr;
    if (ok && SUCCEEDED(link->QueryInterface(IID_IPropertyStore, reinterpret_cast<void**>(&props)))) {
        PROPVARIANT v;
        PropVariantInit(&v);
        const size_t bytes = (title.size() + 1) * sizeof(wchar_t);
        v.vt = VT_LPWSTR;
        v.pwszVal = static_cast<LPWSTR>(CoTaskMemAlloc(bytes));
        ok = v.pwszVal != nullptr;
        if (ok) {
            std::memcpy(v.pwszVal, title.c_str(), bytes);
            ok = SUCCEEDED(props->SetValue(kTitleKey, v)) && SUCCEEDED(props->Commit());
        }
        PropVariantClear(&v);
        release(props);
    } else {
        ok = false;
    }
    if (!ok) release(link);
    return link;
}

// The arguments of each link the user removed from the list: Windows refuses
// a list that puts one back.
std::vector<std::wstring> removedArguments(IObjectArray* removed) {
    std::vector<std::wstring> out;
    UINT n = 0;
    if (!removed || FAILED(removed->GetCount(&n))) return out;
    for (UINT i = 0; i < n; ++i) {
        IShellLinkW* link = nullptr;
        if (FAILED(removed->GetAt(i, IID_IShellLinkW, reinterpret_cast<void**>(&link)))) continue;
        wchar_t buf[INFOTIPSIZE * 4] = {};
        if (SUCCEEDED(link->GetArguments(buf, static_cast<int>(std::size(buf))))) out.emplace_back(buf);
        release(link);
    }
    return out;
}

void writeList(const std::vector<JumpListEntry>& entries) {
    const std::wstring exe = ownExe();
    if (exe.empty()) return;
    ICustomDestinationList* list = nullptr;
    if (FAILED(CoCreateInstance(CLSID_DestinationList, nullptr, CLSCTX_INPROC_SERVER, IID_ICustomDestinationList,
                                reinterpret_cast<void**>(&list))))
        return;
    list->SetAppID(kAppId);
    UINT slots = 0;
    IObjectArray* removed = nullptr;
    if (FAILED(list->BeginList(&slots, IID_IObjectArray, reinterpret_cast<void**>(&removed)))) {
        release(list);
        return;
    }
    const std::vector<std::wstring> gone = removedArguments(removed);
    release(removed);
    IObjectCollection* items = nullptr;
    UINT added = 0;
    if (SUCCEEDED(CoCreateInstance(CLSID_EnumerableObjectCollection, nullptr, CLSCTX_INPROC_SERVER, IID_IObjectCollection,
                                   reinterpret_cast<void**>(&items)))) {
        for (const JumpListEntry& e : entries) {
            const std::wstring args = widen(e.arguments);
            bool skip = false;
            for (const std::wstring& g : gone) skip |= g == args;
            if (skip) continue;
            IShellLinkW* link = makeLink(exe, e);
            if (!link) continue;
            if (SUCCEEDED(items->AddObject(link))) ++added;
            release(link);
        }
    }
    bool ok = items != nullptr;
    if (ok && added) {
        IObjectArray* array = nullptr;
        ok = SUCCEEDED(items->QueryInterface(IID_IObjectArray, reinterpret_cast<void**>(&array)));
        // Fails when the user turned off recent items in Jump Lists: then no list.
        ok = ok && SUCCEEDED(list->AppendCategory(L"Recent projects", array));
        release(array);
    }
    release(items);
    if (ok)
        list->CommitList();
    else
        list->AbortList();
    release(list);
}

}  // namespace

void setTaskbarAppId() { SetCurrentProcessExplicitAppUserModelID(kAppId); }

void updateJumpList(const std::vector<fs::path>& projects) {
    const std::vector<JumpListEntry> entries = jumpListEntries(projects);
    // SDL or a file dialog may have set this thread's apartment already: a
    // second call that succeeds (S_OK or S_FALSE) is ours to undo, a mode
    // clash (RPC_E_CHANGED_MODE) leaves COM usable and is not.
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(init) && init != RPC_E_CHANGED_MODE) return;
    writeList(entries);
    if (SUCCEEDED(init)) CoUninitialize();
}

#else

void setTaskbarAppId() {}

void updateJumpList(const std::vector<fs::path>&) {}

#endif

}  // namespace editor
