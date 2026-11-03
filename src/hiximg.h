/*
 * HixFlix's pictures from the web: TMDB posters, backdrops and episode stills
 * (and custom poster addresses), downloaded and decoded in the background,
 * kept on disk in cache/img, and made textures a few a frame.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#pragma once

#include <string>

#include "imgui.h"

void hiximg_init();
/* Main thread, every frame: finished pictures become textures, ones not drawn
 * for a while are freed. */
void hiximg_update();
/* The picture at this address, or 0 while it's coming (asked for now; the
 * newest asks go first). aspect: width / height. */
ImTextureID hix_image(const std::string &url, float *aspect = nullptr);
/* The picture failed (no such address, not a picture): draw without it. */
bool hix_image_failed(const std::string &url);
