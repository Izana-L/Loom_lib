#include "Check.hpp"
#include <Loom/Chase_Lev_Deque.hpp>

#include <algorithm>
#include <atomic>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <random>
#include <thread>
#include <vector>

// Pruebas de estrés de Loom::Chase_Lev_Deque: un hilo dueño (Push y Pop) y varios ladrones
// (Steal) sobre la misma cola. Todos los escenarios comprueban lo mismo: cada valor metido lo
// obtiene exactamente un hilo. Ninguno se pierde y ninguno sale dos veces.
//
// Cada valor es su propio índice en una tabla de contadores atómicos. Quien obtiene un valor
// suma uno en su casilla, y al final todas tienen que valer 1.
//
// La semilla es fija: el dueño hace la misma secuencia de operaciones en cada ejecución. Lo que
// cambia de una a otra es cómo se entrelazan los hilos.

namespace
{
	using Value = std::uint32_t;
	using Deque = Loom::Chase_Lev_Deque<Value>;
	using Loom::Steal_status;

	constexpr unsigned seed = 20261117;

	// Al menos dos ladrones, para que también compitan entre ellos, y como mucho cuatro.
	unsigned Thief_count() noexcept
	{
		const unsigned hardware = std::thread::hardware_concurrency(); // 0 si no se sabe
		return std::clamp(hardware, 3u, 5u) - 1;
	}

	// Una cola, sus ladrones y la tabla de contadores. El hilo que crea el escenario es el
	// dueño: mete con Push, saca con Pop y termina con Finish.
	class Scenario
	{
	public:
		// Al volver el constructor, los ladrones ya están robando.
		Scenario(const std::int64_t min_capacity, const std::size_t values)
			: deque(min_capacity)
			, seen(std::make_unique<std::atomic<std::uint8_t>[]>(values))
			, value_count(values)
		{
			const unsigned thief_count = Thief_count();
			for (unsigned thief = 0; thief < thief_count; ++thief)
			{
				thieves.emplace_back([this] { Steal_until_done(); });
			}
			while (ready.load() < thief_count)
			{
				std::this_thread::yield();
			}
		}

		[[nodiscard]] bool Push(const Value value) noexcept
		{
			return deque.Push(value);
		}

		// Saca un elemento y lo anota. Devuelve false si no había ninguno.
		bool Pop() noexcept
		{
			const std::optional<Value> value = deque.Pop();
			if (!value)
			{
				return false;
			}
			Record(*value);
			++popped;
			return true;
		}

		// Vacía la cola, para a los ladrones y comprueba que cada valor salió una sola vez.
		void Finish(const char* name)
		{
			// El dueño ya no mete nada más: cuando Pop no encuentra nada, la cola está vacía
			// para siempre y los ladrones pueden irse.
			while (Pop())
			{
			}
			done.store(true);
			for (std::thread& thief : thieves)
			{
				thief.join();
			}

			std::size_t lost = 0;
			std::size_t duplicated = 0;
			for (std::size_t value = 0; value < value_count; ++value)
			{
				const unsigned times = seen[value].load();
				if (times == 0)
				{
					++lost;
				}
				if (times > 1)
				{
					++duplicated;
				}
			}

			std::printf("  %-32s %zu valores: %" PRIu64 " con Pop, %" PRIu64 " robados, %" PRIu64
			            " robos fallidos por carrera\n",
			            name, value_count, popped, stolen.load(), lost_races.load());
			if (lost != 0 || duplicated != 0)
			{
				std::printf("  %-32s %zu perdidos, %zu duplicados\n", name, lost, duplicated);
			}

			CHECK(lost == 0);
			CHECK(duplicated == 0);
			CHECK(out_of_range.load() == 0);
			CHECK(popped + stolen.load() == value_count);
		}

	private:
		void Record(const Value value) noexcept
		{
			if (value < value_count)
			{
				seen[value].fetch_add(1, std::memory_order_relaxed);
			}
			else
			{
				out_of_range.fetch_add(1, std::memory_order_relaxed); // un valor que nadie metió
			}
		}

		void Steal_until_done() noexcept
		{
			std::uint64_t my_stolen = 0;
			std::uint64_t my_lost_races = 0;
			ready.fetch_add(1);
			for (;;)
			{
				const Deque::Steal_result result = deque.Steal();
				if (result.status == Steal_status::Stolen)
				{
					Record(result.value);
					++my_stolen;
					continue;
				}
				if (result.status == Steal_status::Lost_race)
				{
					++my_lost_races;
				}
				if (done.load())
				{
					break;
				}
			}
			stolen.fetch_add(my_stolen);
			lost_races.fetch_add(my_lost_races);
		}

		Deque deque;
		const std::unique_ptr<std::atomic<std::uint8_t>[]> seen;
		const std::size_t value_count;
		std::uint64_t popped = 0; // solo lo toca el dueño
		std::atomic<std::uint64_t> stolen{0};
		std::atomic<std::uint64_t> lost_races{0};
		std::atomic<std::uint64_t> out_of_range{0};
		std::atomic<unsigned> ready{0};
		std::atomic<bool> done{false};
		std::vector<std::thread> thieves;
	};

	// El dueño mete todos los valores y, de vez en cuando, saca unos cuantos. Si la cola está
	// llena, hace sitio sacando uno. Con capacidad grande domina el robo; con capacidad mínima
	// la cola se llena y da la vuelta al búfer sin parar.
	void Stress_load(const char* name, const std::int64_t min_capacity, const std::size_t value_count)
	{
		Scenario scenario(min_capacity, value_count);
		std::minstd_rand random(seed);
		for (std::size_t next = 0; next < value_count; ++next)
		{
			const Value value = static_cast<Value>(next);
			while (!scenario.Push(value))
			{
				scenario.Pop();
			}
			if (random() % 8 == 0)
			{
				for (auto pops = 1 + random() % 4; pops > 0 && scenario.Pop(); --pops)
				{
				}
			}
		}
		scenario.Finish(name);
	}

	// El dueño mete entre uno y tres valores y vacía la cola enseguida, una y otra vez. La cola
	// pasa casi todo el tiempo con un elemento o con ninguno: es el caso en que Pop y Steal
	// compiten por el mismo elemento y lo decide el CAS sobre top.
	void Stress_last_element(const char* name, const std::size_t value_count)
	{
		Scenario scenario(4, value_count);
		std::minstd_rand random(seed);
		std::size_t rejected = 0;
		for (std::size_t next = 0; next < value_count;)
		{
			const std::size_t burst = std::min<std::size_t>(1 + random() % 3, value_count - next);
			for (std::size_t pushed = 0; pushed < burst; ++pushed, ++next)
			{
				// La cola estaba vacía y caben al menos cuatro: Push no puede fallar.
				if (!scenario.Push(static_cast<Value>(next)))
				{
					++rejected;
				}
			}
			while (scenario.Pop())
			{
			}
		}
		CHECK(rejected == 0);
		scenario.Finish(name);
	}
}

int main()
{
	std::printf("Stress_chase_lev_deque: 1 dueno y %u ladrones\n", Thief_count());
	Stress_load("carga general (capacidad 1024)", 1024, 4'000'000);
	Stress_load("capacidad minima", 1, 2'000'000);
	Stress_last_element("ultimo elemento", 4'000'000);
	return Loom_test::Result("Stress_chase_lev_deque");
}
