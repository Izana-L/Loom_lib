#include "Check.hpp"
#include <Loom/Chase_lev_Deque.hpp>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

// Pruebas funcionales de Loom::Chase_Lev_Deque con un solo hilo, que hace de dueño (Push y Pop)
// y de ladrón (Steal). Sin concurrencia el resultado es determinista: aquí se comprueba la lógica
// de los índices. Las carreras entre hilos están en Stress_chase_lev_deque.cpp.

namespace
{
	using Deque = Loom::Chase_Lev_Deque<int>;
	using Loom::Steal_status;

	// La cola no se copia ni se mueve: otros hilos guardan su dirección.
	static_assert(!std::is_copy_constructible_v<Deque>);
	static_assert(!std::is_move_constructible_v<Deque>);
	static_assert(!std::is_copy_assignable_v<Deque>);
	static_assert(!std::is_move_assignable_v<Deque>);

	// Las tres operaciones no lanzan excepciones.
	static_assert(noexcept(std::declval<Deque&>().Push(0)));
	static_assert(noexcept(std::declval<Deque&>().Pop()));
	static_assert(noexcept(std::declval<Deque&>().Steal()));

	// Vacía para el dueño y para un ladrón. Úsala solo donde se espera que lo esté: si no lo
	// está, se lleva un elemento.
	bool Is_empty(Deque& deque)
	{
		return !deque.Pop() && deque.Steal().status == Steal_status::Empty;
	}

	void Test_new_deque_is_empty()
	{
		Deque deque(8);
		CHECK(!deque.Pop());
		CHECK(deque.Steal().status == Steal_status::Empty);
	}

	// Sacar de una cola vacía no puede dejar los índices movidos: lo que se meta después tiene
	// que salir, por cualquiera de los dos extremos.
	void Test_pop_on_empty_keeps_state()
	{
		Deque deque(8);

		for (int attempt = 0; attempt < 5; ++attempt)
		{
			CHECK(!deque.Pop());
		}
		CHECK(deque.Push(7));
		CHECK(deque.Pop() == 7);
		CHECK(Is_empty(deque));

		for (int attempt = 0; attempt < 5; ++attempt)
		{
			CHECK(!deque.Pop());
		}
		CHECK(deque.Push(8));
		const auto stolen = deque.Steal();
		CHECK(stolen.status == Steal_status::Stolen);
		CHECK(stolen.value == 8);
		CHECK(Is_empty(deque));
	}

	// El dueño saca primero lo último que metió.
	void Test_owner_order()
	{
		Deque deque(8);
		CHECK(deque.Push(1));
		CHECK(deque.Push(2));
		CHECK(deque.Push(3));
		CHECK(deque.Pop() == 3);
		CHECK(deque.Pop() == 2);
		CHECK(deque.Pop() == 1);
		CHECK(Is_empty(deque));
	}

	// El ladrón se lleva primero lo más antiguo.
	void Test_thief_order()
	{
		Deque deque(8);
		CHECK(deque.Push(1));
		CHECK(deque.Push(2));
		CHECK(deque.Push(3));
		for (int expected = 1; expected <= 3; ++expected)
		{
			const auto stolen = deque.Steal();
			CHECK(stolen.status == Steal_status::Stolen);
			CHECK(stolen.value == expected);
		}
		CHECK(Is_empty(deque));
	}

	// Los dos extremos a la vez, muchas rondas. En cada ronda los índices avanzan uno, así que
	// con una cola pequeña el contenido cruza el final del búfer muchas veces.
	void Test_both_ends()
	{
		Deque deque(4);
		for (int round = 0; round < 1000; ++round)
		{
			const int base = round * 10;
			CHECK(deque.Push(base + 1));
			CHECK(deque.Push(base + 2));
			CHECK(deque.Push(base + 3));

			const auto stolen = deque.Steal();
			CHECK(stolen.status == Steal_status::Stolen);
			CHECK(stolen.value == base + 1);

			CHECK(deque.Pop() == base + 3);
			CHECK(deque.Pop() == base + 2);
			CHECK(Is_empty(deque));
		}
	}

	// Con un solo elemento, el dueño y el ladrón compiten por el mismo. Con un hilo no hay
	// carrera, pero sí se recorre el camino del CAS de Pop: se lo lleve quien se lo lleve, la
	// cola queda vacía y se puede seguir usando.
	void Test_last_element()
	{
		Deque deque(4);
		for (int round = 0; round < 100; ++round)
		{
			CHECK(deque.Push(round));
			CHECK(deque.Pop() == round);
			CHECK(Is_empty(deque));

			CHECK(deque.Push(round));
			const auto stolen = deque.Steal();
			CHECK(stolen.status == Steal_status::Stolen);
			CHECK(stolen.value == round);
			CHECK(Is_empty(deque));
		}
	}

	// La capacidad real no es menor que la pedida, y caben exactamente Capacity() elementos.
	void Test_capacity()
	{
		for (const std::int64_t requested : { 1, 2, 3, 4, 5, 8, 100, 1000 })
		{
			Deque deque(requested);
			CHECK(deque.Capacity() >= requested);

			// El tope del bucle evita escribir sin fin si Push nunca dijera «llena».
			std::int64_t pushed = 0;
			while (pushed <= deque.Capacity() && deque.Push(static_cast<int>(pushed)))
			{
				++pushed;
			}
			CHECK(pushed == deque.Capacity());
		}
	}

	// Con la cola llena, Push devuelve false y no cambia nada. Al salir un elemento, por
	// cualquiera de los dos extremos, vuelve a caber uno y solo uno.
	void Test_full()
	{
		Deque deque(4);
		const int capacity = static_cast<int>(deque.Capacity());
		for (int value = 0; value < capacity; ++value)
		{
			CHECK(deque.Push(value));
		}
		CHECK(!deque.Push(-1));
		CHECK(!deque.Push(-1));

		// Un ladrón se lleva el más antiguo.
		const auto stolen = deque.Steal();
		CHECK(stolen.status == Steal_status::Stolen);
		CHECK(stolen.value == 0);
		CHECK(deque.Push(capacity));
		CHECK(!deque.Push(-1));

		// El dueño saca el más reciente.
		CHECK(deque.Pop() == capacity);
		CHECK(deque.Push(capacity + 1));
		CHECK(!deque.Push(-1));

		// Queda capacity + 1 y, debajo, de capacity - 1 a 1. Ningún -1 ha entrado.
		CHECK(deque.Pop() == capacity + 1);
		for (int value = capacity - 1; value >= 1; --value)
		{
			CHECK(deque.Pop() == value);
		}
		CHECK(Is_empty(deque));
	}

	// Los índices crecen sin límite y la posición en el búfer sale de la máscara. Una ventana
	// llena que avanza mil veces su tamaño da mil vueltas al búfer sin perder ni repetir nada.
	void Test_wrap_around()
	{
		Deque deque(4);
		const int capacity = static_cast<int>(deque.Capacity());
		const int total = capacity * 1000 + 3;

		int oldest = 0;
		for (int value = 0; value < total; ++value)
		{
			if (deque.Push(value))
			{
				continue;
			}
			// Llena: sale el más antiguo y entra el nuevo.
			const auto stolen = deque.Steal();
			CHECK(stolen.status == Steal_status::Stolen);
			CHECK(stolen.value == oldest);
			++oldest;
			CHECK(deque.Push(value));
		}

		// Quedan los últimos: el dueño los saca del más reciente al más antiguo.
		for (int value = total - 1; value >= oldest; --value)
		{
			CHECK(deque.Pop() == value);
		}
		CHECK(Is_empty(deque));
	}

	// La cola es una plantilla: vale cualquier tipo que quepa en un atómico sin cerrojo.
	void Test_other_types()
	{
		const int first = 1;
		const int second = 2;
		Loom::Chase_Lev_Deque<const int*> pointers(4);
		CHECK(pointers.Push(&first));
		CHECK(pointers.Push(&second));
		const auto stolen = pointers.Steal();
		CHECK(stolen.status == Steal_status::Stolen);
		CHECK(stolen.value == &first);
		CHECK(pointers.Pop() == &second);
		CHECK(!pointers.Pop());
		CHECK(pointers.Steal().value == nullptr);

		const std::uint64_t large = std::numeric_limits<std::uint64_t>::max();
		Loom::Chase_Lev_Deque<std::uint64_t> integers(4);
		CHECK(integers.Push(large));
		CHECK(integers.Pop() == large);
		CHECK(!integers.Pop());
	}

	// Una capacidad negativa, o tan grande que no se puede redondear, se rechaza con
	// std::length_error. Si cambias esa excepción por una aserción, quita esta prueba.
	void Test_invalid_capacity()
	{
		bool negative_rejected = false;
		try
		{
			Deque deque(-1);
		}
		catch (const std::length_error&)
		{
			negative_rejected = true;
		}
		CHECK(negative_rejected);

		bool huge_rejected = false;
		try
		{
			Deque deque(std::numeric_limits<std::int64_t>::max());
		}
		catch (const std::length_error&)
		{
			huge_rejected = true;
		}
		CHECK(huge_rejected);
	}
}

int main()
{
	Test_new_deque_is_empty();
	Test_pop_on_empty_keeps_state();
	Test_owner_order();
	Test_thief_order();
	Test_both_ends();
	Test_last_element();
	Test_capacity();
	Test_full();
	Test_wrap_around();
	Test_other_types();
	Test_invalid_capacity();
	return Loom_test::Result("Test_chase_lev_deque");
}
