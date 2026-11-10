#include <Loom/Version.hpp>

namespace Loom
{
	const char* Version() noexcept
	{
		// LOOM_VERSION lo define CMakeLists.txt solo para los .cpp de la librería.
		return LOOM_VERSION;
	}
}
