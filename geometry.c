/*
  geometry.h - a place for geometry helpers
  Part of Grbl

  Copyright (c) 2009 Simen Svale Skogsrud

  Grbl is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Grbl is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Grbl.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "geometry.h"
#include <avr/io.h>
#include <math.h>
#include <stdlib.h>

// Find the angle in radians of deviance from the positive y axis. negative angles to the left of y-axis, 
// positive to the right.
double theta(double x, double y)
{
  double theta = atan(x/fabs(y));
  if (y>0) {
    return(theta);
  } else {
    if (theta>0) 
    {
      return(M_PI-theta);
    } else {
      return(-M_PI-theta);
    }
  }
}

/* 
  Quadrants of the circle
    
  +---- 0 ----+    0 - y is always positive and |x| < |y|
  |           |    1 - x is always positive and |x| > |y|
  |           |    2 - y is always negative and |x| < |y|
  3     +     1    3 - x is always negative and |x| > |y|
  |           |
  |           |
  +---- 2 ----+                    
*/

// Find the quadrant of the coordinate 
int quadrant_of_the_circle(int32_t x, int32_t y) {
  if (labs(x)<labs(y)){
    if (y>0) {
      return(0);
    } else {
      return(2);
    }      
  } else {
    if (x>0) {
      return(1);
    } else {
      return(3);
    }      
  }
}

// Very specialized helper to calculate the amount of steps to travel in the given quadrant of a circle provided the
// axial direction of the quadrant, the angular_direction of travel (-1 or +1) and amount of steps in one half quadrant 
// of the circle. 
uint32_t steps_in_partial_quadrant(int32_t x, int32_t y, int quadrant, int angular_direction, 
  int32_t steps_in_half_quadrant) {
  if (quadrant_horizontal(quadrant))  { // A horizontal quadrant
    if ((angular_direction == 1) ^ (quadrant == 2)) {
      return(steps_in_half_quadrant-x);
    } else {
      return(x+steps_in_half_quadrant);
    }
  } else { // A vertical quadrant
    if ((angular_direction == 1) ^ (quadrant == 3)) {
      return(steps_in_half_quadrant-y);
    } else {
      return(y+steps_in_half_quadrant);
    }
  }
}

// Counts the amount of full quadrants between quadrant_start and quadrant_target along the angular_direction
int full_quadrants_between(int quadrant_start, int quadrant_target, int angular_direction) {
  int diff = angular_direction*(quadrant_target-quadrant_start);
  if (diff <= 0) { diff += 4; }
  return (diff-1);
}