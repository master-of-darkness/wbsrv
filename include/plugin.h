#include <string>
#include <map>

// Forward declarations
struct Request;
struct Response;

struct Request {
    std::string method; // HTTP method: GET, POST, etc.
    std::string uri; // Request URI.
    std::map<std::string, std::string> headers; // HTTP headers.
    std::string body; // Request body.
};

struct Response {
    int statusCode = 200; // HTTP status code (default OK).
    std::map<std::string, std::string> headers; // HTTP headers.
    std::string body; // Response body.
    // Additional fields can be added as needed.
};

// Base class for web server plugins.
class Plugin {
public:
    virtual ~Plugin() {
    }

    /**
     * @brief Initializes the plugin.
     */
    virtual bool initialize() = 0;

    /**
     * @brief Shuts down the plugin, allowing for cleanup of resources.
     */
    virtual void shutdown() = 0;

    /**
     * @brief Optional: Handle custom events.
     *
     * @param event A string identifying the event.
     * @param data  Any associated event data.
     */
    virtual void onEvent(const std::string &event, const std::string &data) {
        // Default implementation does nothing.
    }

    /**
     * @brief Retrieve the name of the plugin.
     *
     * @return Plugin name as a string.
     */
    virtual std::string getName() const = 0;

    /**
     * @brief Retrieve the version of the plugin.
     *
     * @return Plugin version as a string.
     */
    virtual std::string getVersion() const = 0;
};


// C-style API for dynamic loading.
// These functions allow the web server to create and destroy plugin instances
// without needing to know the concrete type at compile time.
extern "C" {
    // Function pointer type for creating a new plugin instance.
    typedef Plugin *create_t();

    // Function pointer type for destroying a plugin instance.
    typedef void destroy_t(Plugin *);
}
