#!/usr/bin/env python3
"""
Builds the receiver case (base + lid) as solid CAD bodies and exports STEP files for Autodesk Fusion, FreeCAD,
Onshape ... - same geometry and print position as receiver_case.scad / the STL files (values copied from there:
change both). Needs: pip install cadquery   (on this Mac: ../../tools/cadquery-venv/bin/python)
"""
import os
import cadquery as cq

# ---- dimensions (from receiver_case.scad) ----
pico_l, pico_w, pico_t = 51, 21, 1.0
pico_holes = [(2, 4.8), (2, 16.2), (49, 4.8), (49, 16.2)]
pico_hole_d, usb_over, usb_w, usb_h = 2.1, 1.3, 8.0, 2.8
mod_l, mod_w, mod_t = 32, 17, 1.6
jack_u, jack_d, jack_z, jack_over = 26, 6.0, 2.6, 2.0
jack_body = (9, 12, 5.5)
hdr_body, pin_below, plug_len, bend = 2.5, 6.0, 14, 7
wall, floor_t, top_t, play, gap = 2.0, 2.0, 1.6, 0.6, 6
usb_clear, jack_clear, above = 0.3, 0.3, 1.0
peg_d, post_d = pico_hole_d - 0.5, 3.4
lid_fit, lip_t, lip_h = 0.15, 1.2, 1.6
skirt_h, step, skirt_fit, snap, snap_len = 4.0, 1.0, 0.1, 0.35, 0.6
r_out = 2.0
usb_hole = (12, 7.5)
feet = True

in_w = play + pico_w + gap + mod_l + play
in_l = pico_l + usb_over - usb_clear + play
out_w, out_l = in_w + 2 * wall, in_l + 2 * wall
z_pcb = floor_t + hdr_body + plug_len + bend
z_rim = z_pcb + mod_t + jack_body[2] + above
z_step = z_rim - skirt_h
z_bead = z_step + skirt_h * 0.4
total_h = z_rim + top_t
px0 = wall + play
py_usb = wall + in_l - usb_over + usb_clear
mx0 = wall + play + pico_w + gap
my1 = wall + in_l - jack_clear
my0 = my1 - mod_w
jack_x = mx0 + jack_u
z_jack = z_pcb + mod_t + jack_z
z_usb = z_pcb + pico_t + usb_h / 2
slot_w = jack_d + 0.8


def pico_xy(along, across):
    return px0 + across, py_usb - along


def box(x0, y0, z0, x, y, z):
    return cq.Workplane("XY").box(x, y, z, centered=False).translate((x0, y0, z0))


def cyl(cx, cy, z0, d, h):
    return cq.Workplane("XY").circle(d / 2).extrude(h).translate((cx, cy, z0))


def cone(cx, cy, z0, d1, d2, h):
    return cq.Workplane("XY").add(cq.Solid.makeCone(d1 / 2, d2 / 2, h, pnt=cq.Vector(cx, cy, z0)))


def rbox(z0, h):
    return box(0, 0, z0, out_w, out_l, h).edges("|Z").fillet(r_out)


def rrect(inset, z0, h):
    r = max(r_out - inset, 0.2)
    return box(inset, inset, z0, out_w - 2 * inset, out_l - 2 * inset, h).edges("|Z").fillet(r)


def ridges(d, half):               # triangular ridge on the four faces of the thinned wall
    lx, ly = (out_w - 2 * step) * snap_len, (out_l - 2 * step) * snap_len
    x0, y0 = (out_w - lx) / 2, (out_l - ly) / 2
    a, b, c = z_bead - half, z_bead, z_bead + half
    shapes = [
        cq.Workplane("YZ").polyline([(step, a), (step - d, b), (step, c)]).close().extrude(lx).translate((x0, 0, 0)),
        cq.Workplane("YZ").polyline([(out_l - step, a), (out_l - step + d, b), (out_l - step, c)]).close().extrude(lx).translate((x0, 0, 0)),
        cq.Workplane("XZ").polyline([(step, a), (step - d, b), (step, c)]).close().extrude(-ly).translate((0, y0, 0)),
        cq.Workplane("XZ").polyline([(out_w - step, a), (out_w - step + d, b), (out_w - step, c)]).close().extrude(-ly).translate((0, y0, 0)),
    ]
    out = shapes[0]
    for sh in shapes[1:]:
        out = out.union(sh)
    return out


def usb_opening():                  # rounded rectangle through the back wall
    w, h = usb_hole
    return (cq.Workplane("XZ").center(px0 + pico_w / 2, z_usb).rect(w, h).extrude(-(wall + 2))
            .edges("|Y").fillet(1.5).translate((0, wall + in_l - 1, 0)))


def jack_opening():
    y0 = wall + in_l - 1
    hole = cq.Workplane("XZ").center(jack_x, z_jack).circle((jack_d + 0.8) / 2).extrude(-(wall + 2)).translate((0, y0, 0))
    chamfer = cq.Workplane("XZ").add(cq.Solid.makeCone((jack_d + 0.8) / 2, (jack_d + 3.2) / 2, 0.81,
                                                       pnt=cq.Vector(jack_x, wall + in_l + wall - 0.8, z_jack), dir=cq.Vector(0, 1, 0)))
    return hole.union(chamfer)


def base():
    body = rbox(0, z_rim).cut(box(wall, wall, floor_t, in_w, in_l, z_rim))
    for h in pico_holes[2:]:                              # posts under the Pico's front holes
        x, y = pico_xy(*h)
        body = body.union(cone(x, y, 0, post_d + 4, post_d, z_pcb - hdr_body - pin_below - 1))
        body = body.union(cyl(x, y, 0, post_d, z_pcb))
        body = body.union(cyl(x, y, z_pcb - 0.01, peg_d, pico_t + 0.6))
        body = body.union(cone(x, y, z_pcb + pico_t + 0.59, peg_d, peg_d - 0.6, 0.5))
    body = body.union(box(px0 + 4, py_usb - 1.5, 0, pico_w - 8, wall + in_l - py_usb + 1.5, z_pcb))   # Pico ledge
    body = body.union(box(mx0 + 5, my1 - 1.2, 0, jack_x - jack_body[0] / 2 - 1 - (mx0 + 5), wall + in_l - my1 + 1.2, z_pcb))
    body = body.union(box(mx0 + 5, my0 - 1.2, 0, mod_l - 7, 2.0, z_pcb))                               # module rib
    body = body.union(box(mx0 + mod_l + 0.2, my0 + 3, z_pcb - 3, wall + in_w - (mx0 + mod_l + 0.2), mod_w - 6, 3 + mod_t + 1))
    body = body.cut(box(-1, -1, z_step, out_w + 2, out_l + 2, skirt_h + 1).cut(rrect(step, z_step - 1, skirt_h + 3)))
    if snap > 0:
        body = body.union(ridges(snap, 0.4))              # snap bead (the openings below cut through it)
    body = body.cut(usb_opening()).cut(jack_opening())
    body = body.cut(box(jack_x - slot_w / 2, wall + in_l - 1, z_jack, slot_w, wall + 2, z_rim))         # jack slot
    if feet:
        for x in (8, out_w - 8):
            for y in (8, out_l - 8):
                body = body.cut(cyl(x, y, -1, 8.6, 1.6))
    return body


def lid_in_place():
    body = rbox(z_rim, top_t)
    body = body.union(rbox(z_step + 0.2, skirt_h - 0.2 + 0.01).cut(rrect(step - skirt_fit, z_step, skirt_h + 1)))
    rim = box(wall + lid_fit, wall + lid_fit, z_rim - lip_h, in_w - 2 * lid_fit, in_l - 2 * lid_fit, lip_h + 0.01)
    rim = rim.cut(box(wall + lid_fit + lip_t, wall + lid_fit + lip_t, z_rim - lip_h - 1,
                      in_w - 2 * lid_fit - 2 * lip_t, in_l - 2 * lid_fit - 2 * lip_t, lip_h + 2))
    body = body.union(rim)
    body = body.union(box(jack_x - slot_w / 2 + lid_fit, wall + in_l - 0.01 - lip_t, z_jack, slot_w - 2 * lid_fit, wall + lip_t, z_rim - z_jack + 0.01))
    for h in pico_holes[2:]:
        x, y = pico_xy(*h)
        tube = cyl(x, y, z_pcb + pico_t, post_d + 0.6, z_rim - z_pcb - pico_t + 0.01).cut(cyl(x, y, z_pcb + pico_t - 1, peg_d + 0.8, 3))
        body = body.union(tube)
    body = body.union(box(px0 + pico_w / 2 - usb_w / 2, py_usb - 4, z_pcb + pico_t + usb_h, usb_w, 4 - 0.01, z_rim - z_pcb - pico_t - usb_h + 0.01))
    body = body.union(box(jack_x - 2, my1 - jack_body[1] + 2, z_pcb + mod_t + jack_body[2], 4, 6, above + 0.01))
    body = body.union(box(mx0 + 6, my0 + 0.1, z_pcb + mod_t, mod_l - 12, 1.0, z_rim - z_pcb - mod_t + 0.01))
    if snap > 0:
        body = body.cut(ridges(snap + 0.1, 0.5))
    body = body.cut(box(out_w / 2 - 4, -1, z_step + 0.19, 8, step + 1, 0.8))             # finger notch
    body = body.cut(usb_opening()).cut(jack_opening())
    return body


def lid_print():   # top face down, as part = "lid" in the .scad
    return lid_in_place().rotate((0, 0, 0), (1, 0, 0), 180).translate((0, out_l, total_h))


def boards():      # reference bodies for the assembly
    x, y = pico_xy(pico_l, 0)
    pico = box(x, y, z_pcb, pico_w, pico_l, pico_t)
    for h in pico_holes:
        hx, hy = pico_xy(*h)
        pico = pico.cut(cyl(hx, hy, z_pcb - 1, pico_hole_d, 3))
    mod = box(mx0, my0, z_pcb, mod_l, mod_w, mod_t)
    jack = box(jack_x - jack_body[0] / 2, my1 - jack_body[1], z_pcb + mod_t, *jack_body)
    return pico, mod, jack


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, "step")
    os.makedirs(out, exist_ok=True)
    b, lp = base(), lid_print()
    cq.exporters.export(b, os.path.join(out, "receiver_base.step"))
    cq.exporters.export(lp, os.path.join(out, "receiver_lid.step"))
    asm = cq.Assembly(name="receiver_case")
    asm.add(b, name="receiver_base", color=cq.Color(0.23, 0.23, 0.23))
    asm.add(lid_in_place(), name="receiver_lid", color=cq.Color(0.62, 0.62, 0.62))
    pico, mod, jack = boards()
    asm.add(pico, name="pico2w_reference", color=cq.Color(0.12, 0.48, 0.24))
    asm.add(mod, name="pcm5102a_reference", color=cq.Color(0.42, 0.11, 0.6))
    asm.add(jack, name="jack_reference", color=cq.Color(0.13, 0.13, 0.13))
    asm.export(os.path.join(out, "assembly_receiver_case.step"))
    for name, s in (("base", b), ("lid", lp)):
        bb = s.val().BoundingBox()
        print(f"{name}: volume {s.val().Volume():.1f} mm3, box {bb.xmin:.2f}..{bb.xmax:.2f} x {bb.ymin:.2f}..{bb.ymax:.2f} x {bb.zmin:.2f}..{bb.zmax:.2f}")
