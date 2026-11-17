#include <Loom/Thread_Pool.hpp>
class Group
{
public:

	explicit Group(Loom::Thread_Pool& owner) : pool(owner)
	{
	}

	// G6: un grupo no desaparece con tareas en vuelo.
	~Group()
	{
		Wait();
	}

	// G1: las tareas en vuelo guardan la dirección del contador.
	Group(const Group&) = delete;
	Group& operator=(const Group&) = delete;

	/*void Submit(std::function<void()> function)
	{
		pending.fetch_add(1);                              // primero sube el contador
		pool.Push(Task{std::move(function), &pending});    // después la tarea se hace visible
		}*/

	void Wait()
	{
		pool.Help_until_zero(pending);
	}

private:

	Loom::Thread_Pool& pool;
	std::atomic<int> pending{0};
};
