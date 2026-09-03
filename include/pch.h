#pragma once

#include "RE/Skyrim.h"
#include "REL/Relocation.h"
#include "SKSE/SKSE.h"

#include <spdlog/sinks/basic_file_sink.h>

#ifndef NDEBUG
#include <spdlog/sinks/msvc_sink.h>
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#define DLLEXPORT __declspec(dllexport)

using namespace std::literals;
using namespace REL::literals;
