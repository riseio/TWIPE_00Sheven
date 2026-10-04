#include "local_aot.hpp"

#include "librecomp/overlays.hpp"

void twine::local_aot::register_loaded_overlays() {
    const auto& module = twine::local_aot::module();
    recomp::overlays::register_overlays(
        {
            .code_sections = module.sections,
            .num_code_sections = module.section_count,
            .total_num_sections = module.total_sections,
        },
        {
            .table = module.overlays,
            .len = module.overlay_count,
        });
}
