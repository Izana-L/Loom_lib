#pragma once

#include <deque>
#include <condition_variable>
#include <Task.hpp>
#include <thread>
#include <vector>
#include <mutex>
#include <atomic>
namespace Loom
{
    class Thread_Pool
    {
        private:
        std::size_t worker_count;
        std::size_t max_registered;
        std::size_t running_tasks = 0;

        std::mutex mutex;
        std::condition_variable state_changed;

        bool stopping = false;
        bool stopped = false;

        std::deque<Task> queue;
        std::vector<std::thread> workers;


        void Worker_loop(std::size_t index);

        void Enqueue_or_run(Task&& task);
        void Try_run_one(std::unique_lock<std::mutex>& lock);
        void Execute(Task& task);
        public:

        explicit Thread_Pool(const int& _num_threads,const int& _min_capacity,const int& _max_num_threads)
        {
            if (_num_threads>_max_num_threads)
                throw std::error_code();
            for (int i = 0; i <_num_threads; i++)
            {
                workers.push_back(std::thread());
            }
        }



        Thread_Pool(const Thread_Pool&) = delete;
        Thread_Pool& operator=(const Thread_Pool&) = delete;
        ~Thread_Pool()
        {

        }
       // void Submit(F&& f);
        void Shutdown();
        void Help_until_zero(const std::atomic<int>& pending)
        {
        	std::unique_lock lock(mutex);
        	while (pending.load() != 0)
        	{
        		if (queue.empty())
        		{
        			wake.wait(lock);    // duerme hasta que llegue a cero o entre trabajo
        			continue;
        		}

        		Task task = Pop();
        		lock.unlock();
        		Run(task);              // siempre sin el mutex tomado
        		lock.lock();
        	}
        }

    };
}
