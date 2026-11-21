#include <Loom/Thread_Pool.hpp>
#include <cassert>

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
    int Thread_Pool::Participant_index_of_this_thread() const noexcept
    {
        return this_thread_identity.joined_pool == this ? this_thread_identity.participant_index : detail::no_participant;
    }
}
