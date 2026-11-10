#pragma once

namespace Loom
{
	// Versión de la librería, la de project(Loom VERSION ...) en CMakeLists.txt. Por ejemplo "0.1.0".
	[[nodiscard]] const char* Version() noexcept;
}
