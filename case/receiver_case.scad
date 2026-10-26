// =====================================================================
// Case for the C64 audio receiver: Raspberry Pi Pico 2 W + PCM5102A module ("GY-PCM5102", 32 x 17 mm,
// 6-pin header SCK BCK DIN LCK GND VIN on a short edge, 3.5 mm jack at the other end opening sideways).
// Both boards have their headers soldered in pointing DOWN; DuPont plugs on the pins, the wires run
// in the cable room below the boards. USB socket and jack both face the BACK (towards the stereo).
// Gehaeuse fuer den C64-Audioempfaenger: Pico 2 W + PCM5102A-Modul, Stiftleisten nach UNTEN,
// DuPont-Kabel im Kabelraum unter den Platinen, USB- und Klinkenbuchse hinten.
//
//   BASE: the Pico rests on two posts (pegs through its front holes) and a ledge on the back wall;
//         the module on a ledge on the back wall and a rib along its front edge. Jack opening as a
//         slot open to the top, so the module drops in from above with its jack sticking out.
//   LID:  slip-on cap like the light sensor's: skirt over the thinner upper part of the walls with a
//         snap bead, inner rim, finger notch; a tongue closes the jack slot; press posts hold both
//         boards down (on the Pico's front holes, its USB socket and the top of the jack).
//
// The PCM5102A dimensions are from the seller's drawing - MEASURE your module and adjust the values
// marked (!) before printing. / Modulmasse vom Haendler - nachmessen und die mit (!) markierten Werte anpassen.
//
// openscad -D 'part="base"' -o receiver_base.stl receiver_case.scad
// part = "assembly" | "base" | "lid" | "section" | "section_jack"
// =====================================================================

part = "assembly";   // (set to "x" by the preview helpers)

/* ---- Raspberry Pi Pico 2 W (official drawing) ---- */
pico_l = 51;   pico_w = 21;   pico_t = 1.0;
pico_holes = [[2, 4.8], [2, 16.2], [49, 4.8], [49, 16.2]];   // [along from the USB end, across]
pico_hole_d = 2.1;
pico_row = 1.61;            // pin rows from the long edges
usb_over = 1.3;             // USB socket sticks out over the board edge
usb_w = 8.0;  usb_h = 2.8;  // socket size (on top of the board)

/* ---- PCM5102A module (!) ---- */
mod_l = 32;   mod_w = 17;   mod_t = 1.6;     // (!) seller's drawing: 32 x 17 mm
mod_row = 1.3;              // (!) header row from the short edge
jack_u = 26;                // (!) jack axis from the header edge, along the module
jack_d = 6.0;               // (!) diameter of the jack's front barrel
jack_z = 2.6;               // (!) barrel centre above the module top
jack_over = 2.0;            // (!) how far the barrel sticks out over the long edge
jack_body = [9, 12, 5.5];   // (!) jack body: along the module, across, height

/* ---- headers + DuPont plugs ---- */
pitch = 2.54;
hdr_body = 2.5;             // header plastic below the boards
pin_below = 6.0;            // pins below the plastic
plug_len = 14;              // DuPont housing
bend = 7;                   // room below the plugs for the wires to bend

/* ---- case ---- */
wall = 2.0;   floor_t = 2.0;   top_t = 1.6;
play = 0.6;                 // around the boards
gap = 6;                    // between Pico and module
usb_clear = 0.3;            // USB socket front to the inner wall
jack_clear = 0.3;           // module long edge to the inner wall
above = 1.0;                // room above the jack body
peg_d = pico_hole_d - 0.5;  // printed pegs come out thicker
post_d = 3.4;               // posts under the Pico's front holes (clear of the header plastic)
lid_fit = 0.15;  lip_t = 1.2;  lip_h = 1.6;
skirt_h = 4.0;  step = 1.0;  skirt_fit = 0.1;
snap = 0.35;  snap_len = 0.6;
r_out = 2.0;
usb_hole = [12, 7.5];       // opening for the USB plug's moulding
feet = true;                // 4 shallow recesses for 8 mm rubber feet

// ---------------------------------------------------------------------
in_w = play + pico_w + gap + mod_l + play;
in_l = pico_l + usb_over - usb_clear + play;
out_w = in_w + 2 * wall;
out_l = in_l + 2 * wall;
z_pcb = floor_t + hdr_body + plug_len + bend;     // underside of both boards
z_rim = z_pcb + mod_t + jack_body[2] + above;     // wall top = lid underside
z_step = z_rim - skirt_h;
z_bead = z_step + skirt_h * 0.4;
total_h = z_rim + top_t;
echo(str("Receiver case outer size: ", out_w, " x ", out_l, " x ", total_h, " mm"));

// board positions (world): Pico along y with the USB end at the back, module along x at the back right
px0 = wall + play;                                 // Pico left edge (pins 1..20)
py_usb = wall + in_l - usb_over + usb_clear;       // Pico USB end (board edge)
function pico_xy(p) = [px0 + p[1], py_usb - p[0]]; // [along, across] -> world
mx0 = wall + play + pico_w + gap;                  // module header edge
my1 = wall + in_l - jack_clear;                    // module back long edge (jack side)
my0 = my1 - mod_w;
jack_x = mx0 + jack_u;
z_jack = z_pcb + mod_t + jack_z;
z_usb = z_pcb + pico_t + usb_h / 2;

$fn = 48;
module rbox(x, y, z, r = r_out) { translate([r, r, 0]) linear_extrude(z) offset(r = r) square([x - 2 * r, y - 2 * r]); }
module rrect(inset, z0, h) {
    r = max(r_out - inset, 0.2);
    translate([inset + r, inset + r, z0]) linear_extrude(h) offset(r = r) square([out_w - 2 * inset - 2 * r, out_l - 2 * inset - 2 * r]);
}
module ridges(d, half) {       // triangular ridge on the four faces of the thinned wall (snap bead / groove)
    lx = (out_w - 2 * step) * snap_len;  ly = (out_l - 2 * step) * snap_len;
    x0 = (out_w - lx) / 2;  y0 = (out_l - ly) / 2;
    hull() { translate([x0, step, z_bead - half]) cube([lx, 0.01, 2 * half]); translate([x0, step - d, z_bead - 0.005]) cube([lx, d, 0.01]); }
    hull() { translate([x0, out_l - step - 0.01, z_bead - half]) cube([lx, 0.01, 2 * half]); translate([x0, out_l - step, z_bead - 0.005]) cube([lx, d, 0.01]); }
    hull() { translate([step, y0, z_bead - half]) cube([0.01, ly, 2 * half]); translate([step - d, y0, z_bead - 0.005]) cube([d, ly, 0.01]); }
    hull() { translate([out_w - step - 0.01, y0, z_bead - half]) cube([0.01, ly, 2 * half]); translate([out_w - step, y0, z_bead - 0.005]) cube([d, ly, 0.01]); }
}
module rounded_rect_y(w, h, d) {   // opening through a wall facing +y: w wide, h high, d deep (centred on the origin)
    rotate([-90, 0, 0]) linear_extrude(d) offset(r = 1.5) square([w - 3, h - 3], center = true);
}
module jack_hole(extra = 0) {      // through the back wall along +y
    translate([jack_x, wall + in_l - 1, z_jack]) rotate([-90, 0, 0]) cylinder(d = jack_d + 0.8 + extra, h = wall + 2);
    translate([jack_x, wall + in_l + wall - 0.8, z_jack]) rotate([-90, 0, 0]) cylinder(d1 = jack_d + 0.8, d2 = jack_d + 3.2, h = 0.81);   // chamfer
}
slot_w = jack_d + 0.8;

module base() {
    difference() {
        union() {
            difference() {
                rbox(out_w, out_l, z_rim);
                translate([wall, wall, floor_t]) cube([in_w, in_l, z_rim]);
                // upper part of the walls thinner: the lid skirt sits there, flush with the outside
                difference() {
                    translate([-1, -1, z_step]) cube([out_w + 2, out_l + 2, skirt_h + 1]);
                    rrect(step, z_step - 1, skirt_h + 3);
                }
            }
            if (snap > 0) ridges(snap, 0.4);             // snap bead (the openings below cut through it)
            // Pico: posts under the front holes, slim at the top (header plastic beside them), wider at the foot
            for (h = [pico_holes[2], pico_holes[3]]) translate(pico_xy(h)) {
                cylinder(d1 = post_d + 4, d2 = post_d, h = z_pcb - hdr_body - pin_below - 1);
                cylinder(d = post_d, h = z_pcb);
                translate([0, 0, z_pcb - 0.01]) cylinder(d = peg_d, h = pico_t + 0.6);
                translate([0, 0, z_pcb + pico_t + 0.59]) cylinder(d1 = peg_d, d2 = peg_d - 0.6, h = 0.5);
            }
            // Pico: ledge on the back wall under the USB end, between the pin rows
            translate([px0 + 4, py_usb - 1.5, 0]) cube([pico_w - 8, wall + in_l - py_usb + 1.5, z_pcb]);
            // module: ledge on the back wall (clear of the jack's solder legs) and a rib along its front edge
            translate([mx0 + 5, my1 - 1.2, 0]) cube([jack_x - jack_body[0] / 2 - 1 - (mx0 + 5), wall + in_l - my1 + 1.2, z_pcb]);
            translate([mx0 + 5, my0 - 1.2, 0]) cube([mod_l - 7, 2.0, z_pcb]);
            // module: low side guides at both short ends (it cannot slide along x)
            translate([mx0 + mod_l + 0.2, my0 + 3, z_pcb - 3]) cube([wall + in_w - (mx0 + mod_l + 0.2), mod_w - 6, 3 + mod_t + 1]);
        }
        // USB opening
        translate([px0 + pico_w / 2, wall + in_l - 1, z_usb]) rounded_rect_y(usb_hole[0], usb_hole[1], wall + 2);
        // jack: round hole, open to the top (slot) so the module can be dropped in
        jack_hole();
        translate([jack_x - slot_w / 2, wall + in_l - 1, z_jack]) cube([slot_w, wall + 2, z_rim]);
        // rubber feet
        if (feet) for (x = [8, out_w - 8]) for (y = [8, out_l - 8]) translate([x, y, -1]) cylinder(d = 8.6, h = 1.6);
    }
}

module lid() {   // modelled in place; part = "lid" turns it over for printing
    difference() {
        union() {
            translate([0, 0, z_rim]) rbox(out_w, out_l, top_t);
            difference() {     // skirt over the thinned base wall
                translate([0, 0, z_step + 0.2]) rbox(out_w, out_l, skirt_h - 0.2 + 0.01);
                rrect(step - skirt_fit, z_step, skirt_h + 1);
            }
            translate([0, 0, z_rim - lip_h]) difference() {    // rim inside the walls
                translate([wall + lid_fit, wall + lid_fit, 0]) cube([in_w - 2 * lid_fit, in_l - 2 * lid_fit, lip_h + 0.01]);
                translate([wall + lid_fit + lip_t, wall + lid_fit + lip_t, -1]) cube([in_w - 2 * lid_fit - 2 * lip_t, in_l - 2 * lid_fit - 2 * lip_t, lip_h + 2]);
            }
            // tongue that closes the jack slot down to the barrel
            translate([jack_x - slot_w / 2 + lid_fit, wall + in_l - 0.01 - lip_t, z_jack]) cube([slot_w - 2 * lid_fit, wall + lip_t, z_rim - z_jack + 0.01]);
            // press tubes on the Pico's front holes, a block on its USB socket, a post on the jack
            for (h = [pico_holes[2], pico_holes[3]]) translate(pico_xy(h)) translate([0, 0, z_pcb + pico_t]) difference() {
                cylinder(d = post_d + 0.6, h = z_rim - z_pcb - pico_t + 0.01);
                translate([0, 0, -1]) cylinder(d = peg_d + 0.8, h = 3);
            }
            translate([px0 + pico_w / 2 - usb_w / 2, py_usb - 4, z_pcb + pico_t + usb_h]) cube([usb_w, 4 - 0.01, z_rim - z_pcb - pico_t - usb_h + 0.01]);
            translate([jack_x - 2, my1 - jack_body[1] + 2, z_pcb + mod_t + jack_body[2]]) cube([4, 6, above + 0.01]);
            // press rib on the module's front edge (the row of unused pads)
            translate([mx0 + 6, my0 + 0.1, z_pcb + mod_t]) cube([mod_l - 12, 1.0, z_rim - z_pcb - mod_t + 0.01]);
        }
        if (snap > 0) ridges(snap + 0.1, 0.5);
        translate([out_w / 2 - 4, -1, z_step + 0.19]) cube([8, step + 1, 0.8]);       // finger notch
        translate([px0 + pico_w / 2, wall + in_l - 1, z_usb]) rounded_rect_y(usb_hole[0], usb_hole[1], wall + 2);
        jack_hole();
    }
}

module boards() {   // reference only
    color("#1e7a3c") difference() {
        translate([px0, py_usb - pico_l, z_pcb]) cube([pico_w, pico_l, pico_t]);
        for (h = pico_holes) translate(pico_xy(h)) translate([0, 0, z_pcb - 1]) cylinder(d = pico_hole_d, h = 3);
    }
    color("#b0b0b0") translate([px0 + pico_w / 2 - usb_w / 2, py_usb - 4.3, z_pcb + pico_t]) cube([usb_w, 4.3 + usb_over, usb_h]);
    color("#444") translate([px0 + 6, py_usb - 30, z_pcb + pico_t]) cube([9, 9, 1.0]);              // RP2350
    color("#ccc") translate([px0 + 5, py_usb - 46, z_pcb + pico_t]) cube([11, 11, 1.4]);            // WLAN shield
    for (side = [0, 1]) for (i = [0 : 19]) {
        p = pico_xy([1.37 + i * pitch, side ? pico_w - pico_row : pico_row]);
        color("#222") translate([p[0] - 1.27, p[1] - 1.27, z_pcb - hdr_body]) cube([2.54, 2.54, hdr_body]);
        color("#c9a227") translate([p[0] - 0.32, p[1] - 0.32, z_pcb - hdr_body - pin_below]) cube([0.64, 0.64, pin_below + hdr_body + pico_t + 1]);
    }
    // DuPont plugs on the pins in use: VSYS 39, GND 38, GP20 26, GP21 27, GND 28, GP22 29 (right row: pin k at 1.37 + (40 - k) * 2.54)
    cols = ["#c62828", "#212121", "#1565c0", "#2e7d32", "#212121", "#ff9800"];
    used = [39, 38, 26, 27, 28, 29];
    for (j = [0 : 5]) let (p = pico_xy([1.37 + (40 - used[j]) * pitch, pico_w - pico_row]))
        color(cols[j]) translate([p[0] - 1.25, p[1] - 1.25, z_pcb - hdr_body - plug_len]) cube([2.5, 2.5, plug_len]);
    // module
    color("#6a1b9a") translate([mx0, my0, z_pcb]) cube([mod_l, mod_w, mod_t]);
    color("#333") translate([mx0 + 10, my0 + 5, z_pcb + mod_t]) cube([7, 5, 1.2]);
    color("#222") translate([jack_x - jack_body[0] / 2, my1 - jack_body[1], z_pcb + mod_t]) cube([jack_body[0], jack_body[1], jack_body[2]]);
    color("#222") translate([jack_x, my1 - 1, z_jack]) rotate([-90, 0, 0]) cylinder(d = jack_d, h = jack_over + 1);
    for (i = [0 : 5]) let (x = mx0 + mod_row, y = my0 + mod_w / 2 - 2.5 * pitch + i * pitch) {
        color("#222") translate([x - 1.27, y - 1.27, z_pcb - hdr_body]) cube([2.54, 2.54, hdr_body]);
        color(cols[i]) translate([x - 1.25, y - 1.25, z_pcb - hdr_body - plug_len]) cube([2.5, 2.5, plug_len]);
    }
}

if (part == "base") base();
if (part == "lid") translate([0, out_l, total_h]) rotate([180, 0, 0]) lid();
if (part == "assembly") { color("#3a3a3a") base(); boards(); color("#9e9e9e", 0.9) translate([0, 0, 20]) lid(); }
if (part == "section") difference() {        // cut along the Pico's front holes
    union() { color("#3a3a3a") base(); boards(); color("#9e9e9e") lid(); }
    translate([-1, pico_xy(pico_holes[2])[1], -1]) cube([out_w + 2, out_l, 80]);
}
if (part == "section_jack") difference() {   // cut through the jack axis
    union() { color("#3a3a3a") base(); boards(); color("#9e9e9e") lid(); }
    translate([jack_x, -1, -1]) cube([out_w, out_l + 2, 80]);
}
