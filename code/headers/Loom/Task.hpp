#pragma once
#include <atomic>
#include <cassert>
#include <type_traits>
#include <cstddef>
#include <new>
#include <utility>
namespace Loom
{
    class Task
    {
    private:
        static constexpr std::size_t storage_size = 48;
        static constexpr std::size_t storage_alignment = 16;
        alignas(storage_alignment) std::byte storage[storage_size];

        void (*invoker)(void*) noexcept = nullptr;

        template <typename Callable>
        static void Invoke_and_destroy(void* memory) noexcept
        {
            Callable* callable = std::launder(static_cast<Callable*>(memory));
            (*callable)();
            callable->~Callable();
        }

    public:

        std::atomic<int>* pending = nullptr;

        template <typename F>
        Task(F&& function, std::atomic<int>* group_pending) noexcept: pending(group_pending)
        {

            using Callable = std::decay_t<F>;
            static_assert(sizeof(Callable) <= storage_size, "Callable too big for Task: capture by reference or pointer instead of by value");
            static_assert(alignof(Callable) <= storage_alignment, "Callable alignment is greater than the Task storage alignment");
            static_assert(std::is_nothrow_constructible_v<Callable, F>, "Storing the callable must not throw: pass it with std::move");
            static_assert(std::is_invocable_v<Callable&>, "Callable must be invocable with no arguments");
            new (static_cast<void*>(storage)) Callable(std::forward<F>(function));
            invoker = &Invoke_and_destroy<Callable>;
        }
        Task(const Task&) = delete;
        Task& operator=(const Task&) = delete;
        ~Task()
        {
            assert(invoker == nullptr && "Task destroyed without being run");
        }
        void Run() noexcept
        {
            assert(invoker != nullptr && "Run() called on an empty Task");
            invoker(storage);
            invoker = nullptr;
        }

    };
    static_assert(sizeof(Task) == 64, "Task must be exactly 64 bytes");
}
