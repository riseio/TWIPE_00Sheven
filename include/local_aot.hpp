#pragma once

#include <filesystem>
#include <span>
#include "local_aot_abi.h"

namespace twine::local_aot {

bool prepare();
bool prepare(std::span<const uint8_t> validated_rom);
const TwineAotModule& module();
void register_loaded_overlays();
}
