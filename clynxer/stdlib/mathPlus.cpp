// Lynxer `mathPlus` stdlib backend: statistics and vector helpers that the
// Python reference implemented with NumPy.
//
// Lists cross the ABI as tab-separated strings (the reference used the unit
// separator, which cannot be written as a Lynxer string literal). Tab is
// unambiguous because every element is a formatted number.

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

using RegisterFunction = int (*)(const char*, const char*, const char*);
using RegisterConstant = int (*)(const char*, std::int64_t);
using RegisterType = int (*)(const char*, const char*);

static const char kSeparator = '\t';

static const char* stable(std::string value) {
    thread_local std::string result;
    result = std::move(value);
    return result.c_str();
}

static std::string textOrEmpty(const char* text) {
    return text == nullptr ? std::string() : std::string(text);
}

static std::vector<double> parseValues(const std::string& text) {
    std::vector<double> values;
    if (text.empty()) {
        return values;
    }
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t separator = text.find(kSeparator, start);
        const std::string token = text.substr(
            start, separator == std::string::npos ? std::string::npos
                                                  : separator - start);
        if (!token.empty()) {
            values.push_back(std::strtod(token.c_str(), nullptr));
        }
        if (separator == std::string::npos) {
            break;
        }
        start = separator + 1;
    }
    return values;
}

static std::string formatValues(const std::vector<double>& values) {
    std::string output;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            output += kSeparator;
        }
        // Python's str(float(x)) always shows a decimal point.
        char buffer[64];
        const auto converted =
            std::to_chars(buffer, buffer + sizeof(buffer), values[index]);
        std::string text(buffer, converted.ptr);
        if (text.find_first_of(".eEni") == std::string::npos) {
            text += ".0";
        }
        output += text;
    }
    return output;
}

static double meanOf(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }
    double total = 0.0;
    for (const double value : values) {
        total += value;
    }
    return total / static_cast<double>(values.size());
}

extern "C" double mathPlus_median(const char* text) {
    std::vector<double> values = parseValues(textOrEmpty(text));
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2;
    if (values.size() % 2 == 1) {
        return values[middle];
    }
    return (values[middle - 1] + values[middle]) / 2.0;
}

extern "C" double mathPlus_std(const char* text) {
    const std::vector<double> values = parseValues(textOrEmpty(text));
    if (values.empty()) {
        return 0.0;
    }
    const double mean = meanOf(values);
    double total = 0.0;
    for (const double value : values) {
        total += (value - mean) * (value - mean);
    }
    return std::sqrt(total / static_cast<double>(values.size()));
}

extern "C" double mathPlus_variance(const char* text) {
    const std::vector<double> values = parseValues(textOrEmpty(text));
    if (values.empty()) {
        return 0.0;
    }
    const double mean = meanOf(values);
    double total = 0.0;
    for (const double value : values) {
        total += (value - mean) * (value - mean);
    }
    return total / static_cast<double>(values.size());
}

// NumPy's default linear interpolation between order statistics.
extern "C" double mathPlus_percentile(const char* text, double percent) {
    std::vector<double> values = parseValues(textOrEmpty(text));
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    if (values.size() == 1) {
        return values.front();
    }
    const double rank =
        (percent / 100.0) * static_cast<double>(values.size() - 1);
    const double lower = std::floor(rank);
    const double fraction = rank - lower;
    const std::size_t index = static_cast<std::size_t>(lower);
    if (index + 1 >= values.size()) {
        return values.back();
    }
    return values[index] + fraction * (values[index + 1] - values[index]);
}

extern "C" double mathPlus_corrcoef(const char* first, const char* second) {
    const std::vector<double> left = parseValues(textOrEmpty(first));
    const std::vector<double> right = parseValues(textOrEmpty(second));
    if (left.empty() || left.size() != right.size()) {
        return 0.0;
    }
    const double leftMean = meanOf(left);
    const double rightMean = meanOf(right);
    double covariance = 0.0;
    double leftVariance = 0.0;
    double rightVariance = 0.0;
    for (std::size_t index = 0; index < left.size(); ++index) {
        const double leftDelta = left[index] - leftMean;
        const double rightDelta = right[index] - rightMean;
        covariance += leftDelta * rightDelta;
        leftVariance += leftDelta * leftDelta;
        rightVariance += rightDelta * rightDelta;
    }
    const double denominator = std::sqrt(leftVariance * rightVariance);
    if (denominator == 0.0) {
        return 0.0;
    }
    return covariance / denominator;
}

extern "C" double mathPlus_dot(const char* first, const char* second) {
    const std::vector<double> left = parseValues(textOrEmpty(first));
    const std::vector<double> right = parseValues(textOrEmpty(second));
    if (left.size() != right.size()) {
        return 0.0;
    }
    double total = 0.0;
    for (std::size_t index = 0; index < left.size(); ++index) {
        total += left[index] * right[index];
    }
    return total;
}

extern "C" const char* mathPlus_linspace(double start, double stop,
                                         std::int64_t count) {
    if (count <= 0) {
        return stable("");
    }
    std::vector<double> values;
    if (count == 1) {
        values.push_back(start);
    } else {
        const double step =
            (stop - start) / static_cast<double>(count - 1);
        for (std::int64_t index = 0; index < count; ++index) {
            values.push_back(start + step * static_cast<double>(index));
        }
    }
    return stable(formatValues(values));
}

extern "C" const char* mathPlus_cumsum(const char* text) {
    const std::vector<double> values = parseValues(textOrEmpty(text));
    if (values.empty()) {
        return stable("");
    }
    std::vector<double> result;
    double running = 0.0;
    for (const double value : values) {
        running += value;
        result.push_back(running);
    }
    return stable(formatValues(result));
}

extern "C" const char* mathPlus_diff(const char* text) {
    const std::vector<double> values = parseValues(textOrEmpty(text));
    if (values.size() < 2) {
        return stable("");
    }
    std::vector<double> result;
    for (std::size_t index = 1; index < values.size(); ++index) {
        result.push_back(values[index] - values[index - 1]);
    }
    return stable(formatValues(result));
}

extern "C" const char* mathPlus_clip(const char* text, double low,
                                     double high) {
    const std::vector<double> values = parseValues(textOrEmpty(text));
    if (values.empty()) {
        return stable("");
    }
    std::vector<double> result;
    for (const double value : values) {
        result.push_back(value < low ? low : (value > high ? high : value));
    }
    return stable(formatValues(result));
}

extern "C" const char* mathPlus_normalize(const char* text) {
    const std::vector<double> values = parseValues(textOrEmpty(text));
    if (values.empty()) {
        return stable("");
    }
    double total = 0.0;
    for (const double value : values) {
        total += value * value;
    }
    const double magnitude = std::sqrt(total);
    if (magnitude == 0.0) {
        return stable(formatValues(values));
    }
    std::vector<double> result;
    for (const double value : values) {
        result.push_back(value / magnitude);
    }
    return stable(formatValues(result));
}

extern "C" int lynxer_module_init_v1(RegisterFunction function,
                                     RegisterConstant, RegisterType) {
    return function("median", "mathPlus_median", "cdecl:double(cstring)") &&
                   function("std", "mathPlus_std", "cdecl:double(cstring)") &&
                   function("variance", "mathPlus_variance",
                            "cdecl:double(cstring)") &&
                   function("percentile", "mathPlus_percentile",
                            "cdecl:double(cstring,double)") &&
                   function("corrcoef", "mathPlus_corrcoef",
                            "cdecl:double(cstring,cstring)") &&
                   function("dot", "mathPlus_dot",
                            "cdecl:double(cstring,cstring)") &&
                   function("linspace", "mathPlus_linspace",
                            "cdecl:cstring(double,double,int64)") &&
                   function("cumsum", "mathPlus_cumsum",
                            "cdecl:cstring(cstring)") &&
                   function("diff", "mathPlus_diff",
                            "cdecl:cstring(cstring)") &&
                   function("clip", "mathPlus_clip",
                            "cdecl:cstring(cstring,double,double)") &&
                   function("normalize", "mathPlus_normalize",
                            "cdecl:cstring(cstring)")
               ? 0
               : 1;
}
