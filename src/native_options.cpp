#include "native_options.hpp"

#include <atomic>
#include <cstdio>

#include "cheats.hpp"
#include "recompui/config.h"
#include "twine_recomp.h"

namespace {

std::atomic<twine::native_options::Tab> pending_tab{
    twine::native_options::Tab::none};
std::atomic<twine::native_options::Tab> serviced_tab{
    twine::native_options::Tab::none};
std::atomic_bool cancel_transition{false};

bool valid_rdram_address(uint32_t address, uint32_t size) {
    constexpr uint32_t rdram_size = 8U * 1024U * 1024U;
    const uint32_t segment = address & 0xE0000000U;
    const uint32_t physical = address & 0x1FFFFFFFU;
    return (segment == 0x80000000U || segment == 0xA0000000U) &&
        size <= rdram_size && physical <= rdram_size - size;
}

const char* tab_name(twine::native_options::Tab tab) {
    switch (tab) {
    case twine::native_options::Tab::controls:
        return "controls";
    case twine::native_options::Tab::graphics:
        return "graphics";
    case twine::native_options::Tab::cheats:
        return "cheats";
    case twine::native_options::Tab::none:
        return "none";
    }
    return "unknown";
}

}

namespace twine::native_options {

void service_ui_request() {
    const Tab tab = pending_tab.exchange(Tab::none, std::memory_order_acq_rel);
    switch (tab) {
    case Tab::controls:
        recompui::config::set_tab(recompui::config::controls::id);
        recompui::config::open();
        serviced_tab.store(Tab::controls, std::memory_order_release);

        break;
    case Tab::graphics:
        recompui::config::set_tab(recompui::config::graphics::id);
        recompui::config::open();
        serviced_tab.store(Tab::graphics, std::memory_order_release);

        break;
    case Tab::cheats:
        recompui::config::set_tab(twine::cheats::config_id);
        recompui::config::open();
        serviced_tab.store(Tab::cheats, std::memory_order_release);

        break;
    case Tab::none:
        break;
    }
}

Tab take_serviced_tab() {
    return serviced_tab.exchange(Tab::none, std::memory_order_acq_rel);
}

}

extern "C" uint32_t twine_route_native_options(
    uint8_t* rdram,
    recomp_context*
) {
    if (rdram == nullptr) {
        return 0U;
    }
    const uint32_t manager = static_cast<uint32_t>(
        TWINE_MEM_W(0x9BB0U, 0x80100000U));
    if (!valid_rdram_address(manager, 0x34U)) {
        return 0U;
    }
    const uint32_t definition = static_cast<uint32_t>(
        TWINE_MEM_W(0x10U, manager));
    const uint32_t entry = static_cast<uint32_t>(
        TWINE_MEM_W(0x18U, manager));
    const twine::native_options::Tab tab =
        twine::native_options::route(definition, entry);
    if (tab == twine::native_options::Tab::none) {
        return 0U;
    }
    pending_tab.store(tab, std::memory_order_release);
    cancel_transition.store(true, std::memory_order_release);

    return 1U;
}

extern "C" uint32_t twine_cancel_native_options_transition() {
    return cancel_transition.exchange(false, std::memory_order_acq_rel)
        ? 1U : 0U;
}
