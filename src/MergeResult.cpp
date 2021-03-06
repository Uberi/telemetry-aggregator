#include "cache/ResultSet.h"
#include "cache/ChannelVersion.h"
#include "cache/MeasureFile.h"
#include "cache/Aggregate.h"
#include "cache/InternedString.h"

#include "rapidjson/document.h"

#include <unistd.h>

#include <string>
#include <fstream>
#include <iostream>
#include <vector>

/** Print usage */
void usage() {
  printf("Usage: mergeresults -i [FILE] -o [FILE]\n");
}

using namespace std;
using namespace rapidjson;


/** Main file */
int main(int argc, char *argv[]) {
  vector<char*> inputs;
  FILE* output = stdout;
  bool sorted = false;
  char* folder = nullptr;

  // Parse arguments
  int c;
  while ((c = getopt(argc, argv, "hsi:o:f:")) != -1) {
    switch (c) {
      case 'f':
        folder = optarg;
        break;
      case 's':
        sorted = true;
        break;
      case 'i':
        inputs.push_back(optarg);
        break;
      case 'o':
        output = fopen(optarg, "w");
        break;
      case 'h':
        usage();
        exit(0);
        break;
      case '?':
        usage();
        return 1;
        break;
      default:
        abort();
    }
  }

  // We can't have multiple sorted inputs
  if (sorted && inputs.size() > 1) {
    fprintf(stderr, "We cannot exploited more than one source of sorted input.\n");
    fprintf(stderr, "You should merge with GNU sort first.\n");
    exit(1);
  }

  // We can't both write to file and folders (we could be this is stupid!)
  if (folder && output != stdout) {
    fprintf(stderr, "Output (-o) and folder (-f) options cannot be combined\n");
  }

  // We haven't implemented support for sorted input and output to folders
  if (sorted && folder) {
    fprintf(stderr, "Sort (-s) and folder (-f) options cannot be combined\n");
    exit(1);
  }

  // Merge input in memory if input isn't sorted
  if (!sorted) {
    // Input one by one
    ResultSet set;
    // If no input are given read from stdin
    if (inputs.empty()) {
      cin.sync_with_stdio(false);
      set.mergeStream(cin);
    } else {
      for (auto file : inputs) {
        ifstream stream(file);
        set.mergeStream(stream);
      }
    }

    // Write output
    if (folder) {
      set.updateFileInFolder(folder);
    } else {
      // Write out to file
      set.output(output);
    }

  } else {
    // If a single sorted file is given we read from it and output whenever the
    // filePath changes. This will mergeresult of sorted input, hence,
    // perfect when piping in from GNU sort, which can efficiently merge sorted
    // files
    istream* input;

    // If no input file was given at all, we read from stdin
    if (inputs.empty()) {
      cin.sync_with_stdio(false);
      input = &cin;
    } else {
      input = new ifstream(inputs[0]);
    }

    // filePath and measureFile currently aggregated
    string                  filePath;
    MeasureFile*            measureFile = nullptr;
    InternedStringContext*  filterStringCtx = nullptr;

    string outline;
    outline.reserve(10 * 1024 * 1024);
    string line;
    line.reserve(10 * 1024 * 1024);
    int nb_line = 0;
    while (getline(*input, line)) {
      nb_line++;

      // Find delimiter
      size_t del = line.find('\t');
      if (del == string::npos) {
        fprintf(stderr, "No tab on line %i\n", nb_line);
        continue;
      }

      // Find current file path
      string currentFilePath = line.substr(0, del);

      // If we're reached a new filePath, output the old one
      if (filePath != currentFilePath) {
        if (measureFile) {
          fputs(filePath.data(), output);
          fputc('\t', output);
          measureFile->output(output);
          fputc('\n', output);
          delete measureFile;
          measureFile = nullptr;
          delete filterStringCtx;
          filterStringCtx = nullptr;
        }
        filePath = currentFilePath;
      }

      // Parse JSON document
      Document d;
      d.Parse<0>(line.data() + del + 1);

      // Check that we have an object
      if (!d.IsObject()) {
        fprintf(stderr, "JSON root is not an object on line %i\n", nb_line);
        continue;
      }

      // Allocate MeasureFile if not already aggregated
      if (!measureFile) {
        filterStringCtx = new InternedStringContext();
        measureFile = new MeasureFile(*filterStringCtx);
      }

      // Merge in JSON
      measureFile->mergeJSON(d);
    }

    // Output last MeasureFile, if there was ever one
    if (measureFile) {
      fputs(filePath.data(), output);
      fputc('\t', output);
      measureFile->output(output);
      fputc('\n', output);
      delete measureFile;
      measureFile = nullptr;
      delete filterStringCtx;
      filterStringCtx = nullptr;
    }
  }

  // Close output file
  fclose(output);

  return 0;
}
