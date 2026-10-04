#ifndef TWINE_RECOMP_H
#define TWINE_RECOMP_H

#include "recomp.h"

#undef ADD32
#undef SUB32
#ifdef _MSC_VER
static __forceinline gpr twine_add32(gpr a, gpr b) {
    uint32_t result;
    (void)_addcarry_u32(0, (uint32_t)a, (uint32_t)b, &result);
    return (gpr)(int64_t)(int32_t)result;
}

static __forceinline gpr twine_sub32(gpr a, gpr b) {
    uint32_t result;
    (void)_subborrow_u32(0, (uint32_t)a, (uint32_t)b, &result);
    return (gpr)(int64_t)(int32_t)result;
}

#define ADD32(a, b) twine_add32((a), (b))
#define SUB32(a, b) twine_sub32((a), (b))
#else
#define ADD32(a, b) \
    ((gpr)(int64_t)(int32_t)((uint32_t)(a) + (uint32_t)(b)))
#define SUB32(a, b) \
    ((gpr)(int64_t)(int32_t)((uint32_t)(a) - (uint32_t)(b)))
#endif

static inline gpr twine_n64_address(uint32_t address) {
    return (gpr)(int64_t)(int32_t)address;
}

#define TWINE_MEM_W(offset, address) \
    MEM_W((offset), twine_n64_address((uint32_t)(address)))
#define TWINE_MEM_H(offset, address) \
    MEM_H((offset), twine_n64_address((uint32_t)(address)))
#define TWINE_MEM_B(offset, address) \
    MEM_B((offset), twine_n64_address((uint32_t)(address)))
#define TWINE_MEM_HU(offset, address) \
    MEM_HU((offset), twine_n64_address((uint32_t)(address)))
#define TWINE_MEM_BU(offset, address) \
    MEM_BU((offset), twine_n64_address((uint32_t)(address)))

#ifdef __cplusplus
extern "C" {
#endif

uint32_t twine_camera_query_active(recomp_context* ctx);
uint32_t twine_texture_conversion_query(const recomp_context* context);
uint32_t twine_capture_camera_query(uint8_t* rdram, recomp_context* ctx);
uint32_t twine_text_query_active(recomp_context* ctx);
uint32_t twine_query_invalid_word(uint32_t address);
uint32_t twine_text_query_invalid_span(recomp_context* ctx);
void twine_capture_objective(uint8_t* rdram, recomp_context* ctx);
uint32_t twine_material_require(recomp_context* ctx, uint32_t address, uint32_t size);
uint32_t twine_material_cursor(recomp_context* ctx);
uint32_t twine_material_select(uint8_t* rdram, recomp_context* ctx, uint32_t palette);
uint32_t twine_material_draw(recomp_context* ctx);

void kmcWritebackDCache(uint8_t* rdram, recomp_context* ctx);
void osYieldThread_recomp(uint8_t* rdram, recomp_context* ctx);
void twine_register_overlay(uint8_t* rdram, recomp_context* ctx);
void twine_init_render_tags(uint8_t* rdram, recomp_context* ctx);
void twine_begin_render_tags(uint8_t* rdram, recomp_context* ctx);
void twine_finish_render_tags(uint8_t* rdram, recomp_context* ctx);
void twine_tag_model(uint8_t* rdram, uint32_t command, uint32_t owner,
    uint32_t resource, uint32_t part, uint32_t domain);
void twine_tag_projection(uint8_t* rdram, uint32_t command, uint32_t role);
void twine_begin_static_draw(uint8_t* rdram, uint32_t owner, uint32_t part);
void twine_finish_static_draw(uint8_t* rdram, uint32_t resource);
void twine_prepare_visibility(uint8_t* rdram, uint32_t camera, uint32_t view);
void twine_visibility_plane(uint8_t* rdram, recomp_context* ctx);
void twine_visibility_triangle(uint8_t* rdram, recomp_context* ctx);
void twine_visibility_door(uint8_t* rdram, recomp_context* ctx);
void twine_visibility_sphere(uint8_t* rdram, recomp_context* ctx);
void twine_visibility_distance(uint8_t* rdram, recomp_context* ctx);
void twine_capture_float_matrix(uint8_t* rdram, recomp_context* ctx);
void twine_finish_float_matrix(uint8_t* rdram, recomp_context* ctx);
void twine_defer_projection_tag(uint32_t command);
void twine_finish_projection_tag(uint8_t* rdram);
void twine_render_camera(uint8_t* rdram, uint32_t camera, uint32_t view);
uint32_t twine_render_matrix_address(uint32_t task, uint32_t native);
void twine_render_scope(uint32_t owner);
void twine_render_sprite_owner(uint32_t owner);
void twine_tag_sprite(uint8_t* rdram, uint32_t command, uint32_t resource);
void twine_reuse_render_owner(uint32_t owner);
void twine_render_scope_matrix(uint32_t command);
void twine_tag_scoped_model(uint8_t* rdram, uint32_t resource);
void twine_cut_render_tags(void);
void twine_retire_render_tags(uint8_t* rdram, recomp_context* ctx);
void twine_match_portal_aspect(uint8_t* rdram, recomp_context* ctx);
void twine_apply_gameplay_input_layout(uint8_t* rdram, recomp_context* ctx);
void twine_begin_ui_aspect(uint8_t* rdram, recomp_context* ctx);
uint32_t twine_ui_widescreen_alignment_enabled(void);
void twine_align_ui_node(uint8_t* rdram, recomp_context* ctx);
void twine_finish_ui_node(uint8_t* rdram, recomp_context* ctx);
void twine_end_ui_aspect(uint8_t* rdram, recomp_context* ctx);
void twine_reset_input_prompts(uint8_t* rdram, recomp_context* ctx);
void twine_track_attract_session(uint8_t* rdram, recomp_context* ctx);
void twine_begin_menu_transition(uint8_t* rdram, recomp_context* ctx);
void twine_finish_menu_transition(uint8_t* rdram, recomp_context* ctx);
void twine_complete_instant_pause_movie(uint8_t* rdram, recomp_context* ctx);
void twine_complete_instant_pause_close_status(
    uint8_t* rdram, recomp_context* ctx, uint32_t status);
void twine_finish_instant_pause_close(uint8_t* rdram, recomp_context* ctx);
void twine_begin_input_prompt_frame(uint8_t* rdram, recomp_context* ctx);
void twine_finish_input_prompt_frame(uint8_t* rdram, recomp_context* ctx);
void twine_capture_render_task(uint8_t* rdram, recomp_context* ctx);
void twine_initialize_simulation_period(uint8_t* rdram, recomp_context* ctx);
void twine_sample_simulation_time(uint8_t* rdram, recomp_context* ctx);
void twine_commit_simulation_time(uint8_t* rdram, recomp_context* ctx);
void twine_prepare_input_prompts(uint8_t* rdram, recomp_context* ctx);
void twine_restore_input_prompts(uint8_t* rdram, recomp_context* ctx);
void twine_prepare_localized_input_prompt(
    uint8_t* rdram, recomp_context* ctx);
uint32_t twine_override_modern_axis(uint8_t* rdram, recomp_context* ctx);
void twine_guided_target_enabled(uint8_t* rdram, recomp_context* ctx);
void twine_apply_modern_zoom(uint8_t* rdram, recomp_context* ctx);
void twine_apply_modern_look(uint8_t* rdram, recomp_context* ctx);
void twine_begin_gameplay_input_tick(uint8_t* rdram, recomp_context* ctx);
void twine_sprint_observe_interaction(uint8_t* rdram, recomp_context* ctx);
void twine_sprint_apply(uint8_t* rdram, recomp_context* ctx);
uint32_t twine_state_requested(void);
void twine_state_root(uint8_t* rdram, recomp_context* ctx, uint32_t root, uint64_t* locals);

void twine_regeneration_damage(uint8_t* rdram, recomp_context* ctx);
void twine_weapon_initialize(uint8_t* rdram, recomp_context* ctx);
void twine_restore_weapon_mode(uint8_t* rdram, recomp_context* ctx);
void twine_weapon_mode_fallback(uint8_t* rdram, recomp_context* ctx);
uint32_t twine_weapon_pickup_rounds(uint32_t item, uint32_t native_rounds);
uint32_t twine_ammo_pickup_rounds(uint32_t type, uint32_t native_rounds);
uint32_t twine_weapon_pickup_magazine(uint32_t item, uint32_t capacity);
void twine_laser_ammo_cost(uint8_t* rdram, recomp_context* ctx);
void twine_reload_animation_step(uint8_t* rdram, recomp_context* ctx);
void twine_ladder_jump(uint8_t* rdram, recomp_context* ctx);
uint32_t twine_npc_projectile_policy(uint8_t* rdram, uint32_t actor, uint32_t projectile);
void twine_finish_gameplay_input_tick(uint8_t* rdram, recomp_context* ctx);
void twine_filter_sniper_idle_sway(uint8_t* rdram, recomp_context* ctx);
void twine_apply_sniper_scope_toggle(uint8_t* rdram, recomp_context* ctx);
uint32_t twine_take_modern_action(
    uint8_t* rdram,
    recomp_context* ctx,
    uint32_t action);
void twine_apply_weapon_category(uint8_t* rdram, recomp_context* ctx);
void twine_grapple_apply_pull(uint8_t* rdram, recomp_context* ctx);
void twine_grapple_filter_gravity(uint8_t* rdram, recomp_context* ctx);
void twine_grapple_observe_surface(uint8_t* rdram, recomp_context* ctx);
void twine_grapple_finish_landing(uint8_t* rdram, recomp_context* ctx);
void twine_radial_sync(uint8_t* rdram, recomp_context* ctx);
void twine_radial_apply_weapon(uint8_t* rdram, recomp_context* ctx);
void twine_radial_prepare_gadget(uint8_t* rdram, recomp_context* ctx);
void twine_radial_apply_special(uint8_t* rdram, recomp_context* ctx);
uint32_t twine_radial_should_freeze(uint8_t* rdram, recomp_context* ctx);
uint32_t twine_route_native_options(uint8_t* rdram, recomp_context* ctx);
uint32_t twine_cancel_native_options_transition(void);
void twine_sync_cheats(uint8_t* rdram, recomp_context* ctx);
void twine_restore_cheats(uint8_t* rdram, recomp_context* ctx);
void twine_apply_civilian_health(uint8_t* rdram, recomp_context* ctx);
uint32_t twine_npc_is_protected(uint8_t* rdram, uint32_t actor);
uint32_t twine_npc_filter_damage(uint8_t* rdram, uint32_t actor);
uint32_t twine_should_skip_fall_damage(uint8_t* rdram, recomp_context* ctx);
void twine_filter_oxygen_depletion(uint8_t* rdram, recomp_context* ctx);
void twine_invalidate_transient_state(uint8_t* rdram, recomp_context* ctx);
uint32_t twine_profile_should_commit(uint8_t* rdram, recomp_context* ctx);
uint32_t twine_profile_capture(uint8_t* rdram, recomp_context* ctx);
uint32_t twine_profile_capture_level_completion(
    uint8_t* rdram,
    recomp_context* ctx);
uint32_t twine_profile_install(uint8_t* rdram, recomp_context* ctx);
void twine_profile_begin_defaults(uint8_t* rdram, recomp_context* ctx);
void twine_profile_finish_defaults(uint8_t* rdram, recomp_context* ctx);
uint32_t twine_profile_take_pending_apply(
    uint8_t* rdram,
    recomp_context* ctx);
void twine_profile_level_completion_begin(
    uint8_t* rdram,
    recomp_context* ctx);
uint32_t twine_profile_take_level_completion(
    uint8_t* rdram,
    recomp_context* ctx);
void twine_profile_omit_debrief_save(
    uint8_t* rdram,
    recomp_context* ctx);
uint32_t twine_profile_legacy_load(uint8_t* rdram, recomp_context* ctx);
uint32_t twine_profile_legacy_save(uint8_t* rdram, recomp_context* ctx);
void __osViSwapContext_recomp(uint8_t* rdram, recomp_context* ctx);
void osPfsInit_recomp(uint8_t* rdram, recomp_context* ctx);
void osPfsInitPak_recomp(uint8_t* rdram, recomp_context* ctx);
void osPfsFreeBlocks_recomp(uint8_t* rdram, recomp_context* ctx);
void osPfsAllocateFile_recomp(uint8_t* rdram, recomp_context* ctx);
void osPfsDeleteFile_recomp(uint8_t* rdram, recomp_context* ctx);
void osPfsFileState_recomp(uint8_t* rdram, recomp_context* ctx);
void osPfsFindFile_recomp(uint8_t* rdram, recomp_context* ctx);
void osPfsReadWriteFile_recomp(uint8_t* rdram, recomp_context* ctx);
void osPfsChecker_recomp(uint8_t* rdram, recomp_context* ctx);
void osPfsNumFiles_recomp(uint8_t* rdram, recomp_context* ctx);
void twine_prepare_font_textures(uint8_t* rdram, recomp_context* ctx);
void twine_filter_frontend_text(uint8_t* rdram, recomp_context* ctx, uint32_t resource);
void osPfsRepairId_recomp(uint8_t* rdram, recomp_context* ctx);

#ifdef __cplusplus
}
#endif

#endif
