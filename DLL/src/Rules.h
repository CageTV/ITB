/*
 * ITB Wildlife
 * Copyright (c) 2026 CageTV
 *
 * Licensed under the GNU General Public License, version 3 or later; see LICENSE.
 */
#pragma once

namespace Rules
{
	// Reads Data/SKSE/Plugins/ITBWildlife/rules.json and makes sure the animal records still have what ITB Wildlife.esp
	// gives them, even when a later mod overwrote them. Runs once at kDataLoaded. Every rule is idempotent.
	void Apply();
}
