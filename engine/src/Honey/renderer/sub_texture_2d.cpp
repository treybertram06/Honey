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
		uint32_t rows = texture->get_height() / (cell_size.y + padding.y);

		glm::vec2 flipped_coords = {coords.x, (rows - 1) - coords.y};

		// Compute top-left in pixels
		float texture_x_left = flipped_coords.x * (cell_size.x + padding.x) + offset.x;
		float texture_y_top  = flipped_coords.y * (cell_size.y + padding.y) + offset.y;

		// Compute bottom-right in pixels, accounting for sprite size
		float texture_x_right  = texture_x_left + cell_size.x * sprite_size.x;
		float texture_y_bottom = texture_y_top + cell_size.y * sprite_size.y;

		// Convert to UVs
		glm::vec2 min = {texture_x_left / texture->get_width(), texture_y_bottom / texture->get_height()};
		glm::vec2 max = {texture_x_right / texture->get_width(), texture_y_top / texture->get_height()};

		return CreateRef<SubTexture2D>(texture, min, max);
	}



}