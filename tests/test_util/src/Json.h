#pragma once
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace probe {
class Json {
  public:
    using Object = std::map<std::string, Json>;
    using Array = std::vector<Json>;
    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool v) : value_(v) {}
    Json(const char *v) : value_(std::string(v ? v : "")) {}
    Json(std::string v) : value_(std::move(v)) {}
    Json(double v) {
        if (std::isfinite(v))
            value_ = v;
    }
    template <class T>
        requires(std::is_integral_v<T> && !std::is_same_v<T, bool>)
    Json(T v) {
        if constexpr (std::is_unsigned_v<T>) {
            if (v > static_cast<T>(INT64_MAX)) {
                value_ = std::to_string(v);
                return;
            }
        }
        value_ = static_cast<std::int64_t>(v);
    }
    Json(std::initializer_list<Object::value_type> v) : value_(Object(v)) {}
    static Json array() {
        Json j;
        j.value_ = Array{};
        return j;
    }
    Json &operator[](const std::string &key) {
        if (!std::holds_alternative<Object>(value_))
            value_ = Object{};
        return std::get<Object>(value_)[key];
    }
    void push(Json v) {
        if (!std::holds_alternative<Array>(value_))
            value_ = Array{};
        std::get<Array>(value_).push_back(std::move(v));
    }
    const Json *find(const std::string &key) const {
        const auto *object = std::get_if<Object>(&value_);
        if (!object)
            return nullptr;
        const auto it = object->find(key);
        return it == object->end() ? nullptr : &it->second;
    }
    std::string text() const {
        const auto *s = std::get_if<std::string>(&value_);
        return s ? *s : dump();
    }
    std::optional<double> number() const {
        if (const auto *d = std::get_if<double>(&value_))
            return *d;
        if (const auto *i = std::get_if<std::int64_t>(&value_))
            return static_cast<double>(*i);
        if (const auto *s = std::get_if<std::string>(&value_))
            try {
                std::size_t end{};
                const double d = std::stod(*s, &end);
                if (end == s->size() && std::isfinite(d))
                    return d;
            } catch (...) {
            }
        return {};
    }
    std::string dump() const {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        write(out);
        return out.str();
    }

  private:
    std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, Array, Object> value_{nullptr};
    static void string(std::ostream &out, const std::string &value) {
        out << '"';
        for (std::size_t i = 0; i < value.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(value[i]);
            if (c == '"' || c == '\\')
                out << '\\' << static_cast<char>(c);
            else if (c < 32) {
                constexpr char digits[] = "0123456789abcdef";
                out << "\\u00" << digits[c >> 4] << digits[c & 15];
            } else if (c >= 128) {
                std::size_t n = c >= 0xC2 && c <= 0xDF   ? 2
                                : c >= 0xE0 && c <= 0xEF ? 3
                                : c >= 0xF0 && c <= 0xF4 ? 4
                                                         : 0;
                bool valid = n && i + n <= value.size();
                for (std::size_t k = 1; valid && k < n; ++k) {
                    const auto next = static_cast<unsigned char>(value[i + k]);
                    valid = next >= 0x80 && next <= 0xBF;
                }
                if (valid && n >= 3) {
                    const auto next = static_cast<unsigned char>(value[i + 1]);
                    if ((c == 0xE0 && next < 0xA0) || (c == 0xED && next >= 0xA0) ||
                        (c == 0xF0 && next < 0x90) || (c == 0xF4 && next >= 0x90))
                        valid = false;
                }
                if (valid) {
                    out.write(value.data() + i, static_cast<std::streamsize>(n));
                    i += n - 1;
                } else {
                    constexpr char digits[] = "0123456789abcdef";
                    out << "\\u00" << digits[c >> 4] << digits[c & 15];
                }
            } else
                out << static_cast<char>(c);
        }
        out << '"';
    }
    void write(std::ostream &out) const {
        std::visit(
            [&](const auto &v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::nullptr_t>)
                    out << "null";
                else if constexpr (std::is_same_v<T, bool>)
                    out << (v ? "true" : "false");
                else if constexpr (std::is_arithmetic_v<T>)
                    out << std::setprecision(17) << v;
                else if constexpr (std::is_same_v<T, std::string>)
                    string(out, v);
                else {
                    out << (std::is_same_v<T, Array> ? '[' : '{');
                    bool first = true;
                    for (const auto &item : v) {
                        if (!first)
                            out << ',';
                        first = false;
                        if constexpr (std::is_same_v<T, Array>)
                            item.write(out);
                        else {
                            string(out, item.first);
                            out << ':';
                            item.second.write(out);
                        }
                    }
                    out << (std::is_same_v<T, Array> ? ']' : '}');
                }
            },
            value_);
    }
};
} // namespace probe
