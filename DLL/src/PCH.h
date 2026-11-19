/*
 * ITB Wildlife
 * Copyright (c) 2026 CageTV
 *
 * Licensed under the GNU General Public License, version 3 or later; see LICENSE.
 */
#pragma once

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <spdlog/sinks/basic_file_sink.h>

#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace std::literals;

// CommonLib callbacks pass the object by reference in one build and by pointer in another: a generic lambda plus this works with both
namespace compat
{
	template <class T>
	inline T* Ptr(T& a_ref) { return &a_ref; }
	template <class T>
	inline T* Ptr(T* a_ptr) { return a_ptr; }
}
