#!/usr/bin/env python3
"""Generate the demo props in assets/demos/ (see assets/demos/README.md)."""
import json
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / 'assets/demos'
TAU = 2 * math.pi


def write_glb(path, document, binary):
    metadata = json.dumps(document, separators=(',', ':')).encode()
    metadata += b' ' * (-len(metadata) % 4)
    binary += b'\0' * (-len(binary) % 4)
    chunks = (struct.pack('<II', len(metadata), 0x4E4F534A) + metadata +
              struct.pack('<II', len(binary), 0x004E4942) + binary)
    path.write_bytes(struct.pack('<III', 0x46546C67, 2, 12 + len(chunks)) + chunks)


def add(a, b): return tuple(x + y for x, y in zip(a, b))
def sub(a, b): return tuple(x - y for x, y in zip(a, b))
def mul(a, s): return tuple(x * s for x in a)
def dot(a, b): return sum(x * y for x, y in zip(a, b))
def cross(a, b): return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])
def unit(a): return mul(a, 1 / math.sqrt(dot(a, a)))


def basis(axis):
    """Two unit vectors perpendicular to axis and to each other."""
    axis = unit(axis)
    helper = (1, 0, 0) if abs(axis[0]) < .9 else (0, 1, 0)
    u = unit(cross(axis, helper))
    return u, cross(axis, u)


class Model:
    """Triangles grouped by material. Winding follows the vertex normals."""

    def __init__(self):
        self.parts = {}

    def part(self, material):
        return self.parts.setdefault(material, ([], [], []))

    def vertex(self, material, position, normal):
        positions, normals, _ = self.part(material)
        positions.append(tuple(position))
        normals.append(unit(normal))
        return len(positions) - 1

    def triangle(self, material, a, b, c):
        positions, normals, indices = self.part(material)
        face = cross(sub(positions[b], positions[a]), sub(positions[c], positions[a]))
        if dot(face, face) < 1e-20:
            return
        if dot(face, add(add(normals[a], normals[b]), normals[c])) < 0:
            b, c = c, b
        indices.extend((a, b, c))

    def grid(self, material, rows, closed_columns=True):
        """Triangulate rows of (position, normal) lists of equal length."""
        ids = [[self.vertex(material, p, n) for p, n in row] for row in rows]
        for top, bottom in zip(ids, ids[1:]):
            count = len(top) if closed_columns else len(top) - 1
            for i in range(count):
                j = (i + 1) % len(top)
                self.triangle(material, top[i], bottom[i], bottom[j])
                self.triangle(material, top[i], bottom[j], top[j])

    def box(self, material, low, high):
        for axis in range(3):
            for side in (0, 1):
                normal = [0, 0, 0]
                normal[axis] = 1 if side else -1
                u, v = (axis + 1) % 3, (axis + 2) % 3
                corners = []
                for cu, cv in ((0, 0), (1, 0), (1, 1), (0, 1)):
                    point = [0, 0, 0]
                    point[axis] = high[axis] if side else low[axis]
                    point[u] = high[u] if cu else low[u]
                    point[v] = high[v] if cv else low[v]
                    corners.append(self.vertex(material, point, normal))
                self.triangle(material, corners[0], corners[1], corners[2])
                self.triangle(material, corners[0], corners[2], corners[3])

    def cylinder(self, material, start, end, radius, segments=16):
        axis = unit(sub(end, start))
        u, v = basis(axis)
        ring = [add(mul(u, math.cos(TAU * i / segments)), mul(v, math.sin(TAU * i / segments)))
                for i in range(segments)]
        self.grid(material, [[(add(center, mul(n, radius)), n) for n in ring]
                             for center in (start, end)])
        for center, normal in ((start, mul(axis, -1)), (end, axis)):
            middle = self.vertex(material, center, normal)
            rim = [self.vertex(material, add(center, mul(n, radius)), normal) for n in ring]
            for i in range(segments):
                self.triangle(material, middle, rim[i], rim[(i + 1) % segments])

    def sphere(self, material, center, radius, rows=16, columns=32):
        grid = []
        for row in range(rows + 1):
            theta = math.pi * row / rows
            grid.append([])
            for column in range(columns):
                phi = TAU * column / columns
                normal = (math.sin(theta) * math.cos(phi), math.cos(theta),
                          math.sin(theta) * math.sin(phi))
                grid[-1].append((add(center, mul(normal, radius)), normal))
        self.grid(material, grid)

    def torus(self, material, center, radius, tube, segments=64, sides=16):
        """A ring around the vertical axis."""
        rows = []
        for side in range(sides + 1):
            alpha = TAU * side / sides
            rows.append([])
            for segment in range(segments):
                phi = TAU * segment / segments
                outward = (math.cos(phi), 0, math.sin(phi))
                normal = add(mul(outward, math.cos(alpha)), (0, math.sin(alpha), 0))
                rows[-1].append((add(center, add(mul(outward, radius), mul(normal, tube))), normal))
        self.grid(material, rows)

    def save(self, name, materials):
        binary, views, accessors, primitives, document_materials = b'', [], [], [], []
        for material, (positions, normals, indices) in self.parts.items():
            positions_bytes = struct.pack('<' + 'f' * (3 * len(positions)),
                                          *(x for p in positions for x in p))
            normals_bytes = struct.pack('<' + 'f' * (3 * len(normals)),
                                        *(x for n in normals for x in n))
            wide = len(positions) > 65535
            indices_bytes = struct.pack('<' + ('I' if wide else 'H') * len(indices), *indices)
            first = len(accessors)
            for data in (positions_bytes, normals_bytes, indices_bytes):
                views.append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(data)})
                binary += data + b'\0' * (-len(data) % 4)
            accessors.extend([
                {'bufferView': first, 'componentType': 5126, 'count': len(positions), 'type': 'VEC3',
                 'min': [min(p[i] for p in positions) for i in range(3)],
                 'max': [max(p[i] for p in positions) for i in range(3)]},
                {'bufferView': first + 1, 'componentType': 5126, 'count': len(normals), 'type': 'VEC3'},
                {'bufferView': first + 2, 'componentType': 5125 if wide else 5123,
                 'count': len(indices), 'type': 'SCALAR'}])
            color, metallic, roughness = materials[material]
            primitives.append({'attributes': {'POSITION': first, 'NORMAL': first + 1},
                               'indices': first + 2, 'material': len(document_materials)})
            document_materials.append({'name': material, 'pbrMetallicRoughness': {
                'baseColorFactor': [*color, 1], 'metallicFactor': metallic,
                'roughnessFactor': roughness}})
        document = {
            'asset': {'version': '2.0', 'generator': 'Pocketspace demo models'},
            'buffers': [{'byteLength': len(binary)}], 'bufferViews': views,
            'accessors': accessors, 'materials': document_materials,
            'meshes': [{'name': name, 'primitives': primitives}],
            'nodes': [{'name': name, 'mesh': 0}], 'scenes': [{'nodes': [0]}], 'scene': 0,
        }
        write_glb(OUTPUT / f'{name}.glb', document, binary)


def band(model, material, radius, axis, cos_angle, width, segments=96):
    """A thin strip on a sphere along the circle at cos_angle from axis."""
    u, v = basis(axis)
    theta = math.acos(cos_angle)
    rows = []
    for edge in (-width / 2, width / 2):
        angle = theta + edge / radius
        rows.append([])
        for i in range(segments):
            phi = TAU * i / segments
            normal = add(mul(unit(axis), math.cos(angle)),
                         mul(add(mul(u, math.cos(phi)), mul(v, math.sin(phi))), math.sin(angle)))
            rows[-1].append((mul(normal, radius), normal))
    model.grid(material, rows)


# Basketball: 0.18 m ball with dark seams, the meridian and equator great
# circles plus two curved seams that cross the equator.
ball = Model()
ball.sphere('leather', (0, 0, 0), .09, 32, 64)
for axis, cos_angle in (((1, 0, 0), 0), ((0, 1, 0), 0), ((1, 0, 0), .7), ((1, 0, 0), -.7)):
    band(ball, 'seam', .0903, axis, cos_angle, .004)
ball.save('Basketball', {'leather': ((.85, .32, .08), 0, .75), 'seam': ((.05, .04, .04), 0, .6)})

# Mini hoop in a shared frame: floor at y 0 under the pole, +Z toward the player.
# Board 0.6 x 0.4 m, rim 0.30 m inside diameter with its center 1.6 m up.
paint = {'metal': ((.2, .21, .23), .6, .45)}
post = Model()
post.box('metal', (-.2, 0, -.2), (.2, .04, .2))
post.cylinder('metal', (0, .04, 0), (0, 1.76, 0), .03, 24)
post.box('metal', (-.03, 1.70, 0), (.03, 1.76, .25))
post.save('HoopPost', paint)

board = Model()
board.box('board', (-.3, 1.56, .25), (.3, 1.96, .27))
front, line = .272, .015


def outline(model, low_x, high_x, low_y, high_y):
    model.box('line', (low_x, low_y, .27), (high_x, low_y + line, front))
    model.box('line', (low_x, high_y - line, .27), (high_x, high_y, front))
    model.box('line', (low_x, low_y + line, .27), (low_x + line, high_y - line, front))
    model.box('line', (high_x - line, low_y + line, .27), (high_x, high_y - line, front))


outline(board, -.3, .3, 1.56, 1.96)
outline(board, -.12, .12, 1.6, 1.78)
board.save('HoopBackboard', {'board': ((.92, .92, .9), 0, .4), 'line': ((.85, .2, .08), 0, .5)})

rim = Model()
rim.torus('rim', (0, 1.6, .486), .157, .007)
rim.box('rim', (-.04, 1.593, front), (.04, 1.607, .332))
rim.save('HoopRim', {'rim': ((.92, .32, .06), .3, .4)})

# Net: a diamond lattice of cords hung from the rim's centerline, narrowing to
# an open ring 0.26 m below it.
net, cord, ties, rows = Model(), .0025, 12, 5
nodes = []
for row in range(rows + 1):
    radius = .157 - .057 * (row / rows) ** .8
    nodes.append([(radius * math.cos(TAU * (i + row / 2) / ties), 1.6 - .26 * row / rows,
                   .486 + radius * math.sin(TAU * (i + row / 2) / ties)) for i in range(ties)])
for row in range(rows):
    for i in range(ties):
        for j in (i, (i - 1) % ties):
            net.cylinder('cord', nodes[row][i], nodes[row + 1][j], cord, 6)
for row in nodes:
    for node in row:
        net.sphere('cord', node, cord, 4, 8)
net.torus('cord', (0, 1.34, .486), .1, cord, 48, 6)
net.save('HoopNet', {'cord': ((.92, .92, .9), 0, .9)})

# Desk: a 0.9 x 0.5 m wooden top, 0.03 m thick, with rounded corners and edges.
desk = Model()
half_x, half_z, corner, edge = .45, .25, .05, .015
path = []
for cx, cz, start in ((1, 1, 0), (-1, 1, 1), (-1, -1, 2), (1, -1, 3)):
    center = (cx * (half_x - corner), cz * (half_z - corner))
    for step in range(9):
        angle = (start + step / 8) * math.pi / 2
        outward = (math.cos(angle), 0, math.sin(angle))
        point = (center[0] + outward[0] * (corner - edge), 0, center[1] + outward[2] * (corner - edge))
        path.append((point, outward))
rows = []
for step in range(13):
    alpha = math.pi * (step / 12 - .5)
    rows.append([(add(point, add(mul(outward, edge * math.cos(alpha)), (0, edge * math.sin(alpha), 0))),
                  add(mul(outward, math.cos(alpha)), (0, math.sin(alpha), 0))) for point, outward in path])
desk.grid('wood', rows)
for height in (edge, -edge):
    normal = (0, 1 if height > 0 else -1, 0)
    middle = desk.vertex('wood', (0, height, 0), normal)
    ring = [desk.vertex('wood', add(point, (0, height, 0)), normal) for point, _ in path]
    for i in range(len(ring)):
        desk.triangle('wood', middle, ring[i], ring[(i + 1) % len(ring)])
desk.save('Desk', {'wood': ((.55, .36, .2), 0, .6)})

# Newton's cradle: rails 0.22 m above the base bottom, 0.14 m apart. Each ball
# hangs from a pivot on the line between the rails, 0.12 m below it.
cradle_paint = {'base': ((.08, .08, .09), 0, .35), 'chrome': ((.9, .9, .92), 1, .15)}
frame = Model()
frame.box('base', (-.16, 0, -.09), (.16, .02, .09))
for z in (-.07, .07):
    frame.cylinder('chrome', (-.14, .22, z), (.14, .22, z), .004, 12)
    for x in (-.14, .14):
        frame.cylinder('chrome', (x, .02, z), (x, .22, z), .004, 12)
        frame.sphere('chrome', (x, .22, z), .004, 8, 16)
frame.save('CradleFrame', cradle_paint)

cradle_ball = Model()
cradle_ball.sphere('chrome', (0, -.12, 0), .02, 24, 48)
for z in (-.07, .07):
    cradle_ball.cylinder('string', (0, 0, z), (0, -.12, 0), .001, 8)
cradle_ball.save('CradleBall', {**cradle_paint, 'string': ((.85, .85, .82), 0, .8)})
