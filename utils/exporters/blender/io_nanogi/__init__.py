import os
import bpy
from bpy_extras.io_utils import ExportHelper

from .yaml import dump

from .primCamera import *
from .primMesh import *
from .primLamp import *

bl_info = {
  "name" : "nanogi",
  "category" : "Import-Export",
  "version" : ( 0, 0 ),
  "blender" : ( 2, 7, 3 )
}

class ExportNanogi( bpy.types.Operator, ExportHelper ):
  bl_label = "Export nanogi"
  bl_idname = "export.nanogi"
  filename_ext = ".yml"

  flag_selection = bpy.props.BoolProperty(
    name = "Selection Only",
    description = "Export selected objects only",
    default = False
  )

  def export( self, _path ):
    # init
    dirname = os.path.dirname( _path ) + "/"
    output = {
      "version" : 5,
      "scene" : {
        "primitives" : []
      }
    }

    # manage selection
    selected = []
    for obj in bpy.context.selected_objects:
      selected.append( obj )
      obj.select = False

    # prepare objects
    preObjects = []
    if self.flag_selection:
      preObjects = selected
    else:
      for obj in bpy.data.objects:
        preObjects.append( obj )

    objects = []
    for pObj in preObjects:
      pObj.select = True
      bpy.ops.object.duplicate()
      bpy.ops.mesh.separate( type = "MATERIAL" ) # separate meshes by material
      for obj in bpy.context.selected_objects:
        objects.append( obj )
        obj.select = False

    # add objects
    for obj in objects:

      prim = {}

      if obj.type == "CAMERA":
        prim = primCamera( obj )

      elif obj.type == "MESH":
        prim = primMesh( obj, dirname )

      elif obj.type == "LAMP":
        prim = primLamp( obj, dirname )

      output[ "scene" ][ "primitives" ].append( prim )

    # write yaml
    fo = open( _path, "w" )
    fo.write( dump( output ) )
    fo.close()

    # delete objects
    for obj in objects:
      obj.select = True
    bpy.ops.object.delete()

    # reselect objects
    for obj in selected:
      obj.select = True

  def execute( self, context ):
    self.export( self.filepath )
    return { "FINISHED" }

def menu_func( self, context ):
    self.layout.operator( ExportNanogi.bl_idname, text = "nanogi (.yml)" )

def register():
  bpy.utils.register_module( __name__ )
  bpy.types.INFO_MT_file_export.append( menu_func )

def unregister():
  bpy.utils.unregister_module( __name__ )
  bpy.types.INFO_MT_file_export.remove( menu_func )
