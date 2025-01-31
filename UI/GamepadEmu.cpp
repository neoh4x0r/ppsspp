// Copyright (c) 2012- PPSSPP Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0 or later versions.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official git repository and contact information can be found at
// https://github.com/hrydgard/ppsspp and http://www.ppsspp.org/.

#include <algorithm>
#include "Common/Log.h"
#include "Core/Config.h"
#include "Core/System.h"
#include "Core/HLE/sceCtrl.h"
#include "UI/GamepadEmu.h"
#include "UI/ui_atlas.h"
#include "base/colorutil.h"
#include "base/display.h"
#include "base/NativeApp.h"
#include "base/timeutil.h"
#include "math/math_util.h"
#include "ui/ui_context.h"

static u32 GetButtonColor() {
	return g_Config.iTouchButtonStyle == 1 ? 0xFFFFFF : 0xc0b080;
}

GamepadView::GamepadView(UI::LayoutParams *layoutParams) : UI::View(layoutParams), secondsWithoutTouch_(0) {
	lastFrameTime_ = time_now_d();
}

void GamepadView::Touch(const TouchInput &input) {
	secondsWithoutTouch_ = 0.0f;
}

void GamepadView::Update() {
	const float now = time_now();
	float delta = now - lastFrameTime_;
	if (delta > 0) {
		secondsWithoutTouch_ += delta;
	}
	lastFrameTime_ = now;
}

float GamepadView::GetButtonOpacity() {
	float fadeAfterSeconds = g_Config.iTouchButtonHideSeconds;
	float fadeTransitionSeconds = std::min(fadeAfterSeconds, 0.5f);
	float opacity = g_Config.iTouchButtonOpacity / 100.0f;

	float multiplier = 1.0f;
	if (secondsWithoutTouch_ >= fadeAfterSeconds && fadeAfterSeconds > 0.0f) {
		if (secondsWithoutTouch_ >= fadeAfterSeconds + fadeTransitionSeconds) {
			multiplier = 0.0f;
		} else {
			float secondsIntoFade = secondsWithoutTouch_ - fadeAfterSeconds;
			multiplier = 1.0f - (secondsIntoFade / fadeTransitionSeconds);
		}
	}

	return opacity * multiplier;
}

void MultiTouchButton::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	const AtlasImage &image = dc.Draw()->GetAtlas()->images[bgImg_];
	w = image.w * scale_;
	h = image.h * scale_;
}

void MultiTouchButton::Touch(const TouchInput &input) {
	GamepadView::Touch(input);
	if ((input.flags & TOUCH_DOWN) && bounds_.Contains(input.x, input.y)) {
		pointerDownMask_ |= 1 << input.id;
	}
	if (input.flags & TOUCH_MOVE) {
		if (bounds_.Contains(input.x, input.y))
			pointerDownMask_ |= 1 << input.id;
		else
			pointerDownMask_ &= ~(1 << input.id);
	}
	if (input.flags & TOUCH_UP) {
		pointerDownMask_ &= ~(1 << input.id);
	}
	if (input.flags & TOUCH_RELEASE_ALL) {
		pointerDownMask_ = 0;
	}
}

void MultiTouchButton::Draw(UIContext &dc) {
	float opacity = GetButtonOpacity();
	if (opacity <= 0.0f)
		return;

	float scale = scale_;
	if (IsDown()) {
		scale *= 2.0f;
		opacity *= 1.15f;
	}

	uint32_t colorBg = colorAlpha(GetButtonColor(), opacity);
	uint32_t color = colorAlpha(0xFFFFFF, opacity);

	dc.Draw()->DrawImageRotated(bgImg_, bounds_.centerX(), bounds_.centerY(), scale, angle_ * (M_PI * 2 / 360.0f), colorBg, flipImageH_);

	int y = bounds_.centerY();
	// Hack round the fact that the center of the rectangular picture the triangle is contained in
	// is not at the "weight center" of the triangle.
	if (img_ == I_TRIANGLE)
		y -= 2.8f * scale;
	dc.Draw()->DrawImageRotated(img_, bounds_.centerX(), y, scale, angle_ * (M_PI * 2 / 360.0f), color);
}

void BoolButton::Touch(const TouchInput &input) {
	bool lastDown = pointerDownMask_ != 0;
	MultiTouchButton::Touch(input);
	bool down = pointerDownMask_ != 0;

	if (down != lastDown) {
		*value_ = down;
	}
}

void PSPButton::Touch(const TouchInput &input) {
	bool lastDown = pointerDownMask_ != 0;
	MultiTouchButton::Touch(input);
	bool down = pointerDownMask_ != 0;
	if (down && !lastDown) {
		if (g_Config.bHapticFeedback) {
			Vibrate(HAPTIC_VIRTUAL_KEY);
		}
		__CtrlButtonDown(pspButtonBit_);
	} else if (lastDown && !down) {
		__CtrlButtonUp(pspButtonBit_);
	}
}

void ComboKey::Touch(const TouchInput &input) {
	bool lastDown = pointerDownMask_ != 0;
	MultiTouchButton::Touch(input);
	bool down = pointerDownMask_ != 0;
	static const int combo[16] = {CTRL_SQUARE ,CTRL_TRIANGLE ,CTRL_CIRCLE ,CTRL_CROSS ,CTRL_UP ,CTRL_DOWN ,CTRL_LEFT ,CTRL_RIGHT ,CTRL_START ,CTRL_SELECT ,CTRL_LTRIGGER ,CTRL_RTRIGGER };
	if (down || lastDown) {
		for (int i = 0; i < 16; i++) {
			if (pspButtonBit_ & combo[i])
			{
				if (down && !lastDown) {
					if (g_Config.bHapticFeedback) {
						Vibrate(HAPTIC_VIRTUAL_KEY);
					}
					__CtrlButtonDown(combo[i]);
				}
				else if (lastDown && !down) {
					__CtrlButtonUp(combo[i]);
				}
			}
		}
	}

}

bool PSPButton::IsDown() {
	return (__CtrlPeekButtons() & pspButtonBit_) != 0;
}

PSPDpad::PSPDpad(int arrowIndex, int overlayIndex, float scale, float spacing, UI::LayoutParams *layoutParams)
	: GamepadView(layoutParams), arrowIndex_(arrowIndex), overlayIndex_(overlayIndex),
		scale_(scale), spacing_(spacing), dragPointerId_(-1), down_(0) {
}

void PSPDpad::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	w = D_pad_Radius * spacing_ * 4;
	h = D_pad_Radius * spacing_ * 4;
}

void PSPDpad::Touch(const TouchInput &input) {
	int lastDown = down_;
	GamepadView::Touch(input);

	if (input.flags & TOUCH_DOWN) {
		if (dragPointerId_ == -1 && bounds_.Contains(input.x, input.y)) {
			dragPointerId_ = input.id;
			ProcessTouch(input.x, input.y, true);
		}
	}
	if (input.flags & TOUCH_MOVE) {
		if (input.id == dragPointerId_) {
			ProcessTouch(input.x, input.y, true);
		}
	}
	if (input.flags & TOUCH_UP) {
		if (input.id == dragPointerId_) {
			dragPointerId_ = -1;
			ProcessTouch(input.x, input.y, false);
		}
	}
}

void PSPDpad::ProcessTouch(float x, float y, bool down) {
	float stick_size = spacing_ * D_pad_Radius * scale_;
	float inv_stick_size = 1.0f / stick_size;
	const float deadzone = 0.17f;

	float dx = (x - bounds_.centerX()) * inv_stick_size;
	float dy = (y - bounds_.centerY()) * inv_stick_size;
	float rad = sqrtf(dx*dx + dy*dy);
	if (rad < deadzone || rad > 2.0f)
		down = false;

	int ctrlMask = 0;
	int lastDown = down_;

	bool fourWay = g_Config.bDisableDpadDiagonals || rad < 0.7f;
	if (down) {
		if (fourWay) {
			int direction = (int)(floorf((atan2f(dy, dx) / (2 * M_PI) * 4) + 0.5f)) & 3;
			switch (direction) {
			case 0: ctrlMask |= CTRL_RIGHT; break;
			case 1: ctrlMask |= CTRL_DOWN; break;
			case 2: ctrlMask |= CTRL_LEFT; break;
			case 3: ctrlMask |= CTRL_UP; break;
			}
			// 4 way pad
		} else {
			// 8 way pad
			int direction = (int)(floorf((atan2f(dy, dx) / (2 * M_PI) * 8) + 0.5f)) & 7;
			switch (direction) {
			case 0: ctrlMask |= CTRL_RIGHT; break;
			case 1: ctrlMask |= CTRL_RIGHT | CTRL_DOWN; break;
			case 2: ctrlMask |= CTRL_DOWN; break;
			case 3: ctrlMask |= CTRL_DOWN | CTRL_LEFT; break;
			case 4: ctrlMask |= CTRL_LEFT; break;
			case 5: ctrlMask |= CTRL_UP | CTRL_LEFT; break;
			case 6: ctrlMask |= CTRL_UP; break;
			case 7: ctrlMask |= CTRL_UP | CTRL_RIGHT; break;
			}
		}
	}

	down_ = ctrlMask;
	int pressed = down_ & ~lastDown;
	int released = (~down_) & lastDown;
	static const int dir[4] = {CTRL_RIGHT, CTRL_DOWN, CTRL_LEFT, CTRL_UP};
	for (int i = 0; i < 4; i++) {
		if (pressed & dir[i]) {
			if (g_Config.bHapticFeedback) {
				Vibrate(HAPTIC_VIRTUAL_KEY);
			}
			__CtrlButtonDown(dir[i]);
		}
		if (released & dir[i]) {
			__CtrlButtonUp(dir[i]);
		}
	}
}

void PSPDpad::Draw(UIContext &dc) {
	float opacity = GetButtonOpacity();
	if (opacity <= 0.0f)
		return;

	uint32_t colorBg = colorAlpha(GetButtonColor(), opacity);
	uint32_t color = colorAlpha(0xFFFFFF, opacity);

	static const float xoff[4] = {1, 0, -1, 0};
	static const float yoff[4] = {0, 1, 0, -1};
	static const int dir[4] = {CTRL_RIGHT, CTRL_DOWN, CTRL_LEFT, CTRL_UP};
	int buttons = __CtrlPeekButtons();
	float r = D_pad_Radius * spacing_;
	for (int i = 0; i < 4; i++) {
		float x = bounds_.centerX() + xoff[i] * r;
		float y = bounds_.centerY() + yoff[i] * r;
		float x2 = bounds_.centerX() + xoff[i] * (r + 10.f * scale_);
		float y2 = bounds_.centerY() + yoff[i] * (r + 10.f * scale_);
		float angle = i * M_PI / 2;
		float imgScale = (buttons & dir[i]) ? scale_ * 2 : scale_;
		dc.Draw()->DrawImageRotated(arrowIndex_, x, y, imgScale, angle + PI, colorBg, false);
		if (overlayIndex_ != -1)
			dc.Draw()->DrawImageRotated(overlayIndex_, x2, y2, imgScale, angle + PI, color);
	}
}

PSPStick::PSPStick(int bgImg, int stickImg, int stick, float scale, UI::LayoutParams *layoutParams)
	: GamepadView(layoutParams), dragPointerId_(-1), bgImg_(bgImg), stickImageIndex_(stickImg), stick_(stick), scale_(scale), centerX_(-1), centerY_(-1) {
	stick_size_ = 50;
}

void PSPStick::GetContentDimensions(const UIContext &dc, float &w, float &h) const {
	const AtlasImage &image = dc.Draw()->GetAtlas()->images[bgImg_];
	w = image.w;
	h = image.h;
}

void PSPStick::Draw(UIContext &dc) {
	float opacity = GetButtonOpacity();
	if (opacity <= 0.0f)
		return;

	uint32_t colorBg = colorAlpha(GetButtonColor(), opacity);
	uint32_t color = colorAlpha(0x808080, opacity);

	if (centerX_ < 0.0f) {
		centerX_ = bounds_.centerX();
		centerY_ = bounds_.centerY();
	}

	float stickX = centerX_;
	float stickY = centerY_;

	float dx, dy;
	__CtrlPeekAnalog(stick_, &dx, &dy);

	dc.Draw()->DrawImage(bgImg_, stickX, stickY, 1.0f * scale_, colorBg, ALIGN_CENTER);
	dc.Draw()->DrawImage(stickImageIndex_, stickX + dx * stick_size_ * scale_, stickY - dy * stick_size_ * scale_, 1.0f * scale_, colorBg, ALIGN_CENTER);
}

void PSPStick::Touch(const TouchInput &input) {
	GamepadView::Touch(input);
	if (input.flags & TOUCH_RELEASE_ALL) {
		dragPointerId_ = -1;
		centerX_ = bounds_.centerX();
		centerY_ = bounds_.centerY();
		__CtrlSetAnalogX(0.0f, stick_);
		__CtrlSetAnalogY(0.0f, stick_);
		return;
	}
	if (input.flags & TOUCH_DOWN) {
		if (dragPointerId_ == -1 && bounds_.Contains(input.x, input.y)) {
			if (g_Config.bAutoCenterTouchAnalog) {
				centerX_ = input.x;
				centerY_ = input.y;
			} else {
				centerX_ = bounds_.centerX();
				centerY_ = bounds_.centerY();
			}
			dragPointerId_ = input.id;
			ProcessTouch(input.x, input.y, true);
		}
	}
	if (input.flags & TOUCH_MOVE) {
		if (input.id == dragPointerId_) {
			ProcessTouch(input.x, input.y, true);
		}
	}
	if (input.flags & TOUCH_UP) {
		if (input.id == dragPointerId_) {
			dragPointerId_ = -1;
			centerX_ = bounds_.centerX();
			centerY_ = bounds_.centerY();
			ProcessTouch(input.x, input.y, false);
		}
	}
}

void PSPStick::ProcessTouch(float x, float y, bool down) {
	if (down && centerX_ >= 0.0f) {
		float inv_stick_size = 1.0f / (stick_size_ * scale_);

		float dx = (x - centerX_) * inv_stick_size;
		float dy = (y - centerY_) * inv_stick_size;
		// Do not clamp to a circle! The PSP has nearly square range!

		// Old code to clamp to a circle
		// float len = sqrtf(dx * dx + dy * dy);
		// if (len > 1.0f) {
		//	dx /= len;
		//	dy /= len;
		//}

		// Still need to clamp to a square
		dx = std::min(1.0f, std::max(-1.0f, dx));
		dy = std::min(1.0f, std::max(-1.0f, dy));

		__CtrlSetAnalogX(dx, stick_);
		__CtrlSetAnalogY(-dy, stick_);
	} else {
		__CtrlSetAnalogX(0.0f, stick_);
		__CtrlSetAnalogY(0.0f, stick_);
	}
}

void InitPadLayout(float xres, float yres, float globalScale) {
	const float scale = globalScale;

	// PSP buttons (triangle, circle, square, cross)---------------------
	// space between the PSP buttons (triangle, circle, square and cross)
	if (g_Config.fActionButtonSpacing < 0) {
		g_Config.fActionButtonSpacing = 1.0f;
	}

	// Position of the circle button (the PSP circle button). It is the farthest to the left
	float Action_button_spacing = g_Config.fActionButtonSpacing * baseActionButtonSpacing;
	int Action_button_center_X = xres - Action_button_spacing * 2;
	int Action_button_center_Y = yres - Action_button_spacing * 2;

	if (g_Config.fActionButtonCenterX == -1.0 || g_Config.fActionButtonCenterY == -1.0) {
		// Setup defaults
		g_Config.fActionButtonCenterX = (float)Action_button_center_X / xres;
		g_Config.fActionButtonCenterY = (float)Action_button_center_Y / yres;
	}

	//D-PAD (up down left right) (aka PSP cross)----------------------------
	//radius to the D-pad
	// TODO: Make configurable

	int D_pad_X = 2.5 * D_pad_Radius * scale;
	int D_pad_Y = yres - D_pad_Radius * scale;
	if (g_Config.bShowTouchAnalogStick) {
		D_pad_Y -= 200 * scale;
	}

	if (g_Config.fDpadX == -1.0 || g_Config.fDpadY == -1.0 ) {
		//setup defaults
		g_Config.fDpadX = (float)D_pad_X / xres;
		g_Config.fDpadY = (float)D_pad_Y / yres;
	}

	//analog stick-------------------------------------------------------
	//keep the analog stick right below the D pad
	int analog_stick_X = D_pad_X;
	int analog_stick_Y = yres - 80 * scale;

	if (g_Config.fAnalogStickX == -1.0 || g_Config.fAnalogStickY == -1.0 ) {
		g_Config.fAnalogStickX = (float)analog_stick_X / xres;
		g_Config.fAnalogStickY = (float)analog_stick_Y / yres;
		g_Config.fAnalogStickScale = scale;
	}

	//select, start, throttle--------------------------------------------
	//space between the bottom keys (space between select, start and un-throttle)
	float bottom_key_spacing = 100;
	if (dp_xres < 750) {
		bottom_key_spacing *= 0.8f;
	}

	int start_key_X = xres / 2 + (bottom_key_spacing) * scale;
	int start_key_Y = yres - 60 * scale;

	if (g_Config.fStartKeyX == -1.0 || g_Config.fStartKeyY == -1.0 ) {
		g_Config.fStartKeyX = (float)start_key_X / xres;
		g_Config.fStartKeyY = (float)start_key_Y / yres;
		g_Config.fStartKeyScale = scale;
	}

	int select_key_X = xres / 2;
	int select_key_Y = yres - 60 * scale;

	if (g_Config.fSelectKeyX == -1.0 || g_Config.fSelectKeyY == -1.0 ) {
		g_Config.fSelectKeyX = (float)select_key_X / xres;
		g_Config.fSelectKeyY = (float)select_key_Y / yres;
		g_Config.fSelectKeyScale = scale;
	}

	int unthrottle_key_X = xres / 2 - (bottom_key_spacing) * scale;
	int unthrottle_key_Y = yres - 60 * scale;

	if (g_Config.fUnthrottleKeyX == -1.0 || g_Config.fUnthrottleKeyY == -1.0 ) {
		g_Config.fUnthrottleKeyX = (float)unthrottle_key_X / xres;
		g_Config.fUnthrottleKeyY = (float)unthrottle_key_Y / yres;
		g_Config.fUnthrottleKeyScale = scale;
	}

	// L and R------------------------------------------------------------
	// Put them above the analog stick / above the buttons to the right.
	// The corners were very hard to reach..

	int l_key_X = 60 * scale;
	int l_key_Y = yres - 380 * scale;

	if (g_Config.fLKeyX == -1.0 || g_Config.fLKeyY == -1.0 ) {
		g_Config.fLKeyX = (float)l_key_X / xres;
		g_Config.fLKeyY = (float)l_key_Y / yres;
		g_Config.fLKeyScale = scale;
	}

	int r_key_X = xres - 60 * scale;
	int r_key_Y = l_key_Y;

	if (g_Config.fRKeyX == -1.0 || g_Config.fRKeyY == -1.0 ) {
		g_Config.fRKeyX = (float)r_key_X / xres;
		g_Config.fRKeyY = (float)r_key_Y / yres;
		g_Config.fRKeyScale = scale;
	}

	//Combo key
	int combo_key_X = xres / 2 + (bottom_key_spacing)* scale*1.2f;
	int combo_key_Y = yres / 2;

	if (g_Config.fcombo0X == -1.0 || g_Config.fcombo0Y == -1.0) {
		g_Config.fcombo0X = (float)combo_key_X / xres;
		g_Config.fcombo0Y = (float)combo_key_Y / yres;
		g_Config.fcomboScale0 = scale;
	}

	int combo1_key_X = xres / 2 + (bottom_key_spacing)* scale * 2.2;
	int combo1_key_Y = yres / 2;

	if (g_Config.fcombo1X == -1.0 || g_Config.fcombo1Y == -1.0) {
		g_Config.fcombo1X = (float)combo1_key_X / xres;
		g_Config.fcombo1Y = (float)combo1_key_Y / yres;
		g_Config.fcomboScale1 = scale;
	}

	int combo2_key_X = xres / 2 + (bottom_key_spacing)* scale * 3.2;
	int combo2_key_Y = yres / 2;

	if (g_Config.fcombo2X == -1.0 || g_Config.fcombo2Y == -1.0) {
		g_Config.fcombo2X = (float)combo2_key_X / xres;
		g_Config.fcombo2Y = (float)combo2_key_Y / yres;
		g_Config.fcomboScale2 = scale;
	}

	int combo3_key_X = xres / 2 + (bottom_key_spacing)* scale * 1.2;
	int combo3_key_Y = yres / 3;

	if (g_Config.fcombo3X == -1.0 || g_Config.fcombo3Y == -1.0) {
		g_Config.fcombo3X = (float)combo3_key_X / xres;
		g_Config.fcombo3Y = (float)combo3_key_Y / yres;
		g_Config.fcomboScale3 = scale;
	}

	int combo4_key_X = xres / 2 + (bottom_key_spacing)* scale * 2.2;
	int combo4_key_Y = yres / 3;

	if (g_Config.fcombo4X == -1.0 || g_Config.fcombo4Y == -1.0) {
		g_Config.fcombo4X = (float)combo4_key_X / xres;
		g_Config.fcombo4Y = (float)combo4_key_Y / yres;
		g_Config.fcomboScale4 = scale;
	}

};

UI::ViewGroup *CreatePadLayout(float xres, float yres, bool *pause) {
	//standard coord system

	using namespace UI;

	AnchorLayout *root = new AnchorLayout(new LayoutParams(FILL_PARENT, FILL_PARENT));

	//PSP buttons (triangle, circle, square, cross)---------------------
	//space between the PSP buttons (traingle, circle, square and cross)
	const float Action_button_scale = g_Config.fActionButtonScale;
	const float Action_button_spacing = g_Config.fActionButtonSpacing * baseActionButtonSpacing;
	//position of the circle button (the PSP circle button). It is the farthest to the left
	float Action_button_center_X = g_Config.fActionButtonCenterX * xres;
	float Action_button_center_Y = g_Config.fActionButtonCenterY * yres;

	const float Action_circle_button_X = Action_button_center_X + Action_button_spacing;
	const float Action_circle_button_Y = Action_button_center_Y;

	const float Action_cross_button_X = Action_button_center_X;
	const float Action_cross_button_Y =  Action_button_center_Y + Action_button_spacing;

	const float Action_triangle_button_X = Action_button_center_X;
	const float Action_triangle_button_Y = Action_button_center_Y - Action_button_spacing;

	const float Action_square_button_X = Action_button_center_X - Action_button_spacing;
	const float Action_square_button_Y = Action_button_center_Y;

	//D-PAD (up down left right) (aka PSP cross)--------------------------------------------------------------
	//radius to the D-pad

	float D_pad_X = g_Config.fDpadX * xres;
	float D_pad_Y = g_Config.fDpadY * yres;
	float D_pad_scale = g_Config.fDpadScale;
	float D_pad_spacing = g_Config.fDpadSpacing;

	//select, start, throttle--------------------------------------------
	//space between the bottom keys (space between select, start and un-throttle)
	float start_key_X = g_Config.fStartKeyX * xres;
	float start_key_Y = g_Config.fStartKeyY * yres;
	float start_key_scale = g_Config.fStartKeyScale;

	float select_key_X = g_Config.fSelectKeyX * xres;
	float select_key_Y = g_Config.fSelectKeyY * yres;
	float select_key_scale = g_Config.fSelectKeyScale;

	float unthrottle_key_X = g_Config.fUnthrottleKeyX * xres;
	float unthrottle_key_Y = g_Config.fUnthrottleKeyY * yres;
	float unthrottle_key_scale = g_Config.fUnthrottleKeyScale;

	//L and R------------------------------------------------------------
	float l_key_X = g_Config.fLKeyX * xres;
	float l_key_Y = g_Config.fLKeyY * yres;
	float l_key_scale = g_Config.fLKeyScale;

	float r_key_X = g_Config.fRKeyX * xres;
	float r_key_Y = g_Config.fRKeyY * yres;
	float r_key_scale = g_Config.fRKeyScale;

	//analog stick-------------------------------------------------------
	float analog_stick_X = g_Config.fAnalogStickX * xres;
	float analog_stick_Y = g_Config.fAnalogStickY * yres;
	float analog_stick_scale = g_Config.fAnalogStickScale;

	//combo key -------------------------------------------------------
	float combo0_key_X = g_Config.fcombo0X * xres;
	float combo0_key_Y = g_Config.fcombo0Y * yres;
	float combo_key_scale = g_Config.fcomboScale0;
	float combo1_key_X = g_Config.fcombo1X * xres;
	float combo1_key_Y = g_Config.fcombo1Y * yres;
	float combo1_key_scale = g_Config.fcomboScale1;
	float combo2_key_X = g_Config.fcombo2X * xres;
	float combo2_key_Y = g_Config.fcombo2Y * yres;
	float combo2_key_scale = g_Config.fcomboScale2;
	float combo3_key_X = g_Config.fcombo3X * xres;
	float combo3_key_Y = g_Config.fcombo3Y * yres;
	float combo3_key_scale = g_Config.fcomboScale3;
	float combo4_key_X = g_Config.fcombo4X * xres;
	float combo4_key_Y = g_Config.fcombo4Y * yres;
	float combo4_key_scale = g_Config.fcomboScale4;

	const int halfW = xres / 2;

	const int roundImage = g_Config.iTouchButtonStyle ? I_ROUND_LINE : I_ROUND;

	// These platforms always need the pause menu button to be shown.
#if defined(IOS)
	root->Add(new BoolButton(pause, roundImage, I_ARROW, 1.0f, new AnchorLayoutParams(halfW, 20, NONE, NONE, true)))->SetAngle(90);
#endif

	if (g_Config.bShowTouchControls) {
		const int rectImage = g_Config.iTouchButtonStyle ? I_RECT_LINE : I_RECT;
		const int shoulderImage = g_Config.iTouchButtonStyle ? I_SHOULDER_LINE : I_SHOULDER;
		const int dirImage = g_Config.iTouchButtonStyle ? I_DIR_LINE : I_DIR;
		const int stickImage = g_Config.iTouchButtonStyle ? I_STICK_LINE : I_STICK;
		const int stickBg = g_Config.iTouchButtonStyle ? I_STICK_BG_LINE : I_STICK_BG;
		static const int comboKeyImages[5] = { I_1, I_2, I_3, I_4, I_5 };

#if !defined(IOS)
		if (g_Config.bShowTouchPause)
			root->Add(new BoolButton(pause, roundImage, I_ARROW, 1.0f, new AnchorLayoutParams(halfW, 20, NONE, NONE, true)))->SetAngle(90);
#endif
		if (g_Config.bShowTouchCircle)
			root->Add(new PSPButton(CTRL_CIRCLE, roundImage, I_CIRCLE, Action_button_scale, new AnchorLayoutParams(Action_circle_button_X, Action_circle_button_Y, NONE, NONE, true)));

		if (g_Config.bShowTouchCross)
			root->Add(new PSPButton(CTRL_CROSS, roundImage, I_CROSS, Action_button_scale, new AnchorLayoutParams(Action_cross_button_X, Action_cross_button_Y, NONE, NONE, true)));

		if (g_Config.bShowTouchTriangle)
			root->Add(new PSPButton(CTRL_TRIANGLE, roundImage, I_TRIANGLE, Action_button_scale, new AnchorLayoutParams(Action_triangle_button_X, Action_triangle_button_Y, NONE, NONE, true)));

		if (g_Config.bShowTouchSquare)
			root->Add(new PSPButton(CTRL_SQUARE, roundImage, I_SQUARE, Action_button_scale, new AnchorLayoutParams(Action_square_button_X, Action_square_button_Y, NONE, NONE, true)));

		if (g_Config.bShowTouchStart)
			root->Add(new PSPButton(CTRL_START, rectImage, I_START, start_key_scale, new AnchorLayoutParams(start_key_X, start_key_Y, NONE, NONE, true)));

		if (g_Config.bShowTouchSelect)
			root->Add(new PSPButton(CTRL_SELECT, rectImage, I_SELECT, select_key_scale, new AnchorLayoutParams(select_key_X, select_key_Y, NONE, NONE, true)));

		if (g_Config.bShowTouchUnthrottle)
			root->Add(new BoolButton(&PSP_CoreParameter().unthrottle, rectImage, I_ARROW, unthrottle_key_scale, new AnchorLayoutParams(unthrottle_key_X, unthrottle_key_Y, NONE, NONE, true)))->SetAngle(180);

		if (g_Config.bShowTouchLTrigger)
			root->Add(new PSPButton(CTRL_LTRIGGER, shoulderImage, I_L, l_key_scale, new AnchorLayoutParams(l_key_X, l_key_Y, NONE, NONE, true)));

		if (g_Config.bShowTouchRTrigger)
			root->Add(new PSPButton(CTRL_RTRIGGER, shoulderImage, I_R, r_key_scale, new AnchorLayoutParams(r_key_X,r_key_Y, NONE, NONE, true)))->FlipImageH(true);

		if (g_Config.bShowTouchDpad)
			root->Add(new PSPDpad(dirImage, I_ARROW, D_pad_scale, D_pad_spacing, new AnchorLayoutParams(D_pad_X, D_pad_Y, NONE, NONE, true)));

		if (g_Config.bShowTouchAnalogStick)
			root->Add(new PSPStick(stickBg, stickImage, 0, analog_stick_scale, new AnchorLayoutParams(analog_stick_X, analog_stick_Y, NONE, NONE, true)));

		if (g_Config.bShowComboKey0)
			root->Add(new ComboKey(g_Config.iCombokey0, roundImage, comboKeyImages[0], combo_key_scale, new AnchorLayoutParams(combo0_key_X, combo0_key_Y, NONE, NONE, true)));

		if (g_Config.bShowComboKey1)
			root->Add(new ComboKey(g_Config.iCombokey1, roundImage, comboKeyImages[1], combo1_key_scale, new AnchorLayoutParams(combo1_key_X, combo1_key_Y, NONE, NONE, true)));

		if (g_Config.bShowComboKey2)
			root->Add(new ComboKey(g_Config.iCombokey2, roundImage, comboKeyImages[2], combo2_key_scale, new AnchorLayoutParams(combo2_key_X, combo2_key_Y, NONE, NONE, true)));

		if (g_Config.bShowComboKey3)
			root->Add(new ComboKey(g_Config.iCombokey3, roundImage, comboKeyImages[3], combo3_key_scale, new AnchorLayoutParams(combo3_key_X, combo3_key_Y, NONE, NONE, true)));

		if (g_Config.bShowComboKey4)
			root->Add(new ComboKey(g_Config.iCombokey4, roundImage, comboKeyImages[4], combo4_key_scale, new AnchorLayoutParams(combo4_key_X, combo4_key_Y, NONE, NONE, true)));
	}

	return root;
}
