#pragma once
// ---------------------------------------------------------------------------
// Resources.h — loading image files, and loading each one only once.
//
// A texture is an image living in GPU memory. Getting one costs a disk read,
// a PNG decode and an upload, so a game that calls IMG_LoadTexture every time
// it spawns an enemy pays that price over and over and leaks a texture each
// time, because nothing ever frees them.
//
// TextureCache fixes both halves: ask for the same path twice and the second
// call returns the texture already loaded, and every texture is destroyed
// together when the cache goes away. That is all a "resource manager" is at
// this size — a map from a name to a loaded thing, plus ownership of it.
//
// Two details worth knowing:
//
//   - Paths are resolved relative to the EXECUTABLE, not the working
//     directory. Otherwise a game runs from your IDE and fails when
//     double-clicked, because "assets/x.png" means different things depending
//     on where the process happened to start. CMake copies assets/ next to
//     the binary after every build so the two always agree.
//   - A failed load is not fatal. load() returns nullptr, and a Sprite with a
//     null texture falls back to being drawn as a colored rectangle. Delete
//     assets/ entirely and this project still runs.
// ---------------------------------------------------------------------------

#include <SDL.h>
#include <SDL_image.h>

#include <iostream>
#include <string>
#include <unordered_map>

namespace engine {

class TextureCache {
public:
    explicit TextureCache(SDL_Renderer* renderer) : renderer_(renderer) {}
    ~TextureCache() { clear(); }

    // The cache owns raw SDL textures, which can't be meaningfully copied.
    TextureCache(const TextureCache&) = delete;
    TextureCache& operator=(const TextureCache&) = delete;

    // Returns the texture for `path` (relative to the executable), loading it
    // the first time and reusing it afterwards. Returns nullptr if the file
    // is missing or unreadable; callers are expected to cope.
    SDL_Texture* load(const std::string& path) {
        auto it = textures_.find(path);
        if (it != textures_.end()) return it->second;

        const std::string fullPath = resolvePath(path);
        SDL_Texture* texture = IMG_LoadTexture(renderer_, fullPath.c_str());

        if (texture) {
            // Honour the alpha channel a PNG carries, so transparent corners
            // stay transparent instead of drawing as black.
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        } else {
            std::cerr << "Failed to load texture '" << fullPath
                      << "': " << IMG_GetError()
                      << "\n(falling back to flat colored rectangles)\n";
        }

        // Cached either way, nullptr included: a missing file should be
        // reported once, not once per frame forever.
        textures_[path] = texture;
        return texture;
    }

    void clear() {
        for (auto& [path, texture] : textures_) {
            if (texture) SDL_DestroyTexture(texture);
        }
        textures_.clear();
    }

private:
    // Turns "assets/tiles.png" into an absolute path next to the running
    // executable, so where you launched from stops mattering.
    static std::string resolvePath(const std::string& path) {
        char* base = SDL_GetBasePath();
        if (!base) return path;  // Rare; the relative path is the best guess.

        std::string full = std::string(base) + path;
        SDL_free(base);
        return full;
    }

    SDL_Renderer* renderer_ = nullptr;
    std::unordered_map<std::string, SDL_Texture*> textures_;
};

}  // namespace engine
