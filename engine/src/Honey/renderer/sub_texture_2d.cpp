#include "sub_texture_2d.h"

namespace Honey {
	SubTexture2D::SubTexture2D(const Ref<Texture2D> &texture, const glm::vec2 &min, const glm::vec2 &max)
		: m_texture(texture) {
		m_tex_coords[0] = {min.x, min.y};
		m_tex_coords[1] = {max.x, min.y};
		m_tex_coords[2] = {max.x, max.y};
		m_tex_coords[3] = {min.x, max.y};
	}

	Ref<SubTexture2D> SubTexture2D::create_from_coords(
	const Ref<Texture2D>& texture,
	const glm::vec2& coords,
	const glm::vec2& cell_size,
	const glm::vec2& sprite_size,
	const glm::vec2& padding,
	const glm::vec2& offset)
	{
		// Compute how many rows in the sheet
		int sheet_rows = static_cast<int>(texture->get_height() / (cell_size.y + padding.y));

		// Flip the Y coordinate
		float flipped_y = (float)(sheet_rows - coords.y - sprite_size.y);

		// Calculate position in pixels
		float left = offset.x + coords.x * (cell_size.x + padding.x);
		float bottom = offset.y + flipped_y * (cell_size.y + padding.y);

		float right = left + sprite_size.x * cell_size.x;
		float top = bottom + sprite_size.y * cell_size.y;

		glm::vec2 min = {left / texture->get_width(), bottom / texture->get_height()};
		glm::vec2 max = {right / texture->get_width(), top / texture->get_height()};

		return CreateRef<SubTexture2D>(texture, min, max);
	}



}