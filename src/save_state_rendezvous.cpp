#include "save_state_rendezvous.hpp"
#include "campaign_profile.hpp"

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>

#include "ultramodern/ultramodern.hpp"

namespace twine::state {
namespace {
std::atomic<Action> requested{Action::None};
std::atomic_bool ready_for_request{true};
Transaction* transaction_callback = nullptr;
Completion* completion_callback = nullptr;
Roots roots{};
Roots staged_roots{};
bool roots_staged = false;
GameState game_state;
std::unique_ptr<PreparedEnhancements> staged_enhancements;
std::unique_ptr<PreparedModernization> staged_modernization;
std::string owner_capture_error;

uint32_t parked = 0;
bool restore_pending = false;
const char* boundary_error = nullptr;
Action completed_action = Action::None;
std::string completion_error;
std::chrono::steady_clock::time_point deadline;
constexpr uint32_t all_roots = 31;
constexpr std::array<int32_t, 5> root_ids{2, 8, 0, 6, 7};
constexpr std::array<uint32_t, 5> root_queues{0, 0x80108948U, 0x800e4144U, 0x80102ca0U, 0x80109c00U};
std::array<int32_t, 5> waiting{};

void complete() noexcept {
    if (completion_callback) completion_callback(completed_action,
        completion_error.empty() ? nullptr : completion_error.c_str());
    completed_action = Action::None;
    completion_error.clear();
    staged_enhancements.reset();
    staged_modernization.reset();
    ready_for_request.store(true, std::memory_order_release);
}

bool eligible(uint8_t* rdram, size_t root) {
    if (root == 0) return true;

    if ((parked & ((1U << root) - 1)) != (1U << root) - 1) return false;
    if (root == size_t(Root::Dma) || root == size_t(Root::Scheduler)) {
        if (TO_PTR(OSMesgQueue, int32_t(root_queues[root]))->validCount != 0) return false;
    }
    if (root == size_t(Root::Scheduler)) {
        for (uint32_t address : {0x800dd798U, 0x800dd79cU})
            if (MEM_W(0, int32_t(address)) != 0) return false;
        if (MEM_BU(0, int32_t(0x80112fd0U)) || MEM_BU(0, int32_t(0x800e0710U))) return false;
        if (MEM_BU(0, int32_t(0x800dd7a4U)) || MEM_BU(1, int32_t(0x800dd7a4U))) return false;
    }
    return true;
}

void wake_waiting_roots(uint8_t* rdram) {
    for (size_t i = 1; i < waiting.size(); ++i) {
        if (!waiting[i] || !eligible(rdram, i)) continue;
        const auto queue = GET_MEMBER(OSMesgQueue, int32_t(root_queues[i]), blocked_on_recv);
        if (ultramodern::thread_queue_remove(rdram, queue, waiting[i]))
            ultramodern::schedule_running_thread(rdram, waiting[i]);
    }
    ultramodern::check_running_queue(rdram);
}

void release(uint8_t* rdram) {

    for (size_t i = 0; i < roots.size(); ++i) {
        if ((parked & (1U << i)) != 0) {
            ultramodern::schedule_running_thread(rdram, static_cast<int32_t>(roots[i].thread));
        }
    }
    requested.store(Action::None, std::memory_order_release);
    if (parked == 0) complete();

    ultramodern::check_running_queue(rdram);
}
}

void initialize_rendezvous(Transaction* transaction, Completion* completion) {
    if (ultramodern::is_game_started()) throw std::logic_error("State rendezvous must be installed before boot");
    transaction_callback = transaction;
    completion_callback = completion;
    ultramodern::threads::set_state_idle_callback(idle_checkpoint);
}

void stage_restore_roots(const Roots& saved) {
    if (parked != all_roots || requested.load(std::memory_order_acquire) != Action::Restore) {
        throw std::logic_error("Root restore preflight requires a complete rendezvous");
    }
    for (size_t i = 0; i < roots.size(); ++i) validate_root(saved[i], roots[i]);
    const auto game_stack = uint32_t(saved[size_t(Root::Game)].gpr[29]);
    if ((game_stack & 7U) || game_stack < 0x80000440U ||
        !get_function(int32_t(0x8000A22CU)) || !get_function(int32_t(0x8000A1D0U))) {
        throw std::runtime_error("Restored game cannot apply native audio settings safely");
    }
    staged_roots = saved;
    roots_staged = true;
}

const GameState& captured_game_state() {
    if (parked != all_roots) throw std::logic_error("Game sidecar capture requires all roots parked");
    return game_state;
}
void stage_restore_game(uint8_t* rdram, const GameState& saved) {
    if (parked != all_roots || requested.load(std::memory_order_acquire) != Action::Restore)
        throw std::logic_error("Game sidecar preparation requires a restore rendezvous");
    auto enhancements = std::make_unique<PreparedEnhancements>(saved.enhancements);
    auto modernization = std::make_unique<PreparedModernization>(rdram, saved.modernization);
    staged_enhancements = std::move(enhancements);
    staged_modernization = std::move(modernization);
}

bool request(Action action) {
    if ((action != Action::Save && action != Action::Restore) || transaction_callback == nullptr || !ultramodern::is_game_started()) return false;
    bool expected = true;
    if (!ready_for_request.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) return false;
    requested.store(action, std::memory_order_release);
    return true;
}

void idle_checkpoint(uint8_t* rdram) {
    const Action action = requested.load(std::memory_order_acquire);
    if (action == Action::None || (parked == 0 && boundary_error == nullptr)) return;
    std::string error;
    if (boundary_error) {
        error = boundary_error;
    } else if (parked == all_roots) {
        try {
            roots_staged = false;
            staged_enhancements.reset();
            staged_modernization.reset();
            transaction_callback(action, rdram, roots);
            if (action == Action::Restore && (!roots_staged || !staged_enhancements || !staged_modernization)) {
                throw std::logic_error("Restore transaction did not preflight native roots and game sidecars");
            }
            restore_pending = action == Action::Restore;
        } catch (const std::exception& failure) {
            error = failure.what();
        }
    } else if (std::chrono::steady_clock::now() >= deadline) {

        error = "Save-state rendezvous timed out; game state was not changed";
    } else {
        wake_waiting_roots(rdram);
        return;
    }
    completed_action = action;
    completion_error = std::move(error);
    boundary_error = nullptr;
    release(rdram);
}
}

extern "C" uint32_t twine_state_requested() {
    return twine::state::requested.load(std::memory_order_acquire) != twine::state::Action::None;
}

extern "C" void twine_state_root(uint8_t* rdram, recomp_context* ctx, uint32_t root, uint64_t* locals) {
    using namespace twine::state;
    if (root >= roots.size()) return;
    const auto self = ultramodern::this_thread();
    if (TO_PTR(OSThread, self)->id != root_ids[root]) {
        boundary_error = "Unexpected thread at save-state root";
        return;
    }
    for (;;) {

        if (root != uint32_t(Root::Game)) {
            const auto queue_address = int32_t(root_queues[root]);
            for (;;) {
                if (twine_state_requested() && boundary_error == nullptr && eligible(rdram, root)) break;
                if (TO_PTR(OSMesgQueue, queue_address)->validCount != 0) return;
                waiting[root] = self;
                ultramodern::thread_queue_insert(rdram,
                    GET_MEMBER(OSMesgQueue, queue_address, blocked_on_recv), self);
                ultramodern::run_next_thread_and_wait(rdram);
                waiting[root] = 0;
            }
        }
        if (!twine_state_requested() || boundary_error != nullptr) return;
        const uint32_t bit = 1U << root;
        if ((parked & bit) != 0) return;
        if (root == uint32_t(Root::Game)) {
            try {
                game_state.enhancements = capture_enhancements(rdram);
                game_state.modernization = capture_modernization(rdram);
            } catch (const std::exception& failure) {
                owner_capture_error = failure.what();
                boundary_error = owner_capture_error.c_str();
                return;
            }
        }
        if (parked == 0) {
            deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            restore_pending = false;
        }
        roots[root] = capture_root(static_cast<uint32_t>(self), *ctx, locals, get_cop1_cs());
        parked |= bit;
        TO_PTR(OSThread, self)->state = OSThreadState::STOPPED;
        ultramodern::run_next_thread_and_wait(rdram);
        if (restore_pending) {
            bool apply_audio = false;
            if (root == uint32_t(Root::Game)) {
                staged_enhancements->commit();
                staged_modernization->commit();
                apply_audio = twine::campaign::restore_state_options(rdram);
            }
            restore_root(staged_roots[root], *ctx, locals);
            set_cop1_cs(staged_roots[root].cop1);
            if (apply_audio) {

                for (const auto [address, offset] : {std::pair{0x8000A22CU, 0U},
                                                    std::pair{0x8000A1D0U, 1U}}) {
                    recomp_context call = *ctx;
                    call.r29 -= 0x40;
                    call.f_odd = call.mips3_float_mode ? &call.f1.u32l : &call.f0.u32h;
                    call.r4 = rdram[(0x1002F0 + offset) ^ 3U];
                    get_function(int32_t(address))(rdram, &call);
                }
            }
        }
        parked &= ~bit;
        if (parked == 0) complete();
        if (root == uint32_t(Root::Game)) return;

    }
}
