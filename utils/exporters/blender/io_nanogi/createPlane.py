import math
from mathutils import Vector, Matrix, Color
import bpy

def vLine( _vector ):
  return "v " + str( _vector.x ) + " " + str( _vector.z ) + " " + str( -_vector.y ) + "\n"

def createPlane( _obj, _dirname ):
  x = _obj.data.size / 2
  y = _obj.data.size_y / 2
  matrix = _obj.matrix_world
  name = _obj.name

  obj = ""

  obj += "o " + name + "\n"
  obj += vLine( matrix * Vector( ( x, y, 0.0 ) ) )
  obj += vLine( matrix * Vector( ( x, -y, 0.0 ) ) )
  obj += vLine( matrix * Vector( ( -x, y, 0.0 ) ) )
  obj += vLine( matrix * Vector( ( -x, -y, 0.0 ) ) )

  obj += "s off\nf 2 4 3\nf 1 2 3"

  fo = open( _dirname + name + ".obj", "w" )
  fo.write( obj )
  fo.close()
