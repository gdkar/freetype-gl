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
#include "markup.h"
#include "shader.h"
#include "mat4.h"

#include <GLFW/glfw3.h>


// -------------------------------------------------------------- constants ---
const int __SIGNAL_ACTIVATE__     = 0;
const int __SIGNAL_COMPLETE__     = 1;
const int __SIGNAL_HISTORY_NEXT__ = 2;
const int __SIGNAL_HISTORY_PREV__ = 3;
#define MAX_LINE_LENGTH  511


const int MARKUP_NORMAL      = 0;
const int MARKUP_DEFAULT     = 0;
const int MARKUP_ERROR       = 1;
const int MARKUP_WARNING     = 2;
const int MARKUP_OUTPUT      = 3;
const int MARKUP_BOLD        = 4;
const int MARKUP_ITALIC      = 5;
const int MARKUP_BOLD_ITALIC = 6;
const int MARKUP_FAINT       = 7;
#define   MARKUP_COUNT         8


// ------------------------------------------------------- typedef & struct ---
struct vertex_t {
    float x, y, z;
    float s, t;
    float r, g, b, a;
} ;

struct console_t {
    vector_t *     lines;
    char *prompt;
    char killring[MAX_LINE_LENGTH+1];
    char input[MAX_LINE_LENGTH+1];
    size_t         cursor;
    markup_t       markup[MARKUP_COUNT];
    vertex_buffer_t * buffer;
    texture_atlas_t *atlas;
    vec2           pen;
    void (*handlers[4])( console_t *, const char * );
    GLuint shader;
    mat4   model, view, projection;
    int control_key_handled = 0;

    console_t()
    {
        lines = vector_new( sizeof(char *) );
        prompt = strdup( ">>> " );
        cursor = 0;
        buffer = vertex_buffer_new( "vertex:3f,tex_coord:2f,color:4f" );
        input[0] = '\0';
        killring[0] = '\0';
        handlers[__SIGNAL_ACTIVATE__]     = 0;
        handlers[__SIGNAL_COMPLETE__]     = 0;
        handlers[__SIGNAL_HISTORY_NEXT__] = 0;
        handlers[__SIGNAL_HISTORY_PREV__] = 0;
        pen.x = pen.y = 0;

        atlas = texture_atlas_new( 512, 512, 1 );
        glGenTextures( 1, &atlas->id );

        vec4 white = {{1,1,1,1}};
        vec4 black = {{0,0,0,1}};
        vec4 none = {{0,0,1,0}};

        markup_t normal;
        normal.family  = "fonts/VeraMono.ttf";
        normal.size    = 13.0;
        normal.bold    = 0;
        normal.italic  = 0;
        normal.rise    = 0.0;
        normal.spacing = 0.0;
        normal.gamma   = 1.0;
        normal.foreground_color    = black;
        normal.background_color    = none;
        normal.underline           = 0;
        normal.underline_color     = white;
        normal.overline            = 0;
        normal.overline_color      = white;
        normal.strikethrough       = 0;
        normal.strikethrough_color = white;

        normal.font = texture_font_new_from_file( atlas, 13, "fonts/VeraMono.ttf" );

        markup_t bold = normal;
        bold.bold = 1;
        bold.font = texture_font_new_from_file( atlas, 13, "fonts/VeraMoBd.ttf" );

        markup_t italic = normal;
        italic.italic = 1;
        bold.font = texture_font_new_from_file( atlas, 13, "fonts/VeraMoIt.ttf" );

        markup_t bold_italic = normal;
        bold_italic.bold = 1;
        bold_italic.italic = 1;
        bold_italic.font = texture_font_new_from_file( atlas, 13, "fonts/VeraMoBI.ttf" );

        markup_t faint = normal;
        faint.foreground_color.r = 0.35;
        faint.foreground_color.g = 0.35;
        faint.foreground_color.b = 0.35;

        markup_t error = normal;
        error.foreground_color.r = 1.00;
        error.foreground_color.g = 0.00;
        error.foreground_color.b = 0.00;

        markup_t warning = normal;
        warning.foreground_color.r = 1.00;
        warning.foreground_color.g = 0.50;
        warning.foreground_color.b = 0.50;

        markup_t output = normal;
        output.foreground_color.r = 0.00;
        output.foreground_color.g = 0.00;
        output.foreground_color.b = 1.00;

        markup[MARKUP_NORMAL] = normal;
        markup[MARKUP_ERROR] = error;
        markup[MARKUP_WARNING] = warning;
        markup[MARKUP_OUTPUT] = output;
        markup[MARKUP_FAINT] = faint;
        markup[MARKUP_BOLD] = bold;
        markup[MARKUP_ITALIC] = italic;
        markup[MARKUP_BOLD_ITALIC] = bold_italic;
        }
   ~console_t()
   {
        glDeleteTextures( 1, &atlas->id );
        atlas->id = 0;
        texture_atlas_delete( atlas );
   }

    void
    add_glyph(  char* current, char* previous, markup_t *markup )
    {
        auto glyph  = texture_font_get_glyph( markup->font, current );
        if( previous ) {
            pen.x += texture_glyph_get_kerning( glyph, previous );
        }
        auto r = markup->foreground_color.r;
        auto g = markup->foreground_color.g;
        auto b = markup->foreground_color.b;
        auto a = markup->foreground_color.a;
        auto x0  = pen.x + glyph->offset_x;
        auto y0  = pen.y + glyph->offset_y;
        auto x1  = x0 + glyph->width;
        auto y1  = y0 - glyph->height;
        auto s0 = glyph->s0;
        auto t0 = glyph->t0;
        auto s1 = glyph->s1;
        auto t1 = glyph->t1;

        GLuint indices[] = {0,1,2, 0,2,3};
        vertex_t vertices[] = { { x0,y0,0,  s0,t0,  r,g,b,a },
                                { x0,y1,0,  s0,t1,  r,g,b,a },
                                { x1,y1,0,  s1,t1,  r,g,b,a },
                                { x1,y0,0,  s1,t0,  r,g,b,a } };
        vertex_buffer_push_back( buffer, vertices, 4, indices, 6 );

        pen.x += glyph->advance_x;
        pen.y += glyph->advance_y;
    }

    void render( )
    {
        char* cur_char;
        char* prev_char;

        int viewport[4];
        glGetIntegerv( GL_VIEWPORT, viewport );

        size_t i, index;
        pen.x = 0;
        pen.y = viewport[3];
        vertex_buffer_clear( buffer );

        int cursor_x = pen.x;
        int cursor_y = pen.y;

        markup_t _markup;

        // console_t buffer
        _markup = markup[MARKUP_FAINT];
        pen.y -= _markup.font->height;

        for( i=0; i<lines->size; ++i )
        {
            char *text = * (char **) vector_get( lines, i ) ;
            if( strlen(text) > 0 )
            {
                cur_char = text;
                prev_char = NULL;
                add_glyph( cur_char, prev_char, &_markup );
                prev_char = cur_char;
                for( index=1; index < strlen(text)-1; ++index )
                {
                    cur_char = text + index;
                    add_glyph( cur_char, prev_char, &_markup );
                    prev_char = cur_char;
                }
            }
            pen.y -= _markup.font->height - _markup.font->linegap;
            pen.x = 0;
            cursor_x = pen.x;
            cursor_y = pen.y;
        }

        // Prompt
        _markup = markup[MARKUP_BOLD];
        if( strlen( prompt ) > 0 )
        {
            cur_char = prompt;
            prev_char = NULL;
            add_glyph( cur_char, prev_char, &_markup );
            prev_char = cur_char;
            for( index=1; index < strlen(prompt); ++index )
            {
                cur_char = prompt + index;
                add_glyph( cur_char, prev_char, &_markup );
                prev_char = cur_char;
            }
        }
        cursor_x = (int) pen.x;
        // Input
        _markup = markup[MARKUP_NORMAL];
        if( strlen(input) > 0 )
        {
            cur_char = input;
            prev_char = NULL;
            add_glyph( cur_char, prev_char, &_markup );
            prev_char = cur_char;
            if( cursor > 0)
            {
                cursor_x = (int) pen.x;
            }
            for( index=1; index < strlen(input); ++index )
            {
                cur_char = input + index;
                add_glyph( cur_char, prev_char, &_markup );
                prev_char = cur_char;
                if( index < cursor ) {
                    cursor_x = (int) pen.x;
                }
            }
        }
        if( lines->size || prompt[0] != '\0' || input[0] != '\0' ) {
            glBindTexture( GL_TEXTURE_2D, atlas->id );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
            glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
            glTexImage2D( GL_TEXTURE_2D, 0, GL_RED, atlas->width,
                        atlas->height, 0, GL_RED, GL_UNSIGNED_BYTE,
                        atlas->data );
        }

        // Cursor (we use the black character (NULL) as texture )
        auto glyph  = texture_font_get_glyph( _markup.font, NULL );
        float r = _markup.foreground_color.r;
        float g = _markup.foreground_color.g;
        float b = _markup.foreground_color.b;
        float a = _markup.foreground_color.a;
        float x0  = cursor_x+1;
        float y0  = cursor_y +_markup.font->descender;
        float x1  = cursor_x+2;
        float y1  = y0 + _markup.font->height - _markup.font->linegap;
        float s0 = glyph->s0;
        float t0 = glyph->t0;
        float s1 = glyph->s1;
        float t1 = glyph->t1;
        GLuint indices[] = {0,1,2, 0,2,3};
        vertex_t vertices[] = { { x0,y0,0,  s0,t0,  r,g,b,a },
                                { x0,y1,0,  s0,t1,  r,g,b,a },
                                { x1,y1,0,  s1,t1,  r,g,b,a },
                                { x1,y0,0,  s1,t0,  r,g,b,a } };
        vertex_buffer_push_back( buffer, vertices, 4, indices, 6 );
        glEnable( GL_TEXTURE_2D );
        glUseProgram( shader );
        {
            glUniform1i( glGetUniformLocation( shader, "texture" ),0 );
            glUniformMatrix4fv( glGetUniformLocation( shader, "model" ),
                                1, 0, model.data);
            glUniformMatrix4fv( glGetUniformLocation( shader, "view" ),
                                1, 0, view.data);
            glUniformMatrix4fv( glGetUniformLocation( shader, "projection" ),
                                1, 0, projection.data);
            vertex_buffer_render( buffer, GL_TRIANGLES );
        }
    }
    void
    connect( const char *signal, void (*handler)(console_t *, const char *))
    {
        if( strcmp( signal,"activate" ) == 0 )
        {
            handlers[__SIGNAL_ACTIVATE__] = handler;
        }
        else if( strcmp( signal,"complete" ) == 0 )
        {
            handlers[__SIGNAL_COMPLETE__] = handler;
        }
        else if( strcmp( signal,"history-next" ) == 0 )
        {
            handlers[__SIGNAL_HISTORY_NEXT__] = handler;
        }
        else if( strcmp( signal,"history-prev" ) == 0 )
        {
            handlers[__SIGNAL_HISTORY_PREV__] = handler;
        }
    }

    void
    print( const char *text ) {
        // Make sure there is at least one line
        if( lines->size == 0 ) {
            char *line = strdup( "" );
            vector_push_back( lines, &line );
        }

        // Make sure last line does not end with '\n'
        char *last_line = *(char **) vector_get( lines, lines->size-1 ) ;
        if( strlen( last_line ) != 0 )
        {
            if( last_line[strlen( last_line ) - 1] == '\n' )
            {
                char *line = strdup( "" );
                vector_push_back( lines, &line );
            }
        }
        last_line = *(char **) vector_get( lines, lines->size-1 ) ;

        auto start = text;
        auto end   = strchr(start, '\n');
        auto len = strlen( last_line );
        if( end != NULL)
        {
            auto line = (char *) malloc( (len + end - start + 2)*sizeof( char ) );
            strncpy( line, last_line, len );
            strncpy( line + len, text, end-start+1 );

            line[len+end-start+1] = '\0';

            vector_set( lines, lines->size-1, &line );
            free( last_line );
            if( (end-start)  < (strlen( text )-1) ) {
                print(end+1 );
            }
            return;
        } else {
            auto line = (char *) malloc( (len + strlen(text) + 1) * sizeof( char ) );
            strncpy( line, last_line, len );
            strcpy( line + len, text );
            vector_set( lines, lines->size-1, &line );
            free( last_line );
            return;
        }
    }

    void
    process( const char *action,
                    const unsigned char key )
    {
        auto  len = strlen(input);

        if( strcmp(action, "type") == 0 ) {
            if( len < MAX_LINE_LENGTH ) {
                memmove( input + cursor+1,
                        input + cursor,
                        (len - cursor+1)*sizeof(char) );
                input[cursor] = key;
                cursor++;
            } else {
                fprintf( stderr, "Input buffer is full\n" );
            }
        } else {
            if( strcmp( action, "enter" ) == 0 ) {
                if( handlers[__SIGNAL_ACTIVATE__] ) {
                    (*handlers[__SIGNAL_ACTIVATE__])(this, input);
                }
                print( prompt );
                print( input );
                print( "\n" );
                input[0] = '\0';
                cursor = 0;
            }
            else if( strcmp( action, "right" ) == 0 )
            {
                if( cursor < strlen(input) )
                {
                    cursor += 1;
                }
            }
            else if( strcmp( action, "left" ) == 0 )
            {
                if( cursor > 0 )
                {
                    cursor -= 1;
                }
            }
            else if( strcmp( action, "delete" ) == 0 )
            {
                memmove( input + cursor,
                        input + cursor+1,
                        (len - cursor)*sizeof(char) );
            }
            else if( strcmp( action, "backspace" ) == 0 )
            {
                if( cursor > 0 )
                {
                    memmove( input + cursor-1,
                            input + cursor,
                            (len - cursor+1)*sizeof(char) );
                    cursor--;
                }
            }
            else if( strcmp( action, "kill" ) == 0 )
            {
                if( cursor < len )
                {
                    strcpy(killring, input);
                    input[cursor] = '\0';
                    fprintf(stderr, "Kill ring: %s\n", killring);
                }

            }
            else if( strcmp( action, "yank" ) == 0 )
            {
                size_t l = strlen(killring);
                if( (len + l) < MAX_LINE_LENGTH )
                {
                    memmove( input + cursor + l,
                            input + cursor,
                            (len - cursor)*sizeof(char) );
                    memcpy( input + cursor,
                            killring, l*sizeof(char));
                    cursor += l;
                }
            }
            else if( strcmp( action, "home" ) == 0 )
            {
                cursor = 0;
            }
            else if( strcmp( action, "end" ) == 0 )
            {
                cursor = strlen( input );
            }
            else if( strcmp( action, "clear" ) == 0 )
            {
            }
            else if( strcmp( action, "history-prev" ) == 0 )
            {
                if( handlers[__SIGNAL_HISTORY_PREV__] )
                {
                    (*handlers[__SIGNAL_HISTORY_PREV__])( this, input );
                }
            }
            else if( strcmp( action, "history-next" ) == 0 )
            {
                if( handlers[__SIGNAL_HISTORY_NEXT__] )
                {
                    (*handlers[__SIGNAL_HISTORY_NEXT__])( this, input );
                }
            }
            else if( strcmp( action, "complete" ) == 0 )
            {
                if( handlers[__SIGNAL_COMPLETE__] )
                {
                    (*handlers[__SIGNAL_COMPLETE__])( this, input );
                }
            }
        }
    }

};


// ------------------------------------------------------- global variables ---
static std::unique_ptr<console_t> instance;

// -------------------------------------------------------- console_render ---
// ------------------------------------------------------- console_connect ---

// --------------------------------------------------------- console_print ---

// ------------------------------------------------------- console_process ---


// ------------------------------------------------------- console activate ---
void console_activate( console_t *self, const char *input )
{
    //console_print( self, "Activate callback\n" );
    fprintf( stderr, "Activate callback : %s\n", input );
}


// ------------------------------------------------------- console complete ---
void console_complete( console_t *self, const char *input )
{
    // console_print( self, "Complete callback\n" );
    fprintf( stderr, "Complete callback : %s\n", input );
}


// ----------------------------------------------- console previous history ---
void console_history_prev( console_t *self, const char *input )
{
    // console_print( self, "History prev callback\n" );
    fprintf( stderr, "History prev callback : %s\n", input );
}


// --------------------------------------------------- console next history ---
void console_history_next( console_t *self, const char *input )
{
    // console_print( self, "History next callback\n" );
    fprintf( stderr, "History next callback : %s\n", input );
}


// ------------------------------------------------------------------- init ---
void init( void )
{

    instance = std::make_unique<console_t>();
    instance->print( "OpenGL Freetype console\n"
                   "Copyright 2011 Nicolas P. Rougier. All rights reserved.\n \n" );
    instance->connect( "activate",     console_activate );
    instance->connect( "complete",     console_complete );
    instance->connect( "history-prev", console_history_prev );
    instance->connect( "history-next", console_history_next );

    glClearColor( 1.00, 1.00, 1.00, 1.00 );
    glDisable( GL_DEPTH_TEST );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glEnable( GL_TEXTURE_2D );
    glEnable( GL_BLEND );

    instance->shader = shader_load("shaders/v3f-t2f-c4f.vert",
                         "shaders/v3f-t2f-c4f.frag");
    mat4_set_identity( &instance->projection );
    mat4_set_identity( &instance->model );
    mat4_set_identity( &instance->view );
}


// ---------------------------------------------------------------- display ---
void display( GLFWwindow* window )
{
    auto instance = static_cast<console_t*>(glfwGetWindowUserPointer(window));
    glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    instance->render( );
    glfwSwapBuffers( window );
}


// ---------------------------------------------------------------- reshape ---
void reshape( GLFWwindow* window, int width, int height )
{
    auto instance = static_cast<console_t*>(glfwGetWindowUserPointer(window));
    glViewport(0, 0, width, height);
    mat4_set_orthographic( &instance->projection, 0, width, 0, height, -1, 1);
}


// ----------------------------------------------------------- on char input ---
void char_input( GLFWwindow* window, unsigned int cp )
{
    auto instance = static_cast<console_t*>(glfwGetWindowUserPointer(window));
    if( instance->control_key_handled ) {
        instance->control_key_handled = 0;
        return;
    }
    instance->process( "type", cp );
}


// --------------------------------------------------------------- keyboard ---
void keyboard( GLFWwindow* window, int key, int scancode, int action, int mods )
{
    auto instance = static_cast<console_t*>(glfwGetWindowUserPointer(window));
    if( GLFW_PRESS != action && GLFW_REPEAT != action )
    {
        return;
    }

    switch( key )
    {
        case GLFW_KEY_HOME:
            instance->process( "home", 0 );
            break;
        case GLFW_KEY_DELETE:
            instance->process( "delete", 0 );
            break;
        case GLFW_KEY_END:
            instance->process( "end", 0 );
            break;
        case GLFW_KEY_BACKSPACE:
            instance->process( "backspace", 0 );
            break;
        case GLFW_KEY_TAB:
            instance->process( "complete", 0 );
            break;
        case GLFW_KEY_ENTER:
            instance->process( "enter", 0 );
            break;
        case GLFW_KEY_ESCAPE:
            instance->process( "escape", 0 );
            break;
        case GLFW_KEY_UP:
            instance->process( "history-prev", 0 );
            break;
        case GLFW_KEY_DOWN:
            instance->process( "history-next", 0 );
            break;
        case GLFW_KEY_LEFT:
            instance->process(  "left", 0 );
            break;
        case GLFW_KEY_RIGHT:
            instance->process( "right", 0 );
            break;
        default:
            break;
    }

    if( ( GLFW_MOD_CONTROL & mods ) == 0 )
    {
        return;
    }

    switch( key ) {
        case GLFW_KEY_K:
            instance->control_key_handled = 1;
            instance->process( "kill", 0 );
            break;
        case GLFW_KEY_L:
            instance->control_key_handled = 1;
            instance->process( "clear", 0 );
            break;
        case GLFW_KEY_Y:
            instance->control_key_handled = 1;
            instance->process( "yank", 0 );
            break;
        default:
            break;
    }
}


// --------------------------------------------------------- error-callback ---
void error_callback( int error, const char* description )
{
    fputs( description, stderr );
}


// ------------------------------------------------------------------- main ---
int main( int argc, char **argv )
{
    GLFWwindow* window;

    glfwSetErrorCallback( error_callback );

    if (!glfwInit( ))
    {
        exit( EXIT_FAILURE );
    }

    glfwWindowHint( GLFW_VISIBLE, GL_FALSE );
    glfwWindowHint( GLFW_RESIZABLE, GL_FALSE );

    window = glfwCreateWindow( 1, 1, argv[0], NULL, NULL );

    if (!window)
    {
        glfwTerminate( );
        exit( EXIT_FAILURE );
    }

    glfwMakeContextCurrent( window );
    glfwSwapInterval( 1 );

#ifndef __APPLE__
    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (GLEW_OK != err) {
        /* Problem: glewInit failed, something is seriously wrong. */
        fprintf( stderr, "Error: %s\n", glewGetErrorString(err) );
        exit( EXIT_FAILURE );
    }
    fprintf( stderr, "Using GLEW %s\n", glewGetString(GLEW_VERSION) );
#endif

    init();
    glfwSetWindowUserPointer( window,instance.get());
    glfwSetFramebufferSizeCallback( window, reshape );
    glfwSetWindowRefreshCallback( window, display );
    glfwSetKeyCallback( window, keyboard );
    glfwSetCharCallback( window, char_input );

    glfwSetWindowSize( window, 600,400 );
    glfwShowWindow( window );

    while(!glfwWindowShouldClose( window ))
    {
        display( window );
        glfwPollEvents( );
    }
    instance.reset();

    glfwDestroyWindow( window );
    glfwTerminate( );

    return EXIT_SUCCESS;
}
