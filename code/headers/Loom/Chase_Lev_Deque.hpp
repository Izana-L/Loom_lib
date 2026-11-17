#pragma once
#include <atomic>
#include <cassert>
#include <type_traits>
#include <cstddef>
#include <memory>
#include <cstdint>
#include <bit>
#include <limits>
#include <stdexcept>
#include <algorithm>
#include <optional>

namespace Loom
{
    enum class Steal_status : std::uint8_t { Stolen, Empty, Lost_race };

    template <class Type>
    class Chase_Lev_Deque
    {
        static_assert(std::is_trivially_copyable_v<Type>, "T must be trivially copyable");
        static_assert(std::atomic<Type>::is_always_lock_free, "std::atomic<T> must be lock-free");



    private:

        static constexpr std::size_t cache_line_size = 64;


        const std::int64_t capacity;
        const std::int64_t mask;
        const std::unique_ptr<std::atomic<Type>[]> slots;
        alignas(cache_line_size) std::atomic<std::int64_t> top{0};
        alignas(cache_line_size) std::atomic<std::int64_t> bottom{0};
        std::int64_t cached_top{0};

        static std::int64_t Round_capacity(std::int64_t requested)
        {
            constexpr std::int64_t max_capacity = std::int64_t{1} << (std::numeric_limits<std::int64_t>::digits - 1);
            if (requested < 0 || requested > max_capacity)
                throw std::length_error("Chase_Lev_Deque: overflow the capacity");
            return static_cast<std::int64_t>(std::bit_ceil(static_cast<std::uint64_t>(std::max<std::int64_t>(requested, 2))));
        }

    public:

        struct Steal_result
        {
            Steal_status status;
            Type value{};
            Steal_result(Steal_status _status, Type _value = {}) : status(_status), value(_value) {}
        };

        explicit Chase_Lev_Deque(std::int64_t min_capacity): capacity(Round_capacity(min_capacity)),
                                mask(capacity - 1),slots(std::make_unique<std::atomic<Type>[]>(static_cast<std::size_t>(capacity)))
        {}
        Chase_Lev_Deque(const Chase_Lev_Deque&) = delete;
        Chase_Lev_Deque& operator=(const Chase_Lev_Deque&) = delete;

        std::int64_t Capacity() const noexcept { return capacity; }

        [[nodiscard]]  bool Push(const Type& _value) noexcept
        {
            const std::int64_t _temp_bottom = bottom.load(std::memory_order_relaxed);
            if (_temp_bottom - cached_top == capacity)
            {
                cached_top = top.load(std::memory_order::acquire);
                if (_temp_bottom - cached_top == capacity) { return false; }
            }
            slots[static_cast<size_t>(_temp_bottom & mask)].store(_value, std::memory_order_relaxed);
            bottom.store(_temp_bottom + 1, std::memory_order_release);
            return true;
        }
        [[nodiscard]] std::optional<Type> Pop() noexcept
        {
            std::int64_t _temp_bottom = bottom.load(std::memory_order_relaxed) - 1;
            bottom.store(_temp_bottom);
            std::int64_t _temp_top = top.load(std::memory_order::seq_cst);
            if (_temp_top < _temp_bottom)
                return slots[static_cast<size_t>(_temp_bottom & mask)].load(std::memory_order_relaxed);

            const bool won = _temp_top == _temp_bottom && top.compare_exchange_strong(_temp_top, _temp_top + 1);
            bottom.store(_temp_bottom + 1, std::memory_order_relaxed);
            if (!won) return std::nullopt;
            return slots[static_cast<size_t>(_temp_bottom & mask)].load(std::memory_order_relaxed);
        }
        [[nodiscard]] Steal_result Steal() noexcept
        {
            std::int64_t _temp_top = top.load(std::memory_order::seq_cst);
            if (_temp_top >= bottom.load(std::memory_order::seq_cst) )
             return {Steal_status::Empty};
            const Type _value = slots[static_cast<size_t>(_temp_top & mask)].load(std::memory_order_relaxed);
            return top.compare_exchange_strong(_temp_top, _temp_top + 1) ? Steal_result{Steal_status::Stolen, _value} :
                                                                         Steal_result{Steal_status::Lost_race};
        }

    };

}
