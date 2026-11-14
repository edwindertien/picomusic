#pragma once
// In-memory LittleFS stand-in for the host tests (same calls drum_storage.h uses). A "power cut" can be simulated.
#include <map>
#include <string>
#include <vector>
#include <cstdint>
#include <cstring>
struct MockFS;
struct File {
    MockFS* fs = nullptr; std::string name; bool wr = false; size_t pos = 0; std::vector<uint8_t> data; bool ok = false;
    explicit operator bool() const { return ok; }
    size_t write(const uint8_t* b, size_t n); size_t read(uint8_t* b, size_t n); void close();
};
struct MockFS {
    std::map<std::string, std::vector<uint8_t>> files; bool began = false; bool failWrite = false; bool cutAfterTmp = false;
    bool begin() { began = true; return true; }
    File open(const char* p, const char* mode) {
        File f; f.fs = this; f.name = p; f.wr = (mode[0] == 'w');
        if (f.wr) { f.ok = true; } else { auto it = files.find(p); if (it == files.end()) return f; f.data = it->second; f.ok = true; }
        return f;
    }
    bool remove(const char* p) { return files.erase(p) > 0; }
    bool rename(const char* a, const char* b) { if (cutAfterTmp) return false; auto it = files.find(a); if (it == files.end()) return false; files[b] = it->second; files.erase(a); return true; }
};
inline MockFS LittleFS;
inline size_t File::write(const uint8_t* b, size_t n) { if (fs->failWrite) return 0; data.insert(data.end(), b, b + n); return n; }
inline size_t File::read(uint8_t* b, size_t n) { size_t k = std::min(n, data.size() - pos); memcpy(b, data.data() + pos, k); pos += k; return k; }
inline void File::close() { if (wr && ok) fs->files[name] = data; ok = false; }
