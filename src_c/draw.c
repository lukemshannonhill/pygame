/*
    pygame - Python Game Library
    Copyright (C) 2000-2001  Pete Shinners

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public
    License along with this library; if not, write to the Free
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    Pete Shinners
    pete@shinners.org
*/

/*
 *  drawing module for pygame
 */
#include "pygame.h"

#include "pgcompat.h"

#include "doc/draw_doc.h"

#include <math.h>

#include <float.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Declaration of drawing algorithms */
static void
draw_line_width(SDL_Surface *surf, Uint32 color, int width, int *pts,
                         int *drawn_area);
static void
draw_line(SDL_Surface *surf, int x1, int y1, int x2, int y2, Uint32 color,
         int *drawn_area);
static void
draw_aaline(SDL_Surface *surf, Uint32 color, float startx, float starty,
            float endx, float endy, int blend, int *drawn_area);
static void
draw_arc(SDL_Surface *surf, int x, int y, int radius1, int radius2,
         double angle_start, double angle_stop, Uint32 color, int *drawn_area);
static void
draw_circle_bresenham(SDL_Surface *surf, int x0, int y0, int radius,
                      int thickness, Uint32 color, int *drawn_area);
static void
draw_circle_filled(SDL_Surface *surf, int x0, int y0, int radius, Uint32 color,
                   int *drawn_area);
static void
draw_circle_quadrant(SDL_Surface *surf, int x0, int y0, int radius,
                     int thickness, Uint32 color, int top_right, int top_left,
                     int bottom_left, int bottom_right, int *drawn_area);
static void
draw_ellipse(SDL_Surface *surf, int x, int y, int width, int height, int solid,
             Uint32 color, int *drawn_area);
static void
draw_fillpoly(SDL_Surface *surf, int *vx, int *vy, Py_ssize_t n, Uint32 color,
              int *drawn_area);
static void
draw_round_rect(SDL_Surface *surf, int x1, int y1, int x2, int y2, int radius,
                int width, Uint32 color, int top_left, int top_right,
                int bottom_left, int bottom_right, int *drawn_area);

// validation of a draw color
#define CHECK_LOAD_COLOR(colorobj)                                         \
    if (PyInt_Check(colorobj))                                             \
        color = (Uint32)PyInt_AsLong(colorobj);                            \
    else if (pg_RGBAFromColorObj(colorobj, rgba))                          \
        color =                                                            \
            SDL_MapRGBA(surf->format, rgba[0], rgba[1], rgba[2], rgba[3]); \
    else                                                                   \
        return RAISE(PyExc_TypeError, "invalid color argument");

/* Definition of functions that get called in Python */

/* Draws an antialiased line on the given surface.
 *
 * Returns a Rect bounding the drawn area.
 */
static PyObject *
aaline(PyObject *self, PyObject *arg, PyObject *kwargs)
{
    PyObject *surfobj = NULL, *colorobj = NULL, *start = NULL, *end = NULL;
    SDL_Surface *surf = NULL;
    float startx, starty, endx, endy;
    int blend = 1; /* Default blend. */
    float pts[4];
    int drawn_area[4] = {INT_MAX, INT_MAX, INT_MIN,
                         INT_MIN}; /* Used to store bounding box values */
    Uint8 rgba[4];
    Uint32 color;
    static char *keywords[] = {"surface", "color", "start_pos",
                               "end_pos", "blend", NULL};

    if (!PyArg_ParseTupleAndKeywords(arg, kwargs, "O!OOO|i", keywords,
                                     &pgSurface_Type, &surfobj, &colorobj,
                                     &start, &end, &blend)) {
        return NULL; /* Exception already set. */
    }

    surf = pgSurface_AsSurface(surfobj);

    if (surf->format->BytesPerPixel <= 0 || surf->format->BytesPerPixel > 4) {
        return PyErr_Format(PyExc_ValueError,
                            "unsupported surface bit depth (%d) for drawing",
                            surf->format->BytesPerPixel);
    }

    CHECK_LOAD_COLOR(colorobj)

    if (!pg_TwoFloatsFromObj(start, &startx, &starty)) {
        return RAISE(PyExc_TypeError, "invalid start_pos argument");
    }

    if (!pg_TwoFloatsFromObj(end, &endx, &endy)) {
        return RAISE(PyExc_TypeError, "invalid end_pos argument");
    }

    if (!pgSurface_Lock(surfobj)) {
        return RAISE(PyExc_RuntimeError, "error locking surface");
    }

    pts[0] = startx;
    pts[1] = starty;
    pts[2] = endx;
    pts[3] = endy;
    draw_aaline(surf, color, pts[0], pts[1], pts[2], pts[3], blend,
                drawn_area);

    if (!pgSurface_Unlock(surfobj)) {
        return RAISE(PyExc_RuntimeError, "error unlocking surface");
    }

    if (drawn_area[0] != INT_MAX && drawn_area[1] != INT_MAX &&
        drawn_area[2] != INT_MIN && drawn_area[3] != INT_MIN)
        return pgRect_New4(drawn_area[0], drawn_area[1],
                           drawn_area[2] - drawn_area[0] + 1,
                           drawn_area[3] - drawn_area[1] + 1);
    else
        return pgRect_New4((int)startx, (int)starty, 0, 0);
}

/* Draws a line on the given surface.
 *
 * Returns a Rect bounding the drawn area.
 */
static PyObject *
line(PyObject *self, PyObject *arg, PyObject *kwargs)
{
    PyObject *surfobj = NULL, *colorobj = NULL, *start = NULL, *end = NULL;
    SDL_Surface *surf = NULL;
    int startx, starty, endx, endy;
    int pts[4];
    Uint8 rgba[4];
    Uint32 color;
    int width = 1; /* Default width. */
    int drawn_area[4] = {INT_MAX, INT_MAX, INT_MIN,
                         INT_MIN}; /* Used to store bounding box values */
    static char *keywords[] = {"surface", "color", "start_pos",
                               "end_pos", "width", NULL};

    if (!PyArg_ParseTupleAndKeywords(arg, kwargs, "O!OOO|i", keywords,
                                     &pgSurface_Type, &surfobj, &colorobj,
                                     &start, &end, &width)) {
        return NULL; /* Exception already set. */
    }

    surf = pgSurface_AsSurface(surfobj);

    if (surf->format->BytesPerPixel <= 0 || surf->format->BytesPerPixel > 4) {
        return PyErr_Format(PyExc_ValueError,
                            "unsupported surface bit depth (%d) for drawing",
                            surf->format->BytesPerPixel);
    }

    CHECK_LOAD_COLOR(colorobj)

    if (!pg_TwoIntsFromObj(start, &startx, &starty)) {
        return RAISE(PyExc_TypeError, "invalid start_pos argument");
    }

    if (!pg_TwoIntsFromObj(end, &endx, &endy)) {
        return RAISE(PyExc_TypeError, "invalid end_pos argument");
    }

    if (width < 1) {
        return pgRect_New4(startx, starty, 0, 0);
    }

    if (!pgSurface_Lock(surfobj)) {
        return RAISE(PyExc_RuntimeError, "error locking surface");
    }

    pts[0] = startx;
    pts[1] = starty;
    pts[2] = endx;
    pts[3] = endy;
    draw_line_width(surf, color, width, pts, drawn_area);

    if (!pgSurface_Unlock(surfobj)) {
        return RAISE(PyExc_RuntimeError, "error unlocking surface");
    }

    /* Compute return rect. */
    if (drawn_area[0] != INT_MAX && drawn_area[1] != INT_MAX &&
        drawn_area[2] != INT_MIN && drawn_area[3] != INT_MIN)
        return pgRect_New4(drawn_area[0], drawn_area[1],
                           drawn_area[2] - drawn_area[0] + 1,
                           drawn_area[3] - drawn_area[1] + 1);
    else
        return pgRect_New4(startx, starty, 0, 0);
}

/* Draws a series of antialiased lines on the given surface.
 *
 * Returns a Rect bounding the drawn area.
 */
static PyObject *
aalines(PyObject *self, PyObject *arg, PyObject *kwargs)
{
    PyObject *surfobj = NULL, *colorobj = NULL, *closedobj = NULL;
    PyObject *points = NULL, *item = NULL;
    SDL_Surface *surf = NULL;
    Uint32 color;
    Uint8 rgba[4];
    float pts[4];
    float *xlist, *ylist;
    float x, y;
    int l, t;
    int drawn_area[4] = {INT_MAX, INT_MAX, INT_MIN,
                         INT_MIN}; /* Used to store bounding box values */
    int result;
    int closed = 0; /* Default closed. */
    int blend = 1;  /* Default blend. */
    Py_ssize_t loop, length;
    static char *keywords[] = {"surface", "color", "closed",
                               "points",  "blend", NULL};

    if (!PyArg_ParseTupleAndKeywords(arg, kwargs, "O!OOO|i", keywords,
                                     &pgSurface_Type, &surfobj, &colorobj,
                                     &closedobj, &points, &blend)) {
        return NULL; /* Exception already set. */
    }

    surf = pgSurface_AsSurface(surfobj);

    if (surf->format->BytesPerPixel <= 0 || surf->format->BytesPerPixel > 4) {
        return PyErr_Format(PyExc_ValueError,
                            "unsupported surface bit depth (%d) for drawing",
                            surf->format->BytesPerPixel);
    }

    CHECK_LOAD_COLOR(colorobj)

    closed = PyObject_IsTrue(closedobj);

    if (-1 == closed) {
        return RAISE(PyExc_TypeError, "closed argument is invalid");
    }

    if (!PySequence_Check(points)) {
        return RAISE(PyExc_TypeError,
                     "points argument must be a sequence of number pairs");
    }

    length = PySequence_Length(points);

    if (length < 2) {
        return RAISE(PyExc_ValueError,
                     "points argument must contain 2 or more points");
    }

    xlist = PyMem_New(float, length);
    ylist = PyMem_New(float, length);

    if (NULL == xlist || NULL == ylist) {
        return RAISE(PyExc_MemoryError,
                     "cannot allocate memory to draw aalines");
    }

    for (loop = 0; loop < length; ++loop) {
        item = PySequence_GetItem(points, loop);
        result = pg_TwoFloatsFromObj(item, &x, &y);
        if (loop == 0) {
            l = (int) x;
            t = (int) y;
        }
        Py_DECREF(item);

        if (!result) {
            PyMem_Del(xlist);
            PyMem_Del(ylist);
            return RAISE(PyExc_TypeError, "points must be number pairs");
        }

        xlist[loop] = x;
        ylist[loop] = y;
    }

    if (!pgSurface_Lock(surfobj)) {
        PyMem_Del(xlist);
        PyMem_Del(ylist);
        return RAISE(PyExc_RuntimeError, "error locking surface");
    }

    for (loop = 1; loop < length; ++loop) {
        pts[0] = xlist[loop - 1];
        pts[1] = ylist[loop - 1];
        pts[2] = xlist[loop];
        pts[3] = ylist[loop];
        draw_aaline(surf, color, pts[0], pts[1], pts[2], pts[3], blend,
            drawn_area);
    }
    if (closed && length > 2) {
        pts[0] = xlist[length - 1];
        pts[1] = ylist[length - 1];
        pts[2] = xlist[0];
        pts[3] = ylist[0];
        draw_aaline(surf, color, pts[0], pts[1], pts[2], pts[3], blend,
                    drawn_area);
    }

    PyMem_Del(xlist);
    PyMem_Del(ylist);

    if (!pgSurface_Unlock(surfobj)) {
        return RAISE(PyExc_RuntimeError, "error unlocking surface");
    }

    /* Compute return rect. */
    if (drawn_area[0] != INT_MAX && drawn_area[1] != INT_MAX &&
        drawn_area[2] != INT_MIN && drawn_area[3] != INT_MIN)
        return pgRect_New4(drawn_area[0], drawn_area[1],
                           drawn_area[2] - drawn_area[0] + 1,
                           drawn_area[3] - drawn_area[1] + 1);
    else
        return pgRect_New4(l, t, 0, 0);
}

/* Draws a series of lines on the given surface.
 *
 * Returns a Rect bounding the drawn area.
 */
static PyObject *
lines(PyObject *self, PyObject *arg, PyObject *kwargs)
{
    PyObject *surfobj = NULL, *colorobj = NULL, *closedobj = NULL;
    PyObject *points = NULL, *item = NULL;
    SDL_Surface *surf = NULL;
    Uint32 color;
    Uint8 rgba[4];
    int pts[4];
    int x, y, closed, result;
    int *xlist = NULL, *ylist = NULL;
    int width = 1; /* Default width. */
    Py_ssize_t loop, length;
    int drawn_area[4] = {INT_MAX, INT_MAX, INT_MIN,
                         INT_MIN}; /* Used to store bounding box values */
    static char *keywords[] = {"surface", "color", "closed",
                               "points",  "width", NULL};

    if (!PyArg_ParseTupleAndKeywords(arg, kwargs, "O!OOO|i", keywords,
                                     &pgSurface_Type, &surfobj, &colorobj,
                                     &closedobj, &points, &width)) {
        return NULL; /* Exception already set. */
    }

    surf = pgSurface_AsSurface(surfobj);

    if (surf->format->BytesPerPixel <= 0 || surf->format->BytesPerPixel > 4) {
        return PyErr_Format(PyExc_ValueError,
                            "unsupported surface bit depth (%d) for drawing",
                            surf->format->BytesPerPixel);
    }

    CHECK_LOAD_COLOR(colorobj)

    closed = PyObject_IsTrue(closedobj);

    if (-1 == closed) {
        return RAISE(PyExc_TypeError, "closed argument is invalid");
    }

    if (!PySequence_Check(points)) {
        return RAISE(PyExc_TypeError,
                     "points argument must be a sequence of number pairs");
    }

    length = PySequence_Length(points);

    if (length < 2) {
        return RAISE(PyExc_ValueError,
                     "points argument must contain 2 or more points");
    }

    xlist = PyMem_New(int, length);
    ylist = PyMem_New(int, length);

    if (NULL == xlist || NULL == ylist) {
        return RAISE(PyExc_MemoryError,
                     "cannot allocate memory to draw lines");
    }

    for (loop = 0; loop < length; ++loop) {
        item = PySequence_GetItem(points, loop);
        result = pg_TwoIntsFromObj(item, &x, &y);
        Py_DECREF(item);

        if (!result) {
            PyMem_Del(xlist);
            PyMem_Del(ylist);
            return RAISE(PyExc_TypeError, "points must be number pairs");
        }

        xlist[loop] = x;
        ylist[loop] = y;
    }

    x = xlist[0];
    y = ylist[0];

    if (width < 1) {
        PyMem_Del(xlist);
        PyMem_Del(ylist);
        return pgRect_New4(x, y, 0, 0);
    }

    if (!pgSurface_Lock(surfobj)) {
        PyMem_Del(xlist);
        PyMem_Del(ylist);
        return RAISE(PyExc_RuntimeError, "error locking surface");
    }

    for (loop = 1; loop < length; ++loop) {
        pts[0] = xlist[loop - 1];
        pts[1] = ylist[loop - 1];
        pts[2] = xlist[loop];
        pts[3] = ylist[loop];

        draw_line_width(surf, color, width, pts, drawn_area);
    }

    if (closed && length > 2) {
        pts[0] = xlist[length - 1];
        pts[1] = ylist[length - 1];
        pts[2] = xlist[0];
        pts[3] = ylist[0];

        draw_line_width(surf, color, width, pts, drawn_area);
    }

    PyMem_Del(xlist);
    PyMem_Del(ylist);

    if (!pgSurface_Unlock(surfobj)) {
        return RAISE(PyExc_RuntimeError, "error unlocking surface");
    }

    /* Compute return rect. */
    if (drawn_area[0] != INT_MAX && drawn_area[1] != INT_MAX &&
        drawn_area[2] != INT_MIN && drawn_area[3] != INT_MIN)
        return pgRect_New4(drawn_area[0], drawn_area[1],
                           drawn_area[2] - drawn_area[0] + 1,
                           drawn_area[3] - drawn_area[1] + 1);
    else
        return pgRect_New4(x, y, 0, 0);
}

static PyObject *
arc(PyObject *self, PyObject *arg, PyObject *kwargs)
{
    PyObject *surfobj = NULL, *colorobj = NULL, *rectobj = NULL;
    GAME_Rect *rect = NULL, temp;
    SDL_Surface *surf = NULL;
    Uint8 rgba[4];
    Uint32 color;
    int loop, t, l, b, r;
    int width = 1; /* Default width. */
    int drawn_area[4] = {INT_MAX, INT_MAX, INT_MIN,
                         INT_MIN}; /* Used to store bounding box values */
    double angle_start, angle_stop;
    static char *keywords[] = {"surface",    "color", "rect", "start_angle",
                               "stop_angle", "width", NULL};

    if (!PyArg_ParseTupleAndKeywords(
            arg, kwargs, "O!OOdd|i", keywords, &pgSurface_Type, &surfobj,
            &colorobj, &rectobj, &angle_start, &angle_stop, &width)) {
        return NULL; /* Exception already set. */
    }

    rect = pgRect_FromObject(rectobj, &temp);

    if (!rect) {
        return RAISE(PyExc_TypeError, "rect argument is invalid");
    }

    surf = pgSurface_AsSurface(surfobj);

    if (surf->format->BytesPerPixel <= 0 || surf->format->BytesPerPixel > 4) {
        return PyErr_Format(PyExc_ValueError,
                            "unsupported surface bit depth (%d) for drawing",
                            surf->format->BytesPerPixel);
    }

    CHECK_LOAD_COLOR(colorobj)

    if (width < 0) {
        return pgRect_New4(rect->x, rect->y, 0, 0);
    }

    if (width > rect->w / 2 || width > rect->h / 2) {
        width = MAX(rect->w / 2, rect->h / 2);
    }

    if (angle_stop < angle_start) {
        // Angle is in radians
        angle_stop += 2 * M_PI;
    }

    if (!pgSurface_Lock(surfobj)) {
        return RAISE(PyExc_RuntimeError, "error locking surface");
    }

    width = MIN(width, MIN(rect->w, rect->h) / 2);

    for (loop = 0; loop < width; ++loop) {
        draw_arc(surf, rect->x + rect->w / 2, rect->y + rect->h / 2,
                 rect->w / 2 - loop, rect->h / 2 - loop, angle_start,
                 angle_stop, color, drawn_area);
    }

    if (!pgSurface_Unlock(surfobj)) {
        return RAISE(PyExc_RuntimeError, "error unlocking surface");
    }

    l = MAX(rect->x, surf->clip_rect.x);
    t = MAX(rect->y, surf->clip_rect.y);
    r = MIN(rect->x + rect->w, surf->clip_rect.x + surf->clip_rect.w);
    b = MIN(rect->y + rect->h, surf->clip_rect.y + surf->clip_rect.h);
    /* Compute return rect. */
    if (drawn_area[0] != INT_MAX && drawn_area[1] != INT_MAX &&
        drawn_area[2] != INT_MIN && drawn_area[3] != INT_MIN)
        return pgRect_New4(drawn_area[0], drawn_area[1],
                           drawn_area[2] - drawn_area[0] + 1,
                           drawn_area[3] - drawn_area[1] + 1);
    else
        return pgRect_New4(rect->x, rect->y, 0, 0);
}

static PyObject *
ellipse(PyObject *self, PyObject *arg, PyObject *kwargs)
{
    PyObject *surfobj = NULL, *colorobj = NULL, *rectobj = NULL;
    GAME_Rect *rect = NULL, temp;
    SDL_Surface *surf = NULL;
    Uint8 rgba[4];
    Uint32 color;
    int loop, t, l, b, r;
    int width = 0; /* Default width. */
    int drawn_area[4] = {INT_MAX, INT_MAX, INT_MIN,
                         INT_MIN}; /* Used to store bounding box values */
    static char *keywords[] = {"surface", "color", "rect", "width", NULL};

    if (!PyArg_ParseTupleAndKeywords(arg, kwargs, "O!OO|i", keywords,
                                     &pgSurface_Type, &surfobj, &colorobj,
                                     &rectobj, &width)) {
        return NULL; /* Exception already set. */
    }

    rect = pgRect_FromObject(rectobj, &temp);

    if (!rect) {
        return RAISE(PyExc_TypeError, "rect argument is invalid");
    }

    surf = pgSurface_AsSurface(surfobj);

    if (surf->format->BytesPerPixel <= 0 || surf->format->BytesPerPixel > 4) {
        return PyErr_Format(PyExc_ValueError,
                            "unsupported surface bit depth (%d) for drawing",
                            surf->format->BytesPerPixel);
    }

    CHECK_LOAD_COLOR(colorobj)

    if (width < 0) {
        return pgRect_New4(rect->x, rect->y, 0, 0);
    }

    if (width > rect->w / 2 || width > rect->h / 2) {
        width = MAX(rect->w / 2, rect->h / 2);
    }

    if (!pgSurface_Lock(surfobj)) {
        return RAISE(PyExc_RuntimeError, "error locking surface");
    }

    if (!width) {
        /* Draw a filled ellipse. */
        draw_ellipse(surf, rect->x + rect->w / 2, rect->y + rect->h / 2,
                     rect->w, rect->h, 1, color, drawn_area);
    }
    else {
        width = MIN(width, MIN(rect->w, rect->h) / 2);
        for (loop = 0; loop < width; ++loop) {
            draw_ellipse(surf, rect->x + rect->w / 2, rect->y + rect->h / 2,
                         rect->w - loop, rect->h - loop, 0, color, drawn_area);
        }
    }

    if (!pgSurface_Unlock(surfobj)) {
        return RAISE(PyExc_RuntimeError, "error unlocking surface");
    }

    l = MAX(rect->x, surf->clip_rect.x);
    t = MAX(rect->y, surf->clip_rect.y);
    r = MIN(rect->x + rect->w, surf->clip_rect.x + surf->clip_rect.w);
    b = MIN(rect->y + rect->h, surf->clip_rect.y + surf->clip_rect.h);
    if (drawn_area[0] != INT_MAX && drawn_area[1] != INT_MAX &&
        drawn_area[2] != INT_MIN && drawn_area[3] != INT_MIN)
        return pgRect_New4(drawn_area[0], drawn_area[1],
                           drawn_area[2] - drawn_area[0] + 1,
                           drawn_area[3] - drawn_area[1] + 1);
    else
        return pgRect_New4(rect->x, rect->y, 0, 0);
}

static PyObject *
circle(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *surfobj = NULL, *colorobj = NULL;
    SDL_Surface *surf = NULL;
    Uint8 rgba[4];
    Uint32 color;
    PyObject *posobj, *radiusobj;
    int posx, posy, radius;
    int width = 0; /* Default values. */
    int top_right = 0, top_left = 0, bottom_left = 0, bottom_right = 0;
    int drawn_area[4] = {INT_MAX, INT_MAX, INT_MIN,
                         INT_MIN}; /* Used to store bounding box values */
    static char *keywords[] = {"surface",
                               "color",
                               "center",
                               "radius",
                               "width",
                               "draw_top_right",
                               "draw_top_left",
                               "draw_bottom_left",
                               "draw_bottom_right",
                               NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!OOO|iiiii", keywords,
                                     &pgSurface_Type, &surfobj, &colorobj,
                                     &posobj, &radiusobj, &width, &top_right,
                                     &top_left, &bottom_left, &bottom_right))
        return NULL; /* Exception already set. */

    if (!pg_TwoIntsFromObj(posobj, &posx, &posy)) {
        PyErr_SetString(PyExc_TypeError,
                        "center argument must be a pair of numbers");
        return 0;
    }

    if (!pg_IntFromObj(radiusobj, &radius)) {
        PyErr_SetString(PyExc_TypeError, "radius argument must be a number");
        return 0;
    }

    surf = pgSurface_AsSurface(surfobj);

    if (surf->format->BytesPerPixel <= 0 || surf->format->BytesPerPixel > 4) {
        return PyErr_Format(PyExc_ValueError,
                            "unsupported surface bit depth (%d) for drawing",
                            surf->format->BytesPerPixel);
    }

    CHECK_LOAD_COLOR(colorobj)

    if (radius < 1 || width < 0) {
        return pgRect_New4(posx, posy, 0, 0);
    }

    if (width > radius) {
        width = radius;
    }

    if (!pgSurface_Lock(surfobj)) {
        return RAISE(PyExc_RuntimeError, "error locking surface");
    }

    if ((top_right == 0 && top_left == 0 && bottom_left == 0 &&
         bottom_right == 0)) {
        if (!width || width == radius) {
            draw_circle_filled(surf, posx, posy, radius, color, drawn_area);
        }
        else {
            draw_circle_bresenham(surf, posx, posy, radius, width, color,
                                  drawn_area);
        }
    }
    else {
        draw_circle_quadrant(surf, posx, posy, radius, width, color, top_right,
                             top_left, bottom_left, bottom_right, drawn_area);
    }

    if (!pgSurface_Unlock(surfobj)) {
        return RAISE(PyExc_RuntimeError, "error unlocking surface");
    }
    if (drawn_area[0] != INT_MAX && drawn_area[1] != INT_MAX &&
        drawn_area[2] != INT_MIN && drawn_area[3] != INT_MIN)
        return pgRect_New4(drawn_area[0], drawn_area[1],
                           drawn_area[2] - drawn_area[0] + 1,
                           drawn_area[3] - drawn_area[1] + 1);
    else
        return pgRect_New4(posx, posy, 0, 0);
}

static PyObject *
polygon(PyObject *self, PyObject *arg, PyObject *kwargs)
{
    PyObject *surfobj = NULL, *colorobj = NULL, *points = NULL, *item = NULL;
    SDL_Surface *surf = NULL;
    Uint8 rgba[4];
    Uint32 color;
    int *xlist = NULL, *ylist = NULL;
    int width = 0; /* Default width. */
    int x, y, result, l, t;
    int drawn_area[4] = {INT_MAX, INT_MAX, INT_MIN,
                         INT_MIN}; /* Used to store bounding box values */
    Py_ssize_t loop, length;
    static char *keywords[] = {"surface", "color", "points", "width", NULL};

    if (!PyArg_ParseTupleAndKeywords(arg, kwargs, "O!OO|i", keywords,
                                     &pgSurface_Type, &surfobj, &colorobj,
                                     &points, &width)) {
        return NULL; /* Exception already set. */
    }

    if (width) {
        PyObject *ret = NULL;
        PyObject *args =
            Py_BuildValue("(OOiOi)", surfobj, colorobj, 1, points, width);

        if (!args) {
            return NULL; /* Exception already set. */
        }

        ret = lines(NULL, args, NULL);
        Py_DECREF(args);
        return ret;
    }

    surf = pgSurface_AsSurface(surfobj);

    if (surf->format->BytesPerPixel <= 0 || surf->format->BytesPerPixel > 4) {
        return PyErr_Format(PyExc_ValueError,
                            "unsupported surface bit depth (%d) for drawing",
                            surf->format->BytesPerPixel);
    }

    CHECK_LOAD_COLOR(colorobj)

    if (!PySequence_Check(points)) {
        return RAISE(PyExc_TypeError,
                     "points argument must be a sequence of number pairs");
    }

    length = PySequence_Length(points);

    if (length < 3) {
        return RAISE(PyExc_ValueError,
                     "points argument must contain more than 2 points");
    }

    xlist = PyMem_New(int, length);
    ylist = PyMem_New(int, length);

    if (NULL == xlist || NULL == ylist) {
        return RAISE(PyExc_MemoryError,
                     "cannot allocate memory to draw polygon");
    }

    for (loop = 0; loop < length; ++loop) {
        item = PySequence_GetItem(points, loop);
        result = pg_TwoIntsFromObj(item, &x, &y);
        if (loop == 0) {
            l = x;
            t = y;
        }
        Py_DECREF(item);

        if (!result) {
            PyMem_Del(xlist);
            PyMem_Del(ylist);
            return RAISE(PyExc_TypeError, "points must be number pairs");
        }

        xlist[loop] = x;
        ylist[loop] = y;
    }

    if (!pgSurface_Lock(surfobj)) {
        PyMem_Del(xlist);
        PyMem_Del(ylist);
        return RAISE(PyExc_RuntimeError, "error locking surface");
    }

    draw_fillpoly(surf, xlist, ylist, length, color, drawn_area);
    PyMem_Del(xlist);
    PyMem_Del(ylist);

    if (!pgSurface_Unlock(surfobj)) {
        return RAISE(PyExc_RuntimeError, "error unlocking surface");
    }

    if (drawn_area[0] != INT_MAX && drawn_area[1] != INT_MAX &&
        drawn_area[2] != INT_MIN && drawn_area[3] != INT_MIN)
        return pgRect_New4(drawn_area[0], drawn_area[1],
                           drawn_area[2] - drawn_area[0] + 1,
                           drawn_area[3] - drawn_area[1] + 1);
    else
        return pgRect_New4(l, t, 0, 0);
}

static PyObject *
rect(PyObject *self, PyObject *args, PyObject *kwargs)
{
    PyObject *surfobj = NULL, *colorobj = NULL, *rectobj = NULL;
    PyObject *points = NULL, *poly_args = NULL, *ret = NULL;
    GAME_Rect *rect = NULL, temp;
    SDL_Surface *surf = NULL;
    Uint8 rgba[4];
    Uint32 color;
    int t, l, b, r, width = 0, radius = 0; /* Default values. */
    int top_left_radius = -1, top_right_radius = -1, bottom_left_radius = -1,
        bottom_right_radius = -1;
    int drawn_area[4] = {INT_MAX, INT_MAX, INT_MIN,
                         INT_MIN}; /* Used to store bounding box values */
    static char *keywords[] = {"surface",
                               "color",
                               "rect",
                               "width",
                               "border_radius",
                               "border_top_left_radius",
                               "border_top_right_radius",
                               "border_bottom_left_radius",
                               "border_bottom_right_radius",
                               NULL};
    if (!PyArg_ParseTupleAndKeywords(
            args, kwargs, "O!OO|iiiiii", keywords, &pgSurface_Type, &surfobj,
            &colorobj, &rectobj, &width, &radius, &top_left_radius,
            &top_right_radius, &bottom_left_radius, &bottom_right_radius)) {
        return NULL; /* Exception already set. */
    }

    if (!(rect = pgRect_FromObject(rectobj, &temp))) {
        return RAISE(PyExc_TypeError, "rect argument is invalid");
    }

    surf = pgSurface_AsSurface(surfobj);
    if (surf->format->BytesPerPixel <= 0 || surf->format->BytesPerPixel > 4) {
        return PyErr_Format(PyExc_ValueError,
                            "unsupported surface bit depth (%d) for drawing",
                            surf->format->BytesPerPixel);
    }

    CHECK_LOAD_COLOR(colorobj)

    if (width < 0) {
        return pgRect_New4(rect->x, rect->y, 0, 0);
    }
    if (width > rect->w / 2 || width > rect->h / 2) {
        width = MAX(rect->w / 2, rect->h / 2);
    }

    if (radius <= 0 && top_left_radius <= 0 && top_right_radius <= 0 &&
        bottom_left_radius <= 0 && bottom_right_radius <= 0) {
        l = rect->x;
        r = rect->x + rect->w - 1;
        t = rect->y;
        b = rect->y + rect->h - 1;
        points = Py_BuildValue("((ii)(ii)(ii)(ii))", l, t, r, t, r, b, l, b);
        poly_args = Py_BuildValue("(OONi)", surfobj, colorobj, points, width);
        if (NULL == poly_args) {
            return NULL; /* Exception already set. */
        }

        ret = polygon(NULL, poly_args, NULL);
        Py_DECREF(poly_args);
        return ret;
    }
    else {
        if (!pgSurface_Lock(surfobj)) {
            return RAISE(PyExc_RuntimeError, "error locking surface");
        }
        draw_round_rect(surf, rect->x, rect->y, rect->x + rect->w - 1,
                        rect->y + rect->h - 1, radius, width, color,
                        top_left_radius, top_right_radius, bottom_left_radius,
                        bottom_right_radius, drawn_area);
        if (!pgSurface_Unlock(surfobj)) {
            return RAISE(PyExc_RuntimeError, "error unlocking surface");
        }
    }

    if (drawn_area[0] != INT_MAX && drawn_area[1] != INT_MAX &&
        drawn_area[2] != INT_MIN && drawn_area[3] != INT_MIN)
        return pgRect_New4(drawn_area[0], drawn_area[1],
                           drawn_area[2] - drawn_area[0] + 1,
                           drawn_area[3] - drawn_area[1] + 1);
    else
        return pgRect_New4(rect->x, rect->y, 0, 0);
}

/* Functions used in drawing algorithms */

static void
swap(float *a, float *b)
{
    float temp = *a;
    *a = *b;
    *b = temp;
}

static int
compare_int(const void *a, const void *b)
{
    return (*(const int *)a) - (*(const int *)b);
}

static Uint32
get_antialiased_color(SDL_Surface *surf, int x, int y, Uint32 original_color,
                      float brightness, int blend)
{
    Uint8 color_part[4], background_color[4];
    Uint32 *pixels = (Uint32 *)surf->pixels;
    SDL_GetRGBA(original_color, surf->format, &color_part[0], &color_part[1],
                &color_part[2], &color_part[3]);
    if (blend) {
        if (x < surf->clip_rect.x || x >= surf->clip_rect.x + surf->clip_rect.w ||
            y < surf->clip_rect.y || y >= surf->clip_rect.y + surf->clip_rect.h)
            return original_color;
        SDL_GetRGBA(pixels[(y * surf->w) + x], surf->format, &background_color[0],
                    &background_color[1], &background_color[2], &background_color[3]);
        color_part[0] = (Uint8) (brightness * color_part[0] +
                                (1 - brightness) * background_color[0]);
        color_part[1] = (Uint8) (brightness * color_part[1] +
                                (1 - brightness) * background_color[1]);
        color_part[2] = (Uint8) (brightness * color_part[2] +
                                (1 - brightness) * background_color[2]);
        color_part[3] = (Uint8) (brightness * color_part[3] +
                                (1 - brightness) * background_color[3]);
    }
    else {
        color_part[0] =  (Uint8) (brightness * color_part[0]);
        color_part[1] =  (Uint8) (brightness * color_part[1]);
        color_part[2] =  (Uint8) (brightness * color_part[2]);
        color_part[3] =  (Uint8) (brightness * color_part[3]);
    }
    original_color = SDL_MapRGBA(surf->format, color_part[0], color_part[1],
                                 color_part[2], color_part[3]);
    return original_color;
}

static void
add_pixel_to_drawn_list(int x, int y, int *pts)
{
    if (x < pts[0]) {
        pts[0] = x;
    }
    if (y < pts[1]) {
        pts[1] = y;
    }
    if (x > pts[2]) {
        pts[2] = x;
    }
    if (y > pts[3]) {
        pts[3] = y;
    }
}

/* This is an internal helper function.
 *
 * This function draws a line that is clipped by the given rect. To draw thick
 * lines (width > 1), multiple parallel lines are drawn.
 *
 * Params:
 *     surf - pointer to surface to draw on
 *     color - color of line to draw
 *     width - width/thickness of line to draw (expected to be > 0)
 *     pts - array of 4 points which are the endpoints of the line to
 *         draw: {x0, y0, x1, y1}
 *     drawn_area - array of 4 points which are the corners of the
 *         bounding rect
 */
static void
draw_line_width(SDL_Surface *surf, Uint32 color, int width, int *pts,
                int *drawn_area)
{
    int xinc = 0, yinc = 0;
    int original_values[4];
    int loop;
    memcpy(original_values, pts, sizeof(int) * 4);
    /* Decide which direction to grow (width/thickness). */
    if (abs(pts[0] - pts[2]) > abs(pts[1] - pts[3])) {
        /* The line's thickness will be in the y direction. The left/right
         * ends of the line will be flat. */
        yinc = 1;
    }
    else {
        /* The line's thickness will be in the x direction. The top/bottom
         * ends of the line will be flat. */
        xinc = 1;
    }
    /* Draw central line */
    draw_line(surf, pts[0], pts[1], pts[2], pts[3], color, drawn_area);
    /* If width is > 1 start drawing lines connected to the central line, first
     * try to draw to the right / down, and then to the left / right. */
    if (width != 1) {
        for (loop = 1; loop < width; loop += 2) {
            pts[0] = original_values[0] + xinc * (loop / 2 + 1);
            pts[1] = original_values[1] + yinc * (loop / 2 + 1);
            pts[2] = original_values[2] + xinc * (loop / 2 + 1);
            pts[3] = original_values[3] + yinc * (loop / 2 + 1);
            draw_line(surf, pts[0], pts[1], pts[2], pts[3], color, drawn_area);
            if (loop + 1 < width) {
                pts[0] = original_values[0] - xinc * (loop / 2 + 1);
                pts[1] = original_values[1] - yinc * (loop / 2 + 1);
                pts[2] = original_values[2] - xinc * (loop / 2 + 1);
                pts[3] = original_values[3] - yinc * (loop / 2 + 1);
                draw_line(surf, pts[0], pts[1], pts[2], pts[3], color, drawn_area);
            }
        }
    }
}

static int
set_at(SDL_Surface *surf, int x, int y, Uint32 color, int *drawn_area)
{
    SDL_PixelFormat *format = surf->format;
    Uint8 *pixels = (Uint8 *)surf->pixels;
    Uint8 *byte_buf, rgb[4];

    if (x < surf->clip_rect.x || x >= surf->clip_rect.x + surf->clip_rect.w ||
        y < surf->clip_rect.y || y >= surf->clip_rect.y + surf->clip_rect.h)
        return 0;

    switch (format->BytesPerPixel) {
        case 1:
            *((Uint8 *)pixels + y * surf->pitch + x) = (Uint8)color;
            break;
        case 2:
            *((Uint16 *)(pixels + y * surf->pitch) + x) = (Uint16)color;
            break;
        case 4:
            *((Uint32 *)(pixels + y * surf->pitch) + x) = color;
            break;
        default: /*case 3:*/
            SDL_GetRGB(color, format, rgb, rgb + 1, rgb + 2);
            byte_buf = (Uint8 *)(pixels + y * surf->pitch) + x * 3;
#if (SDL_BYTEORDER == SDL_LIL_ENDIAN)
            *(byte_buf + (format->Rshift >> 3)) = rgb[0];
            *(byte_buf + (format->Gshift >> 3)) = rgb[1];
            *(byte_buf + (format->Bshift >> 3)) = rgb[2];
#else
            *(byte_buf + 2 - (format->Rshift >> 3)) = rgb[0];
            *(byte_buf + 2 - (format->Gshift >> 3)) = rgb[1];
            *(byte_buf + 2 - (format->Bshift >> 3)) = rgb[2];
#endif
            break;
    }
    add_pixel_to_drawn_list(x, y, drawn_area);
    return 1;
}

static void
draw_aaline(SDL_Surface *surf, Uint32 color, float from_x, float from_y,
            float to_x, float to_y, int blend, int *drawn_area)
{
    float gradient, dx, dy, intersect_y, brightness;
    int x, x_pixel_start, x_pixel_end;
    Uint32 pixel_color;
    int steep = (to_x - from_x < 0 ? - (to_x - from_x) : (to_x - from_x)) <
                (to_y - from_y < 0 ? - (to_y - from_y) : (to_y - from_y));
    if (steep) {
        swap(&from_x , &from_y);
        swap(&to_x , &to_y);
    }
    if (from_x > to_x) {
        swap(&from_x, &to_x);
        swap(&from_y, &to_y);
    }
    dx = to_x - from_x;
    dy = to_y - from_y;
    x_pixel_start = (int) from_x;
    x_pixel_end = (int) to_x;
    gradient = dx == 0 ? 1 : dy/dx;
    intersect_y = from_y + gradient * ((int) from_x + 0.5f - from_x);
    for (x = x_pixel_start; x <= x_pixel_end; x++) {
        if (steep) {
            brightness = 1 - intersect_y + (int) intersect_y;
            pixel_color = get_antialiased_color(surf, (int) intersect_y, x,
                                                color, brightness, blend);
            set_at(surf, (int) intersect_y, x, pixel_color, drawn_area);
            if ((int) intersect_y < to_y || (x == x_pixel_end && from_y != to_y)) {
                brightness = intersect_y - (int) intersect_y;
                pixel_color = get_antialiased_color(surf, (int) intersect_y + 1,
                                                    x, color, brightness, blend);
                set_at(surf, (int) intersect_y + 1, x, pixel_color, drawn_area);
            }
        }
        else {
            brightness = 1 - intersect_y + (int) intersect_y;
            pixel_color = get_antialiased_color(surf, x, (int) intersect_y,
                                                color, brightness, blend);
            set_at(surf, x, (int) intersect_y, pixel_color, drawn_area);
            if ((int) intersect_y < to_y || (x == x_pixel_end && from_y != to_y)) {
                brightness = intersect_y - (int) intersect_y;
                pixel_color = get_antialiased_color(surf, x, (int) intersect_y + 1,
                                                    color, brightness, blend);
                set_at(surf, x, (int) intersect_y + 1, pixel_color, drawn_area);
            }
        }
        intersect_y += gradient;
    }
}

/* Algorithm modified from
 * https://rosettacode.org/wiki/Bitmap/Bresenham%27s_line_algorithm
 */
static void
draw_line(SDL_Surface *surf, int x1, int y1, int x2, int y2, Uint32 color, int *drawn_area)
{
    int dx, dy, err, e2, sx, sy;
    if (x1 == x2 && y1 == y2) {  /* Single point */
        set_at(surf, x1, y1, color, drawn_area);
        return;
    }
    if (y1 == y2) {  /* Horizontal line */
        dx = (x1 < x2) ? 1 : -1;
        for (sx = 0; sx <= abs(x1 - x2); sx++) {
            set_at(surf, x1 + dx * sx, y1, color, drawn_area);
        }

        return;
    }
    if (x1 == x2) {  /* Vertical line */
        dy = (y1 < y2) ? 1 : -1;
        for (sy = 0; sy <= abs(y1 - y2); sy++)
            set_at(surf, x1, y1 + dy * sy, color, drawn_area);
        return;
    }
    dx = abs(x2 - x1), sx = x1 < x2 ? 1 : -1;
    dy = abs(y2 - y1), sy = y1 < y2 ? 1 : -1;
    err = (dx > dy ? dx : -dy) / 2;
    while (x1 != x2 || y1 != y2) {
        set_at(surf, x1, y1, color, drawn_area);
        e2 = err;
        if (e2 >-dx) { err -= dy; x1 += sx; }
        if (e2 < dy) { err += dx; y1 += sy; }
    }
    set_at(surf, x2, y2, color, drawn_area);
}

static void
draw_arc(SDL_Surface *surf, int x, int y, int radius1, int radius2,
         double angle_start, double angle_stop, Uint32 color, int *drawn_area)
{
    double aStep;  // Angle Step (rad)
    double a;      // Current Angle (rad)
    int x_last, x_next, y_last, y_next;

    // Angle step in rad
    if (radius1 < radius2) {
        if (radius1 < 1.0e-4) {
            aStep = 1.0;
        }
        else {
            aStep = asin(2.0 / radius1);
        }
    }
    else {
        if (radius2 < 1.0e-4) {
            aStep = 1.0;
        }
        else {
            aStep = asin(2.0 / radius2);
        }
    }

    if (aStep < 0.05) {
        aStep = 0.05;
    }

    x_last = (int)(x + cos(angle_start) * radius1);
    y_last = (int)(y - sin(angle_start) * radius2);
    for (a = angle_start + aStep; a <= angle_stop; a += aStep) {
        int points[4];
        x_next = (int)(x + cos(a) * radius1);
        y_next = (int)(y - sin(a) * radius2);
        points[0] = x_last;
        points[1] = y_last;
        points[2] = x_next;
        points[3] = y_next;
        draw_line(surf, points[0], points[1], points[2], points[3],
                  color, drawn_area);
        x_last = x_next;
        y_last = y_next;
    }
}

/* Bresenham Circle Algorithm
 * adapted from: https://de.wikipedia.org/wiki/Bresenham-Algorithmus
 * with additional line width parameter
 */
static void
draw_circle_bresenham(SDL_Surface *surf, int x0, int y0, int radius,
                      int thickness, Uint32 color, int *drawn_area)
{
    int f = 1 - radius;
    int ddF_x = 0;
    int ddF_y = -2 * radius;
    int x = 0;
    int y = radius;
    int y1;
    int i_y = radius - thickness;
    int i_f = 1 - i_y;
    int i_ddF_x = 0;
    int i_ddF_y = -2 * i_y;
    int i;

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        if (i_f >= 0) {
            i_y--;
            i_ddF_y += 2;
            i_f += i_ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x + 1;

        i_ddF_x += 2;
        i_f += i_ddF_x + 1;

        if (thickness > 1)
            thickness = y - i_y;

        /* Numbers represent parts of circle function draw in radians
           interval: [number - 1 * pi / 4, number * pi / 4] */
        for (i = 0; i < thickness; i++) {
            y1 = y - i;
            if ((y0 + y1 - 1) >= (y0 + x - 1)) {
                set_at(surf, x0 + x - 1, y0 + y1 - 1, color,
                       drawn_area);                                  /* 7 */
                set_at(surf, x0 - x, y0 + y1 - 1, color, drawn_area); /* 6 */
            }
            if ((y0 - y1) <= (y0 - x)) {
                set_at(surf, x0 + x - 1, y0 - y1, color, drawn_area); /* 2 */
                set_at(surf, x0 - x, y0 - y1, color, drawn_area);     /* 3 */
            }
            if ((x0 + y1 - 1) >= (x0 + x - 1)) {
                set_at(surf, x0 + y1 - 1, y0 + x - 1, color,
                       drawn_area);                                  /* 8 */
                set_at(surf, x0 + y1 - 1, y0 - x, color, drawn_area); /* 1 */
            }
            if ((x0 - y1) <= (x0 - x)) {
                set_at(surf, x0 - y1, y0 + x - 1, color, drawn_area); /* 5 */
                set_at(surf, x0 - y1, y0 - x, color, drawn_area);     /* 4 */
            }
        }
    }
}

static void
draw_circle_quadrant(SDL_Surface *surf, int x0, int y0, int radius,
                     int thickness, Uint32 color, int top_right, int top_left,
                     int bottom_left, int bottom_right, int *drawn_area)
{
    int f = 1 - radius;
    int ddF_x = 0;
    int ddF_y = -2 * radius;
    int x = 0;
    int y = radius;
    int y1;
    int i_y = radius - thickness;
    int i_f = 1 - i_y;
    int i_ddF_x = 0;
    int i_ddF_y = -2 * i_y;
    int i;
    if (radius == 1) {
        if (top_right > 0)
            set_at(surf, x0, y0 - 1, color, drawn_area);
        if (top_left > 0)
            set_at(surf, x0 - 1, y0 - 1, color, drawn_area);
        if (bottom_left > 0)
            set_at(surf, x0 - 1, y0, color, drawn_area);
        if (bottom_right > 0)
            set_at(surf, x0, y0, color, drawn_area);
        return;
    }

    if (thickness != 0) {
        while (x < y) {
            if (f >= 0) {
                y--;
                ddF_y += 2;
                f += ddF_y;
            }
            if (i_f >= 0) {
                i_y--;
                i_ddF_y += 2;
                i_f += i_ddF_y;
            }
            x++;
            ddF_x += 2;
            f += ddF_x + 1;

            i_ddF_x += 2;
            i_f += i_ddF_x + 1;

            if (thickness > 1)
                thickness = y - i_y;

            /* Numbers represent parts of circle function draw in radians
            interval: [number - 1 * pi / 4, number * pi / 4] */
            if (top_right > 0) {
                for (i = 0; i < thickness; i++) {
                    y1 = y - i;
                    if ((y0 - y1) < (y0 - x))
                        set_at(surf, x0 + x - 1, y0 - y1, color,
                               drawn_area); /* 2 */
                    if ((x0 + y1 - 1) >= (x0 + x - 1))
                        set_at(surf, x0 + y1 - 1, y0 - x, color,
                               drawn_area); /* 1 */
                }
            }
            if (top_left > 0) {
                for (i = 0; i < thickness; i++) {
                    y1 = y - i;
                    if ((y0 - y1) <= (y0 - x))
                        set_at(surf, x0 - x, y0 - y1, color,
                               drawn_area); /* 3 */
                    if ((x0 - y1) < (x0 - x))
                        set_at(surf, x0 - y1, y0 - x, color,
                               drawn_area); /* 4 */
                }
            }
            if (bottom_left > 0) {
                for (i = 0; i < thickness; i++) {
                    y1 = y - i;
                    if ((x0 - y1) <= (x0 - x))
                        set_at(surf, x0 - y1, y0 + x - 1, color,
                               drawn_area); /* 5 */
                    if ((y0 + y1 - 1) > (y0 + x - 1))
                        set_at(surf, x0 - x, y0 + y1 - 1, color,
                               drawn_area); /* 6 */
                }
            }
            if (bottom_right > 0) {
                for (i = 0; i < thickness; i++) {
                    y1 = y - i;
                    if ((y0 + y1 - 1) >= (y0 + x - 1))
                        set_at(surf, x0 + x - 1, y0 + y1 - 1, color,
                               drawn_area); /* 7 */
                    if ((x0 + y1 - 1) > (x0 + x - 1))
                        set_at(surf, x0 + y1 - 1, y0 + x - 1, color,
                               drawn_area); /* 8 */
                }
            }
        }
    }
    else {
        while (x < y) {
            if (f >= 0) {
                y--;
                ddF_y += 2;
                f += ddF_y;
            }
            x++;
            ddF_x += 2;
            f += ddF_x + 1;
            if (top_right > 0) {
                for (y1 = y0 - x; y1 <= y0; y1++) {
                    set_at(surf, x0 + y - 1, y1, color, drawn_area); /* 1 */
                }
                for (y1 = y0 - y; y1 <= y0; y1++) {
                    set_at(surf, x0 + x - 1, y1, color, drawn_area); /* 2 */
                }
            }
            if (top_left > 0) {
                for (y1 = y0 - x; y1 <= y0; y1++) {
                    set_at(surf, x0 - y, y1, color, drawn_area); /* 4 */
                }
                for (y1 = y0 - y; y1 <= y0; y1++) {
                    set_at(surf, x0 - x, y1, color, drawn_area); /* 3 */
                }
            }
            if (bottom_left > 0) {
                for (y1 = y0; y1 < y0 + x; y1++) {
                    set_at(surf, x0 - y, y1, color, drawn_area); /* 4 */
                }
                for (y1 = y0; y1 < y0 + y; y1++) {
                    set_at(surf, x0 - x, y1, color, drawn_area); /* 3 */
                }
            }
            if (bottom_right > 0) {
                for (y1 = y0; y1 < y0 + x; y1++) {
                    set_at(surf, x0 + y - 1, y1, color, drawn_area); /* 1 */
                }
                for (y1 = y0; y1 < y0 + y; y1++) {
                    set_at(surf, x0 + x - 1, y1, color, drawn_area); /* 2 */
                }
            }
        }
    }
}

static void
draw_circle_filled(SDL_Surface *surf, int x0, int y0, int radius, Uint32 color,
                   int *drawn_area)
{
    int f = 1 - radius;
    int ddF_x = 0;
    int ddF_y = -2 * radius;
    int x = 0;
    int y = radius;
    int y1;

    while (x < y) {
        if (f >= 0) {
            y--;
            ddF_y += 2;
            f += ddF_y;
        }
        x++;
        ddF_x += 2;
        f += ddF_x + 1;

        for (y1 = y0 - x; y1 < y0 + x; y1++) {
            set_at(surf, x0 + y - 1, y1, color, drawn_area); /* 1 to 8 */
            set_at(surf, x0 - y, y1, color, drawn_area);     /* 4 to 5 */
        }
        for (y1 = y0 - y; y1 < y0 + y; y1++) {
            set_at(surf, x0 + x - 1, y1, color, drawn_area); /* 2 to 7 */
            set_at(surf, x0 - x, y1, color, drawn_area);     /* 3 to 6 */
        }
    }
}

static void
draw_ellipse(SDL_Surface *surf, int x, int y, int width, int height, int solid,
             Uint32 color, int *drawn_area)
{
    int ix, iy;
    int h, i, j, k;
    int oh, oi, oj, ok;
    int xoff = (width & 1) ^ 1;
    int yoff = (height & 1) ^ 1;
    int rx = (width >> 1);
    int ry = (height >> 1);

    /* Special case: draw a single pixel */
    if (rx == 0 && ry == 0) {
        set_at(surf, x, y, color, drawn_area);
        return;
    }

    /* Special case: draw a vertical line */
    if (rx == 0) {
        draw_line(surf, x, (Sint16)(y - ry), x,
                  (Sint16) (y + ry + (height & 1)), color, drawn_area);
        return;
    }

    /* Special case: draw a horizontal line */
    if (ry == 0) {
        draw_line(surf, (Sint16)(x - rx), y,
                  (Sint16) (x + rx + (width & 1)), y, color, drawn_area);
        return;
    }

    /* Adjust ry for the rest of the ellipses (non-special cases). */
    ry += (solid & 1) - yoff;

    /* Init vars */
    oh = oi = oj = ok = 0xFFFF;

    /* Draw */
    if (rx >= ry) {
        ix = 0;
        iy = rx * 64;

        do {
            h = (ix + 8) >> 6;
            i = (iy + 8) >> 6;
            j = (h * ry) / rx;
            k = (i * ry) / rx;
            if (((ok != k) && (oj != k) && (k < ry)) || !solid) {
                if (solid) {
                    draw_line(surf, x - h, y - k - yoff, x + h - xoff,
                              y - k - yoff, color, drawn_area);
                    draw_line(surf, x - h, y + k, x + h - xoff,
                              y + k, color, drawn_area);
                }
                else {
                    set_at(surf, x - h, y - k - yoff, color, drawn_area);
                    set_at(surf, x + h - xoff, y - k - yoff, color, drawn_area);
                    set_at(surf, x - h, y + k, color, drawn_area);
                    set_at(surf, x + h - xoff, y + k, color, drawn_area);
                }
                ok = k;
            }
            if (((oj != j) && (ok != j) && (k != j)) || !solid) {
                if (solid) {
                    draw_line(surf, x - i, y + j, x + i - xoff, y + j, color,
                             drawn_area);
                    draw_line(surf, x - i, y - j - yoff, x + i - xoff,
                              y - j - yoff, color, drawn_area);
                }
                else {
                    set_at(surf, x - i, y + j, color, drawn_area);
                    set_at(surf, x + i - xoff, y + j, color, drawn_area);
                    set_at(surf, x - i, y - j - yoff, color, drawn_area);
                    set_at(surf, x + i - xoff, y - j - yoff, color, drawn_area);
                }
                oj = j;
            }
            ix = ix + iy / rx;
            iy = iy - ix / rx;

        } while (i > h);
    }
    else {
        ix = 0;
        iy = ry * 64;

        do {
            h = (ix + 8) >> 6;
            i = (iy + 8) >> 6;
            j = (h * rx) / ry;
            k = (i * rx) / ry;

            if (((oi != i) && (oh != i) && (i < ry)) || !solid) {
                if (solid) {
                    draw_line(surf, x - j, y + i, x + j - xoff, y + i,
                             color, drawn_area);
                    draw_line(surf, x - j, y - i - yoff, x + j - xoff,
                              y - i - yoff, color, drawn_area);
                }
                else {
                    set_at(surf, x - j, y + i, color, drawn_area);
                    set_at(surf, x + j - xoff, y + i, color, drawn_area);
                    set_at(surf, x - j, y - i - yoff, color, drawn_area);
                    set_at(surf, x + j - xoff, y - i - yoff, color, drawn_area);
                }
                oi = i;
            }
            if (((oh != h) && (oi != h) && (i != h)) || !solid) {
                if (solid) {
                    draw_line(surf, x - k, y + h, x + k - xoff, y + h,
                             color, drawn_area);
                    draw_line(surf, x - k, y - h - yoff, x + k - xoff,
                              y - h - yoff, color, drawn_area);
                }
                else {
                    set_at(surf, x - k, y + h, color, drawn_area);
                    set_at(surf, x + k - xoff, y + h, color, drawn_area);
                    set_at(surf, x - k, y - h - yoff, color, drawn_area);
                    set_at(surf, x + k - xoff, y - h - yoff, color, drawn_area);
                }
                oh = h;
            }

            ix = ix + iy / ry;
            iy = iy - ix / ry;

        } while (i > h);
    }
}

static void
draw_fillpoly(SDL_Surface *surf, int *point_x, int *point_y,
              Py_ssize_t num_points, Uint32 color, int *drawn_area)
{
    /* point_x : x coordinates of the points
     * point-y : the y coordinates of the points
     * num_points : the number of points
     */
    Py_ssize_t i, i_previous;  // i_previous is the index of the point before i
    int y, miny, maxy;
    int x1, y1;
    int x2, y2;
    /* x_intersect are the x-coordinates of intersections of the polygon
     * with some horizontal line */
    int *x_intersect = PyMem_New(int, num_points);
    if (x_intersect == NULL) {
        PyErr_NoMemory();
        return;
    }

    /* Determine Y maxima */
    miny = point_y[0];
    maxy = point_y[0];
    for (i = 1; (i < num_points); i++) {
        miny = MIN(miny, point_y[i]);
        maxy = MAX(maxy, point_y[i]);
    }

    if (miny == maxy) {
        /* Special case: polygon only 1 pixel high. */

        /* Determine X bounds */
        int minx = point_x[0];
        int maxx = point_x[0];
        for (i = 1; (i < num_points); i++) {
            minx = MIN(minx, point_x[i]);
            maxx = MAX(maxx, point_x[i]);
        }
        draw_line(surf, minx, miny, maxx, miny, color, drawn_area);
        PyMem_Free(x_intersect);
        return;
    }

    /* Draw, scanning y
     * ----------------
     * The algorithm uses a horizontal line (y) that moves from top to the
     * bottom of the polygon:
     *
     * 1. search intersections with the border lines
     * 2. sort intersections (x_intersect)
     * 3. each two x-coordinates in x_intersect are then inside the polygon
     *    (draw line for a pair of two such points)
     */
    for (y = miny; (y <= maxy); y++) {
        // n_intersections is the number of intersections with the polygon
        int n_intersections = 0;
        for (i = 0; (i < num_points); i++) {
            i_previous = ((i) ? (i - 1) : (num_points - 1));

            y1 = point_y[i_previous];
            y2 = point_y[i];
            if (y1 < y2) {
                x1 = point_x[i_previous];
                x2 = point_x[i];
            }
            else if (y1 > y2) {
                y2 = point_y[i_previous];
                y1 = point_y[i];
                x2 = point_x[i_previous];
                x1 = point_x[i];
            }
            else {  // y1 == y2 : has to be handled as special case (below)
                continue;
            }
            if (((y >= y1) && (y < y2)) || ((y == maxy) && (y2 == maxy))) {
                // add intersection if y crosses the edge (excluding the lower
                // end), or when we are on the lowest line (maxy)
                x_intersect[n_intersections++] =
                    (y - y1) * (x2 - x1) / (y2 - y1) + x1;
            }
        }
        qsort(x_intersect, n_intersections, sizeof(int), compare_int);

        for (i = 0; (i < n_intersections); i += 2) {
            draw_line(surf, x_intersect[i], y, x_intersect[i + 1], y, color,
                     drawn_area);
        }
    }

    /* Finally, a special case is not handled by above algorithm:
     *
     * For two border points with same height miny < y < maxy,
     * sometimes the line between them is not colored:
     * this happens when the line will be a lower border line of the polygon
     * (eg we are inside the polygon with a smaller y, and outside with a
     * bigger y),
     * So we loop for border lines that are horizontal.
     */
    for (i = 0; (i < num_points); i++) {
        i_previous = ((i) ? (i - 1) : (num_points - 1));
        y = point_y[i];

        if ((miny < y) && (point_y[i_previous] == y) && (y < maxy)) {
            draw_line(surf, point_x[i], y, point_x[i_previous], y, color,
                     drawn_area);
        }
    }
    PyMem_Free(x_intersect);
}

static void
draw_round_rect(SDL_Surface *surf, int x1, int y1, int x2, int y2, int radius,
                int width, Uint32 color, int top_left, int top_right,
                int bottom_left, int bottom_right, int *drawn_area)
{
    int pts[16], i;
    float q_top, q_left, q_bottom, q_right, f;
    if (top_left < 0)
        top_left = radius;
    if (top_right < 0)
        top_right = radius;
    if (bottom_left < 0)
        bottom_left = radius;
    if (bottom_right < 0)
        bottom_right = radius;
    if ((top_left + top_right) > (x2 - x1 + 1) ||
        (bottom_left + bottom_right) > (x2 - x1 + 1) ||
        (top_left + bottom_left) > (y2 - y1 + 1) ||
        (top_right + bottom_right) > (y2 - y1 + 1)) {
        q_top = (x2 - x1 + 1) / (float)(top_left + top_right);
        q_left = (y2 - y1 + 1) / (float)(top_left + bottom_left);
        q_bottom = (x2 - x1 + 1) / (float)(bottom_left + bottom_right);
        q_right = (y2 - y1 + 1) / (float)(top_right + bottom_right);
        f = MIN(MIN(MIN(q_top, q_left), q_bottom), q_right);
        top_left = (int)(top_left * f);
        top_right = (int)(top_right * f);
        bottom_left = (int)(bottom_left * f);
        bottom_right = (int)(bottom_right * f);
    }
    if (width == 0) { /* Filled rect */
        pts[0] = x1;
        pts[1] = x1 + top_left;
        pts[2] = x2 - top_right;
        pts[3] = x2;
        pts[4] = x2;
        pts[5] = x2 - bottom_right;
        pts[6] = x1 + bottom_left;
        pts[7] = x1;
        pts[8] = y1 + top_left;
        pts[9] = y1;
        pts[10] = y1;
        pts[11] = y1 + top_right;
        pts[12] = y2 - bottom_right;
        pts[13] = y2;
        pts[14] = y2;
        pts[15] = y2 - bottom_left;
        draw_fillpoly(surf, pts, pts + 8, 8, color, drawn_area);
        draw_circle_quadrant(surf, x2 - top_right + 1, y1 + top_right,
                             top_right, 0, color, 1, 0, 0, 0, drawn_area);
        draw_circle_quadrant(surf, x1 + top_left, y1 + top_left, top_left, 0,
                             color, 0, 1, 0, 0, drawn_area);
        draw_circle_quadrant(surf, x1 + bottom_left, y2 - bottom_left + 1,
                             bottom_left, 0, color, 0, 0, 1, 0, drawn_area);
        draw_circle_quadrant(surf, x2 - bottom_right + 1, y2 - bottom_right + 1,
                             bottom_right, 0, color, 0, 0, 0, 1, drawn_area);
    }
    else {
        pts[0] = x1 + top_left;
        pts[1] = y1 + (int)(width / 2) - 1 + width % 2;
        pts[2] = x2 - top_right;
        pts[3] = y1 + (int)(width / 2) - 1 + width % 2;
        if (pts[2] == pts[0]) {
            for (i = 0; i < width; i++) {
                set_at(surf, pts[0], y1 + i, color,
                       drawn_area); /* Fill gap if reduced radius */
            }
        }
        else
            draw_line_width(surf, color, width, pts,
                                     drawn_area); /* Top line */
        pts[0] = x1 + (int)(width / 2) - 1 + width % 2;
        pts[1] = y1 + top_left;
        pts[2] = x1 + (int)(width / 2) - 1 + width % 2;
        pts[3] = y2 - bottom_left;
        if (pts[3] == pts[1]) {
            for (i = 0; i < width; i++) {
                set_at(surf, x1 + i, pts[1], color,
                       drawn_area); /* Fill gap if reduced radius */
            }
        }
        else
            draw_line_width(surf, color, width, pts,
                                     drawn_area); /* Left line */
        pts[0] = x1 + bottom_left;
        pts[1] = y2 - (int)(width / 2);
        pts[2] = x2 - bottom_right;
        pts[3] = y2 - (int)(width / 2);
        if (pts[2] == pts[0]) {
            for (i = 0; i < width; i++) {
                set_at(surf, pts[0], y2 - i, color,
                       drawn_area); /* Fill gap if reduced radius */
            }
        }
        else
            draw_line_width(surf, color, width, pts,
                                     drawn_area); /* Bottom line */
        pts[0] = x2 - (int)(width / 2);
        pts[1] = y1 + top_right;
        pts[2] = x2 - (int)(width / 2);
        pts[3] = y2 - bottom_right;
        if (pts[3] == pts[1]) {
            for (i = 0; i < width; i++) {
                set_at(surf, x2 - i, pts[1], color,
                       drawn_area); /* Fill gap if reduced radius */
            }
        }
        else
            draw_line_width(surf, color, width, pts,
                                     drawn_area); /* Right line */

        draw_circle_quadrant(surf, x2 - top_right + 1, y1 + top_right,
                             top_right, width, color, 1, 0, 0, 0, drawn_area);
        draw_circle_quadrant(surf, x1 + top_left, y1 + top_left, top_left,
                             width, color, 0, 1, 0, 0, drawn_area);
        draw_circle_quadrant(surf, x1 + bottom_left, y2 - bottom_left + 1,
                             bottom_left, width, color, 0, 0, 1, 0,
                             drawn_area);
        draw_circle_quadrant(surf, x2 - bottom_right + 1, y2 - bottom_right + 1,
                             bottom_right, width, color, 0, 0, 0, 1,
                             drawn_area);
    }
}

/* List of python functions */
static PyMethodDef _draw_methods[] = {
    {"aaline", (PyCFunction)aaline, METH_VARARGS | METH_KEYWORDS,
     DOC_PYGAMEDRAWAALINE},
    {"line", (PyCFunction)line, METH_VARARGS | METH_KEYWORDS,
     DOC_PYGAMEDRAWLINE},
    {"aalines", (PyCFunction)aalines, METH_VARARGS | METH_KEYWORDS,
     DOC_PYGAMEDRAWAALINES},
    {"lines", (PyCFunction)lines, METH_VARARGS | METH_KEYWORDS,
     DOC_PYGAMEDRAWLINES},
    {"ellipse", (PyCFunction)ellipse, METH_VARARGS | METH_KEYWORDS,
     DOC_PYGAMEDRAWELLIPSE},
    {"arc", (PyCFunction)arc, METH_VARARGS | METH_KEYWORDS,
     DOC_PYGAMEDRAWARC},
    {"circle", (PyCFunction)circle, METH_VARARGS | METH_KEYWORDS,
     DOC_PYGAMEDRAWCIRCLE},
    {"polygon", (PyCFunction)polygon, METH_VARARGS | METH_KEYWORDS,
     DOC_PYGAMEDRAWPOLYGON},
    {"rect", (PyCFunction)rect, METH_VARARGS | METH_KEYWORDS,
     DOC_PYGAMEDRAWRECT},

    {NULL, NULL, 0, NULL}};

MODINIT_DEFINE(draw)
{
#if PY3
    static struct PyModuleDef _module = {PyModuleDef_HEAD_INIT,
                                         "draw",
                                         DOC_PYGAMEDRAW,
                                         -1,
                                         _draw_methods,
                                         NULL,
                                         NULL,
                                         NULL,
                                         NULL};
#endif

    /* imported needed apis; Do this first so if there is an error
       the module is not loaded.
    */
    import_pygame_base();
    if (PyErr_Occurred()) {
        MODINIT_ERROR;
    }
    import_pygame_color();
    if (PyErr_Occurred()) {
        MODINIT_ERROR;
    }
    import_pygame_rect();
    if (PyErr_Occurred()) {
        MODINIT_ERROR;
    }
    import_pygame_surface();
    if (PyErr_Occurred()) {
        MODINIT_ERROR;
    }

/* create the module */
#if PY3
    return PyModule_Create(&_module);
#else
    Py_InitModule3(MODPREFIX "draw", _draw_methods, DOC_PYGAMEDRAW);
#endif
}
