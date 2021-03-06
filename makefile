
BASEOBJNAMES := hlslparser compiler optimizer common generator
HEADERS := src/hlslparser.hpp src/common.hpp src/compiler.hpp src/hlsloptconv.h
OBJS := $(patsubst %,obj/%.obj,$(BASEOBJNAMES))
CXXFLAGS := /W3 /MDd /GR- /D_DEBUG /Zi /c \
	/DHOC_MALLOC_REPLACEMENT=chkmalloc /DHOC_FREE_REPLACEMENT=chkfree

.PHONY: tools test html5test
tools: sltest.exe hlsloptconv.exe
test: sltest.exe
	sltest
test2: sltest.exe
	sltest -t tests/200-vars.hlsl
test3: sltest.exe
	sltest -t tests/300-preproc.hlsl
test4: sltest.exe
	sltest -t tests/400-func.hlsl
test5: sltest.exe
	sltest -t tests/500-intrin.hlsl
test5t: sltest.exe
	sltest -t tests/560-intrin-tex.hlsl
testbugs: sltest.exe
	sltest -t tests/900-bugs.hlsl
html5test: hlsloptconv.exe
	py runtests/html5-compile.py
four: four.exe
	four

sltest.exe: $(OBJS) obj/test.obj
	link /nologo /out:$@ $^ /DEBUG

four.exe: $(OBJS) obj/four.obj
	link /nologo /out:$@ $^ /DEBUG user32.lib gdi32.lib msimg32.lib \
	d3d9.lib d3d11.lib d3dcompiler.lib OpenGL32.lib

hlsloptconv.exe: $(OBJS) obj/cli.obj
	link /nologo /out:$@ $^ /DEBUG

obj/%.obj: src/%.cpp $(HEADERS) | obj
	cl /nologo /Fo$@ $(CXXFLAGS) $<

obj/%.obj: src/tools/%.cpp $(HEADERS) | obj
	cl /nologo /Fo$@ $(CXXFLAGS) $<

obj:
	mkdir obj

clean:
	del /F/Q sltest.exe hlsloptconv.exe four.exe *.ilk *.pdb obj\*.obj
