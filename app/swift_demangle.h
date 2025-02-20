/*
 * Copyright (C) 2025 Mimir Reynissonr
 *
 * This file is part of perfparser.
 *
 * perfparser is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * perfparser is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with [project name].  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef SWIFT_DEMANGLER_H
#define SWIFT_DEMANGLER_H

int swift_demangle(const char* symbol, char* buffer, size_t bufferLength);

#endif
