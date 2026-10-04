#include "radial_view.hpp"
#include "radial_text.hpp"
#include "font_textures.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>
#include "RmlUi/Core/CallbackTexture.h"
#include "RmlUi/Core/ElementInstancer.h"
#include "RmlUi/Core/Factory.h"
#include "RmlUi/Core/Geometry.h"
#include "RmlUi/Core/MeshUtilities.h"
#include "RmlUi/Core/RenderManager.h"

namespace {
struct MenuFont {
    std::shared_ptr<const twine::fonts::Atlas> atlas = twine::fonts::menu_atlas();
    Rml::CallbackTexture texture;

    Rml::Texture get_texture(Rml::RenderManager& renderer) {
        if (!texture) {
            texture = renderer.MakeCallbackTexture([data = atlas](const Rml::CallbackTextureInterface& target) {

                constexpr int tile = 32, columns = 16, scale = 4;
                constexpr int width = tile * columns * scale, height = tile * 6 * scale;
                std::vector<Rml::byte> pixels(width * height * 4, 0);
                for (size_t i = 0; i < data->glyphs.size(); ++i) {
                    const auto& g = data->glyphs[i];
                    for (unsigned y = 0; y < g.height && y < tile - 2; ++y) {
                        for (unsigned x = 0; x < g.width && x < tile - 2; ++x) {
                            const auto source = (g.y + y) * twine::fonts::width + g.x + x;
                            const auto packed = data->packed[source / 2];
                            const auto alpha = uint8_t(((source & 1) ? packed & 15 : packed >> 4) * 17);
                            for (int dy = 0; dy < scale; ++dy) for (int dx = 0; dx < scale; ++dx) {
                                const auto dest = (((i / columns * tile + y + 1) * scale + dy) * width +
                                    (i % columns * tile + x + 1) * scale + dx) * 4;
                                std::fill_n(pixels.data() + dest, 4, alpha);
                            }
                        }
                    }
                }
                return target.GenerateTexture({pixels.data(), pixels.size()}, {width, height});
            });
        }
        return texture;
    }
};

std::weak_ptr<MenuFont> shared_font;

class NativeLabel final : public Rml::Element {
    std::shared_ptr<MenuFont> font;
    std::string text;
    Rml::Geometry geometry;
    bool dirty = true;

    float pixel_scale() const { return GetComputedValues().font_size() / 8.0f; }

    Rml::Vector2f measure() const {
        const auto metrics = twine::radial::view::measure(*font->atlas, text);
        return {metrics.width * pixel_scale(), std::max(8.0f, metrics.height) * pixel_scale()};
    }

protected:
    bool GetIntrinsicDimensions(Rml::Vector2f& size, float& ratio) override {
        size = measure();
        ratio = size.y > 0 ? size.x / size.y : 1;
        return true;
    }

    void OnAttributeChange(const Rml::ElementAttributes& changed) override {
        Rml::Element::OnAttributeChange(changed);
        if (changed.find("text") != changed.end()) {
            text = GetAttribute<Rml::String>("text", "").substr(0, 80);
            for (char& c : text) if (uint8_t(c) < 32 || uint8_t(c) > 126) c = '?';
            dirty = true;
            DirtyLayout();
        }
    }

    void OnPropertyChange(const Rml::PropertyIdSet& changed) override {
        Rml::Element::OnPropertyChange(changed);
        if (changed.Contains(Rml::PropertyId::Color) || changed.Contains(Rml::PropertyId::Opacity) ||
                changed.Contains(Rml::PropertyId::FontSize)) dirty = true;
        if (changed.Contains(Rml::PropertyId::FontSize)) DirtyLayout();
    }

    void OnResize() override { dirty = true; }

    void OnRender() override {
        auto* renderer = GetRenderManager();
        if (!renderer || text.empty()) return;
        if (dirty) {
            auto mesh = geometry.Release(Rml::Geometry::ReleaseMode::ClearMesh);
            const auto& style = GetComputedValues();
            const auto color = style.color().ToPremultiplied(style.opacity());
            const auto size = GetBox().GetSize(Rml::BoxArea::Content);
            const float fit = std::clamp(size.x / std::max(1.0f, measure().x), 0.0f, 1.0f);
            const float scale = pixel_scale() * fit;
            const auto metrics = twine::radial::view::measure(*font->atlas, text);
            const float inset = std::max(0.0f, (size.y - metrics.height * scale) * 0.5f);
            float x = 0;
            for (unsigned char c : text) {
                if (c == ' ') { x += font->atlas->space_advance * scale; continue; }
                const unsigned index = twine::radial::view::glyph_index(c);
                const auto& g = font->atlas->glyphs[index];
                const Rml::Vector2f uv0{float(index % 16 * 32 + 1) / 512, float(index / 16 * 32 + 1) / 192};
                const Rml::Vector2f uv1 = uv0 + Rml::Vector2f{float(g.width) / 512, float(g.height) / 192};
                const float y = inset + (-float(static_cast<int8_t>(g.baseline)) - metrics.top) * scale;
                Rml::MeshUtilities::GenerateQuad(mesh, {x, y},
                    {g.width * scale, g.height * scale}, color, uv0, uv1);
                x += std::max(0, int(g.width) + font->atlas->letter_spacing) * scale;
            }
            geometry = renderer->MakeGeometry(std::move(mesh));
            dirty = false;
        }
        geometry.Render(GetAbsoluteOffset(Rml::BoxArea::Content).Round(), font->get_texture(*renderer));
    }

public:
    explicit NativeLabel(const Rml::String& tag) : Rml::Element(tag), font(shared_font.lock()) {
        if (!font) { font = std::make_shared<MenuFont>(); shared_font = font; }
    }
};

class Grid final : public Rml::Element {
    Rml::Geometry geometry;
    bool dirty = true;
    void OnResize() override { dirty = true; }
    void OnRender() override {
        auto* renderer = GetRenderManager();
        if (!renderer) return;
        if (dirty) {
            auto mesh = geometry.Release(Rml::Geometry::ReleaseMode::ClearMesh);
            const auto size = GetBox().GetSize(Rml::BoxArea::Content);
            const float step = std::max(12.0f, std::round(size.y / 24));
            const auto ink = Rml::Colourb{25, 69, 60, 90}.ToPremultiplied();
            for (float x = 0; x < size.x; x += step)
                Rml::MeshUtilities::GenerateQuad(mesh, {x, 0}, {1, size.y}, ink);
            for (float y = 0; y < size.y; y += step)
                Rml::MeshUtilities::GenerateQuad(mesh, {0, y}, {size.x, 1}, ink);
            geometry = renderer->MakeGeometry(std::move(mesh));
            dirty = false;
        }
        geometry.Render(GetAbsoluteOffset(Rml::BoxArea::Content).Round());
    }
public:
    explicit Grid(const Rml::String& tag) : Rml::Element(tag) {}
};
}

void twine::radial::view::register_elements() {
    static Rml::ElementInstancerGeneric<NativeLabel> label;
    static Rml::ElementInstancerGeneric<Grid> grid;
    Rml::Factory::RegisterElementInstancer("twine-radial-label", &label);
    Rml::Factory::RegisterElementInstancer("twine-radial-grid", &grid);
}

twine::radial::view::Label::Label(recompui::ResourceId id, recompui::Element* parent)
    : recompui::Element(id, parent, 0, "twine-radial-label") {}

void twine::radial::view::Label::set_text(std::string_view text) {
    if (text == current_text) return;
    current_text = text;
    set_attribute("text", current_text);
}
