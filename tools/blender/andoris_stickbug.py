"""Stick bug block-out: Andoris's companion (docs/andoris-design.md), for Blender 4.x / 5.x.

Run it from the Scripting tab (Run Script), or: blender --background --python andoris_stickbug.py
It clears nothing but its own objects (named sb_*), so you can run it again after changing the numbers below.

Layout (seen from the side):   --o-----o
  the gun sticks forward from the front ball joint; the spine runs between the two joints; each joint carries a pair of
  two-segment legs. One mesh, rigidly weighted (every piece follows one bone 100%), an 11-bone armature, and a two-bone
  IK on each leg so the feet stay planted while the body moves.

Axes: X forward, Y left, Z up. Sizes in metres. The root bone sits on the ground under the middle of the body.
"""
import math
import bpy
from mathutils import Vector

# ---- proportions: mostly legs and a torso, with a small gun head -------------------------------------------------
BODY_HEIGHT = 0.95      # the joints' height over the ground
SPINE_LENGTH = 0.80     # between the two ball joints
SPINE_RADIUS = 0.08     # the torso, a cylinder along the body
JOINT_RADIUS = 0.09     # the ball joints

GUN_LENGTH = 0.30       # the barrel, forward from the receiver
GUN_RADIUS = 0.025
RECEIVER = (0.14, 0.09, 0.09)   # the small box the barrel comes out of (length, width, height)

LEG_RADIUS = 0.03       # upper segment; the lower one tapers to FOOT_RADIUS
FOOT_RADIUS = 0.018
KNEE_OUT = 0.45         # how far out to the side the knees are
KNEE_UP = 0.25          # how far over the joints the knees rise (stick insect knees)
KNEE_SPREAD = 0.10      # front knees this far forward of their joint, hind knees this far back
FOOT_OUT = 0.70         # feet: this far out to the side
FOOT_SPREAD = 0.30      # and this far forward (front) / back (hind) of their joint

METAL = (0.55, 0.57, 0.60)      # base colour of the body
DARK = (0.12, 0.12, 0.13)       # joints and feet
ACCENT = (0.75, 0.55, 0.20)     # the gun
ROUGHNESS = 0.32

PREFIX = "sb_"


# ---- helpers -------------------------------------------------------------------------------------------------------
def clear():
    for ob in [o for o in bpy.data.objects if o.name.startswith(PREFIX)]:
        bpy.data.objects.remove(ob, do_unlink=True)
    for coll in (bpy.data.meshes, bpy.data.armatures, bpy.data.materials):
        for block in [b for b in coll if b.name.startswith(PREFIX) and b.users == 0]:
            coll.remove(block)


def material(name, colour, metallic=1.0, roughness=ROUGHNESS):
    m = bpy.data.materials.get(PREFIX + name) or bpy.data.materials.new(PREFIX + name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes.get("Principled BSDF")
    bsdf.inputs["Base Color"].default_value = (*colour, 1.0)
    bsdf.inputs["Metallic"].default_value = metallic
    bsdf.inputs["Roughness"].default_value = roughness
    m.diffuse_color = (*colour, 1.0)
    return m


def finish(ob, bone, mat):
    """Name it, give it its material and one vertex group (its bone) at weight 1."""
    ob.name = PREFIX + bone + "_" + ob.name
    ob.data.materials.append(mat)
    g = ob.vertex_groups.new(name=bone)
    g.add([v.index for v in ob.data.vertices], 1.0, "REPLACE")
    return ob


def box(size, at, bone, mat):
    bpy.ops.mesh.primitive_cube_add(size=1, location=at)
    ob = bpy.context.active_object
    ob.scale = size
    bpy.ops.object.transform_apply(scale=True)
    bevel(ob, 0.012)
    return finish(ob, bone, mat)


def ball(radius, at, bone, mat):
    bpy.ops.mesh.primitive_uv_sphere_add(radius=radius, location=at, segments=24, ring_count=12)
    ob = bpy.context.active_object
    bpy.ops.object.shade_smooth()
    return finish(ob, bone, mat)


def rod(a, b, r1, r2, bone, mat):
    """A (tapered) cylinder from point a to point b."""
    a, b = Vector(a), Vector(b)
    d = b - a
    bpy.ops.mesh.primitive_cone_add(vertices=24 if r1 > 0.05 else 12, radius1=r1, radius2=r2, depth=d.length, location=(a + b) / 2)
    ob = bpy.context.active_object
    ob.rotation_mode = "QUATERNION"
    ob.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(d.normalized())
    bpy.ops.object.transform_apply(rotation=True)
    bpy.ops.object.shade_smooth()
    return finish(ob, bone, mat)


def bevel(ob, width):
    mod = ob.modifiers.new("bevel", "BEVEL")
    mod.width = width
    mod.segments = 2
    bpy.context.view_layer.objects.active = ob
    bpy.ops.object.modifier_apply(modifier=mod.name)


# ---- build ---------------------------------------------------------------------------------------------------------
def build():
    clear()
    if bpy.context.object and bpy.context.object.mode != "OBJECT":
        bpy.ops.object.mode_set(mode="OBJECT")
    metal, dark, accent = material("metal", METAL), material("dark", DARK, roughness=0.5), material("gun", ACCENT)

    z = BODY_HEIGHT
    front = Vector((SPINE_LENGTH / 2, 0, z))
    rear = Vector((-SPINE_LENGTH / 2, 0, z))
    gun_base = front + Vector((JOINT_RADIUS * 0.6, 0, 0))
    gun_tip = gun_base + Vector((RECEIVER[0] + GUN_LENGTH, 0, 0))

    legs = {}   # name -> (hip, knee, foot)
    for side, y in (("L", 1), ("R", -1)):
        for end, hip, fwd in (("F", front, 1), ("R", rear, -1)):
            knee = hip + Vector((KNEE_SPREAD * fwd, KNEE_OUT * y, KNEE_UP))
            foot = Vector((hip.x + FOOT_SPREAD * fwd, FOOT_OUT * y, 0.0))
            legs[end + side] = (hip + Vector((0, JOINT_RADIUS * 0.5 * y, 0)), knee, foot)

    parts = []
    # the torso: the spine bar between the joints (stops short of the balls so they read as sockets)
    parts.append(rod(rear + Vector((JOINT_RADIUS * 0.5, 0, 0)), front - Vector((JOINT_RADIUS * 0.5, 0, 0)), SPINE_RADIUS, SPINE_RADIUS, "spine", metal))
    parts.append(ball(JOINT_RADIUS, front, "joint_front", dark))
    parts.append(ball(JOINT_RADIUS, rear, "joint_rear", dark))
    # the small gun head
    parts.append(box(RECEIVER, gun_base + Vector((RECEIVER[0] / 2, 0, 0)), "head", accent))
    parts.append(rod(gun_base + Vector((RECEIVER[0], 0, 0)), gun_tip, GUN_RADIUS, GUN_RADIUS * 0.9, "head", accent))
    # the legs
    for name, (hip, knee, foot) in legs.items():
        parts.append(rod(hip, knee, LEG_RADIUS, LEG_RADIUS * 0.85, "leg_%s_upper" % name, metal))
        parts.append(ball(LEG_RADIUS * 1.3, knee, "leg_%s_upper" % name, dark))
        parts.append(rod(knee, foot + Vector((0, 0, FOOT_RADIUS)), LEG_RADIUS * 0.85, FOOT_RADIUS, "leg_%s_lower" % name, metal))

    # one mesh
    bpy.ops.object.select_all(action="DESELECT")
    for p in parts:
        p.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    body = bpy.context.active_object
    body.name = PREFIX + "stickbug"
    body.data.name = PREFIX + "stickbug"

    # ---- the armature ----
    arm_data = bpy.data.armatures.new(PREFIX + "rig")
    rig = bpy.data.objects.new(PREFIX + "rig", arm_data)
    bpy.context.collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    bpy.ops.object.mode_set(mode="EDIT")
    eb = arm_data.edit_bones

    def bone(name, head, tail, parent=None, connect=False, deform=True):
        b = eb.new(name)
        b.head, b.tail = Vector(head), Vector(tail)
        if parent:
            b.parent = eb[parent]
            b.use_connect = connect
        b.use_deform = deform
        return b

    bone("root", (0, 0, 0), (0, 0, 0.25))
    bone("spine", rear, front, "root")
    bone("joint_front", front, front + Vector((0, 0, JOINT_RADIUS * 1.5)), "spine")
    bone("joint_rear", rear, rear + Vector((0, 0, JOINT_RADIUS * 1.5)), "spine")
    bone("head", gun_base, gun_tip, "joint_front")
    bone("muzzle", gun_tip, gun_tip + Vector((0.05, 0, 0)), "head", deform=False)   # where the kit fires from
    for name, (hip, knee, foot) in legs.items():
        parent = "joint_front" if name[0] == "F" else "joint_rear"
        bone("leg_%s_upper" % name, hip, knee, parent)
        bone("leg_%s_lower" % name, knee, foot, "leg_%s_upper" % name, connect=True)
        # the IK target, on the ground, child of the root (it stays put when the body moves)
        bone("ik_%s" % name, foot, foot + Vector((0, 0, 0.12)), "root", deform=False)
    bpy.ops.object.mode_set(mode="POSE")
    for name in legs:
        c = rig.pose.bones["leg_%s_lower" % name].constraints.new("IK")
        c.target, c.subtarget, c.chain_count = rig, "ik_%s" % name, 2
    bpy.ops.object.mode_set(mode="OBJECT")

    # bind: the mesh follows the rig through its vertex groups
    body.parent = rig
    mod = body.modifiers.new("rig", "ARMATURE")
    mod.object = rig
    rig.show_in_front = True
    return body, rig


body, rig = build()
print("stick bug: %d vertices, %d bones" % (len(body.data.vertices), len(rig.data.bones)))
