#include "Loom/Chase_Lev_Deque.hpp"
#include "Loom/Task.hpp"
#include <Loom/Thread_Pool.hpp>
#include <atomic>
#include <cassert>
#include <cstdint>

#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
	#define LOOM_CPU_X86 1
	#include <immintrin.h>       // _mm_pause
#elif defined(_M_ARM64)
	#define LOOM_CPU_MSVC_ARM64 1
	#include <intrin.h>          // __isb
#elif defined(__aarch64__)
	#define LOOM_CPU_ARM64 1
#endif

namespace
{

	void Cpu_pause() noexcept
	{
#if defined(LOOM_CPU_X86)
		_mm_pause();
#elif defined(LOOM_CPU_MSVC_ARM64)
		__isb(_ARM64_BARRIER_SY);
#elif defined(LOOM_CPU_ARM64)
		__asm__ __volatile__("isb" ::: "memory");
#endif

	}


}
namespace Loom
{
    Thread_Pool::Thread_Pool(const Pool_Config& config):worker_count(Resolve_worker_count(config.requested_worker_count))
                                                                     ,participant_count(worker_count + config.max_external_threads)
    {
        participants.reserve(participant_count);
        for (int index = 0; index < participant_count; ++index)
            participants.push_back(std::make_unique<Participant>(config.min_deque_capacity, index));
        worker_threads.reserve(worker_count);
        try
        {
            for (int index = 0; index < worker_count; ++index)
                worker_threads.emplace_back([this, index] { Worker_loop(index); });
        }
        catch (...)
        {
            Stop_and_join_workers();
            throw;
        }
    }

    Thread_Pool::~Thread_Pool()
    {
        assert(this_thread_identity.joined_pool != this
               && "Thread_Pool destroyed from one of its own threads (a worker or a thread still registered in it)");

        assert(No_external_thread_registered()
               && "Thread_Pool destroyed while an external thread is still registered: destroy its Thread_Registration first");

        assert(All_deques_empty()
               && "Thread_Pool destroyed with pending tasks: every Group must finish waiting before the pool is destroyed");
        Stop_and_join_workers();

    }
    void Thread_Pool::Worker_loop(unsigned worker_index) noexcept
    {

    }
    void Thread_Pool::Stop_and_join_workers() noexcept
    {
        shutdown_requested.store(true);
        for (unsigned index = 0; index < worker_count; ++index)
            participants[index]->parker.Unpark();
        for (std::thread& thread : worker_threads)
            thread.join();
    }
    int Thread_Pool::Register_current_thread() noexcept
    {
        assert(this_thread_identity.joined_pool == nullptr
               && "Thread already belongs to a Thread_Pool: it is a worker, or it is already registered in this or another pool");
        bool expected = false;
        for (unsigned index = worker_count; index < participant_count; ++index)
        {

            if(participants[index]->is_registered.compare_exchange_strong(expected, true,
                                                                           std::memory_order_acquire,
                                                                           std::memory_order_relaxed))
            {
                assert(participants[index]->work_deque.Empty()
                       && "Acquired an external slot whose deque is not empty: its previous owner released it with pending tasks");
                this_thread_identity.joined_pool = this;
                this_thread_identity.participant_index = static_cast<int>(index);
                return static_cast<int>(index);
            }
        }
        return detail::no_participant;
    }
    void Thread_Pool::Unregister_current_thread() noexcept
    {
        assert(Participant_index_of_this_thread() >= static_cast<int>(worker_count)
               && "Thread is not registered as an external thread of this pool: it was never registered,it belongs to another pool, or it is a worker");
        unsigned released_index = static_cast<unsigned>(this_thread_identity.participant_index);
        assert(participants[released_index]->work_deque.Empty()
               && "Releasing an external slot with pending tasks: wait for every Group that submitted from this thread before destroying its Thread_Registration");
        this_thread_identity = detail::Thread_identity{};
        participants[released_index]->is_registered.store(false, std::memory_order_release);

    }
    int Thread_Pool::Participant_index_of_this_thread() const noexcept
    {
        return this_thread_identity.joined_pool == this ? this_thread_identity.participant_index : detail::no_participant;
    }
    void Thread_Pool::Run_and_complete(Task* task) noexcept
    {
            assert(task != nullptr && "Run_and_complete called with a null Task");
            task->Run();
            task->pending->fetch_sub(1,std::memory_order_release);

    }
    Task* Thread_Pool::Steal_from(unsigned victim_index) noexcept
	{
	    Task_deque::Steal_result result{Steal_status::Empty};
		do
		{   result = participants[victim_index]->work_deque.Steal();
            if (result.status == Steal_status::Lost_race) Cpu_pause();
		}
		while (result.status == Steal_status::Lost_race);
		if (result.status == Steal_status::Empty) return nullptr;
		return result.value;
	}
    Task* Thread_Pool::Find_task_by_stealing(unsigned thief_index) noexcept
    {
        std::uint32_t _ramdom_state = participants[thief_index]->random_state;
        _ramdom_state ^= _ramdom_state << 13;
        _ramdom_state ^= _ramdom_state >> 17;
        _ramdom_state ^= _ramdom_state << 5;
        participants[thief_index]->random_state = _ramdom_state ;
        unsigned _slot =  static_cast<uint32_t>((std::uint64_t{_ramdom_state}* participant_count) >> 32);
        for (unsigned index = 0; index < participant_count; ++index)
        {
            if (++_slot == participant_count) _slot = 0;
            if (thief_index == _slot) continue;
            Task* _stolen_task = Steal_from(_slot);
            if (_stolen_task) return _stolen_task;
        }
        return nullptr;
    }

}
