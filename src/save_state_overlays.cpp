#include "save_state_overlays.hpp"
#include "save_state_codec.hpp"
#include "librecomp/overlays.hpp"

namespace twine::state {
struct PreparedOverlays::Impl {
    recomp::overlays::PreparedState dispatch;
    explicit Impl(recomp::overlays::PreparedState&& state) : dispatch(std::move(state)) {}
};
Bytes capture_overlays() {
    Writer out;
    const auto sections = recomp::overlays::capture_state(8U * 1024 * 1024);
    out.fields(uint32_t(1), uint32_t(sections.size()));
    for (const auto& section : sections) out.fields(section.rom, section.ram, section.size);
    return std::move(out.bytes);
}
PreparedOverlays::PreparedOverlays(std::span<const uint8_t> saved) {
    Reader in(saved);
    if (in.u32() != 1) throw std::runtime_error("Unsupported overlay state schema");
    const auto count = in.u32();
    if (count > 1024 || count > saved.size() / 12) throw std::runtime_error("Invalid overlay state count");
    std::vector<recomp::overlays::SectionBinding> sections;
    sections.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const auto rom = in.u32(), ram = in.u32(), size = in.u32();
        sections.push_back({rom, ram, size});
    }
    in.end();
    impl = std::make_unique<Impl>(recomp::overlays::prepare_state(sections, 8U * 1024 * 1024));
}
PreparedOverlays::~PreparedOverlays() = default;
PreparedOverlays::PreparedOverlays(PreparedOverlays&&) noexcept = default;
PreparedOverlays& PreparedOverlays::operator=(PreparedOverlays&&) noexcept = default;
void PreparedOverlays::commit() noexcept { impl->dispatch.commit(); }
}
