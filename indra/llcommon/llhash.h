/**
 * @file llhash.h
 * @brief Wrapper for a hash function.
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

#ifndef LL_LLHASH_H
#define LL_LLHASH_H

#include <boost/functional/hash.hpp>
#include <chrono>

// Warning - an earlier template-based version of this routine did not do
// the correct thing on Windows.   Since this is only used to get
// a string hash, it was converted to a regular routine and
// unit tests added.

inline size_t llhash( const char * value )
{
    // boost::hash is defined for std::string and for char, but there's no
    // special overload for const char*. The lazy approach would be to
    // instantiate a std::string and take its hash, but that might be more
    // overhead than our callers want. Or we could use boost::hash_range() --
    // but that would require a preliminary pass over the value to determine
    // the end iterator. Instead, use boost::hash_combine() to hash individual
    // characters.
    std::size_t seed = 0;
    for ( ; *value; ++value)
        boost::hash_combine(seed, *value);
    return seed;
}

//<3T:TommyTheTerrible> A high speed 64-bit performance hash for use with unsorted_sets using 64-bit pointers.
// Code has been given to the public domain.
struct vigna_hash
{
    static uint64_t splitmix64(uint64_t x)
    {
        // http://xorshift.di.unimi.it/splitmix64.c
        x += 0x9e3779b97f4a7c15;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9;
        x = (x ^ (x >> 27)) * 0x94d049bb133111eb;
        return x ^ (x >> 31);
    }

    size_t operator()(uint64_t x) const
    {
        static const uint64_t FIXED_RANDOM = std::chrono::steady_clock::now().time_since_epoch().count();
        return splitmix64(x + FIXED_RANDOM);
    }
};
//</3T>

#endif

