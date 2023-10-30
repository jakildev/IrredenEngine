#!/usr/bin/python

# GL API Generation Script (GetGLAPI)
# ----------------------------------------------------------------------
#
# Copyright (c) 2018-2019, Sergey Kosarevsky sk@linderdaum.com
# (Modified by Evin Killian for the Irreden Game Engine 2022)
# All rights reserved.
#
# Redistribution and use of this software in source and binary forms,
# with or without modification, are permitted provided that the
# following conditions are met:
#
# * Redistributions of source code must retain the above
#   copyright notice, this list of conditions and the
#   following disclaimer.
#
# * Redistributions in binary form must reproduce the above
#   copyright notice, this list of conditions and the
#   following disclaimer in the documentation and/or other
#   materials provided with the distribution.
#
# * Neither the name of the assimp team, nor the names of its
#   contributors may be used to endorse or promote products
#   derived from this software without specific prior
#   written permission of the assimp team.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# ----------------------------------------------------------------------

GetErrorFunc = "GLenum glError = apiHook.glGetError();"
CheckErrorFunc = "IRProfile::glAssert(glError == GL_NO_ERROR, E2S(glError));"
LoggerPrefix = "IRProfile::glLogDebug"
WrapFuncs = []

# dont need this anymore
def typeNameToFormatter(f, typeName):
	if typeName == "GLuint": return "{}"
	if typeName == "GLint": return "{}"
	if typeName == "GLenum": return "{}"
	if typeName == "GLfloat": return "{}"
	if typeName == "GLdouble": return "{}"
	if typeName == "GLsizei": return "{}"
	if typeName == "GLbitfield": return "{}"
	if typeName == "GLboolean": return "{}"
	if typeName == "GLbyte": return "{}"
	if typeName == "GLchar": return "{}"
	if typeName == "GLuint64": return "{}"

	if typeName == "GLsizeiptr": return "{}"
	if typeName == "GLintptr": return "{}"
	if typeName == "GLsync": return "{}"

	if typeName == "const GLfloat": return "{}"
	if typeName == "const GLfloat*": return "{}"
	if typeName == "const GLint*": return "{}"
	if typeName == "const GLuint*": return "{}"
	if typeName == "const GLvoid*": return "{}"
	if typeName == "const void*": return "{}"
	if typeName == "const GLenum*": return "{}"
	if typeName == "const GLchar*": return "{}"
	if typeName == "const GLsizei*": return "{}"
	if typeName == "const GLuint64*": return "{}"
	if typeName == "const GLint64*": return "{}"
	if typeName == "const GLintptr*": return "{}"
	if typeName == "const GLchar* const*": return "{}"
	if typeName == "void**": return "{}"
	if typeName == "GLfloat*": return "{}"
	if typeName == "GLint*": return "{}"
	if typeName == "GLuint*": return "{}"
	if typeName == "GLvoid*": return "{}"
	if typeName == "void*": return "{}"
	if typeName == "GLenum*": return "{}"
	if typeName == "GLchar*": return "{}"
	if typeName == "GLsizei*": return "{}"
	if typeName == "GLuint64*": return "{}"
	if typeName == "GLint64*": return "{}"
	if typeName == "GLintptr*": return "{}"

	if typeName == "void": return ""
	if typeName == "": return ""
	f.write( "ERROR: uknown type: ", typeName )
	exit(255)

def argNameToConverter(typeName, argName):
	#if argName[-1] == ")":
	#	constPointerExpression = "(const void*)("+argName+", *("+argName+")"
	#else:
	#	constPointerExpression = "(const void*)("+argName+"), *("+argName+")"
	#if argName[-1] == ")":
	#	pointerExpression = "(void*)("+argName+", *("+argName+")"
	#else:
	#	pointerExpression = "(void*)("+argName+"), *("+argName+")"
	constPointerExpression = "(const void*)("+argName+")"
	pointerExpression = "(void*)("+argName+")"
	if typeName == "GLenum": return "E2S("+argName+")"
	if typeName == "GLbitfield": return "(unsigned int)("+argName+")"
	if typeName == "GLboolean": return "(unsigned int)("+argName+")"
	if typeName == "GLbyte": return "(unsigned int)("+argName+")"
	if typeName == "GLchar": return "(unsigned int)("+argName+")"
	if typeName == "const GLfloat*": return constPointerExpression
	if typeName == "const GLint*": return constPointerExpression
	if typeName == "const GLuint*": return constPointerExpression
	if typeName == "const GLvoid*": return constPointerExpression
	# if typeName == "const void*": return "(const void*)("+argName+")"
	if typeName == "const GLenum*": return constPointerExpression
	if typeName == "const GLchar*": return constPointerExpression
	if typeName == "const GLsizei*": return constPointerExpression
	if typeName == "const GLuint64*": return constPointerExpression
	if typeName == "const GLint64*": return constPointerExpression
	if typeName == "const GLintptr*": return constPointerExpression
	if typeName == "const GLchar* const*": return constPointerExpression
	if typeName == "void**": return "(void*)("+argName+")"
	if typeName == "GLfloat*": return pointerExpression
	if typeName == "GLint*": return pointerExpression
	if typeName == "GLuint*": return pointerExpression
	if typeName == "GLvoid*": return pointerExpression
	# if typeName == "void*": return argName+", *("+argName+")"
	if typeName == "GLenum*": return pointerExpression
	if typeName == "GLchar*": return pointerExpression
	if typeName == "GLsizei*": return pointerExpression
	if typeName == "GLuint64*": return pointerExpression
	if typeName == "GLint64*": return pointerExpression
	if typeName == "GLintptr*": return pointerExpression

	return argName

def generateStub(f, func):
	func = func.replace("GLAPI ", "").replace("APIENTRY ", " ").replace("*", "* ").replace(" *", "*").replace("(void)", "()")
	if (func[-1] == ";"):
		func = func[0:-1]
	args = func.split("(")
	funcName = args[0].split()[-1]
	returnType = args[0].split(funcName)[0].strip()
	if not funcName in WrapFuncs:
		return
	funcArgs = args[1].split(",")
	call = funcName + "("
	allArgs = []
	for arg in funcArgs:
		argTypeName = arg.strip().split(" ")
		argFullTypeName = " ".join(argTypeName[:-1])
		if len(argFullTypeName) > 0:
			allArgs.append([argFullTypeName, argTypeName[-1]])
		if argTypeName[-1][-1] == ")":
			if argTypeName[-1] == "void)":
				call = call + ")"
				continue
			call = call + argTypeName[-1]
		else:
			if argTypeName[-1] == "void":
				argTypeName[-1] = ""
			call = call + argTypeName[-1] + ", "
	f.write(returnType + " GLTracer_" + funcName + "(" + args[1] + "\n")
	f.write("{\n")
	guardString = ""
	for a in allArgs:
		guardString = guardString + typeNameToFormatter(f, a[0]) + ", "
	if len(allArgs) > 0: guardString = guardString[:-2]
	guardString = guardString + ")\"";
	for a in allArgs:
		guardString = guardString + ", " + argNameToConverter(a[0], a[1])
	if len(allArgs) > 0:
		f.write("\t" + LoggerPrefix + "(\"" + funcName + "(" + guardString + ";\n")
	else:
		f.write("\t" + LoggerPrefix + "(\"" + funcName + "()\\n\");\n")
	if returnType == "void":
		f.write("	apiHook."+call + ";\n")
		f.write("\t" + GetErrorFunc + "\n")
		f.write("	" + CheckErrorFunc + "\n")
	else:
		f.write("	"+returnType + " const r = apiHook." + call + ";\n")
		f.write("\t" + GetErrorFunc + "\n")
		f.write("	" + CheckErrorFunc + "\n")
		f.write("	return r;\n")
	f.write("}\n")
	f.write("\n")

def parseFuncs():
	global WrapFuncs
	lines = open("funcs_list.txt").readlines()
	for l in lines:
		func = l.split()[-1]
		WrapFuncs.append(func)
	WrapFuncs.sort()

def main():
	parseFuncs()
	# TODO: FIX ENCODING ISSUE
	f = open("GLAPITrace.h", "w")
	f.write("/**GLAPITrace.h file generated by GetGLAPI.py\n")
	f.write(" * Script created by Sergey Kosarevsky sk@linderdaum.com\n")
	f.write(" * Modified by Evin Killian jakildev@gmail.com for the Irreden Game Engine.\n")
	f.write(" */\n")
	f.write("#include <string>\n")
	f.write("#include <inttypes.h>\n")
	f.write("#include \<irreden/ir_profiling.hpp>\"\n")
	f.write("\n")
	f.write("namespace\n")
	f.write("{\n")
	f.write("	GL4API apiHook;\n")
	f.write("} // namespace\n")
	f.write("\n")
	f.write("using PFNGETGLPROC = void* (const char*);\n")
	f.write("\n")
	f.write("#define E2S( en ) Enum2String( en ).c_str()\n")
	f.write("extern std::string Enum2String( GLenum e );\n")
	f.write("\n")
	lines = open("glcorearb.h").readlines()
	for l in lines:
		if l[0:5:] == "GLAPI":
			generateStub(f, l.strip())

	f.write("#define INJECT(S) api->S = &GLTracer_##S;\n")
	f.write("\n")
	f.write("void InjectAPITracer4(GL4API* api)\n");
	f.write("{\n")
	f.write("	apiHook = *api;\n")
	Hooks = []
	for l in lines:
		if l[0:5:] == "GLAPI":
			func = l.strip()
			args = func.split("(")
			funcName = args[0].split()[3]
			if (funcName in WrapFuncs) and (funcName != "glGetError"):
				Hooks.append("	INJECT(" + funcName + ");\n")
	Hooks.sort()
	for func in Hooks:
		f.write(func)
	f.write("}\n")
	f.write("\n")
	f.write("#define LOAD_GL_FUNC(func) api->func = ( decltype(api->func) )GetGLProc(#func);\n")
	f.write("\n")
	f.write("void GetAPI4(GL4API* api, PFNGETGLPROC GetGLProc)\n")
	f.write("{\n")
	Funcs = []
	for l in lines:
		if l[0:5:] == "GLAPI":
			func = l.strip()
			args = func.split("(")
			funcName = args[0].split()[3]
			if funcName in WrapFuncs:
				Funcs.append("	LOAD_GL_FUNC(" + funcName + ");\n")
	Funcs.sort();
	for func in Funcs:
		f.write(func)
	f.write("}\n")
	f.write("\n")
	# generate API struct
	out = open( "GLAPI.h", "wt" )
	for line in WrapFuncs:
		line.strip();
		if str.find( line, "//" ) == 0 or line == "": continue
		typeName = "PFN" + line.upper() + "PROC"
		NumTabs = 17 - int( len(typeName) / 3 )
		out.write( '\t' + typeName + '\t'*NumTabs + line + ';\n' )

if __name__ == "__main__":
    main()
