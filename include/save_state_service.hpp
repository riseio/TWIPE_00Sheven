#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

namespace twine::state {
enum class Slot { Manual, Checkpoint };

void initialize(const std::filesystem::path& configuration_directory);
bool save(Slot slot = Slot::Manual);
bool restore(Slot slot = Slot::Manual);
bool take_notification(std::string& title, std::string& message);
struct OperationStatus { uint64_t sequence; bool restored; bool succeeded; };
OperationStatus operation_status();
}
