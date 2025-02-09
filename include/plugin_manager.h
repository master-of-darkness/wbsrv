#include <vector>
#include <iostream>
#include <dlfcn.h>
#include "plugin.h"

// Structure to store information about a loaded plugin.
struct PluginInfo {
    std::string path; // File path to the shared library.
    void *handle; // Handle returned by dlopen.
    Plugin *instance; // The instance of the plugin.
    create_t *create; // Pointer to the create function.
    destroy_t *destroy; // Pointer to the destroy function.
};


class PluginManager {
public:
    ~PluginManager() {
        // Ensure all plugins are unloaded when the manager is destroyed.
        unloadAllPlugins();
    }

    // Loads a plugin from the given path.
    bool loadPlugin(const std::string &path) {
        // Open the shared library.
        void *handle = dlopen(path.c_str(), RTLD_LAZY);
        if (!handle) {
            std::cerr << "dlopen error (" << path << "): " << dlerror() << "\n";
            return false;
        }

        // Clear any existing error.
        dlerror();

        // Load the 'create' symbol.
        create_t *createFunc = reinterpret_cast<create_t *>(dlsym(handle, "create"));
        const char *error = dlerror();
        if (error) {
            std::cerr << "dlsym create error (" << path << "): " << error << "\n";
            dlclose(handle);
            return false;
        }

        // Load the 'destroy' symbol.
        destroy_t *destroyFunc = reinterpret_cast<destroy_t *>(dlsym(handle, "destroy"));
        error = dlerror();
        if (error) {
            std::cerr << "dlsym destroy error (" << path << "): " << error << "\n";
            dlclose(handle);
            return false;
        }

        // Create the plugin instance.
        Plugin *instance = createFunc();
        if (!instance) {
            std::cerr << "Failed to create plugin instance (" << path << ").\n";
            dlclose(handle);
            return false;
        }

        // Store all plugin data.
        PluginInfo info;
        info.path = path;
        info.handle = handle;
        info.instance = instance;
        info.create = createFunc;
        info.destroy = destroyFunc;

        plugins.push_back(info);
        return true;
    }

    // Unloads a plugin at the given index.
    bool unloadPlugin(size_t index) {
        if (index >= plugins.size()) {
            std::cerr << "Invalid plugin index: " << index << "\n";
            return false;
        }

        PluginInfo &info = plugins[index];

        // Destroy the plugin instance.
        info.destroy(info.instance);

        // Close the shared library.
        dlclose(info.handle);

        // Remove the plugin from our list.
        plugins.erase(plugins.begin() + index);

        return true;
    }

    // Unloads all plugins.
    void unloadAllPlugins() {
        for (auto &info: plugins) {
            // Destroy plugin instance.
            info.destroy(info.instance);
            // Close the shared library.
            dlclose(info.handle);
        }
        plugins.clear();
    }

    // Optionally, provide a way to access a loaded plugin.
    Plugin *getPlugin(size_t index) {
        if (index >= plugins.size())
            return nullptr;
        return plugins[index].instance;
    }

    // Returns the number of loaded plugins.
    size_t count() const {
        return plugins.size();
    }

private:
    std::vector<PluginInfo> plugins;
};
