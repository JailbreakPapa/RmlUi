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

#ifndef RMLUICORESTYLESHEET_H
#define RMLUICORESTYLESHEET_H

#include "ReferenceCountable.h"
#include "PropertyDictionary.h"
#include "Spritesheet.h"
#include <set>

namespace Rml {
namespace Core {

class Element;
class ElementDefinition;
class StyleSheetNode;
class Decorator;
class FontEffect;
class SpritesheetList;
struct Sprite;
struct Spritesheet;

struct KeyframeBlock {
	float normalized_time;  // [0, 1]
	PropertyDictionary properties;
};
struct Keyframes {
	std::vector<PropertyId> property_ids;
	std::vector<KeyframeBlock> blocks;
};
using KeyframesMap = UnorderedMap<String, Keyframes>;

struct DecoratorSpecification {
	String decorator_type;
	PropertyDictionary properties;
	std::shared_ptr<Decorator> decorator;
};
using DecoratorSpecificationMap = UnorderedMap<String, DecoratorSpecification>;

/**
	StyleSheet maintains a single stylesheet definition. A stylesheet can be combined with another stylesheet to create
	a new, merged stylesheet.

	@author Lloyd Weehuizen
 */

class RMLUICORE_API StyleSheet : public NonCopyMoveable
{
public:
	typedef std::unordered_set< StyleSheetNode* > NodeList;
	typedef UnorderedMap< String, NodeList > NodeIndex;

	StyleSheet();
	virtual ~StyleSheet();

	/// Loads a style from a CSS definition.
	bool LoadStyleSheet(Stream* stream);

	/// Combines this style sheet with another one, producing a new sheet.
	SharedPtr<StyleSheet> CombineStyleSheet(const StyleSheet& sheet) const;
	/// Builds the node index for a combined style sheet, and optimizes some properties for faster retrieval.
	/// Specifically, converts all decorator properties from strings to instanced decorator lists.
	void BuildNodeIndexAndOptimizeProperties();

	/// Returns the Keyframes of the given name, or null if it does not exist.
	Keyframes* GetKeyframes(const String& name);

	/// Returns the Decorator of the given name, or null if it does not exist.
	std::shared_ptr<Decorator> GetDecorator(const String& name) const;

	/// Parses the decorator property from a string and returns a list of instanced decorators.
	DecoratorList InstanceDecoratorsFromString(const String& decorator_string_value, const String& source_file, int source_line_number) const;

	/// Parses the font-effect property from a string and returns a list of instanced font-effects.
	FontEffectListPtr InstanceFontEffectsFromString(const String& font_effect_string_value, const String& source_file, int source_line_number) const;

	/// Get sprite located in any spritesheet within this stylesheet.
	const Sprite* GetSprite(const String& name) const;

	/// Returns the compiled element definition for a given element hierarchy. A reference count will be added for the
	/// caller, so another should not be added. The definition should be released by removing the reference count.
	std::shared_ptr<ElementDefinition> GetElementDefinition(const Element* element) const;

private:
	// Root level node, attributes from special nodes like "body" get added to this node
	std::unique_ptr<StyleSheetNode> root;

	// The maximum specificity offset used in this style sheet to distinguish between properties in
	// similarly-specific rules, but declared on different lines. When style sheets are merged, the
	// more-specific style sheet (ie, coming further 'down' the include path) adds the offset of
	// the less-specific style sheet onto its offset, thereby ensuring its properties take
	// precedence in the event of a conflict.
	int specificity_offset;

	// Name of every @keyframes mapped to their keys
	KeyframesMap keyframes;

	// Name of every @decorator mapped to their specification
	DecoratorSpecificationMap decorator_map;

	// Name of every @spritesheet and underlying sprites mapped to their values
	SpritesheetList spritesheet_list;

	// Map of only nodes with actual style information.
	NodeIndex styled_node_index;
	// Map of every node, even empty, un-styled, nodes.
	NodeIndex complete_node_index;

	typedef UnorderedMap< size_t, std::shared_ptr<ElementDefinition> > ElementDefinitionCache;
	// Index of node sets to element definitions.
	mutable ElementDefinitionCache node_cache;
};

}
}

#endif
