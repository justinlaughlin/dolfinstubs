"""Unit tests for multimesh volume computation"""

# Copyright (C) 2016 Anders Logg
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
# First added:  2016-05-03
# Last changed: 2016-11-16

import pytest

from dolfin import *
from dolfin_utils.test import skip_in_parallel

from math import pi, sin, cos

@skip_in_parallel
def test_multimesh_init_1():
  mm = MultiMesh()
  mesh0 = UnitSquareMesh(1, 1)
  mesh1 = RectangleMesh(Point(0,0), Point(2,0.5), 1, 1)
  mm.add(mesh0)
  mm.add(mesh1)
  mm.build(2)

  return 1 == 1
