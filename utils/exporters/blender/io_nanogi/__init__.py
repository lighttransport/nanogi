import os
import bpy
from bpy_extras.io_utils import ExportHelper

from .yaml import dump

from .primCamera import *
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
    objects = []
    output = {
      "version" : 5,
      "scene" : {
        "primitives" : []
      }
    }

    # manage selection
    for obj in bpy.data.objects:
      if not self.flag_selection or obj.select:
        objects.append( obj )
      obj.select = False

    # add objects
    for obj in objects:

      prim = {}

      if obj.type == "CAMERA":
        prim = primCamera( obj )

      elif obj.type == "MESH":
        prim = {
          "type" : [ "D" ],
          "mesh" : {
            "path" : obj.name + ".obj",
            "postprocess" : {
              "generate_normals" : True,
              "generate_smooth_normals" : False
            }
          },
          "params" : {
            "D" : {
              "R" : [ 1, 1, 1 ]
            }
          }
        }

        # write obj
        obj.select = True
        bpy.ops.export_scene.obj(
          filepath = dirname + obj.name + ".obj",
          use_selection = True,
          axis_forward = "-Z",
          axis_up = "Y"
        )
        obj.select = False

      elif obj.type == "LAMP":
        prim = primLamp( obj )

      output[ "scene" ][ "primitives" ].append( prim )

    # write yaml
    fo = open( _path, "w" )
    fo.write( dump( output ) )
    fo.close()

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
