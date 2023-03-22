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

#include "ScrollController.h"
#include "../../Include/RmlUi/Core/ComputedValues.h"
#include "../../Include/RmlUi/Core/Core.h"
#include "../../Include/RmlUi/Core/Element.h"
#include "../../Include/RmlUi/Core/SystemInterface.h"

namespace Rml {

static constexpr float AUTOSCROLL_SPEED_FACTOR = 0.09f;
static constexpr float AUTOSCROLL_DEADZONE = 10.0f; // [dp]

static constexpr float SMOOTHSCROLL_WINDOW_SIZE = 50.f;        // The window where smoothing is applied, as a distance from scroll start and end. [dp]
static constexpr float SMOOTHSCROLL_VELOCITY_CONSTANT = 800.f; // The constant velocity, any smoothing is applied on top of this. [dp/s]
static constexpr float SMOOTHSCROLL_VELOCITY_SQUARE_FACTOR = 0.05f;

// Determines the autoscroll velocity based on the distance from the scroll-start mouse position. [px/s]
static Vector2f CalculateAutoscrollVelocity(Vector2f target_delta, float dp_ratio)
{
	target_delta = target_delta / dp_ratio;
	target_delta = {
		Math::AbsoluteValue(target_delta.x) < AUTOSCROLL_DEADZONE ? 0.f : target_delta.x,
		Math::AbsoluteValue(target_delta.y) < AUTOSCROLL_DEADZONE ? 0.f : target_delta.y,
	};

	// We use a signed square model for the velocity, which seems to work quite well. This is mostly about feeling and tuning.
	return AUTOSCROLL_SPEED_FACTOR * target_delta * Math::AbsoluteValue(target_delta);
}

// Determines the smoothscroll velocity based on the distance to the target, and the distance scrolled so far. [px/s]
static Vector2f CalculateSmoothscrollVelocity(Vector2f target_delta, Vector2f scrolled_distance, float dp_ratio)
{
	scrolled_distance = Math::AbsoluteValue(scrolled_distance) / dp_ratio;
	target_delta = target_delta / dp_ratio;

	const Vector2f target_delta_abs = Math::AbsoluteValue(target_delta);
	Vector2f target_delta_signum = {
		target_delta.x > 0.f ? 1.f : (target_delta.x < 0.f ? -1.f : 0.f),
		target_delta.y > 0.f ? 1.f : (target_delta.y < 0.f ? -1.f : 0.f),
	};

	// The window provides velocity smoothing near the start and end of the scroll.
	const Tween tween(Tween::Exponential, Tween::Out);
	const Vector2f alpha_in = Math::Min(scrolled_distance / SMOOTHSCROLL_WINDOW_SIZE, Vector2f(1.f));
	const Vector2f alpha_out = Math::Min(target_delta_abs / SMOOTHSCROLL_WINDOW_SIZE, Vector2f(1.f));
	const Vector2f smooth_window = {
		tween(alpha_in.x) * tween(alpha_out.x),
		tween(alpha_in.y) * tween(alpha_out.y),
	};

	const Vector2f velocity_constant = Vector2f(SMOOTHSCROLL_VELOCITY_CONSTANT);
	const Vector2f velocity_square = SMOOTHSCROLL_VELOCITY_SQUARE_FACTOR * target_delta_abs * target_delta_abs;

	// Short scrolls are dominated by the smoothed constant velocity, while the square term is added for quick longer scrolls.
	return dp_ratio * target_delta_signum * (smooth_window * velocity_constant + velocity_square);
}

float ScrollController::UpdateTime()
{
	const double previous_tick = previous_update_time;
	previous_update_time = GetSystemInterface()->GetElapsedTime();

	const float dt = float(previous_update_time - previous_tick);
	// Clamp the delta time to some reasonable FPS range, to avoid large steps in case of stuttering or freezing.
	return Math::Clamp(dt, 1.f / 500.f, 1.f / 15.f);
}

void ScrollController::UpdateAutoscroll(Vector2i mouse_position, float dp_ratio)
{
	RMLUI_ASSERT(mode == Mode::Autoscroll && target);

	const float dt = UpdateTime();

	const Vector2f scroll_delta = Vector2f(mouse_position - autoscroll_start_position);
	const Vector2f scroll_velocity = CalculateAutoscrollVelocity(scroll_delta, dp_ratio);

	autoscroll_accumulated_length += scroll_velocity * dt;

	// Only submit the integer part of the scroll length, accumulate and store fractional parts to enable sub-pixel-per-frame scrolling speeds.
	Vector2f scroll_length_integral = autoscroll_accumulated_length;
	autoscroll_accumulated_length.x = Math::DecomposeFractionalIntegral(autoscroll_accumulated_length.x, &scroll_length_integral.x);
	autoscroll_accumulated_length.y = Math::DecomposeFractionalIntegral(autoscroll_accumulated_length.y, &scroll_length_integral.y);

	if (scroll_velocity != Vector2f(0.f))
		autoscroll_holding = true;

	if (scroll_length_integral != Vector2f(0.f))
	{
		Dictionary scroll_parameters;
		// GenerateMouseEventParameters(scroll_parameters); // TODO
		scroll_parameters["delta_x"] = scroll_length_integral.x;
		scroll_parameters["delta_y"] = scroll_length_integral.y;

		if (target->DispatchEvent(EventId::Mousescroll, scroll_parameters))
			Reset(); // Scroll event was not handled by any element, meaning that we don't have anything to scroll.
	}
}

void ScrollController::UpdateSmoothscroll(Vector2i /*mouse_position*/, float dp_ratio)
{
	RMLUI_ASSERT(mode == Mode::Smoothscroll && target);

	const Vector2f target_delta = Vector2f(smoothscroll_target_distance - smoothscroll_scrolled_distance);
	const Vector2f velocity = CalculateSmoothscrollVelocity(target_delta, smoothscroll_scrolled_distance, dp_ratio);

	const float dt = UpdateTime();
	Vector2f scroll_distance = (velocity * dt).Round();

	for (int i = 0; i < 2; i++)
	{
		// Ensure minimum scroll speed of 1px/frame, and clamp the distance to the target in case of overshooting
		// integration. As opposed to autoscroll, we don't care about fractional speeds here since we want to be fast.
		if (target_delta[i] > 0.f)
			scroll_distance[i] = Math::Min(Math::Max(scroll_distance[i], 1.f), target_delta[i]);
		else if (target_delta[i] < 0.f)
			scroll_distance[i] = Math::Max(Math::Min(scroll_distance[i], -1.f), target_delta[i]);
		else
			scroll_distance[i] = 0.f;
	}

	if (scroll_distance != Vector2f(0.f))
	{
		smoothscroll_scrolled_distance += scroll_distance;

		Dictionary scroll_parameters;
		// GenerateMouseEventParameters(scroll_parameters); // TODO
		scroll_parameters["delta_x"] = scroll_distance.x;
		scroll_parameters["delta_y"] = scroll_distance.y;

		if (target->DispatchEvent(EventId::Mousescroll, scroll_parameters))
			Reset(); // Scroll event was not handled by any element, meaning that we don't have anything to scroll.
	}

	if (scroll_distance == target_delta)
		Reset();
}

void ScrollController::ActivateAutoscroll(Element* in_target, Vector2i start_position)
{
	Reset();
	mode = Mode::Autoscroll;
	target = in_target;
	autoscroll_start_position = start_position;
	// TODO: Determine the element to scroll first. Only target that directly, don't do scroll event.
	UpdateTime();
}

bool ScrollController::ProcessMouseWheel(Vector2f wheel_delta, Element* hover, float dp_ratio)
{
	if (mode == Mode::Autoscroll)
	{
		Reset();
		return false;
	}
	else if (!hover)
	{
		Reset();
	}
	else
	{
		RMLUI_ASSERT(hover);

		if (mode != Mode::Smoothscroll)
			ActivateSmoothscroll(hover);

		auto OppositeDirection = [](float a, float b) { return (a < 0.f && b > 0.f) || (a > 0.f && b < 0.f); };

		// The scroll length for a single unit of wheel delta is defined as three default sized lines.
		const float default_scroll_length = 100.f * dp_ratio;

		Vector2f delta = smoothscroll_target_distance - smoothscroll_scrolled_distance;

		if (OppositeDirection(wheel_delta.x, delta.x))
		{
			smoothscroll_target_distance.x = 0.f;
			smoothscroll_scrolled_distance.x = 0.f;
		}
		if (OppositeDirection(wheel_delta.y, delta.y))
		{
			smoothscroll_target_distance.y = 0.f;
			smoothscroll_scrolled_distance.y = 0.f;
		}

		smoothscroll_target_distance += wheel_delta * default_scroll_length;

		// TODO test if we can scroll here first!

		return false;
	}

	return true;
}

String ScrollController::GetAutoscrollCursor(Vector2i mouse_position, float dp_ratio) const
{
	RMLUI_ASSERT(mode == Mode::Autoscroll);

	const Vector2f scroll_delta = Vector2f(mouse_position - autoscroll_start_position);
	const Vector2f scroll_velocity = CalculateAutoscrollVelocity(scroll_delta, dp_ratio);

	if (scroll_velocity == Vector2f(0.f))
		return "rmlui-scroll-idle";

	String result = "rmlui-scroll";

	if (scroll_velocity.y > 0.f)
		result += "-up";
	else if (scroll_velocity.y < 0.f)
		result += "-down";

	if (scroll_velocity.x > 0.f)
		result += "-right";
	else if (scroll_velocity.x < 0.f)
		result += "-left";

	return result;
}
} // namespace Rml
