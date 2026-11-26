#pragma once

#include <cassert>
#include <cstdint>
#include <Loom/Task.hpp>
#include <Loom/Chase_Lev_Deque.hpp>
#include <Loom/Parker.hpp>
#include <sys/types.h>
#include <thread>
#include <vector>
#include <memory>
#include <atomic>

namespace Loom
{
    using Task_deque = Chase_Lev_Deque<Task*>;
    class Thread_Pool;
    struct Pool_Config
    {
        unsigned requested_worker_count = 0;
        unsigned max_external_threads = 4;
        std::int64_t min_deque_capacity = 1024;
    };
    namespace detail
    {
        inline constexpr int no_participant = -1;

        struct Thread_identity
        {
            Thread_Pool* joined_pool = nullptr;
            int32_t participant_index = no_participant;
        };
    }

    class Thread_Pool
    {
         friend class Thread_Registration;
         static constexpr unsigned max_workers = 64;
    public:

        explicit Thread_Pool(const Pool_Config& config = {});
        ~Thread_Pool();
        Thread_Pool(const Thread_Pool&) = delete;
        Thread_Pool& operator=(const Thread_Pool&) = delete;

        void Schedule_task(Task* task) noexcept;
        bool Try_run_one_task() noexcept;



        static Thread_Pool* Pool_of_this_thread() noexcept;
        unsigned Worker_count() const noexcept{return worker_count;}

    private:


        struct Participant
        {
            Task_deque work_deque;
            std::atomic<bool> is_registered{false};
            Parker parker;
            std::uint32_t random_state;
            unsigned last_successful_victim;
            explicit Participant(std::int64_t capacity, unsigned index ): work_deque(capacity)
                                                                        , random_state((index + 1u) * 0x9E3779B9u),last_successful_victim{index}
            {

            }
        };


        int Participant_index_of_this_thread() const noexcept;
        inline static constinit thread_local detail::Thread_identity this_thread_identity{};

        const unsigned worker_count;
        const unsigned participant_count;
        std::vector<std::unique_ptr<Participant>> participants;
        alignas(64) std::atomic<std::uint64_t> idle_worker_mask{0};
        alignas(64) std::atomic<int> searching_worker_count{0};
        std::atomic<bool> shutdown_requested{false};
        std::vector<std::thread> worker_threads;

        void Worker_loop(unsigned worker_index) noexcept;
        static void Run_and_complete(Task* task) noexcept;
        Task* Find_task_by_stealing(unsigned thief_index) noexcept;
        void Wake_one_idle_worker() noexcept;
        void Announce_idle_and_park(unsigned worker_index) noexcept;
        void Stop_and_join_workers() noexcept;
        Task* Steal_from(unsigned victim_index) noexcept;

        [[nodiscard]] int Register_current_thread() noexcept;
        void Unregister_current_thread() noexcept;

        static unsigned int Resolve_worker_count(unsigned requested_worker_count)noexcept
        {
                const unsigned hardware = std::thread::hardware_concurrency();
                const unsigned by_default = hardware > 1 ? hardware - 1 : 1; // -3
                if (requested_worker_count == 0)
                    return std::min(max_workers, by_default);
                assert(requested_worker_count <= max_workers);
                return requested_worker_count;
        }
        bool No_external_thread_registered() const noexcept
        {
            for (unsigned index = worker_count; index < participant_count; ++index)
            {
                if(participants[index]->is_registered.load(std::memory_order_acquire))return false;
            }
            return true;
        }
        bool All_deques_empty() const noexcept
        {
            for (unsigned index = 0; index < participant_count; ++index)
            {
                if(!participants[index]->work_deque.Empty())return false;
            }
            return true;
        }

    };
}
