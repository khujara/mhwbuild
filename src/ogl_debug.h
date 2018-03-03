GL_DEBUG_CALLBACK(ogl_debug_callback) {
    if(severity == GL_DEBUG_SEVERITY_HIGH) {
    	GLenum err = glGetError();	
    	char *err_mess = (char *)message;
    	kh_assert(!"OpenGL Error encountered");
    } else if(severity == GL_DEBUG_SEVERITY_MEDIUM){
    	GLenum err = glGetError();	
    	if(err) {
    		kh_assert(!"OpenGL Error encountered");
    	}
    	char *err_mess = (char *)message;
    } else {
    	char *err_mess = (char *)message;
    }
}

KH_INTERN void
DEBUG_ogl_get_shader_log(GLuint shader) {
	GLint success;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
	if(!success) {
		GLchar info_log[4096];
		glGetShaderInfoLog(shader, sizeof(info_log), NULL, info_log);
		kh_assert(!"shader compilation failed");
	}
}

KH_INTERN void
DEBUG_ogl_get_prog_log(GLuint prog) {
	GLint success;
	glGetProgramiv(prog, GL_LINK_STATUS, &success);
	if(!success) {
		GLchar info_log[4096];
		glGetProgramInfoLog(prog, sizeof(info_log), NULL, info_log);
		kh_assert(!"program linking failed");
	}
}