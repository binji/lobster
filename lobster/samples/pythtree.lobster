// graphics demo showing an animated "tree of pythagoras"

include "vec.lobster"
include "color.lobster"

def branch(n, max, scale, len):
    poly := [ xy_x, -xy_x, [ -scale, len ], [ scale, len ] ]
    gl_color(color { n / max, 0.8, n / max, 1 })
    gl_polygon(poly)
    gl_color(color { 0.3, 0.1, 0, 0.7 })
    for(2) i:
        gl_line(poly[i], poly[3 - i], 0.2)
    if n < max:
        gl_translate([ 0.0, len ])
        gl_scale(scale)
        for(2) i:
            gl_rotate_z(sincos((i * 2 - 1) * 30 + sin(gl_time() * 50) * n + rnd(40) - 20)):
                branch(n + 1, max, scale, len + 0.5)

fatal(gl_window("Pythagoras tree in Lobster", 1024, 768))

while gl_frame():
    if gl_wentdown("escape"): return
    gl_clear(color_grey)
    rndseed(0)
    gl_translate(float(gl_windowsize()) / xy { 2.0, 1.0 })
    gl_rotate_z([ -1.0, 0.0 ])
    gl_scale(gl_windowsize().y / 17.0)
    branch(0, 11.0, 0.7, 4)

