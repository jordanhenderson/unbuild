#include <cstdio>
#ifdef _MSC_VER
#include <direct.h>
#else
#include <unistd.h>
#include <sys/stat.h>
#endif
#include <stdlib.h>
#include <cstring>
#include <string>
#include <vector>

#include "rapidxml.hpp"

using namespace std;
using namespace rapidxml;

#ifdef _MSC_VER
#define getcwd _getcwd
#define mkdir _mkdir
#pragma warning(disable: 4996)
#define PLATFORM 0
#else
#define mkdir(x) mkdir(x, 755)
#define PLATFORM 1
#endif
#define PATH_SEP "/"
#define DEFAULT_OUTPUT_SUFFIX ".o"
#define DEFAULT_OUTPUT_DIR "output"
#define OPERATION_LINK 0
#define OPERATION_LIB 1

static int COMPILER = -1;
#define COMPILER_MSVC 0
#define COMPILER_GCC 1
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
vector<string>* EXTS[] = { &OUTPUT_LINK_EXT, &OUTPUT_LIB_EXT };
string STR_APP = "app";
string STR_STATIC = "static";


struct output {
	string compiled_files;
	string extra_files;
	char* output_name;
};

struct build_flags {
	string compiler_flags;
	string linker_flags;
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
};



static flags FLAGS;

//Forward define build_project to allow dependent projects to call it.
int build_project(xml_node<>* project);

char* load_project(const char* path) {
	FILE* fp = NULL;
	if (!(fp = fopen(path, "r"))) {
		fprintf(stderr, "Error: Could not open project.xml.");
		return NULL;
	}

	fseek(fp, 0L, SEEK_END);
	int sz = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	char* project_data = new char[sz + 1];
	size_t read = fread(project_data, sizeof(char), sz, fp);
	project_data[read] = '\0';
	fclose(fp);
	return project_data;
}

int compile_file(sourcefile* file) {
	//Run the configured compiler.
	string command(COMPILER_BINS[COMPILER]);
	string sep = " ";
	command += sep;
	command += file->filename + sep;
	command += file->includes + sep;
	command += COMPILER_NOLINK[COMPILER] + sep;
	command += COMPILER_OUTPUT[COMPILER] + file->output + sep;
	if (file->flags != NULL)
		command += file->flags->compiler_flags + sep;
	/*command += " 2> ";
	command += OUTPUT_NULL[COMPILER];*/
	printf("%s\n", command.c_str());
	if(!FLAGS.safemode) return system(command.c_str());
	return 0;
}

int produce_output(output& file, int operation, build_flags* f) {
	string command(OPERATIONS[operation]->at(COMPILER));
	command += " ";
	if(operation == OPERATION_LINK || COMPILER == COMPILER_MSVC) {
		command += file.compiled_files + " ";
		command += file.extra_files + " ";
		command += f->linker_flags + " ";
		command += OUTPUT_LINK_OUTPUT[COMPILER];
		command += file.output_name;
		command += EXTS[operation]->at(COMPILER);
	} else {
		command += file.output_name;
		command += EXTS[operation]->at(COMPILER);
		command += " " + file.compiled_files + " ";
		command += file.extra_files + " ";
		
	}
	
	
	printf("%s\n", command.c_str());
	if (!FLAGS.safemode) return system(command.c_str());
	return 0;

}

string get_output_file(xml_node<>* project) {
	xml_node<>* output = project->first_node("output");
	string output_str;
	if (output != NULL) {
		string output_name(output->value(), output->value_size());
		xml_attribute<>* output_type = output->first_attribute("type");

		if (output_type != NULL) {
			char* outval = output_type->value();
			if (strcmp(outval, "app") == 0) {
				output_str = output_name + OUTPUT_LINK_EXT[COMPILER];
			}
			else if (strcmp(outval, "static") == 0) {
				output_str = output_name + OUTPUT_LIB_EXT[COMPILER];
			}
		}
	}
	return output_str;
}

string make_output_filename(string& immdir, string& filename) {
	int spos = filename.find_last_of('/');
	if (spos == string::npos) spos = 0;
	else spos++;
	return immdir + PATH_SEP + filename.substr(spos, filename.find_last_of('.')) + DEFAULT_OUTPUT_SUFFIX;
}

string build_compiler_string(xml_node<>* node, char prefix='\0', int escape=0, int useflag=1) {
	int pos = 0;
	int size = node->value_size();
	char* v = node->value();
	string built;
	for (int i = 0; i < size; i++) {
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
		char* output_name = output_node->value();
		xml_attribute<>* output_type_attr = output_node->first_attribute("type");

		if (output_type_attr != NULL) {
			char* output_type = output_type_attr->value();
			int operation = -1;
			output f_output;

			if (output_type == STR_APP) {
				for (xml_node<> *child = project->first_node("link");
					child; child = child->next_sibling("link")) {
					xml_attribute<>* compiler = child->first_attribute("compiler");
					if (compiler != NULL) {
						int target_compiler = check_compiler_str(compiler->value());
						if (target_compiler == COMPILER) 
							f_output.extra_files = build_compiler_string(child, '\0', 0, 0);
					}
				}
				operation = OPERATION_LINK;
			}
			else
			if (output_type == STR_STATIC) operation = OPERATION_LIB;

			
			f_output.compiled_files = compiled_files;
			f_output.output_name = output_name;
			produce_output(f_output, operation, f);

		}
	}
}

int step_compile_files(xml_node<>* project, string& compiled_files, sourcefile& src_file) {
	//Process source files.
	int error = 0;
	for (xml_node<> *child = project->first_node("source");
		child; child = child->next_sibling("source")) {
		xml_attribute<>* out = child->first_attribute("out");
		xml_attribute<>* f = child->first_attribute("f");

		string filename = (f != NULL ? string(f->value(), f->value_size()) :
			string(child->value(), child->value_size()));

		xml_attribute<>* os = child->first_attribute("os");
		if (os != NULL) {
			//Platform specific source.
#ifdef _WIN32
			if (strcmp(os->value(), "win32") != 0) {
				continue;
			}
#endif
#ifdef __GNUC__
			if(strcmp(os->value(), "linux") != 0) {
				continue;
			}
#endif
		}
		string output;
		if (out != NULL) output = filename;
		else {
			string immdir = (FLAGS.config.empty() ? DEFAULT_OUTPUT_DIR : FLAGS.config);
			mkdir(immdir.c_str());
			output = make_output_filename(immdir, filename);
		}

		src_file.filename = filename;
		src_file.output = output;
		if ((error = compile_file(&src_file)) != 0) {
			return error; //Abort compilation.
		}

		compiled_files += output + " ";
	}
	return error;
}

void step_add_flags(xml_node<>* project, build_flags& flags) {
	//Add flags.
	for (xml_node<> *child = project->first_node("flags");
		child; child = child->next_sibling("flags")) {
		xml_attribute<>* compiler = child->first_attribute("compiler");
		xml_attribute<>* config = child->first_attribute("config");
		xml_attribute<>* step = child->first_attribute("step");
		string* target_flags = &flags.compiler_flags;
		if (config != NULL) {
			if (FLAGS.config != string(config->value(), config->value_size())) {
				break; //Not matching config, ignore flags.
			}
		}

		if (step != NULL && strcmp("link", step->value()) == 0) {
			target_flags = &flags.linker_flags;
		}

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
		//Config matches.
		if (compiler != NULL) {
			//Compiler specific flags.
			int target_compiler = check_compiler_str(compiler->value());
			if (target_compiler == COMPILER) {
				*target_flags += build_compiler_string(child, prefix, quote);
			}
		}
		else {
			//Global flags
			*target_flags += build_compiler_string(child, prefix, quote);
		}
	}

}

int step_build_dependencies(xml_node<>* project, string& dependency_outputs) {
	char* path = getcwd(NULL, 0);
	int error = 0;
	for (xml_node<> *child = project->first_node("depends");
		child; child = child->next_sibling("depends")) {
		string path_str = ".";
		xml_attribute<>* link = child->first_attribute("link");
		bool do_link = false;
		if (link != NULL && strcmp(link->value(), "true") == 0) {
			do_link = true;
		}
		path_str += PATH_SEP + string(child->value(), child->value_size());
		chdir(path_str.c_str());

		char* root_project = NULL;
		if ((root_project = load_project("project.xml"))) {
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
				string output_file = get_output_file(p);
				if (do_link) dependency_outputs += path_str + PATH_SEP + output_file + " ";
				FILE* ofile;
				if ((ofile = fopen(output_file.c_str(), "r")) != NULL) {
					fclose(ofile);
				} else 
					if ((error = build_project(p)) != 0)
						return error;
			}
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
	//TODO: Build dependent projects recursively.
	
	if ((error = step_build_dependencies(project, dependency_outputs)) != 0) return error;


	sourcefile src_file;
	build_flags flags;
	string compiled_files = "";

	src_file.flags = &flags;
	//GCC specific transforms. 
	//Includes: Split by ; then produce separate include flags.
	for (xml_node<> *child = project->first_node("include");
		child; child = child->next_sibling("include")) {
		xml_attribute<>* os = child->first_attribute("os");
		if (os != NULL) {
			//Platform specific source.
#ifdef _WIN32
			if (strcmp(os->value(), "win32") != 0) {
				continue;
			}
#endif
#ifdef __GNUC__
			if(strcmp(os->value(), "linux") != 0) {
				continue;
			}
#endif
		}
		src_file.includes += build_compiler_string(child, 'I', 1);
	
	}

	step_add_flags(project, flags);

	if ((error = step_compile_files(project, compiled_files, src_file) != 0)) return error;

	compiled_files += dependency_outputs;
	step_build_output(project, compiled_files, &flags);

	return error;

}

int main(int argc, char** argv) {
	const char* path = NULL;
	FILE* project = NULL;
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
	}
	else {
		return 2;
	}



	return 0;
}
