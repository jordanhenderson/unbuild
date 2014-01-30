#include <cstdio>
#ifdef _MSC_VER
#pragma warning(disable: 4996)
#include <direct.h>
#else
#include <unistd.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include "rapidxml.hpp"

using namespace std;
using namespace rapidxml;

#ifdef _MSC_VER
#define getcwd _getcwd
#define mkdir _mkdir
#define stat _stat
#define PLATFORM 0
#else
#define mkdir(x) mkdir(x, 0755)
#define PLATFORM 1
#endif
#define PATH_SEP "/"
#define DEFAULT_OUTPUT_SUFFIX ".o"
#define OPERATION_LINK 0
#define OPERATION_LIB 1
#define COMPILER_MSVC 0
#define COMPILER_GCC 1

static int COMPILER = -1;
vector<string> COMPILER_BINS { "cl", "gcc" };
vector<string> COMPILER_NOLINK { "/c", "-c" };
vector<string> OUTPUT_LINK { "link", "gcc" };
vector<string> OUTPUT_LIB { "lib", "ar r " };
vector<string> OUTPUT_LINK_EXT { ".exe", "" };
vector<string> OUTPUT_LINK_OUTPUT { "/OUT:", "-o " };
vector<string> OUTPUT_LIB_EXT { ".lib", ".a" };
vector<string> COMPILER_OUTPUT { "/Fo", "-o " };
vector<string> COMPILERS { "msvc", "gcc" };
vector<string> COMPILER_FLAG{ "/", "-" };
vector<string> OUTPUT_NULL{ "nul", "/dev/null" };
vector<string>* OPERATIONS[] = { &OUTPUT_LINK, &OUTPUT_LIB };
const string STR_WIN = "win32";
const string STR_LINUX = "linux";
const string STR_APPLE = "osx";
const string DEFAULT_OUTPUT_DIR = "output";
const string STR_APP = "app";
const string STR_STATIC = "static";
const string EMPTY_STR = "";
const string PATH_SEP_WIN = "\\";
const string PATH_SEP_LINUX = "/";
const string COMMAND_SEP_WIN = "&&";
const string COMMAND_SEP_LINUX = ";";
const string DEFAULT_ARCH = "32";

#ifdef _WIN32
#define CHECK_OS_STR STR_WIN
#endif

#ifdef __APPLE__
#define CHECK_OS_STR STR_APPLE
#define NO_RESPONSE_FILE 1
#else
#ifdef __GNUC__
#define CHECK_OS_STR STR_LINUX
#endif
#endif


struct output {
	string compiled_files;
	string extra_files;
	string output_name;
};

struct build_flags {
	string compiler_flags;
	string linker_flags;
	unordered_map<string, string> ext_flags;
};


struct sourcefile {
	string filename;
	string output;
	string includes;
	build_flags* flags = NULL;
};

struct flags {
	int safemode = 0;
	string config;
	string arch;
};

static flags FLAGS;

bool endsWith(const string &a, const string &b) {
	if (a.length() >= b.length()) {
		return (a.compare(a.length() - b.length(), b.length(), b) == 0);
	}
	else return false;
}

/**
 * @brief Tokenize a string into a container, tokens by the provided delimiter.
 * @param str the string to tokenize
 * @param tokens the output container
 * @param delimiters the delimiters on which to tokenize
 * @param trimEmpty keep empty tokens
 */
template < class T >
void tokenize(const std::string& str, T& tokens,
	const std::string& delimiters = " ", bool trimEmpty = false)
{
	typedef T Base; typedef typename Base::value_type VType;
	typedef typename VType::size_type SType;
	std::string::size_type pos, lastPos = 0;
	while (true)
	{
		pos = str.find_first_of(delimiters, lastPos);
		if (pos == std::string::npos)
		{
			pos = str.length();

			if (pos != lastPos || !trimEmpty)
				tokens.push_back(VType(str.data() + lastPos,
				(SType)pos - lastPos));

			break;
		}
		else
		{
			if (pos != lastPos || !trimEmpty)
				tokens.push_back(VType(str.data() + lastPos,
				(SType)pos - lastPos));
		}

		lastPos = pos + 1;
	}
}

/**
 * @brief macro_replace expands a $() macro.
 * @param key the key of the $(...) macro
 * @return the expanded macro.
 */
const string& macro_replace(const string& key) {
	if (key == "OUTPUT") {
		return (FLAGS.config.empty() ? DEFAULT_OUTPUT_DIR : FLAGS.config);
	}
	else if (key == "SEP") {
#ifdef _WIN32
		return PATH_SEP_WIN;
#endif
#ifdef __GNUC__
		return PATH_SEP_LINUX;
#endif
	}
	else if (key == "CSEP") {
#ifdef _WIN32
		return COMMAND_SEP_WIN;
#endif
#ifdef __GNUC__
		return COMMAND_SEP_LINUX;
#endif
	}
	else if (key == "ARCH") {
		return (FLAGS.arch.empty() ? DEFAULT_ARCH : FLAGS.arch);
	}

	else return EMPTY_STR;
}


/**
 * @brief parse_string identifies and replaces $() macros with the appropriate
 * value, returning the expanded form. No recursive macros, simple substituion
 * supported only.
 * @param src
 * @return the expanded string, with $() macros replaced with appropriate values.
 */
string parse_string(const string &src) {
	string output;
	size_t size = src.size();
	output.reserve(size);
	//Iterate over every character, looking for $()
	for (unsigned int i = 0; i < size; i++) {
		if (i < size - 2 && src[i] == '$' && src[i + 1] == '(') {
			for (unsigned int j = i + 2; j < size; j++) {
				if (src[j] == ')') {
					//Found a macro, push the expanded macro to the output string.
					string macro = src.substr(i + 2, j - i - 2);
					output += macro_replace(macro);
					i += j - i;
					break;
				}
			}
		}
		else output += src[i];
	}
	return output;
}

/**
 * @brief check_os compares the current host os with os= values
 * @param child the xml node containing the os attribute
 * @return 1 if the os attribute matches, 0 otherwise.
 */
int check_os(xml_node<>* child) {
	xml_attribute<>* os = child->first_attribute("os"); 
	if (os != NULL) { 
		string os_str = string(os->value(), os->value_size()); 
		if (os_str != CHECK_OS_STR) 
			return 0;
	}
	return 1;
}

/**
 * @brief load_project opens the provided path, reading the project data
 * into a buffer.
 * @param path the project xml path.
 * @return the loaded file.
 */
char* load_project(const char* path) {
	int sz = 0;
	char* project_data = NULL;
	FILE* fp = NULL;
	size_t read = 0;
	if (!(fp = fopen(path, "r"))) {
		fprintf(stderr, "Error: Could not open project.xml.");
		return NULL;
	}

	fseek(fp, 0L, SEEK_END);
	sz = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	project_data = new char[sz + 1];
	read = fread(project_data, sizeof(char), sz, fp);
	project_data[read] = '\0';
	fclose(fp);
	return project_data;
}

int run_command(const char* cmd) {
	printf("%s\n", cmd);
	if (!FLAGS.safemode) return system(cmd);
	return 0;
}

int compile_file(sourcefile* file) {
	//Run the configured compiler.
	string command;
	string* filename = &file->filename;
	int asm_win = 0;
	if(COMPILER == COMPILER_MSVC && (endsWith(*filename, ".asm") || endsWith(*filename, ".s"))) {
		command = "ml";
		if(FLAGS.arch == "64") command += "64";
		asm_win = 1;
	} else {
		command = COMPILER_BINS[COMPILER];
	}
	const string sep = " ";
	command += sep;	
	command += COMPILER_OUTPUT[COMPILER] + file->output + sep;
	command += COMPILER_NOLINK[COMPILER] + sep;
	command += *filename + sep;
	command += file->includes + sep;
	
	//Don't apply compilation flags to asm files under windows.
	if (!asm_win) {
		if(COMPILER == COMPILER_GCC && !FLAGS.arch.empty()) {
			command += "-m" + FLAGS.arch + sep;
		}
		command += file->flags->compiler_flags + sep;
		for(auto kv : file->flags->ext_flags) {
			if(endsWith(*filename, "." + kv.first)) {
				command += kv.second + sep;
				break;
			}
		}
	}
	
	printf("%s\n", command.c_str());
	if(!FLAGS.safemode) return system(command.c_str());
	return 0;
}

int produce_output(output& file, int operation, build_flags* f) {
	string command_toolchain(OPERATIONS[operation]->at(COMPILER)); 
#ifndef NO_RESPONSE_FILE
	command_toolchain += " @link";
#endif
	string command;
	const string sep = " ";
	int error = 0;
	if(operation == OPERATION_LINK || COMPILER == COMPILER_MSVC) {
		command += file.compiled_files + sep;
		command += file.extra_files + sep;
		command += f->linker_flags + sep;
		command += OUTPUT_LINK_OUTPUT[COMPILER];
		command += file.output_name;
	} else {
		//LIB on linux == ar. 
		command += file.output_name;
		command += sep + file.compiled_files + sep;
		command += file.extra_files + sep;
	}
#ifndef NO_RESPONSE_FILE
	FILE* tmp = fopen("link", "w");
	fwrite(command.c_str(), sizeof(char), command.size(), tmp);
	fclose(tmp);
#endif
	
	printf("%s\n", command.c_str());
	if (!FLAGS.safemode) {
#ifndef NO_RESPONSE_FILE
		error = system(command_toolchain.c_str());
		unlink("link");
#else
		system((command_toolchain + command).c_str());
#endif
	}
	return 0;

}

string get_output_file(xml_node<>* output) {
	string output_str = FLAGS.config.empty() ? DEFAULT_OUTPUT_DIR : FLAGS.config;
	if (output != NULL) {
		string output_name(output->value(), output->value_size());
		xml_attribute<>* output_type = output->first_attribute("type");

		if (output_type != NULL) {
			string outval = string(output_type->value(), output_type->value_size());
			if (outval == STR_APP) {
				output_str += PATH_SEP + output_name + OUTPUT_LINK_EXT[COMPILER];
			}
			else if (outval == STR_STATIC) {
				output_str += PATH_SEP + output_name + OUTPUT_LIB_EXT[COMPILER];
			}
		}
	}
	return output_str;
}

string make_output_filename(string& immdir, string& filename) {
	size_t spos = filename.find_last_of('/');
	if (spos == string::npos) spos = 0;
	else spos++;
	return immdir + PATH_SEP + filename.substr(spos, filename.find_last_of('.')) + DEFAULT_OUTPUT_SUFFIX;
}

string build_compiler_string(xml_node<>* node, char prefix='\0', int escape=0, int useflag=1) {
	size_t pos = 0;
	size_t size = node->value_size();
	char* v = node->value();
	string built;
	for (size_t i = 0; i < size; i++) {
		char c = *(v + i);
		if (i == size - 1) {
			c = ';';
			i = size;
		}
		if (c == ';') {
			if (i - pos > 0) {
				if (useflag) built += COMPILER_FLAG[COMPILER];
				if (prefix != '\0') built += prefix;
				built += (escape ? "\"" : "") + string(node->value() + pos, i - pos) + (escape ? "\" " : " ");
				pos = i + 1;
			}
			else {
				pos++;
			}
		}
	}
	return built;
}

int check_compiler_str(const char* str) {
	for (unsigned int i = 0; i < COMPILERS.size(); i++) {
		if (COMPILERS.at(i) == str) {
			return i;
		}
	}
	return -1;
}

void step_build_output(xml_node<>* project, string& compiled_files, build_flags* f) {
	//Produce output (link/lib...)
	xml_node<>* output_node = project->first_node("output");

	if (output_node != NULL && compiled_files.length() > 0) {
		string output_name = get_output_file(output_node);
		xml_attribute<>* output_type_attr = output_node->first_attribute("type");

		if (output_type_attr != NULL) {
			char* output_type = output_type_attr->value();
			int operation = -1;
			output f_output;

			for (xml_node<> *child = project->first_node("link");
				child; child = child->next_sibling("link")) {
				xml_attribute<>* compiler = child->first_attribute("compiler");
				if (compiler != NULL) {
					int target_compiler = check_compiler_str(compiler->value());
					if (target_compiler == COMPILER)
						f_output.extra_files = parse_string(build_compiler_string(child, '\0', 0, 0));
				}
			}
			if (output_type == STR_APP) 
				operation = OPERATION_LINK;
			else if (output_type == STR_STATIC) 
				operation = OPERATION_LIB;

			
			f_output.compiled_files = compiled_files;
			f_output.output_name = output_name;
			produce_output(f_output, operation, f);

		}
	}
}

int step_compile_files(xml_node<>* project, string& compiled_files, sourcefile& base_file) {
	//Process source files.
	int error = 0;
	for (xml_node<> *child = project->first_node("source");
		child; child = child->next_sibling("source")) {
		xml_attribute<>* out = child->first_attribute("out");
		xml_attribute<>* f = child->first_attribute("f");

		string filename = (f != NULL ? string(f->value(), f->value_size()) :
			string(child->value(), child->value_size()));

		if (filename.size() == 0) {
			continue;
		}
		
		if(!check_os(child))
			continue;
		
		xml_attribute<>* arch = child->first_attribute("arch");
		if(arch != NULL) {
			string arch_str(arch->value(), arch->value_size());
			if(arch_str != (FLAGS.arch.empty() ? DEFAULT_ARCH : FLAGS.arch)) 
				continue;
		}

		string output;
		if (out != NULL) output = filename;
		else {
			string immdir = (FLAGS.config.empty() ? DEFAULT_OUTPUT_DIR : FLAGS.config);
			mkdir(immdir.c_str());
			output = make_output_filename(immdir, filename);
		}

		base_file.filename = filename;
		base_file.output = output;
		
		//Check if output file exists, and is last modified before the input.
		struct stat s_input, s_output;
		if(stat(filename.c_str(), &s_input) < 0) {
			printf("Error: %s could not be opened.\n", filename.c_str());
			return 1; //Could not stat input file.
		}

        if(stat(output.c_str(), &s_output) < 0 || s_output.st_mtime < s_input.st_mtime) {
            if ((error = compile_file(&base_file)) != 0) {
                return error; //Abort compilation.
            }
        }
        compiled_files += output + " ";

	}
	return error;
}

void add_flags(build_flags& flags, xml_node<>* child, char prefix, int quote) {
	string* target_flags = &flags.compiler_flags;
	xml_attribute<>* step = child->first_attribute("step");
	
	if (step != NULL && strcmp("link", step->value()) == 0) 
		target_flags = &flags.linker_flags;
	
	xml_attribute<>* ext = child->first_attribute("ext");
	if(ext != NULL) {
		string ext_str = string(ext->value(), ext->value_size());
		string flag_str = parse_string(build_compiler_string(child, prefix, quote));
		vector<string> exts;
		tokenize(ext_str, exts, ";", true);
		for(auto ext: exts) {
			flags.ext_flags.insert(make_pair(ext, flag_str)); 
		}
		
	} else 
		*target_flags += parse_string(build_compiler_string(child, prefix, quote));
}

void step_add_flags(xml_node<>* project, build_flags& flags) {
	//Add flags.
	for (xml_node<> *child = project->first_node("flags");
		child; child = child->next_sibling("flags")) {
		xml_attribute<>* compiler = child->first_attribute("compiler");
		xml_attribute<>* config = child->first_attribute("config");
		
		if (config != NULL) {
			if (FLAGS.config != string(config->value(), config->value_size())) {
				continue; //Not matching config, ignore flags.
			}
		}
		
		if(!check_os(child)) continue;

		xml_attribute<>* type = child->first_attribute("type");
		char prefix = '\0';
		int quote = 0;
		if (type != NULL) {
			if (strncmp(type->value(), "pp", 2) == 0) {
				//Preprocessor flags.
				prefix = 'D';
				quote = 1;
			}
		}
	
		if (compiler != NULL) {
			//Compiler specific flags.
			int target_compiler = check_compiler_str(compiler->value());
			if (target_compiler != COMPILER) {
				continue;
			}
		}
		
		//All checks passed. Add the flag.
		add_flags(flags, child, prefix, quote);
	}
}

//Forward define build_project to allow dependent projects to call it.
int build_project(xml_node<>* project);

int step_build_dependencies(xml_node<>* project, string& dependency_outputs) {
	char* path = NULL;
	int error = 0;
	for (xml_node<> *child = project->first_node("depends");
		child; child = child->next_sibling("depends")) {
		
		if(!check_os(child))
			continue;

		path = getcwd(NULL, 0);
		xml_attribute<>* link = child->first_attribute("link");
		bool do_link = false;
		if (link != NULL && strcmp(link->value(), "true") == 0) {
			do_link = true;
		}

		string project = string(child->value(), child->value_size());

		size_t pos = project.find_last_of('/');
		if (pos == string::npos) pos = 0;
		bool is_file = endsWith(project, ".xml");
		//Is a file, has a path. 
		string path_str = is_file && pos ? 
			project.substr(0, pos + 1) : project;
		//Just the project file name.
		string project_file = is_file && pos ?
			project.substr(pos, project.size()) : is_file ? project : "project.xml";

		//TODO: Prevent stack overflow (project including same project).
		chdir(path_str.c_str());
		char* root_project = NULL;
		if ((root_project = load_project(project_file.c_str()))) {
			//Root project loaded. Parse xml.
			xml_document<> doc;
			try{
				doc.parse<0>(root_project);
			}
			catch (rapidxml::parse_error ex) {
				fprintf(stderr, "Error: Unable to parse dependent project.");
				return 1;
			}
			xml_node<>* p = doc.first_node("project");
			if (p != NULL) {
				xml_node<>* output = p->first_node("output");
				string output_file = get_output_file(output);
				//If dependency should be linked, add it to the dependency outputs.
				if (do_link) dependency_outputs += path_str + PATH_SEP + output_file + " ";
				FILE* ofile;
				if ((ofile = fopen(output_file.c_str(), "r")) != NULL) {
					fclose(ofile);
				} else 
					if ((error = build_project(p)) != 0)
						return error;
			}
			delete[] root_project;
		}
		else {
			return 1;
		}
		chdir(path);
		free(path);
	}
	return error;
}



int build_project(xml_node<>* project) {
	string dependency_outputs;
	int error = 0;

	if ((error = step_build_dependencies(project, dependency_outputs)) != 0) return error;

	//Run any prebuild commands before commencing the build.
	for (xml_node<>* child = project->first_node("prebuild");
		child; child = child->next_sibling("prebuild")) {
		if(!check_os(child))
			continue;

		string parsed_command = parse_string(string(child->value(), child->value_size()));

		if ((error = run_command(parsed_command.c_str()) != 0))
			return error;
	}

	sourcefile base_file;
	build_flags flags;
	string compiled_files = "";

	base_file.flags = &flags;
	//Includes: Split by ; then produce separate include flags.
	for (xml_node<> *child = project->first_node("include");
		child; child = child->next_sibling("include")) {

		if(!check_os(child))
			continue;

		base_file.includes += build_compiler_string(child, 'I', 1);
	
	}

	step_add_flags(project, flags);

	if ((error = step_compile_files(project, compiled_files, base_file) != 0)) return error;

	compiled_files += dependency_outputs;
	step_build_output(project, compiled_files, &flags);
	
	return error;
}

int main(int argc, char** argv) {
	const char* path = NULL;
	if (argc == 1) {
bad_format:
		fprintf(stderr, "Usage: unbuild compiler [path [options]]");
		return 1;
	}

	if (argc > 3) {
		for (int i = 3; i < argc; i++) {
			char* arg = argv[i];
			if (arg[0] == '-') {
				//Command switches.
				switch (arg[1]) {
				case 's':
					//safe mode. Don't execute commands.
					FLAGS.safemode = 1;
					break;
				case 'c':
					FLAGS.config = string(arg + 2);
					break;
				case 'm':
					FLAGS.arch = string(arg + 2);
					break;
				default:
					goto bad_format;
				}
			}
			else goto bad_format;
		}
	}



	if (argc == 2) {
		//Default path, cwd.
		path = getcwd(NULL, 0);
	}
	else
		path = argv[2];

	COMPILER = check_compiler_str(argv[1]);

	if (COMPILER == -1) goto bad_format;

	string path_str = string(path);
	if(argc == 2) free((void*)path);

	path_str += PATH_SEP "project.xml";
	
	char* root_project = NULL;
	if ((root_project = load_project(path_str.c_str()))) {
		//Root project loaded. Parse xml.
		xml_document<> doc;
		try{
			doc.parse<0>(root_project);
		}
		catch (rapidxml::parse_error ex) {
			fprintf(stderr, "Error: Unable to parse project.");
			return 3;
		}
		xml_node<>* p = doc.first_node("project");
		if (p != NULL) {
			build_project(p);
		}
		delete[] root_project;
	}
	else {
		return 2;
	}

	return 0;
}
