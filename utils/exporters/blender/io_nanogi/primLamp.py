import math
from mathutils import Vector, Matrix, Color
import bpy

def primLamp( _obj ):
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

def v2a( _vector ):
  return [ _vector.x, _vector.z, -_vector.y ]
