
// -----------------------------------------------------------------------------
// SLADE - It's a Doom Editor
// Copyright(C) 2008 - 2017 Simon Judd
//
// Email:       sirjuddington@gmail.com
// Web:         http://slade.mancubus.net
// Filename:    CTextureCanvas.cpp
// Description: An OpenGL canvas that displays a composite texture
//              (ie from doom's TEXTUREx)
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 2 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA  02110 - 1301, USA.
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
//
// Includes
//
// -----------------------------------------------------------------------------
#include "Main.h"
#include "CTextureCanvas.h"
#include "General/Misc.h"
#include "Graphics/CTexture/CTexture.h"
#include "Graphics/SImage/SImage.h"
#include "OpenGL/Drawing.h"


// -----------------------------------------------------------------------------
//
// Variables
//
// -----------------------------------------------------------------------------
wxDEFINE_EVENT(EVT_DRAG_END, wxCommandEvent);
CVAR(Bool, tx_arc, false, CVAR_SAVE)
EXTERN_CVAR(Bool, gfx_show_border)


// -----------------------------------------------------------------------------
//
// CTextureCanvas Class Functions
//
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// CTextureCanvas class constructor
// -----------------------------------------------------------------------------
CTextureCanvas::CTextureCanvas(wxWindow* parent, int id) : OGLCanvas(parent, id)
{
	// Init variables
	scale_         = 1.0;
	hilight_patch_ = -1;
	draw_outside_  = true;
	dragging_      = false;
	texture_       = nullptr;
	show_grid_     = false;
	blend_rgba_    = false;
	view_type_     = View::Normal;
	tex_scale_     = false;

	// Bind events
	Bind(wxEVT_MOTION, &CTextureCanvas::onMouseEvent, this);
	Bind(wxEVT_LEFT_UP, &CTextureCanvas::onMouseEvent, this);
	Bind(wxEVT_LEAVE_WINDOW, &CTextureCanvas::onMouseEvent, this);
}

// -----------------------------------------------------------------------------
// CTextureCanvas class destructor
// -----------------------------------------------------------------------------
CTextureCanvas::~CTextureCanvas()
{
	clearPatchTextures();
}

// -----------------------------------------------------------------------------
// Selects the patch at [index]
// -----------------------------------------------------------------------------
void CTextureCanvas::selectPatch(int index)
{
	// Check patch index is ok
	if (index < 0 || (unsigned)index >= texture_->nPatches())
		return;

	// Select the patch
	selected_patches_[index] = true;
}

// -----------------------------------------------------------------------------
// De-Selects the patch at [index]
// -----------------------------------------------------------------------------
void CTextureCanvas::deSelectPatch(int index)
{
	// Check patch index is ok
	if (index < 0 || (unsigned)index >= texture_->nPatches())
		return;

	// De-Select the patch
	selected_patches_[index] = false;
}

// -----------------------------------------------------------------------------
// Returns true if the patch at [index] is selected, false otherwise
// -----------------------------------------------------------------------------
bool CTextureCanvas::patchSelected(int index)
{
	// Check index is ok
	if (index < 0 || (unsigned)index >= texture_->nPatches())
		return false;

	// Return if patch index is selected
	return selected_patches_[index];
}

// -----------------------------------------------------------------------------
// Clears the current texture and the patch textures list
// -----------------------------------------------------------------------------
void CTextureCanvas::clearTexture()
{
	// Stop listening to the current texture (if it exists)
	if (texture_)
		stopListening(texture_);

	// Clear texture;
	texture_ = nullptr;

	// Clear patch textures
	clearPatchTextures();

	// Reset view offset
	resetOffsets();

	// Clear patch selection
	selected_patches_.clear();
	hilight_patch_ = -1;

	// Clear full preview
	tex_preview_.clear();

	// Refresh canvas
	Refresh();
}

// -----------------------------------------------------------------------------
// Clears the patch textures list
// -----------------------------------------------------------------------------
void CTextureCanvas::clearPatchTextures()
{
	for (size_t a = 0; a < patch_textures_.size(); a++)
		delete patch_textures_[a];
	patch_textures_.clear();

	// Refresh canvas
	Refresh();
}

// -----------------------------------------------------------------------------
// Unloads all patch textures, so they are reloaded on next draw
// -----------------------------------------------------------------------------
void CTextureCanvas::updatePatchTextures()
{
	// Unload single patch textures
	for (size_t a = 0; a < patch_textures_.size(); a++)
		patch_textures_[a]->clear();

	// Unload full preview
	tex_preview_.clear();
}

// -----------------------------------------------------------------------------
// Unloads the full preview texture, so it is reloaded on next draw
// -----------------------------------------------------------------------------
void CTextureCanvas::updateTexturePreview()
{
	// Unload full preview
	tex_preview_.clear();
}

// -----------------------------------------------------------------------------
// Loads a composite texture to be displayed
// -----------------------------------------------------------------------------
bool CTextureCanvas::openTexture(CTexture* tex, Archive* parent)
{
	// Clear the current texture
	clearTexture();

	// Set texture
	texture_      = tex;
	this->parent_ = parent;

	// Init patches
	clearPatchTextures();
	for (uint32_t a = 0; a < tex->nPatches(); a++)
	{
		// Create GL texture
		patch_textures_.push_back(new GLTexture());

		// Set selection
		selected_patches_.push_back(false);
	}

	// Listen to it
	listenTo(tex);

	// Redraw
	Refresh();

	return true;
}

// -----------------------------------------------------------------------------
// Draws the canvas contents
// -----------------------------------------------------------------------------
void CTextureCanvas::draw()
{
	// Setup the viewport
	glViewport(0, 0, GetSize().x, GetSize().y);

	// Setup the screen projection
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, GetSize().x, GetSize().y, 0, -1, 1);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	// Clear
	glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// Translate to inside of pixel (otherwise inaccuracies can occur on certain gl implementations)
	if (OpenGL::accuracyTweak())
		glTranslatef(0.375f, 0.375f, 0);

	// Draw background
	drawCheckeredBackground();

	// Pan by view offset
	glTranslated(offset_.x, offset_.y, 0);

	// Draw texture
	if (texture_)
		drawTexture();

	// Swap buffers (ie show what was drawn)
	SwapBuffers();
}

// -----------------------------------------------------------------------------
// Draws the currently opened composite texture
// -----------------------------------------------------------------------------
void CTextureCanvas::drawTexture()
{
	// Push matrix
	glPushMatrix();

	// Translate to middle of the canvas
	glTranslated(GetSize().x * 0.5, GetSize().y * 0.5, 0);

	// Zoom
	double yscale = (tx_arc ? scale_ * 1.2 : scale_);
	glScaled(scale_, yscale, 1);

	// Draw offset guides if needed
	drawOffsetLines();

	// Apply texture scale
	double tscalex = 1;
	double tscaley = 1;
	if (tex_scale_)
	{
		tscalex = texture_->scaleX();
		if (tscalex == 0)
			tscalex = 1;
		tscaley = texture_->scaleY();
		if (tscaley == 0)
			tscaley = 1;
		glScaled(1.0 / tscalex, 1.0 / tscaley, 1);
	}

	// Calculate top-left position of texture (for glScissor, since it ignores the current translation/scale)
	point2_t screen_tl = texToScreenPosition(0, 0);
	int      left      = screen_tl.x;
	int      top       = screen_tl.y;

	// Translate by offsets if needed
	if (view_type_ == View::Normal) // No offsets
		glTranslated(texture_->width() * -0.5, texture_->height() * -0.5, 0);
	if (view_type_ == View::Sprite || view_type_ == View::HUD) // Sprite offsets
		glTranslated(-texture_->offsetX(), -texture_->offsetY(), 0);
	if (view_type_ == View::HUD) // HUD offsets
		glTranslated(-160 * tscalex, -100 * tscaley, 0);

	// Draw the texture border
	drawTextureBorder();

	// Enable textures
	glEnable(GL_TEXTURE_2D);

	// First, draw patches semitransparently (for anything outside the texture)
	// But only if we are drawing stuff outside the texture area
	if (draw_outside_)
	{
		for (uint32_t a = 0; a < texture_->nPatches(); a++)
			drawPatch(a, true);
	}

	// Reset colouring
	OpenGL::setColour(COL_WHITE);

	// If we're currently dragging, draw a 'basic' preview of the texture using opengl
	if (dragging_)
	{
		glEnable(GL_SCISSOR_TEST);
		glScissor(
			left,
			top,
			texture_->width() * scale_ * (1.0 / tscalex),
			texture_->height() * yscale * (1.0 / tscaley));
		for (uint32_t a = 0; a < texture_->nPatches(); a++)
			drawPatch(a);
		glDisable(GL_SCISSOR_TEST);
	}

	// Otherwise, draw the fully generated texture
	else
	{
		// Generate if needed
		if (!tex_preview_.isLoaded())
		{
			// Determine image type
			SIType type = PALMASK;
			if (blend_rgba_)
				type = RGBA;

			// CTexture -> temp Image -> GLTexture
			SImage temp(type);
			texture_->toImage(temp, parent_, &palette_, blend_rgba_);
			tex_preview_.loadImage(&temp, &palette_);
		}

		// Draw it
		tex_preview_.draw2d();
	}

	// Disable textures
	glDisable(GL_TEXTURE_2D);

	// Now loop through selected patches and draw selection outlines
	OpenGL::setColour(70, 210, 220, 255, BLEND_NORMAL);
	glEnable(GL_LINE_SMOOTH);
	glLineWidth(1.5f);
	for (size_t a = 0; a < selected_patches_.size(); a++)
	{
		// Skip if not selected
		if (!selected_patches_[a])
			continue;

		// Get patch
		CTPatch*   patch  = texture_->patch(a);
		CTPatchEx* epatch = (CTPatchEx*)patch;

		// Check for rotation
		if (texture_->isExtended() && (epatch->rotation() == 90 || epatch->rotation() == -90))
		{
			// Draw outline, width/height swapped
			glBegin(GL_LINE_LOOP);
			glVertex2i(patch->xOffset(), patch->yOffset());
			glVertex2i(patch->xOffset(), patch->yOffset() + (int)patch_textures_[a]->width());
			glVertex2i(
				patch->xOffset() + (int)patch_textures_[a]->height(),
				patch->yOffset() + (int)patch_textures_[a]->width());
			glVertex2i(patch->xOffset() + (int)patch_textures_[a]->height(), patch->yOffset());
			glEnd();
		}
		else
		{
			// Draw outline
			glBegin(GL_LINE_LOOP);
			glVertex2i(patch->xOffset(), patch->yOffset());
			glVertex2i(patch->xOffset(), patch->yOffset() + (int)patch_textures_[a]->height());
			glVertex2i(
				patch->xOffset() + (int)patch_textures_[a]->width(),
				patch->yOffset() + (int)patch_textures_[a]->height());
			glVertex2i(patch->xOffset() + (int)patch_textures_[a]->width(), patch->yOffset());
			glEnd();
		}
	}

	// Finally, draw a hilight outline if anything is hilighted
	if (hilight_patch_ >= 0 && hilight_patch_ < (int)texture_->nPatches())
	{
		// Set colour
		OpenGL::setColour(255, 255, 255, 150, BLEND_ADDITIVE);

		// Get patch
		CTPatch*   patch         = texture_->patch(hilight_patch_);
		CTPatchEx* epatch        = (CTPatchEx*)patch;
		GLTexture* patch_texture = patch_textures_[hilight_patch_];

		// Check for rotation
		if (texture_->isExtended() && (epatch->rotation() == 90 || epatch->rotation() == -90))
		{
			// Draw outline, width/height swapped
			glBegin(GL_LINE_LOOP);
			glVertex2i(patch->xOffset(), patch->yOffset());
			glVertex2i(patch->xOffset(), patch->yOffset() + (int)patch_texture->width());
			glVertex2i(
				patch->xOffset() + (int)patch_texture->height(), patch->yOffset() + (int)patch_texture->width());
			glVertex2i(patch->xOffset() + (int)patch_texture->height(), patch->yOffset());
			glEnd();
		}
		else
		{
			// Draw outline
			glBegin(GL_LINE_LOOP);
			glVertex2i(patch->xOffset(), patch->yOffset());
			glVertex2i(patch->xOffset(), patch->yOffset() + (int)patch_texture->height());
			glVertex2i(
				patch->xOffset() + (int)patch_texture->width(), patch->yOffset() + (int)patch_texture->height());
			glVertex2i(patch->xOffset() + (int)patch_texture->width(), patch->yOffset());
			glEnd();
		}
	}
	glDisable(GL_LINE_SMOOTH);
	glLineWidth(1.0f);

	// Pop matrix
	glPopMatrix();
}

// -----------------------------------------------------------------------------
// Draws the patch at index [num] in the composite texture
// -----------------------------------------------------------------------------
void CTextureCanvas::drawPatch(int num, bool outside)
{
	// Get patch to draw
	CTPatch* patch = texture_->patch(num);

	// Check it exists
	if (!patch)
		return;

	// Load the patch as an opengl texture if it isn't already
	if (!patch_textures_[num]->isLoaded())
	{
		SImage temp(PALMASK);
		if (texture_->loadPatchImage(num, temp, parent_, &palette_))
		{
			// Load the image as a texture
			patch_textures_[num]->loadImage(&temp, &palette_);
		}
		else
			patch_textures_[num]->genChequeredTexture(8, COL_RED, COL_BLACK);
	}

	// Translate to position
	glPushMatrix();
	glTranslated(patch->xOffset(), patch->yOffset(), 0);

	// Setup rendering options
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// Setup extended features
	bool   flipx        = false;
	bool   flipy        = false;
	double alpha        = 1.0;
	bool   shade_select = true;
	rgba_t col          = COL_WHITE;
	if (texture_->isExtended())
	{
		// Get extended patch
		CTPatchEx* epatch = (CTPatchEx*)patch;

		// Flips
		if (epatch->flipX())
			flipx = true;
		if (epatch->flipY())
			flipy = true;

		// Rotation
		if (epatch->rotation() == 90)
		{
			glTranslated(patch_textures_[num]->height(), 0, 0);
			glRotated(90, 0, 0, 1);
		}
		else if (epatch->rotation() == 180)
		{
			glTranslated(patch_textures_[num]->width(), patch_textures_[num]->height(), 0);
			glRotated(180, 0, 0, 1);
		}
		else if (epatch->rotation() == -90)
		{
			glTranslated(0, patch_textures_[num]->width(), 0);
			glRotated(-90, 0, 0, 1);
		}
	}

	// Set colour
	if (outside)
		glColor4f(0.8f, 0.2f, 0.2f, 0.3f);
	else
		glColor4f(col.fr(), col.fg(), col.fb(), alpha);

	// Draw the patch
	patch_textures_[num]->draw2d(0, 0, flipx, flipy);

	glPopMatrix();
}

// -----------------------------------------------------------------------------
// Draws a black border around the texture
// -----------------------------------------------------------------------------
void CTextureCanvas::drawTextureBorder()
{
	// Draw the texture border
	double ext = 0.11;
	glLineWidth(2.0f);
	OpenGL::setColour(COL_BLACK);
	glBegin(GL_LINE_LOOP);
	glVertex2d(-ext, -ext);
	glVertex2d(-ext, texture_->height() + ext);
	glVertex2d(texture_->width() + ext, texture_->height() + ext);
	glVertex2d(texture_->width() + ext, -ext);
	glEnd();
	glLineWidth(1.0f);

	// Draw vertical ticks
	int y = 0;
	glColor4f(0.0f, 0.0f, 0.0f, 0.6f);
	while (y <= texture_->height())
	{
		glBegin(GL_LINES);
		glVertex2i(-4, y);
		glVertex2i(0, y);
		glVertex2i(texture_->width(), y);
		glVertex2i(texture_->width() + 4, y);
		glEnd();

		y += 8;
	}

	// Draw horizontal ticks
	int x = 0;
	while (x <= texture_->width())
	{
		glBegin(GL_LINES);
		glVertex2i(x, -4);
		glVertex2i(x, 0);
		glVertex2i(x, texture_->height());
		glVertex2i(x, texture_->height() + 4);
		glEnd();

		x += 8;
	}

	// Draw grid
	if (show_grid_)
	{
		// Draw inverted grid lines
		glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ZERO);
		glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

		// Vertical
		y = 8;
		while (y <= texture_->height() - 8)
		{
			glBegin(GL_LINES);
			glVertex2i(0, y);
			glVertex2i(texture_->width(), y);
			glEnd();

			y += 8;
		}

		// Horizontal
		x = 8;
		while (x <= texture_->width() - 8)
		{
			glBegin(GL_LINES);
			glVertex2i(x, 0);
			glVertex2i(x, texture_->height());
			glEnd();

			x += 8;
		}


		// Darken grid lines
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glColor4f(0.0f, 0.0f, 0.0f, 0.5f);

		// Vertical
		y = 8;
		while (y <= texture_->height() - 8)
		{
			glBegin(GL_LINES);
			glVertex2i(0, y);
			glVertex2i(texture_->width(), y);
			glEnd();

			y += 8;
		}

		// Horizontal
		x = 8;
		while (x <= texture_->width() - 8)
		{
			glBegin(GL_LINES);
			glVertex2i(x, 0);
			glVertex2i(x, texture_->height());
			glEnd();

			x += 8;
		}
	}
}

// -----------------------------------------------------------------------------
// Draws the offset center lines
// -----------------------------------------------------------------------------
void CTextureCanvas::drawOffsetLines()
{
	if (view_type_ == View::Sprite)
	{
		OpenGL::setColour(COL_BLACK);

		glBegin(GL_LINES);
		glVertex2d(-9999, 0);
		glVertex2d(9999, 0);
		glVertex2d(0, -9999);
		glVertex2d(0, 9999);
		glEnd();
	}
	else if (view_type_ == View::HUD)
	{
		glPushMatrix();
		glEnable(GL_LINE_SMOOTH);
		Drawing::drawHud();
		glDisable(GL_LINE_SMOOTH);
		glPopMatrix();
	}
}

// -----------------------------------------------------------------------------
// Redraws the texture, updating it if [update_texture] is true
// -----------------------------------------------------------------------------
void CTextureCanvas::redraw(bool update_texture)
{
	if (update_texture)
		updateTexturePreview();

	Refresh();
}

// -----------------------------------------------------------------------------
// Convert from [x,y] from the top left of the canvas to coordinates relative to
// the top left of the texture
// -----------------------------------------------------------------------------
point2_t CTextureCanvas::screenToTexPosition(int x, int y)
{
	// Check a texture is open
	if (!texture_)
		return point2_t(0, 0);

	// Get texture scale
	double scalex = 1;
	double scaley = 1;
	if (tex_scale_)
	{
		scalex = texture_->scaleX();
		if (scalex == 0)
			scalex = 1;
		scaley = texture_->scaleY();
		if (scaley == 0)
			scaley = 1;
	}

	// Get top-left of texture in screen coordinates (ie relative to the top-left of the canvas)
	int left = GetSize().x * 0.5 + offset_.x;
	int top  = GetSize().y * 0.5 + offset_.y;

	// Adjust for view type
	double yscale = (tx_arc ? scale_ * 1.2 : scale_);
	if (view_type_ == View::Normal)
	{
		// None (centered)
		left -= ((double)texture_->width() / scalex) * 0.5 * scale_;
		top -= ((double)texture_->height() / scaley) * 0.5 * yscale;
	}
	if (view_type_ == View::Sprite || view_type_ == View::HUD)
	{
		// Sprite
		left -= ((double)texture_->offsetX() / scalex) * scale_;
		top -= ((double)texture_->offsetY() / scaley) * yscale;
	}
	if (view_type_ == View::HUD)
	{
		// HUD
		left -= 160 * scale_;
		top -= 100 * yscale;
	}

	return point2_t(double(x - left) / scale_ * scalex, double(y - top) / yscale * scaley);
}

// -----------------------------------------------------------------------------
// Convert from [x,y] from the top left of the texture to coordinates relative
// to the top left of the canvas
// -----------------------------------------------------------------------------
point2_t CTextureCanvas::texToScreenPosition(int x, int y)
{
	// Get texture scale
	double tscalex = 1;
	double tscaley = 1;
	if (tex_scale_)
	{
		tscalex = texture_->scaleX();
		if (tscalex == 0)
			tscalex = 1;
		tscaley = texture_->scaleY();
		if (tscaley == 0)
			tscaley = 1;
	}
	tscalex = 1.0 / tscalex;
	tscaley = 1.0 / tscaley;

	// Get top/left
	double yscale = (tx_arc ? scale_ * 1.2 : scale_);
	double halfx  = (texture_->width() * 0.5 * scale_ * tscalex);
	double halfy  = (texture_->height() * 0.5 * yscale * tscaley);
	double left   = offset_.x + (GetSize().x * 0.5) - halfx;
	double top    = -offset_.y + (GetSize().y * 0.5) - halfy;

	// Adjust for view types
	if (view_type_ == View::Sprite || view_type_ == View::HUD)
	{
		// Sprite
		left -= (texture_->offsetX() * scale_ * tscalex) - halfx;
		top += (texture_->offsetY() * yscale * tscaley) - halfy;
	}
	if (view_type_ == View::HUD)
	{
		// HUD
		left -= (160 * scale_);
		top += (100 * yscale);
	}

	return point2_t(left, top);
}

// -----------------------------------------------------------------------------
// Returns the index of the patch at [x,y] on the texture, or -1 if no patch is
// at that position
// -----------------------------------------------------------------------------
int CTextureCanvas::patchAt(int x, int y)
{
	// Check a texture is open
	if (!texture_)
		return -1;

	// Go through texture patches backwards (ie from frontmost to back)
	for (int a = texture_->nPatches() - 1; a >= 0; a--)
	{
		// Check if x,y is within patch bounds
		CTPatch* patch = texture_->patch(a);
		if (x >= patch->xOffset() && x < patch->xOffset() + (int)patch_textures_[a]->width() && y >= patch->yOffset()
			&& y < patch->yOffset() + (int)patch_textures_[a]->height())
		{
			return a;
		}
	}

	// No patch at x,y
	return -1;
}

// -----------------------------------------------------------------------------
// Swaps patches at [p1] and [p2] in the texture.
// Returns false if either index is invalid, true otherwise
// -----------------------------------------------------------------------------
bool CTextureCanvas::swapPatches(size_t p1, size_t p2)
{
	// Check a texture is open
	if (!texture_)
		return false;

	// Check indices
	if (p1 >= texture_->nPatches() || p2 >= texture_->nPatches())
		return false;

	// Swap patch gl textures
	GLTexture* temp     = patch_textures_[p1];
	patch_textures_[p1] = patch_textures_[p2];
	patch_textures_[p2] = temp;

	// Swap patches in the texture itself
	return texture_->swapPatches(p1, p2);
}

// -----------------------------------------------------------------------------
// Called when the texture canvas recieves an announcement from the texture
// being displayed
// -----------------------------------------------------------------------------
void CTextureCanvas::onAnnouncement(Announcer* announcer, string event_name, MemChunk& event_data)
{
	// If the announcer isn't this canvas' texture, ignore it
	if (announcer != texture_)
		return;

	// Patches modified
	if (event_name == "patches_modified")
	{
		// Reload patches
		selected_patches_.clear();
		clearPatchTextures();
		hilight_patch_ = -1;
		for (uint32_t a = 0; a < texture_->nPatches(); a++)
		{
			// Create GL texture
			patch_textures_.push_back(new GLTexture());

			// Set selection
			selected_patches_.push_back(false);
		}

		redraw(true);
	}
}

// -----------------------------------------------------------------------------
// Called when and mouse event is generated (movement/clicking/etc)
// -----------------------------------------------------------------------------
void CTextureCanvas::onMouseEvent(wxMouseEvent& e)
{
	bool refresh = false;

	// MOUSE MOVEMENT
	if (e.Moving() || e.Dragging())
	{
		dragging_ = false;

		// Pan if middle button is down
		if (e.MiddleIsDown())
		{
			offset_   = offset_ + point2_t(e.GetPosition().x - mouse_prev_.x, e.GetPosition().y - mouse_prev_.y);
			refresh   = true;
			dragging_ = true;
		}
		else if (e.LeftIsDown())
			dragging_ = true;

		// Check if patch hilight changes
		point2_t pos   = screenToTexPosition(e.GetX(), e.GetY());
		int      patch = patchAt(pos.x, pos.y);
		if (hilight_patch_ != patch)
		{
			hilight_patch_ = patch;
			refresh        = true;
		}
	}

	// LEFT BUTTON UP
	else if (e.LeftUp())
	{
		// If we were dragging, generate end drag event
		if (dragging_)
		{
			dragging_ = false;
			updateTexturePreview();
			refresh = true;
			wxCommandEvent evt(EVT_DRAG_END, GetId());
			evt.SetInt(wxMOUSE_BTN_LEFT);
			ProcessWindowEvent(evt);
		}
	}

	// LEAVING
	if (e.Leaving())
	{
		// Set no hilighted patch
		hilight_patch_ = -1;
		refresh        = true;
	}

	// Refresh is needed
	if (refresh)
		Refresh();

	// Update 'previous' mouse coordinates
	mouse_prev_.set(e.GetPosition().x, e.GetPosition().y);
}
