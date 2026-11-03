"""Generate the SKJÁR tray-mount case (top frame + bottom tray) as STEP.

Run with FreeCAD's command-line Python (1.1):
    "C:/Program Files/FreeCAD 1.1/bin/freecadcmd.exe" make_case.py

Coordinates match the board STEP exported by KiCad (kicad-cli pcb export step):
X = KiCad x, Y = -KiCad y, Z = 0 at the bottom of the board. Positions below are written in KiCad
coordinates (mm, y down) so they can be read straight off the board; the script flips y.
The board lies flat in this frame; the typing angle (ANGLE) is in the wedge of the tray, so to see the
case on a desk, rotate it -ANGLE degrees about X (front edge down).

Read from the board file (save the board first):
- the case outline (plan view): the lines, arcs, rectangles and circles on the user layer CASE_LAYER;
  without any, a rounded rectangle from the constants below;
- the board outline (Edge.Cuts), which sets the well for the board and the keys;
- the board screws: footprints whose reference is H<n> (mounting holes); without any, PCB_SCREWS;
- the positions of the display (DISP1) and of the parts the back ports and the floor holes are for (BACK_PORTS,
  FLOOR_HOLES).
"""
import math
import os
import re
import FreeCAD as App
import Part

HERE = os.path.dirname(os.path.abspath(__file__))
BOARD_FILE = os.path.join(HERE, "..", "pcb", "skjar.kicad_pcb")
CASE_LAYER = "User.1"
SNAP = 0.01                     # join outline end points closer than this
V = App.Vector

# ---- Board and display (from hardware/pcb/skjar.kicad_pcb) -------------------------------------------
BOARD = (34.131, 15.725, 262.731, 161.925)      # Edge.Cuts, x0 y0 x1 y1 (replaced by the board file's)
BOARD_TOP = 1.2
DISPLAY_FRAME = (-120.325, -34.075, 120.325, 34.075)   # glass pocket (the User.Drawings frame), relative to the display
GLASS_TOP = 11.76               # top of the cover glass (DISP1 model, Z set by hand)
GLASS_BOTTOM = 11.06
DISPLAY_CENTRE = (148.43125, 51.725)   # DISP1 (replaced by the board file's)
COVER = (-114.35, -33.05, 118.15, 33.05)       # back cover of the TOUCH-A, relative to the glass centre (DISP1 at 0 deg)
STUD_TIP = BOARD_TOP - 2.9      # bottom of the display studs (model offset -2.9 from the board top)
STUDS = [(-106.8, -28.5), (-106.8, 28.5), (111.2, -28.5), (111.2, 28.5),     # relative to the glass centre (0 deg)
         (-36.8, -24.5), (-36.8, 24.5), (21.2, -24.5), (21.2, 24.5)]
LOWEST_PART = -1.9              # bottom of the key sockets (the parts at the front edge)

# ---- Case ------------------------------------------------------------------------------------------
ANGLE = 3.5                     # typing angle, degrees
BEZEL = 3.75                    # top bezel around the glass (back, left, right)
FRONT_BEZEL = 3.75              # from the key well to the front edge
CORNER_R = 6.0                  # outer corner radius (plan view)
EDGE_FILLET = 1.0               # round-over of the top outer edge (made smaller if it does not fit)
GAP = 0.45                      # board to wall (the walls left, right and front are 7.0 mm)
FLOOR = 1.6                     # tray floor thickness
FLOOR_GAP = 0.6                 # lowest part to the inside of the floor (at the front)
SPLIT = 2.0                     # top frame / tray parting plane (between the board and the plate)
GLASS_LEDGE = 0.2               # the frame steps in under the overhanging glass, this far below it
COVER_GAP = 0.5                 # around the back cover of the display

# Display studs: bosses up from the floor, screwed from below (M2.5)
STUD_BOSS_D = 6.0               # fits inside the 6.5 mm board holes
STUD_SCREW_D = 2.7
STUD_HEAD_D = 5.0
STUD_SCREW_LEN = 6.0            # screw length from the boss top; the rest is counterbore

# Board (tray mount): bosses up to the bottom of the board, screwed from the top (M2 into heat-set inserts)
PCB_BOSS_D = 5.0
PCB_INSERT_D = 3.2
PCB_INSERT_DEPTH = 4.0
# KiCad coordinates. Picked clear of tracks, vias, the keys and the display holes (other parts are to be moved
# out of the way); to be adjusted on the board
PCB_SCREWS_DEFAULT = [(71.88, 23.98), (119.5, 35.5), (166.5, 38.0),
              (44.88, 51.98),
              (71.88, 79.97), (147.88, 78.72), (246.5, 79.0),
              (124.63, 133.22), (219.88, 133.22),
              (56.38, 151.97), (147.88, 151.97), (241.13, 151.97)]

# Top frame to tray: screws from below through the side walls into the frame (M2 into heat-set inserts)
FRAME_SCREW_D = 2.4
FRAME_HEAD_D = 4.2
FRAME_SCREW_LEN = 8.0           # from the head to the parting plane + into the frame
FRAME_INSERT_D = 3.2
FRAME_INSERT_DEPTH = 5.0
FRAME_SCREW_Y = [30.0, 95.0, 150.0]   # KiCad y, on the left and right walls (x is the middle of the wall)
FRAME_WALL_MIN = 0.8            # wall left on each side of an insert; a screw moves forward until it fits
FRAME_SCREW_PITCH = 15.0        # a screw that moved closer than this to another one is dropped

# Openings in the back wall: (part, x offset from the part, z centre, width, height, corner radius)
BACK_PORTS = [("J2", 0.0, -1.63, 12.5, 7.0, 3.0),            # USB-C
              ("J1", 0.0, -1.63, 12.5, 7.0, 3.0),            # USB-C
              ("J402", 0.0, -2.5, 7.0, 7.0, 3.5),            # audio
              ("J201", 0.0, -1.1, 14.5, 2.6, 0.8)]           # microSD (the body sticks out past the board edge)
# Holes in the floor, under a part: (part, diameter)
FLOOR_HOLES = [("SW101", 3.0)]                                # BOOTSEL



# ---- Board file ------------------------------------------------------------------------------------
def top_level_items(text, kind):
    """Yield the text of each top-level (kind ...) item of a KiCad board file (as KiCad formats it: the item
    closes with a line holding a single tab and ")")."""
    for m in re.finditer(r"^\t\(%s\b" % kind, text, re.M):
        line_end = text.index("\n", m.start())
        if text[m.start():line_end].rstrip().endswith(")") and text.count("(", m.start(), line_end) == \
                text.count(")", m.start(), line_end):
            yield text[m.start():line_end]
        else:
            yield text[m.start():text.index("\n\t)", m.start()) + 3]


def xy(item, key):
    m = re.search(r"\(%s (-?[\d.]+) (-?[\d.]+)" % key, item)
    return (float(m.group(1)), float(m.group(2))) if m else None


def P(p):
    return V(p[0], -p[1], 0)


def read_outline(text, layer=CASE_LAYER):
    """Edges (z = 0, case coordinates) drawn on a layer. End points closer than SNAP are joined, since KiCad
    leaves tiny gaps between lines and arcs drawn by hand."""
    edges = []
    ends = []

    def E(p):
        for q in ends:
            if math.dist(p, q) < SNAP:
                return P(q)
        ends.append(p)
        return P(p)

    for kind in ("gr_line", "gr_arc", "gr_rect", "gr_circle", "gr_poly"):
        for it in top_level_items(text, kind):
            if '(layer "%s")' % layer not in it:
                continue
            if kind == "gr_line":
                edges.append(Part.LineSegment(E(xy(it, "start")), E(xy(it, "end"))).toShape())
            elif kind == "gr_arc":
                edges.append(Part.Arc(E(xy(it, "start")), P(xy(it, "mid")), E(xy(it, "end"))).toShape())
            elif kind == "gr_rect":
                (a, b), (c, d) = xy(it, "start"), xy(it, "end")
                pts = [(a, b), (c, b), (c, d), (a, d), (a, b)]
                edges += [Part.LineSegment(P(pts[k]), P(pts[k + 1])).toShape() for k in range(4)]
            elif kind == "gr_circle":
                c, e = xy(it, "center"), xy(it, "end")
                edges.append(Part.makeCircle(math.dist(c, e), P(c)))
            else:
                pts = [(float(a), float(b)) for a, b in re.findall(r"\(xy (-?[\d.]+) (-?[\d.]+)\)", it)]
                pts.append(pts[0])
                edges += [Part.LineSegment(P(pts[k]), P(pts[k + 1])).toShape() for k in range(len(pts) - 1)]
    return edges


def read_mounting_holes(text):
    holes = []
    for it in top_level_items(text, "footprint"):
        if re.search(r'\(property "Reference" "H\d+"', it):
            holes.append(xy(it, "at"))
    return holes


def footprint_at(text, ref, angle=False):
    for it in top_level_items(text, "footprint"):
        if re.search(r'\(property "Reference" "%s"' % re.escape(ref), it):
            if angle:
                m = re.search(r"\(at -?[\d.]+ -?[\d.]+ (-?[\d.]+)\)", it)
                return float(m.group(1)) if m else 0.0
            return xy(it, "at")
    raise SystemExit("no footprint %s on the board" % ref)


def turned(p, deg):
    """A point relative to the display turned like the footprint (KiCad angle, y down)."""
    a = math.radians(deg)
    return (p[0] * math.cos(a) + p[1] * math.sin(a), -p[0] * math.sin(a) + p[1] * math.cos(a))


def turned_rect(r, deg):
    pts = [turned(p, deg) for p in ((r[0], r[1]), (r[2], r[3]))]
    return (min(p[0] for p in pts), min(p[1] for p in pts), max(p[0] for p in pts), max(p[1] for p in pts))


with open(BOARD_FILE, encoding="utf-8") as fh:
    board_text = fh.read()
OUTLINE_EDGES = read_outline(board_text)
PCB_SCREWS = read_mounting_holes(board_text) or PCB_SCREWS_DEFAULT
_edge = read_outline(board_text, "Edge.Cuts")
if _edge:
    _bb = Part.Compound(_edge).BoundBox
    BOARD = (_bb.XMin, -_bb.YMax, _bb.XMax, -_bb.YMin)
DISPLAY_CENTRE = footprint_at(board_text, "DISP1")
DISPLAY_ANGLE = footprint_at(board_text, "DISP1", angle=True)   # the display can be turned (180: connector on the right)
STUDS = [turned(p, DISPLAY_ANGLE) for p in STUDS]
COVER = turned_rect(COVER, DISPLAY_ANGLE)
DISPLAY_FRAME = turned_rect(DISPLAY_FRAME, DISPLAY_ANGLE)
DISPLAY_FRAME = (DISPLAY_CENTRE[0] + DISPLAY_FRAME[0], DISPLAY_CENTRE[1] + DISPLAY_FRAME[1],
                 DISPLAY_CENTRE[0] + DISPLAY_FRAME[2], DISPLAY_CENTRE[1] + DISPLAY_FRAME[3])
PARTS = {ref: footprint_at(board_text, ref) for ref in [p[0] for p in BACK_PORTS + FLOOR_HOLES]}
print("board: %.2f..%.2f x %.2f..%.2f; display at %.3f, %.3f, %g deg" % (BOARD[0], BOARD[2], BOARD[1], BOARD[3],
                                                                   DISPLAY_CENTRE[0], DISPLAY_CENTRE[1], DISPLAY_ANGLE))
print("outline: %s; board screws: %d (%s)" % ("%d edges on %s" % (len(OUTLINE_EDGES), CASE_LAYER) if OUTLINE_EDGES
                                              else "default rounded rectangle", len(PCB_SCREWS),
                                              "H<n> footprints" if PCB_SCREWS is not PCB_SCREWS_DEFAULT else "default"))

# ---- Derived ---------------------------------------------------------------------------------------
if OUTLINE_EDGES:
    _bb = Part.Compound(OUTLINE_EDGES).BoundBox
    X0, X1, Y0, Y1 = _bb.XMin, _bb.XMax, -_bb.YMax, -_bb.YMin
else:
    X0 = DISPLAY_FRAME[0] - BEZEL
    X1 = DISPLAY_FRAME[2] + BEZEL
    Y0 = DISPLAY_FRAME[1] - BEZEL
    Y1 = BOARD[3] + GAP + FRONT_BEZEL
TAN = math.tan(math.radians(ANGLE))
# Inside of the floor clears the lowest part at the front edge of the board and drops by the angle towards the back
FLOOR_IN_AT_BOARD_FRONT = LOWEST_PART - FLOOR_GAP


def floor_in(y):
    return FLOOR_IN_AT_BOARD_FRONT - (BOARD[3] - y) * TAN


def bottom(y):
    return floor_in(y) - FLOOR


TOP = GLASS_TOP
DEEP = bottom(Y0) - 5.0          # below everything


def rrect(x0, y0, x1, y1, r, z0, z1):
    """Rounded rectangle prism from KiCad coordinates."""
    w, h = x1 - x0, y1 - y0
    r = min(r, w / 2 - 0.01, h / 2 - 0.01)
    box = Part.makeBox(w, h, z1 - z0, V(x0, -y1, z0))
    if r <= 0:
        return box
    edges = [e for e in box.Edges if abs(e.Vertexes[0].Point.z - e.Vertexes[1].Point.z) > 1e-6]
    return box.makeFillet(r, edges)


def cyl(x, y, d, z0, z1):
    return Part.makeCylinder(d / 2, z1 - z0, V(x, -y, z0))


def wedge(z_of_y, z_top, x0=-10, x1=320):
    """Solid between the plane z = z_of_y(y) and z_top (KiCad y)."""
    ya, yb = Y0 - 20, Y1 + 20
    pts = [V(x0, -ya, z_of_y(ya)), V(x0, -yb, z_of_y(yb)), V(x0, -yb, z_top), V(x0, -ya, z_top)]
    face = Part.Face(Part.makePolygon(pts + [pts[0]]))
    return face.extrude(V(x1 - x0, 0, 0))


def slot_y(x, z, w, h, r, y0, y1):
    """Rounded opening through a wall that faces y (KiCad), centred at x, z."""
    r = min(r, w / 2 - 0.01, h / 2 - 0.01)
    box = Part.makeBox(w, y1 - y0, h, V(x - w / 2, -y1, z - h / 2))
    if r <= 0:
        return box
    edges = [e for e in box.Edges if abs(e.Vertexes[0].Point.y - e.Vertexes[1].Point.y) > 1e-6]
    return box.makeFillet(r, edges)


cx, cy = DISPLAY_CENTRE
if OUTLINE_EDGES:
    _wire = Part.Wire(Part.__sortEdges__(OUTLINE_EDGES))
    if not _wire.isClosed():
        print("the outline on %s is not closed" % CASE_LAYER)      # freecadcmd does not print the exit message
        raise SystemExit(1)
    outer = Part.Face(_wire).extrude(V(0, 0, TOP - DEEP))
    outer.translate(V(0, 0, DEEP))
else:
    outer = rrect(X0, Y0, X1, Y1, CORNER_R, DEEP, TOP)
board_well = (BOARD[0] - GAP, BOARD[1] - GAP, BOARD[2] + GAP, BOARD[3] + GAP)
cover_well = (cx + COVER[0] - COVER_GAP, cy + COVER[1] - COVER_GAP, cx + COVER[2] + COVER_GAP, cy + COVER[3] + COVER_GAP)


def frame_screws():
    """In the middle of the left and right walls (outline to the board or the display cover). A screw where the
    wall is too thin for the insert moves forward until it fits."""
    r = FRAME_INSERT_D / 2 + FRAME_WALL_MIN
    screws = []
    for side in (0, 1):
        for y0 in FRAME_SCREW_Y:
            y = y0
            while y < Y1 - CORNER_R:
                cover = cover_well[1] - r < y < cover_well[3] + r
                if side == 0:
                    inner = min(board_well[0], cover_well[0]) if cover else board_well[0]
                else:
                    inner = max(board_well[2], cover_well[2]) if cover else board_well[2]
                outer_x = X0 if side == 0 else X1
                if abs(inner - outer_x) >= 2 * r:
                    break
                y += 0.5
            else:
                print("frame screw at y %.0f (%s) does not fit" % (y0, "left" if side == 0 else "right"))
                continue
            name = "left" if side == 0 else "right"
            if any(abs(y - sy) < FRAME_SCREW_PITCH for sx, sy in screws if (sx < (X0 + X1) / 2) == (side == 0)):
                print("frame screw at y %.0f (%s) dropped: too close to another one" % (y0, name))
                continue
            if y != y0:
                print("frame screw at y %.0f (%s) moved to y %.1f: the wall is too thin" % (y0, name, y))
            screws.append(((inner + outer_x) / 2, y))
    return screws


FRAME_SCREWS = frame_screws()

# ---- Top frame -------------------------------------------------------------------------------------
frame = outer.common(Part.makeBox(1000, 1000, TOP - SPLIT, V(-300, -700, SPLIT)))
ledge = GLASS_BOTTOM - GLASS_LEDGE
cut = rrect(*board_well, 2.3, SPLIT - 1, ledge)                       # board, plate and keys
cut = cut.fuse(rrect(*cover_well, 1.0, SPLIT - 1, ledge))             # display body under the glass
cut = cut.fuse(rrect(*DISPLAY_FRAME, 2.0, ledge, TOP + 1))            # glass pocket
cut = cut.fuse(rrect(board_well[0], DISPLAY_FRAME[3] - 1.0, board_well[2], board_well[3], 2.3, ledge, TOP + 1))   # key well
frame = frame.cut(cut)
for x, y in FRAME_SCREWS:
    frame = frame.cut(cyl(x, y, FRAME_INSERT_D, SPLIT - 1, SPLIT + FRAME_INSERT_DEPTH))
frame = frame.removeSplitter()
if EDGE_FILLET > 0:
    top_face = max((f for f in frame.Faces if abs(f.BoundBox.ZMin - TOP) < 1e-6 and abs(f.BoundBox.ZMax - TOP) < 1e-6),
                   key=lambda f: f.Area)
    outer_top = top_face.OuterWire.Edges
    for radius in (EDGE_FILLET, EDGE_FILLET * 0.8, EDGE_FILLET * 0.6, EDGE_FILLET * 0.4):
        try:
            filleted = frame.makeFillet(radius, outer_top)
        except Exception:
            continue
        if filleted.isValid():
            frame = filleted
            if radius != EDGE_FILLET:
                print("top edge fillet reduced to %.2f mm" % radius)
            break
    else:   # keep the sharp edge rather than fail
        print("top edge fillet skipped")

# ---- Tray ------------------------------------------------------------------------------------------
tray = outer.common(wedge(bottom, SPLIT))
cavity = rrect(*board_well, 2.3, DEEP, SPLIT + 1).common(wedge(floor_in, SPLIT + 1))
tray = tray.cut(cavity)
tray = tray.cut(rrect(*cover_well, 1.0, BOARD_TOP - 0.2, SPLIT + 1))   # the display's back cover hangs past the board edge
solid_outline = wedge(bottom, SPLIT)
bosses = []
for sx, sy in STUDS:
    x, y = cx + sx, cy + sy
    bosses.append(cyl(x, y, STUD_BOSS_D, DEEP, STUD_TIP))
for x, y in PCB_SCREWS:
    bosses.append(cyl(x, y, PCB_BOSS_D, DEEP, 0.0))
for b in bosses:
    tray = tray.fuse(b.common(solid_outline))
for sx, sy in STUDS:
    x, y = cx + sx, cy + sy
    tray = tray.cut(cyl(x, y, STUD_SCREW_D, DEEP, STUD_TIP + 1))
    tray = tray.cut(cyl(x, y, STUD_HEAD_D, DEEP, STUD_TIP - STUD_SCREW_LEN))
for x, y in PCB_SCREWS:
    tray = tray.cut(cyl(x, y, PCB_INSERT_D, -PCB_INSERT_DEPTH, 1.0))
for x, y in FRAME_SCREWS:
    tray = tray.cut(cyl(x, y, FRAME_SCREW_D, DEEP, SPLIT + 1))
    tray = tray.cut(cyl(x, y, FRAME_HEAD_D, DEEP, SPLIT - (FRAME_SCREW_LEN - FRAME_INSERT_DEPTH + 1.0)))
for ref, dx, z, w, h, r in BACK_PORTS:
    tray = tray.cut(slot_y(PARTS[ref][0] + dx, z, w, h, r, Y0 - 1, board_well[1] + 1))
for ref, d in FLOOR_HOLES:
    tray = tray.cut(cyl(PARTS[ref][0], PARTS[ref][1], d, DEEP, SPLIT))
tray = tray.removeSplitter()

for shape, name in ((frame, "skjar-case-top.step"), (tray, "skjar-case-tray.step")):
    path = os.path.join(HERE, name)
    shape.exportStep(path)
    bb = shape.BoundBox
    print("%s: valid=%s, %.1f x %.1f x %.1f mm, z %.2f..%.2f" % (name, shape.isValid(), bb.XLength, bb.YLength,
                                                                bb.ZLength, bb.ZMin, bb.ZMax))
print("thickness: front %.2f mm, back %.2f mm" % (TOP - bottom(Y1), TOP - bottom(Y0)))
