#include "CollectorArgs.h"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

#include "Logging.h"
#include "optionparser.h"

#define MAX_CHISEL_LENGTH 8192

namespace collector {

enum optionIndex {
  UNKNOWN,
  HELP,
  COLLECTOR_CONFIG,
  COLLECTION_METHOD,
  GRPC_SERVER,
  CHISEL
};

static option::ArgStatus
checkCollectorConfig(const option::Option& option, bool msg) {
  return CollectorArgs::getInstance()->checkCollectorConfig(option, msg);
}

static option::ArgStatus
checkCollectionMethod(const option::Option& option, bool msg) {
  return CollectorArgs::getInstance()->checkCollectionMethod(option, msg);
}

static option::ArgStatus
checkChisel(const option::Option& option, bool msg) {
  return CollectorArgs::getInstance()->checkChisel(option, msg);
}

static option::ArgStatus
checkGRPCServer(const option::Option& option, bool msg) {
  return CollectorArgs::getInstance()->checkGRPCServer(option, msg);
}

static const option::Descriptor usage[] =
    {
        {UNKNOWN, 0, "", "", option::Arg::None,
         "USAGE: collector [options]\n\n"
         "Options:"},
        {HELP, 0, "", "help", option::Arg::None, "  --help                \tPrint usage and exit."},
        {COLLECTOR_CONFIG, 0, "", "collector-config", checkCollectorConfig, "  --collector-config    \tREQUIRED: Collector config as a JSON string. Please refer to documentation on the valid JSON format."},
        {COLLECTION_METHOD, 0, "", "collection-method", checkCollectionMethod, "  --collection-method   \tCollection method (kernel_module, ebpf or core_bpf)."},
        {CHISEL, 0, "", "chisel", checkChisel, "  --chisel              \tChisel is a base64 encoded string."},
        {GRPC_SERVER, 0, "", "grpc-server", checkGRPCServer, "  --grpc-server         \tGRPC server endpoint string in the form HOST1:PORT1."},
        {UNKNOWN, 0, "", "", option::Arg::None,
         "\nExamples:\n"
         "  collector --grpc-server=\"172.16.0.5:443\"\n"},
        {0, 0, 0, 0, 0, 0},
};

CollectorArgs::CollectorArgs() {
}

CollectorArgs::~CollectorArgs() {
}

CollectorArgs* CollectorArgs::instance;

CollectorArgs*
CollectorArgs::getInstance() {
  if (instance == NULL) {
    instance = new CollectorArgs();
  }
  return instance;
}

void CollectorArgs::clear() {
  delete instance;
  instance = new CollectorArgs();
}

bool CollectorArgs::parse(int argc, char** argv, int& exitCode) {
  using std::stringstream;

  // Skip program name argv[0] if present
  argc -= (argc > 0);
  argv += (argc > 0);

  option::Stats stats(usage, argc, argv);
  option::Option options[stats.options_max], buffer[stats.buffer_max];
  option::Parser parse(usage, argc, argv, options, buffer);

  if (parse.error()) {
    exitCode = 1;
    return false;
  }

  if (options[HELP] || argc == 0) {
    stringstream out;
    option::printUsage(out, usage);
    message = out.str();
    exitCode = 0;
    return false;
  }

  for (int i = 0; i < parse.optionsCount(); ++i) {
    option::Option& opt = buffer[i];
    if (opt.index() == UNKNOWN) {
      stringstream out;

      out << "Unknown option: " << options[UNKNOWN].name;
      message = out.str();
      exitCode = 1;
      return false;
    }
  }

  exitCode = 0;
  return true;
}

option::ArgStatus
CollectorArgs::checkCollectionMethod(const option::Option& option, bool msg) {
  using namespace option;
  using std::string;

  if (option.arg == NULL) {
    if (msg) {
      this->message = "Missing collection method, using default.";
    }
    return ARG_OK;
  }

  // Canonicalize collection method to lowercase, replace '-' with '_'
  std::string s = option.arg;
  std::transform(s.begin(), s.end(), s.begin(), ::tolower);
  std::replace(s.begin(), s.end(), '-', '_');
  collectionMethod = s;

  CLOG(DEBUG) << "CollectionMethod: " << collectionMethod;

  return ARG_OK;
}

option::ArgStatus
CollectorArgs::checkChisel(const option::Option& option, bool msg) {
  using namespace option;
  using std::string;

  if (option.arg == NULL) {
    if (msg) {
      this->message = "Missing chisel. No chisel will be used.";
    }
    return ARG_OK;
  }
  chisel = option.arg;
  int chiselEncodedLength = chisel.length();
  if (chiselEncodedLength > MAX_CHISEL_LENGTH) {
    if (msg) {
      this->message = "Chisel encoded length cannot exceed " + std::to_string(MAX_CHISEL_LENGTH) + ".";
    }
    return ARG_ILLEGAL;
  }

  CLOG(DEBUG) << "Chisel: " << chisel;
  return ARG_OK;
}

option::ArgStatus
CollectorArgs::checkCollectorConfig(const option::Option& option, bool msg) {
  using namespace option;
  using std::string;

  if (option.arg == NULL) {
    if (msg) {
      this->message = "Missing collector config";
    }
    return ARG_ILLEGAL;
  }

  string arg(option.arg);
  CLOG(DEBUG) << "Incoming: " << arg;

  Json::Value root;
  Json::Reader reader;
  bool parsingSuccessful = reader.parse(arg.c_str(), root);
  if (!parsingSuccessful) {
    if (msg) {
      this->message = "A valid JSON configuration is required to start the collector: ";
      this->message += reader.getFormattedErrorMessages();
    }
    return ARG_ILLEGAL;
  }

  // for now check that the keys exist without checking their types
  if (!root.isMember("syscalls")) {
    if (msg) {
      this->message = "No syscalls key. Will extract on the complete syscall set.";
    }
  }

  collectorConfig = root;
  CLOG(DEBUG) << "Collector config: " << collectorConfig.toStyledString();
  return ARG_OK;
}

option::ArgStatus
CollectorArgs::checkGRPCServer(const option::Option& option, bool msg) {
  using namespace option;
  using std::string;

  if (option.arg == NULL || ::strlen(option.arg) == 0) {
    if (msg) {
      this->message = "Missing grpc list. Cannot configure GRPC client. Reverting to stdout.";
    }
    return ARG_OK;
  }

  if (::strlen(option.arg) > 255) {
    if (msg) {
      this->message = "GRPC Server addr too long (> 255)";
    }
    return ARG_ILLEGAL;
  }

  string arg(option.arg);
  string::size_type j = arg.find(':');
  if (j == string::npos) {
    if (msg) {
      this->message = "Malformed grpc server addr";
    }
    return ARG_ILLEGAL;
  }

  string host = arg.substr(0, j);
  if (host.empty()) {
    if (msg) {
      this->message = "Missing grpc host";
    }
    return ARG_ILLEGAL;
  }

  string port = arg.substr(j + 1, arg.length());
  if (port.empty()) {
    if (msg) {
      this->message = "Missing grpc port";
    }
    return ARG_ILLEGAL;
  }

  grpcServer = arg;
  return ARG_OK;
}

const Json::Value&
CollectorArgs::CollectorConfig() const {
  return collectorConfig;
}

const std::string&
CollectorArgs::GetCollectionMethod() const {
  return collectionMethod;
}

const std::string&
CollectorArgs::Chisel() const {
  return chisel;
}

const std::string&
CollectorArgs::GRPCServer() const {
  return grpcServer;
}

const std::string&
CollectorArgs::Message() const {
  return message;
}

} /* namespace collector */
