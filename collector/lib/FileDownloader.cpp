
#include "FileDownloader.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <string_view>
#include <unistd.h>
#include <utils.h>

#include "Logging.h"
#include "Utility.h"

namespace collector {

namespace {

size_t HeaderCallback(char* buffer, size_t size, size_t nitems, void* dd) {
  auto download_data = static_cast<DownloadData*>(dd);
  size_t buffer_size = size * nitems;
  std::string_view data(buffer, buffer_size);

  if (data.substr(0, 5) == "HTTP/") {
    size_t error_code_offset = data.find(' ');

    if (error_code_offset == std::string_view::npos) {
      download_data->http_status = 500;
      download_data->error_msg = Str("Failed extracting HTTP status code (", data.substr(0, 1024), ")");
      return 0;  // Force the download to fail explicitly
    }

    download_data->http_status = std::stoul(std::string(data.substr(error_code_offset + 1, data.find(' ', error_code_offset + 1))));
    // CLOG(DEBUG) << "Set HTTP status code to '" << download_data->http_status << "'";
  }

  return buffer_size;
}

size_t WriteFile(void* content, size_t size, size_t nitems, void* dd) {
  auto download_data = static_cast<DownloadData*>(dd);
  size_t content_size = size * nitems;
  const char* content_bytes = static_cast<const char*>(content);

  if (download_data->http_status >= 400) {
    download_data->error_msg = Str("HTTP Body Response: ", std::string_view(content_bytes, std::min(content_size, 1024UL)));
    return 0;  // Force the download to fail explicitly
  }

  download_data->os->write(content_bytes, content_size);
  return content_size;
}

int DebugCallback(CURL*, curl_infotype type, char* data, size_t size, void*) {
  std::string msg(data, size);
  msg = rtrim(msg);

  if (type == CURLINFO_TEXT) {
    CLOG(DEBUG) << "== Info: " << msg;
    return CURLE_OK;
  }

  if (logging::GetLogLevel() > logging::LogLevel::TRACE) {
    // Skip other types of messages if we are not tracing
    return CURLE_OK;
  }

  std::transform(msg.begin(), msg.end(), msg.begin(), [](char c) -> char {
    if (c < 0x20) return '.';
    return (char)c;
  });

  const char* hdr = nullptr;

  if (type == CURLINFO_HEADER_OUT) {
    hdr = "-> Send header - ";
  } else if (type == CURLINFO_DATA_OUT) {
    hdr = "-> Send data - ";
  } else if (type == CURLINFO_SSL_DATA_OUT) {
    hdr = "-> Send SSL data - ";
  } else if (type == CURLINFO_HEADER_IN) {
    hdr = "<- Recv header - ";
  } else if (type == CURLINFO_DATA_IN) {
    hdr = "<- Recv data - ";
  } else if (type == CURLINFO_SSL_DATA_IN) {
    hdr = "<- Recv SSL data - ";
  }

  if (hdr) {
    CLOG(DEBUG) << hdr << msg;
  }

  return CURLE_OK;
}

}  // namespace

FileDownloader::FileDownloader() : connect_to_(nullptr) {
  curl_ = curl_easy_init();

  if (curl_) {
    SetDefaultOptions();
  }

  error_.fill('\0');
  retry_ = {.times = 0, .delay = 0, .max_time = std::chrono::seconds(0)};
}

FileDownloader::~FileDownloader() {
  if (curl_) {
    curl_easy_cleanup(curl_);
  }

  curl_global_cleanup();

  curl_slist_free_all(connect_to_);
}

bool FileDownloader::SetURL(const char* const url) {
  url_path_ = std::string(url);
  url_path_ = url_path_.substr(url_path_.find_last_of("/") + 1);

  auto result = curl_easy_setopt(curl_, CURLOPT_URL, url);

  if (result != CURLE_OK) {
    CLOG(WARNING) << "Unable to set URL '" << url << "' - " << curl_easy_strerror(result);
    return false;
  }

  return true;
}

bool FileDownloader::SetURL(const std::string& url) {
  return SetURL(url.c_str());
}

void FileDownloader::IPResolve(FileDownloader::resolve_t version) {
  curl_easy_setopt(curl_, CURLOPT_IPRESOLVE, version);
}

void FileDownloader::SetRetries(unsigned int times, unsigned int delay, unsigned int max_time) {
  retry_.times = times;
  retry_.delay = delay;
  retry_.max_time = std::chrono::seconds(max_time);
}

bool FileDownloader::SetConnectionTimeout(int timeout) {
  auto result = curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT, timeout);

  if (result != CURLE_OK) {
    CLOG(WARNING) << "Unable to set connection timeout - " << curl_easy_strerror(result);
    return false;
  }

  return true;
}

bool FileDownloader::FollowRedirects(bool follow) {
  auto result = curl_easy_setopt(curl_, CURLOPT_FOLLOWLOCATION, follow ? 1L : 0L);

  if (result != CURLE_OK) {
    CLOG(WARNING) << "Unable to set value for redirections - " << curl_easy_strerror(result);
    return false;
  }
  return true;
}

void FileDownloader::OutputFile(const std::string& path) {
  output_path_ = path;
}

void FileDownloader::OutputFile(const char* const path) {
  OutputFile(std::string(path));
}

bool FileDownloader::CACert(const char* const path) {
  if (path == nullptr) {
    CLOG(WARNING) << "CA certificate bundle path unset";
    return false;
  }

  auto result = curl_easy_setopt(curl_, CURLOPT_CAINFO, path);

  if (result != CURLE_OK) {
    CLOG(WARNING) << "Unable to set path to CA certificate bundle - " << curl_easy_strerror(result);
    return false;
  }
  return true;
}

bool FileDownloader::Cert(const char* const path) {
  if (path == nullptr) {
    CLOG(WARNING) << "SSL client certificate path unset";
    return false;
  }

  auto result = curl_easy_setopt(curl_, CURLOPT_SSLCERT, path);

  if (result != CURLE_OK) {
    CLOG(WARNING) << "Unable to set SSL client certificate - " << curl_easy_strerror(result);
    return false;
  }
  return true;
}

bool FileDownloader::Key(const char* const path) {
  if (path == nullptr) {
    CLOG(WARNING) << "SSL client key file path unset";
    return false;
  }

  auto result = curl_easy_setopt(curl_, CURLOPT_SSLKEY, path);

  if (result != CURLE_OK) {
    CLOG(WARNING) << "Unable to set key file - " << curl_easy_strerror(result);
    return false;
  }
  return true;
}

bool FileDownloader::ConnectTo(const std::string& entry) {
  return ConnectTo(entry.c_str());
}

bool FileDownloader::ConnectTo(const char* const entry) {
  curl_slist* temp = curl_slist_append(connect_to_, entry);

  if (temp == nullptr) {
    CLOG(WARNING) << "Unable to set option to connect to '" << entry;
    return false;
  }

  connect_to_ = temp;

  return true;
}

void FileDownloader::SetVerboseMode(bool verbose) {
  if (logging::GetLogLevel() <= logging::LogLevel::DEBUG && verbose) {
    curl_easy_setopt(curl_, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl_, CURLOPT_DEBUGFUNCTION, DebugCallback);
  } else {
    curl_easy_setopt(curl_, CURLOPT_VERBOSE, 0L);
  }
}

void FileDownloader::ResetCURL() {
  curl_easy_reset(curl_);

  SetDefaultOptions();

  curl_slist_free_all(connect_to_);
  connect_to_ = nullptr;
}

bool FileDownloader::IsReady() {
  return curl_ != nullptr;
}

bool FileDownloader::Download() {
  if (curl_ == nullptr) {
    CLOG(WARNING) << "Attempted to download a file using an uninitialized object";
    return false;
  }

  if (output_path_.empty()) {
    CLOG(WARNING) << "Attempted to download a file without an output set";
    return false;
  }

  if (connect_to_) {
    auto result = curl_easy_setopt(curl_, CURLOPT_CONNECT_TO, connect_to_);

    if (result != CURLE_OK) {
      CLOG(WARNING) << "Unable to set connection host, the download is likely to fail - " << curl_easy_strerror(result);
    }
  }

  auto start_time = std::chrono::steady_clock::now();
  int failures = 0;
  bool encountered_404 = false;

  for (auto max_attempts = retry_.times; max_attempts; max_attempts--) {
    std::ofstream of(output_path_, std::ios::trunc | std::ios::binary);
    DownloadData download_data = {.http_status = 0, .error_msg = "", .os = &of};

    if (!of.is_open()) {
      CLOG(WARNING) << "Failed to open " << output_path_;
      return false;
    }

    curl_easy_setopt(curl_, CURLOPT_HEADERDATA, static_cast<void*>(&download_data));
    curl_easy_setopt(curl_, CURLOPT_WRITEDATA, static_cast<void*>(&download_data));

    error_.fill('\0');
    auto result = curl_easy_perform(curl_);

    if (result == CURLE_OK) {
      return true;
    }

    failures++;

    if (download_data.http_status == 404) {
      CLOG_IF(!encountered_404, INFO) << "HTTP Request failed with error code 404";
      encountered_404 = true;
    } else if (download_data.http_status >= 400) {
      CLOG_THROTTLED(WARNING, std::chrono::seconds(10))
          << "Unexpected HTTP request failure (HTTP " << download_data.http_status << ")";
    }

    if (max_attempts == 1) break;

    sleep(retry_.delay);

    auto time_elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start_time);
    if (time_elapsed > retry_.max_time) {
      CLOG(WARNING) << "Timeout while retrying to download " << url_path_;
      return false;
    }
  }

  CLOG(WARNING) << "Attempted to download " << url_path_ << " " << failures << " time(s)";
  CLOG(WARNING) << "Failed to download from " << url_path_;

  return false;
}

void FileDownloader::SetDefaultOptions() {
  curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, WriteFile);
  curl_easy_setopt(curl_, CURLOPT_HEADERFUNCTION, HeaderCallback);
  curl_easy_setopt(curl_, CURLOPT_ERRORBUFFER, error_.data());
}

}  // namespace collector
