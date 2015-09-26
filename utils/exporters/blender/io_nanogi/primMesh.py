import math
from mathutils import Vector, Matrix, Color
import bpy

from .saveObj import *

def primMesh( _obj, _dirname ):
  saveObj( _obj, _dirname )

  mtl = _obj.data.materials[ 0 ]

  return {
    "type" : [ "D" ],
    "mesh" : {
      "path" : _obj.name + ".obj",
      "postprocess" : {
        "generate_normals" : True,
        "generate_smooth_normals" : False
      }
    },
    "params" : {
      "D" : {
        "R" : c2a( mtl.diffuse_color )
      }
    }
  }

def v2a( _vector ):
  return [ _vector.x, _vector.z, -_vector.y ]

def c2a( _color ):
  return [ _color.r, _color.g, _color.b ]
