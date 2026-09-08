#pragma once
// ---------------------------------------------------------------------------
// DataFile.h — reading a table of named values out of a text file.
//
// The engine could load PNGs and nothing else. Every number that balanced a
// game was a `constexpr`, which means changing one costs a recompile and
// anyone who is not a programmer cannot change one at all. Real games in this
// genre keep their balance in files exported from a spreadsheet; the reference
// this project is chasing ships one literally called `XlsBalance`.
//
// The format is sections of key/value pairs, chosen because it is the shape
// the data actually has and it says what it means:
//
//     # comments run to the end of the line
//     [unit]
//     name = SOLDIER
//     cost = 60
//     health = 110
//
//     [unit]
//     name = ARCHER
//     cost = 95
//
// Sections repeat. That is the whole reason not to use a flat key/value file:
// a roster is a list of things with the same fields, and so is a set of
// stages, which is what will want this next.
//
// Three deliberate properties:
//
//   - **Paths resolve against the executable**, not the working directory —
//     the same rule TextureCache uses, so "assets/units.txt" means the same
//     thing whether the game was launched from a shell, an IDE, or a
//     double-click.
//   - **A missing or broken file is not fatal.** load() reports whether it
//     found anything, and every getter takes a fallback. A game keeps its
//     built-in defaults and carries on, exactly as a missing PNG leaves a
//     flat coloured rectangle.
//   - **It does not validate.** A key that is not a number returns the
//     fallback. Nothing here throws, and nothing here is a schema; the game
//     knows what it expects and says so at each call site.
//
// What it is not: fast, or a general parser. It is read once at startup.
// ---------------------------------------------------------------------------

#include <SDL.h>

#include <cstdlib>
#include <limits>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {

// One [section] and the keys under it.
class DataSection {
public:
    explicit DataSection(std::string name) : name_(std::move(name)) {}

    const std::string& name() const { return name_; }
    bool has(const std::string& key) const {
        return values_.find(key) != values_.end();
    }

    // Every getter takes what to use when the key is missing or unreadable,
    // which is what makes a partial file useful rather than dangerous: a row
    // that sets only `cost` keeps the built-in value for everything else.
    std::string text(const std::string& key, const std::string& fallback) const {
        auto found = values_.find(key);
        return found == values_.end() ? fallback : found->second;
    }

    float number(const std::string& key, float fallback) const {
        auto found = values_.find(key);
        if (found == values_.end()) return fallback;

        // strtof rather than stof: it reports failure by not advancing the
        // pointer instead of by throwing, and a malformed number in a data
        // file is a thing to shrug at, not to crash over.
        const char* start = found->second.c_str();
        char* end = nullptr;
        const float parsed = std::strtof(start, &end);
        return (end == start) ? fallback : parsed;
    }

    // Clamped to what an int can hold before the cast, not after.
    //
    // Every data file this engine reads is a text file somebody can edit, and
    // a float outside int's range is UNDEFINED BEHAVIOUR to cast, not a large
    // number to reject later. `stages_unlocked = 1e20` in a save file went
    // through this and was clamped by its caller afterwards — one step too
    // late to matter. NaN fails both comparisons and falls through to the
    // fallback, which is the right answer for it too.
    int integer(const std::string& key, int fallback) const {
        const float value = number(key, static_cast<float>(fallback));
        constexpr float kIntMax = 2147483520.0f;   // the largest float < INT_MAX
        constexpr float kIntMin = -2147483648.0f;
        if (value >= kIntMax) return std::numeric_limits<int>::max();
        if (value <= kIntMin) return std::numeric_limits<int>::min();
        if (!(value == value)) return fallback;    // NaN
        return static_cast<int>(value);
    }

    void set(std::string key, std::string value) {
        values_[std::move(key)] = std::move(value);
    }

private:
    std::string name_;
    std::unordered_map<std::string, std::string> values_;
};

// Where a game may WRITE.
//
// Everything else in this engine resolves paths against the executable, which
// is right for things that ship with the game and wrong for a save: the folder
// a game is installed into is frequently read-only, and on every desktop
// platform there is a proper per-user place for this. SDL knows where it is on
// each of them.
//
// Falls back to the plain relative path if SDL cannot say — a save that lands
// in the working directory is worse than one in the right place and far better
// than none.
inline std::string userPath(const std::string& organisation,
                            const std::string& application,
                            const std::string& file) {
    char* base = SDL_GetPrefPath(organisation.c_str(), application.c_str());
    if (!base) return file;

    std::string full = std::string(base) + file;
    SDL_free(base);
    return full;
}

// Writes the format DataFile reads.
//
// Deliberately the same format in both directions rather than something
// terser or binary for saves. A save file you can open, read and correct by
// hand is worth more than a compact one in a project this size — and a single
// format means the loader is already tested by everything that reads a data
// file.
class DataWriter {
public:
    void beginSection(const std::string& name) {
        text_ += "[" + name + "]\n";
    }

    void set(const std::string& key, const std::string& value) {
        text_ += key + " = " + value + "\n";
    }

    void set(const std::string& key, int value) {
        set(key, std::to_string(value));
    }

    void set(const std::string& key, float value) {
        set(key, std::to_string(value));
    }

    void blank() { text_ += "\n"; }

    // Returns false if the file could not be written — a full disk, a
    // read-only folder, a path that does not exist. Callers are expected to
    // carry on: failing to save is a thing to report, not to die of.
    bool save(const std::string& path) const {
        std::ofstream file(path, std::ios::trunc);
        if (!file) return false;
        file << text_;
        return file.good();
    }

    const std::string& text() const { return text_; }

private:
    std::string text_;
};

class DataFile {
public:
    // Returns false if the file could not be opened. The object is still
    // usable afterwards — it simply has no sections, so every lookup falls
    // back and the caller keeps whatever it had.
    bool load(const std::string& path) {
        sections_.clear();

        std::ifstream file(resolve(path));
        if (!file) return false;

        std::string line;
        while (std::getline(file, line)) parse(line);
        return true;
    }

    // Every section with this name, in file order.
    std::vector<const DataSection*> all(const std::string& name) const {
        std::vector<const DataSection*> found;
        for (const DataSection& section : sections_) {
            if (section.name() == name) found.push_back(&section);
        }
        return found;
    }

    // The first section with this name, or nullptr.
    const DataSection* first(const std::string& name) const {
        for (const DataSection& section : sections_) {
            if (section.name() == name) return &section;
        }
        return nullptr;
    }

    std::size_t sectionCount() const { return sections_.size(); }

private:
    void parse(std::string line) {
        // Comments first, so a '#' anywhere ends the line. That means a value
        // cannot contain one, which no value here ever needs to.
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) line.erase(comment);

        trim(line);
        if (line.empty()) return;

        if (line.front() == '[' && line.back() == ']') {
            sections_.emplace_back(line.substr(1, line.size() - 2));
            return;
        }

        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) return;  // not a pair; ignore it

        // A key before any [section] would have nowhere to go, so an unnamed
        // section is opened for it rather than dropping it silently.
        if (sections_.empty()) sections_.emplace_back("");

        std::string key = line.substr(0, equals);
        std::string value = line.substr(equals + 1);
        trim(key);
        trim(value);
        if (!key.empty()) sections_.back().set(std::move(key), std::move(value));
    }

    static void trim(std::string& text) {
        const char* spaces = " \t\r\n";
        const std::size_t first = text.find_first_not_of(spaces);
        if (first == std::string::npos) {
            text.clear();
            return;
        }
        text = text.substr(first, text.find_last_not_of(spaces) - first + 1);
    }

    // The same rule TextureCache uses: relative to the executable, not to
    // wherever the shell happened to be. An absolute path is left alone.
    static std::string resolve(const std::string& path) {
        const char backslash = static_cast<char>(92);  // '\\', spelled out
        if (!path.empty() && (path.front() == '/' || path.front() == backslash)) {
            return path;
        }
        if (path.size() > 1 && path[1] == ':') return path;  // a drive letter

        char* base = SDL_GetBasePath();
        if (!base) return path;
        std::string full = std::string(base) + path;
        SDL_free(base);
        return full;
    }

    std::vector<DataSection> sections_;
};

}  // namespace engine
