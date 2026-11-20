/*
 * ITB Wildlife
 * Copyright (c) 2026 CageTV
 *
 * Licensed under the GNU General Public License, version 3 or later; see LICENSE.
 */
#pragma once

namespace Birds
{
	// Reads Data/SKSE/Plugins/ITBWildlife/birds.json and starts the bird tick (ambient pigeons, sparrows and crows
	// that perch on roofs, fences, rocks and trees and fly between them). Own design, vanilla critter animation graph.
	void Init();
	// Removes birds left in the world by an earlier session (dynamic references of our bird species).
	void OnGameLoaded();
}
