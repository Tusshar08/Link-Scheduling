#include "config.h"

#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

struct Json {
    enum class Type { Null, Bool, Number, String, Object, Array };

    Type type = Type::Null;
    bool boolean = false;
    bool number_is_int = false;
    long long integer = 0;
    double real = 0.0;
    std::string str;
    std::map<std::string, Json> object;
    std::vector<Json> array;

    bool is_object() const { return type == Type::Object; }
    bool is_array() const { return type == Type::Array; }
    bool is_string() const { return type == Type::String; }
    bool is_int() const { return type == Type::Number && number_is_int; }

    bool contains(const std::string& key) const
    {
        return is_object() && object.find(key) != object.end();
    }

    const Json& at(const std::string& key) const
    {
        return object.at(key);
    }
};

class Parser {
public:
    explicit Parser(std::string text) : text_(std::move(text)) {}

    Json parse()
    {
        skip_ws();
        Json value = parse_value();
        skip_ws();
        if (pos_ != text_.size()) {
            throw std::runtime_error("error: malformed JSON");
        }
        return value;
    }

private:
    std::string text_;
    std::size_t pos_ = 0;

    [[noreturn]] void fail() const
    {
        throw std::runtime_error("error: malformed JSON");
    }

    void skip_ws()
    {
        while (pos_ < text_.size() &&
               std::isspace(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
    }

    bool consume(char c)
    {
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    char peek()
    {
        skip_ws();
        if (pos_ >= text_.size()) {
            fail();
        }
        return text_[pos_];
    }

    Json parse_value()
    {
        const char c = peek();
        if (c == '{') {
            return parse_object();
        }
        if (c == '[') {
            return parse_array();
        }
        if (c == '"') {
            return parse_string();
        }
        if (c == 't' || c == 'f') {
            return parse_bool();
        }
        if (c == 'n') {
            return parse_null();
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            return parse_number();
        }
        fail();
    }

    Json parse_object()
    {
        if (!consume('{')) {
            fail();
        }

        Json result;
        result.type = Json::Type::Object;

        skip_ws();
        if (consume('}')) {
            return result;
        }

        while (true) {
            skip_ws();
            if (peek() != '"') {
                fail();
            }
            Json key = parse_string();
            if (!consume(':')) {
                fail();
            }
            result.object.emplace(key.str, parse_value());
            if (consume('}')) {
                break;
            }
            if (!consume(',')) {
                fail();
            }
        }

        return result;
    }

    Json parse_array()
    {
        if (!consume('[')) {
            fail();
        }

        Json result;
        result.type = Json::Type::Array;

        skip_ws();
        if (consume(']')) {
            return result;
        }

        while (true) {
            result.array.push_back(parse_value());
            if (consume(']')) {
                break;
            }
            if (!consume(',')) {
                fail();
            }
        }

        return result;
    }

    Json parse_string()
    {
        skip_ws();
        if (pos_ >= text_.size() || text_[pos_] != '"') {
            fail();
        }
        ++pos_;

        Json result;
        result.type = Json::Type::String;

        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') {
                return result;
            }
            if (c == '\\') {
                if (pos_ >= text_.size()) {
                    fail();
                }
                const char esc = text_[pos_++];
                switch (esc) {
                    case '"':
                    case '\\':
                    case '/':
                        result.str.push_back(esc);
                        break;
                    case 'b':
                        result.str.push_back('\b');
                        break;
                    case 'f':
                        result.str.push_back('\f');
                        break;
                    case 'n':
                        result.str.push_back('\n');
                        break;
                    case 'r':
                        result.str.push_back('\r');
                        break;
                    case 't':
                        result.str.push_back('\t');
                        break;
                    case 'u':
                        if (pos_ + 4 > text_.size()) {
                            fail();
                        }
                        pos_ += 4;
                        result.str.push_back('?');
                        break;
                    default:
                        fail();
                }
            } else if (static_cast<unsigned char>(c) < 0x20) {
                fail();
            } else {
                result.str.push_back(c);
            }
        }

        fail();
    }

    Json parse_number()
    {
        skip_ws();
        const std::size_t start = pos_;

        if (pos_ < text_.size() && text_[pos_] == '-') {
            ++pos_;
        }
        if (pos_ >= text_.size() ||
            !std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
            fail();
        }
        if (text_[pos_] == '0') {
            ++pos_;
        } else {
            while (pos_ < text_.size() &&
                   std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                ++pos_;
            }
        }

        bool is_int = true;
        if (pos_ < text_.size() && text_[pos_] == '.') {
            is_int = false;
            ++pos_;
            if (pos_ >= text_.size() ||
                !std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                fail();
            }
            while (pos_ < text_.size() &&
                   std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                ++pos_;
            }
        }
        if (pos_ < text_.size() &&
            (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            is_int = false;
            ++pos_;
            if (pos_ < text_.size() &&
                (text_[pos_] == '+' || text_[pos_] == '-')) {
                ++pos_;
            }
            if (pos_ >= text_.size() ||
                !std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                fail();
            }
            while (pos_ < text_.size() &&
                   std::isdigit(static_cast<unsigned char>(text_[pos_]))) {
                ++pos_;
            }
        }

        Json result;
        result.type = Json::Type::Number;
        const std::string token = text_.substr(start, pos_ - start);
        try {
            if (is_int) {
                result.number_is_int = true;
                result.integer = std::stoll(token);
                result.real = static_cast<double>(result.integer);
            } else {
                result.number_is_int = false;
                result.real = std::stod(token);
            }
        } catch (const std::exception&) {
            fail();
        }
        return result;
    }

    Json parse_bool()
    {
        Json result;
        result.type = Json::Type::Bool;
        if (text_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            result.boolean = true;
            return result;
        }
        if (text_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            result.boolean = false;
            return result;
        }
        fail();
    }

    Json parse_null()
    {
        if (text_.compare(pos_, 4, "null") != 0) {
            fail();
        }
        pos_ += 4;
        Json result;
        result.type = Json::Type::Null;
        return result;
    }
};

}  // namespace

Config load_config(
    const std::string& config_path,
    bool require_load_balancer)
{
    std::ifstream in(config_path);
    if (!in) {
        throw std::runtime_error(
            "error: could not open config file '" + config_path + "'"
        );
    }

    std::ostringstream ss;
    ss << in.rdbuf();

    Parser parser(ss.str());
    const Json root = parser.parse();

    if (!root.is_object()) {
        throw std::runtime_error("error: missing required field 'server'");
    }
    if (!root.contains("server")) {
        throw std::runtime_error("error: missing required field 'server'");
    }
    if (!root.at("server").is_object()) {
        throw std::runtime_error("error: invalid type for field 'server'");
    }

    const Json& server = root.at("server");
    Config config;

    if (!server.contains("ip")) {
        throw std::runtime_error("error: missing required field 'server.ip'");
    }
    if (!server.at("ip").is_string()) {
        throw std::runtime_error("error: invalid type for field 'server.ip'");
    }
    config.server.ip = server.at("ip").str;

    if (!server.contains("port")) {
        throw std::runtime_error("error: missing required field 'server.port'");
    }
    if (!server.at("port").is_int()) {
        throw std::runtime_error(
            "error: invalid type for field 'server.port'"
        );
    }
    config.server.port = static_cast<int>(server.at("port").integer);

    if (!server.contains("server_threads")) {
        throw std::runtime_error(
            "error: missing required field 'server.server_threads'"
        );
    }
    if (!server.at("server_threads").is_int()) {
        throw std::runtime_error(
            "error: invalid type for field 'server.server_threads'"
        );
    }
    config.server.server_threads =
        static_cast<int>(server.at("server_threads").integer);

    if (!server.contains("client_threads")) {
        throw std::runtime_error(
            "error: missing required field 'server.client_threads'"
        );
    }
    if (!server.at("client_threads").is_int()) {
        throw std::runtime_error(
            "error: invalid type for field 'server.client_threads'"
        );
    }
    config.server.client_threads =
        static_cast<int>(server.at("client_threads").integer);

    if (config.server.port <= 0 || config.server.port > 65535) {
        throw std::runtime_error("error: invalid type for field 'server.port'");
    }
    if (config.server.server_threads <= 0) {
        throw std::runtime_error(
            "error: invalid type for field 'server.server_threads'"
        );
    }
    if (config.server.client_threads <= 0) {
        throw std::runtime_error(
            "error: invalid type for field 'server.client_threads'"
        );
    }

    if (!root.contains("load_balancer")) {
        if (require_load_balancer) {
            throw std::runtime_error(
                "error: missing required field 'load_balancer'"
            );
        }
        return config;
    }

    const Json& load_balancer = root.at("load_balancer");
    if (!load_balancer.is_object()) {
        throw std::runtime_error(
            "error: invalid type for field 'load_balancer'"
        );
    }

    if (!load_balancer.contains("ip")) {
        throw std::runtime_error(
            "error: missing required field 'load_balancer.ip'"
        );
    }
    if (!load_balancer.at("ip").is_string()) {
        throw std::runtime_error(
            "error: invalid type for field 'load_balancer.ip'"
        );
    }
    config.load_balancer.ip = load_balancer.at("ip").str;

    if (!load_balancer.contains("port")) {
        throw std::runtime_error(
            "error: missing required field 'load_balancer.port'"
        );
    }
    if (!load_balancer.at("port").is_int()) {
        throw std::runtime_error(
            "error: invalid type for field 'load_balancer.port'"
        );
    }
    config.load_balancer.port =
        static_cast<int>(load_balancer.at("port").integer);

    if (!load_balancer.contains("health_interval_ms")) {
        throw std::runtime_error(
            "error: missing required field 'load_balancer.health_interval_ms'"
        );
    }
    if (!load_balancer.at("health_interval_ms").is_int()) {
        throw std::runtime_error(
            "error: invalid type for field 'load_balancer.health_interval_ms'"
        );
    }
    config.load_balancer.health_interval_ms = static_cast<int>(
        load_balancer.at("health_interval_ms").integer
    );

    if (!load_balancer.contains("backends")) {
        throw std::runtime_error(
            "error: missing required field 'load_balancer.backends'"
        );
    }
    const Json& backends = load_balancer.at("backends");
    if (!backends.is_array() || backends.array.size() != 4) {
        throw std::runtime_error(
            "error: field 'load_balancer.backends' must contain exactly four entries"
        );
    }

    for (std::size_t i = 0; i < backends.array.size(); ++i) {
        const std::string prefix =
            "load_balancer.backends[" + std::to_string(i) + "]";
        const Json& backend = backends.array[i];
        if (!backend.is_object()) {
            throw std::runtime_error(
                "error: invalid type for field '" + prefix + "'"
            );
        }
        if (!backend.contains("ip")) {
            throw std::runtime_error(
                "error: missing required field '" + prefix + ".ip'"
            );
        }
        if (!backend.at("ip").is_string()) {
            throw std::runtime_error(
                "error: invalid type for field '" + prefix + ".ip'"
            );
        }
        if (!backend.contains("port")) {
            throw std::runtime_error(
                "error: missing required field '" + prefix + ".port'"
            );
        }
        if (!backend.at("port").is_int()) {
            throw std::runtime_error(
                "error: invalid type for field '" + prefix + ".port'"
            );
        }

        Config::BackendConfig parsed;
        parsed.ip = backend.at("ip").str;
        parsed.port = static_cast<int>(backend.at("port").integer);
        if (parsed.port <= 0 || parsed.port > 65535) {
            throw std::runtime_error(
                "error: invalid value for field '" + prefix + ".port'"
            );
        }
        config.load_balancer.backends.push_back(std::move(parsed));
    }

    if (config.load_balancer.port <= 0 ||
        config.load_balancer.port > 65535) {
        throw std::runtime_error(
            "error: invalid value for field 'load_balancer.port'"
        );
    }
    if (config.load_balancer.health_interval_ms <= 0) {
        throw std::runtime_error(
            "error: invalid value for field 'load_balancer.health_interval_ms'"
        );
    }
    config.has_load_balancer = true;

    return config;
}
