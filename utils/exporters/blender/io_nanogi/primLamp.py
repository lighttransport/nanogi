import math
from mathutils import Vector, Matrix, Color
import bpy

from .createPlane import *

def primLamp( _obj, _dirname ):
  if _obj.data.type == "POINT":
    return {
      "type" : [ "L" ],
      "params" : {
        "L" : {
          "type" : "point",
          "point" : {
            "Le" : [ 1, 1, 1 ],
            "position" : v2a( _obj.location )
          }
        }
      }
    }

  elif _obj.data.type == "SUN":
    return {
      "type" : [ "L" ],
      "params" : {
        "L" : {
          "type" : "directional",
          "directional" : {
            "Le" : [ 1, 1, 1 ],
            "direction" : v2a( ( _obj.matrix_world * Vector( ( 0, 0, -1, 0 ) ) ).xyz )
          }
        }
      }
    }

  elif _obj.data.type == "AREA":
    createPlane( _obj, _dirname )

    return {
      "type" : [ "L" ],
      "mesh" : {
        "path" : _obj.name + ".obj",
        "postprocess" : {
          "generate_normals" : True,
          "generate_smooth_normals" : False
        }
      },
      "params" : {
        "L" : {
          "type" : "area",
          "area" : {
            "Le" : [ 1, 1, 1 ]
          }
        }
      }
    }

def v2a( _vector ):
  return [ _vector.x, _vector.z, -_vector.y ]
