#include "hnpch.h"
#include "vector_icon.h"

#include "renderer.h"

namespace Honey {

    Ref<VectorIcon> VectorIcon::create(const std::filesystem::path& path) {
        SlugIcon parsed(path);
        if (!parsed.is_valid()) return nullptr;

        auto* globals = Renderer::icon_globals();
        auto slot = globals->allocate_icon_slot(
            (uint32_t)parsed.get_band_table().size(), (uint32_t)parsed.get_curves().size());

        // Curve offsets inside BandEntry are local to this icon's own m_curves - rebase them to the slots
        // global curve offset before upload.
        std::vector<BandEntry> rebased_bands = parsed.get_band_table();
        for (auto& b : rebased_bands) b.curve_offset += slot.curve_offset;

        globals->upload_icon_data(rebased_bands.data(), slot.band_count, slot.band_offset,
            parsed.get_curves().data(), slot.curve_count, slot.curve_offset);

        Ref<VectorIcon> icon = CreateRef<VectorIcon>();
        icon->m_width = parsed.get_width();
        icon->m_height = parsed.get_height();
        icon->m_band_slot_offset = slot.band_offset;
        icon->m_band_slot_count = slot.band_count;
        icon->m_curve_slot_offset = slot.curve_offset;
        icon->m_curve_slot_count = slot.curve_count;

        icon->m_shapes = parsed.get_shapes();
        for (auto& s : icon->m_shapes) s.band_table_offset += slot.band_offset;

        return icon;
    }

    VectorIcon::~VectorIcon() {
        Renderer::icon_globals()->free_icon_slot(
            {m_band_slot_offset, m_band_slot_count,
            m_curve_slot_offset, m_curve_slot_count }
        );
    }

}
