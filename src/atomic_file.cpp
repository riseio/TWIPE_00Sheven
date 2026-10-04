#include "atomic_file.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <sys/stat.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace twine::files {
WriteResult write_atomic_stream(const std::filesystem::path& path, uint64_t maximum_size,
    const std::function<void(FILE*)>& produce) {
    if(path.filename().empty() || !maximum_size || maximum_size>8ULL*1024*1024*1024)
        throw std::runtime_error("Invalid atomic stream destination or bound");
    const auto directory=path.has_parent_path()?path.parent_path():std::filesystem::path(".");
    std::filesystem::create_directories(directory);
    static std::atomic_uint64_t sequence{0};
#ifdef _WIN32
    const auto pid=GetCurrentProcessId();
#else
    const auto pid=getpid();
#endif
    auto temporary=path;
    temporary+=".stream-"+std::to_string(pid)+"-"+std::to_string(sequence.fetch_add(1));
#ifdef _WIN32
    const int fd=_wopen(temporary.c_str(),_O_RDWR|_O_CREAT|_O_EXCL|_O_BINARY,_S_IREAD|_S_IWRITE);
    FILE* file=fd<0?nullptr:_fdopen(fd,"w+b");
#else
    const int fd=open(temporary.c_str(),O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC,0600);
    FILE* file=fd<0?nullptr:fdopen(fd,"w+b");
#endif
    if(fd<0) throw std::system_error(errno,std::generic_category(),"Creating atomic stream");
    bool owned=true;
    try {
        if(!file) {
#ifdef _WIN32
            _close(fd);
#else
            close(fd);
#endif
            throw std::runtime_error("Opening atomic output stream failed");
        }
        produce(file);
        if(std::ferror(file) || std::fflush(file)!=0 || std::filesystem::file_size(temporary)>maximum_size)
            throw std::runtime_error("Atomic stream write failed or exceeded its bound");
#ifdef _WIN32
        const bool flushed=_commit(fd)==0;
#else
        const bool flushed=fsync(fd)==0;
#endif
        const bool closed=std::fclose(file)==0;file=nullptr;
        if(!flushed || !closed) throw std::runtime_error("Atomic stream flush failed");
#ifdef _WIN32
        if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::system_error(GetLastError(),std::system_category(),"Publishing atomic stream");
#else
        std::filesystem::rename(temporary,path);
#endif
        owned=false;
#ifndef _WIN32
        const int parent=open(directory.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC);
        if(parent<0) return {false,"Cache published but directory durability sync unavailable"};
        const bool synced=fsync(parent)==0;
        const bool closed_parent=close(parent)==0;
        if(!synced || !closed_parent) return {false,"Cache published but directory durability sync failed"};
#endif
        return {};
    } catch(...) {
        if(file) std::fclose(file);
        if(owned) {std::error_code error;std::filesystem::remove(temporary,error);}
        throw;
    }
}

WriteResult write_atomic(const std::filesystem::path& path, std::span<const uint8_t> bytes) {
    if (bytes.size() > 128ULL * 1024 * 1024) throw std::runtime_error("Atomic file exceeds bounded size");
    if (path.filename().empty()) throw std::runtime_error("Invalid atomic file destination");
    const auto directory = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
    std::filesystem::create_directories(directory);
    static std::atomic_uint64_t sequence{0};
#ifdef _WIN32
    const auto pid = GetCurrentProcessId();
#else
    const auto pid = getpid();
#endif
    auto temporary = path;
    temporary += ".tmp-" + std::to_string(pid) + "-" + std::to_string(sequence.fetch_add(1));

    bool owned = false;
    try {
#ifdef _WIN32
        HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw std::system_error(GetLastError(), std::system_category(), "Creating atomic file temporary file");
        owned = true;
        DWORD written = 0;
        const bool wrote = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) != 0 && written == bytes.size();
        const bool flushed = wrote && FlushFileBuffers(file) != 0;
        const bool closed = CloseHandle(file) != 0;
        if (!flushed || !closed) throw std::runtime_error("Writing/flushing file failed; previous file preserved");
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            throw std::system_error(GetLastError(), std::system_category(), "Replacing file");
        }
        owned = false;
        return {};
#else
        const int file = open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
        if (file < 0) throw std::system_error(errno, std::generic_category(), "Creating atomic file temporary file");
        owned = true;
        size_t offset = 0;
        while (offset < bytes.size()) {
            const auto count = write(file, bytes.data() + offset, bytes.size() - offset);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) break;
            offset += static_cast<size_t>(count);
        }
        const bool flushed = offset == bytes.size() && fsync(file) == 0;
        const bool closed = close(file) == 0;
        if (!flushed || !closed) throw std::runtime_error("Writing/flushing file failed; previous file preserved");
        std::filesystem::rename(temporary, path);
        owned = false;
        const int parent = open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (parent < 0) return {false, "File replaced, but its directory could not be opened for durability sync"};
        const bool synced = fsync(parent) == 0;
        const bool parent_closed = close(parent) == 0;
        if (!synced || !parent_closed) return {false, "File replaced, but directory durability sync failed"};
        return {};
#endif
    } catch (...) {
        if (owned) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
        }
        throw;
    }
}
}
