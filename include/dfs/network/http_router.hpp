#pragma once

#include "dfs/network/http_types.hpp"
#include <functional>
#include <vector>
#include <unordered_map>
#include <regex>
#include <memory>

namespace dfs {
namespace network {

/**
 * @brief Request context with URL parameters extracted from route
 */
struct HttpContext {
    const HttpRequest& request;
    std::unordered_map<std::string, std::string> params;  // URL parameters like :id

    explicit HttpContext(const HttpRequest& req) : request(req) {}

    // Get URL parameter by name
    std::string get_param(const std::string& name, const std::string& default_value = "") const {
        auto it = params.find(name);
        return (it != params.end()) ? it->second : default_value;
    }

    bool has_param(const std::string& name) const {
        return params.find(name) != params.end();
    }
};

/**
 * @brief Route handler function type
 */
using RouteHandler = std::function<HttpResponse(const HttpContext&)>;

/**
 * @brief Middleware function type (can modify request or short-circuit)
 */
using Middleware = std::function<bool(const HttpContext&, HttpResponse&)>;

/**
 * @brief Single route in the router
 */
struct Route {
    HttpMethod method;
    std::string pattern;              // Original pattern like "/users/:id"
    std::regex regex;                 // Compiled regex for matching
    std::vector<std::string> param_names;  // Extracted param names like ["id"]
    RouteHandler handler;

    Route(HttpMethod m, const std::string& pat, RouteHandler h);

    // Check if this route matches the request
    bool matches(HttpMethod method, const std::string& url) const;

    // Extract URL parameters from matched URL
    std::unordered_map<std::string, std::string> extract_params(const std::string& url) const;
};

/**
 * @brief HTTP Router for organizing request handlers
 *
 * Features:
 * - Method-based routing (GET, POST, PUT, DELETE, etc.)
 * - URL parameter extraction (/users/:id)
 * - Middleware support (logging, auth, etc.)
 * - Route groups (/api/v1/...)
 * - Custom 404 handlers
 *
 * Example usage:
 * @code
 * Router router;
 *
 * // Simple routes
 * router.get("/", [](const HttpContext& ctx) {
 *     HttpResponse res(HttpStatus::OK);
 *     res.set_body("Home page");
 *     return res;
 * });
 *
 * // URL parameters
 * router.get("/users/:id", [](const HttpContext& ctx) {
 *     std::string id = ctx.get_param("id");
 *     HttpResponse res(HttpStatus::OK);
 *     res.set_body("User ID: " + id);
 *     return res;
 * });
 *
 * // POST with body
 * router.post("/api/users", [](const HttpContext& ctx) {
 *     std::string body = ctx.request.body_as_string();
 *     // Process body...
 *     return HttpResponse(HttpStatus::CREATED);
 * });
 *
 * // Route groups
 * auto api = router.group("/api");
 * api->post("/login", handle_login);
 * api->get("/status", handle_status);
 *
 * // Middleware
 * router.use([](const HttpContext& ctx, HttpResponse& res) {
 *     spdlog::info("{} {}", HttpMethodUtils::to_string(ctx.request.method), ctx.request.url);
 *     return true;  // Continue to handler
 * });
 * @endcode
 */
class HttpRouter {
public:
    HttpRouter();

    // ────────────────────────────────────────────────────────────
    // Route Registration
    // ────────────────────────────────────────────────────────────

    /**
     * @brief Register a GET route
     * @param pattern URL pattern (e.g., "/users/:id")
     * @param handler Function to handle matching requests
     */
    void get(const std::string& pattern, RouteHandler handler);

    /**
     * @brief Register a POST route
     */
    void post(const std::string& pattern, RouteHandler handler);

    /**
     * @brief Register a PUT route
     */
    void put(const std::string& pattern, RouteHandler handler);

    /**
     * @brief Register a DELETE route
     */
    void delete_(const std::string& pattern, RouteHandler handler);

    /**
     * @brief Register a HEAD route
     */
    void head(const std::string& pattern, RouteHandler handler);

    /**
     * @brief Register a route for any HTTP method
     */
    void add_route(HttpMethod method, const std::string& pattern, RouteHandler handler);

    // ────────────────────────────────────────────────────────────
    // Route Groups (for organizing related routes)
    // ────────────────────────────────────────────────────────────

    /**
     * @brief Create a route group with a common prefix
     * @param prefix Common URL prefix (e.g., "/api/v1")
     * @return Pointer to new router scoped to prefix
     *
     * Example:
     * @code
     * auto api = router.group("/api");
     * api->get("/users", list_users);      // Matches GET /api/users
     * api->post("/users", create_user);    // Matches POST /api/users
     * @endcode
     */
    std::shared_ptr<HttpRouter> group(const std::string& prefix);

    // ────────────────────────────────────────────────────────────
    // Middleware
    // ────────────────────────────────────────────────────────────

    /**
     * @brief Add middleware to run before route handlers
     * @param middleware Function that returns true to continue, false to short-circuit
     *
     * Middleware is executed in the order it's added. If middleware returns false,
     * request handling stops and the response is sent.
     *
     * Example:
     * @code
     * router.use([](const HttpContext& ctx, HttpResponse& res) {
     *     // Logging middleware
     *     spdlog::info("Request: {} {}", ctx.request.method, ctx.request.url);
     *     return true;  // Continue
     * });
     *
     * router.use([](const HttpContext& ctx, HttpResponse& res) {
     *     // Auth middleware
     *     if (!ctx.request.has_header("Authorization")) {
     *         res = HttpResponse(HttpStatus::UNAUTHORIZED);
     *         return false;  // Stop, don't call handler
     *     }
     *     return true;  // Continue
     * });
     * @endcode
     */
    void use(Middleware middleware);

    // ────────────────────────────────────────────────────────────
    // Custom Handlers
    // ────────────────────────────────────────────────────────────

    /**
     * @brief Set custom 404 Not Found handler
     * @param handler Function to call when no route matches
     */
    void set_not_found_handler(RouteHandler handler);

    // ────────────────────────────────────────────────────────────
    // Request Handling
    // ────────────────────────────────────────────────────────────

    /**
     * @brief Handle an HTTP request by finding matching route
     * @param request The HTTP request to handle
     * @return HTTP response from matched route handler or 404
     *
     * This is called by HttpServer for each request.
     */
    HttpResponse handle_request(const HttpRequest& request);

    // ────────────────────────────────────────────────────────────
    // Introspection
    // ────────────────────────────────────────────────────────────

    /**
     * @brief Get list of all registered routes (for debugging)
     */
    std::vector<std::string> list_routes() const;

    /**
     * @brief Get number of registered routes
     */
    size_t route_count() const { return routes_.size(); }

private:
    std::vector<Route> routes_;
    std::vector<Middleware> middlewares_;
    RouteHandler not_found_handler_;
    std::string prefix_;  // For route groups

    // Default 404 handler
    static HttpResponse default_not_found_handler(const HttpContext& ctx);

    // Find matching route for request
    const Route* find_route(HttpMethod method, const std::string& url) const;
};

// ────────────────────────────────────────────────────────────
// Helper Functions
// ────────────────────────────────────────────────────────────

/**
 * @brief Convert URL pattern to regex
 * @param pattern Pattern like "/users/:id/posts/:post_id"
 * @param param_names Output vector of parameter names
 * @return Regex that matches URLs like "/users/123/posts/456"
 *
 * Converts:
 *   "/users/:id"           → "^/users/([^/]+)$"
 *   "/posts/:id/comments"  → "^/posts/([^/]+)/comments$"
 */
std::string pattern_to_regex(const std::string& pattern, std::vector<std::string>& param_names);

} // namespace network
} // namespace dfs
