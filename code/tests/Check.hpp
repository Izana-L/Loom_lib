#pragma once

// Mini arnés de tests de Loom. Cada archivo Test_*.cpp o Stress_*.cpp de esta carpeta es un
// ejecutable con su propio main, y CMake lo registra en ctest con add_test. Un test pasa si
// su main devuelve 0.
//
// CHECK no usa assert: sigue funcionando en Release (con NDEBUG).
//
// A diferencia del de Math, CHECK se puede llamar desde varios hilos a la vez: los contadores
// son atómicos. Con int normales, el propio arnés tendría una carrera y ThreadSanitizer
// avisaría de ella en cada test con hilos.

#include <atomic>
#include <cstdio>

namespace Loom_test
{
	inline std::atomic<int> total_checks = 0;
	inline std::atomic<int> failed_checks = 0;

	inline void Check(const bool passed, const char* expression, const char* file, const int line) noexcept
	{
		total_checks.fetch_add(1, std::memory_order_relaxed);
		if (!passed)
		{
			failed_checks.fetch_add(1, std::memory_order_relaxed);
			std::fprintf(stderr, "%s:%d: FALLO: %s\n", file, line, expression);
		}
	}

	// Devuelve el código de salida del test: 0 si todo ha ido bien.
	// Llámalo desde main cuando todos los hilos del test hayan terminado (join).
	[[nodiscard]] inline int Result(const char* test_name) noexcept
	{
		const int total = total_checks.load();
		const int failed = failed_checks.load();
		std::printf("%s: %d de %d comprobaciones correctas\n", test_name, total - failed, total);
		return failed == 0 ? 0 : 1;
	}
}

// "? true : false" convierte a bool igual que un if (vale para std::optional) sin avisos de cast inútil.
#define CHECK(...) ::Loom_test::Check((__VA_ARGS__) ? true : false, #__VA_ARGS__, __FILE__, __LINE__)
