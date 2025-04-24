#include <iostream>
#include <fstream>
#include <string>
#include <filesystem>
#include <ctime>
#include <random>
#include <nlohmann/json.hpp>
#include <archive.h>
#include <archive_entry.h>
#include <algorithm>
#include <vector>
#include <chrono>
#include <iomanip>

namespace fs = std::filesystem;
using json = nlohmann::json;

// ANSI Color codes
namespace Color {
    const std::string RED = "\033[31m";
    const std::string GREEN = "\033[32m";
    const std::string BLUE = "\033[34m";
    const std::string YELLOW = "\033[33m";
    const std::string CYAN = "\033[36m";
    const std::string RESET = "\033[0m";
}

class Version {
public:
    Version(const std::string& ver) : version(ver) {
        base_version = version;
        size_t pos = base_version.find('+');
        if (pos != std::string::npos)
            base_version = base_version.substr(0, pos);
        pos = base_version.find('-');
        if (pos != std::string::npos)
            base_version = base_version.substr(0, pos);
    }

    bool operator>(const Version& other) const {
        return compareVersions(version, other.version) > 0;
    }

    std::string getBaseVersion() const {
        return base_version;
    }

private:
    std::string version;
    std::string base_version;

    static int compareVersions(const std::string& v1, const std::string& v2) {
        std::vector<int> ver1 = parseVersion(v1);
        std::vector<int> ver2 = parseVersion(v2);
        
        for (size_t i = 0; i < std::min(ver1.size(), ver2.size()); ++i) {
            if (ver1[i] != ver2[i])
                return ver1[i] - ver2[i];
        }
        return ver1.size() - ver2.size();
    }

    static std::vector<int> parseVersion(const std::string& version) {
        std::vector<int> result;
        std::string num;
        for (char c : version) {
            if (c == '.') {
                if (!num.empty()) {
                    result.push_back(std::stoi(num));
                    num.clear();
                }
            } else if (std::isdigit(c)) {
                num += c;
            } else break;
        }
        if (!num.empty())
            result.push_back(std::stoi(num));
        return result;
    }
};


void log(const std::string& text, const std::string& color) {
    std::cout << color << "[Tulpar Server, APGunpacker] " 
              << text << Color::RESET << std::endl;
}

bool extractApg(const std::string& archivePath, const std::string& destPath) {
    struct archive *a = archive_read_new();
    struct archive *ext = archive_write_disk_new();
    struct archive_entry *entry;
    bool metadata_found = false;

    if (!a || !ext) {
        log("Failed to initialize archive handlers", Color::RED);
        return false;
    }

    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    
    archive_write_disk_set_options(ext, ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | 
                                      ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS);
    archive_write_disk_set_standard_lookup(ext);

    if (archive_read_open_filename(a, archivePath.c_str(), 10240) != ARCHIVE_OK) {
        log("Failed to open archive: " + std::string(archive_error_string(a)), Color::RED);
        archive_read_free(a);
        archive_write_free(ext);
        return false;
    }

    int result;
    while ((result = archive_read_next_header(a, &entry)) == ARCHIVE_OK) {
        const char* current_file = archive_entry_pathname(entry);
        
        if (std::string(current_file) == "metadata.json") {
            metadata_found = true;
        }

        std::string full_path = destPath + "/" + current_file;
        archive_entry_set_pathname(entry, full_path.c_str());
        
        result = archive_write_header(ext, entry);
        if (result != ARCHIVE_OK) {
            log("Failed to write header: " + std::string(archive_error_string(ext)), Color::RED);
            continue;
        }

        if (archive_entry_size(entry) > 0) {
            const void *buff;
            size_t size;
            la_int64_t offset;
            
            while ((result = archive_read_data_block(a, &buff, &size, &offset)) == ARCHIVE_OK) {
                if (archive_write_data_block(ext, buff, size, offset) != ARCHIVE_OK) {
                    log("Failed to write data block: " + std::string(archive_error_string(ext)), Color::RED);
                    break;
                }
            }
            
            if (result != ARCHIVE_OK && result != ARCHIVE_EOF) {
                log("Failed to read data block: " + std::string(archive_error_string(a)), Color::RED);
            }
        }
    }

    archive_read_close(a);
    archive_read_free(a);
    archive_write_close(ext);
    archive_write_free(ext);

    if (!metadata_found) {
        log("metadata.json not found in the archive", Color::RED);
        return false;
    }

    return true;
}

std::string generateTempPath() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<long long> dis(1, 99999999999999);
    return std::to_string(dis(gen));
}

int main(int argc, char* argv[]) {
    if (argc != 2) {
        log("Usage: " + std::string(argv[0]) + " <archivepath>", Color::RED);
        return 1;
    }

    std::string archive_path = argv[1];
    if (!fs::exists(archive_path)) {
        log("Archive " + archive_path + " is not found.", Color::RED);
        return 0;
    }

    if (fs::path(archive_path).extension() != ".apg") {
        log("Error: File must be an APG package.", Color::RED);
        return 1;
    }

    std::string package_name = fs::path(archive_path).filename().string();
    log("Retrieving metadata from " + package_name + "...", Color::BLUE);

    std::string temp_metadata_path = generateTempPath();
    fs::create_directories(temp_metadata_path);

    if (!extractApg(archive_path, temp_metadata_path)) {
        log("Failed to extract APG package. Please check if the file is a valid APG package.", Color::RED);
        fs::remove_all(temp_metadata_path);
        return 1;
    }

    fs::path metadata_file_path = fs::path(temp_metadata_path) / "metadata.json";
    if (!fs::exists(metadata_file_path)) {
        log("metadata.json not found after extraction", Color::RED);
        fs::remove_all(temp_metadata_path);
        return 1;
    }

    json metadata;
    try {
        std::ifstream metadata_file(metadata_file_path);
        if (!metadata_file.is_open()) {
            log("Cannot open metadata.json: " + std::string(strerror(errno)), Color::RED);
            fs::remove_all(temp_metadata_path);
            return 1;
        }

        std::string content((std::istreambuf_iterator<char>(metadata_file)),
                           std::istreambuf_iterator<char>());
        
        if (content.empty()) {
            log("metadata.json is empty", Color::RED);
            fs::remove_all(temp_metadata_path);
            return 1;
        }

        metadata = json::parse(content);
    } catch (const std::exception& e) {
        log("Failed to parse metadata.json: " + std::string(e.what()), Color::RED);
        fs::remove_all(temp_metadata_path);
        return 1;
    }

    std::string package_arch = metadata["architecture"];
    if (package_arch != "x86_64" && package_arch != "aarch64") {
        log("Note: unknown architecture value (" + package_arch + 
            ") detected, selecting other value... This package is not passed apgcheck!", Color::YELLOW);
        
        if (package_arch.find("amd") != std::string::npos)
            package_arch = "x86_64";
        else if (package_arch.find("x86") != std::string::npos)
            package_arch = "i386";
        else if (package_arch.find("arm") != std::string::npos || 
                 package_arch.find("aarch") != std::string::npos)
            package_arch = "aarch64";
        else {
            log("Failed to change architecture of package!", Color::RED);
            fs::remove_all(temp_metadata_path);
            return 1;
        }
    }

    fs::path package_path = fs::path("packages") / metadata["name"].get<std::string>();
    Version package_version(metadata["version"]);
    fs::path package_arch_path = package_path / package_arch;
    fs::path main_metadata_path = package_path / "metadata.json";
    fs::path new_version_package_path = package_arch_path / (package_version.getBaseVersion() + ".apg");

    if (fs::exists(package_path)) {
        log("This package exists in this server, checking...", Color::CYAN);
        
        json main_metadata;
        {
            std::ifstream main_metadata_file(main_metadata_path);
            main_metadata = json::parse(main_metadata_file);
        }

        if (std::find(main_metadata["architecture"].begin(),
                     main_metadata["architecture"].end(),
                     package_arch) == main_metadata["architecture"].end()) {
            log("The package of new architecture will be added...", Color::GREEN);
            fs::create_directories(package_arch_path);
        } else {
            log("The package of this architecture is already added!", Color::BLUE);
        }

        Version current_version(main_metadata["version"]);
        if (package_version > current_version) {
            log("The package of newer version will be added...", Color::GREEN);
            fs::rename(archive_path, new_version_package_path);
        } else if (fs::exists(new_version_package_path)) {
            log("The package of this version is already added!", Color::BLUE);
        } else {
            log("The package of older version will be added.", Color::YELLOW);
        }

        if (!main_metadata["architecture"].is_array())
            main_metadata["architecture"] = json::array();
        
        if (std::find(main_metadata["architecture"].begin(),
                     main_metadata["architecture"].end(),
                     package_arch) == main_metadata["architecture"].end()) {
            main_metadata["architecture"].push_back(package_arch);
        }

        for (auto& [key, value] : metadata.items()) {
            if (key != "architecture")
                main_metadata[key] = value;
        }

        std::ofstream main_metadata_file(main_metadata_path);
        main_metadata_file << main_metadata.dump(4);
    } else {
        log("Adding a new package...", Color::GREEN);
        fs::create_directories(package_arch_path);
        fs::rename(archive_path, new_version_package_path);
        
        log("Parsing a main metadata...", Color::GREEN);
        metadata["architecture"] = json::array({package_arch});
        
        std::ofstream main_metadata_file(main_metadata_path);
        main_metadata_file << metadata.dump(4);
    }

    fs::remove_all(temp_metadata_path);
    return 0;
}