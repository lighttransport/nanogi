import math
from mathutils import Vector, Matrix, Color
import bpy

def primCamera( _obj ):
  return {
    "type" : [ "E" ],
    "main_sensor" : True,
    "params" : {
      "E" : {
        "type" : "pinhole",
        "pinhole" : {
          "We" : [ 1, 1, 1 ],
          "view" : {
            "eye" : v2a( _obj.location ),
            "center" : v2a( ( _obj.matrix_world * Vector( ( 0, 0, -1, 1 ) ) ).xyz ),
            "up" : v2a( ( _obj.matrix_world * Vector( ( 0, 1, 0, 0 ) ) ).xyz )
          },
          "perspective" : {
            "fov" : _obj.data.angle_y * 180.0 / math.pi
          }
        }
      }
    }
  }

def v2a( _vector ):
  return [ _vector.x, _vector.z, -_vector.y ]
