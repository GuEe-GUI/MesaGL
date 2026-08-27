#include "GLES2/gl2.h"
#include "mesaGL/ntgl.h"

#include <stdint.h>
#include <stdio.h>

static GLuint compile_shader(GLenum stage, const char *source, int expected)
{
    GLuint shader = glCreateShader(stage);
    GLint status = 0;

    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!!status != !!expected) {
        GLchar log[256];

        glGetShaderInfoLog(shader, sizeof(log), NULL, log);
        fprintf(stderr, "unexpected shader status %d: %s\n", status, log);
        glDeleteShader(shader);
        return 0;
    }
    if (!expected) {
        glDeleteShader(shader);
        return 1;
    }
    return shader;
}

int main(void)
{
    static const char vertex_source[] =
        "precision mediump float;"
        "void main(){gl_PointSize=4.0;gl_Position=vec4(0.0,0.0,0.0,1.0);}";
    static const char fragment_source[] =
        "precision mediump float;"
        "void scoped_type(){struct Scoped{sampler2D image;};}"
        "struct Scoped{float red;};Scoped scoped=Scoped(0.03125);"
        "float ShadowName;void shadow_name(){"
        "struct ShadowName{float value;};ShadowName value;value.value=0.0;}"
        "struct Layer{float value;};float scoped_value(){float result=0.0;{"
        "struct Layer{vec2 value;};Layer inner;inner.value=vec2(0.01,0.02);"
        "result=inner.value.y;}Layer outer;outer.value=0.03;"
        "return result+outer.value;}"
        "float partial_return(bool selected){if(selected)return 0.03125;}"
        "struct Surface { mediump vec2 rg; float values[2]; vec2 pairs[2]; };"
        "struct Global { float bias; };Global global=Global(0.125);"
        "void main(){float values[2];bool index_first=true;"
        "values[index_first?0:1]=0.75;values[index_first?0:1]+=0.0;values[1]=1.0;"
        "const float phase=sin(0.0);const float red=0.25,green=red+0.25;"
        "bool enabled=bool(vec2(1.0,0.0));"
        "Surface s;s.rg=enabled?vec2(red+global.bias,green)+vec2(phase):vec2(0.0);"
        "s.values[0]=values[0];s.values[1]=values[1];"
        "s.pairs[0]=vec2(0.1,0.2);s.pairs[1]=s.pairs[0].yx;"
        "s.rg+=s.pairs[0]+s.pairs[1];"
        "bool running=false;int iterations=0;while(bool running=iterations++<1)"
        "if(running){s.rg+=vec2(0.05);}"
        "if(!running){s.rg+=vec2(0.0);}"
        "bool looping=false;int loops=0;for(;bool looping=loops++<1;){"
        "if(looping){s.rg+=vec2(0.025);}}"
        "if(!looping){s.rg+=vec2(0.0);}"
        "for(;const bool once=true;){if(once){s.rg+=vec2(0.0125);}break;}"
        "while(const bool never=false){s.rg=vec2(0.0);}"
        "s.rg.x+=scoped.red;"
        "s.rg.x+=scoped_value();"
        "s.rg.x+=partial_return(true);"
        "s.rg+=(-vec2(0.0)).yx;"
        "vec2 incremented=vec2(0.0);s.rg+=(incremented++).yx;"
        "s.rg+=(++incremented)*0.0;"
        "s.rg+=(-mat2(0.0))[0].yx;"
        "vec2 shortened=vec2(vec3(0.05,0.06,0.07));"
        "float parenthesized=-0.99;(parenthesized)=-0.99;"
        "++(parenthesized);s.rg+=shortened+vec2(parenthesized);"
        "mat2 matrix_value=mat2(vec4(0.01,0.02,0.03,0.04));"
        "s.rg+=vec2(matrix_value)+vec2(float(matrix_value));"
        "if(bool(1)){s.rg+=vec2(0.0);}"
        "gl_FragColor=vec4(s.rg,s.values[0],s.values[1]);}";
    static const struct {
        GLenum stage;
        const char *source;
    } invalid[] = {
        {GL_VERTEX_SHADER,
         "void main(){bool invalid=true+false;gl_Position=vec4(invalid);}"},
        {GL_VERTEX_SHADER,
         "void main(){bool value=true;bool invalid=value+false;"
         "gl_Position=vec4(invalid);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;float scalar(){return 1.0;}"
         "void main(){gl_FragColor=vec4(scalar().x);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){float scalar=1.0;"
         "gl_FragColor=vec4(scalar.x);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;mat2 matrix(){return mat2(1.0);}"
         "void main(){gl_FragColor=vec4(matrix().x);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){"
         "gl_FragColor=vec4((-vec2(1.0)).z);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){vec2 value=vec2(1.0);"
         "gl_FragColor=vec4((value++).z);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){vec2 value=vec2(1.0);"
         "gl_FragColor=vec4((++value).z);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){"
         "gl_FragColor=vec4((-mat2(1.0))[0].z);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;struct Value{float present;};"
         "Value make_value(){return Value(1.0);}"
         "void main(){Value value=make_value();"
         "gl_FragColor=vec4(value.missing);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;float helper(float x){return x;}"
         "void main(){float helper(float x);gl_FragColor=vec4(helper(1.0));}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){void nested(){discard;}"
         "gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;struct Value{float x;};"
         "void main(){Value convert(Value x);gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;struct Outer{struct Inner{float x;} value;};"
         "void main(){gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;struct Outer{struct{float x;} value;};"
         "void main(){gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;float read_value(struct Value{float x;} value){"
         "return value.x;}void main(){gl_FragColor=vec4(1.0);}"},
        {GL_VERTEX_SHADER,
         "precision mediump float;varying int x;"
         "void main(){x=1;gl_Position=vec4(0.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;varying bool x;"
         "void main(){gl_FragColor=vec4(x?1.0:0.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){const float x=1.0;x=2.0;"
         "gl_FragColor=vec4(x);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;struct S{vec2 x;float y;};"
         "void main(){S s=S(vec2(1.0));gl_FragColor=vec4(s.x,s.y,1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;struct S{vec2 x;float y;};"
         "void main(){S s=S(1.0,vec2(1.0));gl_FragColor=vec4(s.x,s.y,1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;struct S{float x;int y;};"
         "void main(){S s=S(1,2);gl_FragColor=vec4(s.x+float(s.y));}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){float a[2];float b[2];a=b;"
         "gl_FragColor=vec4(a[0]);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;struct S{float values[2];};"
         "void main(){S a;a.values[0]=0.25;S b=a;"
         "gl_FragColor=vec4(b.values[0]);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;struct S{float values[2];};"
         "void main(){float values[2];S s=S(values);"
         "gl_FragColor=vec4(s.values[0]);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;struct S{float values[2];};"
         "void main(){S a;S b;a=b;gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){float a[2];float b[2];"
         "bool equal=a==b;gl_FragColor=vec4(equal);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;struct S{float values[2];};"
         "void main(){S a;S b;bool equal=a==b;gl_FragColor=vec4(equal);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){float x=1.0;float x=2.0;"
         "gl_FragColor=vec4(x);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){float x=1.0,y=vec2(1.0);"
         "gl_FragColor=vec4(x+y);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;struct A{float x;};struct B{float x;};"
         "void main(){A a=A(1.0),b=B(1.0);gl_FragColor=vec4(a.x+b.x);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;float[2] f(){float x[2];return x;}"
         "void main(){gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;float f(float x[]){return x[0];}"
         "void main(){float a[2];gl_FragColor=vec4(f(a));}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){void x;gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){int x=abs(true);"
         "gl_FragColor=vec4(float(x));}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){ivec2 x=min(ivec2(1),2.0);"
         "gl_FragColor=vec4(vec2(x),0.0,1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){ivec2 x=clamp(ivec2(1),0.0,2.0);"
         "gl_FragColor=vec4(vec2(x),0.0,1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){bvec2 x=sign(bvec2(true));"
         "gl_FragColor=vec4(x.x);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){const float x;gl_FragColor=vec4(x);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;uniform float u;void main(){const float x=u;"
         "gl_FragColor=vec4(x);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){float y=1.0;const float x=y;"
         "gl_FragColor=vec4(x);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;float f(){return 1.0;}"
         "void main(){const float x=f();gl_FragColor=vec4(x);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;float abs(int x){return float(x);}"
         "void main(){const float x=abs(2);gl_FragColor=vec4(x);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;float f(in const float x){return x;}"
         "void main(){gl_FragColor=vec4(f(1.0));}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){const float x=x+1.0;"
         "gl_FragColor=vec4(x);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){break;gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){continue;gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){return 1.0;}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;float f(){return;}"
         "void main(){gl_FragColor=vec4(f());}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;float f(){return vec2(1.0);}"
         "void main(){gl_FragColor=vec4(f());}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;float f(){}"
         "void main(){gl_FragColor=vec4(f());}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){sampler2D image;"
         "gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;sampler2D image;"
         "void main(){gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;uniform sampler2D image;"
         "void set_image(out sampler2D result){result=image;}"
         "void main(){gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;struct Material{sampler2D image;};"
         "void main(){Material material;gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;uniform sampler2D image;"
         "sampler2D get_image(){return image;}"
         "void main(){gl_FragColor=texture2D(get_image(),vec2(0.0));}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;struct Material{sampler2D image;};"
         "uniform Material first;uniform Material second;"
         "void main(){bool equal=first==second;gl_FragColor=vec4(equal);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;struct Material{sampler2D image;};"
         "struct Scene{Material material;float weight;};"
         "uniform Scene first;uniform Scene second;"
         "void main(){bool equal=first!=second;gl_FragColor=vec4(equal);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;uniform sampler2D first;"
         "uniform sampler2D second;void main(){"
         "gl_FragColor=texture2D(true?first:second,vec2(0.0));}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;uniform sampler2D first;"
         "uniform sampler2D second;"
         "vec4 sample_pair(sampler2D left,sampler2D right){"
         "left=right;return texture2D(left,vec2(0.0));}"
         "void main(){gl_FragColor=sample_pair(first,second);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;struct Material{sampler2D image;};"
         "uniform sampler2D image;uniform Material material;"
         "vec4 sample_material(Material value){"
         "return texture2D(value.image,vec2(0.0));}"
         "void main(){gl_FragColor=sample_material(Material(image));}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){float values[2][3];"
         "gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;uniform float values[2][3];"
         "void main(){gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;struct Values{float items[2][3];};"
         "uniform Values values;void main(){gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){float scalar,values[2][3];"
         "gl_FragColor=vec4(scalar);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){float values[0];"
         "gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){float values[];"
         "gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){float values[-1];"
         "gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){int count=2;"
         "float values[count];gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;int count=2;"
         "struct Values{float items[count];};uniform Values values;"
         "void main(){gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){"
         "if(bool selected=true){gl_FragColor=vec4(selected);}}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){"
         "do{gl_FragColor=vec4(1.0);}while(bool repeat=false);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){int count=0;"
         "while(bool running=count++<1){}"
         "gl_FragColor=vec4(running);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){int count=0;"
         "for(;bool looping=count++<1;){}"
         "gl_FragColor=vec4(looping);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){"
         "for(int value=0;bool value=true;){break;}"
         "gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;struct Item{const float value;};"
         "void main(){gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;struct Item{float value=1.0;};"
         "void main(){gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;struct Item{};"
         "void main(){gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;struct Item{float value;vec2 value;};"
         "void main(){gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;struct Item{void value;};"
         "void main(){gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;struct Item{Item value;};"
         "void main(){gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;struct First{Second value;};"
         "struct Second{float value;};"
         "void main(){gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void helper(){"
         "struct Local{float value;};Local item;item.value=1.0;}"
         "Local global_item;void main(){gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){{"
         "struct Local{float value;};Local item;item.value=1.0;}"
         "Local leaked;gl_FragColor=vec4(leaked.value);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){"
         "struct Local{float first;};struct Local{vec2 second;};"
         "gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){float Local=0.0;"
         "struct Local{float value;};gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){struct Local{float value;};"
         "float Local=0.0;gl_FragColor=vec4(Local);}"},
        {GL_FRAGMENT_SHADER,
         "void main(){{precision mediump float;float local=0.5;}"
         "float expired=0.25;gl_FragColor=vec4(expired);}"},
        {GL_FRAGMENT_SHADER,
         "void main(){if(true)precision mediump float;"
         "float leaked=0.25;gl_FragColor=vec4(leaked);}"},
        {GL_FRAGMENT_SHADER,
         "struct Item{precision mediump float;float value;};"
         "void main(){gl_FragColor=vec4(1.0);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){if(true)float local=0.5;"
         "gl_FragColor=vec4(local);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){while(false)float local=0.5;"
         "gl_FragColor=vec4(local);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){do float local=0.5;while(false);"
         "gl_FragColor=vec4(local);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){"
         "for(int i=0;i<1;i++)float local=0.5;"
         "gl_FragColor=vec4(local);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){"
         "if(true)struct Local{float value;};Local leaked;"
         "gl_FragColor=vec4(leaked.value);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){float self=self+0.25;"
         "gl_FragColor=vec4(self);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;float helper(){return 0.5;}"
         "void main(){float helper=0.25;gl_FragColor=vec4(helper());}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){float sin=0.25;"
         "gl_FragColor=vec4(sin(0.0));}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){float flat=1.0;"
         "gl_FragColor=vec4(flat);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){float user__value=1.0;"
         "gl_FragColor=vec4(user__value);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){vec4 a=vec4(0.0);"
         "(true?a.xy:a.zw)=vec2(1.0);gl_FragColor=a;}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){vec4 a=vec4(0.0);"
         "(true?a.xy:a.zw)+=vec2(1.0);gl_FragColor=a;}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){int value=4294967296;"
         "gl_FragColor=vec4(float(value));}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void main(){float a=0.0,b=0.0;"
         "(true?a:b)++;gl_FragColor=vec4(a);}"},
        {GL_FRAGMENT_SHADER,
         "precision mediump float;void write(out vec2 value){value=vec2(1.0);}"
         "void main(){vec4 a=vec4(0.0);write(true?a.xy:a.zw);gl_FragColor=a;}"},
    };
    static const char prototype_only[] =
        "precision mediump float;float helper(float);"
        "void main(){gl_FragColor=vec4(helper(1.0));}";
    static const char sampler_source[] =
        "precision mediump float;uniform sampler2D image;"
        "vec4 fetch_color(in sampler2D source,vec2 uv){"
        "return texture2D(source,uv);}"
        "void main(){gl_FragColor=fetch_color(image,vec2(0.5));}";
    static const char sampler_struct_source[] =
        "precision mediump float;struct Material{sampler2D image;};"
        "uniform Material material;"
        "vec4 fetch_color(in Material value){"
        "return texture2D(value.image,vec2(0.5));}"
        "void main(){gl_FragColor=fetch_color(material);}";
    static const char constant_array_source[] =
        "precision mediump float;void main(){const int count=2;"
        "float values[count];values[0]=0.25;values[1]=0.5;"
        "gl_FragColor=vec4(values[0]+values[1]);}";
    static const char constant_struct_array_source[] =
        "precision mediump float;const int count=2;"
        "struct Values{float items[count];};uniform Values values;"
        "void main(){gl_FragColor=vec4(values.items[1]);}";
    static const char local_struct_source[] =
        "precision mediump float;void main(){{"
        "struct Local{float value;};Local item;item.value=0.25;"
        "gl_FragColor=vec4(item.value);}}";
    static const char wide_integer_source[] =
        "precision mediump float;void main(){"
        "int decimal_value=4294967295;int hexadecimal_value=0xffffffff;"
        "gl_FragColor=vec4(0.25,0.5,0.75,1.0);}";
    static const char local_precision_source[] =
        "struct Control{mediump float value;};"
        "mediump float shadowed_function(){return 0.03125;}"
        "mediump float initializer_function(){return 0.03125;}"
        "void main(){precision mediump float;float value=0.375;{"
        "precision highp float;float nested=0.125;value+=nested;}"
        "float shadow=0.125;if(true)float shadow=0.75;"
        "if(false)float branch=0.1;else float branch=0.2;"
        "for(int i=0;i<1;i++)float shadow=0.5;value+=shadow;"
        "if(true)struct Control{vec2 value;} hidden=Control(vec2(0.1));"
        "Control outer;outer.value=0.0625;value+=outer.value;"
        "int initializer_shadow=1;{"
        "float initializer_shadow=float(initializer_shadow)*0.0625;"
        "value+=initializer_shadow;}"
        "float first=0.03125,second=first;value+=second;"
        "float before=shadowed_function();{"
        "float shadowed_function=0.5;}"
        "value+=before+shadowed_function();"
        "float initializer_function=initializer_function();"
        "value+=initializer_function;"
        "gl_FragColor=vec4(value,0.25,0.75,1.0);}";
    uint8_t pixels[8 * 8 * 4] = {0};
    NTGLframebuffer framebuffer = {pixels, 8, 8, 8 * 4, NTGL_RGBA8888,
                                   NTGL_ORIGIN_BOTTOM_LEFT};
    NTGLcontext *context = ntglCreateContext(&framebuffer, NULL);
    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint sampler_shader;
    GLuint prototype_shader;
    GLuint sampler_struct_shader;
    GLuint constant_array_shader;
    GLuint constant_struct_array_shader;
    GLuint local_struct_shader;
    GLuint wide_integer_shader;
    GLuint local_precision_shader;
    GLuint local_precision_program;
    GLuint wide_integer_program;
    GLuint program;
    GLuint sampler_program;
    GLuint texture;
    GLint linked = 0;
    GLubyte pixel[4];
    const GLubyte texel[4] = {17, 99, 201, 255};
    unsigned int index;

    if (!context)
        return 1;
    for (index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index)
        if (!compile_shader(invalid[index].stage, invalid[index].source, 0)) {
            fprintf(stderr, "invalid shader case %u unexpectedly passed\n", index);
            return 2 + (int)index;
        }
    prototype_shader = compile_shader(GL_FRAGMENT_SHADER, prototype_only, 1);
    if (!prototype_shader)
        return 20;
    glDeleteShader(prototype_shader);
    sampler_struct_shader = compile_shader(GL_FRAGMENT_SHADER,
                                           sampler_struct_source, 1);
    if (!sampler_struct_shader)
        return 27;
    glDeleteShader(sampler_struct_shader);
    constant_array_shader = compile_shader(GL_FRAGMENT_SHADER,
                                           constant_array_source, 1);
    if (!constant_array_shader)
        return 28;
    glDeleteShader(constant_array_shader);
    constant_struct_array_shader = compile_shader(
        GL_FRAGMENT_SHADER, constant_struct_array_source, 1);
    if (!constant_struct_array_shader)
        return 29;
    glDeleteShader(constant_struct_array_shader);
    local_struct_shader = compile_shader(GL_FRAGMENT_SHADER,
                                         local_struct_source, 1);
    if (!local_struct_shader)
        return 30;
    glDeleteShader(local_struct_shader);
    wide_integer_shader = compile_shader(GL_FRAGMENT_SHADER,
                                         wide_integer_source, 1);
    if (!wide_integer_shader)
        return 34;

    vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source, 1);
    fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source, 1);
    if (!vertex_shader || !fragment_shader)
        return 21;
    wide_integer_program = glCreateProgram();
    glAttachShader(wide_integer_program, vertex_shader);
    glAttachShader(wide_integer_program, wide_integer_shader);
    glLinkProgram(wide_integer_program);
    glGetProgramiv(wide_integer_program, GL_LINK_STATUS, &linked);
    if (!linked)
        return 35;
    glUseProgram(wide_integer_program);
    glViewport(0, 0, 8, 8);
    glDrawArrays(GL_POINTS, 0, 1);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] < 63 || pixel[0] > 65 || pixel[1] < 127 || pixel[1] > 129 ||
        pixel[2] < 190 || pixel[2] > 192 || pixel[3] != 255)
        return 36;
    glDeleteProgram(wide_integer_program);
    glDeleteShader(wide_integer_shader);

    program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked)
        return 22;
    glUseProgram(program);
    glViewport(0, 0, 8, 8);
    glDrawArrays(GL_POINTS, 0, 1);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] < 242 || pixel[0] > 245 || pixel[1] < 250 || pixel[1] > 253 ||
        pixel[2] < 190 || pixel[2] > 192 || pixel[3] != 255) {
        fprintf(stderr, "structure pixel: %u %u %u %u\n", pixel[0], pixel[1], pixel[2],
                pixel[3]);
        return 23;
    }

    local_precision_shader = compile_shader(GL_FRAGMENT_SHADER,
                                            local_precision_source, 1);
    if (!local_precision_shader)
        return 31;
    local_precision_program = glCreateProgram();
    glAttachShader(local_precision_program, vertex_shader);
    glAttachShader(local_precision_program, local_precision_shader);
    glLinkProgram(local_precision_program);
    glGetProgramiv(local_precision_program, GL_LINK_STATUS, &linked);
    if (!linked)
        return 32;
    glUseProgram(local_precision_program);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_POINTS, 0, 1);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] < 222 || pixel[0] > 224 || pixel[1] < 63 || pixel[1] > 65 ||
        pixel[2] < 190 || pixel[2] > 192 || pixel[3] != 255) {
        fprintf(stderr, "local precision pixel: %u %u %u %u\n", pixel[0],
                pixel[1], pixel[2], pixel[3]);
        return 33;
    }
    glDeleteProgram(local_precision_program);
    glDeleteShader(local_precision_shader);

    sampler_shader = compile_shader(GL_FRAGMENT_SHADER, sampler_source, 1);
    if (!sampler_shader)
        return 24;
    sampler_program = glCreateProgram();
    glAttachShader(sampler_program, vertex_shader);
    glAttachShader(sampler_program, sampler_shader);
    glLinkProgram(sampler_program);
    glGetProgramiv(sampler_program, GL_LINK_STATUS, &linked);
    if (!linked)
        return 25;
    glUseProgram(sampler_program);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, texel);
    glUniform1i(glGetUniformLocation(sampler_program, "image"), 0);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawArrays(GL_POINTS, 0, 1);
    glReadPixels(4, 4, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    if (pixel[0] != texel[0] || pixel[1] != texel[1] ||
        pixel[2] != texel[2] || pixel[3] != texel[3]) {
        fprintf(stderr, "sampler parameter pixel: %u %u %u %u\n",
                pixel[0], pixel[1], pixel[2], pixel[3]);
        return 26;
    }

    glUseProgram(0);
    glDeleteTextures(1, &texture);
    glDeleteProgram(sampler_program);
    glDeleteShader(sampler_shader);
    glDeleteProgram(program);
    glDeleteShader(fragment_shader);
    glDeleteShader(vertex_shader);
    ntglDestroyContext(context);
    return 0;
}
