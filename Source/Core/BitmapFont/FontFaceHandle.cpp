/*
 * This source file is part of RmlUi, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://github.com/mikke89/RmlUi
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 * Copyright (c) 2019 The RmlUi Team, and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "precompiled.h"
#include "FontFaceHandle.h"
#include "FontFaceLayer.h"
#include <algorithm>
#include "../TextureLayout.h"

namespace Rml {
namespace Core {
namespace BitmapFont {


FontFaceHandle::FontFaceHandle()
{
	size = 0;
	average_advance = 0;
	x_height = 0;
	line_height = 0;
	baseline = 0;

	underline_position = 0;
	underline_thickness = 0;

	base_layer = NULL;
}

FontFaceHandle::~FontFaceHandle()
{
}

// Initialises the handle so it is able to render text.
bool FontFaceHandle::Initialise(BitmapFontDefinitions *bm_face, const String& _charset, int _size)
{
	this->bm_face = bm_face;
	size = _size;
	line_height = _size;
	texture_width = bm_face->CommonCharactersInfo.ScaleWidth;
	texture_height = bm_face->CommonCharactersInfo.ScaleHeight;
	raw_charset = _charset;

	// Construct proper path to texture
	URL fnt_source = bm_face->Face.Source;
	URL bitmap_source = bm_face->Face.BitmapSource;
	if(bitmap_source.GetPath().empty())
	{
		texture_source = fnt_source.GetPath() + bitmap_source.GetFileName();
		if(!bitmap_source.GetExtension().empty())
		{
			texture_source += "." + bitmap_source.GetExtension();
		}
	}
	else
	{
		texture_source = bitmap_source.GetPathedFileName();
	}

	if (!UnicodeRange::BuildList(charset, raw_charset))
	{
		Log::Message(Log::LT_ERROR, "Invalid font charset '%s'.", raw_charset.c_str());
		return false;
	}

	// Construct the list of the characters specified by the charset.
	for (size_t i = 0; i < charset.size(); ++i)
		BuildGlyphMap(bm_face, charset[i]);

	// Generate the metrics for the handle.
	GenerateMetrics(bm_face);

	// Generate the default layer and layer configuration.
	base_layer = GenerateLayer(NULL);
	layer_configurations.push_back(LayerConfiguration());
	layer_configurations.back().push_back(base_layer);

	return true;
}

// Returns the width a string will take up if rendered with this handle.
int FontFaceHandle::GetStringWidth(const WString& string, word prior_character) const
{
	int width = 0;

	for (size_t i = 0; i < string.size(); i++)
	{
		word character_code = string[i];

		if (character_code >= glyphs.size())
			continue;
		const FontGlyph &glyph = glyphs[character_code];

		// Adjust the cursor for the kerning between this character and the previous one.
		if (prior_character != 0)
			width += GetKerning(prior_character, string[i]);
		// Adjust the cursor for this character's advance.
		width += glyph.advance;

		prior_character = character_code;
	}

	return width;
}

// Generates the texture data for a layer (for the texture database).
bool FontFaceHandle::GenerateLayerTexture(const byte*& texture_data, Vector2i& texture_dimensions, Rml::Core::FontEffect* layer_id, int texture_id)
{
	FontLayerMap::iterator layer_iterator = layers.find(layer_id);
	if (layer_iterator == layers.end())
		return false;

	return layer_iterator->second->GenerateTexture(texture_data, texture_dimensions, texture_id);
}

// Generates the geometry required to render a single line of text.
int FontFaceHandle::GenerateString(GeometryList& geometry, const WString& string, const Vector2f& position, const Colourb& colour, int layer_configuration_index) const
{
	int geometry_index = 0;
	int line_width = 0;

	RMLUI_ASSERT(layer_configuration_index >= 0);
	RMLUI_ASSERT(layer_configuration_index < (int) layer_configurations.size());

	// Fetch the requested configuration and generate the geometry for each one.
	const LayerConfiguration& layer_configuration = layer_configurations[layer_configuration_index];
	for (size_t i = 0; i < layer_configuration.size(); ++i)
	{
		Rml::Core::FontFaceLayer* layer = layer_configuration[i];

		Colourb layer_colour;
		if (layer == base_layer)
			layer_colour = colour;
		else
			layer_colour = layer->GetColour();

		// Resize the geometry list if required.
		if ((int) geometry.size() < geometry_index + layer->GetNumTextures())
			geometry.resize(geometry_index + layer->GetNumTextures());

		// Bind the textures to the geometries.
		for (int i = 0; i < layer->GetNumTextures(); ++i)
			geometry[geometry_index + i].SetTexture(layer->GetTexture(i));

		line_width = 0;
		word prior_character = 0;

		const word* string_iterator = string.c_str();
		const word* string_end = string.c_str() + string.size();

		for (; string_iterator != string_end; string_iterator++)
		{
			if (*string_iterator >= glyphs.size())
				continue;
			const FontGlyph &glyph = glyphs[*string_iterator];

			// Adjust the cursor for the kerning between this character and the previous one.
			if (prior_character != 0)
				line_width += GetKerning(prior_character, *string_iterator);

			layer->GenerateGeometry(&geometry[geometry_index], *string_iterator, Vector2f(position.x + line_width, position.y), layer_colour);

			line_width += glyph.advance;
			prior_character = *string_iterator;
		}

		geometry_index += layer->GetNumTextures();
	}

	// Cull any excess geometry from a previous generation.
	geometry.resize(geometry_index);

	return line_width;
}

// Generates the geometry required to render a line above, below or through a line of text.
void FontFaceHandle::GenerateLine(Geometry* geometry, const Vector2f& position, int width, Font::Line height, const Colourb& colour) const
{
	std::vector< Vertex >& line_vertices = geometry->GetVertices();
	std::vector< int >& line_indices = geometry->GetIndices();

	float offset;
	switch (height)
	{
		case Font::UNDERLINE:			offset = -underline_position; break;
		case Font::OVERLINE:			// where to place? offset = -line_height - underline_position; break;
		case Font::STRIKE_THROUGH:		// where to place? offset = -line_height * 0.5f; break;
		default:						return;
	}

	line_vertices.resize(line_vertices.size() + 4);
	line_indices.resize(line_indices.size() + 6);
	GeometryUtilities::GenerateQuad(
		&line_vertices[0] + ((int)line_vertices.size() - 4),
		&line_indices[0] + ((int)line_indices.size() - 6),
		Vector2f(position.x, position.y + offset), 
		Vector2f((float) width, underline_thickness), 
		colour,
		(int)line_vertices.size() - 4
	);
}

// Destroys the handle.
void FontFaceHandle::OnReferenceDeactivate()
{
	delete this;
}

void FontFaceHandle::GenerateMetrics(BitmapFontDefinitions *bm_face)
{
	line_height = bm_face->CommonCharactersInfo.LineHeight;
	baseline = bm_face->CommonCharactersInfo.BaseLine;

	underline_position = (float)line_height - bm_face->CommonCharactersInfo.BaseLine;
	baseline += int( underline_position / 1.6f );
	underline_thickness = 1.0f;

	average_advance = 0;
	for (FontGlyphList::iterator i = glyphs.begin(); i != glyphs.end(); ++i)
		average_advance += i->advance;

	// Bring the total advance down to the average advance, but scaled up 10%, just to be on the safe side.
	average_advance = Math::RealToInteger((float) average_advance / (glyphs.size() * 0.9f));

	// Determine the x-height of this font face.
	word x = (word) 'x';
	int index = bm_face->BM_Helper_GetCharacterTableIndex( x );// FT_Get_Char_Index(ft_face, x);

	if ( index >= 0)
		x_height = bm_face->CharactersInfo[ index ].Height;
	else
		x_height = 0;
}

void FontFaceHandle::BuildGlyphMap(BitmapFontDefinitions *bm_face, const UnicodeRange& unicode_range)
{
	glyphs.resize(unicode_range.max_codepoint + 1);

	for (word character_code = (word) (Math::Max< unsigned int >(unicode_range.min_codepoint, 32)); character_code <= unicode_range.max_codepoint; ++character_code)
	{
		int index = bm_face->BM_Helper_GetCharacterTableIndex( character_code );

		if ( index < 0 )
		{
			continue;
		}

		FontGlyph glyph;
		glyph.character = character_code;
		BuildGlyph(glyph, &bm_face->CharactersInfo[ index ] );
		glyphs[character_code] = glyph;
	}
}

void Rml::Core::BitmapFont::FontFaceHandle::BuildGlyph(FontGlyph& glyph, CharacterInfo *bm_glyph)
{
	// Set the glyph's dimensions.
	glyph.dimensions.x = bm_glyph->Width;
	glyph.dimensions.y = bm_glyph->Height;

	// Set the glyph's bearing.
	glyph.bearing.x = bm_glyph->XOffset;
	glyph.bearing.y = bm_glyph->YOffset;

	// Set the glyph's advance.
	glyph.advance = bm_glyph->Advance;

	// Set the glyph's bitmap position.
	glyph.bitmap_dimensions.x = bm_glyph->X;
	glyph.bitmap_dimensions.y = bm_glyph->Y;

	glyph.bitmap_data = NULL;
}

int Rml::Core::BitmapFont::FontFaceHandle::GetKerning(word lhs, word rhs) const
{
	if( bm_face != NULL)
	{
		return bm_face->BM_Helper_GetXKerning(lhs, rhs);
	}

	return 0;
}

}
}
}
