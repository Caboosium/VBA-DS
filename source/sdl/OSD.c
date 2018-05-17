// VisualBoyAdvance - Nintendo Gameboy/GameboyAdvance (TM) emulator.
// Copyright (C) 2008 VBA-M development team

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2, or(at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

#include "OSD.h"
#include "Timer.h"
#include "../common/DisplayDriver.h"
#include "../common/Util.h"

#include <SDL_ttf.h>

struct TextOSD {
	Renderable *renderable;
	Timeout *autoclear;

	gchar *message;
	SDL_Color color;
	gint opacity;

	/** Texture to render */
	SDL_Texture *texture;
};

struct ImageOSD {
	Renderable *renderable;

	/** Texture to render */
	SDL_Texture *texture;
};

struct RectOSD {
	Renderable *renderable;

	SDL_Color color;
	gint opacity;

	guint borderThickness;
	SDL_Color borderColor;
};

static guint text_compute_size(TextOSD *text) {
	return display_sdl_scale(text->renderable->display, text->renderable->height);
}

static gboolean text_update_texture(TextOSD *text, GError **err) {
	g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

	SDL_DestroyTexture(text->texture);

	gchar *fontFile = data_get_file_path("fonts", "DroidSans-Bold.ttf");
	TTF_Font *font = TTF_OpenFont(fontFile, text_compute_size(text));
	g_free(fontFile);

	if (font == NULL) {
		g_set_error(err, DISPLAY_ERROR, G_DISPLAY_ERROR_FAILED,
				"Failed to load font: %s", TTF_GetError());
		return FALSE;
	}

	SDL_Surface *surface = TTF_RenderText_Blended(font, text->message, text->color);
	if (surface == NULL) {
		g_set_error(err, DISPLAY_ERROR, G_DISPLAY_ERROR_FAILED,
				"Failed render text: %s", TTF_GetError());
		return FALSE;
	}

	text->texture = SDL_CreateTextureFromSurface(text->renderable->renderer, surface);
	if (text->texture == NULL) {
		g_set_error(err, DISPLAY_ERROR, G_DISPLAY_ERROR_FAILED,
				"Failed create texture : %s", SDL_GetError());
		return FALSE;
	}

	if (text->opacity != 100) {
		SDL_SetTextureAlphaMod(text->texture, text->opacity * 255 / 100);
	}

	SDL_FreeSurface(surface);
	TTF_CloseFont(font);

	return TRUE;
}

static void osd_render(Renderable *renderable, SDL_Texture *texture) {
	int textWidth, textHeight;
	SDL_QueryTexture(texture, NULL, NULL, &textWidth, &textHeight);

	int rendWidth = display_sdl_scale(renderable->display, renderable->width);
	int rendHeight = display_sdl_scale(renderable->display, renderable->height);

	gint x, y;
	display_sdl_renderable_get_absolute_position(renderable, &x, &y);

	SDL_Rect screenRect;
	screenRect.w = textWidth;
	screenRect.h = textHeight;
	screenRect.y = y + (rendHeight - textHeight) / 2;

	switch (renderable->horizontalAlignment) {
	case ALIGN_LEFT:
		screenRect.x = x;
		break;
	case ALIGN_CENTER:
		screenRect.x = x + (rendWidth - textWidth) / 2;
		break;
	case ALIGN_RIGHT:
		screenRect.x = x + rendWidth - textWidth;
		break;
	}

	SDL_RenderCopy(renderable->renderer, texture, NULL, &screenRect);
}

static void text_osd_render(gpointer entity) {
	TextOSD *text = entity;

	if (text->message == NULL) {
		return; // Nothing to render
	}

	if (text->texture == NULL) {
		text_update_texture(text, NULL);
	}

	osd_render(text->renderable, text->texture);
}

static void text_osd_resize(gpointer entity) {
	TextOSD *text = entity;

	SDL_DestroyTexture(text->texture);
	text->texture = NULL;
}


TextOSD *text_osd_create(Display *display, const gchar *message, const Renderable *parent, GError **err) {
	g_return_val_if_fail(err == NULL || *err == NULL, NULL);

	TextOSD *text = g_new(TextOSD, 1);
	text->renderable = display_sdl_renderable_create(display, text, parent);
	text->renderable->render = text_osd_render;
	text->renderable->resize = text_osd_resize;
	text->autoclear = NULL;

	text->message = g_strdup(message);
	text->opacity = 100;
	text->color.r = 0;
	text->color.g = 0;
	text->color.b = 0;
	text->texture = NULL;

	// Render here to perform error checking
	if (text->message && !text_update_texture(text, err)) {
		text_osd_free(text);
		return NULL;
	}

	return text;
}

void text_osd_free(TextOSD *text) {
	if (text == NULL)
		return;

	SDL_DestroyTexture(text->texture);

	display_sdl_renderable_free(text->renderable);
	timeout_free(text->autoclear);

	g_free(text->message);
	g_free(text);
}

void text_osd_set_message(TextOSD *text, const gchar *message) {
	g_assert(text != NULL);

	if (g_strcmp0(message, text->message) == 0)
		return; // Nothing to update

	g_free(text->message);
	text->message = g_strdup(message);

	SDL_DestroyTexture(text->texture);
	text->texture = NULL;
}

void text_osd_set_color(TextOSD *text, guint8 r, guint8 g, guint8 b) {
	g_assert(text != NULL);

	text->color.r = r;
	text->color.g = g;
	text->color.b = b;

	SDL_DestroyTexture(text->texture);
	text->texture = NULL;
}

void text_osd_set_position(TextOSD *text, gint x, gint y) {
	g_assert(text != NULL);

	display_sdl_renderable_set_position(text->renderable, x, y);
}

void text_osd_set_alignment(TextOSD *text, HorizontalAlignment horizontal, VerticalAlignment vertical) {
	g_assert(text != NULL);

	display_sdl_renderable_set_alignment(text->renderable, horizontal, vertical);
}

void text_osd_set_opacity(TextOSD *text, gint opacity) {
	g_assert(text != NULL);
	g_assert(opacity >= 0 && opacity <= 100);

	text->opacity = opacity;

	SDL_DestroyTexture(text->texture);
	text->texture = NULL;
}

void text_osd_set_size(TextOSD *text, gint width, gint height) {
	g_assert(text != NULL);
	display_sdl_renderable_set_size(text->renderable, width, height);

	SDL_DestroyTexture(text->texture);
	text->texture = NULL;
}

static void text_osd_autoclear(gpointer entity) {
	TextOSD *text = entity;
	text_osd_set_message(text, " ");
}

void text_osd_set_auto_clear(TextOSD *text, guint duration) {
	g_assert(text != NULL);

	timeout_free(text->autoclear);

	text->autoclear = timeout_create(text, text_osd_autoclear);

	timeout_set_duration(text->autoclear, duration);
}

static void image_osd_render(gpointer entity) {
	ImageOSD *image = entity;

	osd_render(image->renderable, image->texture);
}

ImageOSD *image_osd_create(Display *display, const gchar *file, const Renderable *parent, GError **err) {
	g_return_val_if_fail(err == NULL || *err == NULL, NULL);

	ImageOSD *image = g_new(ImageOSD, 1);
	image->renderable = display_sdl_renderable_create(display, image, parent);
	image->renderable->render = image_osd_render;

	image->texture = display_sdl_load_png(display, file, err);

	if (image->texture == NULL) {
		image_osd_free(image);
		return NULL;
	}

	return image;
}

void image_osd_free(ImageOSD *image) {
	if (image == NULL)
		return;

	SDL_DestroyTexture(image->texture);

	display_sdl_renderable_free(image->renderable);
	g_free(image);
}

void image_osd_set_position(ImageOSD *image, gint x, gint y) {
	g_assert(image != NULL);

	display_sdl_renderable_set_position(image->renderable, x, y);
}

void image_osd_set_size(ImageOSD *image, gint width, gint height) {
	g_assert(image != NULL);
	display_sdl_renderable_set_size(image->renderable, width, height);
}

void image_osd_set_alignment(ImageOSD *image, HorizontalAlignment horizontal, VerticalAlignment vertical) {
	g_assert(image != NULL);

	display_sdl_renderable_set_alignment(image->renderable, horizontal, vertical);
}

static void sdl_rect_grow(SDL_Rect *screenRect, guint amount) {
	screenRect->x -= amount;
	screenRect->y -= amount;
	screenRect->w += 2 * amount;
	screenRect->h += 2 * amount;
}

static void rect_osd_render(gpointer entity) {
	RectOSD *rect = entity;
	Renderable* renderable = rect->renderable;

	gint x, y;
	display_sdl_renderable_get_absolute_position(renderable, &x, &y);

	SDL_Rect screenRect;
	screenRect.x = x;
	screenRect.y = y;
	screenRect.w = display_sdl_scale(renderable->display, renderable->width);
	screenRect.h = display_sdl_scale(renderable->display, renderable->height);


	// Draw the border
	if (rect->borderThickness > 0) {
		SDL_SetRenderDrawBlendMode(renderable->renderer, SDL_BLENDMODE_ADD);
		SDL_SetRenderDrawColor(renderable->renderer, rect->borderColor.r, rect->borderColor.g, rect->borderColor.b, rect->opacity * 255 / 100);
		SDL_RenderFillRect(renderable->renderer, &screenRect);
	}

	// Draw the background
	sdl_rect_grow(&screenRect, -rect->borderThickness);
	SDL_SetRenderDrawBlendMode(renderable->renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderable->renderer, rect->color.r, rect->color.g, rect->color.b, rect->opacity * 255 / 100);
	SDL_RenderFillRect(renderable->renderer, &screenRect);
}

RectOSD *rect_osd_create(Display *display, gint x, gint y, guint w, guint h, const Renderable *parent, GError **err) {
	g_return_val_if_fail(err == NULL || *err == NULL, NULL);

	RectOSD *rect = g_new(RectOSD, 1);
	rect->renderable = display_sdl_renderable_create(display, rect, parent);
	rect->renderable->render = rect_osd_render;
	rect->renderable->x = x;
	rect->renderable->y = y;
	rect->renderable->width = w;
	rect->renderable->height = h;

	rect->opacity = 100;
	rect->color.r = 0;
	rect->color.g = 0;
	rect->color.b = 0;

	rect->borderThickness = 0;
	rect->borderColor.r = 0;
	rect->borderColor.g = 0;
	rect->borderColor.b = 0;

	return rect;
}

void rect_osd_free(RectOSD *rect) {
	if (rect == NULL)
		return;

	display_sdl_renderable_free(rect->renderable);

	g_free(rect);
}

void rect_osd_set_opacity(RectOSD *rect, gint opacity) {
	g_assert(rect != NULL);
	g_assert(opacity >= 0 && opacity <= 100);

	rect->opacity = opacity;
}

void rect_osd_set_alignment(RectOSD *rect, HorizontalAlignment horizontal, VerticalAlignment vertical) {
	g_assert(rect != NULL);

	display_sdl_renderable_set_alignment(rect->renderable, horizontal, vertical);
}

void rect_osd_set_color(RectOSD *rect, guint8 r, guint8 g, guint8 b) {
	g_assert(rect != NULL);

	rect->color.r = r;
	rect->color.g = g;
	rect->color.b = b;
}

void rect_osd_set_border(RectOSD *rect, guint thickness, guint8 r, guint8 g, guint8 b) {
	g_assert(rect != NULL);

	rect->borderThickness = thickness;
	rect->borderColor.r = r;
	rect->borderColor.g = g;
	rect->borderColor.b = b;
}

const Renderable *rect_osd_get_renderable(RectOSD *rect) {
	g_assert(rect != NULL);

	return rect->renderable;
}
