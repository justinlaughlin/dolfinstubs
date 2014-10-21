#!/usr/bin/env py.test

"""Unit tests for the Scalar interface"""

# Copyright (C) 2011 Garth N. Wells
#
# This file is part of DOLFIN.
#
# DOLFIN is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# DOLFIN is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with DOLFIN. If not, see <http://www.gnu.org/licenses/>.
#
# First added:  2011-03-01
# Last changed: 2011-03-01

from dolfin import *

def test_parallel_sum():
    a = Scalar()
    b = 1.0
    a.assign(b)
    a.apply("add")
    assert round(a.getval() - b*MPI.size(a.mpi_comm()), 7) == 0