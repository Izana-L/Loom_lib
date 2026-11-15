#pragma once

#include "Loom/Thread_Pool.hpp"

namespace  Loom
{
    class Thread_registration
    {
    public:
	[[nodiscard]] explicit Thread_registration(Thread_pool& pool) noexcept;
	~Thread_registration();

	Thread_registration(const Thread_registration&) = delete;
	Thread_registration& operator=(const Thread_registration&) = delete;

    private:
	Thread_pool& pool;
    };
}
