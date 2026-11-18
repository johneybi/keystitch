/*
    InputLeap -- mouse and keyboard sharing utility
    Copyright (C) InputLeap contributors

    This package is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    found in the file LICENSE that should have accompanied this file.

    This package is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "TestUtils.h"
#include <random>

namespace inputleap {

std::vector<std::uint8_t> generate_pseudo_random_bytes(std::size_t seed, std::size_t size)
{
    std::mt19937_64 engine{seed};
    std::vector<std::uint8_t> bytes;

    bytes.reserve(size);
    for (std::size_t i = 0; i < size; ++i) {
        // mt19937_64 has a specified sequence, but uniform_int_distribution's
        // mapping differs across standard libraries. Use the high byte
        // explicitly to preserve the existing fingerprint golden vectors on
        // libc++, libstdc++ and MSVC. This is fixture data, not security RNG.
        bytes.push_back(static_cast<std::uint8_t>(engine() >> 56));
    }

    return bytes;
}

} // namespace inputleap
