#pragma once

#include <deque>
#include <condition_variable>
#include <Loom/Task.hpp>
#include <Loom/Chase_Lev_Deque.hpp>
#include <Loom/Parker.hpp>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>

namespace Loom
{
    class Thread_Pool
    {
    public:

        struct Pool_Config
        {
            unsigned requested_worker_count = 0;            // 0 = núcleos - 1, mínimo 1; máximo 64 (4)
            unsigned max_external_threads = 4;          // K (2)
            std::int64_t min_deque_capacity = 1024;   // (3)
        };

        explicit Thread_Pool(const Pool_Config& config);
        ~Thread_Pool();                                        // (7)
        Thread_Pool(const Thread_Pool&) = delete;
        Thread_Pool& operator=(const Thread_Pool&) = delete;

        void Schedule_task(Task* task) noexcept;                      // (3) inline si el deque está lleno
        bool Try_run_one_task() noexcept;                           // (6) para Group::Wait

        [[nodiscard]] bool Register_current_thread() noexcept;            // (2) para Thread_Registration
        void Unregister_current_thread() noexcept;

        static Thread_Pool* Pool_of_this_thread() noexcept;           // (8)
        unsigned Worker_count() const noexcept;

    private:



        struct Participant
        {
            Chase_Lev_Deque<Task*> work_deque;
            std::atomic<bool> is_registered{false};                    // (2) solo ranuras externas
            Parker parker;                                     // (4) solo workers
            std::uint32_t random_state;                        // (5) solo lo toca el dueño
            unsigned last_successful_victim;                              // (5) solo lo toca el dueño
            explicit Participant(std::int64_t capacity, std::uint32_t seed);
        };

        struct Thread_identity                                 // (8)
        {
            Thread_Pool* joined_pool = nullptr;
            int participant_index = -1;
        };
        inline static constinit thread_local Thread_identity this_thread_identity{};

        const unsigned worker_count;
        const unsigned participant_count;                             // workers + externas
        std::vector<std::unique_ptr<Participant>> participants;              // [0, worker_count) son workers
        alignas(64) std::atomic<std::uint64_t> idle_worker_mask{0};   // (4)
        alignas(64) std::atomic<int> searching_worker_count{0};             // (4, ajuste 2)
        std::atomic<bool> shutdown_requested{false};                     // (7)
        std::vector<std::thread> worker_threads;                      // el último miembro

        void Worker_loop(unsigned worker_index) noexcept;
        static void Run_and_complete(Task* task) noexcept;              // (6)
        Task* Find_task_by_stealing(unsigned thief_index) noexcept;     // (5) nullptr = todo Empty
        void Wake_one_idle_worker() noexcept;                              // (4) reclamar bit y Unpark
        void Announce_idle_and_park(unsigned worker_index) noexcept;             // (4) secuencia de aparcado completa
        static void Cpu_pause() noexcept;                      // (4, ajuste 1)
    };
}
