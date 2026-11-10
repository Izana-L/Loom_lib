#include <Loom/Loom.hpp>
#include <cstdio>

// Punto de entrada de Loom_bench. Ahora solo imprime con qué se ha compilado.
// Aquí va tu arnés de medida y los seis escenarios de la fase 0; puedes repartirlos en
// más archivos .cpp y .hpp dentro de code/benchmarks: CMake los recoge todos.
//
// LOOM_BENCH_COMPILER y LOOM_BENCH_BUILD_TYPE los define CMakeLists.txt.

int main()
{
	std::printf("Loom %s\n", Loom::Version());
	std::printf("Compilador:    %s\n", LOOM_BENCH_COMPILER);
	std::printf("Configuracion: %s\n", LOOM_BENCH_BUILD_TYPE);
	return 0;
}
