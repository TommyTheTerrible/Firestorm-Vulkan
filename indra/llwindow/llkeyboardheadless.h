/**
 * @file llkeyboardheadless.h
 * @brief Handler for assignable key bindings
 *
 * $LicenseInfo:firstyear=2004&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#ifndef LL_LLKEYBOARDHEADLESS_H
#define LL_LLKEYBOARDHEADLESS_H

#include "llkeyboard.h"

class LLKeyboardHeadless : public LLKeyboard
{
public:
    LLKeyboardHeadless();
    /*virtual*/ ~LLKeyboardHeadless() {};

#ifndef LL_SDL2
    /*virtual*/ bool    handleKeyUp(const U16 key, MASK mask) { return false; }
    /*virtual*/ bool    handleKeyDown(const U16 key, MASK mask) { return false; }
#else
    /*virtual*/ bool    handleKeyUp(const U32 key, MASK mask) { return false; }
    /*virtual*/ bool    handleKeyDown(const U32 key, MASK mask) { return false; }
#endif
    /*virtual*/ void    resetMaskKeys();
    /*virtual*/ MASK    currentMask(bool for_mouse_event);
    /*virtual*/ void    scanKeyboard();
#ifdef LL_DARWIN
    /*virtual*/ void    handleModifier(MASK mask);
#endif
};

#endif
