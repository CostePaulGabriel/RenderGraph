#pragma once

// @TODO : implement a structure that represents an MaterialX.

#include "../Concepts/API.h"
#include "../Math/Color.h"

namespace LynxEngine
{
	struct LYNX_ENGINE_API MaterialData
	{
		//Color BaseColor = Color::White;
		uint32_t AlbedoIndex;
	};
}