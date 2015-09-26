import bpy

def saveObj( _obj, _dirname ):
  # write obj
  _obj.select = True
  bpy.ops.export_scene.obj(
    filepath = _dirname + _obj.name + ".obj",
    use_selection = True,
    axis_forward = "-Z",
    axis_up = "Y"
  )
  _obj.select = False
