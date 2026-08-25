#ifndef MESAGL_GLES2_INTERNAL_H
#define MESAGL_GLES2_INTERNAL_H

const void *mesaGLResolveElementPointer(const void *pointer);
void mesaGLPrepareGLES2Draw(void);
void mesaGLGLES2GetIntegerv(unsigned int pname, int *value, int *handled);
void mesaGLGLES2ReleaseCurrentContext(void);

#endif
