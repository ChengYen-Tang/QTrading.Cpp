#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <type_traits>
#include <utility>

namespace QTrading::Log {

    struct LogPayload {
        std::atomic<uint32_t> ref_count{ 1 };
        void* data{ nullptr };
        void (*destroy)(LogPayload*){ nullptr };
        std::pmr::memory_resource* resource{ nullptr };

        void AddRef() noexcept {
            ref_count.fetch_add(1, std::memory_order_relaxed);
        }

        void Release() noexcept {
            if (ref_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                destroy(this);
            }
        }
    };

    class PayloadPtr {
    public:
        PayloadPtr() noexcept = default;
        explicit PayloadPtr(LogPayload* payload) noexcept : payload_(payload) {}

        PayloadPtr(const PayloadPtr& other) noexcept : payload_(other.payload_) {
            if (payload_) {
                payload_->AddRef();
            }
        }

        PayloadPtr(PayloadPtr&& other) noexcept : payload_(other.payload_) {
            other.payload_ = nullptr;
        }

        PayloadPtr& operator=(const PayloadPtr& other) noexcept {
            if (this == &other) {
                return *this;
            }
            Reset();
            payload_ = other.payload_;
            if (payload_) {
                payload_->AddRef();
            }
            return *this;
        }

        PayloadPtr& operator=(PayloadPtr&& other) noexcept {
            if (this == &other) {
                return *this;
            }
            Reset();
            payload_ = other.payload_;
            other.payload_ = nullptr;
            return *this;
        }

        ~PayloadPtr() {
            Reset();
        }

        void* get() const noexcept {
            return payload_ ? payload_->data : nullptr;
        }

        explicit operator bool() const noexcept {
            return payload_ != nullptr;
        }

        LogPayload* raw() const noexcept {
            return payload_;
        }

    private:
        void Reset() noexcept {
            if (!payload_) {
                return;
            }
            payload_->Release();
            payload_ = nullptr;
        }

        LogPayload* payload_{ nullptr };
    };

    template <typename T>
    struct is_shared_ptr : std::false_type {};

    template <typename T>
    struct is_shared_ptr<std::shared_ptr<T>> : std::true_type {};

    template <typename T>
    struct PayloadBox final : LogPayload {
        template <typename... Args>
        explicit PayloadBox(std::pmr::memory_resource* res, Args&&... args)
            : value(std::forward<Args>(args)...)
        {
            data = &value;
            resource = res ? res : std::pmr::new_delete_resource();
            destroy = [](LogPayload* base) {
                auto* box = static_cast<PayloadBox*>(base);
                std::pmr::polymorphic_allocator<PayloadBox> alloc(box->resource);
                box->~PayloadBox();
                alloc.deallocate(box, 1);
            };
        }

        T value;
    };

    template <typename T, typename... Args>
    inline PayloadPtr MakePayload(std::pmr::memory_resource* res, Args&&... args) {
        using Box = PayloadBox<T>;
        std::pmr::polymorphic_allocator<Box> alloc(res ? res : std::pmr::get_default_resource());
        Box* box = alloc.allocate(1);
        ::new (box) Box(res, std::forward<Args>(args)...);
        return PayloadPtr(box);
    }

    template <typename T>
    inline PayloadPtr MakePayload() {
        return MakePayload<T>(std::pmr::get_default_resource());
    }

    template <typename T, typename First, typename... Rest,
        typename std::enable_if_t<!std::is_convertible_v<std::decay_t<First>, std::pmr::memory_resource*>, int> = 0>
    inline PayloadPtr MakePayload(First&& first, Rest&&... rest) {
        return MakePayload<T>(std::pmr::get_default_resource(), std::forward<First>(first), std::forward<Rest>(rest)...);
    }

} // namespace QTrading::Log
