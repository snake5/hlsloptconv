

#include "../compiler.hpp"

#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <stdarg.h>

#include <sys/types.h>
#include <sys/stat.h>

#ifdef _WIN32
#  include <direct.h>
#  define getcwd _getcwd
#  define mkdir _mkdir
#  define rmdir _rmdir
#  define stat _stat
#  include "msvc_dirent.h"
#else
#  include <unistd.h>
#  include <dirent.h>
#endif

#include <unordered_map>


using namespace HOC;


const char* outfile = "tests-output.log";
const char* outfile_errors = "tests-errors.log";


size_t g_memSize = 0;
size_t g_numAllocBytes = 0;
size_t g_numAllocs = 0;
size_t g_numFrees = 0;
size_t g_numBlocks = 0;
extern "C" void* chkmalloc(size_t sz)
{
	g_numAllocs++;
	g_numBlocks++;
	void* p = malloc(sz + 16);
	if (p)
	{
		memcpy(p, &sz, sizeof(sz));
		p = ((char*)p) + 16;
		g_memSize += sz;
		g_numAllocBytes += sz;
	}
	return p;
}
extern "C" void chkfree(void* p)
{
	if (!p)
		return;
	p = ((char*)p) - 16;
	g_memSize -= *(size_t*)p;
	g_numFrees++;
	g_numBlocks--;
	free(p);
}
void chkempty(const char* where)
{
	if (g_memSize != 0 || g_numBlocks != 0)
	{
		fprintf(stderr, "\n\n[%s] memory %s [allocs=%zu frees=%zu blocks=%zu size=%zd]\n\n",
			where,
			g_memSize > 0 ? "LEAK" : "OVERFREE",
			g_numAllocs, g_numFrees, g_numBlocks, g_memSize);
		exit(1);
	}
}


static int sgrx_snprintf(char* buf, size_t len, const char* fmt, ...)
{
	if (len == 0)
		return -1;
	va_list args;
	va_start(args, fmt);
	int ret = vsnprintf(buf, len, fmt, args);
	va_end(args);
	buf[len - 1] = 0;
	return ret;
}



typedef struct testfile_
{
	char* nameonly;
	char* fullname;
	int loadtf;
}
testfile;


int tf_compare(const void* p1, const void* p2)
{
	const testfile* f1 = (const testfile*) p1;
	const testfile* f2 = (const testfile*) p2;
	return strcmp(f1->nameonly, f2->nameonly);
}

int load_testfiles(const char* dir, testfile** files, size_t* count)
{
	DIR* d;
	struct dirent* e;
	struct stat sdata;
	char namebuf[260];
	testfile* TF;
	size_t TFM = 32, TFC = 0;
	d = opendir(dir);
	if (!d)
		return 0;
	TF = (testfile*) malloc(sizeof(testfile) * TFM);

	while ((e = readdir(d)) != NULL)
	{
		if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
			continue;
		if (strncmp(e->d_name, "!_", 2) == 0) continue;
		if (strstr(e->d_name, ".hlsl") != e->d_name + strlen(e->d_name) - 5)
			continue;

		sgrx_snprintf(namebuf, 260, "%s/%s", dir, e->d_name);
		stat(namebuf, &sdata);
		if (!(sdata.st_mode & S_IFREG))
			continue;

		if (TFC == TFM)
		{
			TFM *= 2;
			TF = (testfile*) realloc(TF, sizeof(testfile) * TFM);
		}

		TF[TFC].nameonly = (char*) malloc(strlen(e->d_name) + 1);
		strcpy(TF[TFC].nameonly, e->d_name);
		TF[TFC].fullname = (char*) malloc(strlen(namebuf) + 1);
		strcpy(TF[TFC].fullname, namebuf);
		TF[TFC].loadtf = strstr(e->d_name, "TF") != NULL;
		TFC++;
	}
	closedir(d);

	qsort(TF, TFC, sizeof(testfile), tf_compare);

	*files = TF;
	*count = TFC;
	return 1;
}

void free_testfiles(testfile* files, size_t count)
{
	testfile* f = files, *fend = files + count;
	while (f < fend)
	{
		free(f->nameonly);
		free(f->fullname);
		f++;
	}
	free(files);
}


/* test statistics */
int tests_executed = 0;
int tests_failed = 0;

int numallocs = 0;
static void* ext_memfunc(void* ud, void* ptr, size_t size)
{
	if (ptr) numallocs--;
	if (size) numallocs++;
	else if (!ptr)
		return NULL;
	(void) ud;
	return realloc(ptr, size);
}

/* check if equal, ignoring newline differences */
static int memstreq_nnl(const char* mem, const char* str)
{
	while (*mem && *str)
	{
		char c1 = *mem++;
		char c2 = *str++;

		if (c1 == '\r')
		{
			c1 = '\n';
			if (*mem == '\n')
				mem++;
		}
		if (c2 == '\r')
		{
			c2 = '\n';
			if (*str == '\n')
				str++;
		}

		if (c1 != c2)
			return 0;
	}
	return *mem == '\0' && *str == '\0';
}

typedef std::unordered_map<std::string, std::string> IncludeMap;

static int LoadIncludeFileTest(const char* file, const char* requester, char** outbuf, void* userdata)
{
	if (file == NULL)
	{
		delete[] * outbuf;
		return 1;
	}

	auto& includes = *static_cast<IncludeMap*>(userdata);
	auto it = includes.find(file);
	if (it != includes.end())
	{
		*outbuf = new char[it->second.size() + 1];
		memcpy(*outbuf, it->second.c_str(), it->second.size() + 1);
		return 1;
	}
	return 0;
}

bool g_runFXC = true;
bool g_runGLSLV = true;

std::string longestMyBuildShader = "<none?";
std::string longestFXCBuildShader = "<none?>";
std::string longestGLSLVBuildShader = "<none?>";
double longestMyBuildTime = 0;
double longestFXCBuildTime = 0;
double longestGLSLVBuildTime = 0;

ShaderVariable shaderVarBuf[32];
char shaderVarStrBuf[1024];
size_t nextBuildVarBufSize = 0;
size_t nextBuildVarStrBufSize = 0;
bool nextBuildVarRequest = false;
bool nextSlotAssignRequest = false;
bool nextHLSLSM3BufferRegsAreSlots = false;

static void exec_test(const char* fname, const char* nameonly)
{
	bool hasErrors = false;
	FILE* fpe = fopen(outfile_errors, "a");
	numallocs = 0;

	fprintf(fpe, "\n>>> test: %s\n", nameonly);
	printf("> running %20s\t", nameonly);

	FILE* fp = fopen(outfile, "a");
	fprintf(fp, "//\n/// O U T P U T  o f  %s\n//\n\n", nameonly);

	double tm1 = GetTime();
	{
		IncludeMap includes;
		int lastExec = -1000;
		std::string lastSource;
		std::string lastByprod;
		std::string lastShader;
		std::string lastErrors;
		std::string lastVarDump;
		char testName[64] = "<unknown>";
		std::string testFile = GetFileContents<std::string>(fname);
		/* parse contents
			syntax: <ident [a-zA-Z0-9_]> <string `...`> ...
			every item is a command, some commands are tests, failures will impact the return value
		*/
		const char* data = testFile.c_str();
		while (*data)
		{
			/* skip spaces */
			while (*data == ' ' || *data == '\t' || *data == '\r' || *data == '\n') data++;

			/* parse identifier */
			const char* ident_start = data;
			while (*data == '_' || *data == '/' ||
				(*data >= 'a' && *data <= 'z') ||
				(*data >= 'A' && *data <= 'Z') ||
				(*data >= '0' && *data <= '9')) data++;
			const char* ident_end = data;
			if (ident_start == ident_end)
			{
				fprintf(stderr, "%s - expected identifier at '%16s...'\n", fname, data);
				hasErrors = true;
				break;
			}
			std::string ident(ident_start, ident_end - ident_start);

			/* skip spaces */
			while (*data == ' ' || *data == '\t' || *data == '\r' || *data == '\n') data++;

			/* parse value */
			if (*data != '`')
			{
				fprintf(stderr, "%s - expected start of value (`) at '%16s...'\n", fname, data);
				hasErrors = true;
				break;
			}
			const char* value_start = ++data;
			bool esc = false;
			while (*data)
			{
				if (*data == '`' && !esc)
					break;
				if (*data == '\\' && !esc)
					esc = true;
				else esc = false;
				data++;
			}
			if (*data != '`')
			{
				fprintf(stderr, "%s - expected end of value (`) at '%16s...'\n", fname, data);
				hasErrors = true;
				break;
			}
			const char* value_end = data++;

			/* skip spaces */
			while (*data == ' ' || *data == '\t' || *data == '\r' || *data == '\n') data++;

			/* DECODE VALUE */
			std::string decoded_value;
			decoded_value.resize(value_end - value_start);
			if (decoded_value.size())
			{
				char* op = &decoded_value[0];
				const char* ip = value_start;
				while (ip < value_end)
				{
					if (*ip == '\\' && (ip[1] == '\\' || ip[1] == '`'))
						*op = *++ip;
					else
						*op = *ip;
					op++;
					ip++;
				}
				decoded_value.resize(op - decoded_value.data());
			}

			auto Source = [&](const std::string& src)
			{
				lastSource = src;
			};
			auto GetShaderStage = [](const std::string& cmdline)
			{
				if (cmdline.find("-S frag") != std::string::npos ||
					cmdline.find("/T ps_") != std::string::npos)
					return ShaderStage_Pixel;
				return ShaderStage_Vertex;
			};
			auto Compile = [&](ShaderStage stage, OutputShaderFormat outputFmt)
			{
				size_t allocsBefore = g_numAllocs;
				size_t allocBytesBefore = g_numAllocBytes;
				/* to detect buffer overruns in compilation ..
				.. using memory checking tools, allocate a new buffer ..
				.. with the exact size of the data */
				size_t memLen = lastSource.size() + 1;
				char* bc = new char[memLen];
				memcpy(bc, lastSource.c_str(), memLen);
				std::string strErrors, strCode, strByprod;
				double tm1 = GetTime();
				HOC_InterfaceOutput ifo;
				HOC_Config cfg;
				HOC_TextOutput toErrors = { &HOC_WriteStr_String<std::string>, &strErrors };
				HOC_TextOutput toCode   = { &HOC_WriteStr_String<std::string>, &strCode   };
				HOC_TextOutput toByprod = { &HOC_WriteStr_String<std::string>, &strByprod };
				cfg.loadIncludeFileFunc     = LoadIncludeFileTest;
				cfg.loadIncludeFileUserData = &includes;
				cfg.errorOutputStream = &toErrors;
				cfg.codeOutputStream  = &toCode;
				cfg.ASTDumpStream     = &toByprod;
				cfg.outputFmt = outputFmt;
				cfg.stage     = stage;
				if (nextBuildVarRequest)
				{
					ifo.outVarBuf        = shaderVarBuf;
					ifo.outVarBufSize    = nextBuildVarBufSize;
					ifo.outVarStrBuf     = shaderVarStrBuf;
					ifo.outVarStrBufSize = nextBuildVarStrBufSize;
					cfg.interfaceOutput  = &ifo;
				}
				if (nextSlotAssignRequest)
				{
					cfg.outputFlags |= HOC_OF_SPECIFY_REGISTERS;
					nextSlotAssignRequest = false;
				}
				if (nextHLSLSM3BufferRegsAreSlots)
				{
					cfg.outputFlags |= HOC_OF_HLSL3_BUFFER_SLOTS;
					nextHLSLSM3BufferRegsAreSlots = false;
				}
				lastExec = HOC_CompileShader("<memory>", bc, &cfg);
				lastShader = strCode;
				lastErrors = strErrors;
				lastByprod = strByprod;
				if (nextBuildVarRequest)
				{
					nextBuildVarRequest = false;

					lastVarDump = "\n";
					if (lastExec)
					{
						HOC_TextOutput to = { &HOC_WriteStr_String<std::string>, &lastVarDump };
						HOC_DumpShaderInterfaceOutput(&ifo, &to);
					}
					HOC_FreeInterfaceOutputBuffers(&ifo);
				}
				double tm2 = GetTime();
				fprintf(fp, "\ncompile time: %f seconds | %s\n",
					tm2 - tm1, lastExec ? "SUCCESS" : "FAILURE");
				if (tm2 - tm1 > longestMyBuildTime)
				{
					longestMyBuildTime = tm2 - tm1;
					longestMyBuildShader = testName;
				}
				fprintf(fp, "%s", lastByprod.c_str());
				fprintf(fp, "%s", lastShader.c_str());
				fprintf(fpe, "-- [%s] memory allocated: %zu blocks, %zu bytes\n",
					testName, g_numAllocs - allocsBefore, g_numAllocBytes - allocBytesBefore);
				fprintf(fpe, "-- compile (errors) --\n%s", lastErrors.c_str());
				delete[] bc;
				chkempty(testName);
			};
			auto Result = [&](const char* expected)
			{
				const char* lastExecStr = "<unknown>";
				switch (lastExec)
				{
				case 0: lastExecStr = "false"; break;
				case 1: lastExecStr = "true"; break;
				}
				if (strcmp(expected, lastExecStr) != 0)
				{
					printf("[%s] ERROR in '%s/result': expected %s, got %s\n",
						testName, ident.c_str(), expected, lastExecStr);
					if (strcmp(expected, "true") == 0)
					{
						printf("[%s] compile errors:\n%sintermediate dumps:\n%s",
							testName, lastErrors.c_str(), lastByprod.c_str());
					}
					hasErrors = true;
				}
				return strcmp(lastExecStr, "true") == 0;
			};
			auto HLSLShaderCmdLine = [](const std::string& args, const std::string& type)
			{
				std::string out = "fxc /nologo " + args + " ";
				if (args.find("/E") == std::string::npos)
					out += "/E main ";
				if (args.find("/T") == std::string::npos)
					out += "/T vs_3_0 ";
				out += ".tmp/shader." + type + ".hlsl";
				out += " 1>.tmp/shader." + type + ".bc";
				out += " 2>.tmp/shader." + type + ".err";
				return out;
			};
			auto HasSyntaxErrors = [](const std::string& errstr)
			{
				return errstr.find("error") != std::string::npos
					|| errstr.find("ERROR") != std::string::npos
					|| errstr.find("failed") != std::string::npos;
			};
			auto HasExecuteErrors = [](const std::string& errstr)
			{
				return errstr.find("/?") != std::string::npos
					|| errstr.find("is not recognized") != std::string::npos;
			};
			auto HasBuildErrors = [&](const std::string& errstr)
			{
				return HasSyntaxErrors(errstr) || HasExecuteErrors(errstr);
			};
			auto HLSL = [&](const std::string& args)
			{
				if (!g_runFXC)
					return;

				mkdir(".tmp");
				SetFileContents(".tmp/shader.rcmp.hlsl", lastShader);

				double tm1 = GetTime();
				system(HLSLShaderCmdLine(args, "rcmp").c_str());
				double tm2 = GetTime();
				if (tm2 - tm1 > longestFXCBuildTime)
				{
					longestFXCBuildTime = tm2 - tm1;
					longestFXCBuildShader = testName + std::string("[rcmp]");
				}
				fprintf(fp, "-- hlsl --\ncompile time [rcmp]: %f seconds\n", tm2 - tm1);

				auto errRcmp = GetFileContents<std::string>(".tmp/shader.rcmp.err", true);
				auto bcRcmp = GetFileContents<std::string>(".tmp/shader.rcmp.bc", true);

				fprintf(fpe, "-- hlsl --\nRECOMPILED:\n%s\n", errRcmp.c_str());
				fprintf(fp, "-- hlsl --\nRECOMPILED:\n%s\n", bcRcmp.c_str());

				if (HasBuildErrors(errRcmp))
				{
					printf("[%s] ERROR in '%s': compilation of recompiled shader failed, errors:\n%s\n",
						testName, ident.c_str(), errRcmp.c_str());
					hasErrors = true;
				}
			};
			auto HLSLOrigFail = [&](const std::string& args)
			{
				if (!g_runFXC)
					return;

				mkdir(".tmp");
				SetFileContents(".tmp/shader.orig.hlsl", lastSource);

				double tm1 = GetTime();
				system(HLSLShaderCmdLine(args, "orig").c_str());
				double tm2 = GetTime();
				if (tm2 - tm1 > longestFXCBuildTime)
				{
					longestFXCBuildTime = tm2 - tm1;
					longestFXCBuildShader = testName + std::string("[orig]");
				}
				fprintf(fp, "-- hlsl --\ncompile time [orig]: %f seconds\n", tm2 - tm1);

				auto errOrig = GetFileContents<std::string>(".tmp/shader.orig.err", true);

				fprintf(fpe, "-- hlsl --\nORIGINAL:\n%s\n", errOrig.c_str());

				if (HasExecuteErrors(errOrig))
				{
					printf("[%s] ERROR in '%s': compiler execution of BAD original shader failed:\n%s\n",
						testName, ident.c_str(), errOrig.c_str());
					hasErrors = true;
				}
				else if (HasSyntaxErrors(errOrig) == false)
				{
					printf("[%s] ERROR in '%s': compilation of BAD original shader was successful!\n",
						testName, ident.c_str());
					hasErrors = true;
				}
			};
			auto HLSLBeforeAfter = [&](const std::string& args)
			{
				if (!g_runFXC)
					return;

				mkdir(".tmp");
				SetFileContents(".tmp/shader.orig.hlsl", lastSource);
				SetFileContents(".tmp/shader.rcmp.hlsl", lastShader);

				double tm1 = GetTime();
				system(HLSLShaderCmdLine(args, "orig").c_str());
				double tm2 = GetTime();
				if (tm2 - tm1 > longestFXCBuildTime)
				{
					longestFXCBuildTime = tm2 - tm1;
					longestFXCBuildShader = testName + std::string("[orig]");
				}
				fprintf(fp, "-- hlsl --\ncompile time [orig]: %f seconds\n", tm2 - tm1);

				tm1 = GetTime();
				system(HLSLShaderCmdLine(args, "rcmp").c_str());
				tm2 = GetTime();
				if (tm2 - tm1 > longestFXCBuildTime)
				{
					longestFXCBuildTime = tm2 - tm1;
					longestFXCBuildShader = testName + std::string("[rcmp]");
				}
				fprintf(fp, "-- hlsl --\ncompile time [rcmp]: %f seconds\n", tm2 - tm1);

				auto errOrig = GetFileContents<std::string>(".tmp/shader.orig.err", true);
				auto errRcmp = GetFileContents<std::string>(".tmp/shader.rcmp.err", true);
				auto bcOrig = GetFileContents<std::string>(".tmp/shader.orig.bc", true);
				auto bcRcmp = GetFileContents<std::string>(".tmp/shader.rcmp.bc", true);

				fprintf(fpe, "-- hlsl_before_after --\nORIGINAL:\n%s\nRECOMPILED:\n%s\n",
					errOrig.c_str(), errRcmp.c_str());
				fprintf(fp, "-- hlsl_before_after --\nORIGINAL:\n%s\nRECOMPILED:\n%s\n",
					bcOrig.c_str(), bcRcmp.c_str());

				if (HasBuildErrors(errOrig))
				{
					printf("[%s] ERROR in '%s': compilation of original shader failed, errors:\n%s\n",
						testName, ident.c_str(), errOrig.c_str());
					hasErrors = true;
				}
				else if (HasBuildErrors(errRcmp))
				{
					printf("[%s] ERROR in '%s': compilation of recompiled shader failed, errors:\n%s\n",
						testName, ident.c_str(), errRcmp.c_str());
					hasErrors = true;
				}
				else if (bcOrig != bcRcmp)
				{
					printf("[%s] ERROR in '%s': result differs from expected\n", testName, ident.c_str());
					hasErrors = true;
				}
			};

			auto GLSLShaderCmdLine = [](const std::string& args, const std::string& type)
			{
				std::string out = "glslangValidator -i " + args + " ";
				if (args.find("-e") == std::string::npos)
					out += "-e main ";
				if (args.find("-S") == std::string::npos)
					out += "-S vert ";
				out += ".tmp/shader." + type + ".glsl";
				out += " 1>.tmp/shader." + type + ".out";
				return out;
			};
			auto GLSL = [&](const std::string& args)
			{
				if (!g_runGLSLV)
					return;

				mkdir(".tmp");
				SetFileContents(".tmp/shader.rcmp.glsl", lastShader);

				double tm1 = GetTime();
				system(GLSLShaderCmdLine(args, "rcmp").c_str());
				double tm2 = GetTime();
				if (tm2 - tm1 > longestGLSLVBuildTime)
				{
					longestGLSLVBuildTime = tm2 - tm1;
					longestGLSLVBuildShader = testName + std::string("[rcmp]");
				}
				fprintf(fp, "-- glsl --\ncompile time [rcmp]: %f seconds\n", tm2 - tm1);

				auto outRcmp = GetFileContents<std::string>(".tmp/shader.rcmp.out", true);

				fprintf(fpe, "-- glsl --\nRECOMPILED:\n%s\n", outRcmp.c_str());
				fprintf(fp, "-- glsl --\nRECOMPILED:\n%s\n", outRcmp.c_str());

				if (HasBuildErrors(outRcmp))
				{
					printf("[%s] ERROR in '%s': compilation of recompiled shader failed, errors:\n%s\n",
						testName, ident.c_str(), outRcmp.c_str());
					hasErrors = true;
				}
			};

			/* PROCESS ACTION */
			if (ident.size() >= 2 && ident[0] == '/' && ident[1] == '/')
			{
				strncpy(testName, decoded_value.c_str(), 64);
				testName[63] = 0;
				printf(".");
				fprintf(fp, "----- subtest:[%s] -----\n", testName);
				fprintf(fpe, "----- subtest:[%s] -----\n", testName);
			}
			else if (ident == "addinc")
			{
				size_t pos = decoded_value.find("=");
				if (pos == String::npos)
				{
					printf("[%s] ERROR in 'addinc': name/value separator '=' not found\n", testName);
					hasErrors = true;
				}
				else
				{
					includes[decoded_value.substr(0, pos)] = decoded_value.substr(pos + 1);
				}
			}
			else if (ident == "rminc")
			{
				includes.clear();
			}
			else if (ident == "request_vars")
			{
				nextBuildVarRequest = true;
				int vbs = 32, vsbs = 1024;
				sscanf(decoded_value.c_str(), " %d %d", &vbs, &vsbs);
				if (vbs < 0)
					vbs = 0;
				else if (vbs > 32)
					vbs = 32;
				if (vsbs < 0)
					vsbs = 0;
				else if (vsbs > 1024)
					vsbs = 1024;
				nextBuildVarBufSize = vbs;
				nextBuildVarStrBufSize = vsbs;
			}
			else if (ident == "request_specify_registers")
			{
				nextSlotAssignRequest = true;
			}
			else if (ident == "request_hlsl_sm3_buffer_slots")
			{
				nextHLSLSM3BufferRegsAreSlots = true;
			}
			else if (ident == "verify_vars")
			{
				if (!memstreq_nnl(lastVarDump.c_str(), decoded_value.c_str()))
				{
					printf("[%s] ERROR in 'verify_vars': expected '%s', got '%s'\n",
						testName, decoded_value.c_str(), lastVarDump.c_str());
					hasErrors = true;
				}
			}
			else if (ident == "source")
			{
				Source(decoded_value);
			}
			else if (ident == "source_replace")
			{
				auto splitpos = decoded_value.find("=>");
				if (splitpos == std::string::npos)
				{
					printf("[%s] ERROR in 'source_replace': "
						"find/replace separator '=>' not found\n", testName);
					hasErrors = true;
				}
				else
				{
					std::string strToFind = decoded_value.substr(0, splitpos);
					std::string strReplacement = decoded_value.substr(splitpos + 2);

					size_t i = 0;
					for (;;)
					{
						i = lastSource.find(strToFind, i);
						if (i == std::string::npos)
							break;
						lastSource.replace(i, strToFind.size(), strReplacement);
						i += strReplacement.size();
					}
				}
			}
			else if (ident == "compile_fail" || ident == "compile_fail_hlsl")
			{
				Compile(decoded_value == "pixel" ? ShaderStage_Pixel : ShaderStage_Vertex, OSF_HLSL_SM3);
				Result("false");
			}
			else if (ident == "compile_fail_with_hlsl")
			{
				Compile(decoded_value == "pixel" ? ShaderStage_Pixel : ShaderStage_Vertex, OSF_HLSL_SM3);
				Result("false");
				HLSLOrigFail(decoded_value);
			}
			else if (ident == "compile_fail_glsl_es100")
			{
				Compile(decoded_value == "pixel" ? ShaderStage_Pixel : ShaderStage_Vertex, OSF_GLSL_ES_100);
				Result("false");
			}
			else if (ident == "hlsl_before_after")
			{
				HLSLBeforeAfter(decoded_value);
			}
			else if (ident == "compile_hlsl")
			{
				Compile(GetShaderStage(decoded_value), OSF_HLSL_SM3);
				if (Result("true"))
					HLSL(decoded_value);
			}
			else if (ident == "compile_hlsl_before_after")
			{
				Compile(GetShaderStage(decoded_value), OSF_HLSL_SM3);
				if (Result("true"))
					HLSLBeforeAfter(decoded_value);
			}
			else if (ident == "compile_hlsl4")
			{
				Compile(GetShaderStage(decoded_value), OSF_HLSL_SM4);
				if (Result("true"))
					HLSL(decoded_value.size() ? decoded_value : "/T vs_4_0");
			}
			else if (ident == "compile_hlsl4_before_after")
			{
				Compile(GetShaderStage(decoded_value), OSF_HLSL_SM4);
				if (Result("true"))
					HLSLBeforeAfter(decoded_value.size() ? decoded_value : "/T vs_4_0");
			}
			else if (ident == "compile_glsl" || ident == "compile_glsl_140")
			{
				Compile(GetShaderStage(decoded_value), OSF_GLSL_140);
				if (Result("true"))
					GLSL(decoded_value);
			}
			else if (ident == "compile_glsl_es100")
			{
				Compile(GetShaderStage(decoded_value), OSF_GLSL_ES_100);
				if (Result("true"))
					GLSL(decoded_value);
			}
			else if (ident == "in_shader")
			{
				if (lastShader.find(decoded_value) == String::npos)
				{
					printf("[%s] ERROR in 'in_shader': not found '%s' in shader\n",
						testName, decoded_value.c_str());
					hasErrors = true;
				}
			}
			else if (ident == "not_in_shader")
			{
				if (lastShader.find(decoded_value) != String::npos)
				{
					printf("[%s] ERROR in 'not_in_shader': found '%s' in shader\n",
						testName, decoded_value.c_str());
					hasErrors = true;
				}
			}
			else if (ident == "check_err")
			{
				if (!memstreq_nnl(lastErrors.c_str(), decoded_value.c_str()))
				{
					printf("[%s] ERROR in 'check_err': expected '%s', got '%s'\n",
						testName, decoded_value.c_str(), lastErrors.c_str());
					hasErrors = true;
				}
			}
			else if (ident == "beep")
			{
				/* debug helper */
				static int which = 0;
				printf("[BEEP %d]", ++which);
			}
			else
			{
				printf("[%s] ERROR: unknown command '%s'\n", testName, ident.c_str());
				hasErrors = true;
			}
		}
	}
	double tm2 = GetTime();

	fprintf(fp, "\n\n");
	fclose(fp);

	tests_executed++;

	const char* sucfail = hasErrors == false ? " OK" : " FAIL";
	if (hasErrors)
		tests_failed++;
	printf("time: %f seconds |%s", tm2 - tm1, sucfail);

	fclose(fpe);
	printf("\n");
}

static void exec_tests(const char* dirname)
{
	int ret;
	size_t count;
	testfile* files, *f, *fend;
	fclose(fopen(outfile, "w"));
	fclose(fopen(outfile_errors, "w"));
	printf("\n");

	ret = load_testfiles(dirname, &files, &count);
	if (!ret)
	{
		printf("\n\nfailed to load tests! aborting...\n\n");
		exit(1);
	}

	double tm1 = GetTime();
	f = files;
	fend = files + count;
	while (f < fend)
	{
		exec_test(f->fullname, f->nameonly);
		f++;
	}
	double tm2 = GetTime();
	printf("\ntotal testing time: %f seconds\n", tm2 - tm1);

	printf("\n");
	printf("longest self compile: %s, %g seconds\n",
		longestMyBuildShader.c_str(), longestMyBuildTime);
	printf("longest fxc compile: %s, %g seconds\n",
		longestFXCBuildShader.c_str(), longestFXCBuildTime);
	printf("longest glslValidator compile: %s, %g seconds\n",
		longestGLSLVBuildShader.c_str(), longestGLSLVBuildTime);

	free_testfiles(files, count);
}

int main(int argc, char** argv)
{
	int i;
	char *testName = NULL, *dirname = "tests";
	setvbuf(stdout, NULL, _IONBF, 0);
	printf("\n//\n/// ShaderCompiler test framework\n//\n");

	for (i = 1; i < argc; ++i)
	{
		if ((!strcmp(argv[i], "--dir") || !strcmp(argv[i], "-d")) && i + 1 < argc)
			dirname = argv[++i];
		else if ((!strcmp(argv[i], "--test") || !strcmp(argv[i], "-t")) && i + 1 < argc)
			testName = argv[++i];
		else if (!strcmp(argv[i], "--no-fxc"))
			g_runFXC = false;
		else if (!strcmp(argv[i], "--no-glslv"))
			g_runGLSLV = false;
	}
	printf("- test directory: %s\n", dirname);
	if (testName)
	{
		printf("- one test: %s\n", testName);
	}

	if (testName)
	{
		printf("\n/// Executing test %s...\n", testName);
		fclose(fopen(outfile, "w"));
		fclose(fopen(outfile_errors, "w"));
		exec_test(testName, testName);
		return 0;
	}

	exec_tests(dirname);

	printf("\n///\n/// Tests failed:  %d  / %d\n///\n", tests_failed, tests_executed);

	return 0;
}

