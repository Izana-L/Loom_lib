#pragma once

#include <Loom/Thread_Pool.hpp>

namespace  Loom
{
    class Thread_Registration
    {
    public:
	[[nodiscard]] explicit Thread_Registration(Thread_Pool& pool) noexcept;
	~Thread_Registration();

	Thread_Registration(const Thread_Registration&) = delete;
	Thread_Registration& operator=(const Thread_Registration&) = delete;

    private:
	Thread_Pool& pool;
    };
}
