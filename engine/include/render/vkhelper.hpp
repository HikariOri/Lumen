#pragma once

#include <string>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include <vulkan/vulkan.hpp>

namespace cake {
    struct Error {
        std::error_code type;
        vk::Result result = vk::Result::eSuccess;
        std::vector<std::string> detailedFailureReasons;
    };

    template <typename T>
    class Result {
    public:
        Result(const T &value) noexcept : data { value } {}
        Result(T &&value) noexcept : data { std::move(value) } {}

        Result(const Error &error) noexcept : data { error } {}
        Result(Error &&error) noexcept : data { std::move(error) } {}

        Result(std::error_code errorCode,
               vk::Result result = vk::Result::eSuccess) noexcept
            : data { Error { errorCode, result, {} } } {}

        Result(std::error_code errorCode,
               std::vector<std::string> const &detailedFailureReasons) noexcept
            : data { Error { errorCode, vk::Result::eSuccess,
                             detailedFailureReasons } } {}

        Result &operator=(const T &expect) noexcept {
            data = expect;
            return *this;
        }
        Result &operator=(T &&expect) noexcept {
            data = std::move(expect);
            return *this;
        }
        Result &operator=(const Error &error) noexcept {
            data = error;
            return *this;
        }
        Result &operator=(Error &&error) noexcept {
            data = std::move(error);
            return *this;
        }

        const T *operator->() const { return &std::get<T>(data); }
        T *operator->() { return &std::get<T>(data); }
        const T &operator*() const & { return std::get<T>(data); }
        T &operator*() & { return std::get<T>(data); }
        T operator*() && { return std::move(std::get<T>(data)); }
        const T &value() const & { return std::get<T>(data); }
        T &value() & { return std::get<T>(data); }
        T value() && { return std::move(std::get<T>(data)); }

        // std::error_code associated with the error
        std::error_code error() const { return std::get<Error>(data).type; }
        // optional VkResult that could of been produced due to the error
        VkResult vk_result() const { return std::get<Error>(data).vk_result; }
        // Returns the struct that holds the std::error_code and VkResult
        Error full_error() const { return std::get<Error>(data); }
        // Returns the detailed error list that contributed to the error. Example: Reasons why VkPhysicalDevices failed to be selected
        std::vector<std::string> const &detailedFailureReasons() const {
            return std::get<Error>(data).detailedFailureReasons;
        }

        // check if the result has an error that matches a specific error case
        template <typename E>
        bool matches_error(E error_enum_value) const {
            return !has_value() &&
                   static_cast<E>(std::get<Error>(data).type.value()) ==
                       error_enum_value;
        }

        bool has_value() const { return std::holds_alternative<T>(data); }
        explicit operator bool() const { return has_value(); }

    private:
        std::variant<T, Error> data;
    };

}; // namespace cake
