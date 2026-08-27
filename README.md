# MesaGL

MesaGL is a compact software OpenGL-compatible renderer for framebuffer-only systems without a
GPU driver. The core library has no dependency on Linux, X11, EGL, Mesa, or RT-Thread. A platform
only needs to provide writable framebuffer memory, allocation functions, and an optional present
callback.

It is intended for operating systems without graphics drivers, direct-framebuffer applications,
and reasonably capable MCUs. The X11 backend is provided only as a Linux reference port and
development environment.

## Features

- Fixed-function OpenGL compatibility API with matrix stacks, immediate mode, vertex arrays,
  colors, textures, depth testing, blending, scissoring, and face culling.
- A configurable OpenGL ES 2.0 frontend with shader/program objects, VBOs, index buffers,
attributes, uniforms, programmable vertex/fragment execution, and framebuffer objects.
- RGB565, RGB888, BGR888, XRGB8888, ARGB8888, RGBA8888, and BGRA8888 framebuffers.
- Positive or negative framebuffer stride and top-left or bottom-left memory origins.
- Dear ImGui support through `imgui_impl_opengl2` and the OpenGL ES 2 mode of
  `imgui_impl_opengl3`.
- No EGL or window-system dependency in the core library.

The FULL archive exports all 142 OpenGL ES 2.0 core entry points. `make test` checks that symbol
set against the checked-in core list before running API, shader, rasterization, and framebuffer
readback tests. This is an implementation and regression claim, not Khronos conformance
certification; no Khronos CTS result or conformance mark is claimed.

The fixed-function path currently provides:

| Category | Supported functionality |
| --- | --- |
| Primitives | Points, lines, line strips/loops, triangles, triangle strips/fans, and quads |
| Transforms | Model-view, projection, and texture stacks; ortho, frustum, translation, scaling, and rotation |
| Depth/stencil | All eight comparison functions, front/back state, saturated references, write masks, and saturating/wrapping 8-bit operations |
| Color | RGBA interpolation, per-channel write masking, and 16-bit through 32-bit framebuffers |
| Lighting | Eight lights, ambient/diffuse/specular material, inverse-transpose normal transform, normalization, and color material |
| Fog | Linear, exponential, and squared-exponential eye-distance fog |
| Blending | Source/destination, constant color/alpha, and source-alpha-saturate factors, with separate add/subtract/reverse-subtract RGB and alpha equations |
| Textures | RGBA upload and sub-update, nearest/linear filtering, repeat/clamp, and texture env |
| Raster state | Viewport, scissor, front-face selection, culling, polygon modes, point size, and line width |
| Render targets | Default framebuffer plus texture or RGB565/RGBA4/RGB5_A1 renderbuffer color attachments, with depth/stencil attachments |
| Queries | Enable, binding, viewport, format-bit, implementation-limit, blend, depth, scissor, and color-mask state |

Two compile-time GLES2 profiles are provided. `MESAGL_GLES2_PROFILE_FULL` is the default and uses
the programmable software pipeline. It supports scalar, vector, and matrix expressions, including
matrix resizing and matrix/matrix, matrix/vector, vector/matrix, and scalar products;
component-wise scalar/matrix arithmetic and same-size matrix addition, subtraction, and division;
swizzles and strictly integral indexing, including writable local or structure-member vector
elements, matrix columns, matrix-column swizzles, matrix scalar elements, and scalar elements of
local or structure-member vector/matrix arrays through chained subscripts; local and uniform arrays; integer and Boolean
vectors; read-only
scalar/vector attributes and matrix attributes backed by consecutive vertex attribute slots, with
unused declarations and inactive pre-link bindings excluded from active resource counts;
resources referenced only by functions that are unreachable from `main` are likewise excluded
from active attribute, uniform, and varying counts and do not consume implementation limits;
active varying allocation uses the GLSL ES 1.00 fixed-orientation 4-component register model,
including shared rows for scalar, vector, and matrix-column values; two `mat2` values may share
the two halves of the same register rows. Rasterizer-provided `gl_FragCoord`, `gl_PointCoord`,
and `gl_FrontFacing` inputs do not consume the user-varying limit;
logical interpolator storage is kept separate from the advertised vector-register count so packed
values remain independently perspective-interpolated;
reachability resolves overloaded calls by their exact argument types so an unselected same-named
overload cannot keep its resources active;
active-resource discovery follows lexical scope, so same-named function parameters, local
variables, and structure members neither activate nor appear to write a global attribute,
uniform, or varying; an outer resource becomes visible again after an inner shadowing block ends;
active basic uniform arrays are shortened to the highest reachable element selected by a supported
integral constant expression, including earlier global `const int` values, while dynamic indexing
or whole-array use keeps the full declared size; reflection, uniform
locations, and per-stage vector accounting use that active size;
the overload-aware reachability graph is transient and built once per shader stage during linking,
rather than retained in the context or rebuilt for each reflected resource;
conditionals;
bounded `for`, `while`, and `do` loops, with loop-scoped `break` and `continue` that target the
innermost loop and are rejected at shader compilation outside loop bodies, including inside a
separately called function; user functions, return values, and `in`/`out`/`inout`
parameters; basic named structures with scalar/vector/matrix members, constructors, copying, and
function parameters; exact scalar/vector/matrix/structure function overload selection; bounded
structure arrays and nested structure reads; recursive structure value copying for initialization
and by-value function parameters; per-invocation mutable global scalar, vector, matrix,
array, and named-structure variables, including reads and writes from user functions; global and
local scalar/vector/matrix constants, including integral constant expressions used by local and
uniform array declarations; local constants and array bounds reject mutable-variable references,
self-reference, and user-function calls while allowing constant chains, constructors, and built-ins;
comma-separated local scalar, vector, matrix, array, and named-structure declarations with
independent initializers; required `const` initialization and exact initializer type matching for
every declarator, including later declarators and distinct named-structure types;
lexical block lifetime with legal inner-scope shadowing, same-scope duplicate rejection, and
reclaimed local array/structure storage across branch and loop iterations;
exact assignment typing for variables, array elements, structure members, and swizzles, with
duplicate swizzle lvalue components rejected; simple and compound assignments are expressions,
return their assigned value, associate right-to-left, and respect suppressed short-circuit paths;
typed writes to vertex/fragment built-ins and varying outputs;
matrix-element and varying-array component writes use read-modify-write semantics for external
interface storage, including assignment expressions and `out`/`inout` function write-back;
exact user-function return typing, including void/non-void return forms and structure types;
typed unary, arithmetic, scalar relational, component-complete scalar/vector/matrix and recursive
structure equality, logical, control-flow,
conditional, and left-to-right comma operators, including multiple `for` iteration expressions;
conditional branches accept matching recursive structure values while rejecting arrays and opaque
members;
scalar Boolean `^^` evaluates both operands at its precedence between `&&` and `||`; `&&`, `||`,
and `?:` type-check unselected expressions in a
side-effect-suppressed dry run so function calls and `out`/`inout` write-back occur only on the
selected path;
prefix and postfix `++`/`--` on writable integer or floating scalar/vector/matrix lvalues, preserving the
old or new expression value and respecting short-circuit dry runs;
exact scalar, vector, and matrix constructor component counts, with scalar splats, matrix
arguments restricted to single-argument matrix conversions, and matrix
resize construction but no silent truncation of excess arguments;
GLES 2.0 typed overloads for common angle, exponential, geometric, relational, and interpolation
built-ins, including scalar-edge vector `step` and scalar-limit vector `clamp`;
discard; perspective-correct scalar, vector, and matrix varyings and their screen-space
derivatives; multiple texture units; fixed-size varying arrays with constant or dynamic indexing,
with only statically used fragment inputs consuming interpolation storage and matrices and arrays
charged against the varying-vector limit by physical column count;
global `invariant` interface declarations, including built-in position/point and fragment
coordinate interfaces, with matching invariant qualifiers required across user varying links;
`#pragma STDGL invariant(all)` in both shader stages, propagated to shader outputs, with
cross-stage built-in invariance checks for `gl_Position`/`gl_FragCoord` and
`gl_PointSize`/`gl_PointCoord`; matching vertex/fragment varying invariance is
enforced from declarations even when the varying is inactive in both executable paths;
`gl_FrontFacing`, programmable `gl_PointSize`, and per-fragment `gl_PointCoord` for points;
`gl_PointCoord` uses the GLES2 upper-left point-sprite origin independently of framebuffer memory
origin;
the read-only `gl_DepthRange` structure and GLES2 `gl_Max*` implementation constants, including
their use in global constant expressions and array sizes;
single-target `gl_FragData[0]` output with `gl_MaxDrawBuffers == 1`, including constant-index
validation and the required prohibition on statically writing both `gl_FragColor` and
`gl_FragData`;
explicit mip levels and derivative-driven implicit mip selection for transformed coordinates,
including signed lambda and texture-bias handling before the minification/magnification decision; alpha,
depth, and stencil tests; slope-scaled polygon depth offset; blending; 2D and cube-map textures; and
texture-backed FBOs. Programmable fragments receive window-space `gl_FragCoord`, including the
interpolated reciprocal clip W component.

The FULL texture built-ins include `texture2D`, `texture2DProj`, `texture2DLod`,
`texture2DProjLod`, `textureCube`, and `textureCubeLod`. Projected vec3/vec4 coordinates preserve
derivatives through the perspective divide, and both 2D and cube-map samplers honor explicit mip
levels and fragment bias arguments. Explicit-LOD texture functions are restricted to vertex
shaders, while the optional bias overload is restricted to fragment shaders. `dFdx`, `dFdy`, and
`fwidth` are exposed through `GL_OES_standard_derivatives`, require an
enable/require extension directive, and are restricted to fragment shaders.
Fragment-only built-ins and `discard` are rejected in vertex shaders; vertex output built-ins are
rejected in fragment shaders.
Stage-invalid attributes and initializers on uniforms, varyings, or attributes are rejected.
`attribute`, `uniform`, and `varying` declarations are restricted to global scope at shader
compilation, including rejection inside function parameter lists and structure bodies.
Declaration qualifiers allow only one storage qualifier, require precision to be the final
qualifier before the type, and enforce the `invariant varying [precision]` ordering used by
GLSL ES 1.00. Precision qualifiers are accepted only on floating-point, integer, matrix, and
sampler types; Boolean, Boolean-vector, structure, and `void` declarations reject them.
Fragment varying inputs, uniforms, and vertex attributes are enforced as read-only, including
array-element and member writes.
Opaque sampler types are restricted to uniforms and input function parameters. Direct sampler
locals and ordinary globals, sampler return values, output/inout sampler parameters, and local
instances of structures containing samplers are rejected during shader compilation; uniform and
input-parameter sampler structures remain legal. Samplers and structures recursively containing
samplers cannot be assigned, compared, selected with the conditional operator, or constructed as
temporary structure values.
`MESAGL_GLES2_PROFILE_LITE` keeps a
smaller UI-oriented shader path for memory-constrained targets and omits the general GLSL VM from
the LITE archive. The limits in
`include/mesaGL/config.h` may be overridden by compiler definitions.
`MESAGL_MAX_SHADER_IDENTIFIER_LENGTH` and `MESAGL_MAX_SHADER_LVALUE_PATH_LENGTH` bound stored
shader names and flattened aggregate paths. FULL defaults to longer identifiers, while LITE keeps
the smaller storage footprint; both remain overrideable by the port build. Link-time type
expressions, function signatures, structure members, and VM aggregate paths use those configured
limits instead of legacy 48- or 64-byte temporary buffers. The comparatively large function
validation table is allocated only while compiling or linking and does not remain in a context.
`MESAGL_MAX_VERTICES` controls the internal software batch size rather than imposing an API-visible
draw-count limit. Larger point, line, triangle, strip, fan, and loop draws are divided into
topology-preserving batches.

All OpenGL ES 2.0 entry points declared by the bundled header are exported and exercised by the
headless test suite. This includes every scalar/vector/matrix uniform upload form, generic vertex
attribute setters and queries, object and render-state APIs, and their specified error paths.
MesaGL has not been certified by the Khronos conformance test suite, so it must not advertise
itself as a Khronos-conformant implementation. The GLSL ES frontend is an interpreter rather than
a conventional optimizing compiler. It includes bounded object-like and function-like macros, nested
macro expansion, `#define`, `#undef`, `#if`/`#ifdef`/`#ifndef`/`#elif`/`#else`, common integer
preprocessor expressions, `#version 100`, `#line` with optional source numbers, dynamic
`__LINE__`/`__FILE__`, and conditional nesting. `#line` operands are macro-expanded before their
constant integer expressions are evaluated and validated. Extension directives are rejected after
the first active non-preprocessor token. Comments are removed as preprocessing whitespace.
Backslash line continuations are rejected because backslash is not part of the GLSL ES 1.00 source
character set. Recursive macros stabilize by disabling the
currently active expansion chain, and active `#error` directives terminate compilation.
The tokens following an active `#error` directive are preserved in the shader information log.
Recognized `optimize(on/off)` and `debug(on/off)` pragmas are syntax-checked and restricted to
positions outside function definitions; unknown implementation pragmas remain ignorable as
required by the language.
`glShaderSource` retains zero-width boundaries between every supplied source string without
inserting characters into the compilation unit. Dynamic `__FILE__` and `__LINE__` expansion follows
those boundaries, including boundaries in the middle of a line or function and source numbering
after `#line`; explicit-length source containing NUL is retained for `glGetShaderSource` but rejected
at compilation because NUL is outside the GLSL ES 1.00 character set.
Preprocessor diagnostics prefix their messages with the mapped source-string number and logical
line, so errors after both ordinary boundaries and `#line` remain attributable to the original
source array.
Extension directives are parsed only in active conditional branches; supported `enable`,
`require`, `warn`, and `disable` behaviors are tracked in declaration order. Supported extensions
in `warn` mode remain enabled and report feature use through the shader information log; requests
for unsupported extensions warn or fail according to their requested behavior. Preprocessor
conditions include short-circuit `&&`/`||`; unselected expressions are still syntax-checked
without triggering arithmetic errors. The shader-language `?:` operator is deliberately rejected
inside preprocessor conditions because it is absent from the GLSL ES 1.00 preprocessor operator
set. Empty macro replacements remain empty,
identical token-wise redefinitions are accepted, conflicting redefinitions and duplicate
parameters are rejected, and reserved/predefined macros cannot be replaced or undefined.
Macro names, parameter names, and replacement lists reference the preprocessed source directly,
so they are not truncated by small fixed-size token buffers; aggregate source and expansion limits
remain configurable for bounded targets.
Fixed-size scalar, vector,
matrix, and named-structure arrays may be passed to functions with `in`, `out`, or `inout`.
Function parameter array lengths accept integral global constant expressions and reject mutable
length sources.
Arrays of arrays are rejected at declaration time because GLSL ES 1.00 supports only one array
dimension; repeated indexing remains legal when the first index selects a vector, matrix, or
structure member that is itself indexable. Local array bounds may use earlier local or global
`const int` expressions but reject mutable values. Structure-member array bounds are evaluated in
global scope and likewise require previously declared global `const int` values.
The FULL linker accepts integral global `const int` expressions as uniform array sizes; LITE keeps
literal uniform array sizes to avoid linking the general expression VM. Macro token pasting and
stringification are explicitly rejected because they are not legal GLES preprocessing features;
variadic macros are likewise outside GLSL ES 1.00. Complete semantic diagnostics are not yet
implemented. Attribute
arrays are rejected because GLSL ES 1.00 explicitly prohibits them. Nested structure reads, member
assignments, terminal swizzles, and compound assignments traverse named-structure chains up to the
configured aggregate-storage limits. Member selection is statically checked on ordinary values,
function results, and nested chains; selecting an undeclared structure member or applying a
swizzle to a scalar or matrix is a compile error. Fixed-size arrays may be members of named structures and may
themselves contain named structures; constant or dynamic integer indices work in reads and deep
lvalue chains. Comma-separated structure-member declarators have independent names and array
sizes. Anonymous and inline named structure declarations support multiple instances, arrays,
copying within the same declared type when it contains no array member, and deep member lvalues.
A structure member may use a previously declared structure type, but defining another named or
anonymous structure directly inside a structure is rejected by the FULL compiler, as required by
GLSL ES 1.00. Named structure types are visible only after their definition and within
their enclosing lexical block; local types do not leak into sibling blocks, other functions, or
global declarations. A nested structure type may shadow an outer type or variable, while a type
and variable—or two type declarations—cannot reuse a name in the same scope. Sampler containment,
constructor/member typing, and VM structure lookup resolve the nearest visible declaration rather
than another same-named declaration elsewhere in the shader. Unknown global declaration types are
rejected during linking;
separate anonymous declarations remain distinct types even when their layouts match. Decimal,
octal, and hexadecimal integer constants follow GLSL ES 1.00 lexical rules, including rejection
of invalid octal digits, malformed hexadecimal/exponent forms, identifier suffixes, and positive
literals beyond the implementation's 32-bit spelling range. Values through `0xffffffff` are
accepted; values outside the interpreter's advertised integer precision may produce undefined
results as permitted by GLSL ES 1.00. Integer division and compound division truncate toward zero, including
negative operands; a zero integer divisor fails shader execution without invoking host-language
undefined behavior. A plus or minus is treated as an exponent sign only while scanning a numeric
preprocessing token; arithmetic following a Boolean literal or an identifier ending in `e` still
receives normal static operand-type validation. Integer values use the interpreter's numeric storage and therefore
do not emulate every precision and overflow detail. Nearest-level and trilinear mip filters are implemented,
including the fragment `texture2D` bias argument. Derivatives propagate through constructors,
swizzles, indexing, arithmetic, matrix-vector multiplication, and common unary math operations;
common multi-argument math operations also propagate derivatives, while some discontinuous edge
cases remain implementation-defined. Geometry built-ins including distance, cross, reflect,
faceforward, and refract preserve screen-space derivatives through their vector operations.
`abs` propagates the signed derivative on both nonzero sides, while its cusp and discontinuous
functions retain the GLSL-defined undefined/zero-gradient behavior used by the interpreter.
Complete semantic diagnostics are still being expanded. Texture-LOD, texture-bias, derivative,
and other stage-specific built-ins are checked during compilation, including the
`GL_OES_standard_derivatives` enable requirement.
FULL compilation performs per-translation-unit function and expression validation before link,
while allowing unresolved function prototypes to be completed by another attached shader object.
It rejects writes to local constants, duplicate locals, array assignment/equality, array return
types, unsized array parameters, `void` variables, non-floating varying types, and structure
constructors with the wrong member count. Named local structure members retain their declared
types during constructor and built-in overload resolution rather than being treated as vector
swizzles. Structure constructors require each argument to exactly match its corresponding member.
Structure declarations require at least one uniquely named member, reject member initializers and
storage/const/invariant qualifiers, and retain the precision qualifiers permitted by the ES 1.00
structure grammar.
Local structure arrays and array-valued structure members retain their element type through chained
subscripts, member selection, swizzles, assignments, and function arguments. GLSL ES condition
declarations are executed with their specified loop scope for `while` and `for`; declarations in
`if` and `do-while` conditions are rejected because those grammar productions require expressions.
Loop-condition names may shadow an enclosing name, remain visible in the loop body and `for`
iteration expression, and expire before the statement following the loop. A `for` initializer and
condition share the loop scope, so they cannot declare the same name.
Comma-separated `for` declaration initializers retain the shared declared type for every name;
comma expressions in the iteration clause execute left to right with all side effects preserved.
Loop clauses and increment/decrement lvalues no longer pass through fixed 512-byte rewrite buffers,
and delimiter validation uses source-sized storage instead of imposing an undocumented 64-level
nesting ceiling. Common short clauses and direct lvalues remain stack-backed; longer clauses and
parenthesized lvalue normalization use the configured platform allocator.
Any legal expression may be used as an expression statement, rather than only declarations,
function calls, assignments, and increments. Arithmetic/comparison results may be discarded, while
conditional, logical, and comma expressions retain their specified side effects and short-circuit
behavior.
Empty expression statements are accepted as standalone statements and as control-flow bodies.
Linked vertex and fragment `main` bodies are stored at their exact source length, so valid shaders
are not silently truncated by a fixed executable-body buffer; an allocation failure instead causes
the link to fail with an information log while preserving any previously installed executable.
Structures containing arrays cannot be initialized, assigned, compared, or selected because GLSL
ES 1.00 has no array initializer or array value operations. They remain valid as function
parameters, whose copying is defined separately. Mutable global structures may use
constant-expression structure constructors as initializers. Scalar constructors accept scalar or vector inputs and select
the first component as required by GLSL ES 1.00; matrix inputs remain invalid.
The compiler reserves the complete lowercase `gl_` namespace, accepts only GLES2 core built-in
identifiers, ignores identifiers inside comments, and rejects unknown built-ins or user variables,
functions, and structure members using that prefix. GLSL ES 1.00 future-language words, including
`flat`, and every identifier containing two consecutive underscores are rejected after macro
expansion, while longer identifiers that merely contain a reserved word remain legal. Macro names
and parameters obey the same future-identifier rule; macro names prefixed with `GL_` are also
reserved. Unlike the C preprocessor, an undefined identifier in an evaluated GLSL ES 1.00 `#if`
or `#elif` operand is an error; an operand skipped by `&&`, `||`, or `?:` does not trigger it.
Programs may attach multiple compiled shader objects of either stage. Their translation units are
combined at link time, so helper-only shader objects need no `main`; the linker requires exactly
one `main` in each complete stage. Function prototypes without a definition in the combined stage
are rejected during linking. Function overloads are matched by return type, parameter types, and
array shape when comparing declarations, while overload selection uses the parameter signature as
required by GLSL ES: overloads that differ only by return type are rejected. Direct and indirect
recursive calls are rejected from the resolved overload call graph. Unnamed parameters are
accepted in prototypes and definitions. Calls must follow a matching declaration or definition;
a later definition without an earlier prototype does not make a forward call legal. Function
prototypes and definitions are accepted only at global scope; declarations nested inside another
function are rejected as required by GLSL ES 1.00. Parameter
qualifiers reject output-qualified `const`
parameters, repeated precision qualifiers, qualifiers placed after precision, and `const` placed
after a direction qualifier. Mismatched
parameter modes between a prototype and its definition, duplicate definitions, and
unresolved calls without a prototype are also rejected. User-function calls must match the
argument count and exact array shape of at least one
overload. Arguments bound to `out` or `inout` parameters must be writable lvalues; literals,
constructors, arithmetic results, constants, uniforms, read-only built-ins, and repeated-component
swizzles are rejected, as are fragment-stage `varying` inputs. Vertex-stage `varying` outputs,
writable variables, array elements, structure members, and unique
swizzles are accepted. Deep structure/array/vector/matrix scalar paths use the same lvalue resolver
for ordinary assignment and `out`/`inout` function write-back. Constructor, literal, declared
variable, array-element, and swizzle arguments also
participate in static overload type selection. Parenthesized and same-type arithmetic expressions
propagate that type, as do scalar/vector or scalar/matrix arithmetic, square matrix/vector products,
nested user calls with an unambiguous return type, scalar comparisons and logical expressions, and
same-type conditional branches. Known function return expressions are checked against their
declared return type; value-returning `void` functions and empty returns in non-void functions are
rejected. Statically typed `if`, `while`, `do-while`, and `for` conditions must be scalar `bool`.
Local and global constants require a constant-expression initializer. Constant expressions may
use constructors, GLSL built-in functions, implementation constants, and previously declared
constants; they reject mutable variables, uniforms, and user-function calls.
Known declaration initializers, ordinary assignments, swizzle/array-element assignments, and
arithmetic compound assignments are checked for exact GLSL type compatibility.
Conditional expressions independently require a scalar `bool` condition and identical true/false
branch types, including nested expressions used in returns and function arguments.
Logical operators require scalar booleans, ordered comparisons require matching scalar numeric
types, and equality operands are checked for compatible non-sampler, non-array types. Operators
reserved but unavailable in GLSL ES 1.00 (`%`, bitwise operators, and shifts) are rejected during
shader compilation rather than being exposed as implementation extensions.
Logical-not requires a scalar boolean. Unary numeric signs and prefix/postfix increment or
decrement require numeric scalar, vector, or matrix operands; increment/decrement additionally
requires a writable lvalue and rejects constructor results and repeated-component swizzles.
Statically typed array, vector, matrix, and nested structure subscripts must be scalar `int`.
Scalar, sampler, and non-array structure values are rejected as subscript bases, and constant
indices are checked against known array, vector, and matrix bounds. Runtime evaluation enforces
the same type, indexability, non-negative-index, and bounds rules for dynamically resolved
expressions. Static type inference consumes every subscript in an array/matrix/vector chain, so
terminal scalar types participate in initializer, assignment, overload, and `out`/`inout`
validation rather than falling back to runtime typing.
Writable lvalues may be parenthesized at any depth, including a parenthesized vector or structure
base followed by member selection. Assignment, compound assignment, prefix/postfix increment, and
`out`/`inout` copy-back preserve the selected storage instead of updating a temporary value;
whole-array function arguments retain the same behavior when their identifier is parenthesized.
Conditional expressions are always r-values, so a selected scalar, swizzle, structure member, or
matrix column is rejected as an assignment, increment, or `out`/`inout` target. A conditional
expression remains legal inside an array, vector, or matrix subscript and its integer result is
type-checked without consuming the surrounding `]` or assignment.
Vector swizzles are checked for one consistent naming set (`xyzw`, `rgba`, or `stpq`), one to four
components, and source-vector dimensional bounds. Repeated components remain legal for reads but
are rejected for writable swizzles. Static type propagation also follows unary numeric expressions,
prefix/postfix increment results, and parenthesized matrix/vector subscript chains, so swizzles on
their results receive the same dimensional checks. The VM enforces the same rules during execution.
Arithmetic operators reject boolean, sampler, array, and structure operands. Numeric scalar,
vector, and matrix combinations follow GLSL ES dimension and scalar-broadcast rules, including
matrix/vector multiplication and component-wise scalar/matrix arithmetic.
Scalar and vector constructors accept a single larger vector or matrix and consume its leading
components, as required by GLSL ES 1.00; these paths are exercised by fragment framebuffer
readback rather than compile-only coverage. Parenthesized ordinary lvalues remain writable for
assignment and increment operations.
Supported math, geometry, relational, derivative, and texture
built-ins propagate their GLSL result types. Built-in calls are rejected when their argument count
does not match a GLSL ES 1.00 overload. Known angle, exponential, common, interpolation, vector
relational, matrix, geometry, derivative, and sampler argument types are checked during linking,
including their permitted scalar/vector broadcast forms. GLSL ES 1.00 defines `abs`, `sign`,
`min`, `max`, and `clamp` only for floating-point `genType`; integer arguments are rejected rather
than accepting overloads introduced by later shading-language versions. Mixed integer and
floating-point calls are likewise rejected rather than implicitly converted. Other expressions
whose type cannot yet be proven remain runtime-typed.
Function names share the global namespace with variables and structure types, so conflicting names
are rejected while local variables may legally shadow a function. Named parameters and local
variables may not be redeclared in the same lexical scope. Nested and sibling blocks may reuse a
name, and leaving a block or `for` loop restores the enclosing binding; loop initializer names do
not leak beyond the loop. Static type propagation follows the same scope ancestry, so a closed
inner block cannot affect later return checks, assignments, or overload selection. Every name in
a comma-separated local declaration retains the shared base type and its own array shape; these
types participate in initializer checks, bounds checks, and overload-result inference.
single-statement `if`, `else`, `while`, `do`, and `for` bodies create implicit lexical scopes just
like explicit blocks. Variables, structure types, inline structure instances, and precision
defaults declared there may shadow outer names but expire before the following statement; sibling
branches may independently reuse a name.
The binding introduced by a local declarator begins after its initializer: an inner declaration's
initializer resolves an outer same-named variable, while an initializer that refers to itself
without an outer binding is rejected. Comma-separated declarators are evaluated left to right, so
each later initializer can read variables introduced by earlier declarators.
Local variables share call lookup scope with user and built-in functions. Calling a name while a
local variable shadows it is rejected; leaving the block restores the function. Because a new
binding starts after its initializer, `float helper = helper();` may initialize from a previously
declared same-named function before the local variable begins shadowing it.
User functions may overload a built-in name with a new parameter signature; overload resolution
selects the user function or the original built-in from the argument types. Redeclaring an exact
built-in signature is rejected, as required by GLSL ES 1.00. Both selection paths are covered by
fragment readback. A call that selects such a user overload remains a user-defined-function call
and is therefore rejected in a constant expression, even though its name is also a built-in name.
Ordinary
global variables may
not be redeclared, including when duplicate definitions arrive from separate attached shader
objects. Compatible repeated uniform, attribute, and varying declarations remain legal and are
validated by their corresponding interface rules. Identifier references are checked against the
active lexical scope even in dead branches; references to names from closed blocks, expired loop
initializers, another function's parameters, or globals declared later in the translation unit are
rejected. Types, structure members, swizzles,
functions, comments, scientific-notation exponents, and stage-valid built-ins are distinguished
from variable references during this check.
Function parameters support sized arrays and `in`, `out`, `inout`, and `const` semantics, including
copy-back of writable array arguments. Duplicate direction or `const` qualifiers are rejected as
malformed signatures; array copy-in/copy-out is verified through fragment output readback.
Every defined non-`void` function must contain a value-returning statement; an empty non-`void`
function is rejected during compilation rather than failing only if executed. The return need not
cover every control-flow path: a path that executes `return` produces that value, while falling
off the end yields a deterministic zero-initialized value for the otherwise undefined GLSL ES
result. This keeps valid partial-return functions executable without imposing behavior beyond the
language guarantee.
Detaching an object does not invalidate an already linked
executable, and delete-pending shader objects remain alive until their final attachment is removed.
If relinking a currently bound program fails, its link status and information log report that
failure while draw calls continue using the last successfully installed executable.
Cube-map filtering follows the OpenGL ES 2.0 rule that selects one face before filtering; it does
not add the cross-face seamless behavior required by later GLES versions. No compressed texture
format is advertised, which is a legal GLES2 configuration, and compressed uploads therefore
report `GL_INVALID_ENUM`. The software default framebuffer is single-sampled and reports zero for
both `GL_SAMPLE_BUFFERS` and `GL_SAMPLES`; application FBOs are likewise single-sampled. FBOs
expose the single color attachment required by GLES2, not desktop multiple render targets.
Each of the six cube-map faces can be used as an FBO color attachment. Tests clear and read back
every attached face, then sample all six through `samplerCube` after unbinding the FBO to verify
that rendering and sampling share the correct face storage.

`glGetShaderPrecisionFormat` reports the actual shared numeric model in both profiles: IEEE-style
float storage with exponent range `{127, 127}` and 23 mantissa bits, and float-backed integer
storage with exactly representable magnitude range `{24, 24}` and precision value zero. Default
precision statements are syntax-checked for legal qualifiers and scalar/sampler types. They may
appear globally, in a function block, or in a `for` initializer, and follow lexical scope: a nested
default applies only after its declaration and expires on leaving that block or unbraced control
statement. Structure bodies reject precision statements. Fragment floating-point declarations
must also have an explicit qualifier or a visible default float precision; constructor-only
expressions do not spuriously require one. Precision qualifiers
do not quantize arithmetic at each operation.

Texture uploads support the GLES2 uncompressed base formats, RGB565, RGBA4444, and RGBA5551,
including packed sub-image updates, unpack row alignment, mip levels, cube faces, and the default
name-zero objects for both texture targets. Sampling enforces GLES2 mip-chain and cube
completeness, including matching image format and upload type across every required mip level and
cube face, as well as non-power-of-two wrap/filter restrictions; incomplete textures produce
opaque black. Without advertising `GL_OES_texture_npot`, image definitions above level zero also
require power-of-two nonzero dimensions; rejected NPOT or zero-sized mip redefinitions preserve
the previous level. A mixed `UNSIGNED_BYTE`/packed-type mip chain is covered by framebuffer readback. Rejected
image, sub-image, and pixel-store calls preserve existing texture storage and unpack state. Upload
and readback stride arithmetic is overflow-checked for narrow `size_t` MCU targets.
Nearest and linear filtering use GLES texel-center coordinates. Repeat and mirrored-repeat wrap
the individual neighboring texels across image boundaries, while clamp-to-edge duplicates edge
texels; programmable and fixed-function sampling use the same rules. Coordinates are reduced
according to the selected wrap mode before conversion to texel indices, so very large finite
coordinates do not overflow host integers and mirrored-repeat boundaries follow the coordinate
period rather than an already-rounded texel index.
The minification/magnification crossover follows the GLES2 filter rules, including the `0.5`
threshold for linear magnification combined with nearest-level mipmap minification. Exact
half-level nearest-mipmap ties select the lower mip level. Both boundaries are covered by
framebuffer readback tests.
Framebuffer-to-texture copies support
all five GLES2 base formats, mip levels and cube faces; framebuffer samples outside the drawable
are treated as undefined data rather than an API error. Their internal framebuffer read uses a
tightly packed temporary buffer independently of the caller's `GL_PACK_ALIGNMENT`, and restores
that state afterward; non-default alignment is covered by content readback and ASan tests.
Copying from an RGB color buffer into `ALPHA`, `LUMINANCE_ALPHA`, or `RGBA` would introduce an
absent alpha component and is rejected without changing the destination image, as required by the
GLES2 color-buffer compatibility table. The accepted RGB-to-RGB path is covered through FBO copy
and `glReadPixels`.
Texture image and copy operations preflight the profile-specific maximum mip level and per-level
dimensions before allocating or reading pixels. Undefined sub-image destinations are distinguished
from legal zero-sized images, and rejected copies preserve both texture storage and pixel-store
state. Negative texture-image and sub-image dimensions take error precedence over an invalid
format/type combination. Renderbuffer storage first requires a bound renderbuffer before validating
its internal format and dimensions. The extension string advertises only implemented GLES
extensions; separate blend factors and equations remain GLES 2.0 core functionality rather than
being mislabeled as the desktop `GL_EXT_blend_func_separate` extension.
Fragment shader outputs are clamped to `[0, 1]` before their values participate in blend factors
and equations, including out-of-range alpha; the final blended result is clamped again during
framebuffer conversion.
BGRA8 uploads are exposed explicitly as `GL_EXT_texture_format_BGRA8888`; packed RGB565,
RGBA4444, RGBA5551, invalid packed-format pairs, and BGRA channel order are covered by headless
sampling tests. Applications can include `GLES2/gl2ext.h` for the advertised `GL_BGRA_EXT` and
`GL_OES_standard_derivatives` extension definitions. The public GLES2 headers also provide the
usual `GLES2/gl2platform.h` ABI macro override point for freestanding ports.

Core state queries include initialized drawable-sized viewport/scissor state, clamped clear color
and depth values, independent front/back stencil state, blend factors/equations/color, framebuffer
bindings, attachment-specific color/depth/stencil bit depths, renderbuffer component sizes, implementation
read formats, shader-compiler and
zero binary/compressed-format capabilities, and configurable shader resource limits.
Invalid Boolean, floating-point, and integer state queries preserve the caller's output storage;
conversions are performed only after recognizing a valid pname, so error paths never expose
uninitialized temporary values.
Float, integer, and boolean query entry points apply the GLES conversion category of each state:
normalized clear/blend/depth values map across the signed integer range, ordinary floats round to
the nearest integer, and every nonzero float maps to true. Current generic vertex attributes are
available through both float and integer queries. Empty shader-binary and compressed-format lists
write zero elements. Framebuffer attachment queries distinguish an unattached image from a named
texture/renderbuffer and reject object-specific pnames for the wrong attachment type. Floating
texture-parameter entry points require an exact legal enumerant and preserve state on rejection.
An unattached framebuffer point accepts only the object-type query; every other attachment pname
reports `GL_INVALID_ENUM`. Detaching a renderbuffer with name zero ignores the supplied
renderbuffer target as required by GLES2.
Mismatched
FBO attachment sizes report `GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS` separately from invalid
attachments. Texture attachments follow the GLES2 level-zero and color-renderable-format rules.
The framebuffer API also tracks texture objects selected for depth or stencil attachment points,
reports them through attachment queries, applies delete-pending attachment lifetime rules, and classifies their
core GLES2 color formats as incomplete rather than rejecting the attachment selector itself.
Depth and stencil renderbuffers own their actual backing storage, retain contents across FBO
switches, and are absent from rasterization when the corresponding attachment is absent. FBOs with
only depth and/or stencil images are complete when their dimensions agree; the software backend
uses an internal discardable color surface until a color image is attached, while `glReadPixels`
correctly rejects a framebuffer with no color attachment. Depth storage and polygon-offset units
use the advertised 16-bit depth resolution rather than hidden floating-point precision.
Depth `EQUAL` and `NOTEQUAL` comparisons operate on those quantized stored values exactly; they do
not merge adjacent depth levels through an additional floating-point epsilon. Shader
and program introspection rejects invalid objects and enumerants instead of silently fabricating
zero-valued results. Core defaults include `ONE/ZERO` blending and enabled dithering as required by
GLES2. Default and assigned stencil value/write masks are reduced to the advertised 8-bit
stencil precision. Fragment writes apply deterministic 4x4 ordered dithering to RGB565, RGBA4444, and
RGBA5551 framebuffers; disabling `GL_DITHER` restores uniform nearest-level quantization, while
clears remain undithered. Sample coverage capabilities are tracked even though this software
backend advertises zero multisample buffers.
Color and depth clears honor their respective write masks as well as the scissor box. Active
texture and mipmap/derivative hint queries report the current per-context state rather than fixed
defaults. Depth range, polygon offset and sample coverage state can be queried consistently through
the Boolean, integer and floating-point GLES2 query entry points. Every GLES2 capability state,
including blending, culling, depth/stencil/scissor tests, dithering, polygon offset, and the two
sample-coverage enables, is likewise available through all three generic query entry points in
addition to `glIsEnabled`.

Binding an unused nonzero texture, buffer, framebuffer, or renderbuffer name creates that GLES
object even when the name did not come from `glGen*`. Generated but never-bound names do not
satisfy `glIs*`; first binding does. Deletion clears current bindings, vertex-attribute buffer
references, and texture-unit bindings. Deleting a texture or renderbuffer detaches it from the
currently bound framebuffer; attachments in other framebuffer objects retain the delete-pending
object and its storage until their references are replaced or their framebuffer is deleted.

The implemented feature set is intended for Dear ImGui, framebuffer user interfaces, and simple
software-rendered 2D/3D applications. `make test` validates rendering through exact framebuffer
pixel reads without X11, including programmable shaders, control flow and functions, multiple
textures, cube maps, mip levels and implicit LOD, texture upload validation, structures, separate
stencil state, geometry-builtin derivatives, polygon offset, FBOs, all GLES2 uniform upload
entry points, every GLSL ES 1.00 core built-in function family, and both profile builds.
Every symbol in the GLES2 core entry-point list is called by at least one test; `glFlush`,
`glFinish`, and `glReleaseShaderCompiler` are also checked for successful no-op behavior, with
`glFlush` preserving an error already pending in the context.
The integrated framebuffer test combines VBO and index-buffer drawing, programmable loop and
function execution, a texture-backed color target, depth and stencil renderbuffers, two texture
units, scissoring, alpha blending, and RGBA readback. Shared triangle edges use a half-open
top-left fill rule, so indexed meshes neither blend shared-edge pixels twice nor leave cracks.
Homogeneous clipping tests cover all six clip-volume planes for triangles, lines, and points:
crossing triangles and lines remain visible after clipping, while primitives wholly outside a
plane and outside points produce no fragments.
Programmable line tests verify GLES2 major-axis wide-line replication, nearest-integer width
selection, half-open endpoint coverage, and perspective-correct varying interpolation.
Programmable points use the actual
floating-point shader size for pixel-center coverage rather than rounding it to an integer, and
expose GLES2 upper-left-origin `gl_PointCoord` values computed from the unrounded point center and
size, with isolated derivative state.
The triangle interpolation test uses deliberately different clip-space `w` values and checks both
a varying and `gl_FragCoord.w` at an interior pixel. Its expected RGBA result was cross-checked
against Mesa llvmpipe through a headless EGL OpenGL ES 2.0 pbuffer; the permanent regression test
itself remains framebuffer-only and has no EGL or Mesa dependency.
Fragment builtin tests exercise front- and back-facing primitives through `gl_FrontFacing`, verify
window-space `gl_FragCoord`, and carry `faceforward`, `refract`, and `matrixCompMult` through the
complete shader, rasterization, and pixel-readback path.
Readback tests cover the GLES2 `RGBA`/`UNSIGNED_BYTE` path for every supported framebuffer format,
both memory origins, positive and negative framebuffer strides, pack alignment and clipped
rectangles while checking untouched padding and out-of-bounds destination bytes with sentinels.
Framebuffer creation rejects undersized strides, invalid origins, and dimensions whose address
span cannot be represented. Unsupported GLES2 readback format/type pairs
report `GL_INVALID_OPERATION`; negative dimensions take precedence and report `GL_INVALID_VALUE`,
matching Mesa's GLES2 validation order.
Preprocessor tests also verify rejected versions/directives and that
`glGetShaderSource` preserves the application-provided source. Link tests cover mismatched shader
interfaces and varying-array lengths, uniform conflicts, unsupported declarations, comments,
configured resource limits, multi-object stages, and attach/detach/deferred-deletion lifetimes.
Function-link tests additionally enforce the GLSL ES 1.00 one-prototype rule and require matching
return, storage, `const`, and effective precision qualifiers between a prototype and definition;
precision matching accounts for the stage defaults and visible `precision` declarations without
making precision part of overload selection.
Failed relinks preserve the executable already in use, including its uniform storage and
framebuffer output, while the program's latest link status and subsequent `glUseProgram` calls
still report the failed link as required.
Framebuffer attachment queries follow the GLES 2.0 rule that an empty attachment reports
`GL_NONE` for `GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE`, but querying any other attachment pname
then produces `GL_INVALID_ENUM` without modifying the destination. This intentionally differs
from Mesa llvmpipe versions that return an object name of zero for the empty attachment.
Shader and program information-log length queries return zero when no log exists, and include the
terminating null character only when the implementation has produced a non-empty log.
Shader and program objects share one GLuint namespace. Simultaneously live objects never receive
the same name, and passing a name of the wrong object type reports `GL_INVALID_OPERATION` while a
name belonging to neither namespace reports `GL_INVALID_VALUE`.
As required for object deletion rather than ordinary object queries, deleting shader or program
name zero is a silent no-op. Nonzero unknown names retain the corresponding invalid-name error.
Attribute bindings requested after a successful link are deferred until the next successful
link; they do not mutate locations in the executable currently being rendered. Active uniform
maximum-name queries use the exact names returned by introspection, including flattened structure
and array members. Array introspection appends `[0]` directly into the caller's bounded output
buffer, so legal FULL-profile identifiers are not constrained by an unrelated temporary buffer;
long array names are covered by sanitizer-backed tests.
Long structure type and member identifiers are also exercised through construction, function
copy-in/copy-out, fragment execution, and exact framebuffer readback.
Legal matrix attributes reserve consecutive column locations, honor pre-link bindings, and consume
independent generic attribute inputs during vertex execution. GLSL ES 1.00 attribute arrays are
rejected during shader compilation, as required by the language version, rather than accepted as
a desktop-GL extension.

Draw calls validate primitive modes, counts, attribute formats, and complete VBO/EBO byte ranges
before vertex execution. Invalid or deleted buffer references report an error without partially
modifying the framebuffer. The same preflight is used by FULL and by the LITE fixed-pipeline
fallback. Vertex attributes expose the GLES2 default `(0, 0, 0, 1)` current value, size/type and
buffer-binding queries, and accept byte, short, 16.16 fixed-point, and floating-point input.
Signed normalized byte and short inputs use the GLES2 asymmetric mapping: the most-negative
value clamps to `-1`, while other negative values are divided by the positive maximum. Mixed
scalar and vector varyings carry these converted values through actual point rasterization and
framebuffer readback tests.
For calls with several invalid arguments, negative draw ranges are reported before invalid
primitive or index-type enumerants, and a missing buffer binding is reported before buffer-data
size or usage validation. With a valid buffer binding, a negative allocation size is reported
before an invalid usage enumerant, matching Mesa's GLES2 validation order. Invalid state-query enumerants
likewise take precedence over MesaGL's defensive null-output-pointer check.
Buffer allocation and sub-data updates validate targets, usage modes, bindings, and full
byte ranges before replacing or modifying storage, so rejected operations preserve existing buffer
data and binding state.

Uniform updates validate the current linked program, location, exact scalar/vector/matrix type,
array range, count, and the GLES-required non-transposed matrix layout before changing storage.
Rejected uniform calls preserve all previous values; location `-1` remains the specified silent
no-op when a linked program is current. A missing current executable takes error precedence over
count, transpose, and location validation. Comma-separated uniform declarators are linked independently, including per-declarator
array sizes, locations, active-uniform introspection, and uploads.
Uniform queries return every matrix component; floating-point values queried through
`glGetUniformiv` are rounded to the nearest integer rather than truncated.
Integer uploads to Boolean scalar and vector uniforms normalize every component to zero or one;
the normalized value is used consistently by shader execution and both uniform query APIs.
Program validation and draw calls reject sampler values outside the configured texture-unit range,
as well as simultaneous `sampler2D` and `samplerCube` access through the same unit. Rejected draws
leave the framebuffer unchanged. Zero-count draw calls still perform framebuffer-completeness and
current-program sampler validation, while avoiding vertex/index memory access and rasterization.
Validation also refreshes the program information log: invalid
sampler state and an unlinked program produce diagnostics, while successful validation clears the
previous validation message. Empty and otherwise unlinked programs report `GL_FALSE` through
`GL_VALIDATE_STATUS`, consistent with that failure log.
Linking independently enforces the configured active vertex, fragment, and combined sampler
limits. Active sampler-array elements and sampler leaves flattened from structure uniforms count
toward the appropriate stage; a sampler referenced in both stages counts in both stage totals.
Uniform interface compatibility is checked before inactive declarations are eliminated: same-name
declarations across stages must agree in basic type, resolved explicit-or-default precision, array
length, and complete structure layout. Matching redeclarations contributed by multiple shader
objects in one stage are accepted, while conflicting ones fail the link even if no executable
expression reads them.
FULL also supports named structure uniforms whose members are scalar, vector, matrix, Boolean,
integer, or sampler types. Active members use dotted names such as `material.color`; inactive
members consume no uniform storage, and uploaded member values are reassembled into the structure
during shader execution. Structure arrays such as `lights[2].color` and array-valued members such
as `material.weights[2]` support constant or dynamic shader indexing, active-uniform queries, and
contiguous member uploads. Both dimensions may be combined, as in
`lights[light_index].weights[weight_index]`; each outer structure element exposes its member array
with contiguous locations. Scalar nested structure members are recursively flattened for active
uniform queries and resolved component by component during VM execution, including inside an outer
structure array. One nested structure-array dimension is also supported, including combinations
such as `lights[light_index].surfaces[surface_index].tint`. Multiple nested structure-array
dimensions may be combined in one member chain; each intermediate constant or dynamic index is
resolved independently and each innermost leaf array keeps contiguous uniform locations. The
linker fingerprints complete
flattened structure layouts, so differing member types, resolved precision qualifiers, order, or
array sizes across shader stages are rejected even when the referenced leaf itself has the same
type. LITE continues to accept only
the basic uniform types and arrays used by its compact shader path.
Comma-separated vertex attributes likewise receive independent locations and active-resource
entries; attribute arrays remain prohibited by GLSL ES 1.00.
Comma-separated varying interfaces are assigned independent interpolation slots and matched per
declarator across stages, including array sizes.
GLSL ES 1.00 does not require a vertex shader to statically write `gl_Position` or a fragment
shader to statically write a color output; those values are undefined when omitted. MesaGL accepts
both cases and initializes omitted outputs to zero so framebuffer-only targets remain deterministic.

Raster state validates viewport/scissor sizes, clear masks, point and line widths, face-selection
enums, and preserves the previous state on rejected calls. Depth ranges are clamped to `[0, 1]`
without reordering their endpoints, and `GL_FRONT_AND_BACK` culling is supported. A positive
requested line width remains visible through state queries, while the framebuffer rasterizer
limits its effective footprint to the advertised implementation range. NaN is retained as API
state because it is not less than or equal to zero, but is converted to a safe one-pixel effective
width before software rasterization.
Stencil operations implement 8-bit saturating increment/decrement, wrapping variants, inversion,
reference/value masks, and separate front/back fail/depth-fail/pass state. Independent point and
line primitives explicitly use front-face stencil state, so they cannot inherit stale back-face
state from an earlier triangle; polygon-mode lines and points retain their originating polygon
face.

## Building

The core library requires a C99 compiler, Make, a C library, and a small subset of the math
library:

```sh
make
```

Available targets include:

```sh
make test
make examples
make run-showcase
make x11-example
make x11-blend-example
make x11-stencil-example
make run-x11-polygon-mode-example
make run-x11-line-width-example
make run-x11-texture-matrix-example
make run-x11-lighting-example
make run-x11-specular-example
make run-x11-multilight-example
make run-x11-fog-example
make run-x11-normalize-example
make x11-imgui-example
make clean
```

The X11 examples additionally require the X11 and Xext development files and `pkg-config`.

`make run-showcase` is the primary integration demo. It combines an animated texture-backed FBO,
depth testing, two-light material shading, specular highlights, fog, texture compositing,
framebuffer blending, and Dear ImGui through the GLES2 UI path in one X11 window.
The X11 ImGui adapter forwards mouse position and buttons, horizontal and vertical wheels,
keyboard keys and modifiers, UTF-8 text, focus changes, and native key metadata. The showcase
includes an editable text field in addition to the checkbox and slider for interactive validation.

### Blend Validation

Run `make run-x11-blend-example` to open the visual blend test. Each cell draws the same
semi-transparent red rectangle over a blue background:

- Top left: standard source-alpha blending; the overlap is a red/blue mixture.
- Top right: additive blending; the overlap is brighter than either input.
- Bottom left: source minus destination; the blue component is removed.
- Bottom right: destination minus source; the red component is removed.
- Top far right: constant-color blending; the red source is reduced to one quarter intensity.
- Bottom far right: constant-alpha blending; the result is mostly blue with a small red component.

`make test` also verifies subtract blending and channel write masks against exact RGB565 pixel
results without requiring a window system.

### Stencil Validation

Run `make run-x11-stencil-example`. The result must be a green diamond clipped horizontally by a
rectangle on a dark background. No green pixels may appear outside the diamond stencil mask.

## Framebuffer Port

A platform port supplies a `MesaGLPortConfig`:

```c
MesaGLPortConfig config = {
    .framebuffer = {
        .pixels = framebuffer,
        .width = width,
        .height = height,
        .stride = stride_bytes,
        .format = NTGL_RGB565,
        .origin = NTGL_ORIGIN_TOP_LEFT,
    },
    .allocator = {
        .alloc = platform_alloc,
        .free = platform_free,
        .user = platform_data,
    },
    .pixel_ops = {
        .linear_rgba8888_to_xrgb8888 = platform_linear_blit_span,
        .user = platform_data,
    },
    .present = platform_present,
    .user = platform_data,
};

MesaGLPortContext *context = mesaGLPortCreate(&config);
mesaGLPortMakeCurrent(context);

/* Render through the gl* or ntgl* API. */
mesaGLPortPresent(context);
mesaGLPortDestroy(context);
```

The `present` callback may be null when the framebuffer maps directly to display memory. A port
can use it to flush caches, wait for vertical blanking, copy a back buffer, or submit an LCD DMA
transfer. Platforms without a standard heap can provide custom allocation functions; otherwise,
the core uses `malloc` and `free`.

`pixel_ops` is optional and may be zero-initialized. It provides scanline-sized hooks for CPU,
DSP, or MCU vector acceleration without making the renderer depend on SSE, AVX, NEON, or any
other instruction set. The current hook accelerates RGBA8888 bilinear sampling into contiguous
XRGB8888 pixels. It receives two source rows, precomputed horizontal sample indices and weights,
and one vertical weight. The implementation returns nonzero only after writing the complete
span. Returning zero selects the built-in scalar C implementation for that row, so partial or
unsupported platform implementations cannot disable the portable fallback.

`mesaGLInitSIMDPixelOps()` performs runtime CPU detection on x86 and selects AVX2 when available,
with an SSE2 fallback. AVX2 processes two output pixels per iteration without requiring the whole
application to be built with `-mavx2`, so the same binary remains usable on older x86 processors.
It selects the NEON backend when `__ARM_NEON` is available. All implementations are covered by an
exact scalar-output comparison test. X11 and RT-Thread DM ports call this selector automatically.
Other ports may call it or install their own operations after a safe CPU feature probe.
Unsupported targets leave the operation table empty and use scalar C.

### RT-Thread DM port

`ports/rtthread_dm` integrates MesaGL with RT-Thread device-model graphics and input drivers. The
port discovers `fbN` and `inputN` devices in `auto` mode, or accepts explicit device names. It
uses `RTGRAPHIC_CTRL_GET_INFO` and `FBIOGET_VSCREENINFO` to select the MesaGL framebuffer format.
RGB565, RGBA4444, RGBA5551, RGB888, BGR888, XRGB8888, ARGB8888, and RGBA8888 layouts are accepted
when their framebuffer bitfields match.

Rendering uses a private back buffer. On a device with multiple framebuffer pages and a working
`ypanstep`, presentation copies into the next page and uses `FBIOPAN_DISPLAY` with
`FB_ACTIVATE_VBL`. Single-page devices use `RTGRAPHIC_CTRL_RECT_UPDATE` followed by
`RTGRAPHIC_CTRL_WAIT_VSYNC`. This follows the RT-Thread device-model HMI path and avoids rendering
into a page currently scanned out.

In `auto` mode the port attaches up to eight compatible input devices, allowing QEMU keyboard and
pointer devices to be separate `inputN` instances. It handles relative mouse motion, absolute
pointer coordinates, buttons, keys, single-touch, and the first active multitouch slot. Driver
callbacks enqueue events in a bounded ring; `mesaGLRTThreadDMPollEvents()` dispatches them from the
render thread, so UI libraries are not called from input-driver context.

The RT-Thread source list and include flags are provided by `ports/rtthread_dm/Makefile`. The example
`examples/rtthread_dm.c` exports this FinSH/MSH command:

```text
mesagl_dm [graphic-device|auto] [input-device|auto]
```

For example:

```text
mesagl_dm fb0 auto
```

The demo displays a Y-axis rotating colored triangle. Mouse or touch position moves it, a primary
button changes its top color, and Escape exits. The application must enable RT-Thread graphic,
input, device-model framebuffer, and FinSH/MSH support.

The optional Dear ImGui platform backend is
`ports/rtthread_dm/mesaGL_imgui_rtthread_dm.cpp`. It translates RT-Thread key codes, modifiers,
basic US keyboard text, mouse buttons, relative/absolute pointer input, and touch input to the
modern ImGui event API. `examples/rtthread_dm_imgui.cpp` combines it with the GLES2
`imgui_impl_opengl3` renderer and exports:

```text
mesagl_dm_imgui [graphic-device|auto] [input-device|auto]
```

The demo contains buttons, a slider, progress bar, editable text, and the standard Dear ImGui demo
window. Input events are dispatched by `mesaGLRTThreadDMPollEvents()` on the rendering thread.

Public interfaces are organized as follows:

- `include/GL/gl.h`: fixed-function OpenGL compatibility API.
- `include/GLES2/gl2.h`: OpenGL ES 2.0 API.
- `include/mesaGL/ntgl.h`: low-level software rasterizer API.
- `include/mesaGL/port.h`: platform integration API.
- `include/mesaGL/simd.h`: optional built-in SIMD pixel-operation selector.
- `ports/x11`: X11/MIT-SHM reference port.
- `ports/rtthread_dm`: RT-Thread device-model graphics and input port.

Memory-constrained platforms can override `MESAGL_MAX_VERTICES` and all shader execution limits
in `include/mesaGL/config.h` at compile time. In particular, local variables, aggregate storage,
uniform value slots, loop iterations, and function call depth have separate bounds. The interpreter never grows an
unbounded shader array or loop. FULL reserves substantially more temporary stack per shader
invocation and defaults to a 4096-iteration watchdog and 64 nested calls; a headless regression
executes 300 loop iterations through a 20-function chain. Framebuffer UI firmware should begin
with LITE and raise only the limits it needs.
Texture, renderbuffer, viewport, point-size, and line-width limits are configurable as well. The
reported GLES2 limits, validation rules, state clamping, and rasterizer use the same values; LITE
defaults are smaller than FULL defaults.

For a small build, compile the library and the application with:

```sh
-DMESAGL_GLES2_PROFILE=MESAGL_GLES2_PROFILE_LITE
```

Both sides must use the same profile definition. The Makefile produces `libmesaGL.a` for FULL and
`libmesaGL_lite.a` for LITE.

The default `libmesaGL.a` also keeps the fixed-function compatibility enumerants needed by
`imgui_impl_opengl2`. `libmesaGL_gles2.a` builds the same FULL programmable renderer with
`MESAGL_STRICT_GLES2=1`; this variant rejects desktop-only capability, `GL_CLAMP`, and
`GL_UNPACK_ROW_LENGTH` usage with the GLES2 error behavior. It also rejects fixed-pipeline point,
polygon, shading, matrix-mode, and matrix-value state queries without modifying their output.
Applications and the library must use
the same setting. Use the strict archive when exposing MesaGL specifically as an OpenGL ES 2.0
implementation, and the compatibility archive when both API families share one binary.

`MESAGL_ENABLE_UINT_ELEMENT_INDICES` defaults to `0`, so `glDrawElements` accepts only the GLES2
core index types `GL_UNSIGNED_BYTE` and `GL_UNSIGNED_SHORT`. Defining it as `1` also enables
desktop-style `GL_UNSIGNED_INT` element indices for applications using the fixed-function
compatibility API; that configuration intentionally relaxes GLES2 error behavior.

`MESAGL_ENABLE_SHADER_FAST_PATHS` defaults to `1`. The FULL linker recognizes the canonical
Dear ImGui GLES2 vertex/fragment program only after its complete declarations and executable
bodies match, then installs a compact native execution plan for its transform, varyings, texture
sample, and color modulation. All other shaders continue through the general GLSL ES interpreter.
Defining the option as `0` disables specialization for differential testing. On the reference
host, the 320 x 240 framebuffer ImGui test fell from roughly 0.319 seconds to 0.027 seconds per
process run; enabled and disabled paths produced the same 64-bit checksum over every RGB565 pixel.

The default object-pool sizes are deliberately different:

| Resource | FULL | LITE |
| --- | ---: | ---: |
| Contexts | 8 | 1 |
| Shader objects | 32 | 8 |
| Program objects | 16 | 4 |
| Buffer objects | 64 | 16 |
| Texture objects | 256 | 32 |
| Framebuffer/renderbuffer objects | 32 / 32 | 4 / 4 |
| Physical texture units | 8 | 1 |
| Vertex / fragment / combined sampler limits | 8 / 8 / 8 | 1 / 1 / 1 |
| Vertex attributes | 8 | 4 |
| Varying vectors | 8 | 2 |
| Internal varying interpolators | 32 | 2 |
| Uniform declarations/storage entries | 144 / 144 | 8 / 8 |
| Advertised vertex/fragment uniform vectors | 128 / 16 | 4 / 4 |
| Maximum shader array length | 128 | 4 |

These are capacity limits rather than allocations of texture or framebuffer pixel storage. Every
value can be overridden with the corresponding `MESAGL_MAX_*` definition. With the default GCC
build, FULL meets the GLES2 minimum vertex and fragment uniform-vector limits and tests both stage
limits in one linked program. The FULL linker counts only active uniforms and applies the GLSL ES
fixed-orientation four-component packing model independently to the vertex and fragment stages;
scalars and small vectors share rows, arrays remain contiguous rectangles, and `mat2` occupies two
complete rows. Uniform locations and upload storage remain independent from this resource packing;
the matching GLSL `gl_Max*`
constants use the same configuration values. Configuration checks require combined uniform storage and declaration
capacity to cover the advertised stage limits. LITE intentionally advertises smaller limits and is
therefore a compatibility profile rather than a conformant GLES2 resource configuration. With the
default GCC build on the reference host, removing the general GLSL VM and reducing the static pools lowers the
LITE archive's renderer/GLES state BSS from hundreds of KiB to roughly 42 KiB; exact code and data
sizes depend on the compiler, ABI, and overridden limits.

## Code Style

Project-owned C and C++ code uses Linux-style braces with four-space indentation. Files in the
Dear ImGui submodule retain their upstream formatting.

## License

MesaGL is distributed under the same MIT License used by the Mesa core library. See `LICENSE` for
the complete text. The Dear ImGui submodule retains its own license.
