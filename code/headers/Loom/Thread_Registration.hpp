#pragma once

#include <Loom/Thread_Pool.hpp>
#include <cstdio>
#include <exception>
namespace  Loom
{
    class Thread_Registration
    {
    public:
        [[nodiscard]] explicit Thread_Registration(Thread_Pool& _pool) noexcept: pool(_pool),participant_index(pool.Register_current_thread())
	{

		assert(participant_index == detail::no_participant && "No free external slot in the Thread_Pool: increase Pool_Config::max_external_threads");
		if ( participant_index == detail::no_participant ) [[unlikely]]
		{
            std::fputs("Loom: no free external slot in the Thread_Pool\n", stderr);
            std::terminate();
		}

	}
	~Thread_Registration()
	{
	    assert(pool.Participant_index_of_this_thread() == participant_index && "Thread_Registration destroyed on a different thread from the one that created it");
		pool.Unregister_current_thread();
	}

	Thread_Registration(const Thread_Registration&) = delete;
	Thread_Registration& operator=(const Thread_Registration&) = delete;

    private:
	Thread_Pool& pool;
	const int participant_index;
    };
}
