/* ============================================================================
 * Freetype GL - A C OpenGL Freetype engine
 * Platform:    Any
 * WWW:         https://github.com/rougier/freetype-gl
 * ----------------------------------------------------------------------------
 * Copyright 2011,2012 Nicolas P. Rougier. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  1. Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NICOLAS P. ROUGIER ''AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL NICOLAS P. ROUGIER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and documentation are
 * those of the authors and should not be interpreted as representing official
 * policies, either expressed or implied, of Nicolas P. Rougier.
 * ============================================================================
 */
#include <cmath>
#include <numeric>
#include <algorithm>
#include <utility>
#include <iterator>
#include <memory>
#include <functional>
#include <cstdio>
#include <cstring>


#include "freetype-gl.h"
#include "vertex-buffer.h"
#include "text-buffer.h"
#include "markup.h"
#include "shader.h"
#include "mat4.h"

#include <GLFW/glfw3.h>


double total_time = 0.0;

// ------------------------------------------------------- typedef & struct ---
struct vertex {
    float x, y, z;    // position
    float s, t;       // texture
    float r, g, b, a; // color
};


// ------------------------------------------------------- global variables ---
auto shader = GLuint{};
auto buffer = static_cast<vertex_buffer_t *>(nullptr);
auto atlas  = static_cast<texture_atlas_t *>(nullptr);
auto model = mat4{};
auto view  = mat4{};
auto projection = mat4{};

// --------------------------------------------------------------- add_text ---
vec4
add_text( vertex_buffer_t * buffer, texture_font_t * font,
          const char *text, vec4 * color, vec2 * pen )
{
    auto bbox = vec4{0,0,0,0};
    size_t i;
    auto r = color->red, g = color->green, b = color->blue, a = color->alpha;
    for( i = 0; i < strlen(text); ++i ) {
        glfwSetTime(total_time);
        auto glyph = texture_font_get_glyph( font, text + i );
        total_time += glfwGetTime();
        if( glyph  ) {
            float kerning = 0.0f;
            if( i > 0) {
                kerning = texture_glyph_get_kerning( glyph, text + i - 1 );
            }
            pen->x += kerning;
            auto x0  = (float)( pen->x + glyph->offset_x );
            auto y0  = (float)( pen->y + glyph->offset_y );
            auto x1  = (float)( x0 + glyph->width );
            auto y1  = (float)( y0 - glyph->height );
            auto s0 = glyph->s0;
            auto t0 = glyph->t0;
            auto s1 = glyph->s1;
            auto t1 = glyph->t1;
            GLuint indices[6] = {0,1,2, 0,2,3};
            vertex vertices[4]   = { { x0,y0,0,  s0,t0,  r,g,b,a },
                                     { x0,y1,0,  s0,t1,  r,g,b,a },
                                     { x1,y1,0,  s1,t1,  r,g,b,a },
                                     { x1,y0,0,  s1,t0,  r,g,b,a } };
            vertex_buffer_push_back( buffer, vertices, 4, indices, 6 );
            pen->x += glyph->advance_x;

            if  (x0 < bbox.x)                bbox.x = x0;
            if  (y1 < bbox.y)                bbox.y = y1;
            if ((x1 - bbox.x) > bbox.width)  bbox.width  = x1-bbox.x;
            if ((y0 - bbox.y) > bbox.height) bbox.height = y0-bbox.y;
        }
    }
    return bbox;
}

// ------------------------------------------------------------------- init ---
void init( void )
{
    atlas = texture_atlas_new( 512, 512, 1 );
    auto filename = "fonts/Vera.ttf";
    auto text = "A Quick Brown Fox Jumps Over The Lazy Dog 0123456789";
    buffer = vertex_buffer_new( "vertex:3f,tex_coord:2f,color:4f" );
    vec2 pen = {0,0};
    auto black = vec4{1,1,1,1};
    auto font = texture_font_new_from_file( atlas, 48, filename );
    font->rendermode = RENDER_SIGNED_DISTANCE_FIELD;
    auto bbox = add_text( buffer, font, text, &black, &pen );
    size_t i;
    auto vertices = buffer->vertices;
    for( i=0; i< vector_size(vertices); ++i ) {
        auto vert = (vertex *) vector_get(vertices,i);
        vert->x -= (int)(bbox.x + bbox.width/2);
        vert->y -= (int)(bbox.y + bbox.height/2);
    }

    glGenTextures( 1, &atlas->id );
    glBindTexture( GL_TEXTURE_2D, atlas->id );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    glTexImage2D( GL_TEXTURE_2D, 0, GL_RED, atlas->width, atlas->height,
                  0, GL_RED, GL_UNSIGNED_BYTE, atlas->data );

    shader = shader_load( "shaders/distance-field-2v5.vert",
                          "shaders/distance-field-2v5.frag" );
    mat4_set_identity( &projection );
    mat4_set_identity( &model );
    mat4_set_identity( &view );
}


// ---------------------------------------------------------------- display ---
void display( GLFWwindow* window )
{
    glClearColor( 1, 1, 1, 1 );
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

    GLint viewport[4];
    glGetIntegerv( GL_VIEWPORT, viewport );
    auto width  = viewport[2];
    auto height = viewport[3];

    srand(4);
    vec4 color = {{0.067,0.333, 0.486, 1.0}};
    for( auto i=0; i<40; ++i)
    {
        auto scale = float(.25 + 4.75 * pow(rand()/(float)(RAND_MAX),2));
        auto angle = float(90*(rand()%2));
        float x = (.05 + .9*(rand()/(float)(RAND_MAX)))*width;
        float y = (-.05 + .9*(rand()/(float)(RAND_MAX)))*height;
        float a =  0.1+.8*(pow((1.0-scale/5),2));

        mat4_set_identity( &model );
        mat4_rotate( &model, angle,0,0,1);
        mat4_scale( &model, scale, scale, 1);
        mat4_translate( &model, x, y, 0);

        glUseProgram( shader );
        {
            glUniform1i( glGetUniformLocation( shader, "u_texture" ),0 );
            glUniform4f( glGetUniformLocation( shader, "u_color" ),color.r, color.g, color.b, a);
            glUniformMatrix4fv( glGetUniformLocation( shader, "u_model" ),1, 0, model.data);
            glUniformMatrix4fv( glGetUniformLocation( shader, "u_view" ),1, 0, view.data);
            glUniformMatrix4fv( glGetUniformLocation( shader, "u_projection" ),1, 0, projection.data);
            vertex_buffer_render( buffer, GL_TRIANGLES );
        }
    }
    glfwSwapBuffers( window );
}
// ---------------------------------------------------------------- reshape ---
void reshape( GLFWwindow* window, int width, int height )
{
    glViewport(0, 0, width, height);
    mat4_set_orthographic( &projection, 0, width, 0, height, -1, 1);
}


// --------------------------------------------------------------- keyboard ---
void keyboard( GLFWwindow* window, int key, int scancode, int action, int mods )
{
    if ( key == GLFW_KEY_ESCAPE && action == GLFW_PRESS ) {
        glfwSetWindowShouldClose( window, GL_TRUE );
    }
}
// --------------------------------------------------------- error-callback ---
void error_callback( int error, const char* description )
{
   fprintf( stderr, "ERROR: \"%s\"\n",description);
}
// ------------------------------------------------------------------- main ---
int main( int argc, char **argv )
{
    glfwSetErrorCallback( error_callback );
    if (!glfwInit( )) { exit( EXIT_FAILURE ); }

    glfwWindowHint( GLFW_VISIBLE, GL_FALSE );
    glfwWindowHint( GLFW_RESIZABLE, GL_FALSE );
    glfwWindowHint( GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint( GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint( GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    auto window = glfwCreateWindow( 1, 1, argv[0], nullptr, nullptr);

    if (!window) {
        glfwTerminate( );
        exit( EXIT_FAILURE );
    }

    glfwMakeContextCurrent( window );
    glfwSwapInterval( 1 );

    glfwSetFramebufferSizeCallback( window, reshape );
    glfwSetWindowRefreshCallback( window, display );
    glfwSetKeyCallback( window, keyboard );

#ifndef __APPLE__
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (GLEW_OK != err)
    {
        /* Problem: glewInit failed, something is seriously wrong. */
        fprintf( stderr, "Error: %s\n", glewGetErrorString(err) );
        exit( EXIT_FAILURE );
    }
    fprintf( stderr, "Using GLEW %s\n", glewGetString(GLEW_VERSION) );
#endif

    init();
    fprintf(stderr, "Total time to generate distance map: %fs\n", total_time);
    glfwSetWindowSize( window, 800, 600 );
    glfwShowWindow( window );

    glfwSetTime(0.0);

    while(!glfwWindowShouldClose( window )) {
        display( window );
        glfwPollEvents( );
    }

    glDeleteTextures( 1, &atlas->id );
    atlas->id = 0;
    texture_atlas_delete( atlas );

    glfwDestroyWindow( window );
    glfwTerminate( );

    return EXIT_SUCCESS;
}
