#pragma once

#include <string>
#include <utility>

namespace hex {

    class UnlocalizedString {
    public:
        UnlocalizedString() = default;
        UnlocalizedString(const char* value) : m_value(value != nullptr ? value : "") {}
        UnlocalizedString(std::string value) : m_value(std::move(value)) {}

        [[nodiscard]] const std::string& get() const {
            return m_value;
        }

        [[nodiscard]] bool empty() const {
            return m_value.empty();
        }

        bool operator<(const UnlocalizedString& other) const {
            return m_value < other.m_value;
        }

        bool operator==(const UnlocalizedString& other) const {
            return m_value == other.m_value;
        }

    private:
        std::string m_value;
    };

    std::string Lang(const UnlocalizedString& value);

}
