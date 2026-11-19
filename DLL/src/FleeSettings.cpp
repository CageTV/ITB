#include "PCH.h"
#include "Flee.h"

namespace Flee { Settings* Settings::GetSingleton() { static Settings s; return &s; } }
