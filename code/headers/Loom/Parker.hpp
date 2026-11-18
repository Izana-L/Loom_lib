#pragma once
#include <atomic>
#include <cstdint>

namespace Loom
{
    class Parker
    {
    private:
        std::atomic<std::uint32_t> wake_notice{0};   // 0 = sin aviso, 1 = aviso
    public:
        Parker() = default;
        Parker(const Parker&) = delete;
        Parker& operator=(const Parker&) = delete;
        void Park() noexcept
        {
            for (;;)
            {
                std::uint32_t expected = 1;
                if (wake_notice.compare_exchange_strong(expected, 0u, std::memory_order_acquire,std::memory_order_relaxed))
                    return;
                wake_notice.wait(0u, std::memory_order_acquire);
            }
        }

        void Unpark() noexcept
        {
            wake_notice.store(1u, std::memory_order_release);
            wake_notice.notify_one();
        }
    };
}
