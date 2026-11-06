#include "memory.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
size_t granularity() {
#ifdef _WIN32
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  return info.dwAllocationGranularity;
#else
  return static_cast<size_t>(sysconf(_SC_PAGESIZE));
#endif
}
}

namespace memory {
mapped_file map_rd(const std::filesystem::path& path, uint64_t offset, size_t size) {
  const size_t delta = offset % granularity();
#ifdef _WIN32
  HANDLE file =
      CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return {};
  }

  LARGE_INTEGER file_size;
  if (!GetFileSizeEx(file, &file_size) || file_size.QuadPart == 0) {
    CloseHandle(file);
    return {};
  }

  HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
  CloseHandle(file);
  if (!mapping) {
    return {};
  }

  if (size == 0) {
    size = static_cast<size_t>(file_size.QuadPart - offset);
  }
  offset -= delta;
  auto* data = static_cast<uint8_t*>(MapViewOfFile(mapping, FILE_MAP_READ, static_cast<DWORD>(offset >> 32),
                                                   static_cast<DWORD>(offset & 0xFFFFFFFF), size + delta));
  CloseHandle(mapping);
  if (!data) {
    return {};
  }

  return {.data = data + delta, .size = size};
#else
  int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    return {};
  }

  struct stat st{};
  if (fstat(fd, &st) != 0 || st.st_size == 0) {
    close(fd);
    return {};
  }

  if (size == 0) {
    size = static_cast<size_t>(st.st_size - offset);
  }
  auto* data = static_cast<uint8_t*>(
      mmap(nullptr, size + delta, PROT_READ, MAP_SHARED, fd, static_cast<off_t>(offset - delta)));
  close(fd);
  if (data == reinterpret_cast<uint8_t*>(MAP_FAILED)) {
    return {};
  }

  return {.data = data + delta, .size = size};
#endif
}

mapped_file map_rw(const std::filesystem::path& path, size_t file_size) {
  if (file_size == 0) {
    return {};
  }

#ifdef _WIN32
  HANDLE file = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return {};
  }

  LARGE_INTEGER size;
  size.QuadPart = static_cast<LONGLONG>(file_size);
  if (!SetFilePointerEx(file, size, nullptr, FILE_BEGIN) || !SetEndOfFile(file)) {
    CloseHandle(file);
    return {};
  }

  HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READWRITE, size.HighPart, size.LowPart, nullptr);
  CloseHandle(file);
  if (!mapping) {
    return {};
  }

  auto* data = static_cast<uint8_t*>(MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0, file_size));
  CloseHandle(mapping);
  if (!data) {
    return {};
  }

  return {.data = data, .size = file_size};
#else
  int fd = open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
  if (fd < 0) {
    return {};
  }

  if (ftruncate(fd, static_cast<off_t>(file_size)) < 0) {
    close(fd);
    return {};
  }

  auto* data = static_cast<uint8_t*>(mmap(nullptr, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
  close(fd);
  if (data == reinterpret_cast<uint8_t*>(MAP_FAILED)) {
    return {};
  }

  return {.data = data, .size = file_size};
#endif
}

void unmap(mapped_file mapping) {
  if (!mapping.data) {
    return;
  }

  const size_t delta = reinterpret_cast<uintptr_t>(mapping.data) % granularity();
#ifdef _WIN32
  FlushViewOfFile(mapping.data - delta, 0);
  UnmapViewOfFile(mapping.data - delta);
#else
  msync(mapping.data - delta, mapping.size + delta, MS_SYNC);
  munmap(mapping.data - delta, mapping.size + delta);
#endif
}

intptr_t open_rd(const std::filesystem::path& path) {
#ifdef _WIN32
  return reinterpret_cast<intptr_t>(
      CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
#else
  return open(path.c_str(), O_RDONLY);
#endif
}

void read_at(intptr_t file, uint64_t offset, void* out, size_t size) {
#ifdef _WIN32
  OVERLAPPED overlapped{};
  overlapped.Offset = static_cast<DWORD>(offset & 0xFFFFFFFF);
  overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32);
  ReadFile(reinterpret_cast<HANDLE>(file), out, static_cast<DWORD>(size), nullptr, &overlapped);
#else
  pread(static_cast<int>(file), out, size, static_cast<off_t>(offset));
#endif
}

void close_rd(intptr_t file) {
#ifdef _WIN32
  CloseHandle(reinterpret_cast<HANDLE>(file));
#else
  close(static_cast<int>(file));
#endif
}
}
