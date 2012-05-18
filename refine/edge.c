#include <math.h>
#include <glib.h>

#include "point.h"
#include "edge.h"
#include "triangle.h"
#include "mesh.h"

static void
p2tr_edge_init (P2trEdge  *self,
                P2trPoint *start,
                P2trPoint *end,
                gboolean   constrained,
                P2trEdge  *mirror)
{
  self->angle       = atan2 (end->c.y - start->c.y,
                          end->c.x - start->c.x);
  self->constrained = constrained;
  self->delaunay    = FALSE;
  self->end         = end;
  self->mirror      = mirror;
  self->refcount    = 0;
  self->tri         = NULL;
}

P2trEdge*
p2tr_edge_new (P2trPoint *start,
               P2trPoint *end,
               gboolean   constrained)
{
  P2trEdge *self   = g_slice_new (P2trEdge);
  P2trEdge *mirror = g_slice_new (P2trEdge);

  p2tr_edge_init (self, start, end, constrained, mirror);
  p2tr_edge_init (mirror, end, start, constrained, self);

  p2tr_point_ref (start);
  p2tr_point_ref (end);
  
  _p2tr_point_insert_edge (start, self);
  _p2tr_point_insert_edge (end,   mirror);

  ++self->refcount;
  return self;
}

P2trEdge*
p2tr_edge_ref (P2trEdge *self)
{
  ++self->refcount;
  return self;
}

void
p2tr_edge_unref (P2trEdge *self)
{
  if (--self->refcount == 0 && self->mirror->refcount == 0)
    p2tr_edge_free (self);
}

gboolean
p2tr_edge_is_removed (P2trEdge *self)
{
  return self->end == NULL;
}

void
p2tr_edge_remove (P2trEdge *self)
{
  P2trMesh *mesh;
  P2trPoint *start, *end;

  if (self->end == NULL) /* This is only true if the edge was removed */
    return;

  mesh = p2tr_edge_get_mesh (self);

  start = P2TR_EDGE_START(self);
  end = self->end;

  if (self->tri != NULL)
    p2tr_triangle_remove (self->tri);
  if (self->mirror->tri != NULL)
    p2tr_triangle_remove (self->mirror->tri);

  _p2tr_point_remove_edge(start, self);
  _p2tr_point_remove_edge(end, self->mirror);

  self->end = NULL;
  self->mirror->end = NULL;

  p2tr_point_unref (start);
  p2tr_point_unref (end);
  
  if (mesh != NULL)
  {
    p2tr_mesh_on_edge_removed (mesh, self);
    p2tr_mesh_on_edge_removed (mesh, self->mirror);
  }
}

void
p2tr_edge_free (P2trEdge *self)
{
  p2tr_edge_remove (self);
  g_slice_free (P2trEdge, self);
  g_slice_free (P2trEdge, self->mirror);
}

void
p2tr_edge_get_diametral_circle (P2trEdge   *self,
                                P2trCircle *circle)
{
  P2trVector2 radius;
  
  p2tr_vector2_center (&self->end->c, &P2TR_EDGE_START(self)->c, &circle->center);
  p2tr_vector2_sub (&self->end->c, &circle->center, &radius);

  circle->radius = p2tr_vector2_norm (&radius);
}

P2trMesh*
p2tr_edge_get_mesh (P2trEdge *self)
{
  if (self->end != NULL)
    return p2tr_point_get_mesh (self->end);
  else
    return NULL;
}

gdouble
p2tr_edge_get_length(P2trEdge* self)
{
  return sqrt (p2tr_math_length_sq2 (&self->end->c, &P2TR_EDGE_START(self)->c));
}

gdouble
p2tr_edge_get_length_squared(P2trEdge* self)
{
  return p2tr_math_length_sq2 (&self->end->c, &P2TR_EDGE_START(self)->c);
}

gdouble
p2tr_edge_angle_between(P2trEdge *e1, P2trEdge *e2)
{
  /* A = E1.angle, a = abs (A)
   * B = E1.angle, b = abs (B)
   *
   * W is the angle we wish to find. Note the fact that we want
   * to find the angle so that the edges go CLOCKWISE around it.
   *
   * Case 1: Signs of A and B agree | Case 2: Signs of A and B disagree
   *         and A > 0              |         and A > 0
   *                                |
   * a = A, b = B                   | a = A, b = -B
   *             ^^                 |
   *         E2 //                  |           /
   *           //\                  |          /
   *          //b|                  |         /a
   * - - - - * - |W- - - - - - - -  | - - - - * - - - - 
   *         ^^a'|                  |       ^^ \\b
   *         ||_/                   |      // W \\
   *      E1 ||\                    |  E1 // \_/ \\ E2
   *        '||a\                   |    //       \\
   *     - - - - - -                |   //         vv
   *                                |
   * W = A' + B = (180 - A) + B     | W = 180 - (a + b) = 180 - (A - B) 
   * W = 180 - A + B                | W = 180 - A + B
   * 
   * By the illustration above, we can see that in general the angle W
   * can be computed by W = 180 - A + B in every case. The only thing to
   * note is that the range of the result of the computation is
   * [180 - 360, 180 + 360] = [-180, +540] so we may need to subtract
   * 360 to put it back in the range [-180, +180].
   */
  gdouble result;
  
  if (e1->end != P2TR_EDGE_START(e2))
    p2tr_exception_programmatic ("The end-point of the first edge isn't"
        " the end-point of the second edge!");

  result = G_PI - e1->angle + e2->angle;
  if (result > 2 * G_PI)
      result -= 2 * G_PI;

  return result;
}
