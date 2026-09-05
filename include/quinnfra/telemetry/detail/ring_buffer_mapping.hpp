#pragma once
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <type_traits>
#include <unistd.h>
#include <utility>

#include "ring_buffer_layout.hpp"

namespace telemetry::detail {

/// @brief RAII Manager for POSIX shared memory lifetime (shm_open, mmap, munmap, close).
template <typename Layout>
class RingBufferMapping {
public:
    ~RingBufferMapping() {
        reset();
    }

    // Delete copy semantics
    RingBufferMapping(const RingBufferMapping&) = delete;
    RingBufferMapping& operator=(const RingBufferMapping&) = delete;

    // Move constructor
    RingBufferMapping(RingBufferMapping&& other) noexcept
        : layout_(std::exchange(other.layout_, nullptr)),
          shm_name_(std::move(other.shm_name_)),
          is_owner_(std::exchange(other.is_owner_, false)) {}

    // Move assignment overload
    RingBufferMapping& operator=(RingBufferMapping&& other) noexcept {
        if (this != &other) {
            reset();

            layout_ = std::exchange(other.layout_, nullptr);
            shm_name_ = std::move(other.shm_name_);
            is_owner_ = std::exchange(other.is_owner_, false);
        }
        return *this;
    }

    /// @brief Initializes a new shared memory ring buffer mapping for an event producer.
    /// @param shm_name POSIX name starting with '/'.
    /// @return Mapped layout or std::nullopt on OS syscall failure.
    static std::optional<RingBufferMapping<Layout>> create(std::string_view shm_name) {
        std::string shm_name_str(shm_name);

        // Remove stale shared memory
        ::shm_unlink(shm_name_str.c_str());

        // Open file descriptor for memory
        int fd = ::shm_open(shm_name_str.c_str(), O_CREAT | O_EXCL | O_RDWR, 0660);
        if (fd < 0) {
            return std::nullopt;
        }

        // Resize shared memory to fit layout
        const size_t layout_size = sizeof(Layout);
        if (::ftruncate(fd, static_cast<off_t>(layout_size)) < 0) {
            ::close(fd);
            ::shm_unlink(shm_name_str.c_str());
            return std::nullopt;
        }

        // Map to virtual address space
        void* addr = ::mmap(
            nullptr,
            layout_size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            fd,
            0
        );
        ::close(fd);

        if (addr == MAP_FAILED) {
            ::shm_unlink(shm_name_str.c_str());
            return std::nullopt;
        }

        // Initialize layout
        auto* layout = static_cast<Layout*>(addr);
        new (layout) Layout();

        RingBufferMapping mapping;
        mapping.layout_ = layout;
        mapping.shm_name_ = std::move(shm_name_str);
        mapping.is_owner_ = true;
        return mapping;
    }

    /// @brief Attach an event consumer to an existing shared memory ring buffer mapping.
    /// @param shm_name POSIX name starting with '/'.
    /// @return Mapped layout or std::nullopt if the segment does not exist or fails to map.
    static std::optional<RingBufferMapping<Layout>> attach(std::string_view shm_name) {
        std::string shm_name_str(shm_name);

        // Open file descriptor for memory
        int fd = ::shm_open(shm_name_str.c_str(), O_RDWR, 0);
        if (fd < 0) {
            return std::nullopt;
        }

        // Check if buffer was created prior
        const size_t layout_size = sizeof(Layout);
        struct stat sb{};
        if (::fstat(fd, &sb) < 0 || sb.st_size < static_cast<off_t>(layout_size)) {
            ::close(fd);
            return std::nullopt;
        }

        // Map to virtual address space
        void* addr = ::mmap(
            nullptr,
            layout_size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            fd,
            0
        );
        ::close(fd);

        if (addr == MAP_FAILED) {
            return std::nullopt;
        }

        RingBufferMapping mapping;
        mapping.layout_ = static_cast<Layout*>(addr);
        mapping.shm_name_ = std::move(shm_name_str);
        mapping.is_owner_ = false;
        return mapping;
    }

    /// @brief Removes the named shared memory file entry from the OS.
    /// @param shm_name POSIX name starting with '/'.
    /// @return true if the file was successfully removed, false otherwise.
    static bool unlink(std::string_view shm_name) noexcept {
        std::string shm_name_str(shm_name);
        return ::shm_unlink(shm_name_str.c_str()) == 0;
    }

    [[nodiscard]] Layout& layout() const noexcept { return *layout_; }
    [[nodiscard]] bool is_owner() const noexcept { return is_owner_; }

private:
    RingBufferMapping() noexcept = default;

    void reset() noexcept {
        if (layout_ != nullptr && layout_ != MAP_FAILED) {
            // Only the owner/producer unlinks
            if (is_owner_ && !shm_name_.empty()) {
                ::shm_unlink(shm_name_.c_str());
            }

            // Unmap memory from virtual address space
            ::munmap(static_cast<void*>(layout_), sizeof(Layout));
            layout_ = nullptr;
        }
        shm_name_.clear();
        is_owner_ = false;
    }

    Layout* layout_{nullptr};
    std::string shm_name_{};
    bool is_owner_{false};
};

} // namespace telemetry::detail
