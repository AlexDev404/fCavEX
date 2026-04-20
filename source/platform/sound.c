/*
	Copyright (c) 2026 fCavEX contributors

	This file is part of fCavEX.

	fCavEX is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.
*/

#include "sound.h"

#ifdef PLATFORM_WII
#include "wii/sound.c"
#endif

#ifdef PLATFORM_PC
#include "pc/sound.c"
#endif
