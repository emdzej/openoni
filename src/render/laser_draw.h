/* The laser triggers' beams (FXrDrawLaser 0x4f4720): each a contrail of two points from the beam's
   start to its end, 0.2 wide across the view, in the trigger's colour. */
#pragma once

/* With the view matrix the frame draws with (column-major). */
void laser_draw(const float view[16]);
