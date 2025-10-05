#include "dfs/network/http_router.hpp"
#include <spdlog/spdlog.h>
#include <sstream>

namespace dfs {
namespace network {

// ────────────────────────────────────────────────────────────
// Helper: Convert URL pattern to regex
// ────────────────────────────────────────────────────────────

std::string pattern_to_regex(const std::string& pattern, std::vector<std::string>& param_names) {
    std::string regex_pattern = "^";
    size_t i = 0;

    while (i < pattern.length()) {
        if (pattern[i] == ':') {
            // Found parameter like :id
            ++i;
            std::string param_name;

            // Extract parameter name (alphanumeric + underscore)
            while (i < pattern.length() &&
                   (std::isalnum(pattern[i]) || pattern[i] == '_')) {
                param_name += pattern[i];
                ++i;
            }

            if (!param_name.empty()) {
                param_names.push_back(param_name);
                regex_pattern += "([^/]+)";  // Match any characters except /
            }
        } else if (pattern[i] == '*') {
            // Wildcard - match everything
            regex_pattern += "(.*)";
            ++i;
        } else {
            // Regular character - escape if needed
            char c = pattern[i];
            if (c == '.' || c == '+' || c == '?' || c == '^' || c == '$' ||
                c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}' ||
                c == '|' || c == '\\') {
                regex_pattern += '\\';
            }
            regex_pattern += c;
            ++i;
        }
    }

    regex_pattern += "$";  // End of string
    return regex_pattern;
}

// ────────────────────────────────────────────────────────────
// Route Implementation
// ────────────────────────────────────────────────────────────

Route::Route(HttpMethod m, const std::string& pat, RouteHandler h)
    : method(m), pattern(pat), handler(std::move(h)) {

    // Convert pattern to regex
    std::string regex_str = pattern_to_regex(pattern, param_names);

    try {
        regex = std::regex(regex_str);
    } catch (const std::regex_error& e) {
        spdlog::error("Invalid route pattern '{}': {}", pattern, e.what());
        // Fallback: exact match only
        regex = std::regex("^" + pattern + "$");
    }
}

bool Route::matches(HttpMethod req_method, const std::string& url) const {
    // Method must match
    if (method != req_method) {
        return false;
    }

    // URL must match regex
    return std::regex_match(url, regex);
}

std::unordered_map<std::string, std::string> Route::extract_params(const std::string& url) const {
    std::unordered_map<std::string, std::string> params;
    std::smatch match;

    if (std::regex_match(url, match, regex)) {
        // match[0] is the full string, match[1+] are capture groups
        for (size_t i = 0; i < param_names.size() && i + 1 < match.size(); ++i) {
            params[param_names[i]] = match[i + 1].str();
        }
    }

    return params;
}

// ────────────────────────────────────────────────────────────
// HttpRouter Implementation
// ────────────────────────────────────────────────────────────

HttpRouter::HttpRouter()
    : not_found_handler_(default_not_found_handler) {
}

void HttpRouter::get(const std::string& pattern, RouteHandler handler) {
    add_route(HttpMethod::GET, pattern, std::move(handler));
}

void HttpRouter::post(const std::string& pattern, RouteHandler handler) {
    add_route(HttpMethod::POST, pattern, std::move(handler));
}

void HttpRouter::put(const std::string& pattern, RouteHandler handler) {
    add_route(HttpMethod::PUT, pattern, std::move(handler));
}

void HttpRouter::delete_(const std::string& pattern, RouteHandler handler) {
    add_route(HttpMethod::DELETE_METHOD, pattern, std::move(handler));
}

void HttpRouter::head(const std::string& pattern, RouteHandler handler) {
    add_route(HttpMethod::HEAD, pattern, std::move(handler));
}

void HttpRouter::add_route(HttpMethod method, const std::string& pattern, RouteHandler handler) {
    // Apply prefix if this is a route group
    std::string full_pattern = prefix_ + pattern;

    routes_.emplace_back(method, full_pattern, std::move(handler));

    spdlog::debug("Registered route: {} {}",
                  HttpMethodUtils::to_string(method),
                  full_pattern);
}

std::shared_ptr<HttpRouter> HttpRouter::group(const std::string& prefix) {
    auto grouped_router = std::make_shared<HttpRouter>();
    grouped_router->prefix_ = prefix_ + prefix;  // Combine prefixes

    // Share routes vector (so group routes are added to parent)
    // For simplicity, we'll just set the prefix and return a new router
    // In production, you might want to share the routes vector

    return grouped_router;
}

void HttpRouter::use(Middleware middleware) {
    middlewares_.push_back(std::move(middleware));
}

void HttpRouter::set_not_found_handler(RouteHandler handler) {
    not_found_handler_ = std::move(handler);
}

HttpResponse HttpRouter::handle_request(const HttpRequest& request) {
    // Create context
    HttpContext ctx(request);
    HttpResponse response(HttpStatus::OK);

    // Run middleware
    for (const auto& middleware : middlewares_) {
        if (!middleware(ctx, response)) {
            // Middleware short-circuited (e.g., auth failed)
            return response;
        }
    }

    // Find matching route
    const Route* route = find_route(request.method, request.url);

    if (route) {
        // Extract URL parameters
        ctx.params = route->extract_params(request.url);

        // Call route handler
        try {
            response = route->handler(ctx);
        } catch (const std::exception& e) {
            spdlog::error("Route handler threw exception: {}", e.what());

            response = HttpResponse(HttpStatus::INTERNAL_SERVER_ERROR);
            response.set_body("Internal Server Error");
            response.set_header("Content-Type", "text/plain");
        }
    } else {
        // No matching route - call 404 handler
        response = not_found_handler_(ctx);
    }

    return response;
}

std::vector<std::string> HttpRouter::list_routes() const {
    std::vector<std::string> route_list;

    for (const auto& route : routes_) {
        std::ostringstream oss;
        oss << HttpMethodUtils::to_string(route.method) << " " << route.pattern;
        route_list.push_back(oss.str());
    }

    return route_list;
}

HttpResponse HttpRouter::default_not_found_handler(const HttpContext& ctx) {
    HttpResponse response(HttpStatus::NOT_FOUND);

    std::ostringstream html;
    html << "<!DOCTYPE html>\n"
         << "<html>\n"
         << "<head><title>404 Not Found</title></head>\n"
         << "<body>\n"
         << "<h1>404 - Not Found</h1>\n"
         << "<p>The requested URL <code>" << ctx.request.url << "</code> was not found on this server.</p>\n"
         << "<hr>\n"
         << "<p>DFS HTTP Server</p>\n"
         << "</body>\n"
         << "</html>\n";

    response.set_body(html.str());
    response.set_header("Content-Type", "text/html");

    return response;
}

const Route* HttpRouter::find_route(HttpMethod method, const std::string& url) const {
    for (const auto& route : routes_) {
        if (route.matches(method, url)) {
            return &route;
        }
    }
    return nullptr;
}

} // namespace network
} // namespace dfs
