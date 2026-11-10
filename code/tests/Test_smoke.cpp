#include "Check.hpp"
#include <Loom/Loom.hpp>
#include <cstring>
#include <thread>

// Test de humo de la estructura del proyecto: comprueba que un test enlaza con Loom y
// que se pueden crear hilos. Bórralo cuando tengas tests de verdad.

namespace
{
	void Test_version()
	{
		CHECK(Loom::Version() != nullptr);
		CHECK(std::strlen(Loom::Version()) > 0);
	}

	void Test_thread_runs()
	{
		bool ran = false;
		std::thread thread([&ran] { ran = true; });
		thread.join();
		CHECK(ran);
	}
}

int main()
{
	Test_version();
	Test_thread_runs();
	return Loom_test::Result("Test_smoke");
}
