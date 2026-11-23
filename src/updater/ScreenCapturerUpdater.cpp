#include <windows.h>
#include <shellapi.h>

#include <iterator>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {
constexpr auto appName = L"ScreenCapturer.exe";
constexpr auto updaterName = L"ScreenCapturerUpdater.exe";
constexpr auto manifestName = L"ScreenCapturer-files.txt";
constexpr auto stagePrefix = L".screencapturer-update-";

class Handle {
public:
    explicit Handle(HANDLE value) : value_(value) {}
    ~Handle() { if (*this) CloseHandle(value_); }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    explicit operator bool() const { return value_ && value_ != INVALID_HANDLE_VALUE; }
    operator HANDLE() const { return value_; }

private:
    HANDLE value_;
};

enum class PathKind { Missing, File, Directory };

PathKind pathKind(const fs::path &path)
{
    const auto attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return PathKind::Missing;
        throw std::runtime_error("Cannot access an update path.");
    }
    if (attributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DEVICE))
        throw std::runtime_error("Update paths cannot contain links or special files.");
    return (attributes & FILE_ATTRIBUTE_DIRECTORY) ? PathKind::Directory : PathKind::File;
}

std::string fileKey(std::string name)
{
    if (name.empty() || name.size() > 512) throw std::runtime_error("Invalid file name in the update manifest.");
    for (auto &ch : name) {
        if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') ||
            ch == '_' || ch == '-' || ch == '.' || ch == '/') continue;
        throw std::runtime_error("Invalid file name in the update manifest.");
    }
    std::string_view remaining(name);
    for (;;) {
        const auto separator = remaining.find('/');
        const auto part = remaining.substr(0, separator);
        const auto stem = part.substr(0, part.find('.'));
        if (part.empty() || part.front() == '.' || part.back() == '.' ||
            stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
            (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) &&
             stem.back() >= '0' && stem.back() <= '9'))
            throw std::runtime_error("Invalid file name in the update manifest.");
        if (separator == std::string_view::npos) break;
        remaining.remove_prefix(separator + 1);
    }
    return name;
}

using Manifest = std::map<std::string, fs::path>;

Manifest readManifest(const fs::path &root)
{
    const auto manifest = root / manifestName;
    if (pathKind(manifest) != PathKind::File || fs::file_size(manifest) > 1024 * 1024)
        throw std::runtime_error("The portable package file manifest is missing or invalid.");
    std::ifstream input(manifest, std::ios::binary);
    Manifest files;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!files.emplace(fileKey(line), fs::path(line)).second)
            throw std::runtime_error("Duplicate file name in the update manifest.");
    }
    if (!input.eof() || !files.contains("screencapturer.exe") ||
        !files.contains("screencapturerupdater.exe") || !files.contains("screencapturer-files.txt"))
        throw std::runtime_error("The portable package file manifest is incomplete.");
    return files;
}

PathKind targetKind(const fs::path &root, const fs::path &relative)
{
    auto path = root;
    for (auto part = relative.begin(); part != relative.end(); ++part) {
        path /= *part;
        const auto kind = pathKind(path);
        if (std::next(part) == relative.end() || kind == PathKind::Missing) return kind;
        if (kind != PathKind::Directory) throw std::runtime_error("A file blocks an update directory.");
    }
    throw std::runtime_error("Empty update path.");
}

void validatePayload(const fs::path &payload, const Manifest &files)
{
    size_t found = 0;
    for (const auto &entry : fs::recursive_directory_iterator(payload)) {
        const auto name = entry.path().lexically_relative(payload).generic_string();
        const auto key = fileKey(name);
        if (pathKind(entry.path()) == PathKind::Directory) continue;
        if (!files.contains(key) || files.at(key).generic_string() != name)
            throw std::runtime_error("The ZIP contains a file outside its manifest.");
        ++found;
    }
    if (found != files.size()) throw std::runtime_error("The ZIP is missing files from its manifest.");
}

struct Change {
    fs::path oldFile;
    fs::path newFile;
    fs::path backup;
    fs::path staged;
    bool backedUp = false;
    bool installed = false;
};

std::vector<Change> updates(const fs::path &root, const fs::path &stage)
{
    const auto payload = stage / L"payload";
    if (pathKind(payload) != PathKind::Directory) throw std::runtime_error("The ZIP has not been extracted.");
    const auto previous = readManifest(root);
    const auto next = readManifest(payload);
    validatePayload(payload, next);

    std::vector<Change> changes;
    for (const auto &[key, relative] : previous) {
        const auto kind = targetKind(root, relative);
        if (kind == PathKind::Directory)
            throw std::runtime_error("A directory would be overwritten by the update.");
        if (kind == PathKind::File)
            changes.push_back({root / relative, {}, stage / L"backup" / relative, {}});
    }
    for (const auto &[key, relative] : next) {
        const auto kind = targetKind(root, relative);
        if (kind == PathKind::Directory || (kind == PathKind::File && !previous.contains(key)))
            throw std::runtime_error("The update would overwrite a file outside the previous package.");
        changes.push_back({{}, root / relative, {}, payload / relative});
    }
    return changes;
}

bool samePath(const fs::path &left, const fs::path &right)
{
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}

void moveFile(const fs::path &from, const fs::path &to)
{
    if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot move an application file. Close all copies of ScreenCapturer and retry.");
}

bool start(const fs::path &executable, std::wstring arguments, const fs::path &directory,
           DWORD flags, DWORD waitMilliseconds, bool expectExit)
{
    std::wstring command = L"\"" + executable.native() + L"\"" + arguments;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, flags,
                        nullptr, directory.c_str(), &startup, &process))
        return false;

    const Handle handle(process.hProcess);
    const Handle thread(process.hThread);
    const auto result = WaitForSingleObject(handle, waitMilliseconds);
    if (!expectExit) return result == WAIT_TIMEOUT;
    DWORD exitCode = 1;
    if (result == WAIT_TIMEOUT) {
        TerminateProcess(handle, 1);
        WaitForSingleObject(handle, 5000);
    }
    return result == WAIT_OBJECT_0 && GetExitCodeProcess(handle, &exitCode) && exitCode == 0;
}

void prepare(const fs::path &root, const fs::path &stage)
{
    wchar_t systemDirectory[MAX_PATH];
    const auto length = GetSystemDirectoryW(systemDirectory, MAX_PATH);
    if (!length || length >= MAX_PATH) throw std::runtime_error("Cannot locate Windows tar.");

    const auto archive = stage / L"update.zip";
    const auto payload = stage / L"payload";
    if (pathKind(archive) != PathKind::File || !fs::create_directory(payload))
        throw std::runtime_error("Cannot prepare the ZIP staging directory.");
    auto tar = fs::path(systemDirectory) / L"tar.exe";
    if (pathKind(tar) == PathKind::Missing) {
        const auto windowsLength = GetWindowsDirectoryW(systemDirectory, MAX_PATH);
        if (!windowsLength || windowsLength >= MAX_PATH) throw std::runtime_error("Cannot locate Windows tar.");
        tar = fs::path(systemDirectory) / L"Sysnative" / L"tar.exe";
    }
    const auto arguments = L" -xf \"" + archive.native() + L"\" -C \"" + payload.native() +
                           L"\" --no-same-owner --no-same-permissions";
    if (!start(tar, arguments, stage, CREATE_NO_WINDOW, 300000, true))
        throw std::runtime_error("Windows tar could not extract the ZIP.");
    updates(root, stage);
    fs::remove(archive);
}

void waitForApp(const fs::path &expected, DWORD processId)
{
    const Handle process(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId));
    if (!process) {
        if (GetLastError() == ERROR_INVALID_PARAMETER) return;
        throw std::runtime_error("Cannot wait for ScreenCapturer to exit.");
    }
    std::wstring image(32768, L'\0');
    DWORD length = static_cast<DWORD>(image.size());
    if (!QueryFullProcessImageNameW(process, 0, image.data(), &length)) {
        if (WaitForSingleObject(process, 0) == WAIT_OBJECT_0) return;
        throw std::runtime_error("Cannot identify the running ScreenCapturer process.");
    }
    image.resize(length);
    if (!samePath(fs::canonical(image), expected) || WaitForSingleObject(process, 60000) != WAIT_OBJECT_0)
        throw std::runtime_error("ScreenCapturer did not exit. Close it and try again.");
}

void cleanStage(const fs::path &stage)
{
    std::error_code error;
    fs::remove_all(stage / L"backup", error);
    fs::remove_all(stage / L"payload", error);
    std::ofstream(stage / L"finished");
}

void apply(const fs::path &root, const fs::path &stage, DWORD processId)
{
    const auto lock = root / L".screencapturer-update.lock";
    const Handle lockHandle(CreateFileW(lock.c_str(), GENERIC_WRITE | DELETE, 0, nullptr,
                                        CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr));
    if (!lockHandle) throw std::runtime_error("Another update is running or the folder cannot be written.");

    const auto executable = root / appName;
    waitForApp(executable, processId);
    std::vector<Change> changes;
    bool backupCreated = false;
    try {
        changes = updates(root, stage);
        backupCreated = fs::create_directory(stage / L"backup");
        if (!backupCreated) throw std::runtime_error("Cannot create the update backup directory.");
        for (auto &change : changes) {
            if (change.oldFile.empty()) continue;
            fs::create_directories(change.backup.parent_path());
            moveFile(change.oldFile, change.backup);
            change.backedUp = true;
        }
        for (auto &change : changes) {
            if (change.newFile.empty()) continue;
            fs::create_directories(change.newFile.parent_path());
            moveFile(change.staged, change.newFile);
            change.installed = true;
        }
        if (!start(executable, {}, root, 0, 2000, false))
            throw std::runtime_error("The new version could not start.");
    } catch (const std::exception &) {
        bool restored = true;
        for (auto change = changes.rbegin(); change != changes.rend(); ++change) {
            std::error_code error;
            if (change->installed) fs::remove(change->newFile, error);
            if (error) restored = false;
            if (change->backedUp &&
                !MoveFileExW(change->backup.c_str(), change->oldFile.c_str(), MOVEFILE_WRITE_THROUGH))
                restored = false;
        }
        if (!restored) throw std::runtime_error("Automatic recovery failed. The backup is in the update folder.");
        if (backupCreated) cleanStage(stage);
        start(executable, {}, root, 0, 0, false);
        throw std::runtime_error("Update failed. The previous version was restored.");
    }
    cleanStage(stage);
}
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    int count = 0;
    auto **args = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!args) return 1;
    const bool applying = count == 5 && std::wstring_view(args[1]) == L"--apply";
    const bool preparing = count == 4 && std::wstring_view(args[1]) == L"--prepare";
    int result = 1;
    try {
        if (!preparing && !applying) throw std::runtime_error("Invalid update command.");
        const auto root = fs::canonical(args[2]);
        const auto stage = fs::path(args[3]).lexically_normal();
        if (!stage.filename().native().starts_with(stagePrefix) || !samePath(stage.parent_path(), root) ||
            pathKind(stage) != PathKind::Directory || !samePath(fs::canonical(args[0]), stage / updaterName))
            throw std::runtime_error("Invalid update staging directory.");
        if (preparing) {
            prepare(root, stage);
        } else {
            wchar_t *end = nullptr;
            const auto processId = std::wcstoul(args[4], &end, 10);
            if (!processId || *end) throw std::runtime_error("Invalid application process ID.");
            apply(root, stage, processId);
        }
        result = 0;
    } catch (const std::exception &error) {
        if (applying) {
            const std::string detail(error.what());
            const auto message = std::wstring(detail.begin(), detail.end()) + L"\n\n" + args[3];
            MessageBoxW(nullptr, message.c_str(), L"ScreenCapturer Update", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
        } else {
            const std::string message(error.what());
            DWORD bytesWritten;
            WriteFile(GetStdHandle(STD_ERROR_HANDLE), message.data(), static_cast<DWORD>(message.size()),
                      &bytesWritten, nullptr);
        }
    }
    LocalFree(args);
    return result;
}
